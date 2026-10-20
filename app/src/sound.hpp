/*
 * PS5 Homebrew Starter - short sound effects, made in code: no audio files
 * (docs/08-lists-images-keyboard-sound.md). Played through the PS5's audio
 * output, and through your PC's speakers in the simulator.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cstdint>

namespace starter::sound
{
enum class Effect
{
    move,   // a tick: the selection moved
    select, // a bright chirp: something happened (Cross)
    back,   // a falling tone: closed, cancelled, deleted
    error,  // a low buzz: not possible, or "press again"
};

// Starts the sound output. False when there's none: the app then just stays
// silent, nothing else changes.
bool init() noexcept;

// Plays an effect, cutting off one still playing. Cheap: call it from update().
void play(Effect effect) noexcept;

// Sound on or off (the Settings page). Off: play() does nothing.
void enable(bool on) noexcept;

namespace detail
{
constexpr int rate = 48000; // samples a second

// The mixer, for the outputs (sound.cpp on the PS5, app/sim/sound_sim.cpp on
// a PC): fills `frames` stereo frames of 16-bit samples, left then right.
void render(std::int16_t *out, int frames) noexcept;
} // namespace detail
} // namespace starter::sound
