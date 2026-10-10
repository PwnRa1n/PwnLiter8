# Vendored code: `pio_usb/`

The `pio_usb/` directory is a **vendored copy** of
[sekigon-gonnoc/Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB)
and is treated as frozen in this repository.

Do **not** reformat, restructure, "clean up", or casually update it.
Changes to PIO USB code alter USB transaction behavior, interrupt timing,
and multicore interaction, all of which the exploit depends on directly.

## Fork point

The copy was taken from upstream commit:

```
a5a2f5a  Merge pull request #207 from mikeysklar/crc16-not-in-flash
```

## Divergence from upstream `a5a2f5a`

Every file under `pio_usb/src/` is **byte-identical** to upstream `a5a2f5a`,
with exactly one exception:

| File | Line | Local (`pio_usb/`) | Upstream `a5a2f5a` |
|---|---|---|---|
| `src/pio_usb_ll.h` | 144 | `#ifdef PICO_RP2350` | `#if defined(PICO_RP2350) && PICO_RP2350` |

The upstream form was introduced by upstream commit `2f17cec`
("Use #if, because PICO_RP2350 is always defined, and might be 0 or 1.").
The local `#ifdef` form matches an older upstream revision. This single-line
difference is preserved as-is: it is part of the code base that has been
validated on hardware, and "fixing" it would change compiled firmware output.

## Known upstream changes *not* present here

Upstream commit `cbf055d` (merged as `5a37a66`, PR #208) clamps the
transaction length before `memcpy` in `pio_usb_host.c` (`usb_in_transaction`)
and `pio_usb_device.c`, guarding against an out-of-bounds copy when a
non-compliant USB peer reports a length larger than the allocated buffer.

**This fix is intentionally NOT applied** in the vendored copy. Applying it
would modify low-level USB transaction code, which is frozen here for
reliability reasons. Treat this as a known, documented divergence — not as
something to sync during routine maintenance. Any decision to adopt it (or
any other upstream update) requires re-validating exploit reliability on real
hardware for every supported board and target CPID.

## How to re-verify the fork point

```sh
git clone https://github.com/sekigon-gonnoc/Pico-PIO-USB /tmp/piousb_upstream
git -C /tmp/piousb_upstream checkout a5a2f5a
diff -ru /tmp/piousb_upstream/src pio_usb/src
```

Expected result: a single hunk in `src/pio_usb_ll.h` (line 144) and nothing
else. Any other difference means `pio_usb/` has drifted and should be
investigated before anything else in the project is changed.
