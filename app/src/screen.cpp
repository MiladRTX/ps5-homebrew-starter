/*
 * PS5 Homebrew Starter - full-screen CPU drawing on the PS5's VideoOut, antialiased,
 * with the Inter and JetBrains Mono fonts. Based on ps5-native-app-boilerplate's demo
 * renderer, Copyright (C) 2026 BlackBearReloaded; changes for Breeze (MiladRTX): a
 * linear frame in RAM copied to the tiled scanout buffer in one pass,
 * antialiased shapes and text, and a scene drawn again only when it changes.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "screen.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifndef STARTER_PREVIEW
extern "C"
{
    std::size_t sceKernelGetDirectMemorySize();
    int sceKernelAllocateDirectMemory(std::int64_t search_start, std::int64_t search_end,
                                      std::size_t length, std::size_t alignment, int memory_type,
                                      std::int64_t *physical_address);
    int sceKernelMapDirectMemory(void **address, std::size_t length, int protection, int flags,
                                 std::int64_t physical_address, std::size_t alignment);
    int sceKernelSendNotificationRequest(std::uint32_t device, void *request, std::size_t size,
                                         int blocking);
    int sceKernelUsleep(std::uint32_t microseconds);
    int sceSystemServiceHideSplashScreen();
    int sceVideoOutOpen(std::int32_t user_id, std::int32_t bus_type, std::int32_t index,
                        const void *param);
    int sceVideoOutSetFlipRate(std::int32_t handle, std::int32_t rate);
    int sceVideoOutSubmitFlip(std::int32_t handle, std::int32_t buffer_index,
                              std::uint32_t flip_mode, std::int64_t flip_argument);
    int sceVideoOutWaitVblank(std::int32_t handle);
}
#endif

namespace starter::screen
{
namespace
{
constexpr std::size_t frame_bytes = 0x1000000;
[[maybe_unused]] constexpr std::size_t memory_bytes = frame_bytes * 2;
[[maybe_unused]] constexpr std::size_t memory_alignment = 0x200000;
// The scanout buffer is tiled in 128x128 blocks of 64 KB; 1080 rows use 9 rows of blocks.
constexpr int block = 128;
constexpr int blocks_x = width / block;
constexpr int blocks_y = (height + block - 1) / block;
[[maybe_unused]] constexpr std::size_t used_bytes =
    static_cast<std::size_t>(blocks_x) * blocks_y << 16;

std::uint32_t frame[height * width];    // the scene, row by row
std::uint8_t mask[height * width];      // coverage for one polyline at a time
int clip_x0 = 0, clip_y0 = 0, clip_x1 = width, clip_y1 = height;

inline float clamp01(float v) noexcept
{
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

// Opacity into 0..255: a fade that overshoots to -3 is invisible, not solid.
inline int clamp_alpha(int a) noexcept
{
    return a < 0 ? 0 : a > 255 ? 255 : a;
}

// d = c over d with opacity a (0..255), in the frame's sRGB values, as a browser blends.
inline void blend(std::uint32_t &d, std::uint32_t c, unsigned a) noexcept
{
    if (a >= 255)
    {
        d = c | 0xff000000U;
        return;
    }
    if (a == 0)
        return;
    const unsigned ia = 255 - a;
    std::uint32_t rb = (c & 0x00ff00ffU) * a + (d & 0x00ff00ffU) * ia + 0x00800080U;
    rb = ((rb + ((rb >> 8) & 0x00ff00ffU)) >> 8) & 0x00ff00ffU;
    std::uint32_t g = ((c >> 8) & 0xffU) * a + ((d >> 8) & 0xffU) * ia + 0x80U;
    g = ((g + (g >> 8)) >> 8) & 0xffU;
    d = 0xff000000U | rb | g << 8;
}

inline void put(int x, int y, std::uint32_t c, unsigned a) noexcept
{
    if (x >= clip_x0 && y >= clip_y0 && x < clip_x1 && y < clip_y1)
        blend(frame[y * width + x], c, a);
}

void span(int x0, int x1, int y, std::uint32_t c, unsigned a) noexcept
{
    if (y < clip_y0 || y >= clip_y1)
        return;
    if (x0 < clip_x0)
        x0 = clip_x0;
    if (x1 > clip_x1)
        x1 = clip_x1;
    std::uint32_t *p = frame + y * width;
    for (int x = x0; x < x1; ++x)
        blend(p[x], c, a);
}

// Coverage of a pixel by a rounded rectangle (x, y, w, h, radius r), antialiased.
float rrect_cover(float px, float py, float x, float y, float w, float h, float r) noexcept
{
    const float cx = px < x + r ? x + r : px > x + w - r ? x + w - r : px;
    const float cy = py < y + r ? y + r : py > y + h - r ? y + h - r : py;
    const float dx = px - cx, dy = py - cy;
    if (dx == 0 && dy == 0)
    {
        // Inside the straight part: only the outer edges are soft.
        const float edge = std::fmin(std::fmin(px - x, x + w - px), std::fmin(py - y, y + h - py));
        return clamp01(edge + 0.5f);
    }
    return clamp01(r - std::sqrt(dx * dx + dy * dy) + 0.5f);
}

// Pixel coverage of a segment of width thick, round caps.
inline float segment_cover(float px, float py, float x0, float y0, float x1, float y1,
                           float half) noexcept
{
    const float vx = x1 - x0, vy = y1 - y0;
    const float len2 = vx * vx + vy * vy;
    float t = len2 > 0 ? ((px - x0) * vx + (py - y0) * vy) / len2 : 0;
    t = clamp01(t);
    const float dx = px - (x0 + t * vx), dy = py - (y0 + t * vy);
    return clamp01(half + 0.5f - std::sqrt(dx * dx + dy * dy));
}

struct Box
{
    int x0 = width, y0 = height, x1 = 0, y1 = 0;
    void add(float x, float y, float pad) noexcept
    {
        // Far off the screen counts as just off it (and NaN is skipped), so
        // the conversions to int below always fit.
        if (!(x == x) || !(y == y))
            return;
        x = x < -2 * pad - 1 ? -2 * pad - 1 : x > width + 2 * pad + 1 ? width + 2 * pad + 1 : x;
        y = y < -2 * pad - 1 ? -2 * pad - 1 : y > height + 2 * pad + 1 ? height + 2 * pad + 1 : y;
        const int a = static_cast<int>(std::floor(x - pad)), b = static_cast<int>(std::floor(y - pad));
        const int c = static_cast<int>(std::ceil(x + pad)), d = static_cast<int>(std::ceil(y + pad));
        x0 = a < x0 ? a : x0;
        y0 = b < y0 ? b : y0;
        x1 = c > x1 ? c : x1;
        y1 = d > y1 ? d : y1;
    }
    void clip() noexcept
    {
        x0 = x0 < 0 ? 0 : x0;
        y0 = y0 < 0 ? 0 : y0;
        x1 = x1 > width ? width : x1;
        y1 = y1 > height ? height : y1;
    }
};

// One segment into the mask, keeping the larger coverage where they overlap.
void mask_segment(float x0, float y0, float x1, float y1, float half) noexcept
{
    Box b;
    b.add(x0, y0, half + 1);
    b.add(x1, y1, half + 1);
    b.clip();
    for (int y = b.y0; y < b.y1; ++y)
        for (int x = b.x0; x < b.x1; ++x)
        {
            const float c = segment_cover(x + 0.5f, y + 0.5f, x0, y0, x1, y1, half);
            const auto v = static_cast<std::uint8_t>(c * 255 + 0.5f);
            std::uint8_t &m = mask[y * width + x];
            if (v > m)
                m = v;
        }
}

#ifndef STARTER_PREVIEW
constexpr int memory_type_wc_garlic = 3;
constexpr int map_protection = 0x33;
constexpr std::uint64_t pixel_format_rgba8_srgb = UINT64_C(0x8000000022000000);

struct VideoBuffer
{
    void *data;
    void *metadata;
    void *reserved0;
    void *reserved1;
};

struct VideoAttribute
{
    std::uint8_t reserved[80];
};

extern "C" void sceVideoOutSetBufferAttribute2(VideoAttribute *attribute,
                                               std::uint64_t pixel_format,
                                               std::uint32_t tiling_mode, std::uint32_t width,
                                               std::uint32_t height, std::uint64_t option,
                                               std::uint32_t dcc_control,
                                               std::uint64_t dcc_clear_color);
extern "C" int sceVideoOutRegisterBuffers2(std::int32_t handle, std::int32_t set_index,
                                           std::int32_t buffer_index_start, VideoBuffer *buffers,
                                           std::int32_t buffer_count, VideoAttribute *attribute,
                                           std::int32_t category, void *option);

struct NotificationRequest
{
    std::uint8_t reserved[45];
    char message[3075];
};
NotificationRequest notification{};

[[noreturn]] void halt(std::string_view message) noexcept
{
    notify(message);
    for (;;)
        (void)sceKernelUsleep(1000000);
}

[[nodiscard]] constexpr std::size_t tiled_offset(unsigned x, unsigned y) noexcept
{
    const std::uint32_t offset = ((y << 4) & 0x70U) ^ ((y << 5) & 0xf00U) ^ ((y << 9) & 0x1000U) ^
                                 ((y << 8) & 0x4000U) ^ ((x << 2) & 0xcU) ^ ((x << 5) & 0x380U) ^
                                 ((x << 4) & 0x400U) ^ ((x << 6) & 0x800U) ^ ((x << 9) & 0xa000U);
    const std::uint32_t block_index = (y >> 7) * blocks_x + (x >> 7);
    return (static_cast<std::size_t>(block_index) << 16) + offset;
}

// The scene into the tiled scanout buffer, in 4x8-pixel groups so the writes
// to write-combined memory run in address order.
void copy_to(void *scanout) noexcept
{
    auto *bytes = static_cast<std::uint8_t *>(scanout);
    for (int by = 0; by < blocks_y; ++by)
        for (int bx = 0; bx < blocks_x; ++bx)
            for (int y0 = 0; y0 < block; y0 += 8)
            {
                const int top = by * block + y0;
                if (top >= height)
                    break;
                for (int x0 = 0; x0 < block; x0 += 4)
                {
                    const int left = bx * block + x0;
                    for (int yy = 0; yy < 8 && top + yy < height; ++yy)
                    {
                        const std::uint32_t *src = frame + (top + yy) * width + left;
                        for (int xx = 0; xx < 4; ++xx)
                            *reinterpret_cast<std::uint32_t *>(
                                bytes + tiled_offset(static_cast<unsigned>(left + xx),
                                                     static_cast<unsigned>(top + yy))) = src[xx];
                    }
                }
            }
}

void flush_range(void *address, std::size_t length) noexcept
{
    auto *at = static_cast<std::uint8_t *>(address);
    const auto *end = at + length;
    for (; at < end; at += 64)
        __asm__ volatile("clflush (%0)" : : "r"(at) : "memory");
    __asm__ volatile("mfence" ::: "memory");
}
#endif
} // namespace

std::uint64_t now_ms() noexcept
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000U +
           static_cast<std::uint64_t>(ts.tv_nsec) / 1000000U;
}

void Canvas::clip(int x, int y, int w, int h) noexcept
{
    clip_x0 = x < 0 ? 0 : x;
    clip_y0 = y < 0 ? 0 : y;
    clip_x1 = x + w > width ? width : x + w;
    clip_y1 = y + h > height ? height : y + h;
}

void Canvas::unclip() noexcept
{
    clip_x0 = clip_y0 = 0;
    clip_x1 = width;
    clip_y1 = height;
}

void Canvas::clear(Color c) noexcept
{
    const auto v = static_cast<std::uint32_t>(c) | 0xff000000U;
    for (std::uint32_t &p : frame)
        p = v;
}

void Canvas::rect(int x, int y, int w, int h, Color c, int alpha) noexcept
{
    alpha = clamp_alpha(alpha);
    const int top = y > clip_y0 ? y : clip_y0, bottom = y + h < clip_y1 ? y + h : clip_y1;
    for (int row = top; row < bottom; ++row)
        span(x, x + w, row, static_cast<std::uint32_t>(c), static_cast<unsigned>(alpha));
}

void Canvas::round_rect(int x, int y, int w, int h, int r, Color c, int alpha) noexcept
{
    alpha = clamp_alpha(alpha);
    const auto col = static_cast<std::uint32_t>(c);
    if (r * 2 > w)
        r = w / 2;
    if (r * 2 > h)
        r = h / 2;
    for (int row = y; row < y + h; ++row)
    {
        const bool corner_row = row < y + r || row >= y + h - r;
        if (!corner_row)
        {
            span(x, x + w, row, col, static_cast<unsigned>(alpha));
            continue;
        }
        for (int col_x = x; col_x < x + w; ++col_x)
        {
            const bool corner = col_x < x + r || col_x >= x + w - r;
            const float cov = corner ? rrect_cover(col_x + 0.5f, row + 0.5f, static_cast<float>(x),
                                                   static_cast<float>(y), static_cast<float>(w),
                                                   static_cast<float>(h), static_cast<float>(r))
                                     : 1.0f;
            put(col_x, row, col, static_cast<unsigned>(cov * alpha + 0.5f));
        }
    }
}

void Canvas::round_frame(int x, int y, int w, int h, int r, int thick, Color c, int alpha) noexcept
{
    alpha = clamp_alpha(alpha);
    // The same corners as round_rect() with the same arguments, a "pill"
    // radius like 999 included.
    if (r * 2 > w)
        r = w / 2;
    if (r * 2 > h)
        r = h / 2;
    const auto col = static_cast<std::uint32_t>(c);
    const float fx = static_cast<float>(x), fy = static_cast<float>(y);
    const float fw = static_cast<float>(w), fh = static_cast<float>(h);
    const float ir = r > thick ? static_cast<float>(r - thick) : 0.0f;
    const int band = r > thick ? r : thick;
    for (int row = y; row < y + h; ++row)
    {
        const bool edge_row = row < y + band || row >= y + h - band;
        for (int col_x = x; col_x < x + w; ++col_x)
        {
            if (!edge_row && col_x >= x + band && col_x < x + w - band)
            {
                col_x = x + w - band - 1; // skip the inside
                continue;
            }
            const float px = col_x + 0.5f, py = row + 0.5f;
            const float outer = rrect_cover(px, py, fx, fy, fw, fh, static_cast<float>(r));
            const float inner = rrect_cover(px, py, fx + thick, fy + thick, fw - 2 * thick,
                                            fh - 2 * thick, ir);
            const float cov = outer - inner;
            if (cov > 0)
                put(col_x, row, col, static_cast<unsigned>(cov * alpha + 0.5f));
        }
    }
}

void Canvas::circle(float cx, float cy, float r, Color c, int alpha) noexcept
{
    alpha = clamp_alpha(alpha);
    const auto col = static_cast<std::uint32_t>(c);
    Box b;
    b.add(cx, cy, r + 1);
    b.clip();
    for (int y = b.y0; y < b.y1; ++y)
        for (int x = b.x0; x < b.x1; ++x)
        {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            const float cov = clamp01(r - std::sqrt(dx * dx + dy * dy) + 0.5f);
            if (cov > 0)
                put(x, y, col, static_cast<unsigned>(cov * alpha + 0.5f));
        }
}

void Canvas::ring(float cx, float cy, float r, float thick, Color c, int alpha) noexcept
{
    alpha = clamp_alpha(alpha);
    const auto col = static_cast<std::uint32_t>(c);
    Box b;
    b.add(cx, cy, r + thick / 2 + 1);
    b.clip();
    for (int y = b.y0; y < b.y1; ++y)
        for (int x = b.x0; x < b.x1; ++x)
        {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            const float cov = clamp01(thick / 2 + 0.5f - std::fabs(std::sqrt(dx * dx + dy * dy) - r));
            if (cov > 0)
                put(x, y, col, static_cast<unsigned>(cov * alpha + 0.5f));
        }
}

void Canvas::polyline(const float *xy, int n, float thick, Color c, int alpha, float dash,
                      float gap) noexcept
{
    if (n < 1)
        return;
    alpha = clamp_alpha(alpha);
    gap = gap < 0 ? 0 : gap; // a negative gap would never finish the walk below
    const float half = thick / 2;
    Box b;
    for (int i = 0; i < n; ++i)
        b.add(xy[2 * i], xy[2 * i + 1], half + 1);
    b.clip();
    if (b.x0 >= b.x1 || b.y0 >= b.y1)
        return;
    for (int y = b.y0; y < b.y1; ++y)
        for (int x = b.x0; x < b.x1; ++x)
            mask[y * width + x] = 0;

    if (dash <= 0)
    {
        for (int i = 0; i + 1 < n; ++i)
            mask_segment(xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], half);
        if (n == 1)
            mask_segment(xy[0], xy[1], xy[0], xy[1], half);
    }
    else
    {
        // Walk the line, drawing dash-long pieces with gap-long breaks.
        float left = dash; // of the current dash or gap
        bool on = true;
        for (int i = 0; i + 1 < n; ++i)
        {
            float x0 = xy[2 * i], y0 = xy[2 * i + 1];
            const float x1 = xy[2 * i + 2], y1 = xy[2 * i + 3];
            float len = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
            while (len > 0)
            {
                const float step = left < len ? left : len;
                const float t = step / len;
                const float xm = x0 + (x1 - x0) * t, ym = y0 + (y1 - y0) * t;
                if (on)
                    mask_segment(x0, y0, xm, ym, half);
                x0 = xm;
                y0 = ym;
                len -= step;
                left -= step;
                if (left <= 0)
                {
                    on = !on;
                    left = on ? dash : gap;
                }
            }
        }
    }

    // Onto the frame through put(), which keeps it inside clip().
    const auto col = static_cast<std::uint32_t>(c);
    for (int y = b.y0; y < b.y1; ++y)
        for (int x = b.x0; x < b.x1; ++x)
        {
            const unsigned m = mask[y * width + x];
            if (m)
                put(x, y, col, (m * static_cast<unsigned>(alpha) + 127) / 255);
        }
}

void Canvas::fill_under(const float *xy, int n, int floor_y, Color c, int alpha_top) noexcept
{
    if (n < 2)
        return;
    alpha_top = clamp_alpha(alpha_top);
    // Points that aren't numbers (a graph of all-equal values divides 0 by
    // 0) are skipped; the loops stay inside the clip box however far off the
    // points are.
    const auto finite = [](float v) { return std::isfinite(v); };
    float top = static_cast<float>(floor_y);
    for (int i = 0; i < n; ++i)
        if (finite(xy[2 * i + 1]))
            top = xy[2 * i + 1] < top ? xy[2 * i + 1] : top;
    const float span_h = floor_y - top > 1 ? floor_y - top : 1;
    const auto col = static_cast<std::uint32_t>(c);
    const int y_end = floor_y < clip_y1 ? floor_y : clip_y1;
    for (int i = 0; i + 1 < n; ++i)
    {
        const float x0 = xy[2 * i], y0 = xy[2 * i + 1], x1 = xy[2 * i + 2], y1 = xy[2 * i + 3];
        if (!finite(x0) || !finite(y0) || !finite(x1) || !finite(y1))
            continue;
        const float xa = std::fmax(std::ceil(x0 - 0.5f), static_cast<float>(clip_x0));
        const float xb = std::fmin(x1 + (i + 2 == n ? 0.5f : 0) - 0.5f, static_cast<float>(clip_x1));
        for (int x = static_cast<int>(xa); static_cast<float>(x) < xb; ++x)
        {
            const float t = x1 > x0 ? (x + 0.5f - x0) / (x1 - x0) : 0;
            const float yc = y0 + (y1 - y0) * clamp01(t);
            for (int y = static_cast<int>(std::fmax(std::floor(yc), static_cast<float>(clip_y0))); y < y_end; ++y)
            {
                const float cover = y + 1 > yc ? clamp01(y + 1 - yc) : 1.0f; // soft top edge
                const float fade = 1.0f - (y + 0.5f - top) / span_h;
                put(x, y, col, static_cast<unsigned>(alpha_top * clamp01(fade) * cover + 0.5f));
            }
        }
    }
}

void Canvas::dashed_h(int x0, int x1, int y, int thick, int dash, int gap, Color c,
                      int alpha) noexcept
{
    if (dash <= 0 || gap < 0) // no dashes to make: a plain line
    {
        rect(x0, y, x1 - x0, thick, c, alpha);
        return;
    }
    for (int x = x0; x < x1; x += dash + gap)
        rect(x, y, x + dash < x1 ? dash : x1 - x, thick, c, alpha);
}

void Canvas::dashed_v(int x, int y0, int y1, int thick, int dash, int gap, Color c,
                      int alpha) noexcept
{
    if (dash <= 0 || gap < 0)
    {
        rect(x, y0, thick, y1 - y0, c, alpha);
        return;
    }
    for (int y = y0; y < y1; y += dash + gap)
        rect(x, y, thick, y + dash < y1 ? dash : y1 - y, c, alpha);
}

void Canvas::image(int x, int y, int w, int h, const image::Image &picture, int alpha) noexcept
{
    alpha = alpha > 255 ? 255 : alpha;
    if (!picture.loaded() || w <= 0 || h <= 0 || alpha <= 0)
        return;
    const float sx = static_cast<float>(picture.w) / static_cast<float>(w);
    const float sy = static_cast<float>(picture.h) / static_cast<float>(h);
    // Which source pixels make screen pixel i along one side, and how much
    // each counts: shrunk, every pixel it covers, all the same; enlarged, the
    // two nearest, blended by distance. Each side picks for itself, so a
    // picture squeezed on one side only is still averaged on that side.
    struct Taps
    {
        int at[64];
        float weight[64];
        int n = 0;
        float total = 0;
    };
    const auto taps = [](int i, float s, int size, Taps &t) {
        t.n = 0;
        t.total = 0;
        if (s >= 1)
        {
            int p0 = static_cast<int>(static_cast<float>(i) * s);
            int p1 = static_cast<int>(static_cast<float>(i + 1) * s);
            p0 = p0 >= size ? size - 1 : p0;
            p1 = p1 <= p0 ? p0 + 1 : p1 > size ? size : p1;
            p1 = p1 - p0 > 64 ? p0 + 64 : p1; // more than 64 to 1: a sample of them
            for (int p = p0; p < p1; ++p)
            {
                t.at[t.n] = p;
                t.weight[t.n++] = 1;
            }
            t.total = static_cast<float>(t.n);
            return;
        }
        float u = (static_cast<float>(i) + 0.5f) * s - 0.5f;
        u = u < 0 ? 0 : u;
        const int p = static_cast<int>(u);
        const float f = u - static_cast<float>(p);
        t.at[0] = p;
        t.weight[0] = 1 - f;
        t.at[1] = p + 1 < size ? p + 1 : p;
        t.weight[1] = f;
        t.n = 2;
        t.total = 1;
    };
    Taps across, down;
    for (int j = 0; j < h; ++j)
    {
        const int dy = y + j;
        if (dy < clip_y0 || dy >= clip_y1)
            continue;
        taps(j, sy, picture.h, down);
        for (int i = 0; i < w; ++i)
        {
            const int dx = x + i;
            if (dx < clip_x0 || dx >= clip_x1)
                continue;
            taps(i, sx, picture.w, across);
            // Colours are summed weighted by their alpha ("premultiplied"), so
            // a transparent pixel's colour doesn't bleed into its neighbours.
            float sum[4] = {0, 0, 0, 0};
            for (int b = 0; b < down.n; ++b)
            {
                const std::uint32_t *row = picture.pixels + down.at[b] * picture.w;
                for (int a = 0; a < across.n; ++a)
                {
                    const std::uint32_t p = row[across.at[a]];
                    const float weight = static_cast<float>(p >> 24) * across.weight[a] * down.weight[b];
                    sum[0] += static_cast<float>(p & 255) * weight;
                    sum[1] += static_cast<float>((p >> 8) & 255) * weight;
                    sum[2] += static_cast<float>((p >> 16) & 255) * weight;
                    sum[3] += weight;
                }
            }
            const float total = across.total * down.total;
            if (sum[3] <= 0)
                continue; // fully transparent here
            const auto channel = [&](int k) {
                const float c = sum[k] / sum[3];
                return static_cast<std::uint32_t>(c > 255 ? 255 : c);
            };
            const std::uint32_t c = channel(0) | channel(1) << 8 | channel(2) << 16;
            const float coverage = sum[3] / total; // 0..255
            put(dx, dy, c, static_cast<unsigned>(coverage * static_cast<float>(alpha) / 255.0f + 0.5f));
        }
    }
}

void Canvas::icon(int x, int y, int w, int h, const std::uint8_t *cover, Color c, int alpha) noexcept
{
    alpha = clamp_alpha(alpha);
    const auto col = static_cast<std::uint32_t>(c);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
            if (const unsigned m = cover[j * w + i])
                put(x + i, y + j, col, (m * static_cast<unsigned>(alpha) + 127) / 255);
}

float Canvas::text(float x, float y, std::string_view s, font::Face face, int px, Color c,
                   int alpha, float tracking) noexcept
{
    alpha = clamp_alpha(alpha);
    const auto col = static_cast<std::uint32_t>(c);
    const float start = x;
    std::uint32_t prev = 0;
    const int len = static_cast<int>(s.size());
    for (int at = 0; at < len;)
    {
        const std::uint32_t cp = font::next(s.data(), len, at);
        const font::Glyph *g = font::glyph(face, px, cp);
        if (!g)
            break;
        if (prev) // between two letters: kerning, and the tracking
            x += font::kern(face, px, prev, cp) + tracking * px;
        if (g->bits)
        {
            const int gx = static_cast<int>(std::floor(x + 0.5f)) + g->xoff;
            const int gy = static_cast<int>(std::floor(y + 0.5f)) + g->yoff;
            for (int row = 0; row < g->h; ++row)
            {
                const int py = gy + row;
                if (py < 0 || py >= height)
                    continue;
                const std::uint8_t *src = g->bits + row * g->w;
                for (int col_x = 0; col_x < g->w; ++col_x)
                    if (src[col_x])
                        put(gx + col_x, py, col, (src[col_x] * static_cast<unsigned>(alpha) + 127) / 255);
            }
        }
        x += g->advance;
        prev = cp;
    }
    return x - start;
}

// Measures exactly as text() draws.
float Canvas::text_width(std::string_view s, font::Face face, int px, float tracking) noexcept
{
    float w = 0;
    std::uint32_t prev = 0;
    const int len = static_cast<int>(s.size());
    for (int at = 0; at < len;)
    {
        const std::uint32_t cp = font::next(s.data(), len, at);
        const font::Glyph *g = font::glyph(face, px, cp);
        if (!g)
            break;
        if (prev)
            w += font::kern(face, px, prev, cp) + tracking * px;
        w += g->advance;
        prev = cp;
    }
    return w;
}

#ifdef STARTER_PREVIEW
const std::uint32_t *pixels() noexcept
{
    return frame;
}

bool write_ppm(const char *path) noexcept
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    fprintf(f, "P6 %d %d 255\n", width, height);
    for (const std::uint32_t p : frame)
    {
        const unsigned char rgb[3] = {static_cast<unsigned char>(p), static_cast<unsigned char>(p >> 8),
                                      static_cast<unsigned char>(p >> 16)};
        fwrite(rgb, 1, 3, f);
    }
    return fclose(f) == 0;
}

bool preview(Draw draw, const char *ppm_path) noexcept
{
    Canvas canvas;
    draw(canvas);
    return write_ppm(ppm_path);
}

#ifndef STARTER_SIM // the simulator's run() and notify() are in app/sim/screen_sim.cpp
void notify(std::string_view message) noexcept
{
    printf("notify: %.*s\n", static_cast<int>(message.size()), message.data());
}

// The preview takes pictures: it has no frame loop to run.
[[noreturn]] void run(Update, Draw) noexcept
{
    fputs("screen::run() isn't in the PC preview: app/sim runs the app live\n", stderr);
    abort();
}
#endif
#else
bool preview(Draw, const char *) noexcept
{
    return false; // pictures are for the PC builds
}

void notify(std::string_view message) noexcept
{
    const std::size_t count = message.size() < sizeof notification.message - 1
                                  ? message.size()
                                  : sizeof notification.message - 1;
    for (std::size_t i = 0; i < count; ++i)
        notification.message[i] = message[i];
    notification.message[count] = '\0';
    (void)sceKernelSendNotificationRequest(0, &notification, sizeof(notification), 0);
}

[[noreturn]] void run(Update update, Draw draw) noexcept
{
    (void)sceSystemServiceHideSplashScreen();
    const int video = sceVideoOutOpen(0xff, 0, 0, nullptr);
    if (video < 0)
        halt("Hello PS5: sceVideoOutOpen failed");

    const std::size_t pool_size = sceKernelGetDirectMemorySize();
    if (pool_size < memory_bytes)
        halt("Hello PS5: not enough direct memory");

    std::int64_t physical_address = 0;
    int result =
        sceKernelAllocateDirectMemory(0, static_cast<std::int64_t>(pool_size), memory_bytes,
                                      memory_alignment, memory_type_wc_garlic, &physical_address);
    if (result < 0)
        halt("Hello PS5: direct-memory allocation failed");

    void *mapped = nullptr;
    result = sceKernelMapDirectMemory(&mapped, memory_bytes, map_protection, 0, physical_address,
                                      memory_alignment);
    if (result < 0)
        halt("Hello PS5: direct-memory mapping failed");

    std::array<VideoBuffer, 2> buffers{{
        {mapped, nullptr, nullptr, nullptr},
        {static_cast<std::uint8_t *>(mapped) + frame_bytes, nullptr, nullptr, nullptr},
    }};
    Canvas canvas;
    (void)update();
    draw(canvas);
    copy_to(buffers[0].data);
    flush_range(buffers[0].data, used_bytes);

    VideoAttribute attribute{};
    (void)sceVideoOutSetFlipRate(video, 0);
    sceVideoOutSetBufferAttribute2(&attribute, pixel_format_rgba8_srgb, 0, width, height, 0, 0,
                                   0);
    result = sceVideoOutRegisterBuffers2(video, 0, 0, buffers.data(),
                                         static_cast<std::int32_t>(buffers.size()), &attribute, 0,
                                         nullptr);
    if (result < 0)
        halt("Hello PS5: buffer registration failed");
    if (sceVideoOutSubmitFlip(video, 0, 1, 1) < 0)
        halt("Hello PS5: first flip failed");
    (void)sceVideoOutWaitVblank(video);

    // Returning from main crashes this launch context, so this never ends.
    std::size_t shown = 0;
    for (std::int64_t flip = 2;; ++flip)
    {
        (void)sceVideoOutWaitVblank(video);
        if (!update())
            continue;
        const std::size_t back = shown ^ 1U;
        draw(canvas);
        copy_to(buffers[back].data);
        flush_range(buffers[back].data, used_bytes);
        if (sceVideoOutSubmitFlip(video, static_cast<std::int32_t>(back), 1, flip) >= 0)
            shown = back;
    }
}
#endif
} // namespace starter::screen
