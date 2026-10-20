/*
 * PS5 Homebrew Starter - the pages. Each page has two functions, like the app
 * itself: update (what its buttons do) and draw (how it looks). To add a page:
 * write its two functions in a new file, declare them here, and add one line
 * to the page table in hello.cpp (docs/07-menus-and-pages.md).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "screen.hpp"

namespace starter::hello
{
// pressed: the buttons pressed this frame (L1 and R1 are the tab bar's).
// Called every frame, also with nothing pressed. Returns true when the page
// changed something on screen.
bool play_update(unsigned pressed) noexcept;
void play_draw(screen::Canvas &c) noexcept;

bool settings_update(unsigned pressed) noexcept;
void settings_draw(screen::Canvas &c) noexcept;

bool about_update(unsigned pressed) noexcept;
void about_draw(screen::Canvas &c) noexcept;
} // namespace starter::hello
