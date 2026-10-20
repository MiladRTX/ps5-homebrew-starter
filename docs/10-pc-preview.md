# 10. Testing on your PC

Copying the app to the console and opening it takes a minute every time. Your
PC can do most of that work in seconds, with two tools:

| | What it does | Command |
| --- | --- | --- |
| **The simulator** | runs the app live in a window: keyboard, mouse, controller, the service | `app/sim/run.sh` |
| **The preview** | saves pictures of chosen screens | `app/preview/run.sh` |

Both use the app's own code. What you see is what the TV shows, pixel for pixel.

## The simulator

```bash
app/sim/run.sh
```

A window opens with Hello PS5 running, and you use it like on the console:

| PS5 | PC |
| --- | --- |
| D-pad / left stick | arrow keys |
| Cross | X or Enter |
| Circle | C or Backspace |
| Square / Triangle | Z / V |
| L1 / R1 | Q / E |
| L2 / R2 | 1 / 3 |
| OPTIONS | Tab |
| L3 / R3 | F / G |
| Touchpad | drag with the mouse |
| (close the app) | Esc, or close the window |

When the app opens the on-screen keyboard (Triangle on the Notes page), a
text box takes its place and your keys type: **Enter** is OK, **Esc** cancels.

Your DualSense works too, buttons, sticks and touchpad, over Bluetooth or USB
(see *A real controller* below).

The loop is now: **edit, `app/sim/run.sh`, try it**. Building takes a few
seconds. Once something works in the simulator, build the real app and check
it on the console.

### What's real in it

Most of the app runs exactly as on the PS5, not imitated:

- **Drawing:** the same code. The window just shows the frame the PS5 would.
- **Buttons:** the same press-and-repeat timing (`app/src/pad_input.cpp` is
  shared). Holding a key repeats exactly like holding a button.
- **The service:** `service.c` is built as a PC program too. The app starts it,
  asks it for `/status`, and replaces an older one, the same code as on the
  console. Its page is at `http://localhost:8080`, and like on the PS5, it
  keeps running after the window closes: `app/sim/run.sh stop` stops it.
  Changed `service.c` without raising its version? Then the app won't replace
  the one that's running (it isn't older), so stop it before running the
  simulator again.
- **Files:** `build/sim/data` stands in for `/data`, and `build/sim/app0`
  for the app's own folder. The saved settings are in
  `build/sim/data/ps5-starter/hello-app.txt`.
- **Notifications** pop up in the window's corner, and in the terminal.
- **Sound:** the same effects, made by the same code, out of your PC's speakers.

Only a few things are swapped for PC versions, all in
[`app/sim`](../app/sim) and two small spots in `app/src`:

| PS5 | PC |
| --- | --- |
| `screen.cpp` shows frames on the TV | `screen_sim.cpp` shows them in a window |
| `pad.cpp` reads the DualSense | `pad_sim.cpp` reads keyboard, mouse and controller |
| `keyboard.cpp` opens the PS5's keyboard | `keyboard_sim.cpp` shows a text box |
| `sound.cpp` plays through the console | `sound_sim.cpp` plays through SDL |
| `start_payload()` sends the ELF to port 9021 | it starts the PC service program |
| `/app0`, `/data` | `build/sim/app0`, `build/sim/data` ([`paths.hpp`](../app/src/paths.hpp)) |

In your own code, write file paths with the two macros from `paths.hpp`, and
they work in both places:

```cpp
#include "paths.hpp"
FILE *f = fopen(STARTER_DATA "/my-app/settings.txt", "r");
```

### Trying an update

The simulator is the easiest place to watch chapter 9's update flow:

1. Run the simulator and close it. The service (1.0) keeps running.
2. Raise `SERVICE_VERSION` in `payload/service/service_version.h` to `1.1`.
3. Run the simulator again and open **About**: *Updating the service from
   1.0 to 1.1...*, and a moment later it says 1.1.
4. Put it back to `1.0` when you're done, and `app/sim/run.sh stop`.

### Scripted runs

The simulator can press the buttons itself, which makes a repeatable test of
a whole flow:

```bash
STARTER_SIM_SCRIPT="r1 r1 down right shot:settings.ppm quit" app/sim/run.sh
```

That opens Settings (R1 twice), chooses the second row, changes it, saves a
picture of the screen in `build/sim`, and closes. Steps run in order:

