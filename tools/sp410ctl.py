#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The sp410-cups-driver contributors.
"""
sp410ctl - talk to an iDPRT SP410-family printer directly (no CUPS).

Useful for bring-up, calibration and for checking that USB/Bluetooth/network
communication works before blaming the driver.

  sp410ctl status                     # ESC !? status byte, decoded
  sp410ctl info                       # model name, code page, mileage
  sp410ctl calibrate [--blackmark]    # GAPDETECT / BLINEDETECT
  sp410ctl selftest                   # prints the configuration label
  sp410ctl feed | home | reset
  sp410ctl test-label [--size 4x6]    # label drawn with printer-resident fonts
  sp410ctl raw job.prn                # send a file verbatim

Transport (default: auto-detect a usblp node):
  --device /dev/usb/lp0               # USB (usblp) or /dev/rfcomm0 (SP410BT)
  --host 192.168.1.50[:9100]          # raw TCP (print servers)
  --dry-run                           # write the bytes to stdout instead

Without the udev rule from this project you may need `sudo` or membership
of the `lp` group.  Stop CUPS jobs first: the printer handles one host at a time.
"""

from __future__ import annotations

import argparse
import glob
import os
import select
import socket
import sys
import time

ESC = b"\x1b"

STATUS_BITS = [
    (0x01, "print head open"),
    (0x02, "paper jam"),
    (0x04, "out of paper"),
    (0x08, "out of ribbon"),
    (0x10, "paused"),
    (0x20, "printing"),
    (0x40, "cover open / model-specific"),
    (0x80, "other error"),
]


def decode_status(b: int) -> list[str]:
    if b == 0:
        return ["ready"]
    return [name for bit, name in STATUS_BITS if b & bit]


class Transport:
    def write(self, data: bytes) -> None: ...
    def read(self, timeout: float) -> bytes: ...
    def close(self) -> None: ...


class DevTransport(Transport):
    def __init__(self, path: str):
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY)

    def write(self, data: bytes) -> None:
        view = memoryview(data)
        while view:
            n = os.write(self.fd, view)
            view = view[n:]

    def read(self, timeout: float) -> bytes:
        out = b""
        end = time.monotonic() + timeout
        while True:
            left = end - time.monotonic()
            if left <= 0:
                break
            r, _, _ = select.select([self.fd], [], [], left)
            if not r:
                break
            chunk = os.read(self.fd, 256)
            if not chunk:
                break
            out += chunk
            if out.endswith((b"\r", b"\n")) or len(out) >= 1 and timeout < 0.6:
                # short replies (status byte) arrive in one read
                break
        return out

    def close(self) -> None:
        os.close(self.fd)


class TcpTransport(Transport):
    def __init__(self, host: str, port: int):
        self.s = socket.create_connection((host, port), timeout=5)

    def write(self, data: bytes) -> None:
        self.s.sendall(data)

    def read(self, timeout: float) -> bytes:
        self.s.settimeout(timeout)
        out = b""
        try:
            while True:
                chunk = self.s.recv(256)
                if not chunk:
                    break
                out += chunk
                if out.endswith((b"\r", b"\n")) or timeout < 0.6:
                    break
        except socket.timeout:
            pass
        return out

    def close(self) -> None:
        self.s.close()


class DryRun(Transport):
    def write(self, data: bytes) -> None:
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()

    def read(self, timeout: float) -> bytes:
        return b""

    def close(self) -> None:
        pass


def autodetect() -> str | None:
    for pattern in ("/dev/usb/idprt-sp410-*", "/dev/usb/lp*", "/dev/rfcomm*"):
        hits = sorted(glob.glob(pattern))
        if hits:
            return hits[0]
    return None


def open_transport(args) -> Transport:
    if args.dry_run:
        return DryRun()
    if args.host:
        host, _, port = args.host.partition(":")
        return TcpTransport(host, int(port or 9100))
    dev = args.device or autodetect()
    if not dev:
        sys.exit("sp410ctl: no printer device found; use --device or --host")
    try:
        return DevTransport(dev)
    except PermissionError:
        sys.exit(f"sp410ctl: permission denied on {dev} "
                 "(install udev/60-idprt-sp410.rules, join group 'lp', or use sudo)")


