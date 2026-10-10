#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# ── Prerequisites ───────────────────────────────────────────────────────────

if [[ -z "${PICO_SDK_PATH:-}" ]]; then
    echo "ERROR: PICO_SDK_PATH is not set." >&2
    echo "       Point it to your Pico SDK 2.2.0 checkout, e.g.:" >&2
    echo "         export PICO_SDK_PATH=/path/to/pico-sdk" >&2
    exit 1
fi

if [[ ! -d "$PICO_SDK_PATH" ]]; then
    echo "ERROR: PICO_SDK_PATH '$PICO_SDK_PATH' does not exist." >&2
    exit 1
fi

if [[ ! -f "$PICO_SDK_PATH/pico_sdk_init.cmake" ]]; then
    echo "ERROR: '$PICO_SDK_PATH' does not look like a Pico SDK (missing pico_sdk_init.cmake)." >&2
    exit 1
fi

if ! command -v cmake &>/dev/null; then
    echo "ERROR: 'cmake' not found in PATH." >&2
    exit 1
fi

if ! command -v arm-none-eabi-gcc &>/dev/null; then
    echo "ERROR: 'arm-none-eabi-gcc' not found in PATH." >&2
    echo "       Install the ARM GNU toolchain (15.2.Rel1 or compatible)." >&2
    exit 1
fi

# ── Options ─────────────────────────────────────────────────────────────────

CLEAN=0
if [[ "${1:-}" == "--clean" ]]; then
    CLEAN=1
fi

# ── Build ───────────────────────────────────────────────────────────────────

BOARDS=(
    waveshare_rp2350_usb_a
    waveshare_rp2350_zero
    pimoroni_tiny2350
    pico2
    # adafruit_feather_rp2040
    # pico
)

mkdir -p artifacts

if [[ $CLEAN -eq 1 ]]; then
    echo "Cleaning old artifacts..."
    rm -f artifacts/Pwnliter8.*.uf2
fi

FAILED=()

for b in "${BOARDS[@]}"; do
    BUILD_DIR="build/$b"
    echo ""
    echo "════════════════════════════════════════════════════════════"
    echo "  Building: $b"
    echo "════════════════════════════════════════════════════════════"

    if ! cmake -S . -B "$BUILD_DIR" \
            -DPICO_BOARD="$b" \
            -DPICO_SDK_PATH="$PICO_SDK_PATH" \
            -DPICO_TOOLCHAIN_PATH="${PICO_TOOLCHAIN_PATH:-}"; then
        echo "ERROR: CMake configuration failed for '$b'." >&2
        FAILED+=("$b")
        continue
    fi

    if ! cmake --build "$BUILD_DIR" -j"$(nproc)"; then
        echo "ERROR: Build failed for '$b'." >&2
        FAILED+=("$b")
        continue
    fi

    cp "$BUILD_DIR/Pwnliter8.uf2" "artifacts/Pwnliter8.$b.uf2"
    echo "  → artifacts/Pwnliter8.$b.uf2"
done

# ── Summary ─────────────────────────────────────────────────────────────────

echo ""
if [[ ${#FAILED[@]} -gt 0 ]]; then
    echo "════════════════════════════════════════════════════════════"
    echo "  BUILD FAILURES: ${#FAILED[@]}"
    for f in "${FAILED[@]}"; do
        echo "    - $f"
    done
    echo "════════════════════════════════════════════════════════════"
    exit 1
else
    echo "════════════════════════════════════════════════════════════"
    echo "  All ${#BOARDS[@]} boards built successfully."
    echo "  Artifacts in: artifacts/"
    echo "════════════════════════════════════════════════════════════"
fi
