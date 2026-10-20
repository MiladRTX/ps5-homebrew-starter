#!/usr/bin/env bash
# Builds "Hello PS5" as a PS5 app folder and ZIP, in Linux or WSL
# (docs/05-first-app.md), with the kit in kit/ (ps5-native-app-boilerplate by
# BlackBearReloaded) and the service payload, which goes inside the app.
#
#   app/build.sh                    -> build/app/<title ID>/ and <title ID>.zip
#   app/build.sh deploy <PS5 IP>    -> also copies it to /data/homebrew over FTP
#
# The kit's scripts don't take spaces in paths, so the kit is copied to
# $PS5_STARTER_HOME/kit-work (default ~/.ps5-starter/kit-work) and built there.
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(dirname -- "$here")
home=${PS5_STARTER_HOME:-$HOME/.ps5-starter}
kit=$home/kit-work

# 1. The service payload the app starts (payload/service).
make -s -C "$repo/payload/service" PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-$home/ps5-payload-sdk}"

# 2. The kit, copied to the work folder whenever kit/ changed. What the kit
#    downloads on its first build (its SDK copy, in .deps) is kept.
stamp=$(cd "$repo/kit" && find . -type f -print0 | sort -z | xargs -0 sha256sum | sha256sum | cut -c1-16)
if [[ ! -f $kit/.kit-stamp || $(cat "$kit/.kit-stamp") != "$stamp" ]]; then
    echo "Preparing the kit..."
    mkdir -p "$kit"
    find "$kit" -mindepth 1 -maxdepth 1 ! -name .deps -exec rm -rf {} +
    cp -r "$repo/kit/." "$kit/"
    echo "$stamp" > "$kit/.kit-stamp"
fi

# 3. This app's files beside the kit's own.
rm -rf "$kit/starter"
mkdir -p "$kit/starter/assets/fonts"
cp -r "$here/src" "$here/sce_sys" "$kit/starter/"
# The service's version, so the app knows which one it carries.
cp "$repo/payload/service/service_version.h" "$kit/starter/src/"
cp "$here/fonts/"* "$kit/starter/assets/fonts/"
cp "$repo/payload/service/service.elf" "$kit/starter/assets/service.elf"
# Your own files (pictures, data), if you made app/assets: /app0/assets on the console.
[[ ! -d $here/assets ]] || cp -r "$here/assets/." "$kit/starter/assets/"
bash "$kit/tools/validate-assets.sh" "$kit/starter/sce_sys"

# System functions the SDK has no stubs for, as link-only stubs (app/stubs):
# the keyboard's sceCommonDialogInitialize.
sdk=${PS5_PAYLOAD_SDK:-$home/ps5-payload-sdk}
mkdir -p "$kit/starter/stubs"
PS5_PAYLOAD_SDK=$sdk sh "$kit/tooling/prospero-clang18" -O2 -Wall -Wextra -fPIC \
    -c "$here/stubs/common_dialog.c" -o "$kit/starter/stubs/common_dialog.o"
"$sdk/bin/prospero-lld" --shared -soname libSceCommonDialog.sprx \
    -o "$kit/starter/stubs/libSceCommonDialog.so" "$kit/starter/stubs/common_dialog.o"

# 4. Build (the kit downloads its own SDK copy the first time).
vars=(APP_SOURCE_DIR=starter/src APP_PARAM=starter/sce_sys/param.json
      APP_SCE_SYS=starter/sce_sys APP_ASSETS=starter/assets
      APP_IMPORT_STUBS=starter/stubs/libSceCommonDialog.so)
if [[ ${1:-} == deploy ]]; then
    [[ -n ${2:-} ]] || { echo "usage: app/build.sh deploy <PS5 IP>" >&2; exit 2; }
    make -C "$kit" "${vars[@]}" deploy PS5_HOST="$2"
else
    make -C "$kit" "${vars[@]}"
fi

# 5. The result, next to your sources.
title=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' \
    "$here/sce_sys/param.json")
rm -rf "$repo/build/app"
mkdir -p "$repo/build/app"
cp -r "$kit/dist/$title" "$kit/dist/$title.zip" "$repo/build/app/"
echo
echo "App folder: build/app/$title"
echo "ZIP:        build/app/$title.zip"
