/*
 * PS5 Homebrew Starter - the Play page: a box you move with the D-pad, the
 * stick or the touchpad, and the controller's buttons.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "buttons.hpp"
#include "hello.hpp"
#include "pad.hpp"
#include "pages.hpp"
#include "sound.hpp"

#include <stdio.h>

namespace starter::hello
{
using font::Face;
using screen::Color;

namespace
{
// The playground, where the box moves, and the panel beside it.
constexpr int play_x = margin, play_y = content_y, play_w = 1140, play_h = content_h;
constexpr int side_x = play_x + play_w + 40, side_w = 1920 - margin - side_x;

const char *button_name(unsigned pressed) noexcept
{
    static const struct
    {
        unsigned bit;
        const char *name;
    } names[] = {
        {pad::up, "Up"},           {pad::down, "Down"},     {pad::left, "Left"},
        {pad::right, "Right"},     {pad::cross, "Cross"},   {pad::circle, "Circle"},
        {pad::square, "Square"},   {pad::triangle, "Triangle"}, {pad::l2, "L2"},
        {pad::r2, "R2"},           {pad::options, "OPTIONS"}, {pad::l3, "L3"},
        {pad::r3, "R3"},
    };
    for (const auto &n : names)
        if (pressed & n.bit)
            return n.name;
    return nullptr;
}
} // namespace

void centre_box() noexcept
{
    State &s = state();
    s.box_x = play_x + play_w / 2.0f;
    s.box_y = play_y + play_h / 2.0f;
}

void clamp_box() noexcept
{
    State &s = state();
    const float half = box_size_px() / 2.0f;
    const float lo_x = play_x + half, hi_x = play_x + play_w - half;
    const float lo_y = play_y + half, hi_y = play_y + play_h - half;
    s.box_x = s.box_x < lo_x ? lo_x : s.box_x > hi_x ? hi_x : s.box_x;
    s.box_y = s.box_y < lo_y ? lo_y : s.box_y > hi_y ? hi_y : s.box_y;
}

bool play_update(unsigned pressed) noexcept
{
    State &s = state();
    bool redraw = false;

    if (pressed)
    {
        if (const char *name = button_name(pressed))
            snprintf(s.last_button, sizeof s.last_button, "%s", name);
        const int step = step_px();
        if (pressed & pad::left)
            s.box_x -= step;
        if (pressed & pad::right)
            s.box_x += step;
        if (pressed & pad::up)
            s.box_y -= step;
        if (pressed & pad::down)
            s.box_y += step;
        if (pressed & pad::cross)
        {
            screen::notify("Hello from Hello PS5!");
            sound::play(sound::Effect::select);
            say("Notification sent");
        }
        if (pressed & pad::square)
        {
            s.colour = (s.colour + 1) % colour_count;
            sound::play(sound::Effect::move);
        }
        if (pressed & pad::triangle)
        {
            centre_box();
            sound::play(sound::Effect::back);
            say("Box back in the middle");
        }
        if (pressed & (pad::left | pad::right | pad::up | pad::down | pad::square | pad::triangle))
        {
            clamp_box();
            changed();
        }
        redraw = true;
    }

    // The touchpad: the box follows your finger across the playground.
    const pad::Touch t = pad::touch();
    if (t.down)
    {
        s.box_x = play_x + t.x * play_w;
        s.box_y = play_y + t.y * play_h;
        clamp_box();
        changed();
        redraw = true;
    }
    return redraw;
}

void play_draw(screen::Canvas &c) noexcept
{
    const State &s = state();

    // The playground and the box.
    c.round_rect(play_x, play_y, play_w, play_h, 18, Color::surface);
    c.round_frame(play_x, play_y, play_w, play_h, 18, 2, Color::line);
    const int size = box_size_px();
    c.round_rect(static_cast<int>(s.box_x) - size / 2, static_cast<int>(s.box_y) - size / 2, size,
                 size, size / 7, box_colour());
    char pos[48];
    snprintf(pos, sizeof pos, "%d, %d", static_cast<int>(s.box_x), static_cast<int>(s.box_y));
    c.text(play_x + 24, play_y + play_h - 24, pos, Face::mono_medium, 18, Color::faint);

    // The side panel: controls, last button, the service in one line.
    c.round_rect(side_x, content_y, side_w, content_h, 18, Color::surface);
    c.round_frame(side_x, content_y, side_w, content_h, 18, 2, Color::line);
    const int x = side_x + 32;
    int y = content_y + 60;
    c.text(x, y, "CONTROLS", Face::semibold, 15, Color::faint, 255, 0.08f);
    using namespace buttons;
    static const Hint controls[] = {
        {dpad, l_stick, "move the box"},         {touchpad, none, "drag the box"},
        {cross, none, "send a notification"},    {square, none, "change its colour"},
        {triangle, none, "back to the middle"},  {l1, r1, "switch pages"},
    };
    y += 4;
    for (const Hint &line : controls)
    {
        y += 48; // the middle of the row
        buttons::draw(c, line.first, x, y - 20, 40);
        buttons::draw(c, line.second, x + 42, y - 20, 40);
        c.text(x + 100, y + font::cap_height(Face::regular, 20) / 2, line.what, Face::regular, 20,
               Color::muted);
    }

    y += 80;
    c.text(x, y, "LAST BUTTON", Face::semibold, 15, Color::faint, 255, 0.08f);
    y += 52;
    c.text(x, y, s.last_button[0] ? s.last_button : "none yet", Face::bold, 38,
           s.last_button[0] ? Color::accent : Color::faint);

    y += 80;
    c.text(x, y, "BACKGROUND SERVICE", Face::semibold, 15, Color::faint, 255, 0.08f);
    y += 44;
    c.circle(x + 8, y - 8, 7, s.service_up ? Color::green : Color::faint);
    char line[64];
    if (s.service_up)
        snprintf(line, sizeof line, "Running, up %lds", s.service_uptime_s);
    else
        snprintf(line, sizeof line, "Not running");
    c.text(x + 26, y, line, Face::semibold, 22, s.service_up ? Color::text : Color::muted);
    c.text(x + 26, y + 36, "Details on the About page", Face::regular, 18, Color::faint);
}
} // namespace starter::hello
