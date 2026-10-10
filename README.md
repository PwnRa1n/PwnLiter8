<div align="center">

# Pwnliter8

**A tethered bootrom exploit for Apple A12, S4/S5 & A13**

![target](https://img.shields.io/badge/target-A12%20%C2%B7%20S4%2FS5%20%C2%B7%20A13-2ea44f)
![boards](https://img.shields.io/badge/boards-RP2350%20%C2%B7%20RP2040-007ec6)
![led](https://img.shields.io/badge/LED-status%20palette%20%2B%20startup%20sweep-8A2BE2)
![fork](https://img.shields.io/badge/fork%20of-usbliter8-lightgrey)

[LED status light](#the-led-status-light) · [Quick start](#quick-start) · [Building](#building) · [Maintenance](#maintenance)

</div>

---

A12X/Z can theoretically be supported as well, but it's not implemented yet.
This is a fork of [`usbliter8`](https://github.com/20obb/usbliter8), renamed
**Pwnliter8** — the target-side banner now reports `PWND:[Pwnliter8]`.

> [!NOTE]
> The technical write-up for the underlying bug is in the
> [original blog post](https://ps.tc/pages/blog-usbliter8.html).

---

## Contents

| | Section |
|---|---|
| 🔧 | [**Technical changes**](#technical-changes) — what changed and how, at the code level |
| 💡 | [**The LED status light**](#the-led-status-light) — what every colour means |
| 🚀 | [**Quick start**](#quick-start) — hardware, flashing, exploiting |
| 🛠 | [**Building**](#building) — from source |
| 🎛 | [**Pwnliter8ctl**](#pwnliter8ctl) — demote / boot raw iBoot |
| 📁 | [**Repository layout**](#repository-layout) |
| ⚠️ | [**Maintenance**](#maintenance) — read before changing anything |

---

## Technical changes

Five areas changed. **Firmware logic changed in exactly one file — `led.c`
(+107 / −13)** — everything else is build tooling, tests, or documentation.

| Area | File(s) | Nature of the change |
|---|---|---|
| **LED subsystem** | `led.c` | Animation engine, cross-backend palette, brightness scaling, state semantics |
| **Build & payload toolchain** | `CMakeLists.txt`, `build_release.sh`, `*/make.sh`, `start.S` ×2, `resources/*.h` | Target rename, reproducible cross-assembly, payload regeneration |
| **Host-side test suite** | `tests/test_pwnliter8ctl.py` *(new)* | Protocol regression tests, hardware-free |
| **Static analysis** | `.clang-tidy` *(new)* | Read-only lint config, deliberately not wired into the build |
| **Engineering docs** | `MAINTENANCE.md`, `VENDORED.md` *(new)* | Protected-area and vendored-fork-point documentation |

---

### 1 · LED subsystem

#### a) One-shot startup animation

A new timer-driven engine in `led.c`, self-contained and independent of the
existing blink/breathe timers:

```c
static const uint32_t startup_colors[] = { AMBER, CYAN, LIME, PURPLE, RED };

#define STARTUP_STEPS   (sizeof(startup_colors) / sizeof(startup_colors[0]))
#define STARTUP_STEP_MS (150)

static bool _led_startup_cb(__unused repeating_timer_t *rt) {
    led_ctx.startup_idx++;

    if (led_ctx.startup_idx >= STARTUP_STEPS) {
        /* every colour shown: settle on steady idle cyan and stop repeating */
        led_ctx.startup_active = false;
        led_set_color(CYAN);
        return false;
    }

    led_switch_color(startup_colors[led_ctx.startup_idx]);
    return true;
}
```

Engineering properties, each of which is a deliberate design choice:

| Property | How it is achieved |
|---|---|
| **Non-blocking** | Driven by an SDK `repeating_timer_t` firing from the alarm IRQ. No `sleep_ms()`, no busy-wait, no polling loop — the main thread is never held. |
| **Runs exactly once** | The callback returns `false` on the final step, which is how the SDK cancels a repeating timer. Normal completion needs no cleanup call. |
| **Own timer slot** | Uses a dedicated `led_ctx.startup_timer`, separate from the pre-existing blink and breathe timers — no timer aliasing between boot animation and status blinking. |
| **Cannot leak past boot** | `led_stop_startup()` runs unconditionally at the top of `led_set_state()`, so no animation timer can survive a transition into `RUNNING` / `SUCCESS` / `ERROR`. |
| **Fails open** | `add_repeating_timer_ms()`'s return value is checked; if no timer slot is available the LED jumps straight to cyan instead of stalling on amber forever. |
| **Never runs during the exploit** | It is driven from `LED_STATE_BOOTING`, which executes strictly inside `main()`'s load-bearing `sleep_ms(2000)` window — before `usb_start()` / `exploit_run()`. The sweep is therefore always finished and cancelled before USB, PIO and the exploit start. |
| **No added per-tick cost** | Each step performs exactly one colour write — the same cost as the 200 ms boot blink it replaced — and nothing at all while the exploit runs. |

#### b) Palette unified across both RGB backends

Previously each backend carried its own independently hand-tuned table, so the
same status looked different on a Waveshare than on a Pimoroni — and there were
only four colours for five states:

```c
/* NeoPixel backend */                 /* PWM backend */
#define GREEN  NEOPIXEL_RGB(1, 18, 2)  #define GREEN  PWM_RGB(10, 150, 50)
#define BLUE   NEOPIXEL_RGB(0, 10, 18) #define BLUE   PWM_RGB(0, 30, 120)
#define AMBER  NEOPIXEL_RGB(18, 2, 0)  #define AMBER  PWM_RGB(170, 24, 0)
#define RED    NEOPIXEL_RGB(24, 0, 0)  #define RED    PWM_RGB(120, 0, 0)
```

Both backends now carry **the same five hex literals**, each expressed as a
single scale macro applied to all three channels:

```c
#define NEO_SCALE(v)    (((v) * 128) / 255)

#define AMBER       NEOPIXEL_RGB(NEO_SCALE(0xff), NEO_SCALE(0xb0), NEO_SCALE(0x20))  /* #FFB020 */
#define CYAN        NEOPIXEL_RGB(NEO_SCALE(0x00), NEO_SCALE(0xd5), NEO_SCALE(0xff))  /* #00D5FF */
#define LIME        NEOPIXEL_RGB(NEO_SCALE(0xa3), NEO_SCALE(0xe6), NEO_SCALE(0x35))  /* #A3E635 */
#define PURPLE      NEOPIXEL_RGB(NEO_SCALE(0xa8), NEO_SCALE(0x55), NEO_SCALE(0xf7))  /* #A855F7 */
#define RED         NEOPIXEL_RGB(NEO_SCALE(0xff), NEO_SCALE(0x3b), NEO_SCALE(0x4f))  /* #FF3B4F */

#define SUCCESS_COLOR   PURPLE
```

- The PWM backend's `PWM_SCALE` expands to the **identical expression** as
  `NEO_SCALE` above, and substitutes `PWM_RGB(...)` for `NEOPIXEL_RGB(...)` —
  same names, same order, same hex, same arguments on both backends.
- One factor applied to all three channels means **hue ratios are preserved by
  construction**, not by hand-tuning per board.
- All values are integer macro constants → the entire palette folds to
  immediates at compile time. Zero runtime cost and no floating point
  (`PICO_PRINTF_SUPPORT_FLOAT=0` is already set in `CMakeLists.txt`).
- `SUCCESS_COLOR` is an indirection, so success resolves to purple on both
  backends without duplicating the switch case.

#### c) Brightness scaling — 50 %

```c
#define NEO_SCALE(v)    (((v) * 128) / 255)
#define PWM_SCALE(v)    (((v) * 128) / 255)
```

- **Why 128 and not 127:** `led_init()` configures `PWM_WRAP = 256`, so a
  channel landing on level 128 gives `128 / 256` = **exactly 50.0 % duty**. The
  NeoPixel path lands at `128 / 255` = 50.2 % of the 8-bit scale. Using 127
  would have given 49.4 % on both.
- **NeoPixel limits:** the SDK driver passes values through unchanged and
  addresses a **single** pixel —
  `pio_claim_free_sm_and_add_program_for_gpio_range(..., PICO_DEFAULT_WS2812_PIN, 1, true)`
  — on a hard-wired rail (there is no `PICO_DEFAULT_WS2812_POWER_PIN`). The
  worst colour draws ≈ 16 mA against a ~50 mA white ceiling. A WS2812 frame is a
  fixed 24-bit / ~30 µs transfer regardless of value, so brightness changes the
  **duty ratio only — never the write timing**, which matters on a
  timing-sensitive firmware.
- **PWM limits:** `led_init()` already requests level `PWM_WRAP` (100 %) on all
  three channels before the first colour is ever written — that is pre-existing
  shipped behaviour — so 50 % is comfortably inside the board's design.
  `led_switch_color()` still masks `& 0xFF`, capping the level at 255.

#### d) State semantics — five states, five distinguishable outcomes

| State | Before | After |
|---|---|---|
| `LED_STATE_BOOTING` | amber + 200 ms blink | one-shot 5-colour sweep |
| `LED_STATE_IDLE` | **amber — identical to booting** | steady cyan |
| `LED_STATE_RUNNING` | blue | lime |
| `LED_STATE_SUCCESS` | green | purple |
| `LED_STATE_ERROR` | red | red |

The old mapping made *idle* and *booting* indistinguishable, so you could not
tell "waiting for a target" from "still starting up". All five states are now
visually distinct.

- `led_set_state()` is the **only** place a colour is chosen, and it is called
  exclusively from the pre-existing confirmed-state sites (`main.c` for
  `BOOTING`/`IDLE`, `exploit.c` for `RUNNING`/`SUCCESS`/`ERROR`).
- Consequently **no edit to `main.c`, `exploit.c` or `led.h` was required** — the
  whole feature is a `led.c`-local change.
- The `LED_SINGLE_COLOR` (Pico 2) path and the no-LED stub were left untouched.

---

### 2 · Scope: what was deliberately *not* changed

| Untouched | Detail |
|---|---|
| `exploit.c`, `usb.c`, `bus.c`, `main.c`, `led.h` | Zero diff |
| `pio_usb/` | Vendored and frozen — see [`VENDORED.md`](VENDORED.md) |
| `boards/` | No pin, LED-selection or build-type changes |
| `PWM_WRAP`, `led_init()`, `led_switch_color()` bodies | Unchanged — only the palette constants they feed |
| `DISABLED = (0, 0, 0)` | Unchanged, so "off" is still genuinely off |
| `sleep_ms(2000)` (`main.c`), `sleep_ms(60)` (`exploit.c`) | Load-bearing, not touched |
| `// blinking here breaks reliability, IDK` | Preserved; every steady state still calls `led_set_blinking(0)` |

---

### 3 · Build & payload toolchain

- **CMake rename** — project, executable, program name and the shell-enable
  guard: `usbliter8` → `Pwnliter8`, `USBLITER8_ENABLE_SHELL` →
  `PWNLITER8_ENABLE_SHELL`. `build_release.sh` now copies `Pwnliter8.uf2`.
- **Reproducible payload cross-assembly** — both `make.sh` scripts pin the
  target triple and linker explicitly instead of relying on the host driver
  defaults:

  ```diff
  -clang -arch arm64 start.S ...
  +clang -target arm64-apple-macos11 \
  +       -fuse-ld=$HOME/cctools/bin/arm64-apple-darwin11-ld start.S ...
  ```

- **Toolchain flags preserved** — `t8030` keeps `-Os` and
  `-Wl,-order_file,symorder.txt`, both load-bearing for payload size and symbol
  layout; neither was added to `t8020_t8006`, which has no order file.
- **Target-side banner** — `start.S` ×2 now emit `.asciz " PWND:[Pwnliter8]"`,
  and `resources/shellcode_{t8030,t8020,t8006}.h` were regenerated by `make.sh`.
  Headers are build artifacts and are only ever produced by the script — never
  hand-edited.
- **No size regression:**

  | Payload | Size | `PWND:` offset |
  |---|---|---|
  | `t8030` | 1136 B | 704 |
  | `t8020` | 816 B | 488 |
  | `t8006` | 816 B | 488 |

---

### 4 · Host-side test suite *(new)*

`tests/test_pwnliter8ctl.py` — **9 tests** that lock down the externally
observable USB protocol of `Pwnliter8ctl`:

- device discovery by DFU product ID, plus the "no device" and "not pwned"
  failure paths
- DFU download chunking at **0x800** and the exact final-chunk remainder
- every chunk uses `wValue=0`, `wIndex=0` and the DFU class request (`0x21`)
- `boot` sequence ordering; `demote` sends a single custom request
- transfer timeouts

A recording `usb` stub is injected into `sys.modules` **before** the tool is
loaded, so the suite runs on a bare Python 3 installation with no `pyusb`, no
board and no device:

```sh
python3 -m unittest discover -s tests -v
```

Nothing in `tests/` is part of the firmware build — it cannot affect firmware
output, timing or exploit behaviour.

---

### 5 · Static analysis *(new, deliberately inert)*

`.clang-tidy` selects a `bugprone-*` + `clang-analyzer-*` check set configured
for **analysis only**:

- `WarningsAsErrors: ''`, `HeaderFilterRegex: '^$'`, `FormatStyle: none` →
  no warnings promoted to errors, no header noise, no formatting suggestions
- **not referenced by CMake**, so it cannot affect compilation, linking, timing
  or UF2 output
- the file itself documents that `clang-tidy --fix` must never be run over this
  repository

```sh
# manual, targeted, read-only
clang-tidy --config-file=.clang-tidy <file-you-are-reviewing>
```

---

### 6 · Engineering documentation *(new)*

- **`MAINTENANCE.md`** — why RP2350 QSPI-cache timing makes unrelated edits
  risky; a table of strictly protected files; the load-bearing-oddity list
  (`sleep_ms(2000)`, `sleep_ms(60)`, the blink-during-exploit hazard, the
  overloaded `exploit_run() == -2` return, and the forced `Release` build type
  on RP2040); and pre/post-change checklists, including how to syntax-check all
  five LED code paths in isolation.
- **`VENDORED.md`** — records the exact upstream fork point of `pio_usb/`
  (`a5a2f5a`) and the single known line of divergence, so that divergence is
  preserved *deliberately* instead of being "fixed" by accident.

---

### Verification status

| Check | Result |
|---|---|
| `./build_release.sh` — all 4 release boards | **4/4 pass**, zero compiler warnings |
| All 5 LED code paths compiled in isolation | 0 warnings each |
| `python3 -m unittest discover -s tests` | **9/9 pass** |
| Payload sizes / `PWND:` offsets | unchanged (1136/816/816 @ 704/488/488) |
| Protected files (`exploit.c`, `usb.c`, `bus.c`, `main.c`, `pio_usb/`) | zero diff |
| **On-hardware validation** | **NOT PERFORMED** |

> [!WARNING]
> No board has been flashed with these changes. The firmware bytes changed, and
> this exploit is sensitive to QSPI-cache timing — treat exploit reliability as
> **unverified** until you have reproduced it on real hardware.

---

## The LED status light

The on-board LED is the primary status indicator. The RP2350 also appears as a
**virtual COM port** where exploit logs are printed, but the LED is what you
watch while plugging the target in.

### Palette at a glance

| | Colour | Hex | Role |
|---|---|---|---|
| 🟧 | **Amber** | `#FFB020` | Animation step 1 |
| 🟦 | **Electric Cyan** | `#00D5FF` | Animation step 2 → **Idle** |
| 🟩 | **Lime Green** | `#A3E635` | Animation step 3 → **Exploit running** |
| 🟪 | **Royal Purple** | `#A855F7` | Animation step 4 → **Success** |
| 🟥 | **Signal Red** | `#FF3B4F` | Animation step 5 → **Failure** |

All colours run at **50 % brightness**. Every RGB channel is scaled by the same
factor, so the hue ratios above are reproduced exactly — the colours are not
washed out or shifted.

### Power-on animation

On every power-on or reset the LED plays a colour sweep **once**, then settles
on idle. It runs entirely inside the firmware's 2-second boot window, before USB
or the exploit start.

```
  t =   0 ms   🟧  Amber
  t = 150 ms   🟦  Electric Cyan        ┐
  t = 300 ms   🟩  Lime Green           │ decorative sweep
  t = 450 ms   🟪  Royal Purple         │ runs ONCE per power-on
  t = 600 ms   🟥  Signal Red           ┘
  t = 750 ms   🟦  Electric Cyan  ← animation over, steady idle
  t =  2 s     USB starts, waits for a target
```

> [!IMPORTANT]
> **The animation is purely decorative.** The purple and red you see during the
> sweep are *not* success/failure events — they are simply two of the five
> palette colours being displayed in sequence. **Only the steady states below
> carry meaning.**

### Steady states

| | Colour | Meaning | Set when |
|---|---|---|---|
| 🟦 | **Electric Cyan** `#00D5FF` | **Idle** | Boot finished, waiting for a target |
| 🟩 | **Lime Green** `#A3E635` | **Exploit running** | A target was actually detected *and* the race has begun |
| 🟪 | **Royal Purple** `#A855F7` | **Success** | The exploit has **confirmed** success |
| 🟥 | **Signal Red** `#FF3B4F` | **Failure** | The exploit failed, or a fatal error occurred |

Every state changes **only when the existing firmware logic confirms it**:

- Merely **connecting** a device does *not* turn the LED lime — USB connection
  and device detection are not the same as "exploit started".
- **Lime** is set at the moment the race actually begins.
- **Purple** is set only on a confirmed success, never optimistically.
- **Red** is set on a genuine failure path.

> [!TIP]
> After **either** success or failure the firmware stops and waits forever.
> Power-cycle the board to retry — on-board button, `picotool load`, or just
> unplug and replug.

### Single-colour LED (Raspberry Pi Pico 2)

The Pico 2 has no RGB LED, so states are encoded as blink patterns instead:

| Pattern | Meaning |
|---|---|
| 💡 Slow blink — 200 ms period | Booting (~2 s) |
| 🌊 Breathing — fading in and out | Idle, ready to exploit |
| ⚡ Rapid blink — 100 ms period | Exploit running |
| ✅ Steady on | Success |
| ⭕ Off | Failure |

### Which LED does my board have?

| Board | LED type | Behaviour |
|---|---|---|
| **Waveshare RP2350 USB-A** | RGB · WS2812 on GP16 | Colour sweep + status colours |
| **Waveshare RP2350 Zero** | RGB · WS2812 on GP16 | Colour sweep + status colours |
| **Pimoroni TINY2350** | RGB · PWM on GP18/19/20 | Colour sweep + status colours |
| **Adafruit Feather RP2040** | RGB · WS2812 on GP16 | Colour sweep + status colours · RP2040, less reliable |
| **Raspberry Pi Pico 2** | Single colour | Blink patterns, no sweep |
| **Raspberry Pi Pico** | **Disabled** | Deliberately turned off in `boards/pico.cmake` — it made reliability worse |

---

## Quick start

### 1 · Hardware

The exploit abuses a very low-level bug in the USB controller, which the default
Mac/PC USB stack can't normally reach. So we use a Raspberry Pi **RP2350**
microcontroller board instead.

The primary board is the [**Waveshare RP2350 USB-A**](https://www.waveshare.com/wiki/RP2350-USB-A)
with a Lightning-to-USB-A cable, and the R13 resistor *optionally*
[removed](https://qsantos.fr/2025/11/21/fixing-the-rp2350-usb-a-not-working-as-usb-host/).

Other RP2350 boards work too if you cut a Lightning cable and solder it directly
to the corresponding pins.

> [!WARNING]
> - **Do NOT use USB-C cables** — they often have a very different pinout.
> - Keep the Lightning end of the cable **relatively short**.
> - Signal pins are **GPIO12 (D+)** and **GPIO13 (D−)** by default, but that's configurable.

**Tested on:** Waveshare RP2350 USB-A · Waveshare RP2350 Zero · Pimoroni TINY2350 · Raspberry Pi Pico 2

RP2040 can theoretically be used too, but it is currently **not** very stable
and the Apple A13 SoC does **not** work on it at all.

### 2 · Flash the firmware

Prebuilt **UF2** images for the boards above are in the Releases section. Flash
either via the RP2350 bootrom's mass-storage mode or with
[picotool](https://github.com/raspberrypi/picotool).

Not on a supported board? Add your own config file under `boards/` and follow
[Building](#building).

### 3 · Exploit

1. Put the device into [**DFU mode**](https://theapplewiki.com/wiki/DFU_Mode#A11_and_newer_devices_without_clickable_home_buttons_(iPhone_8_and_above,_iPad_Pro_2018_and_above,_iPad_Mini_2021,_iPad_Air_2020_and_above,_iPad_10th_generation))
   while it's connected to your Mac/PC.

   > Do **NOT** enter DFU by breaking LLB — that will not work.

2. Unplug it from the Mac/PC and plug it into your RP2350 board.

3. Watch the LED — see [The LED status light](#the-led-status-light). Or open the
   virtual COM port for the logs. The exploit typically takes **0.7 – 1.2 s**.

### 4 · Verify and use

Replug the device back into your Mac/PC. The USB serial number should now end
with the `PWND` string:

```
CPID:8020 CPRV:11 CPFM:03 SCEP:01 BDID:0E ECID:XXXXXXXXXXXXXXXX IBFL:3C SRTG:[iBoot-3865.0.0.4.7] PWND:[Pwnliter8]
```

From here you can demote production mode or boot raw iBoot — see
[Pwnliter8ctl](#pwnliter8ctl).

---

## Building

### Requirements

| Tool | Version |
|---|---|
| [Pico SDK](https://github.com/raspberrypi/pico-sdk) | **2.2.0** — commit `a1438dff1d38bd9c65dbd693f0e5db4b9ae91779` |
| [ARM GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) | **15.2.Rel1** |
| [picotool](https://github.com/raspberrypi/picotool) | any recent (depends on `libusb`) |
| CMake | ≥ 3.13 |

### One board

```sh
cd Pwnliter8
cmake -S . -B build -DPICO_BOARD=$PICO_BOARD \
      -DPICO_SDK_PATH=$PICO_SDK_PATH -DPICO_TOOLCHAIN_PATH=$PICO_TOOLCHAIN_PATH
cmake --build build
```

`$PICO_BOARD` is one of:

| Value | Board |
|---|---|
| `waveshare_rp2350_usb_a` | Waveshare RP2350 USB-A |
| `waveshare_rp2350_zero` | Waveshare RP2350 Zero |
| `pimoroni_tiny2350` | Pimoroni TINY2350 |
| `pico2` | Raspberry Pi Pico 2 |

### All release boards at once

```sh
export PICO_SDK_PATH=/path/to/pico-sdk   # must be 2.2.0
./build_release.sh                       # writes artifacts/Pwnliter8.<board>.uf2
```

### Host-side tests

No hardware and no `pyusb` required:

```sh
python3 -m unittest discover -s tests -v
```

---

## Pwnliter8ctl

`Pwnliter8ctl` is a Python control tool for an already-pwned DFU device. It can
**demote** production mode or **boot raw iBoot** ("raw" meaning decrypted and
free of any container).

It depends on `pyusb`:

```sh
pip install pyusb
```

```console
$ ./Pwnliter8ctl
usage: Pwnliter8ctl [-h] {boot,demote} ...

Love is Control

positional arguments:
  {boot,demote}
    boot         boot raw iBoot
    demote       demote production mode

options:
  -h, --help     show this help message and exit
```

```sh
./Pwnliter8ctl demote
./Pwnliter8ctl boot /path/to/iboot.raw
```

---

## Repository layout

| Path | What it is |
|---|---|
| `main.c` | Entry point — clock setup, auto-retry loop / debug shell |
| `exploit.c` | The exploit itself |
| `usb.c`, `bus.c` | USB plumbing and bus management |
| `led.c` | Status LED — colour palette, power-on animation, state machine |
| `pio_usb/` | **Vendored** [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB) — frozen, see [`VENDORED.md`](VENDORED.md) |
| `usb_req_handler/` | Target-side USB request handler |
| `t8030_shellcode/`, `t8020_t8006_shellcode/` | Target-side payload sources |
| `resources/*.h` | Generated payload headers consumed by the firmware |
| `boards/` | Per-board pin & LED configuration |
| `Pwnliter8ctl` | Host-side Python control tool (`pyusb`) |
| `tests/` | Host-side unit tests |
| [`MAINTENANCE.md`](MAINTENANCE.md) | **Read before changing anything** |
| [`VENDORED.md`](VENDORED.md) | Vendored USB library — fork point and known divergence |

---

## Maintenance

> [!WARNING]
> RP2350 executes this firmware from external QSPI flash through a small
> instruction cache, and cache evictions shift instruction timing. **Seemingly
> unrelated code changes can reduce exploit reliability.** Read
> [`MAINTENANCE.md`](MAINTENANCE.md) before touching a single line — it lists the
> strictly protected files, the load-bearing oddities that look like bugs but
> aren't, and the pre/post-change checklists.

The short version:

- ⏱️ `sleep_ms(2000)` in `main.c` and `sleep_ms(60)` in `exploit.c` are
  **load-bearing**. Do not remove them as dead delays.
- 💡 **Blinking the LED during the exploit breaks reliability** — which is why
  LED activity in timing-sensitive paths is deliberately minimal.
- 📦 `pio_usb/` is vendored and frozen. See [`VENDORED.md`](VENDORED.md).
- ⚙️ RP2040 builds are inherently less reliable than RP2350. That's a property
  of the hardware timing, not a code defect to patch.

---

<div align="center">

**Love is Control**

</div>
