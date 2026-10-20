/*
 * PS5 Homebrew Starter - controller button icons, for saying what the buttons
 * do: "(Triangle) new  (Cross) edit". The pictures are in app/assets/buttons
 * ("PS4 Buttons" by Arks, CC BY 4.0: see CREDITS.txt there), loaded once.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "screen.hpp"

namespace starter::buttons
{
// One per file in app/assets/buttons (the same names). A new one also needs
// its row in names[] in buttons.cpp, in the same place.
enum Icon : int
{
    cross,
    circle,
    square,
    triangle,
    dpad,
    dpad_up,
    dpad_down,
    dpad_left,
    dpad_right,
    l1,
    r1,
    l2,
    r2,
    l_stick,
    options,
    touchpad,
    icon_count,
    none = -1,
};

// Reads the icons. Call once at startup. One that's missing is drawn as its
// button's name instead, so the app works without them.
void load() noexcept;

// Draws an icon size x size with its top left at x, y, at an opacity of
// alpha (0..255). Returns the width used.
int draw(screen::Canvas &c, Icon icon, int x, int y, int size, int alpha = 255) noexcept;

// A hint: one or two icons and what they do, e.g. {l2, r2, "page"}.
struct Hint
{
    Icon first;
    Icon second; // none for just one
    const char *what;
};

// A row of hints from x, centred on the line at y (the bottom of a page).
void hints(screen::Canvas &c, int x, int y, const Hint *list, int count) noexcept;

// The same for an array, counting it for you: hints(c, x, y, what_does_what);
template <int N>
void hints(screen::Canvas &c, int x, int y, const Hint (&list)[N]) noexcept
{
    hints(c, x, y, list, N);
}
} // namespace starter::buttons