def parse_size(s: str) -> tuple[float, float]:
    """'4x6' (inches) or '100x150mm' -> (width_mm, height_mm)."""
    mm = s.lower().endswith("mm")
    w, h = s.lower().removesuffix("mm").split("x")
    f = 1.0 if mm else 25.4
    return float(w) * f, float(h) * f


def test_label(width_mm: float, height_mm: float, gap_mm: float) -> bytes:
    """A label that only uses printer-resident commands (no raster)."""
    wd, hd = int(width_mm * 8), int(height_mm * 8)
    m = 16
    cmds = [
        f"SIZE {width_mm:g} mm,{height_mm:g} mm",
        f"GAP {gap_mm:g} mm,0 mm",
        "DIRECTION 0",
        "REFERENCE 0,0",
        "CLS",
        f"BOX {m},{m},{wd - m},{hd - m},4",
        f'TEXT {m + 16},{m + 16},"3",0,1,1,"sp410-cups-driver"',
        f'TEXT {m + 16},{m + 56},"2",0,1,1,"TSPL test label {width_mm:g}x{height_mm:g} mm"',
    ]
    if hd > 240:
        cmds.append(f'BARCODE {m + 16},{m + 100},"128",80,1,0,2,2,"SP410-OK-1234"')
    if hd > 420 and wd > 300:
        cmds.append(f'QRCODE {m + 16},{m + 230},L,6,A,0,"https://github.com/"')
    cmds.append("PRINT 1,1")
    return ("\r\n".join(cmds) + "\r\n").encode("ascii")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="sp410ctl", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--device")
    ap.add_argument("--host")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--timeout", type=float, default=2.0)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status")
    sub.add_parser("info")
    c = sub.add_parser("calibrate")
    c.add_argument("--blackmark", action="store_true")
    sub.add_parser("selftest")
    sub.add_parser("feed")
    sub.add_parser("home")
    sub.add_parser("reset")
    t = sub.add_parser("test-label")
    t.add_argument("--size", default="4x6", help="4x6 (in) or 100x150mm")
    t.add_argument("--gap", type=float, default=3.0, help="gap in mm")
    r = sub.add_parser("raw")
    r.add_argument("file")
    args = ap.parse_args(argv)

    tr = open_transport(args)
    try:
        if args.cmd == "status":
            tr.write(ESC + b"!?")
            reply = tr.read(min(args.timeout, 0.5) if not args.dry_run else 0)
            if args.dry_run:
                return 0
            if not reply:
                print("no reply (usblp read not supported, or printer busy/off)")
                return 2
            b = reply[0]
            print(f"status 0x{b:02X}: " + ", ".join(decode_status(b)))
            return 0 if b in (0x00, 0x20) else 1
        if args.cmd == "info":
            for label, q in (("model", b"~!T\r\n"), ("code page", b"~!I\r\n"),
                             ("mileage", b"~!@\r\n")):
                tr.write(q)
                rep = tr.read(args.timeout)
                if not args.dry_run:
                    txt = rep.decode("latin-1").strip() or "(no reply)"
                    print(f"{label:>10}: {txt}")
            return 0
        if args.cmd == "calibrate":
            tr.write(b"BLINEDETECT\r\n" if args.blackmark else b"GAPDETECT\r\n")
        elif args.cmd == "selftest":
            tr.write(b"SELFTEST\r\n")
        elif args.cmd == "feed":
            tr.write(b"FORMFEED\r\n")
        elif args.cmd == "home":
            tr.write(b"HOME\r\n")
        elif args.cmd == "reset":
            tr.write(ESC + b"!R")
        elif args.cmd == "test-label":
            w, h = parse_size(args.size)
            tr.write(test_label(w, h, args.gap))
        elif args.cmd == "raw":
            with open(args.file, "rb") as f:
                tr.write(f.read())
        return 0
    finally:
        tr.close()


if __name__ == "__main__":
    sys.exit(main())
