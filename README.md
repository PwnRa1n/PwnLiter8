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

## What's new in this fork

| Change | Details |
|---|---|
| **LED status palette** | Five named colours replace the old orange/blue/green/red set, with defined meaning per state |
| **Power-on sweep** | One-shot colour animation at boot, then a steady idle colour |
| **50 % brightness** | Every RGB channel scaled by the same factor — full hue accuracy at half power |
| **`Pwnliter8ctl`** | Control tool renamed; same `boot` / `demote` commands |
| **Host-side tests** | New `tests/` — 9 unit tests that run without hardware or `pyusb` |
| **Docs** | New `README.md`, `MAINTENANCE.md`, `VENDORED.md` and `.clang-tidy` |

---

## Contents

- [**The LED status light**](#the-led-status-light) — what every colour means
- [**Quick start**](#quick-start) — hardware, flashing, exploiting
- [**Building**](#building) — from source
- [**Pwnliter8ctl**](#pwnliter8ctl) — demote / boot raw iBoot
- [**Repository layout**](#repository-layout)
- [**Maintenance**](#maintenance) — read before changing anything

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
