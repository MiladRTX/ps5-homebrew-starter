/*
 * PS5 Homebrew Starter - sound effects made in code: a tone that slides from
 * one pitch to another and fades out. No audio files to load or carry
 * (docs/08-lists-images-keyboard-sound.md). The PS5's output is at the bottom;
 * the simulator's is app/sim/sound_sim.cpp.
 * The PS5 output follows ProsperoStore's audio_out.cpp, Copyright (C) 2026 BlackBearReloaded.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sound.hpp"

#include <atomic>
#include <cmath>

namespace starter::sound
{
namespace
{
enum class Wave
{
    sine,
    soft_square, // brighter, for the buzz
};

// One effect: from `from` Hz to `to` Hz in `ms` milliseconds, at `volume` (0..1).
struct Tone
{
    float from, to;
    int ms;
    float volume;
    Wave wave;
};

// The four effects, in the order of Effect. Change these to change the sounds.
constexpr Tone tones[] = {
    {1500, 1300, 28, 0.16f, Wave::sine},        // move: a short tick
    {700, 1400, 75, 0.20f, Wave::sine},         // select: a rising chirp
    {900, 450, 90, 0.18f, Wave::sine},          // back: a falling tone
    {170, 150, 150, 0.14f, Wave::soft_square},  // error: a low buzz
};
// The build stops here if the table and Effect (sound.hpp) don't match, or a
// tone can't be played.
static_assert(sizeof tones / sizeof tones[0] == static_cast<int>(Effect::count),
              "sound.cpp: one tones[] row per sound::Effect, in the same order");
constexpr bool tones_ok() noexcept
{
    for (const Tone &t : tones)
        if (t.ms <= 0 || t.from <= 0 || t.to <= 0 || t.volume < 0 || t.volume > 1)
            return false;
    return true;
}
static_assert(tones_ok(), "sound.cpp: each tone needs ms > 0, from and to > 0 Hz, volume 0 to 1");

std::atomic<bool> g_enabled{true};
std::atomic<int> g_request{-1}; // the effect play() asked for, -1 none

// The voice: only render() touches these (on the sound thread).
const Tone *g_tone = nullptr;
int g_at = 0;      // samples played
int g_length = 0;  // samples in all
float g_phase = 0; // 0..1 through one wave
} // namespace

void play(Effect effect) noexcept
{
    if (g_enabled.load(std::memory_order_relaxed))
        g_request.store(static_cast<int>(effect), std::memory_order_relaxed);
}

void enable(bool on) noexcept
{
    g_enabled.store(on, std::memory_order_relaxed);
}

namespace detail
{
void render(std::int16_t *out, int frames) noexcept
{
    // A new effect cuts off the one playing.
    const int request = g_request.exchange(-1, std::memory_order_relaxed);
    if (request >= 0 && request < static_cast<int>(sizeof tones / sizeof tones[0]))
    {
        g_tone = &tones[request];
        g_at = 0;
        g_length = g_tone->ms * rate / 1000;
        g_phase = 0;
    }
    if (!g_enabled.load(std::memory_order_relaxed))
        g_tone = nullptr;

    constexpr float two_pi = 6.2831853f;
    constexpr int attack = rate / 500; // 2 ms in: no click at the start
    for (int i = 0; i < frames; ++i)
    {
        float sample = 0;
        if (g_tone)
        {
            const float done = static_cast<float>(g_at) / static_cast<float>(g_length);
            // Pitch slides evenly in musical terms: a ratio, not a straight line.
            const float hz = g_tone->from * std::pow(g_tone->to / g_tone->from, done);
            g_phase += hz / rate;
            g_phase -= std::floor(g_phase);
            float wave = std::sin(two_pi * g_phase);
            if (g_tone->wave == Wave::soft_square)
                wave = std::tanh(wave * 4.0f); // a sine pushed flat: squarer, not harsh
            // Up in 2 ms, then down to nothing by the end.
            const float fade = (1 - done) * (1 - done);
            const float envelope = g_at < attack ? static_cast<float>(g_at) / attack * fade : fade;
            sample = wave * envelope * g_tone->volume;
            if (++g_at >= g_length)
                g_tone = nullptr;
        }
        // Within -1..1 before it becomes 16 bits: louder would wrap round to
        // a crackle, not just clip.
        sample = sample > 1.0f ? 1.0f : sample < -1.0f ? -1.0f : sample;
        const auto s = static_cast<std::int16_t>(sample * 32767.0f);
        out[2 * i] = s;     // left
        out[2 * i + 1] = s; // right
    }
}
} // namespace detail
} // namespace starter::sound

#if !defined(STARTER_PREVIEW)

// ---- The PS5's output ------------------------------------------------------
// A thread hands the console 256 frames at a time. sceAudioOutOutput waits
// until the previous batch has started playing, so the loop runs at the
// speed sound plays and needs no timer.
#include <pthread.h>

extern "C"
{
    int sceAudioOutInit();
    int sceAudioOutOpen(int user, int type, int index, unsigned grain, unsigned rate, unsigned format);
    int sceAudioOutOutput(int handle, const void *samples);
}

namespace starter::sound
{
namespace
{
constexpr int system_user = 0xff, main_port = 0, s16_stereo = 1;
constexpr unsigned already_initialized = 0x8026000eu;
constexpr int grain = 256; // frames a batch: about 5 ms

int g_handle = -1;

void *output(void *) noexcept
{
    alignas(64) static std::int16_t samples[grain * 2];
    for (;;)
    {
        detail::render(samples, grain);
        if (sceAudioOutOutput(g_handle, samples) < 0)
            break; // the output is gone: stay silent
    }
    return nullptr;
}
} // namespace

bool init() noexcept
{
    if (g_handle >= 0)
        return true;
    const int r = sceAudioOutInit();
    if (r != 0 && static_cast<unsigned>(r) != already_initialized)
        return false;
    g_handle = sceAudioOutOpen(system_user, main_port, 0, grain, detail::rate, s16_stereo);
    if (g_handle < 0)
        return false;
    // Don't name the thread: pthread_setname_np hangs on the PS5.
    pthread_t thread;
    if (pthread_create(&thread, nullptr, output, nullptr) != 0)
        return false;
    pthread_detach(thread);
    return true;
}
} // namespace starter::sound

#elif !defined(STARTER_SIM)

// The PC preview draws still pictures: no sound.
namespace starter::sound
{
bool init() noexcept
{
    return false;
}
} // namespace starter::sound

#endif
