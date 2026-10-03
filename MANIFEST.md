# Manifest

Every file in the repository, what it is for, and where `make install` /
the `.deb` puts it. Keep this in sync when adding, renaming or removing files.

## Driver (installed)

| Path | Purpose | Installed as |
|---|---|---|
| `src/rastertotspl.c` | Filter entry point: argument handling, SIGTERM, raster read loop, pixel unpacking for all supported colour spaces/bit depths, resampling to 203 dpi, clipping, image shift | `$(cups-config --serverbin)/filter/sp410-rastertotspl` (with the three files below) |
| `src/settings.c`, `src/settings.h` | Option model; resolves PPD defaults, then job options, with range validation and fallback | ″ |
| `src/dither.c`, `src/dither.h` | Ink-plane → 1-bit: threshold, Atkinson, Floyd–Steinberg (serpentine), 8×8 Bayer; Auto photo detection | ″ |
| `src/tspl.c`, `src/tspl.h` | TSPL writer: per-label setup, band detection and trimming, bit inversion, C-locale number formatting | ″ |
| `ppd/idprt-sp410.ppd` | PPD for the SP410 (generated) | `/usr/share/ppd/sp410-cups-driver/` |
| `ppd/idprt-sp410bt.ppd` | PPD for the SP410BT (generated) | ″ |
| `ppd/idprt-sp420.ppd` | PPD for the SP420 (generated) | ″ |
| `udev/60-idprt-sp410.rules` | USB permissions, autosuspend off, `/dev/usb/idprt-sp410-N` symlink | `/usr/lib/udev/rules.d/` |
| `tools/sp410ctl.py` | Direct printer utility: status, info, calibrate, self-test, feed, reset, test label, raw send (USB / serial / TCP) | `/usr/bin/sp410ctl` |
| `tools/tspl_decode.py` | TSPL stream parser, renderer (PBM/PNG) and differ; also the test oracle | `/usr/bin/tspl-decode` |
| `README.md` | Overview, install, printing, options | `/usr/share/doc/sp410-cups-driver/` |
| `docs/PROTOCOL.md` | Wire protocol: commands emitted, BITMAP encoding, status byte | ″ |
| `docs/HARDWARE.md` | Models, Raspberry Pi setup, calibration, troubleshooting | ″ |
| `LICENSE` | Apache License 2.0 | `/usr/share/doc/sp410-cups-driver/copyright` |
| `CHANGELOG.md` | Release notes | `/usr/share/doc/sp410-cups-driver/changelog.gz` |

## Build, packaging and installation

| Path | Purpose |
|---|---|
| `Makefile` | `all`, `check`, `ppd`, `ppd-check`, `install`, `uninstall`, `deb`, `dist`, `clean`; finds libcups via `cups-config` or `pkg-config` |
| `VERSION` | Single source of the version number (filter, PPDs, package) |
| `ppd/gen_ppd.py` | Generates all PPDs from one option table; `--check` fails on stale PPDs |
| `packaging/build-deb.sh` | Builds `build/sp410-cups-driver_<ver>_<arch>.deb` with `dpkg-deb` (amd64/arm64/armhf) |
| `scripts/install.sh` | Source install for end users; optional dependency install and queue creation with USB URI auto-detection |
| `scripts/uninstall.sh` | Removes a source install, optionally the queues that use it |

## Tests

| Path | Purpose |
|---|---|
| `tests/run_tests.py` | 28 end-to-end cases: dot-exact rendering, every input format, options, multi-page, clipping, resampling, shift, locale, bad/truncated input, SIGTERM, performance, `sp410ctl` against a fake printer, capture extraction |
| `tests/mkraster.c` | Generates CUPS/PWG raster test pages and the exact expected PBM |
| `tests/ppdcheck.c` | Parses PPDs with libcups; checks options, defaults, page sizes, filter line |

## Reverse-engineering tools (not installed)

| Path | Purpose |
|---|---|
| `tools/re/inspect-vendor-pkg.sh` | Static inventory of a vendor zip/deb: files, hashes, maintainer scripts, PPD option tables, ELF imports and TSPL strings, hints |
| `tools/re/compare-vendor.sh` | Runs vendor filter and ours on the same rasters (optionally under `qemu-x86_64`) and diffs the decoded output |
| `tools/re/usbcap2prn.py` | Extracts host→printer bytes from usbmon or USBPcap captures (pcap/pcapng) |
| `docs/REVERSE_ENGINEERING.md` | Clean-room rules, known facts with sources, the four-tier method, open questions |

## Project metadata

| Path | Purpose |
|---|---|
| `MANIFEST.md` | This file |
| `CONTRIBUTING.md` | Hardware reports, clean-room rules, code and test conventions |
| `.github/workflows/ci.yml` | CI: x86-64 and arm64 build + tests + sanitizers + `.deb`; Debian bookworm armhf (Raspberry Pi OS) under QEMU; ShellCheck and PPD freshness |
| `.gitignore` | Build output and RE workspaces (vendor material must never be committed) |
| `.editorconfig` | Indentation and line-ending conventions |

## Generated at build time (not in git)

| Path | Produced by |
|---|---|
| `build/sp410-rastertotspl` | `make` |
| `build/mkraster`, `build/ppdcheck` | `make` (test helpers) |
| `build/*.deb` | `make deb` |
| `build/sp410-cups-driver-<ver>.tar.gz` | `make dist` |
