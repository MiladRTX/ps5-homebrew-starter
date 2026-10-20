/*
 * PS5 Homebrew Starter - "Hello PS5", the starter app: its state and what the
 * pages share. hello.cpp runs the app and draws the tab bar; each page is its
 * own file (page_play.cpp, page_settings.cpp, page_about.cpp).
 * docs/06-app-basics.md and docs/07-menus-and-pages.md walk through it.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "screen.hpp"

#include <cstdint>

namespace starter::hello
{
// The pages, in tab order. L1 and R1 switch between them.
enum class Page
{
    play,
    settings,
    about,
};
constexpr int page_count = 3;

// Choices on the Settings page, saved with the box's position.
constexpr int size_count = 3;   // small, medium, large
constexpr int speed_count = 3;  // slow, normal, fast
constexpr int colour_count = 3; // blue, green, orange

struct State
{
    Page page;

    // Play: the box, by its centre, in screen pixels.
    float box_x;
    float box_y;
    char last_button[24];

    // Settings (saved).
    int size;
    int speed;
    int colour;
    int menu_selected; // the highlighted row on the Settings page

    // The background service (payload/service), as last asked.
    bool service_up;
    long service_uptime_s;
    long service_cpu_c;
    char service_version[16];
    char address[40];          // where its page is: the console's address, "" without a network
    bool service_start_tried;  // once per launch, so a broken service isn't restarted forever
    bool service_update_tried; // the same for replacing an older one
    std::uint64_t next_service_check_ms;

    // A short message at the bottom of the screen.
    char message[96];
    std::uint64_t message_until_ms;

    // When to write the settings to disk; 0 when nothing changed.
    std::uint64_t save_at_ms;
};

State &state() noexcept;

// Reads the saved settings. Call once at startup.
void load() noexcept;

// Called about 60 times a second; returns true when the screen must be drawn again.
bool update() noexcept;

// Draws the whole screen: title, tab bar, the current page, the message.
void draw(screen::Canvas &c) noexcept;

// ---- Shared by the pages -----------------------------------------------------

// The service's port (PORT in payload/service/service.c): change both together.
constexpr int service_port = 8080;

// Where pages draw: below the title and the tab bar.
constexpr int margin = 80;
constexpr int content_y = 190;
constexpr int content_h = 780;

// A message at the bottom of the screen for 2.5 seconds.
void say(const char *text) noexcept;
// Something saved changed: write it to disk a second from now.
void changed() noexcept;
// The current settings in pixels and colours.
[[nodiscard]] int box_size_px() noexcept;
[[nodiscard]] int step_px() noexcept;
[[nodiscard]] screen::Color box_colour() noexcept;
// Puts the box back in the middle of the playground.
void centre_box() noexcept;
// Moves the box back inside the playground if it's partly outside (after
// moving it, or after it got bigger).
void clamp_box() noexcept;
} // namespace starter::hello
