#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# ── Prerequisites ───────────────────────────────────────────────────────────

for tool in clang vmacho xxd; do
    if ! command -v "$tool" &>/dev/null; then
        echo "ERROR: '$tool' not found in PATH." >&2
        exit 1
    fi
done

if [[ ! -d "$HOME/cctools/bin" ]]; then
    echo "ERROR: cctools not found at ~/cctools/bin." >&2
    echo "       Install cctools and ensure arm64-apple-darwin11-ld is present." >&2
    exit 1
fi

# ── Build ───────────────────────────────────────────────────────────────────

mkdir -p build

TARGET=t8030

clang -target arm64-apple-macos11 -fuse-ld=$HOME/cctools/bin/arm64-apple-darwin11-ld -Os -Wl,-preload -Wl,-segalign,0x10 -Wl,-order_file,symorder.txt -nostdlib -fno-vectorize start.S patch.c tt.c -o build/shellcode_$TARGET.o
vmacho -f build/shellcode_$TARGET.o build/shellcode_$TARGET.bin

echo -n "const " > ../../resources/shellcode_$TARGET.h
xxd -i -n shellcode_$TARGET build/shellcode_$TARGET.bin >> ../../resources/shellcode_$TARGET.h

echo "OK: resources/shellcode_$TARGET.h regenerated."
