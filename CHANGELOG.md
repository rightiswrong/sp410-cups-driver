# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [1.0.0] - 2026-10-04

First stable release.

### Verified
- Printing to a physical iDPRT SP410 over USB from a Raspberry Pi 3B+
  (compiled and installed from source) confirmed working.

### Added
- Release packages built on Debian 12 "bookworm" for amd64, arm64 and armhf,
  so one `.deb` per architecture installs on Raspberry Pi OS (bookworm or
  later), Debian 12+ and Ubuntu 24.04+.
- Release automation: pushing a `v*` tag, or running the CI workflow by hand
  with "publish_release", rebuilds, retests and publishes the `.deb`
  packages, a source tarball and SHA-256 checksums as a GitHub release.

### Changed
- PPD version strings are now 1.0.0. Existing queues keep working; re-select
  the driver in CUPS to pick up the new PPD text.

## [0.1.0] - 2026-10-03 (pre-release, CI only)

### Fixed
- Cancel test failed on Python 3.11/3.12 ("flush of closed file"), which
  stopped every CI job before the `.deb` packages were built.
- CI now shows the tail of a failing build as an error annotation.

### Added
- `sp410-rastertotspl` CUPS filter: CUPS v1–v3 and PWG raster input in
  1/2/4/8/16-bit K, W/sGray, sRGB/AdobeRGB and CMYK; automatic resampling of
  non-203-dpi rasters; Auto/Threshold/Atkinson/Floyd–Steinberg/Ordered
  halftoning; head-width and max-length clipping; image shift; banded,
  trimmed `BITMAP` output; per-label setup; hardware copies from the raster
  header; C-locale number formatting; clean SIGTERM handling (no partial
  labels sent).
- Generated PPDs for SP410, SP410BT and SP420 with 21 label sizes and custom
  sizes up to 108 × 300 mm.
- udev rule (permissions, autosuspend off, stable device symlink).
- `sp410ctl` printer utility (status, info, calibrate, self-test, feed,
  reset, test label, raw) over USB, Bluetooth serial or TCP.
- `tspl-decode` stream parser/renderer/differ.
- Reverse-engineering tooling: vendor package inspector, USB capture
  extractor (usbmon and USBPcap, pcap and pcapng), vendor-vs-ours
  differential runner.
- 28-case end-to-end test suite, PPD checker, `.deb` builder, install and
  uninstall scripts, CI for x86-64 and arm64.
