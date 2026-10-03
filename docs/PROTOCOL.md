# SP410 wire protocol (TSPL subset)

This is the subset of TSPL/TSPL2 that `sp410-rastertotspl` emits, plus the
utility commands `sp410ctl` uses. It is written from the public TSPL
programming references and checked against our own decoder; items not yet
confirmed on real SP410 hardware are marked **unverified** and tracked in
[REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md#open-questions).

## Transport

TSPL is transport-agnostic: the same byte stream works over every link.

| Link | Linux device | CUPS URI |
|---|---|---|
| USB (printer class, bulk OUT + IN) | `/dev/usb/lpN` via `usblp` | `usb://iDPRT/SP410?serial=…` (from `lpinfo -v`) |
| Bluetooth SPP (SP410BT) | `/dev/rfcommN` | `serial:/dev/rfcomm0?baud=115200` |
| Network print server | — | `socket://host:9100` |

USB ID reported by community tables: `20d1:7008` (**unverified** for every
SP410 revision — check `lsusb`).

## Units and geometry

* Head: 203 dpi; TSPL treats this as exactly **8 dots/mm** when an argument
  is given in `mm`. Without a unit, sizes are inches.
* Printable width: 108 mm = **864 dots** (spec sheet). Max length: 300 mm =
  2400 dots (spec sheet; firmware limit **unverified**).
* Origin (0,0) is the top-left of the label as it leaves the printer with
  `DIRECTION 0`.

## Line format

ASCII commands, one per line, terminated by `CR LF`. Arguments are
comma-separated. Decimal numbers always use `.` — the filter never calls
`setlocale()`, so a system locale such as `de_DE` cannot turn `101.6` into
`101,6` (which the printer would parse as two arguments).

## Job emitted per label

```text
SIZE 101.6 mm,152.4 mm          label width, label length
GAP 3 mm,0 mm                   or  BLINE 3 mm,0 mm  (black mark)  or  GAP 0,0  (continuous)
DIRECTION 0                     0 = normal, 1 = rotated 180°
REFERENCE 0,0                   image origin; positioning is done in the bitmap
OFFSET -2 mm                    only when TearOffset ≠ 0
SPEED 4                         only when PrintSpeed ≠ Printer Default (ips)
DENSITY 12                      only when Darkness ≠ Printer Default (0–15)
SET TEAR ON                     advance the label to the tear bar after printing
CLS                             clear the image buffer
BITMAP 8,10,37,90,0,<3330 raw bytes>      one band per inked region,
BITMAP 8,120,49,5,0,<245 raw bytes>       trimmed left/right/top/bottom
PRINT 1,1                       1 set × N copies (N from the raster header)
```

Setup is re-sent for every label so that mixed page sizes in one job work
and so that a job never depends on state left by a previous one. Darkness
and speed are deliberately *not* sent when set to "Printer Default", so a
value configured with the vendor utility or the printer's own menu wins.

## BITMAP

```text
BITMAP x,y,width_bytes,height,mode,<width_bytes × height raw bytes>
```

| Field | Meaning |
|---|---|
| `x`, `y` | top-left position in dots (`x` is always a multiple of 8 in our output) |
| `width_bytes` | bytes per row (8 dots per byte) |
| `height` | rows |
| `mode` | `0` overwrite, `1` OR, `2` XOR — we always use `0` |
| data | rows top-to-bottom, MSB = leftmost dot, **bit 1 = white, bit 0 = black** |

The polarity is the opposite of PBM and of CUPS `K` rasters, so the filter
inverts every byte on output. Padding bits past the right edge of the image
are sent as `1` (white). Data follows the 5th comma immediately — there is no
length prefix and no line terminator inside it; the trailing `CR LF` after the
data is ignored as whitespace.

Worked example — a 10-dot row, █ = black:

```text
dots          █ █ ░ ░ █ █ █ █ █ █ | (6 padding dots)
black=1       1 1 0 0 1 1 1 1   1 1 0 0 0 0 0 0   -> CF C0   (PBM / CUPS K order)
TSPL bytes    0 0 1 1 0 0 0 0   0 0 1 1 1 1 1 1   -> 30 3F   (inverted, padding white)
```

### Band splitting

A 4×6 in label is 102 × 1218 = 124 236 bytes if sent whole. Most shipping
labels are mostly white, so the filter:

1. finds rows containing ink,
2. groups them into bands, keeping blank runs shorter than 16 rows inside a
   band (a new `BITMAP` header costs ~25 bytes, a blank 4-inch row 102 bytes),
3. trims each band to its leftmost/rightmost inked byte,
4. doubles the merge distance if more than 64 bands result (bounded command
   count for striped pages).

The test suite checks that band output is dot-identical to the full image.

## Status and utility commands (sp410ctl)

| Bytes | Purpose | Reply |
|---|---|---|
| `ESC ! ?` | status, processed immediately | 1 byte, see below |
| `ESC ! R` | reset printer | none |
| `~!T CR LF` | model name | text + `CR` |
| `~!I CR LF` | code page | text + `CR` |
| `~!@ CR LF` | mileage (head use) | text + `CR` |
| `SELFTEST` | print configuration label | — |
| `GAPDETECT` | measure label + gap length | — (**unverified** on SP410; some firmwares use `AUTODETECT`) |
| `BLINEDETECT` | measure black-mark media | — |
| `FORMFEED` / `HOME` | feed one label / feed to next label start | — |

Status byte bits (TSPL standard; bit 6 **unverified** on SP410):

| Bit | Value | Meaning |
|---|---|---|
| 0 | 0x01 | head open |
| 1 | 0x02 | paper jam |
| 2 | 0x04 | out of paper |
| 3 | 0x08 | out of ribbon (n/a, direct thermal) |
| 4 | 0x10 | paused |
| 5 | 0x20 | printing |
| 6 | 0x40 | cover open / model-specific |
| 7 | 0x80 | other error |

`0x00` = ready. The filter itself does not use the back-channel yet; see the
roadmap in the README.
