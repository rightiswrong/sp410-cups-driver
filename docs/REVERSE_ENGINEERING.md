# Reverse-engineering notes

How this driver was built, how to keep checking it against iDPRT's
closed-source Linux/Raspberry Pi package, and what is still unknown.

## Ground rules (clean room)

The goal is **interoperability**: make the SP410 print from any Linux
machine, including the Raspberry Pi the vendor package does not properly
support. To keep the project's copyright clean:

1. **Never commit vendor files** — binaries, PPDs, scripts, extracted
   strings, decompiler output, screenshots of disassembly. `vendor/`,
   `vendor-inspect/` and `compare-out/` are git-ignored for this reason.
2. **Record facts, in your own words.** "The vendor filter sends `SET TEAR ON`
   once per label" is a finding. A pasted function body is not.
3. **Prefer observation over disassembly.** Everything the driver needs is
   visible on the wire; tiers 1–3 below have answered every question so far.
   Use tier 4 only for a specific, documented interoperability question.
4. Whoever disassembles should not be the person who writes the matching
   code; they hand over a written description of the behaviour.

Reverse engineering for interoperability is generally permitted (e.g. EU
Software Directive art. 6, US 17 U.S.C. §1201(f), *Sega v. Accolade*), but
conditions apply and the iDPRT download terms may add restrictions. This is
not legal advice — if in doubt, stay with tiers 1–3.

## What is already known (public sources)

| Fact | Source |
|---|---|
| SP410 speaks TSPL; 203 dpi (8 dots/mm); max print width 108 mm; max length 300 mm; gap, black-mark, paper-out and head-open sensors | iDPRT SP410 product page |
| iDPRT offers a Linux driver (v1.4.x) and a separate "Raspberry Pi driver" download for the SP410 | iDPRT downloads page |
| The vendor Linux package's CUPS filter is a binary named `raster-tspl`; v1.4.2 shipped only x86/x86-64 builds, so on a Pi it installs the wrong architecture and jobs fail with "filter failed" | Raspberry Pi forum thread #343166 |
| An older Pi build exists as a zip containing an armhf `.deb` (`com.tspl.idprt_1.3.0_armhf`); the PPD model shows as "iDPRT SP410, 1.3.0" | same thread |
| USB ID `20d1:7008` reported for SP410/SP420 | RunTheWall/tspl-cups-driver compatibility table |
| Independent TSPL CUPS filters exist and print on SP420-class hardware, confirming plain `BITMAP` raster printing works without vendor code | thorrak/rpi-tspl-cups-driver, RunTheWall/tspl-cups-driver, ch0dak/Label-Printer-for-Linux |
| TSPL `BITMAP` data is MSB-first with **1 = white, 0 = black** | TSPL programming manuals (TSC), independently confirmed by community implementations |

**Status of the vendor binary itself:** the iDPRT download server refuses
automated fetching, so the package has not yet been run through the tooling
below. Running `inspect-vendor-pkg.sh` and `compare-vendor.sh` on a manually
downloaded copy is the first item on the open-questions list.

## Tier 1 — static inventory

```sh
# download the Raspberry Pi and Linux driver zips from idprt.com by hand
mkdir -p vendor && mv ~/Downloads/*SP410* vendor/
tools/re/inspect-vendor-pkg.sh vendor/<file>.zip vendor-inspect/
less vendor-inspect/report.md
```

The report lists every file with hashes, the `.deb` control data and
maintainer scripts, each PPD's identity lines and complete option table
(including invocation code — `cupsInteger`/`cupsString` assignments reveal
how the vendor filter receives settings), and for every ELF: architecture,
needed libraries, imported symbols and TSPL-looking strings. A "hints"
section flags imports that answer open questions directly, e.g. a zlib
`compress` import means the vendor probably uses a compressed bitmap command.

What to transcribe (in your words) into the table at the bottom of this file:
PPD option names, ranges and defaults; the TSPL keywords present in the
binary; architecture list; anything that suggests status polling.

## Tier 2 — black-box differential testing

Run the vendor filter and ours on identical raster input and diff the
decoded output. The vendor filter is a normal CUPS filter, so it can be run
by hand with no printer and no cupsd:

```sh
make
tools/re/compare-vendor.sh \
    --vendor-filter vendor-inspect/extracted/…/usr/lib/cups/filter/raster-tspl \
    --vendor-ppd    vendor-inspect/extracted/…/SP410.ppd \
    --out compare-out
```

