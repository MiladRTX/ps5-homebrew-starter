/*
 * PS5 Homebrew Starter - the simulator's on-screen keyboard: where the PS5
 * shows its own keyboard over the app, the simulator shows a text box you
 * type into with your PC's keyboard. Same keyboard.hpp, so the app's code
 * doesn't know the difference.
 *   Enter  OK     Esc  cancel     Backspace  delete     Ctrl+V  paste
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "keyboard.hpp"
#include "font.hpp"
#include "screen.hpp"
#include "sim.hpp"

#include <SDL.h>

#include <stdio.h>
#include <string.h>

namespace starter::keyboard
{
namespace
{
bool g_open = false;
State g_result = State::idle; // what poll() reports next, once
int g_limit = max_length;     // in characters
char g_title[96];
char g_placeholder[96];
char g_text[max_length * 4 + 1];

int characters(const char *s) noexcept
{
    int n = 0;
    for (; *s; ++s)
        if ((static_cast<unsigned char>(*s) & 0xc0) != 0x80) // not the middle of a character
            ++n;
    return n;
}

// Adds whole characters while there's room.
void append(const char *in) noexcept
{
    const int len = static_cast<int>(strlen(in));
    int at = 0;
    while (at < len)
    {
        const int start = at;
        (void)font::next(in, len, at);
        const size_t used = strlen(g_text), bytes = static_cast<size_t>(at - start);
        if (characters(g_text) >= g_limit || used + bytes >= sizeof g_text)
            break;
        memcpy(g_text + used, in + start, bytes);
        g_text[used + bytes] = '\0';
    }
}

void erase_last() noexcept
{
    size_t n = strlen(g_text);
    while (n > 0 && (static_cast<unsigned char>(g_text[n - 1]) & 0xc0) == 0x80)
        --n; // back over the middle of a character
    if (n > 0)
        --n;
    g_text[n] = '\0';
}

void close(State result) noexcept
{
    g_open = false;
    g_result = result;
    SDL_StopTextInput();
}
} // namespace

bool open(const char *title, const char *placeholder, const char *text, int length) noexcept
{
    if (g_open)
        return false;
    snprintf(g_title, sizeof g_title, "%s", title);
    snprintf(g_placeholder, sizeof g_placeholder, "%s", placeholder);
    g_limit = length >= 1 && length <= max_length ? length : max_length;
    g_text[0] = '\0';
    append(text);
    g_open = true;
    g_result = State::idle;
    SDL_StartTextInput();
    return true;
}

State poll() noexcept
{
    if (g_open)
        return State::open;
    const State result = g_result;
    g_result = State::idle;
    return result;
}

const char *text() noexcept
{
    return g_text;
}
} // namespace starter::keyboard

namespace starter::sim
{
bool keyboard_active() noexcept
{
    return keyboard::g_open;
}

bool keyboard_event(const SDL_Event &e) noexcept
{
    using keyboard::State;
    if (!keyboard::g_open)
        return false;
    switch (e.type)
    {
    case SDL_TEXTINPUT:
        keyboard::append(e.text.text);
        return true;
    case SDL_KEYDOWN:
        // A held key repeats: only Backspace should. A held Enter (Cross,
        // which opened the box) mustn't also accept it.
        if (e.key.repeat && e.key.keysym.scancode != SDL_SCANCODE_BACKSPACE)
            return true;
        switch (e.key.keysym.scancode)
        {
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
            keyboard::close(State::accepted);
            break;
        case SDL_SCANCODE_ESCAPE:
            keyboard::close(State::cancelled);
            break;
        case SDL_SCANCODE_BACKSPACE:
            keyboard::erase_last();
            break;
        default: // Ctrl+V, by the letter on the key (any keyboard layout)
            if (e.key.keysym.sym == SDLK_v && (e.key.keysym.mod & KMOD_CTRL))
                if (char *clip = SDL_GetClipboardText())
                {
                    for (char *p = clip; *p; ++p) // one line
                        if (*p == '\n' || *p == '\r' || *p == '\t')
                            *p = ' ';
                    keyboard::append(clip);
                    SDL_free(clip);
                }
            break;
        }
        return true;
    case SDL_KEYUP:
    case SDL_TEXTEDITING:
        return true;
    default:
        return false;
    }
}

void keyboard_draw(screen::Canvas &c) noexcept
{
    using font::Face;
    using screen::Color;
    c.rect(0, 0, screen::width, screen::height, Color::bg, 190); // the app, dimmed

    constexpr int w = 1200, h = 300;
    constexpr int x = (screen::width - w) / 2, y = 300;
    c.round_rect(x, y, w, h, 18, Color::surface2);
    c.round_frame(x, y, w, h, 18, 2, Color::line2);
    c.text(x + 40, y + 64, keyboard::g_title, Face::semibold, 28, Color::text);
    const char *where = "PC simulator: your keyboard types";
    c.text(x + w - 40 - screen::Canvas::text_width(where, Face::regular, 18), y + 64, where,
           Face::regular, 18, Color::faint);

    // The box. Long text slides left so its end and the caret stay in view.
    constexpr int bx = x + 40, by = y + 100, bw = w - 80, bh = 84, inset = 24;
    c.round_rect(bx, by, bw, bh, 12, Color::bg);
    c.round_frame(bx, by, bw, bh, 12, 2, Color::accent);
    c.clip(bx + 12, by, bw - 24, bh);
    float caret = bx + inset;
    if (keyboard::g_text[0])
    {
        const float tw = screen::Canvas::text_width(keyboard::g_text, Face::regular, 26);
        const float room = bw - 2 * inset - 6;
        const float tx = bx + inset - (tw > room ? tw - room : 0);
        c.text(tx, by + 52, keyboard::g_text, Face::regular, 26, Color::text);
        caret = tx + tw + 3;
    }
    else
        c.text(bx + inset, by + 52, keyboard::g_placeholder, Face::regular, 26, Color::faint);
    c.rect(static_cast<int>(caret), by + 22, 3, 40, Color::accent);
    c.unclip();

    c.text(x + 40, y + h - 40, "Enter  OK      Esc  cancel      Backspace  delete      Ctrl+V  paste",
           Face::regular, 18, Color::muted);
    char count[32];
    snprintf(count, sizeof count, "%d / %d", keyboard::characters(keyboard::g_text), keyboard::g_limit);
    c.text(x + w - 40 - screen::Canvas::text_width(count, Face::mono_medium, 18), y + h - 40, count,
           Face::mono_medium, 18, Color::faint);
}

void keyboard_type(const char *text) noexcept
{
    if (!keyboard::g_open)
        return;
    char spaced[keyboard::max_length * 4 + 1];
    snprintf(spaced, sizeof spaced, "%s", text);
    for (char *p = spaced; *p; ++p)
        if (*p == '_')
            *p = ' ';
    keyboard::append(spaced);
}

void keyboard_finish(bool accept) noexcept
{
    if (keyboard::g_open)
        keyboard::close(accept ? keyboard::State::accepted : keyboard::State::cancelled);
}
} // namespace starter::sim
