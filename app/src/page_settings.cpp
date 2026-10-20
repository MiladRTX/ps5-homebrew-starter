/*
 * PS5 Homebrew Starter - the Settings page: a menu. Up and down choose a row,
 * left and right change it, Cross changes it or runs it. The rows come from
 * one table: a new setting is a line in it (and a field in State).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "buttons.hpp"
#include "hello.hpp"
#include "pad.hpp"
#include "pages.hpp"
#include "sound.hpp"

namespace starter::hello
{
using font::Face;
using screen::Color;

namespace
{
const char *const size_names[size_count] = {"Small", "Medium", "Large"};
const char *const speed_names[speed_count] = {"Slow", "Normal", "Fast"};
const char *const colour_names[colour_count] = {"Blue", "Green", "Orange"};
const char *const sound_names[2] = {"Off", "On"};

// A row is either a choice (value points at the setting) or an action.
struct Row
{
    const char *label;
    int *value;                // nullptr for an action
    int count;                 // how many choices
    const char *const *names;  // their names
    const char *help;          // the line under the label
};

constexpr int row_count = 5;

const Row *rows() noexcept
{
    State &s = state();
    static Row table[row_count];
    table[0] = {"Box size", &s.size, size_count, size_names, "How big the box on the Play page is."};
    table[1] = {"Move speed", &s.speed, speed_count, speed_names, "How far one press moves it."};
    table[2] = {"Box colour", &s.colour, colour_count, colour_names, "Square on the Play page changes it too."};
    table[3] = {"Sound", &s.sound, 2, sound_names, "The clicks and chirps as you use the app."};
    table[4] = {"Reset position", nullptr, 0, nullptr, "Puts the box back in the middle."};
    return table;
}

// Steps a choice by delta (+1 or -1), going round at the ends.
void step_choice(const Row &row, int delta) noexcept
{
    *row.value = (*row.value + delta + row.count) % row.count;
    clamp_box(); // a bigger box may now stick out of the playground
    sound::enable(state().sound != 0);
    sound::play(sound::Effect::select); // after enable: turning sound on plays it, off doesn't
    changed();
}

// Layout: the menu on the left, a preview of the box on the right.
constexpr int card_x = margin, card_w = 1920 - 2 * margin;
constexpr int menu_x = card_x + 32, menu_w = 1060;
constexpr int row_y = content_y + 120, row_h = 92;
} // namespace

bool settings_update(unsigned pressed) noexcept
{
    State &s = state();
    if (!pressed)
        return false;
    const Row *table = rows();
    const Row &row = table[s.menu_selected];

    const int before = s.menu_selected;
    if ((pressed & pad::up) && s.menu_selected > 0)
        --s.menu_selected;
    if ((pressed & pad::down) && s.menu_selected + 1 < row_count)
        ++s.menu_selected;
    if (s.menu_selected != before)
        sound::play(sound::Effect::move);
    if (row.value && (pressed & pad::left))
        step_choice(row, -1);
    if (row.value && (pressed & (pad::right | pad::cross)))
        step_choice(row, +1);
    if (!row.value && (pressed & pad::cross))
    {
        centre_box();
        changed();
        sound::play(sound::Effect::select);
        say("Box back in the middle");
    }
    return true;
}

void settings_draw(screen::Canvas &c) noexcept
{
    const State &s = state();
    c.round_rect(card_x, content_y, card_w, content_h, 18, Color::surface);
    c.round_frame(card_x, content_y, card_w, content_h, 18, 2, Color::line);
    c.text(menu_x, content_y + 62, "Settings", Face::semibold, 28, Color::text);
    c.text(menu_x + 140, content_y + 62, "saved in /data/ps5-starter", Face::regular, 18,
           Color::faint);

    const Row *table = rows();
    for (int i = 0; i < row_count; ++i)
    {
        const Row &row = table[i];
        const int y = row_y + i * row_h;
        const bool selected = i == s.menu_selected;
        if (selected)
            c.round_rect(menu_x - 12, y, menu_w + 24, row_h - 8, 12, Color::hover);
        c.text(menu_x + 12, y + 38, row.label, Face::semibold, 24, Color::text);
        c.text(menu_x + 12, y + 68, row.help, Face::regular, 18, Color::muted);

        // The value on the right: ‹ Medium › for a choice, the button for an action.
        const int right = menu_x + menu_w - 12;
        if (row.value)
        {
            const char *value = row.names[*row.value];
            const float w = screen::Canvas::text_width(value, Face::semibold, 22);
            const Color arrows = selected ? Color::accent : Color::faint;
            c.text(right - 18, y + 52, "\xE2\x80\xBA", Face::semibold, 26, arrows); // ›
            c.text(right - 40 - w, y + 51, value, Face::semibold, 22,
                   selected ? Color::text : Color::muted);
            c.text(right - 62 - w, y + 52, "\xE2\x80\xB9", Face::semibold, 26, arrows); // ‹
        }
        else
            buttons::draw(c, buttons::cross, right - 44, y + (row_h - 8) / 2 - 22, 44, selected ? 255 : 120);
    }

    // The box as the settings make it.
    const int preview_x = menu_x + menu_w + 60, preview_w = card_x + card_w - 32 - preview_x;
    const int preview_y = row_y, preview_h = row_count * row_h - 8;
    c.round_rect(preview_x, preview_y, preview_w, preview_h, 14, Color::bg);
    c.text(preview_x + 20, preview_y + 36, "PREVIEW", Face::semibold, 15, Color::faint, 255, 0.08f);
    const int size = box_size_px();
    c.round_rect(preview_x + (preview_w - size) / 2, preview_y + (preview_h - size) / 2 + 10, size,
                 size, size / 7, box_colour());

    // What the buttons do here.
    using namespace buttons;
    static const Hint what_does_what[] = {
        {dpad_up, dpad_down, "choose"},
        {dpad_left, dpad_right, "change"},
        {cross, none, "change or reset"},
    };
    hints(c, menu_x, content_y + content_h - 38, what_does_what);
}
} // namespace starter::hello
