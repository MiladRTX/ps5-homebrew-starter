/*
 * PS5 Homebrew Starter - the PC simulator's controller: the keyboard, the
 * mouse as the touchpad, and a game controller, all turned into the same
 * buttons as the DualSense. The press-and-repeat timing is pad_input.cpp's,
 * shared with the PS5.
 *
 * A controller comes from one of two places:
 *  - SDL on this PC, when it can see it (Linux, or a controller passed to WSL);
 *  - the controller bridge (dualsense_bridge.py), which reads it on Windows and
 *    sends its state here over 127.0.0.1:9278. WSL can't see a controller
 *    connected to Windows, so app/sim/run.sh starts the bridge there.
 *
 *   Arrow keys      D-pad            X / Enter       Cross
 *   C / Backspace   Circle           Z               Square
 *   V               Triangle         Q / E           L1 / R1
 *   1 / 3           L2 / R2          Tab             OPTIONS
 *   F / G           L3 / R3          mouse drag      touchpad
 *   Esc             close the simulator
 * While the on-screen keyboard is open, the keys type instead (keyboard_sim.cpp).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "pad.hpp"
#include "pad_input.hpp"
#include "screen.hpp"
#include "sim.hpp"

#include <SDL.h>

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

namespace starter::pad
{
namespace
{
// A controller's state, from SDL here or from the bridge: SDL's own button
// and axis numbers, so both go through the same mapping below.
struct ControllerState
{
    bool present;
    std::uint32_t buttons; // bit n: SDL_CONTROLLER_BUTTON n is held
    std::int16_t axes[SDL_CONTROLLER_AXIS_MAX];
    Touch touch;
};

SDL_GameController *g_controller = nullptr;
Touch g_touch{};

// Scripted input (screen_sim.cpp), on top of the real input.
unsigned g_script_buttons = 0;
Touch g_script_touch{};

// ---- The keyboard -----------------------------------------------------------
unsigned keyboard() noexcept
{
    // By the letter on the key, whatever the keyboard layout: Z is Square on
    // a QWERTZ or AZERTY keyboard too.
    static const struct
    {
        SDL_Keycode key;
        unsigned bit;
    } keys[] = {
        {SDLK_UP, up},           {SDLK_DOWN, down},
        {SDLK_LEFT, left},       {SDLK_RIGHT, right},
        {SDLK_x, cross},         {SDLK_RETURN, cross},
        {SDLK_c, circle},        {SDLK_BACKSPACE, circle},
        {SDLK_z, square},        {SDLK_v, triangle},
        {SDLK_q, l1},            {SDLK_e, r1},
        {SDLK_1, l2},            {SDLK_3, r2},
        {SDLK_TAB, options},     {SDLK_f, l3},
        {SDLK_g, r3},
    };
    const Uint8 *state = SDL_GetKeyboardState(nullptr);
    unsigned held = 0;
    for (const auto &k : keys)
        if (state[SDL_GetScancodeFromKey(k.key)])
            held |= k.bit;
    return held;
}

// ---- A controller's state -> DualSense buttons ---------------------------------
unsigned map(const ControllerState &c) noexcept
{
    if (!c.present)
        return 0;
    static const struct
    {
        SDL_GameControllerButton button;
        unsigned bit;
    } buttons[] = {
        {SDL_CONTROLLER_BUTTON_DPAD_UP, up},       {SDL_CONTROLLER_BUTTON_DPAD_DOWN, down},
        {SDL_CONTROLLER_BUTTON_DPAD_LEFT, left},   {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, right},
        // SDL names buttons by position, Xbox style: A is the bottom one, Cross.
        {SDL_CONTROLLER_BUTTON_A, cross},          {SDL_CONTROLLER_BUTTON_B, circle},
        {SDL_CONTROLLER_BUTTON_X, square},         {SDL_CONTROLLER_BUTTON_Y, triangle},
        {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, l1},  {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, r1},
        {SDL_CONTROLLER_BUTTON_START, options},    {SDL_CONTROLLER_BUTTON_LEFTSTICK, l3},
        {SDL_CONTROLLER_BUTTON_RIGHTSTICK, r3},
    };
    unsigned held = 0;
    for (const auto &b : buttons)
        if (c.buttons & (1U << b.button))
            held |= b.bit;
    if (c.axes[SDL_CONTROLLER_AXIS_TRIGGERLEFT] > 16000)
        held |= l2;
    if (c.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] > 16000)
        held |= r2;
    // The left stick as a D-pad, exactly as on the PS5 (-32768..32767 -> -128..127).
    held |= input::stick_as_dpad(c.axes[SDL_CONTROLLER_AXIS_LEFTX] / 256,
                                 c.axes[SDL_CONTROLLER_AXIS_LEFTY] / 256);
    return held;
}

// ---- A controller SDL can see on this PC ---------------------------------------
void open_first_controller() noexcept
{
    for (int i = 0; i < SDL_NumJoysticks() && !g_controller; ++i)
        if (SDL_IsGameController(i))
            g_controller = SDL_GameControllerOpen(i);
}

ControllerState local() noexcept
{
    ControllerState c{};
    if (!g_controller)
        return c;
    c.present = true;
    for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b)
        if (SDL_GameControllerGetButton(g_controller, static_cast<SDL_GameControllerButton>(b)))
            c.buttons |= 1U << b;
    for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX; ++a)
        c.axes[a] = SDL_GameControllerGetAxis(g_controller, static_cast<SDL_GameControllerAxis>(a));
    if (SDL_GameControllerGetNumTouchpads(g_controller) > 0)
    {
        Uint8 down = 0;
        float x = 0, y = 0, pressure = 0;
        if (SDL_GameControllerGetTouchpadFinger(g_controller, 0, 0, &down, &x, &y, &pressure) == 0 && down)
            c.touch = Touch{true, x, y};
    }
    return c;
}

// ---- The controller bridge ---------------------------------------------------
// dualsense_bridge.py sends 26-byte packets, little-endian: "PS5B", the
// buttons (u32), six axes (i16), the finger down (u8), a spare byte, and the
// touchpad's x and y (u16, 0..65535).
constexpr std::size_t packet_size = 26;
constexpr std::uint64_t bridge_timeout_ms = 500; // nothing for that long: let go

int g_listen = -1, g_client = -1;
unsigned char g_buffer[packet_size * 8];
std::size_t g_buffered = 0;
ControllerState g_remote{};
std::uint64_t g_remote_at = 0;

int bridge_port() noexcept
{
    const char *env = getenv("STARTER_SIM_BRIDGE_PORT");
    const int port = env ? atoi(env) : 0;
    return port > 0 && port < 65536 ? port : 9278;
}

void bridge_listen() noexcept
{
    // Close-on-exec: the PC service the app starts must not inherit the
    // port, or it would hold it after the simulator closes.
    const int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return;
    const int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<unsigned short>(bridge_port()));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // this PC only
    if (bind(fd, reinterpret_cast<sockaddr *>(&a), sizeof a) != 0 || listen(fd, 1) != 0)
    {
        printf("simulator: port %d is busy, no controller bridge (keyboard only)\n", bridge_port());
        close(fd);
        return;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    g_listen = fd;
}

std::uint16_t u16(const unsigned char *p) noexcept
{
    return static_cast<std::uint16_t>(p[0] | p[1] << 8);
}

void bridge_poll() noexcept
{
    if (g_listen < 0)
        return;
    if (g_client < 0)
    {
        g_client = accept4(g_listen, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (g_client < 0)
            return;
        g_buffered = 0;
        printf("simulator: controller bridge connected\n");
        fflush(stdout);
    }
    for (;;)
    {
        const ssize_t n = recv(g_client, g_buffer + g_buffered, sizeof g_buffer - g_buffered, 0);
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
        {
            printf("simulator: controller bridge disconnected\n");
            fflush(stdout);
            close(g_client);
            g_client = -1;
            g_remote = ControllerState{};
            return;
        }
        if (n < 0)
            break;
        g_buffered += static_cast<std::size_t>(n);
        // Whole packets; a byte that isn't the start of one is skipped.
        std::size_t at = 0;
        while (g_buffered - at >= packet_size)
        {
            const unsigned char *p = g_buffer + at;
            if (memcmp(p, "PS5B", 4) != 0)
            {
                ++at;
                continue;
            }
            ControllerState c{};
            c.present = true;
            c.buttons = static_cast<std::uint32_t>(u16(p + 4)) | static_cast<std::uint32_t>(u16(p + 6)) << 16;
            for (int i = 0; i < 6 && i < SDL_CONTROLLER_AXIS_MAX; ++i)
                c.axes[i] = static_cast<std::int16_t>(u16(p + 8 + 2 * i));
            if (p[20])
                c.touch = Touch{true, u16(p + 22) / 65535.0f, u16(p + 24) / 65535.0f};
            g_remote = c;
            g_remote_at = screen::now_ms();
            at += packet_size;
        }
        memmove(g_buffer, g_buffer + at, g_buffered - at);
        g_buffered -= at;
    }
}

ControllerState remote() noexcept
{
    if (g_client < 0 || screen::now_ms() - g_remote_at > bridge_timeout_ms)
        return ControllerState{};
    return g_remote;
}

// ---- The touchpad -----------------------------------------------------------------
// A script's touch, a controller's touchpad, or the mouse dragged over the window.
Touch touchpad(const ControllerState &a, const ControllerState &b) noexcept
{
    if (g_script_touch.down)
        return g_script_touch;
    if (a.touch.down)
        return a.touch;
    if (b.touch.down)
        return b.touch;
    int mx = 0, my = 0;
    if (!(SDL_GetMouseState(&mx, &my) & SDL_BUTTON_LMASK) || !sim::renderer())
        return Touch{false, 0, 0};
    float lx = 0, ly = 0; // window pixels -> the app's 1920 x 1080
    SDL_RenderWindowToLogical(sim::renderer(), mx, my, &lx, &ly);
    const float x = lx / (screen::width - 1), y = ly / (screen::height - 1);
    if (x < 0 || x > 1 || y < 0 || y > 1)
        return Touch{false, 0, 0}; // in the black bars around the picture
    return Touch{true, x, y};
}
} // namespace

bool init() noexcept
{
    // The keyboard and the mouse always work; a controller is a bonus.
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0)
        open_first_controller();
    bridge_listen();
    return true;
}

unsigned poll() noexcept
{
    bridge_poll();
    if (sim::keyboard_active())
    {
        // The keyboard is open: the keys type, and the controller is the
        // keyboard's, as on the PS5.
        input::suspend();
        g_touch = Touch{false, 0, 0};
        return input::take_presses();
    }
    const ControllerState here = local(), bridged = remote();
    input::sample(keyboard() | map(here) | map(bridged) | g_script_buttons);
    g_touch = touchpad(here, bridged);
    return input::take_presses();
}

Touch touch() noexcept
{
    return g_touch;
}
} // namespace starter::pad

namespace starter::sim
{
void pad_event(const SDL_Event &event) noexcept
{
    if (event.type == SDL_CONTROLLERDEVICEADDED && !pad::g_controller)
    {
        pad::g_controller = SDL_GameControllerOpen(event.cdevice.which);
        if (pad::g_controller)
            printf("simulator: controller connected: %s\n", SDL_GameControllerName(pad::g_controller));
    }
    if (event.type == SDL_CONTROLLERDEVICEREMOVED && pad::g_controller &&
        SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad::g_controller)) == event.cdevice.which)
    {
        SDL_GameControllerClose(pad::g_controller);
        pad::g_controller = nullptr;
        pad::open_first_controller(); // another one, if there's one
    }
}

void script_hold(unsigned buttons, bool touch, float x, float y) noexcept
{
    pad::g_script_buttons = buttons;
    pad::g_script_touch = pad::Touch{touch, x, y};
}
} // namespace starter::sim
