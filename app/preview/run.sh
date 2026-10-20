#!/usr/bin/env bash
# Draws the app's screens on a PC (Linux or WSL) as PNGs in build/preview, with
# the same drawing code as on the PS5. Needs clang++ and python3-pil
# (tools/setup.sh installs both).
#
#   app/preview/run.sh
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
app=$(dirname -- "$here")
repo=$(dirname -- "$app")
out=$repo/build/preview
mkdir -p "$out"
rm -f "$out"/*.png "$out"/*.ppm # only this run's shots
obj=$(mktemp -d)
trap 'rm -rf "$obj"' EXIT

cxx=$(command -v clang++-18 || command -v clang++)
cc=$(command -v clang-18 || command -v clang)
flags=(-DSTARTER_PREVIEW -O2 -Wall -Wextra -I"$app/src" -I"$repo/payload/service")
"$cc" -std=c11 "${flags[@]}" -c "$app/src/stb_impl.c" -o "$obj/stb.o"
# The app's .cpp files, but not main.cpp or the controller's: the preview has
# its own main() and no controller.
for f in font screen image keyboard sound hello link page_play page_notes page_settings page_about; do
    "$cxx" -std=c++20 -fno-exceptions -fno-rtti "${flags[@]}" -c "$app/src/$f.cpp" -o "$obj/$f.o"
done
"$cxx" -std=c++20 -fno-exceptions -fno-rtti "${flags[@]}" -c "$here/preview.cpp" -o "$obj/preview.o"
"$cxx" "$obj"/*.o -lm -o "$obj/preview"
# It runs in an empty folder with the app's icon at app0/sce_sys, like /app0
# on the console, and no saved notes or settings.
mkdir -p "$obj/run/app0/sce_sys" "$obj/run/app0/assets"
cp "$app/sce_sys/icon0.png" "$obj/run/app0/sce_sys/"
[[ ! -d $app/assets ]] || cp -r "$app/assets/." "$obj/run/app0/assets/"
(cd "$obj/run" && "$obj/preview" "$app/fonts/" "$out")
python3 - "$out" <<'PY'
import glob, os, sys
from PIL import Image
for p in sorted(glob.glob(os.path.join(sys.argv[1], "*.ppm"))):
    Image.open(p).save(p[:-4] + ".png")
    os.remove(p)
PY
ls "$out"
