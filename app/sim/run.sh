#!/usr/bin/env bash
# The app on your PC, live, in a window (docs/10-pc-preview.md). The app's own
# code, with a window instead of the TV, the keyboard, mouse or a controller
# instead of the DualSense, and the service running as a PC program.
#
#   app/sim/run.sh          build it and start it
#   app/sim/run.sh stop     stop the PC service it started (it keeps running
#                           after the window closes, like on the PS5)
#
# Needs SDL2 (tools/setup.sh installs it) and a display: Windows 11's WSL has
# one built in. Files: build/sim/app0 is the app's folder, build/sim/data the
# console's storage.
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
app=$(dirname -- "$here")
repo=$(dirname -- "$app")
out=$repo/build/sim

if [[ ${1:-} == stop ]]; then
    lock=$out/data/ps5-starter/service.lock
    pid=$(head -c 16 "$lock" 2>/dev/null | tr -dc 0-9 || true)
    # The pid only counts while a running service holds the file's lock: an
    # old pid may belong to some other program by now.
    if [[ -n $pid ]] && ! flock -n "$lock" true 2>/dev/null && kill "$pid" 2>/dev/null; then
        echo "Stopped the PC service (pid $pid)."
    # No lock file (build/ deleted while it ran): by how the simulator starts it.
    elif pkill -x -f 'app0/assets/service' 2>/dev/null; then
        echo "Stopped the PC service."
    else
        echo "The PC service isn't running."
    fi
    exit 0
fi

if [[ -z ${DISPLAY:-}${WAYLAND_DISPLAY:-} && -z ${SDL_VIDEODRIVER:-} ]]; then
    echo "No display to open a window on. On Windows 11, WSL has one built in: run" >&2
    echo "'wsl --update' in PowerShell, then restart WSL. See docs/13-troubleshooting.md." >&2
    exit 1
fi
command -v sdl2-config >/dev/null || { echo "SDL2 is missing: run tools/setup.sh" >&2; exit 1; }

cxx=$(command -v clang++-18 || command -v clang++)
cc=$(command -v clang-18 || command -v clang)
rm -rf "$out/obj" # everything is compiled again: no old objects of renamed files
mkdir -p "$out/obj" "$out/app0/assets/fonts" "$out/data"

# 1. The service, built for the PC (payload/service/sim_stubs.c stands in for
#    the console's functions). Where the PS5 app has service.elf.
"$cc" -std=gnu11 -O2 -Wall -Wextra -DDATA_DIR='"data/ps5-starter"' \
    "$repo/payload/service/service.c" "$repo/payload/service/sim_stubs.c" \
    -o "$out/app0/assets/service"

# 2. The app: every .c, .cc and .cpp in app/src (subfolders too, like the PS5
#    build) except pad.cpp (the PS5's controller), plus the simulator's window,
#    input, keyboard and sound from app/sim. Compiled in parallel.
read -r -a sdl_cflags <<< "$(sdl2-config --cflags)"
read -r -a sdl_libs <<< "$(sdl2-config --libs)"
flags=(-DSTARTER_PREVIEW -DSTARTER_SIM -O2 -Wall -Wextra -I"$app/src" -I"$here"
       -I"$repo/payload/service" "${sdl_cflags[@]}")
mapfile -d '' -t sources < <(
    find "$app/src" -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' \) ! -name pad.cpp -print0
    find "$here" -maxdepth 1 -name '*.cpp' -print0)