| Step | Does |
| --- | --- |
| `cross`, `r1`, `up`, ... | presses a button |
| `hold:right:800` | holds it for 800 ms (repeats included) |
| `touch:0.5,0.5` | touches the touchpad (0 to 1 across and down) |
| `wait:500` | waits half a second |
| `type:Buy_milk` | types into the open keyboard (`_` is a space) |
| `enter`, `cancel` | closes the keyboard with OK, or without |
| `shot:name.ppm` | saves the screen |
| `quit` | closes the simulator |

Add `SDL_VIDEODRIVER=offscreen` in front and no window opens at all, which is
handy for checking a change without looking.

### A real controller

Turn your DualSense on, connected to your PC over Bluetooth or a USB cable,
and use it in the simulator like on the console, touchpad included.

**In WSL** there's a catch: WSL can't see controllers connected to Windows.
So `app/sim/run.sh` starts a small **controller bridge** on the Windows side,
[`app/sim/dualsense_bridge.py`](../app/sim/dualsense_bridge.py). It reads the
controller where Windows sees it and sends its state to the simulator, about
120 times a second, over a connection that stays on your PC (`127.0.0.1`,
port 9278). You don't start or stop it yourself:

```
Controller bridge: started (build/sim/bridge.log). Turn your controller on any time.
```

- It needs **Python on Windows** ([python.org](https://www.python.org)), and
  nothing else to install: just Python's own modules.
- It reads the controller with **SDL2.dll**, the official Windows build of the
  same SDL2 the simulator uses. The first run downloads it once, from SDL's
  GitHub releases, and checks it against its known SHA-256 before using it.
  It's kept in `%LOCALAPPDATA%\ps5-starter`.
- It closes by itself when the simulator closes. `build/sim/bridge.log` says
  what it found:

  ```
  bridge: connected to the simulator
  bridge: DualSense Wireless Controller connected
  ```

- To check it without the simulator, in PowerShell:
  `python $env:LOCALAPPDATA\ps5-starter\dualsense_bridge.py --sdl $env:LOCALAPPDATA\ps5-starter\SDL2.dll --check`
- `STARTER_SIM_CONTROLLER=0 app/sim/run.sh` runs without it.

> Over Bluetooth, a DualSense only reports its touchpad in an extended mode,
> which the bridge switches on. The controller stays in that mode until you
> turn it off. If another Windows program stops reading it afterwards, turn
> the controller off and on again.

**On Linux** there's no bridge: SDL sees a connected controller directly,
a DualSense and its touchpad included.

### What it can't show you

- **Speed.** Your PC draws much faster than the PS5. Something smooth in the
  simulator can be slow on the console.
- **The console itself:** its system menus, its keyboard and real
  notifications, rest mode, memory limits, and any PS5 function you add later.
  A new `sce...` call needs a PC stand-in, like the service's in
  `payload/service/sim_stubs.c`, and only the console shows whether the call
  really works there.

So: build and try in the simulator, then **always** check on the console
before you share anything.

## The preview

```bash
app/preview/run.sh
```

```
1_play_start.png
2_play.png
3_notes.png
4_settings.png
5_about_updating.png
```

They're in `build/preview/`. Open them from Windows with `explorer.exe build/preview`
from WSL.

![The preview's second shot](images/hello-ps5.png)

The preview doesn't run the app: it sets a state by hand and takes a picture,
in [`app/preview/preview.cpp`](../app/preview/preview.cpp):

```cpp
// After playing a bit: box moved and recoloured, the service running.
s.box_x = 420;
s.box_y = 380;
s.colour = 1;
strcpy(s.last_button, "Cross");
s.service_up = true;
...
shot(argv[2], "2_play");
```

That's what it's good at: states that are tedious to reach by hand, like an
empty list, a very long name, an error message, or the "older service" case
from chapter 9. Add a shot for each screen and state you want to keep an eye
on, and look through them after every change. `run.sh` compiles the app's
files from a list (not `main.cpp` or the controller's); when you add a new
`.cpp`, add it there.

The Notes shot presses Down the same way the console would, through the
page's own code, so the list in the picture has really scrolled:

```cpp
for (int i = 0; i < 9; ++i)
    (void)starter::hello::notes_update(starter::pad::down);
```

Next: [Make it yours](11-make-it-yours.md)
