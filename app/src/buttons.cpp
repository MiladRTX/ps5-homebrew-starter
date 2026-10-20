/*
 * PS5 Homebrew Starter - controller button icons (buttons.hpp).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "buttons.hpp"

#include "image.hpp"
#include "paths.hpp"

#include <stdio.h>

namespace starter::buttons
{
namespace
{
using font::Face;
using screen::Color;

const struct
{
    const char *file;  // in app/assets/buttons, without .png
    const char *label; // drawn when the picture is missing
} names[] = {
    {"cross", "Cross"},     {"circle", "Circle"},    {"square", "Square"},
    {"triangle", "Triangle"}, {"dpad", "D-pad"},     {"dpad_up", "Up"},
    {"dpad_down", "Down"},  {"dpad_left", "Left"},   {"dpad_right", "Right"},
    {"l1", "L1"},           {"r1", "R1"},            {"l2", "L2"},
    {"r2", "R2"},           {"l_stick", "Stick"},    {"options", "OPTIONS"},
    {"touchpad", "Touchpad"},
};
static_assert(sizeof names / sizeof names[0] == icon_count,
              "buttons.cpp: one names[] row per Icon in buttons.hpp, in the same order");

image::Image g_icons[icon_count];

constexpr int hint_icon = 40, hint_text = 20;
} // namespace

void load() noexcept
{
    for (int i = 0; i < icon_count; ++i)
    {
        char path[96];
        snprintf(path, sizeof path, STARTER_APP0 "/assets/buttons/%s.png", names[i].file);
        (void)image::load(path, g_icons[i]);
    }
}

int draw(screen::Canvas &c, Icon icon, int x, int y, int size, int alpha) noexcept
{
    if (icon < 0 || icon >= icon_count)
        return 0;
    if (g_icons[icon].loaded())
    {
        c.image(x, y, size, size, g_icons[icon], alpha);
        return size;
    }
    // No picture: the name, in the same space.
    const int px = size / 2;
    const float baseline = static_cast<float>(y) + (size + font::cap_height(Face::semibold, px)) / 2;
    return static_cast<int>(c.text(static_cast<float>(x), baseline, names[icon].label, Face::semibold, px,
                                   Color::text, alpha)) + 4;
}

void hints(screen::Canvas &c, int x, int y, const Hint *list, int count) noexcept
{
    const int top = y - hint_icon / 2;
    const float baseline = static_cast<float>(y) + font::cap_height(Face::regular, hint_text) / 2;
    for (int i = 0; i < count; ++i)
    {
        x += draw(c, list[i].first, x, top, hint_icon);
        x += draw(c, list[i].second, x, top, hint_icon);
        x += 8;
        x += static_cast<int>(c.text(static_cast<float>(x), baseline, list[i].what, Face::regular, hint_text,
                                     Color::muted));
        x += 34; // to the next hint
    }
}
} // namespace starter::buttons
