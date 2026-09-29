#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
make -j"${JOBS:-4}" GPU_SUPPORT=true INTEL_GPU_SUPPORT=true
mkdir -p "$HOME/.local/bin"
next_binary=$(mktemp "$HOME/.local/bin/.btop-XXXXXXXX")
trap 'rm -f -- "$next_binary"' EXIT
install -m755 bin/btop "$next_binary"
mv -f -- "$next_binary" "$HOME/.local/bin/btop"
"$HOME/.local/bin/btop" --version
