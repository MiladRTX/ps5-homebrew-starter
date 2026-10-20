/*
 * PS5 Homebrew Starter - the Inter and JetBrains Mono fonts, rasterised with
 * stb_truetype and cached per face, size and character.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "font.hpp"

#include "stb_truetype.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace starter::font
{
namespace
{
const char *const files[face_count] = {
    "Inter-Regular.ttf",        "Inter-Medium.ttf",          "Inter-SemiBold.ttf",
    "Inter-Bold.ttf",           "JetBrainsMono-Medium.ttf",  "JetBrainsMono-SemiBold.ttf",
};

struct Loaded
{
    bool ok;
    stbtt_fontinfo info;
    int cap; // capital height in font units
};
Loaded g_faces[face_count];

struct Entry
{
    bool used;
    std::uint8_t face;
    std::uint16_t px;
    std::uint32_t cp;
    Glyph g;
};
constexpr unsigned table_size = 8192; // a power of two
Entry g_table[table_size];
std::uint8_t g_pool[8 << 20];
size_t g_pool_used;

unsigned char *read_file(const char *path) noexcept
{
    const int fd = open(path, O_RDONLY);
    if (fd < 0)
        return nullptr;
    const off_t size = lseek(fd, 0, SEEK_END);
    if (size <= 0 || lseek(fd, 0, SEEK_SET) != 0)
    {
        (void)close(fd);
        return nullptr;
    }
    auto *data = static_cast<unsigned char *>(malloc(static_cast<size_t>(size)));
    size_t got = 0;
    while (data && got < static_cast<size_t>(size))
    {
        const ssize_t n = read(fd, data + got, static_cast<size_t>(size) - got);
        if (n <= 0)
            break;
        got += static_cast<size_t>(n);
    }
    (void)close(fd);
    if (data && got != static_cast<size_t>(size))
    {
        free(data);
        return nullptr;
    }
    return data; // kept for the app's lifetime: stb_truetype reads it in place
}

float scale(Face face, int px) noexcept
{
    return stbtt_ScaleForMappingEmToPixels(&g_faces[face].info, static_cast<float>(px));
}
} // namespace

bool load(const char *dir) noexcept
{
    bool all = true;
    for (int f = 0; f < face_count; ++f)
    {
        char path[256];
        const size_t n = strlen(dir);
        if (n + strlen(files[f]) + 1 > sizeof path)
            return false;
        memcpy(path, dir, n);
        strcpy(path + n, files[f]);
        unsigned char *data = read_file(path);
        Loaded &l = g_faces[f];
        l.ok = data && stbtt_InitFont(&l.info, data, stbtt_GetFontOffsetForIndex(data, 0)) != 0;
        if (l.ok)
        {
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            l.cap = stbtt_GetCodepointBox(&l.info, 'H', &x0, &y0, &x1, &y1) ? y1 : 0;
        }
        all = all && l.ok;
    }
    return all;
}

const Glyph *glyph(Face face, int px, std::uint32_t cp) noexcept
{
    if (face < 0 || face >= face_count || !g_faces[face].ok || px <= 0 || px > max_px)
        return nullptr;
    unsigned h = (cp * 2654435761U) ^ (static_cast<unsigned>(px) * 40503U) ^
                 (static_cast<unsigned>(face) * 0x9e3779b9U);
    for (int pass = 0; pass < 2; ++pass)
    {
        for (unsigned probe = 0; probe < table_size; ++probe)
        {
            Entry &e = g_table[(h + probe) & (table_size - 1)];
            if (e.used && e.cp == cp && e.px == px && e.face == face)
                return &e.g;
            if (e.used)
                continue;

            const stbtt_fontinfo &info = g_faces[face].info;
            const float s = scale(face, px);
            int adv = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(&info, static_cast<int>(cp), &adv, &lsb);
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            stbtt_GetCodepointBitmapBox(&info, static_cast<int>(cp), s, s, &x0, &y0, &x1, &y1);
            Glyph g{x1 - x0, y1 - y0, x0, y0, adv * s, nullptr};
            const size_t bytes = static_cast<size_t>(g.w > 0 ? g.w : 0) * (g.h > 0 ? g.h : 0);
            if (bytes > 0 && g_pool_used + bytes > sizeof g_pool && pass == 0 && g_pool_used > 0)
                break; // the pool is full: start again with an empty cache
            if (bytes > 0 && g_pool_used + bytes <= sizeof g_pool)
            {
                std::uint8_t *bits = g_pool + g_pool_used;
                stbtt_MakeCodepointBitmap(&info, bits, g.w, g.h, g.w, s, s, static_cast<int>(cp));
                g_pool_used += bytes;
                g.bits = bits;
            }
            e = Entry{true, static_cast<std::uint8_t>(face), static_cast<std::uint16_t>(px), cp, g};
            return &e.g;
        }
        // Full (the table, or the pool): forget every glyph and make them
        // again as they're drawn. No one keeps a Glyph across calls, so this
        // is safe; it costs a few slow frames, once in a long while.
        memset(g_table, 0, sizeof g_table);
        g_pool_used = 0;
    }
    return nullptr;
}

float kern(Face face, int px, std::uint32_t left, std::uint32_t right) noexcept
{
    if (!g_faces[face].ok)
        return 0;
    return stbtt_GetCodepointKernAdvance(&g_faces[face].info, static_cast<int>(left),
                                         static_cast<int>(right)) *
           scale(face, px);
}

float cap_height(Face face, int px) noexcept
{
    return g_faces[face].ok ? g_faces[face].cap * scale(face, px) : px * 0.7f;
}

std::uint32_t next(const char *s, int len, int &at) noexcept
{
    constexpr std::uint32_t unknown = 0xfffd; // the "replacement character"
    const auto b = static_cast<unsigned char>(s[at++]);
    if (b < 0x80)
        return b;
    // A lead byte: 1 to 3 more bytes follow. Anything else here (a stray
    // continuation byte, text that isn't UTF-8) is one unknown character.
    if (b < 0xc2 || b > 0xf4)
        return unknown;
    const int extra = b >= 0xf0 ? 3 : b >= 0xe0 ? 2 : 1;
    std::uint32_t cp = b & (0x3fU >> extra);
    for (int i = 0; i < extra; ++i)
    {
        // Not a continuation byte: unknown, and that byte starts the next one.
        if (at >= len || (static_cast<unsigned char>(s[at]) & 0xc0) != 0x80)
            return unknown;
        cp = cp << 6 | (static_cast<unsigned char>(s[at++]) & 63U);
    }
    static constexpr std::uint32_t smallest[] = {0, 0x80, 0x800, 0x10000};
    if (cp < smallest[extra] || (cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff)
        return unknown; // too long a form, or not a character
    return cp;
}
} // namespace starter::font
