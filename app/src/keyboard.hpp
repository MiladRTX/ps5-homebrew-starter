/*
 * PS5 Homebrew Starter - the on-screen keyboard: the PS5's own, over the app,
 * for typing text (docs/08-lists-images-keyboard-sound.md). In the PC
 * simulator it's a text box you type into with your PC's keyboard.
 *
 *   keyboard::open("New note", "Write something", "", 80);
 *   ...every frame:
 *   switch (keyboard::poll()) { case keyboard::State::accepted: use(keyboard::text()); ... }
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

namespace starter::keyboard
{
enum class State
{
    idle,      // nothing open
    open,      // the user is typing
    accepted,  // done: text() has what they typed (reported once)
    cancelled, // closed without it (reported once)
    failed,    // the keyboard couldn't open or broke off (reported once)
};

// Up to this many characters.
constexpr int max_length = 120;

// Opens the keyboard. title says what it's for, placeholder shows in an empty
// box, text is what's in the box to start with (UTF-8). False when it can't:
// one is open already, or the system refused.
bool open(const char *title, const char *placeholder, const char *text, int length) noexcept;

// Call every frame while it's open. While it's open, the controller belongs
// to the keyboard: pad::poll() returns nothing.
[[nodiscard]] State poll() noexcept;

// What was typed, UTF-8, after poll() said accepted.
[[nodiscard]] const char *text() noexcept;
} // namespace starter::keyboard
