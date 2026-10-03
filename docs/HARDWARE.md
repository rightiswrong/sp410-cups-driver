# Hardware notes, setup and troubleshooting

## Models

| Model | Head | Max width | Interface | PPD | Status |
|---|---|---|---|---|---|
| iDPRT SP410 | 203 dpi | 108 mm | USB | `idprt-sp410.ppd` | protocol-compatible, tested with the named hardware, works when compiled and installed on RPi 3B+. I'm expecting this will work on RPi 4 and above as well.|
| iDPRT SP410BT | 203 dpi | 108 mm | USB, Bluetooth SPP | `idprt-sp410bt.ppd` | protocol-compatible, awaiting hardware report |
| iDPRT SP420 | 203 dpi | 108 mm | USB | `idprt-sp420.ppd` | protocol-compatible; other open TSPL filters report it working |

Other 203-dpi TSPL printers (HPRT SL42, iDPRT SP310/SP320 with a narrower
head) will very likely work with the SP410 PPD and a smaller page size, but
are out of scope until someone reports results.

**Please report your unit:** open an issue with `lsusb` output, the line for
your printer from `sudo /usr/lib/cups/backend/usb`, `sp410ctl info`, and
whether the `test-label` and a CUPS test page printed correctly.

## Raspberry Pi quick setup (Raspberry Pi OS Bookworm, armhf or arm64)

```sh
sudo apt update
sudo apt install -y git build-essential libcups2-dev cups cups-filters python3
sudo usermod -aG lpadmin,lp "$USER"        # log out/in afterwards

git clone <this repository> sp410-cups-driver
cd sp410-cups-driver
make check                                  # optional, runs the test suite
sudo ./scripts/install.sh --add-queue SP410
lp -d SP410 /usr/share/cups/data/testprint
```

If a vendor driver was installed before, remove its queue first
(`lpadmin -x <name>`) and its package (`dpkg -l | grep -i -e tspl -e idprt`,
then `sudo dpkg -r <package>`), otherwise two filters fight over the queue.

## Checking the connection without CUPS

```sh
lsusb | grep -i 20d1                 # printer enumerated?
dmesg | grep -i usblp                # /dev/usb/lp0 created?
sudo cupsdisable SP410               # keep CUPS from grabbing the port
sp410ctl status                      # "status 0x00: ready"
sp410ctl info                        # model / code page / mileage
sp410ctl test-label --size 4x6       # printed with built-in fonts, no raster
sudo cupsenable SP410
```

CUPS' `usb` backend talks to the printer through libusb and may detach
`usblp`; if `/dev/usb/lp0` is missing while CUPS is idle, unplug and replug.

## Calibration

After loading a new roll, or if labels skip or print across gaps:

```sh
sp410ctl calibrate              # die-cut labels with gaps
sp410ctl calibrate --blackmark  # black-mark media
```

or hold the feed button per the printer's manual. Then set the queue's
**Media Tracking** (and gap height, if your labels differ from 3 mm):

```sh
lpadmin -p SP410 -o MediaTracking-default=Gap -o GapHeight-default=3
```

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| "Filter failed" | filter missing/wrong architecture (the vendor package's classic Pi failure) | `file /usr/lib/cups/filter/sp410-rastertotspl` must match `dpkg --print-architecture`; reinstall |
| Nothing prints, job "completed" | queue points at the wrong URI | `lpstat -v SP410`, compare with `lpinfo -v` |
| Every other label blank / skips a label | wrong gap setting or uncalibrated | calibrate; check Media Tracking and Gap / Mark Height |
| Image shifted / clipped at the tear bar | mechanical tolerances | `-o ShiftX=…`, `-o ShiftY=…` (mm) or `-o TearOffset=…` |
| Barcodes fuzzy or won't scan | photo halftoning applied | `-o Dither=Threshold`; keep barcodes at integer dot widths in the source |
| Gray boxes look blotchy | threshold mode on a photo | `-o Dither=Atkinson` (or `Ordered` for flat fills) |
| Too light / too dark | darkness | `-o Darkness=10` (0–15); faster speeds print lighter |
| Right edge cut off | page wider than 108 mm | choose a page size ≤ 108 mm wide |
| Decimal sizes misread by printer | locale-dependent formatting in another driver | not possible with this filter (C-locale formatting) |

Debug logging:

```sh
sudo cupsctl --debug-logging
lp -d SP410 file.pdf
sudo grep -E 'sp410|Page [0-9]+:' /var/log/cups/error_log | tail -40
sudo cupsctl --no-debug-logging
```

To see exactly what was sent, print to a file queue and decode it:

```sh
sudo lpadmin -p SP410-capture -E -v file:/tmp/sp410.prn -P /usr/share/ppd/sp410-cups-driver/idprt-sp410.ppd
lp -d SP410-capture label.pdf && tspl-decode /tmp/sp410.prn --png-dir /tmp/render
```

(`file:` device URIs require `FileDevice Yes` in `/etc/cups/cups-files.conf`.)
