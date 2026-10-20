/*
 * PS5 Homebrew Starter - the PC simulator's pieces talking to each other:
 * screen_sim.cpp owns the window, pad_sim.cpp the input, keyboard_sim.cpp the
 * on-screen keyboard and sound_sim.cpp the sound.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

union SDL_Event;
struct SDL_Renderer;

namespace starter::screen
{
class Canvas;
}

namespace starter::sim
{
// The window's renderer (screen_sim.cpp), for turning mouse positions into
// screen pixels; nullptr before the window is open.
[[nodiscard]] SDL_Renderer *renderer() noexcept;

// Events the input side needs: controllers plugged in and out (pad_sim.cpp).
void pad_event(const SDL_Event &event) noexcept;

// Scripted input (STARTER_SIM_SCRIPT, see screen_sim.cpp): these buttons held,
// and the touchpad touched at x, y (0 to 1), on top of the real input.
void script_hold(unsigned buttons, bool touch, float x, float y) noexcept;

// The keyboard (keyboard_sim.cpp): open while the app waits for text. Then
// the PC's keys type, and the controller is the keyboard's, as on the PS5.
[[nodiscard]] bool keyboard_active() noexcept;
// A key or typed text; true when the keyboard used it.
bool keyboard_event(const SDL_Event &event) noexcept;
// Draws it over the app's frame.
void keyboard_draw(screen::Canvas &c) noexcept;
// Scripted typing: text (_ for a space), then OK (true) or cancel (false).
void keyboard_type(const char *text) noexcept;
void keyboard_finish(bool accept) noexcept;
} // namespace starter::sim
