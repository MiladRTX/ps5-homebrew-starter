# 6. App basics: drawing, input, files

Hello PS5's code is in [`app/src`](../app/src): `hello.cpp` runs the app,
and each page has its own file (chapter 7 explains the pages). This chapter
goes through the basics, mostly with the **Play** page,
[`page_play.cpp`](../app/src/page_play.cpp), so you know where to change what.

## How the app runs

[`main.cpp`](../app/src/main.cpp) is short:

```cpp
starter::font::load(STARTER_APP0 "/assets/fonts/");
starter::pad::init();
starter::sound::init();   // the sound output (chapter 8)
starter::hello::load();
starter::screen::run(starter::hello::update, starter::hello::draw);
```

`STARTER_APP0` ([`paths.hpp`](../app/src/paths.hpp)) is `/app0`, the app's own
folder, on the console, and the simulator's stand-in for it on a PC (chapter 10).

`screen::run` never returns. It calls your two functions:

- **`update()`** about 60 times a second. Read the controller, change your
  state, and return `true` when the screen needs drawing again.
- **`draw(canvas)`** only after `update()` returned `true`. Draw the whole
  screen from your state.

Keeping them apart is the one rule to follow: **`update` changes things,
`draw` only looks at them.** It keeps the code easy to follow, and it's what
makes the PC preview (chapter 10) possible: the preview sets a state by hand
and calls `draw`.

Drawing happens on the CPU, and a full 1920x1080 frame takes a few
milliseconds. That's why `draw` only runs when something changed. A screen
that doesn't change costs nothing.

## The state

```cpp
struct State {
    Page page;              // which page is open
    float box_x, box_y;     // where the box is
    int size, speed, colour;
    char last_button[24];
    bool service_up;
    ...
};
```

All of the app's state in one struct, in [`hello.hpp`](../app/src/hello.hpp).
To add a feature, add its state here, change it in an `update`, and show it
in a `draw`.

## Drawing

The screen is 1920 x 1080 pixels; (0, 0) is the top left. Everything is drawn
on a `Canvas`, every frame from scratch:

```cpp
c.clear(Color::bg);
c.round_rect(x, y, w, h, radius, Color::surface);       // filled, rounded
c.round_frame(x, y, w, h, radius, 2, Color::line);      // the outline, 2 px
c.rect(x, y, w, h, Color::accent);
c.circle(cx, cy, r, Color::green);
c.polyline(points, n, thickness, Color::text);          // lines, graphs
```

Shapes are antialiased: edges are smooth, not jagged. Most take an opacity
(0 to 255) as a last argument.

**Text** is drawn on its **baseline**, the line the letters sit on, not its top:

```cpp
c.text(80, 118, "Hello PS5", Face::bold, 44, Color::text);
```

That's x = 80, baseline at y = 118, bold, 44 pixels. The faces are `regular`,
`medium`, `semibold` and `bold` (Inter), plus `mono_medium` and
`mono_semibold` (JetBrains Mono) for numbers that shouldn't jump around.
`Canvas::text_width()` measures text before drawing it, for centring.

**Colours** live in [`screen.hpp`](../app/src/screen.hpp) as `0xAABBGGRR`:
blue and red are swapped compared to the usual `#RRGGBB`. `#8ea2ff` is
`0xffffa28e`. Add your own there.

## The controller

```cpp
const unsigned pressed = pad::poll();
if (pressed & pad::cross) { ... }
```

`poll()` returns the buttons pressed since the last call, each press once.
Held directions (D-pad and left stick), L1/R1 and L2/R2 repeat, faster after a
moment, like a keyboard. So a held button moves the box steadily with no extra
code.

That timing lives in [`pad_input.cpp`](../app/src/pad_input.cpp), shared by
the PS5 and the PC simulator (chapter 10), so a held key in the simulator feels
exactly like a held button on the console.

The names are in [`pad.hpp`](../app/src/pad.hpp): `up down left right cross
circle square triangle l1 r1 l2 r2 options l3 r3`.

**The touchpad:**

```cpp
const pad::Touch t = pad::touch();
if (t.down) {
    s.box_x = play_x + t.x * play_w;   // t.x and t.y go from 0 to 1
    ...
}
```

The PS button belongs to the system. While its menu is open, `poll()` returns
nothing, so your app doesn't react to presses meant for the menu.

## Notifications

```cpp
screen::notify("Hello from Hello PS5!");
```

The same pop-up as the payload in chapter 2. Good for things that matter
outside the app. For feedback inside the app, Hello PS5 shows a short message
at the bottom of the screen instead (`say()`), which disappears after 2.5
seconds.

## Files

| Path | What | |
| --- | --- | --- |
| `/app0/...` | your app's own folder | read-only |
| `/data/...` | the console's storage | read and write, survives reboots and app updates |

Hello PS5 saves the box's position and its settings in
`/data/ps5-starter/hello-app.txt`. Whatever changes them calls `changed()`:

```cpp
void changed() noexcept {
    g_state.save_at_ms = screen::now_ms() + 1000;   // save a second later
}
```

and `update()` in `hello.cpp` writes the file once that second has passed.

Not on every press: holding a direction repeats ten times a second, and
writing the file each time would be wasted work. It saves one second after the
last change. `load()` reads the file at startup, and checks every value before
using it: a file can be old, damaged or edited by hand.

Give your app its own folder in `/data`, named after it.

## Don't block the frame loop

`update()` runs between frames. Anything slow in it, like a network request
that waits ten seconds, freezes the app for that long. Hello PS5 asks the
service for its status every 2 seconds, with a 1-second timeout, so even a
stuck service can't freeze it for long. For anything slower, use a thread.

## Try this

- Change the title in `hello.cpp`'s `draw()`.
- Change the box sizes or how far it moves: the `sizes` and `steps` tables in `hello.cpp`.
- Add a fourth box colour: raise `colour_count` in `hello.hpp`, add it to
  `colours` in `box_colour()` and its name in `page_settings.cpp`.
- Make **Circle** do something on the Play page: it's already detected, it
  just has no job yet.
- Add a second box that moves with the right stick. You'll need to read the
  stick in `pad.cpp`; look at how it reads the left one.

Next: [Menus and pages](07-menus-and-pages.md)
