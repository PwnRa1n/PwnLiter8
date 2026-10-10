# Maintenance guide

This document describes how to make **safe maintenance changes** to this
repository without breaking the exploit.

## Why conservatism is required

The firmware implements a timing-sensitive bootrom exploit. RP2350 executes
code from external QSPI flash through a small instruction cache; evictions
from that cache shift instruction timing. As a consequence:

* **Seemingly unrelated code changes can decrease exploit reliability.**
  Even changes that look behavior-preserving can move code around in flash,
  change inlining, or alter cache behavior.
* Source layout, compiler output, memory placement, and execution timing all
  count as behavior.

Treat any change to firmware sources as potentially hardware-affecting until
proven otherwise **on real hardware**.

## Strictly protected areas

Do not modify, reformat, rename, move, split, merge, simplify, optimize, or
rewrite:

| Area | Files |
|---|---|
| Exploit implementation | `src/exploit.c`, `src/exploit.h` |
| USB / multicore plumbing | `src/usb.c`, `src/usb.h`, `src/bus.c`, `src/bus.h` |
| Vendored PIO USB library | `pio_usb/` (see `VENDORED.md`) |
| Target-side payloads | `shellcode/`, `resources/*.h` |
| Timing / memory / hardware config | all sleep durations, retry counts, clock settings, buffer sizes, alignments, compiler flags, build types, board definitions (`boards/*.cmake`, `CMakeLists.txt`), function attributes controlling inlining/RAM execution |
| Timing-sensitive logging & LED | `src/led.c`, LED/printf calls inside exploit execution paths |

A change is **not** safe merely because the source "looks equivalent".
Equivalence must be demonstrated (see checklist below); if it cannot be,
leave the code alone.

## Reliability-critical oddities (do not "fix" blindly)

These look like accidents but are load-bearing. They are documented here
because the original README that mentioned some of them is gone:

* `main.c` — `sleep_ms(2000)` after boot: deliberately long; has a **huge**
  impact on exploit reliability.
* `exploit.c` — `sleep_ms(60)` before the t8030 race: exists solely to
  restore reliability that was lost during an earlier refactor. Do not remove
  it as "dead delay".
* `led.c` — LED blinking during the exploit **breaks reliability**; LED
  activity in timing-sensitive paths is intentionally minimal.
* Error code `-2` is overloaded: it means "retry" in `main.c:do_auto()` and
  "device already PWNED" in `exploit.c`. Any refactor must preserve this
  exact convention.
* `boards/pico.cmake` and `boards/adafruit_feather_rp2040.cmake` force
  `CMAKE_BUILD_TYPE Release` because **MinSizeRel breaks `usb_task()` on
  RP2040**. Do not unify these with the top-level default.
* RP2040 builds are inherently less reliable than RP2350; this is a property
  of the hardware/timing, not a code defect to patch.

## Pinned build versions

Official release UF2s are built with (from the original README):

* Pico SDK **2.2.0** — commit `a1438dff1d38bd9c65dbd693f0e5db4b9ae91779`
* ARM GNU Toolchain **15.2.Rel1**

Local verification builds in this repo's history used Arch's
`arm-none-eabi-gcc` **16.2.0** (documented deviation). Before/after
comparisons are still valid because both sides use the same compiler, but
locally built UF2s are **not** bit-comparable to official release artifacts.

Build all release boards with:

```sh
export PICO_SDK_PATH=/path/to/pico-sdk   # must be 2.2.0
./build_release.sh                       # writes artifacts/Pwnliter8.<board>.uf2
```

## Pre-change checklist

1. `git status --porcelain` — note every pre-existing modification,
   untracked file, and deletion; preserve all of them exactly.
2. Baseline protected files:
   `git ls-files 'src/exploit.*' 'src/usb.*' 'src/bus.*' 'pio_usb/*' 'resources/*' \
   'shellcode/*' 'src/led.*' 'CMakeLists.txt' 'boards/*' 'src/main.c' | xargs sha256sum > /tmp/baseline.sha256`
3. Build baseline artifacts and record hashes: `sha256sum artifacts/*.uf2`.

## Post-change checklist

1. `sha256sum -c` (or diff) protected files against the baseline — any
   mismatch means a protected file changed: **stop and report**.
2. Review the complete `git diff`, including renames and new files.
3. Rebuild all boards and compare UF2 hashes byte-for-byte with the baseline
   (sources contain no `__DATE__`/`__TIME__`, so builds are reproducible).
4. Run the host-side tests (no hardware needed):
   ```sh
   python3 -m unittest discover -s tests -v
   ```
5. State clearly in the change report which checks were **not** performed
   (e.g. hardware exploit tests). Never claim hardware or reliability
   verification that was not actually performed.

## Static analysis

A `.clang-tidy` configuration exists for optional manual analysis of
non-vendored code. It is **inert**: nothing in the CMake build invokes
clang-tidy, so it cannot affect firmware output.

**Never run `clang-tidy --fix` over this repository.** Auto-fixes touching
protected files (including reformatting whole files) are forbidden. If you
run clang-tidy at all, restrict it to files you are deliberately
maintaining, and never apply fixes inside the protected areas listed above.
