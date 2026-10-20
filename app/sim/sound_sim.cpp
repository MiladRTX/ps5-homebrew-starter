/*
 * PS5 Homebrew Starter - the simulator's sound: the same effects as on the
 * PS5 (app/src/sound.cpp makes them), out of your PC's speakers through SDL.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sound.hpp"

#include <SDL.h>

#include <stdio.h>

namespace starter::sound
{
namespace
{
// SDL asks for more sound from its own thread, a few times a second.
void fill(void *, Uint8 *stream, int bytes) noexcept
{
    detail::render(reinterpret_cast<std::int16_t *>(stream), bytes / 4); // 4 bytes a stereo frame
}
} // namespace

bool init() noexcept
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
    {
        printf("simulator: no sound (%s)\n", SDL_GetError());
        return false;
    }
    SDL_AudioSpec want{};
    want.freq = detail::rate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 512; // about 10 ms: short enough to feel instant
    want.callback = fill;
    // Allowed changes 0: if the PC's sound card wants another format, SDL converts.
    const SDL_AudioDeviceID device = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
    if (!device)
    {
        printf("simulator: no sound (%s)\n", SDL_GetError());
        return false;
    }
    SDL_PauseAudioDevice(device, 0); // start playing
    return true;
}
} // namespace starter::sound
