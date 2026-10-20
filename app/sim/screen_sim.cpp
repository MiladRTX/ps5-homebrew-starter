/*
 * PS5 Homebrew Starter - the PC simulator's screen: the app's frames in a
 * window, live. The drawing is the PS5's own code (app/src/screen.cpp draws
 * into a frame in memory); this file only shows that frame in a window,
 * paces it at 60 frames a second, and draws notifications in the corner.
 *
 * Scripted runs, for testing without touching the keyboard:
 *   STARTER_SIM_SCRIPT="r1 down right hold:right:800 touch:0.2,0.5 shot:a.ppm quit"
 * Steps, in order: a button name (up down left right cross circle square
 * triangle l1 r1 l2 r2 options l3 r3) presses it; hold:<button>:<ms> holds
 * it; touch:<x>,<y> touches the touchpad (0 to 1); wait:<ms> waits;
 * type:<text> types into the open keyboard (_ for a space), enter and cancel
 * close it; shot:<file.ppm> saves the screen; quit closes the simulator.
 * Steps are separated by spaces or new lines.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "pad.hpp"
#include "screen.hpp"
#include "sim.hpp"

#include <SDL.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace starter::sim
{
namespace
{
SDL_Window *g_window = nullptr;
SDL_Renderer *g_renderer = nullptr;
SDL_Texture *g_texture = nullptr;

// The last notification, shown in the corner for 4 seconds.
char g_note[256];
std::uint64_t g_note_until = 0;

[[noreturn]] void fail(const char *what) noexcept
{
    fprintf(stderr, "simulator: %s: %s\n", what, SDL_GetError());
    exit(1);
}

void open_window() noexcept
{
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
        fail("no display (see docs/13-troubleshooting.md)");
    g_window = SDL_CreateWindow("Hello PS5 - PC simulator", SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_RESIZABLE);
    if (!g_window)
        fail("can't open a window");
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_renderer && !(g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE)))
        fail("can't draw in the window");
    // The app always draws 1920 x 1080; the window shows it scaled to fit.
    SDL_RenderSetLogicalSize(g_renderer, screen::width, screen::height);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    // 0xAABBGGRR in memory is R, G, B, A bytes: SDL's ABGR8888.
    g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING,
                                  screen::width, screen::height);
    if (!g_texture)
        fail("can't make the screen texture");
}

// A notification, like the PS5's, in the top right corner.
void draw_note(screen::Canvas &c) noexcept
{
    using screen::Color;
    const float tw = screen::Canvas::text_width(g_note, font::Face::medium, 20);
    const int w = static_cast<int>(tw) + 48 + 28, h = 64;
    const int x = screen::width - 40 - w, y = 40;
    c.round_rect(x, y, w, h, 14, Color::surface2);
    c.round_frame(x, y, w, h, 14, 2, Color::line2);
    c.circle(static_cast<float>(x + 30), static_cast<float>(y + h / 2), 6, Color::accent);
    c.text(static_cast<float>(x + 48), static_cast<float>(y + 39), g_note, font::Face::medium, 20,
           Color::text);
}

// ---- Scripted input -------------------------------------------------------
struct Script
{
    char text[2048];
    char *next = nullptr;     // the step after the current one
    std::uint64_t until = 0;  // the current step ends then
    bool active = false;
};
Script g_script;

unsigned button_named(const char *name) noexcept
{
    static const struct
    {
        const char *name;
        unsigned bit;
    } names[] = {
        {"up", pad::up},         {"down", pad::down},       {"left", pad::left},
        {"right", pad::right},   {"cross", pad::cross},     {"circle", pad::circle},
        {"square", pad::square}, {"triangle", pad::triangle}, {"l1", pad::l1},
        {"r1", pad::r1},         {"l2", pad::l2},           {"r2", pad::r2},
        {"options", pad::options}, {"l3", pad::l3},         {"r3", pad::r3},
    };
    for (const auto &n : names)
        if (!strcmp(name, n.name))
            return n.bit;
    return 0;
}

void start_script() noexcept
{
    const char *s = getenv("STARTER_SIM_SCRIPT");
    if (!s || !*s)
        return;
    snprintf(g_script.text, sizeof g_script.text, "%s", s);
    g_script.next = g_script.text;
    g_script.active = true;
}

// Runs the script: each step holds its input for a while, then lets go for a
// moment, so the app sees separate presses. A "shot" is saved once the frame
// after the previous step has been drawn. Returns a shot's path when one is due;
// redraw is set when a step changed the screen without a button (typing).
const char *script_step(std::uint64_t now, bool &quit, bool &redraw) noexcept
{
    static char shot[512];
    static bool releasing = false;
    if (!g_script.active || now < g_script.until)
        return nullptr;
    script_hold(0, false, 0, 0);
    if (!releasing && g_script.until)
    {
        releasing = true;
        g_script.until = now + 90; // let go before the next step
        return nullptr;
    }
    releasing = false;

    // The next word.
    char *word = g_script.next;
    while (*word && isspace(static_cast<unsigned char>(*word))) // spaces or new lines
        ++word;
    if (!*word)
    {
        g_script.active = false;
        return nullptr;
    }
    char *end = word;
    while (*end && !isspace(static_cast<unsigned char>(*end)))
        ++end;
    g_script.next = *end ? end + 1 : end;
    *end = '\0';

    int ms = 0;
    float x = 0, y = 0;
    char name[32];
    if (!strcmp(word, "quit"))
        quit = true;
    else if (!strncmp(word, "type:", 5) || !strcmp(word, "enter") || !strcmp(word, "cancel"))
    {
        if (word[0] == 't')
            keyboard_type(word + 5);
        else
            keyboard_finish(word[0] == 'e');
        redraw = true;
        g_script.until = now + 60;
    }
    else if (sscanf(word, "wait:%d", &ms) == 1)
        g_script.until = now + static_cast<std::uint64_t>(ms);
    else if (sscanf(word, "touch:%f,%f", &x, &y) == 2)
    {
        script_hold(0, true, x, y);
        g_script.until = now + 150;
    }
    else if (sscanf(word, "hold:%31[a-z0-9]:%d", name, &ms) == 2 && button_named(name))
    {
        script_hold(button_named(name), false, 0, 0);
        g_script.until = now + static_cast<std::uint64_t>(ms);
    }
    else if (!strncmp(word, "shot:", 5))
    {
        snprintf(shot, sizeof shot, "%s", word + 5);
        g_script.until = now; // nothing to hold
        return shot;
    }
    else if (button_named(word))
    {
        script_hold(button_named(word), false, 0, 0);
        g_script.until = now + 60;
    }
    else
        fprintf(stderr, "simulator: unknown script step '%s'\n", word);
    return nullptr;
}
} // namespace

SDL_Renderer *renderer() noexcept
{
    return g_renderer;
}
} // namespace starter::sim

namespace starter::screen
{
void notify(std::string_view message) noexcept
{
    printf("notify: %.*s\n", static_cast<int>(message.size()), message.data());
    fflush(stdout);
    snprintf(sim::g_note, sizeof sim::g_note, "%.*s", static_cast<int>(message.size()),
             message.data());
    for (char *p = sim::g_note; *p; ++p) // one line, like the PS5's short form
        if (*p == '\n')
            *p = ' ';
    sim::g_note_until = now_ms() + 4000;
}

[[noreturn]] void run(Update update, Draw draw) noexcept
{
    sim::open_window();
    sim::start_script();
    Canvas canvas;
    bool dirty = true, note_shown = false, keyboard_shown = false, quit = false;
    const char *pending_shot = nullptr;
    (void)update();

    for (;;)
    {
        const std::uint64_t frame_start = now_ms();
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            // The keyboard first: while it's open, Esc cancels it rather than
            // closing the simulator.
            if (sim::keyboard_event(event))
            {
                dirty = true;
                continue;
            }
            if (event.type == SDL_QUIT ||
                (event.type == SDL_KEYDOWN && !event.key.repeat && // a fresh press, not a held one
                 event.key.keysym.scancode == SDL_SCANCODE_ESCAPE))
                quit = true;
            if (event.type == SDL_WINDOWEVENT)
                dirty = true; // resized or uncovered: show the frame again
            sim::pad_event(event);
        }
        bool script_redraw = false;
        if (const char *shot = sim::script_step(frame_start, quit, script_redraw))
            pending_shot = shot;
        if (script_redraw)
            dirty = true;
        if (quit && !pending_shot)
        {
            SDL_Quit();
            exit(0);
        }

        if (update())
            dirty = true;
        const bool note_now = sim::g_note[0] && now_ms() < sim::g_note_until;
        const bool keyboard_now = sim::keyboard_active();
        if (note_now != note_shown || keyboard_now != keyboard_shown)
            dirty = true;
        if (dirty)
        {
            draw(canvas);
            note_shown = note_now;
            keyboard_shown = keyboard_now;
            if (keyboard_now)
                sim::keyboard_draw(canvas);
            if (note_now)
                sim::draw_note(canvas);
            SDL_UpdateTexture(sim::g_texture, nullptr, pixels(), width * 4);
            dirty = false;
        }
        if (pending_shot)
        {
            printf(write_ppm(pending_shot) ? "simulator: saved %s\n" : "simulator: can't write %s\n",
                   pending_shot);
            fflush(stdout);
            pending_shot = nullptr;
        }

        SDL_SetRenderDrawColor(sim::g_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sim::g_renderer);
        SDL_RenderCopy(sim::g_renderer, sim::g_texture, nullptr, nullptr);
        SDL_RenderPresent(sim::g_renderer);
        // About 60 frames a second, like the PS5. Where vsync works, presenting
        // already took most of the frame and this waits little or not at all.
        const std::uint64_t spent = now_ms() - frame_start;
        if (spent < 16)
            SDL_Delay(static_cast<Uint32>(16 - spent));
    }
}
} // namespace starter::screen
