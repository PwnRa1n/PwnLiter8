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

if [[ -z "${TARGET:-}" ]]; then
    echo "ERROR: TARGET is not set." >&2
    echo "       Valid targets: t8020, t8006, t8030" >&2
    echo "       Usage: TARGET=t8020 $0" >&2
    exit 1
fi

if [[ "$TARGET" != "t8020" && "$TARGET" != "t8006" && "$TARGET" != "t8030" ]]; then
    echo "ERROR: Unknown TARGET '$TARGET'." >&2
    echo "       Valid targets: t8020, t8006, t8030" >&2
    exit 1
fi

# ── Build ───────────────────────────────────────────────────────────────────

mkdir -p build

clang -arch arm64 handler.c -Os -Wl,-preload -nostdlib -e _custom_handle_usb_req -Wl,-order_file,symorder.txt -Itargets/$TARGET -o build/handler_$TARGET.o

vmacho -f build/handler_$TARGET.o build/handler_$TARGET.bin

echo -n "const " > ../../resources/handler_$TARGET.h
xxd -i -n handler_$TARGET build/handler_$TARGET.bin >> ../../resources/handler_$TARGET.h

echo "OK: resources/handler_$TARGET.h regenerated."
