/*
 * PS5 Homebrew Starter - pictures: PNG and JPEG files loaded into memory, for
 * Canvas::image() to draw (docs/08-lists-images-keyboard-sound.md).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cstdint>

namespace starter::image
{
// A picture in memory: w x h pixels, row by row, 0xAABBGGRR like the screen.
struct Image
{
    int w = 0;
    int h = 0;
    std::uint32_t *pixels = nullptr;

    [[nodiscard]] bool loaded() const noexcept
    {
        return pixels != nullptr;
    }
};

// Loads a PNG or JPEG file. False (and an empty image) when it can't: a
// missing file, or one that isn't a picture. Load once, draw every frame.
bool load(const char *path, Image &out) noexcept;

// Frees its memory; the image is empty afterwards.
void unload(Image &image) noexcept;
} // namespace starter::image
