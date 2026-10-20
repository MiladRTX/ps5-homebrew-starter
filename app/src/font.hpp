/*
 * PS5 Homebrew Starter - the Inter and JetBrains Mono fonts, rasterised with
 * stb_truetype and cached per face, size and character.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cstdint>

namespace starter::font
{
enum Face
{
    regular,
    medium,
    semibold,
    bold,
    mono_medium,
    mono_semibold,
    face_count,
};

struct Glyph
{
    int w, h;       // bitmap size
    int xoff, yoff; // from the pen position on the baseline to the bitmap's top left
    float advance;  // in pixels
    const std::uint8_t *bits; // w * h coverage, nullptr for an empty glyph
};

// Reads the faces from dir (with a trailing slash). False if any is missing.
bool load(const char *dir) noexcept;

// Text sizes from 1 to this many pixels; outside them nothing is drawn.
constexpr int max_px = 512;

// nullptr when the face isn't loaded or px is out of range; a glyph without
// bits for blanks. Valid until the next call (the cache may start over).
[[nodiscard]] const Glyph *glyph(Face face, int px, std::uint32_t codepoint) noexcept;
[[nodiscard]] float kern(Face face, int px, std::uint32_t left, std::uint32_t right) noexcept;
// Distance from the baseline up to the top of capital letters.
[[nodiscard]] float cap_height(Face face, int px) noexcept;

// The next UTF-8 code point at s[at], moving at past it. Bytes that aren't
// valid UTF-8 come out as U+FFFD, one at a time, never eating the text after them.
[[nodiscard]] std::uint32_t next(const char *s, int len, int &at) noexcept;
} // namespace starter::font