On a Raspberry Pi with only the x86-64 build available, run it under
user-mode emulation: `sudo apt install qemu-user libc6-amd64-cross`, then add
`--wrapper "qemu-x86_64 -L /usr/x86_64-linux-gnu"` (the blob also needs an
x86-64 `libcups.so.2` in that prefix). On any x86-64 Linux machine no wrapper
is needed.

For each test pattern you get both streams, command listings, PBM renders and
a diff of setup commands and dots. Things to look for:

* **setup differences** — extra commands (`SET COUNTER`, `CODEPAGE`, `SHIFT`,
  `LIMITFEED`, `SET GAP`…), different defaults, different ordering;
* **size differences** — a vendor stream much smaller than ours means a
  compressed bitmap format; `tspl-decode` will report its header as an
  unknown command, which is the starting point for tier 3/4;
* **dot differences on the `gray` pattern** — reveals the vendor's halftone
  algorithm and threshold;
* **option mapping** — sweep one vendor option at a time with
  `--vendor-options "VendorDarkness=…"` and watch which TSPL value changes.

Use `--raster page.ras` to compare on real documents; create the raster with
`cupsfilter -m application/vnd.cups-raster -p ppd/idprt-sp410.ppd doc.pdf > page.ras`.

## Tier 3 — wire capture

The most faithful record of what reaches the printer, and the only way to
observe the Windows/macOS drivers or the vendor label-design software.

**Linux (usbmon):**

```sh
sudo modprobe usbmon
lsusb | grep 20d1                        # note the Bus number, e.g. 001
sudo tcpdump -i usbmon1 -w capture.pcap  # or Wireshark on usbmon1
# … print a test page with the vendor driver, then Ctrl-C …
```

**Windows (USBPcap):** install Wireshark with the USBPcap component, capture
on the root hub the printer is attached to, print, save as `.pcapng`.

Then:

```sh
tools/re/usbcap2prn.py capture.pcapng --list          # find the printer's device address
tools/re/usbcap2prn.py capture.pcapng --device 7 -o vendor.prn
tools/tspl_decode.py vendor.prn --pbm-dir vendor-render/
tools/tspl_decode.py vendor.prn --diff ours.prn
```

Capturing `sp410ctl status`/`info` exchanges the same way (IN endpoint)
documents reply formats.

## Tier 4 — disassembly (only when needed)

If tiers 1–3 leave a specific question open (for example the exact framing
of a compressed bitmap command), load the vendor `raster-tspl` into Ghidra,
find the `fwrite`/`printf` call sites that emit the relevant keyword from the
string table, and describe the behaviour in prose here. Follow ground rule 4.

## Open questions

| # | Question | Current assumption in the driver | How to settle it | Status |
|---|---|---|---|---|
| 1 | USB VID:PID of retail SP410 units | `20d1:7008` in udev rule | `lsusb` on real units | needs reports |
| 2 | IEEE-1284 device ID string | `MFG:iDPRT;MDL:SP410;CMD:TSPL;` in PPD (only used for auto-matching) | `sudo /usr/lib/cups/backend/usb` or `lpinfo -l -v` | needs reports |
| 3 | Does the vendor filter use a compressed bitmap command? | No; plain `BITMAP` mode 0 | tier 1 hints + tier 2 stream sizes | open |
| 4 | Vendor default gap and media settings | `GAP 3 mm,0 mm`; "Printer Default" darkness/speed | tier 1 PPD defaults + tier 2 | open |
| 5 | Accepted `SPEED` values | 2–6 ips offered (1 accepted by filter) | send each with `sp410ctl raw`, observe | open |
| 6 | `DENSITY` range and visual effect | 0–15 | darkness sweep test labels | open |
| 7 | Status byte bit 6 | "cover open / model-specific" | open the cover, `sp410ctl status` | open |
| 8 | Firmware max label length | 300 mm (2400 dots), clipped by filter | print a 300 mm continuous test | open |
| 9 | `GAPDETECT` vs `AUTODETECT` support | `GAPDETECT` | `sp410ctl calibrate`, watch feed | open |
| 10 | Horizontal origin for labels narrower than 4" | printer centers from `SIZE` width; no x offset added | print a 2" label with border | open |
| 11 | Vendor halftone algorithm/threshold | ours: Auto (threshold, Atkinson for gray fills), threshold 128 | tier 2 `gray` pattern | open |
| 12 | Does the vendor filter query status mid-job? | no back-channel use | tier 1 `cupsBackChannel` import | open |

When you settle one, update the table, the affected code/PPD defaults, and
add a line to `CHANGELOG.md`.
