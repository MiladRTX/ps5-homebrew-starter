/*
 * PS5 Homebrew Starter - the app's screens as PNGs on a PC, without a console
 * (docs/09-pc-preview.md). The same drawing code as on the PS5, with sample
 * state set by hand. Add a shot for every page and state you want to check.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hello.hpp"
#include "pad.hpp"
#include "screen.hpp"

#include <stdio.h>
#include <string.h>

// No controller on a PC: the preview never calls update(), these just link.
namespace starter::pad
{
bool init() noexcept
{
    return true;
}
unsigned poll() noexcept
{
    return 0;
}
Touch touch() noexcept
{
    return Touch{false, 0, 0};
}
} // namespace starter::pad

namespace
{
void shot(const char *dir, const char *name) noexcept
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    starter::screen::preview(starter::hello::draw, path);
    printf("wrote %s\n", path);
}
} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: preview <fonts dir/> <output dir>\n");
        return 2;
    }
    if (!starter::font::load(argv[1]))
        return 1;
    using starter::hello::Page;
    starter::hello::State &s = starter::hello::state();
    starter::hello::load(); // the defaults: there's no saved file on a PC

    // Just opened: nothing pressed, the service not answering yet.
    shot(argv[2], "1_play_start");

    // After playing a bit: box moved and recoloured, the service running.
    s.box_x = 420;
    s.box_y = 380;
    s.colour = 1;
    strcpy(s.last_button, "Cross");
    s.service_up = true;
    s.service_uptime_s = 42;
    s.service_cpu_c = 51;
    strcpy(s.service_version, "1.0");
    strcpy(s.address, "192.168.0.10");
    strcpy(s.message, "Notification sent");
    s.message_until_ms = ~0ULL;
    shot(argv[2], "2_play");
    s.message[0] = '\0';

    // The Settings menu, on its second row, with a large box.
    s.page = Page::settings;
    s.menu_selected = 1;
    s.size = 2;
    shot(argv[2], "3_settings");

    // About, with an older service running than the app carries.
    s.page = Page::about;
    strcpy(s.service_version, "0.9");
    strcpy(s.message, "Updating the service from 0.9 to 1.0...");
    shot(argv[2], "4_about_updating");
    return 0;
}
