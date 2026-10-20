/*
 * PS5 Homebrew Starter - turning held buttons into presses (pad_input.hpp).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "pad_input.hpp"

#include "pad.hpp"
#include "screen.hpp"

#include <cstdint>

namespace starter::pad::input
{
namespace
{
constexpr unsigned repeating = up | down | left | right | l1 | r1 | l2 | r2;
constexpr std::uint64_t repeat_delay_ms = 350, repeat_ms = 110, fast_after_ms = 1500,
                        fast_repeat_ms = 40;

unsigned g_held = 0;     // held in the last sample
unsigned g_pressed = 0;  // new presses since take_presses()
unsigned g_repeat_button = 0;
std::uint64_t g_held_since = 0, g_next_repeat = 0;
} // namespace

void sample(unsigned held) noexcept
{
    g_pressed |= held & ~g_held;
    g_held = held;
}

void suspend() noexcept
{
    g_held = ~0U; // everything "held": only a press after letting go counts
    g_pressed = 0;
    g_repeat_button = 0;
}

unsigned take_presses() noexcept
{
    unsigned pressed = g_pressed;
    g_pressed = 0;

    // Repeat the newest held repeating button.
    const std::uint64_t t = screen::now_ms();
    const unsigned fresh = pressed & repeating;
    if (fresh)
    {
        g_repeat_button = fresh & (~fresh + 1U); // its lowest bit
        g_held_since = t;
        g_next_repeat = t + repeat_delay_ms;
    }
    else if (g_repeat_button && (g_held & g_repeat_button))
    {
        if (t >= g_next_repeat)
        {
            pressed |= g_repeat_button;
            g_next_repeat = t + (t - g_held_since >= fast_after_ms ? fast_repeat_ms : repeat_ms);
        }
    }
    else
        g_repeat_button = 0;
    return pressed;
}

unsigned stick_as_dpad(int x, int y) noexcept
{
    const int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    if (ax >= 80 && ax > ay)
        return x < 0 ? left : right;
    if (ay >= 80)
        return y < 0 ? up : down;
    return 0;
}
} // namespace starter::pad::input
