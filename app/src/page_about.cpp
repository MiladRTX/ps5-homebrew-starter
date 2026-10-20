/*
 * PS5 Homebrew Starter - the About page: the background service in detail,
 * including its version, and what the app is built with. Nothing to press.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hello.hpp"
#include "link.hpp"
#include "pages.hpp"
#include "service_version.h"

#include <stdio.h>

namespace starter::hello
{
using font::Face;
using screen::Color;

bool about_update(unsigned) noexcept
{
    return false; // a page that only shows things
}

void about_draw(screen::Canvas &c) noexcept
{
    const State &s = state();
    const int card_w = (1920 - 2 * margin - 40) / 2;

    // Left: the service.
    int x = margin, y = content_y;
    c.round_rect(x, y, card_w, content_h, 18, Color::surface);
    c.round_frame(x, y, card_w, content_h, 18, 2, Color::line);
    x += 32;
    y += 62;
    c.text(x, y, "Background service", Face::semibold, 28, Color::text);
    y += 60;
    c.circle(x + 9, y - 9, 8, s.service_up ? Color::green : Color::faint);
    c.text(x + 30, y, s.service_up ? "Running" : "Not running", Face::semibold, 24,
           s.service_up ? Color::text : Color::muted);

    char line[96];
    auto fact = [&](const char *label, const char *value, Color colour) {
        y += 58;
        c.text(x, y, label, Face::semibold, 15, Color::faint, 255, 0.08f);
        c.text(x + 300, y, value, Face::mono_medium, 22, colour);
    };
    fact("VERSION RUNNING", s.service_up && s.service_version[0] ? s.service_version : "-", Color::text);
    const bool older = s.service_up && link::compare_versions(s.service_version, SERVICE_VERSION) < 0;
    fact("IN THIS APP", SERVICE_VERSION, older ? Color::orange : Color::text);
    snprintf(line, sizeof line, "%lds", s.service_uptime_s);
    fact("UP FOR", s.service_up ? line : "-", Color::text);
    snprintf(line, sizeof line, "%ld \xC2\xB0" "C", s.service_cpu_c);
    fact("CPU", s.service_up && s.service_cpu_c > 0 ? line : "-", Color::text);

    // Its page, at the real address.
    if (s.address[0])
        snprintf(line, sizeof line, "http://%s:%d", s.address, service_port);
    else
        snprintf(line, sizeof line, "no network");
    fact("STATUS PAGE", line, s.address[0] ? Color::accent : Color::faint);

    y += 70;
    static const char *const how[] = {
#ifdef STARTER_SIM
        // The simulator's service runs on the PC, not on the console.
        "Open the status page in your PC's browser: in the",
        "simulator, the service runs on this PC, not the PS5.",
#else
        "Open the status page on your phone or PC. The service",
        "keeps running after you close the app.",
#endif
        "The app starts the service when it isn't running, and",
        "replaces it when it's older than the one it carries.",
    };
    for (const char *text : how)
    {
        c.text(x, y, text, Face::regular, 20, Color::muted);
        y += 32;
    }

    // Right: the app.
    x = margin + card_w + 40;
    y = content_y;
    c.round_rect(x, y, card_w, content_h, 18, Color::surface);
    c.round_frame(x, y, card_w, content_h, 18, 2, Color::line);
    x += 32;
    y += 62;
    c.text(x, y, "This app", Face::semibold, 28, Color::text);
    static const char *const about[][2] = {
        {"MADE WITH", "PS5 Homebrew Starter"},
        {"BUILT WITH", "ps5-native-app-boilerplate"},
        {"", "by BlackBearReloaded"},
        {"PAYLOADS", "PS5 payload SDK"},
        {"FONTS", "Inter, JetBrains Mono"},
        {"BUTTONS", "PS4 Buttons by Arks (CC BY 4.0)"},
        {"", "arks.itch.io/ps4-buttons"},
    };
    y += 4;
    for (const auto &row : about)
    {
        y += row[0][0] ? 58 : 34;
        if (row[0][0])
            c.text(x, y, row[0], Face::semibold, 15, Color::faint, 255, 0.08f);
        c.text(x + 220, y, row[1], Face::regular, 22, Color::text);
    }

    // A picture: the app's own icon, read from its PNG at startup (hello.cpp)
    // and drawn smaller than it is (512 pixels, here 180).
    y += 60;
    c.text(x, y, "ICON", Face::semibold, 15, Color::faint, 255, 0.08f);
    constexpr int icon_px = 180;
    if (s.icon.loaded())
    {
        c.image(x + 220, y - 16, icon_px, icon_px, s.icon);
        c.round_frame(x + 220, y - 16, icon_px, icon_px, 4, 2, Color::line2);
    }
    else
        c.text(x + 220, y, "icon0.png wasn't found", Face::regular, 22, Color::faint);
    static const char *const picture[] = {"sce_sys/icon0.png,", "read with stb_image", "and scaled to fit."};
    for (int i = 0; i < 3; ++i)
        c.text(x + 220 + icon_px + 28, y + 14 + i * 32, picture[i], Face::regular, 20, Color::muted);
}
} // namespace starter::hello
