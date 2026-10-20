/*
 * PS5 Homebrew Starter - "Hello PS5": what all pages share, the tab bar, and
 * keeping the background service running and up to date.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hello.hpp"

#include "link.hpp"
#include "pad.hpp"
#include "pages.hpp"
#include "paths.hpp"
#include "service_version.h" // SERVICE_VERSION: the service this app carries

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

namespace starter::hello
{
using font::Face;
using screen::Color;

namespace
{
State g_state{};

// ---- The pages, in tab order ----------------------------------------------
// To add a page: a value in Page (hello.hpp), its functions (pages.hpp), and
// a line here.
const struct
{
    const char *name;
    bool (*update)(unsigned pressed) noexcept;
    void (*draw)(screen::Canvas &c) noexcept;
} pages[page_count] = {
    {"Play", play_update, play_draw},
    {"Settings", settings_update, settings_draw},
    {"About", about_update, about_draw},
};

// ---- Where things are kept -------------------------------------------------
// /data is the console's own storage: it survives reboots and app updates.
constexpr const char *data_dir = STARTER_DATA "/ps5-starter";
constexpr const char *save_path = STARTER_DATA "/ps5-starter/hello-app.txt";
// The service payload, packaged inside the app (app/build.sh copies it there).
constexpr const char *service_elf = STARTER_APP0 "/assets/service.elf";

void save() noexcept
{
    mkdir(data_dir, 0777);
    FILE *f = fopen(save_path, "w");
    if (!f)
        return;
    const State &s = g_state;
    fprintf(f, "%d %d %d %d %d\n", static_cast<int>(s.box_x), static_cast<int>(s.box_y), s.colour,
            s.size, s.speed);
    fclose(f);
}

// Asks the service for its status. Starts it if nothing answers, and
// replaces it if it's older than the one this app carries. Each at most
// once per launch: if it fails, trying every 2 seconds won't help.
void check_service() noexcept
{
    State &s = g_state;
    // Looked up again every time: it changes after rest mode or on another network.
    if (!link::console_address(s.address, sizeof s.address))
        s.address[0] = '\0';
    char body[512];
    const int status = link::http_get(service_port, "/status", body, sizeof body);
    s.service_up = status == 200;
    if (!s.service_up)
    {
        if (!s.service_start_tried)
        {
            s.service_start_tried = true;
            const int r = link::start_payload(service_elf);
            say(r == 0    ? "Starting the service..."
                : r == -1 ? "service.elf is missing from the app"
                          : "No ELF loader on port 9021");
        }
        return;
    }
    s.service_uptime_s = link::json_number(body, "uptime", -1);
    s.service_cpu_c = link::json_number(body, "cpu", -1);
    link::json_text(body, "version", s.service_version, sizeof s.service_version);

    // An older service is still running, e.g. after the app was updated: start
    // ours. Its single-copy lock stops the old one and takes its place.
    // (One without a version is older than any.)
    if (!s.service_update_tried && link::compare_versions(s.service_version, SERVICE_VERSION) < 0)
    {
        s.service_update_tried = true;
        static char line[96];
        snprintf(line, sizeof line, "Updating the service from %s to %s...",
                 s.service_version[0] ? s.service_version : "an old version", SERVICE_VERSION);
        say(link::start_payload(service_elf) == 0 ? line : "Couldn't update the service");
    }
}

// The tab bar: L1  Play | Settings | About  R1, at the top right.
void draw_tabs(screen::Canvas &c) noexcept
{
    constexpr int h = 52, pad = 26, key_w = 52, gap = 12, size = 20;
    constexpr int y = 70;
    const int right = 1920 - margin;

    float widths[page_count];
    float total = 0;
    for (int i = 0; i < page_count; ++i)
    {
        widths[i] = screen::Canvas::text_width(pages[i].name, Face::semibold, size) + 2 * pad;
        total += widths[i];
    }
    int x = right - key_w - gap - static_cast<int>(total) - gap - key_w;

    auto key = [&](int kx, const char *label) {
        c.round_frame(kx, y + 8, key_w, h - 16, 8, 2, Color::line2);
        const float w = screen::Canvas::text_width(label, Face::mono_medium, 16);
        c.text(kx + (key_w - w) / 2, y + h / 2 + 6, label, Face::mono_medium, 16, Color::muted);
    };
    key(x, "L1");
    x += key_w + gap;

    const int bar_x = x;
    c.round_rect(bar_x, y, static_cast<int>(total), h, 12, Color::surface);
    c.round_frame(bar_x, y, static_cast<int>(total), h, 12, 2, Color::line);
    const int current = static_cast<int>(g_state.page);
    for (int i = 0; i < page_count; ++i)
    {
        const int w = static_cast<int>(widths[i]);
        if (i == current)
            c.round_rect(x + 4, y + 4, w - 8, h - 8, 9, Color::hover);
        c.text(x + pad, y + h / 2 + 7, pages[i].name, Face::semibold, size,
               i == current ? Color::accent : Color::muted);
        x += w;
    }
    key(x + gap, "R1");
}
} // namespace

State &state() noexcept
{
    return g_state;
}

void say(const char *text) noexcept
{
    snprintf(g_state.message, sizeof g_state.message, "%s", text);
    g_state.message_until_ms = screen::now_ms() + 2500;
}

void changed() noexcept
{
    // Not on every press: a held button repeats, and one write a second
    // after the last change is enough.
    g_state.save_at_ms = screen::now_ms() + 1000;
}

int box_size_px() noexcept
{
    static const int sizes[size_count] = {100, 150, 200};
    return sizes[g_state.size];
}

int step_px() noexcept
{
    static const int steps[speed_count] = {15, 30, 60};
    return steps[g_state.speed];
}

screen::Color box_colour() noexcept
{
    static const Color colours[colour_count] = {Color::accent, Color::green, Color::orange};
    return colours[g_state.colour];
}

void load() noexcept
{
    State &s = g_state;
    s.size = 1;
    s.speed = 1;
    centre_box();

    FILE *f = fopen(save_path, "r");
    if (!f)
        return;
    // A file can be old, damaged or edited by hand: check every value.
    int x = 0, y = 0, colour = 0, size = 1, speed = 1;
    if (fscanf(f, "%d %d %d %d %d", &x, &y, &colour, &size, &speed) >= 3)
    {
        s.box_x = static_cast<float>(x);
        s.box_y = static_cast<float>(y);
        s.colour = colour >= 0 && colour < colour_count ? colour : 0;
        s.size = size >= 0 && size < size_count ? size : 1;
        s.speed = speed >= 0 && speed < speed_count ? speed : 1;
    }
    fclose(f);
}

bool update() noexcept
{
    State &s = g_state;
    const std::uint64_t now = screen::now_ms();
    bool redraw = false;

    // L1 and R1 belong to the tab bar; every other button goes to the page.
    unsigned pressed = pad::poll();
    if (pressed & (pad::l1 | pad::r1))
    {
        const int step = (pressed & pad::r1) ? 1 : page_count - 1;
        s.page = static_cast<Page>((static_cast<int>(s.page) + step) % page_count);
        pressed &= ~(pad::l1 | pad::r1);
        redraw = true;
    }
    if (pages[static_cast<int>(s.page)].update(pressed))
        redraw = true;

    if (s.save_at_ms && now >= s.save_at_ms)
    {
        save();
        s.save_at_ms = 0;
    }

    // The service, every 2 seconds, whatever page is open.
    if (now >= s.next_service_check_ms)
    {
        check_service();
        s.next_service_check_ms = now + 2000;
        redraw = true;
    }

    // The message disappearing is a change too.
    if (s.message[0] && now >= s.message_until_ms)
    {
        s.message[0] = '\0';
        redraw = true;
    }
    return redraw;
}

void draw(screen::Canvas &c) noexcept
{
    const State &s = g_state;
    c.clear(Color::bg);

    // Title. Text is drawn on its baseline (the line letters sit on).
    c.text(margin, 118, "Hello PS5", Face::bold, 44, Color::text, 255, -0.02f);
    c.text(margin, 158, "Your first PS5 app. Change app/src and build it again.", Face::regular, 20,
           Color::muted);
    draw_tabs(c);

    pages[static_cast<int>(s.page)].draw(c);

    // The message, bottom centre.
    if (s.message[0])
    {
        const float w = screen::Canvas::text_width(s.message, Face::medium, 20) + 48;
        const int mx = static_cast<int>((1920 - w) / 2);
        c.round_rect(mx, 1000, static_cast<int>(w), 48, 12, Color::hover);
        c.text(mx + 24, 1031, s.message, Face::medium, 20, Color::text);
    }
}
} // namespace starter::hello
