/*
 * PS5 Homebrew Starter - the PS5's own on-screen keyboard (the IME dialog).
 * Opening it doesn't wait: the system draws it over the app, and poll() asks
 * every frame whether it's done (docs/08-lists-images-keyboard-sound.md).
 * Based on ProsperoStore's ime.cpp, Copyright (C) 2026 BlackBearReloaded.
 * The simulator's keyboard is app/sim/keyboard_sim.cpp.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "keyboard.hpp"

#if !defined(STARTER_PREVIEW)

#include "font.hpp"
#include "screen.hpp"

#include <cstddef>
#include <cstdint>
#include <string.h>

namespace
{
// What sceImeDialogInit takes. The layout must match the system's to the
// byte: the asserts below stop the build if it doesn't.
struct ImeParam
{
    std::int32_t user_id, type;
    std::uint64_t supported_languages;
    std::int32_t enter_label, input_method;
    void *filter;
    std::uint32_t option, max_text_length;
    std::uint16_t *input_text_buffer; // the text, in and out (UTF-16)
    float pos_x, pos_y;
    std::int32_t horizontal_alignment, vertical_alignment;
    const std::uint16_t *placeholder, *title;
    std::int8_t reserved[16];
};
struct ImeResult
{
    std::int32_t outcome; // 0: the user pressed OK
    std::int8_t reserved[12];
};
static_assert(sizeof(ImeParam) == 96);
static_assert(offsetof(ImeParam, input_text_buffer) == 40);
static_assert(offsetof(ImeParam, title) == 72);
static_assert(sizeof(ImeResult) == 16);
} // namespace

extern "C"
{
    // Not in the SDK: app/stubs/common_dialog.c tells the build where it is.
    int sceCommonDialogInitialize();
    int sceSysmoduleLoadModule(std::uint16_t id);
    int sceUserServiceGetForegroundUser(std::int32_t *user_id);
    int sceImeDialogInit(const ImeParam *param, const void *extended);
    int sceImeDialogGetStatus();
    int sceImeDialogGetResult(ImeResult *result);
    int sceImeDialogTerm();
}

namespace starter::keyboard
{
namespace
{
constexpr std::uint16_t ime_dialog_module = 0x0096;
constexpr std::uint32_t already_initialized = 0x80b80002u;

std::uint16_t g_buffer[max_length + 1];
std::uint16_t g_title[64];
std::uint16_t g_placeholder[64];
char g_text[max_length * 4 + 1];
bool g_ready = false;
bool g_open = false;
std::uint64_t g_closing_ms = 0; // when closing it was first refused, 0: not yet
std::uint64_t g_opened_ms = 0;

// UTF-8 in, UTF-16 out (what the dialog reads), at most `room` units with the 0.
void to_utf16(const char *in, std::uint16_t *out, int room) noexcept
{
    const int len = static_cast<int>(strlen(in));
    int at = 0, n = 0;
    while (at < len && n + 1 < room)
    {
        std::uint32_t cp = font::next(in, len, at);
        if (cp >= 0x10000) // two units: a surrogate pair
        {
            if (n + 2 >= room)
                break;
            cp -= 0x10000;
            out[n++] = static_cast<std::uint16_t>(0xd800 | (cp >> 10));
            out[n++] = static_cast<std::uint16_t>(0xdc00 | (cp & 0x3ff));
        }
        else
            out[n++] = static_cast<std::uint16_t>(cp);
    }
    out[n] = 0;
}

// UTF-16 back to UTF-8, for the app.
void to_utf8(const std::uint16_t *in, char *out, int room) noexcept
{
    int n = 0;
    for (int i = 0; in[i]; ++i)
    {
        std::uint32_t cp = in[i];
        if (cp >= 0xd800 && cp <= 0xdbff && in[i + 1] >= 0xdc00 && in[i + 1] <= 0xdfff)
            cp = 0x10000 + ((cp - 0xd800) << 10) + (in[++i] - 0xdc00);
        else if (cp >= 0xd800 && cp <= 0xdfff)
            cp = 0xfffd; // half a pair: the "unknown" character
        const int bytes = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
        if (n + bytes >= room)
            break;
        if (bytes == 1)
            out[n++] = static_cast<char>(cp);
        else
        {
            out[n++] = static_cast<char>((0xf0u << (4 - bytes)) | (cp >> (6 * (bytes - 1))));
            for (int b = bytes - 1; b > 0; --b)
                out[n++] = static_cast<char>(0x80 | ((cp >> (6 * (b - 1))) & 0x3f));
        }
    }
    out[n] = '\0';
}

// Once: the system's dialog support, then the keyboard's module.
bool prepare() noexcept
{
    if (g_ready)
        return true;
    const int r = sceCommonDialogInitialize();
    if (r < 0 && static_cast<std::uint32_t>(r) != already_initialized)
        return false;
    if (sceSysmoduleLoadModule(ime_dialog_module) < 0)
        return false;
    g_ready = true;
    return true;
}
} // namespace

bool open(const char *title, const char *placeholder, const char *text, int length) noexcept
{
    if (g_open || !prepare())
        return false;
    ImeParam param{};
    if (sceUserServiceGetForegroundUser(&param.user_id) < 0)
        return false;
    if (length < 1 || length > max_length)
        length = max_length;
    to_utf16(title, g_title, 64);
    to_utf16(placeholder, g_placeholder, 64);
    to_utf16(text, g_buffer, length + 1);
    param.max_text_length = static_cast<std::uint32_t>(length);
    param.input_text_buffer = g_buffer;
    // Where it appears: pos is a point on the 1920 x 1080 screen, and the
    // alignments say which part of the keyboard goes there (1, 1: its centre).
    param.pos_x = screen::width / 2.0f;
    param.pos_y = screen::height / 2.0f;
    param.horizontal_alignment = param.vertical_alignment = 1;
    param.placeholder = g_placeholder;
    param.title = g_title;
    if (sceImeDialogInit(&param, nullptr) != 0)
        return false;
    g_text[0] = '\0';
    g_open = true;
    g_opened_ms = screen::now_ms();
    g_closing_ms = 0;
    return true;
}

State poll() noexcept
{
    if (!g_open)
        return State::idle;
    // 1: open. Right after opening it can still say 0 for a moment.
    const int status = sceImeDialogGetStatus();
    if (status == 1 || (status == 0 && screen::now_ms() - g_opened_ms < 1000))
        return State::open;
    State state = State::failed;
    if (status == 2) // finished
    {
        ImeResult result{};
        if (sceImeDialogGetResult(&result) >= 0)
        {
            state = result.outcome == 0 ? State::accepted : State::cancelled;
            if (state == State::accepted)
                to_utf8(g_buffer, g_text, sizeof g_text);
        }
    }
    // Still closing: the controller isn't the app's again until this works.
    // But not forever: a finished keyboard gets 2 s, and one that isn't
    // running at all (status 0 or an error) can't be closing.
    if (sceImeDialogTerm() < 0)
    {
        const std::uint64_t now = screen::now_ms();
        if (!g_closing_ms)
            g_closing_ms = now;
        if (status == 2 && now - g_closing_ms < 2000)
            return State::open;
        state = State::failed;
    }
    g_open = false;
    g_closing_ms = 0;
    return state;
}

const char *text() noexcept
{
    return g_text;
}
} // namespace starter::keyboard

#elif !defined(STARTER_SIM)

// The PC preview draws still pictures: there's no one to type.
namespace starter::keyboard
{
bool open(const char *, const char *, const char *, int) noexcept
{
    return false;
}
State poll() noexcept
{
    return State::idle;
}
const char *text() noexcept
{
    return "";
}
} // namespace starter::keyboard

#endif
