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
# Every .cpp of the app except main.cpp: the preview has its own main().
for f in font screen hello link page_play page_settings page_about; do
    "$cxx" -std=c++20 -fno-exceptions -fno-rtti "${flags[@]}" -c "$app/src/$f.cpp" -o "$obj/$f.o"
done
"$cxx" -std=c++20 -fno-exceptions -fno-rtti "${flags[@]}" -c "$here/preview.cpp" -o "$obj/preview.o"
"$cxx" "$obj"/*.o -lm -o "$obj/preview"
"$obj/preview" "$app/fonts/" "$out"
python3 - "$out" <<'PY'
import glob, os, sys
from PIL import Image
for p in sorted(glob.glob(os.path.join(sys.argv[1], "*.ppm"))):
    Image.open(p).save(p[:-4] + ".png")
    os.remove(p)
PY
ls "$out"