pids=()
for src in "${sources[@]}"; do
    rel=${src#"$app/"}
    object=$out/obj/${rel//\//_}.o # src/a/b.cpp -> src_a_b.cpp.o: no two files share one
    if [[ $src == *.c ]]; then
        "$cc" -std=c11 "${flags[@]}" -c "$src" -o "$object" & pids+=($!)
    else
        "$cxx" -std=c++20 -fno-exceptions -fno-rtti "${flags[@]}" -c "$src" -o "$object" & pids+=($!)
    fi
done
for pid in "${pids[@]}"; do wait "$pid"; done
"$cxx" "$out"/obj/*.o "${sdl_libs[@]}" -lm -o "$out/hello-ps5"
cp "$app/fonts/"* "$out/app0/assets/fonts/"
mkdir -p "$out/app0/sce_sys" && cp "$app/sce_sys/icon0.png" "$out/app0/sce_sys/" # the About page shows it
[[ ! -d $app/assets ]] || cp -r "$app/assets/." "$out/app0/assets/" # your own files, as app/build.sh

# 3. In WSL: the controller bridge. WSL can't see a controller connected to
#    Windows, so dualsense_bridge.py reads it on Windows and sends it here.
#    It runs with Windows' own Python and SDL2.dll (SDL's official Windows
#    build, downloaded once and checked). STARTER_SIM_CONTROLLER=0 skips it.
sdl_win_zip_url=https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10-win32-x64.zip
sdl_win_zip_sha256=6cf9706eefd0a4a06dc764007934d428afaf029fabdd408a9e646048c91e18fb
start_bridge() {
    grep -qi microsoft /proc/version 2>/dev/null || return 0 # not WSL: SDL sees controllers itself
    [[ ${STARTER_SIM_CONTROLLER:-1} != 0 ]] || return 0
    # Everything here is optional: a step that fails means keyboard only, never
    # no simulator. Windows programs run from a Windows folder (C:\).
    local win_c py= candidate path bits
    win_c=$(wslpath -u 'C:\' 2>/dev/null) || win_c=/mnt/c
    # Windows' Python: py.exe (python.org's launcher) first. A python.exe can be
    # the Microsoft Store's stand-in that only says "Python was not found", so
    # a candidate counts only if it really runs Python.
    for candidate in py.exe python.exe python3.exe; do
        path=$(command -v "$candidate") || continue
        if (cd "$win_c" && "$path" -c 'import sys' </dev/null >/dev/null 2>&1); then
            py=$path
            break
        fi
    done
    [[ -n $py ]] || {
        echo "Controller bridge: no Python on Windows (python.org), so keyboard only."
        return 0
    }
    # SDL2.dll below is the 64-bit x86 build: it needs a 64-bit x86 Python.
    bits=$(cd "$win_c" && "$py" -c 'import platform, struct; print(struct.calcsize("P") * 8, platform.machine())' \
        2>/dev/null | tr -d '\r') || bits=
    [[ $bits == "64 AMD64" ]] || {
        echo "Controller bridge: needs 64-bit x86 Python on Windows (found: ${bits:-unknown}), so keyboard only."
        return 0
    }
    # A Windows folder for it, so Windows can load the DLL: %LOCALAPPDATA%\ps5-starter.
    # (Asked through PowerShell in UTF-8: cmd.exe would mangle a name like Müller.)
    local win_local dir
    win_local=$(cd "$win_c" && powershell.exe -NoProfile -NonInteractive -Command \
        '[Console]::OutputEncoding=[Text.Encoding]::UTF8; [Environment]::GetFolderPath("LocalApplicationData")' \
        2>/dev/null | tr -d '\r') || win_local=
    [[ -n $win_local ]] || {
        echo "Controller bridge: can't reach Windows from WSL, so keyboard only."
        return 0
    }
    dir=$(wslpath -u "$win_local" 2>/dev/null)/ps5-starter || return 0
    mkdir -p "$dir" 2>/dev/null || {
        echo "Controller bridge: can't create $dir, so keyboard only."
        return 0
    }
    if [[ ! -f $dir/SDL2.dll ]]; then
        echo "Controller bridge: downloading SDL2.dll (SDL's official Windows build, once)..."
        local tmp
        tmp=$(mktemp -d)
        if curl -fsSL -o "$tmp/sdl2.zip" "$sdl_win_zip_url" &&
            echo "$sdl_win_zip_sha256  $tmp/sdl2.zip" | sha256sum -c --quiet; then
            unzip -o -q -j "$tmp/sdl2.zip" SDL2.dll -d "$dir" || true # checked just below
        else
            echo "Controller bridge: SDL2.dll download failed or didn't match its checksum: keyboard only."
        fi
        rm -rf "$tmp"
        [[ -f $dir/SDL2.dll ]] || return 0
    fi
    cp "$here/dualsense_bridge.py" "$dir/" || return 0
    (cd "$win_c" && "$py" "$(wslpath -w "$dir/dualsense_bridge.py")" --sdl "$(wslpath -w "$dir/SDL2.dll")" \
        --port "${STARTER_SIM_BRIDGE_PORT:-9278}" > "$out/bridge.log" 2>&1 &)
    echo "Controller bridge: started (build/sim/bridge.log). Turn your controller on any time."
}
start_bridge

# 4. Start it in build/sim, where its app0 and data folders are. (Linux's
#    graphics library reports its search for a GPU as "warnings"; they're
#    harmless, so only real errors are shown.)
export EGL_LOG_LEVEL=${EGL_LOG_LEVEL:-fatal}
echo "Hello PS5 on your PC. Keys: arrows, X Cross, C Circle, Z Square, V Triangle,"
echo "Q/E L1/R1, 1/3 L2/R2, Tab OPTIONS; drag the mouse for the touchpad; Esc closes."
cd "$out"
exec ./hello-ps5
