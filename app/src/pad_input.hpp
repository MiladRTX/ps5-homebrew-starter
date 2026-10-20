/*
 * PS5 Homebrew Starter - turning held buttons into presses, the same way on
 * every platform: pad.cpp feeds it the DualSense on the PS5, app/sim/pad_sim.cpp
 * the keyboard, mouse and controller on a PC. So a held button repeats with
 * exactly the same timing in the simulator as on the console.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

namespace starter::pad::input
{
// One controller sample: the buttons held in it (pad::Button bits).
void sample(unsigned held) noexcept;

// A sample while the system has the controller (its menu, the on-screen
// keyboard): nothing is pressed, and a button still down when the app gets
// the controller back isn't a new press. So the Cross that closed the
// keyboard doesn't also press Cross in the app.
void suspend() noexcept;

// The presses since the last call: each new press once, plus repeats of a
// held direction, L1/R1 or L2/R2 (after 350 ms, every 110 ms, then every
// 40 ms once held for 1.5 s).
[[nodiscard]] unsigned take_presses() noexcept;

// A stick as a D-pad, along its stronger axis: x and y from -128 to 127.
[[nodiscard]] unsigned stick_as_dpad(int x, int y) noexcept;
} // namespace starter::pad::input
