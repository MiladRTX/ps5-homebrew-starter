"""PS5 Homebrew Starter - the controller bridge for the PC simulator.

WSL can't see a controller connected to Windows: never over Bluetooth, and over
USB only with extra tools. This small program runs on Windows, where the
controller is. It reads it with SDL2, the same library the simulator uses, and
sends its buttons, sticks, triggers and touchpad to the simulator about 120
times a second. app/sim/run.sh starts it by itself, and it ends when the
simulator closes.

    python dualsense_bridge.py --sdl C:\\path\\to\\SDL2.dll [--port 9278]

It needs only Python itself (its own modules) and SDL2.dll.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import ctypes
import select
import socket
import struct
import sys
import time

SDL_INIT_GAMECONTROLLER = 0x00002000
BUTTON_COUNT = 21  # SDL_CONTROLLER_BUTTON_MAX: A, B, X, Y, back, guide, start, sticks, ...
AXIS_COUNT = 6     # left x/y, right x/y, left/right trigger

# What the simulator receives, little-endian, 26 bytes (app/sim/pad_sim.cpp):
# "PS5B", the buttons as bits (bit n = SDL button n), the six axes, the
# touchpad (finger down, then x and y from 0 to 65535).
PACKET = struct.Struct("<4sI6hBxHH")
MAGIC = b"PS5B"
RATE = 120  # packets a second


def log(text):
    print(f"bridge: {text}", flush=True)


def load_sdl(path):
    c = ctypes
    sdl = c.CDLL(path)
    api = {
        "SDL_Init": ([c.c_uint32], c.c_int),
        "SDL_GetError": ([], c.c_char_p),
        "SDL_SetHint": ([c.c_char_p, c.c_char_p], c.c_int),
        "SDL_PumpEvents": ([], None),
        "SDL_GameControllerUpdate": ([], None),
        "SDL_NumJoysticks": ([], c.c_int),
        "SDL_IsGameController": ([c.c_int], c.c_int),
        "SDL_GameControllerOpen": ([c.c_int], c.c_void_p),
        "SDL_GameControllerClose": ([c.c_void_p], None),
        "SDL_GameControllerName": ([c.c_void_p], c.c_char_p),
        "SDL_GameControllerGetAttached": ([c.c_void_p], c.c_int),
        "SDL_GameControllerGetButton": ([c.c_void_p, c.c_int], c.c_uint8),
        "SDL_GameControllerGetAxis": ([c.c_void_p, c.c_int], c.c_int16),
        "SDL_GameControllerGetNumTouchpads": ([c.c_void_p], c.c_int),
        "SDL_GameControllerGetTouchpadFinger": (
            [c.c_void_p, c.c_int, c.c_int, c.POINTER(c.c_uint8), c.POINTER(c.c_float),
             c.POINTER(c.c_float), c.POINTER(c.c_float)], c.c_int),
    }
    for name, (args, result) in api.items():
        fn = getattr(sdl, name)
        fn.argtypes = args
        fn.restype = result
    return sdl


def open_controller(sdl):
    for i in range(sdl.SDL_NumJoysticks()):
        if sdl.SDL_IsGameController(i):
            pad = sdl.SDL_GameControllerOpen(i)
            if pad:
                name = (sdl.SDL_GameControllerName(pad) or b"a controller").decode(errors="replace")
                log(f"{name} connected")
                return pad
    return None


def read(sdl, pad):
    """The controller's state as a packet; all released without a controller."""
    buttons, axes, touch = 0, [0] * AXIS_COUNT, (0, 0, 0)
    if pad:
        for b in range(BUTTON_COUNT):
            if sdl.SDL_GameControllerGetButton(pad, b):
                buttons |= 1 << b
        axes = [sdl.SDL_GameControllerGetAxis(pad, a) for a in range(AXIS_COUNT)]
        if sdl.SDL_GameControllerGetNumTouchpads(pad) > 0:
            down = ctypes.c_uint8()
            x, y, pressure = ctypes.c_float(), ctypes.c_float(), ctypes.c_float()
            if sdl.SDL_GameControllerGetTouchpadFinger(
                    pad, 0, 0, ctypes.byref(down), ctypes.byref(x), ctypes.byref(y),
                    ctypes.byref(pressure)) == 0 and down.value:
                clamp = lambda v: max(0, min(65535, int(v * 65535)))
                touch = (1, clamp(x.value), clamp(y.value))
    return PACKET.pack(MAGIC, buttons, *axes, *touch)


def connect(port, seconds):
    """The simulator, retried while it starts up."""
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        try:
            return socket.create_connection(("127.0.0.1", port), timeout=1)
        except OSError:
            time.sleep(0.25)
    return None


def closed(sock):
    """True once the simulator has closed the connection."""
    ready, _, _ = select.select([sock], [], [], 0)
    if not ready:
        return False
    try:
        return sock.recv(64) == b""
    except OSError:
        return True


def main():
    parser = argparse.ArgumentParser(description="Sends a controller to the PS5 Homebrew Starter simulator.")
    parser.add_argument("--sdl", required=True, help="path to SDL2.dll")
    parser.add_argument("--port", type=int, default=9278)
    parser.add_argument("--check", action="store_true",
                        help="show which controller it finds, then stop")
    args = parser.parse_args()

    sdl = load_sdl(args.sdl)
    # No window here, so read the controller in the background too.
    sdl.SDL_SetHint(b"SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", b"1")
    # A DualSense over Bluetooth only reports its touchpad in its extended
    # mode. Once switched on, it stays on until the controller is turned off.
    sdl.SDL_SetHint(b"SDL_JOYSTICK_HIDAPI_PS5_RUMBLE", b"1")
    if sdl.SDL_Init(SDL_INIT_GAMECONTROLLER) != 0:
        log(f"SDL couldn't start: {sdl.SDL_GetError().decode(errors='replace')}")
        return 1

    if args.check:
        for _ in range(20):  # give a Bluetooth controller a moment to show up
            sdl.SDL_PumpEvents()
            sdl.SDL_GameControllerUpdate()
            time.sleep(0.05)
        pad = open_controller(sdl)
        if not pad:
            log("no controller found")
            return 1
        touchpads = sdl.SDL_GameControllerGetNumTouchpads(pad)
        log(f"touchpad: {'yes' if touchpads else 'no'}")
        return 0

    sock = connect(args.port, 30)
    if not sock:
        log(f"no simulator on port {args.port}")
        return 1
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    log("connected to the simulator")

    pad, next_look = None, 0.0
    while True:
        sdl.SDL_PumpEvents()
        sdl.SDL_GameControllerUpdate()
        if pad and not sdl.SDL_GameControllerGetAttached(pad):
            log("controller disconnected")
            sdl.SDL_GameControllerClose(pad)
            pad = None
        if not pad and time.monotonic() >= next_look:
            pad = open_controller(sdl)
            if not pad and next_look == 0.0:
                log("no controller yet: turn one on, or connect it")
            next_look = time.monotonic() + 1
        try:
            sock.sendall(read(sdl, pad))
        except OSError:
            break
        if closed(sock):
            break
        time.sleep(1 / RATE)
    log("the simulator closed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
