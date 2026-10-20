# Hello PS5

The starter app: four pages (Play, Notes, Settings, About), switched with L1 and R1.
Start reading at [`src/hello.cpp`](src/hello.cpp).

```bash
app/build.sh                    # build/app/PPSA99901 and its ZIP
app/build.sh deploy <PS5 IP>    # and copy it to /data/homebrew over FTP
app/sim/run.sh                  # it live in a window on your PC (the simulator)
app/preview/run.sh              # its screens as PNGs on your PC
```

| | |
| --- | --- |
| `src/hello.cpp` | runs the app: the page table, the tab bar, saving, the service |
| `src/hello.hpp` | the state and what the pages share |
| `src/page_*.cpp`, `src/pages.hpp` | the pages: Play, Notes (a list and the keyboard), Settings (a menu), About |
| `src/main.cpp` | startup |
| `src/screen.*`, `src/font.*` | drawing and text |
| `src/pad.*` | the DualSense |
| `src/keyboard.*` | the PS5's on-screen keyboard |
| `src/image.*`, `src/stb_image.h` | pictures (PNG, JPEG) |
| `src/sound.*` | sound effects, made in code |
| `stubs/` | link-only stubs for system functions the SDK lacks (the keyboard's) |
| `assets/` | your own files, if you make the folder: `/app0/assets` on the console |
| `src/link.*` | talking to the service payload |
| `sce_sys/` | `param.json` and the icon |
| `fonts/` | Inter and JetBrains Mono (SIL Open Font License) |
| `src/pad_input.*` | buttons to presses with repeats, shared by the PS5 and the simulator |
| `src/paths.hpp` | `/app0` and `/data`, or their simulator folders |
| `sim/` | the PC simulator: the window (`screen_sim.cpp`), keyboard, mouse and controller (`pad_sim.cpp`), the on-screen keyboard (`keyboard_sim.cpp`), sound (`sound_sim.cpp`), and the Windows controller bridge (`dualsense_bridge.py`) |
| `preview/` | the PC preview |

The guide: [chapter 5](../docs/05-first-app.md) onwards.
