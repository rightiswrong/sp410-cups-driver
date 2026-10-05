# sp410-cups-driver

An open-source CUPS driver for the **iDPRT SP410** 4-inch direct-thermal
label printer (and the SP410BT / SP420), built for Linux on every
architecture — in particular the **Raspberry Pi**, where the vendor's
closed-source x86-only filter fails with *"filter failed"*.

It is a clean-room implementation: the printer speaks TSPL, a documented
command language, so the driver converts CUPS raster pages into TSPL
`BITMAP` commands. No vendor code is used or required.
See [docs/REVERSE_ENGINEERING.md](docs/REVERSE_ENGINEERING.md) for how the
vendor driver is studied and what is still unverified.

> **Status: 1.0.0, tested on real hardware.** The SP410 works when the
> driver is compiled and installed on a Raspberry Pi 3B+; Raspberry Pi 4 and
> later are expected to work as well. The SP410BT and SP420 still await
> hardware reports. If you own one of those, a five-minute report helps a
> lot — see [docs/HARDWARE.md](docs/HARDWARE.md#models).

## What's in the box

| Component | What it does |
|---|---|
| `sp410-rastertotspl` | CUPS filter (C99, only depends on libcups): CUPS/PWG raster → TSPL |
| PPDs | SP410, SP410BT, SP420: 21 label sizes + custom sizes up to 108 × 300 mm, darkness, speed, media tracking, tear-off, halftoning, image shift |
| `60-idprt-sp410.rules` | udev rule: non-root access, no USB autosuspend, stable `/dev/usb/idprt-sp410-N` |
| `sp410ctl` | talk to the printer without CUPS: status, info, calibrate, self-test, test label |
| `tspl-decode` | parse and render any TSPL stream to PBM/PNG, diff two streams |
| `tools/re/` | vendor-package inspector, USB-capture extractor, vendor-vs-ours differential runner |

Full file list: [MANIFEST.md](MANIFEST.md).

## How it works

```text
 PDF / PNG / text ──► cups-filters (pdftoraster, gstoraster, …)
                         │  8-bit gray raster @ 203 dpi (asked for by the PPD)
                         ▼
              sp410-rastertotspl
                ├─ unpack 1/2/4/8/16-bit K, W/sGray, sRGB, CMYK; resample if not 203 dpi
                ├─ halftone: Auto (threshold for text/barcodes, Atkinson for photos) …
                ├─ clip to the 864-dot head, apply ShiftX/ShiftY
                └─ split into inked bands, trim blank margins
                         │  SIZE / GAP / DIRECTION / … / CLS / BITMAP… / PRINT
                         ▼
              CUPS backend (usb://, socket://, serial:) ──► SP410
```

A typical 4×6 shipping label is sent as a handful of trimmed bitmaps rather
than one 124 KB block. Details: [docs/PROTOCOL.md](docs/PROTOCOL.md).

## Install

### Raspberry Pi / Debian / Ubuntu, from a release (easiest)

Download the `.deb` for your system from
[Releases](https://github.com/rightiswrong/sp410-cups-driver/releases/latest).
`dpkg --print-architecture` tells you which one you need: `armhf` for
32-bit Raspberry Pi OS, `arm64` for 64-bit Raspberry Pi OS, `amd64` for PCs.
The packages are built on Debian 12, so they install on Raspberry Pi OS
(bookworm or later), Debian 12+ and Ubuntu 24.04+.

```sh
sudo apt install ./sp410-cups-driver_1.0.0_arm64.deb   # use your architecture
sudo lpadmin -p SP410 -E -v "$(sudo lpinfo -v | awk '/usb:.*(iDPRT|SP410|20d1)/{print $2; exit}')" \
     -P /usr/share/ppd/sp410-cups-driver/idprt-sp410.ppd -o PageSize=w288h432
lp -d SP410 /usr/share/cups/data/testprint
```

Check the download against `SHA256SUMS.txt` on the release page with
`sha256sum -c SHA256SUMS.txt --ignore-missing`.

### Raspberry Pi / Debian / Ubuntu, from source

```sh
sudo apt install -y build-essential libcups2-dev cups cups-filters python3
git clone <this repository> sp410-cups-driver && cd sp410-cups-driver
make check                                   # build + run the tests (no printer needed)
sudo ./scripts/install.sh --add-queue SP410  # installs, then creates queue "SP410"
```

`install.sh` finds the printer's USB URI automatically; pass `--uri`,
`--model sp410bt|sp420` or `--media w288h288` to override.

### As a .deb

```sh
make deb                                     # build/sp410-cups-driver_1.0.0_<arch>.deb
sudo apt install ./build/sp410-cups-driver_*.deb
```

### Manually

`sudo make install` puts the filter in `$(cups-config --serverbin)/filter`,
PPDs in `/usr/share/ppd/sp410-cups-driver/`, the udev rule in
`/usr/lib/udev/rules.d/`. Then add the printer in the CUPS web UI
(`http://localhost:631`) choosing *iDPRT SP410, sp410-cups-driver*, or:

```sh
lpinfo -v | grep usb                         # find the URI
sudo lpadmin -p SP410 -E -v 'usb://…' \
     -P /usr/share/ppd/sp410-cups-driver/idprt-sp410.ppd -o PageSize=w288h432
```

Remove with `sudo ./scripts/uninstall.sh [--remove-queues]` or `sudo apt remove sp410-cups-driver`.

## Printing

```sh
lp -d SP410 label.pdf                                    # 4x6 default
lp -d SP410 -o PageSize=w288h144 -o Darkness=12 tag.png  # 4x2, darker
lp -d SP410 -o PageSize=Custom.100x70mm sticker.pdf      # any size up to 108 x 300 mm
lp -d SP410 -n 20 -o Dither=Threshold barcode.pdf        # 20 copies, crisp barcodes
```

Make a setting the queue default with `lpadmin -p SP410 -o Darkness-default=12`.

| Option | Values | Default | TSPL |
|---|---|---|---|
| `PageSize` | `w288h432` (4×6 in) … `100x150mm` … `Custom.WxH` | 4×6 in | `SIZE` |
| `Darkness` | `Default`, `0`–`15` | printer's setting | `DENSITY` |
| `PrintSpeed` | `Default`, `2`–`6` (in/s) | printer's setting | `SPEED` |
| `MediaTracking` | `Gap`, `BlackMark`, `Continuous` | `Gap` | `GAP` / `BLINE` / `GAP 0,0` |
| `GapHeight` | `0`–`10` (mm) | `3` | `GAP m,…` |
| `GapOffset` | `0`–`5` (mm) | `0` | `GAP …,n` |
| `PrintDirection` | `Normal`, `Rotate180` | `Normal` | `DIRECTION` |
| `TearOff` | `True`, `False` | `True` | `SET TEAR` |
| `TearOffset` | `-3`–`3` (mm) | `0` | `OFFSET` |
| `Dither` | `Auto`, `Threshold`, `Atkinson`, `FloydSteinberg`, `Ordered` | `Auto` | — |
| `Threshold` | `64`–`192` (lower = darker) | `128` | — |
| `ShiftX`, `ShiftY` | `-5`–`5` (mm) | `0` | — (applied to the image) |
| `ColorModel` | `Gray` (driver halftones), `BW` (renderer halftones) | `Gray` | — |

Invalid values are logged as `WARNING` in the CUPS error log and fall back to
the queue default.

## Printer utilities

```sh
sp410ctl status                  # status 0x00: ready
sp410ctl calibrate               # measure label/gap after loading media
sp410ctl test-label --size 4x6   # print with built-in fonts (tests the link, not the filter)
sp410ctl --host 10.0.0.9 info    # via a network print server
tspl-decode job.prn --png-dir out/   # see what a job would print
```

## Development

```sh
make                  # build/sp410-rastertotspl, test helpers
make check            # PPD lint + 28 end-to-end tests (no printer, no cupsd)
make ppd              # regenerate PPDs from ppd/gen_ppd.py (never edit .ppd by hand)
make CFLAGS="-O1 -g -fsanitize=address,undefined" LDFLAGS=-fsanitize=address,undefined check
```

Without `cups-config`/`pkg-config`, pass `CUPS_CFLAGS=-I… CUPS_LIBS="-L… -lcups"`.
CI builds and tests on x86-64 and arm64. See [CONTRIBUTING.md](CONTRIBUTING.md).

## Roadmap

* Hardware validation on SP410 / SP410BT / SP420 and settling the
  [open questions](docs/REVERSE_ENGINEERING.md#open-questions)
* Optional back-channel status (`ESC !?`) → CUPS `printer-state-reasons` (media-empty, cover-open)
* Hardware copies for multi-page collated jobs; compressed bitmap mode if the firmware supports one
* IPP Everywhere "printer application" (PAPPL) wrapper for driverless setups

## License and trademarks

Apache License 2.0 — see [LICENSE](LICENSE).
iDPRT is a trademark of its owner. This project is independent and is not
affiliated with, endorsed by, or supported by iDPRT or HPRT. Use at your own
risk; thermal print heads are consumables — very high darkness settings at
low speeds shorten their life.
