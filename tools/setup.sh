#!/usr/bin/env bash
# Sets up everything the starters need, in Linux or WSL (Ubuntu):
#   1. the compilers and tools, from apt (asks for your password once)
#   2. the PS5 payload SDK v0.42, checked against its SHA-256, into
#      ~/.ps5-starter/ps5-payload-sdk
# Safe to run again: finished steps are skipped.
#
#   tools/setup.sh          set everything up
#   tools/setup.sh check    only report what's missing
set -euo pipefail

sdk_url=https://github.com/ps5-payload-dev/sdk/releases/download/v0.42/ps5-payload-sdk.zip
sdk_sha256=8cfbc7cd5811e719eb4f0c47eea668d3dc7b40bc8ab11c4a5031d40c23ec02da
home=${PS5_STARTER_HOME:-$HOME/.ps5-starter}
sdk=$home/ps5-payload-sdk

# libsdl2-dev: the PC simulator's window and input (app/sim).
packages=(clang-18 lld-18 make git curl unzip python3 python3-pil pkg-config ccache libsdl2-dev)

missing_packages() {
    local p
    for p in "${packages[@]}"; do
        dpkg -s "$p" >/dev/null 2>&1 || echo "$p"
    done
}

report() {
    local ok=1 missing
    missing=$(missing_packages)
    if [[ -n $missing ]]; then
        echo "missing packages: $(echo "$missing" | tr '\n' ' ')"
        ok=0
    else
        echo "packages: ok"
    fi
    if [[ -x $sdk/bin/prospero-clang ]]; then
        echo "payload SDK: ok ($sdk)"
    else
        echo "payload SDK: missing"
        ok=0
    fi
    if [[ ${PS5_PAYLOAD_SDK:-} != "$sdk" ]]; then
        echo "PS5_PAYLOAD_SDK: not set in this shell (the build scripts find the SDK anyway)"
    fi
    return $((1 - ok))
}

if [[ ${1:-} == check ]]; then
    report
    exit
fi

missing=$(missing_packages)
if [[ -n $missing ]]; then
    echo "Installing: $(echo "$missing" | tr '\n' ' ')"
    sudo apt-get update -qq
    # shellcheck disable=SC2086
    sudo apt-get install -y -qq $missing
fi

if [[ ! -x $sdk/bin/prospero-clang ]]; then
    mkdir -p "$home"
    zip=$home/ps5-payload-sdk-v0.42.zip
    echo "Downloading the PS5 payload SDK v0.42..."
    curl -fsSL --retry 3 -o "$zip" "$sdk_url"
    echo "$sdk_sha256  $zip" | sha256sum -c --quiet || {
        echo "The SDK download doesn't match its expected SHA-256: not using it." >&2
        rm -f "$zip"
        exit 1
    }
    rm -rf "$sdk"
    unzip -q "$zip" -d "$home"
    rm -f "$zip"
fi

echo
report && echo && echo "All set. Next: docs/02-first-payload.md"
