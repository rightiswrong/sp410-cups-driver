# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [0.1.0] - unreleased

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
