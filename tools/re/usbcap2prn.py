#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The sp410-cups-driver contributors.
"""
usbcap2prn - pull the host->printer byte stream out of a USB packet capture.

Capture the *vendor* driver printing a known page, then extract exactly what
it sent and decode it with tspl-decode.  This is the most faithful way to
learn the vendor's command sequence, because it observes the wire rather
than the binary.

Supported captures (pcap or pcapng, no third-party modules needed):
  * Linux usbmon       (Wireshark/tcpdump on usbmonN; DLT 189 and 220)
  * Windows USBPcap    (Wireshark + USBPcap; DLT 249)

  usbcap2prn capture.pcapng -o vendor.prn            # all bulk-OUT data
  usbcap2prn capture.pcapng --list                   # show devices/endpoints
  usbcap2prn capture.pcapng --device 7 -o vendor.prn # pick one device
  tspl-decode vendor.prn

How to capture:  see docs/REVERSE_ENGINEERING.md ("Wire capture").
"""

from __future__ import annotations

import argparse
import collections
import pathlib
import struct
import sys

DLT_USB_LINUX = 189
DLT_USB_LINUX_MMAPPED = 220
DLT_USBPCAP = 249


def iter_packets(data: bytes):
    """Yield (linktype, packet_bytes) from pcap or pcapng."""
    if len(data) < 24:
        raise ValueError("file too short to be a capture")
    magic = data[:4]
    if magic in (b"\xd4\xc3\xb2\xa1", b"\x4d\x3c\xb2\xa1", b"\xa1\xb2\xc3\xd4", b"\xa1\xb2\x3c\x4d"):
        e = "<" if magic in (b"\xd4\xc3\xb2\xa1", b"\x4d\x3c\xb2\xa1") else ">"
        linktype = struct.unpack(e + "I", data[20:24])[0] & 0x0FFFFFFF
        pos = 24
        while pos + 16 <= len(data):
            incl = struct.unpack(e + "I", data[pos + 8:pos + 12])[0]
            yield linktype, data[pos + 16:pos + 16 + incl]
            pos += 16 + incl
        return
    if magic == b"\x0a\x0d\x0d\x0a":
        pos, e, ifaces = 0, "<", []
        while pos + 12 <= len(data):
            btype = struct.unpack(e + "I", data[pos:pos + 4])[0]
            if btype == 0x0A0D0D0A:                              # SHB
                bom = data[pos + 8:pos + 12]
                e = "<" if bom == b"\x4d\x3c\x2b\x1a" else ">"
                ifaces = []
            blen = struct.unpack(e + "I", data[pos + 4:pos + 8])[0]
            if blen < 12:
                raise ValueError(f"corrupt pcapng block at {pos}")
            body = data[pos + 8:pos + blen - 4]
            if btype == 1:                                       # IDB
                ifaces.append(struct.unpack(e + "H", body[:2])[0])
            elif btype == 6:                                     # EPB
                if_id, _, _, cap = struct.unpack(e + "IIII", body[:16])
                yield ifaces[if_id], body[20:20 + cap]
            elif btype == 3:                                     # SPB
                yield ifaces[0], body[4:]
            pos += blen
        return
    raise ValueError("not a pcap/pcapng file")


def bulk_out(linktype: int, pkt: bytes):
    """Return (device, endpoint, payload) for a host->device bulk transfer
    submission, else None."""
    if linktype in (DLT_USB_LINUX, DLT_USB_LINUX_MMAPPED):
        hdr = 64 if linktype == DLT_USB_LINUX_MMAPPED else 48
        if len(pkt) < hdr:
            return None
        ev, xfer, ep, dev = pkt[8], pkt[9], pkt[10], pkt[11]
        if ev != ord("S") or xfer != 3 or ep & 0x80:
            return None
        return dev, ep & 0x7F, pkt[hdr:]
    if linktype == DLT_USBPCAP:
        if len(pkt) < 27:
            return None
        hlen = struct.unpack("<H", pkt[:2])[0]
        info = pkt[16]
        dev = struct.unpack("<H", pkt[19:21])[0]
        ep, xfer = pkt[21], pkt[22]
        if xfer != 3 or ep & 0x80 or info & 1:                   # bulk, OUT, submission
            return None
        return dev, ep & 0x7F, pkt[hlen:]
    return None


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="usbcap2prn", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture", type=pathlib.Path)
    ap.add_argument("-o", "--output", type=pathlib.Path)
    ap.add_argument("--device", type=int, help="USB device address to keep")
    ap.add_argument("--endpoint", type=int, help="bulk-OUT endpoint number to keep")
    ap.add_argument("--list", action="store_true", help="summarize bulk-OUT traffic")
    args = ap.parse_args(argv)

    stats = collections.Counter()
    out = bytearray()
    for lt, pkt in iter_packets(args.capture.read_bytes()):
        r = bulk_out(lt, pkt)
        if not r:
            continue
        dev, ep, payload = r
        stats[(dev, ep)] += len(payload)
        if args.device is not None and dev != args.device:
            continue
        if args.endpoint is not None and ep != args.endpoint:
            continue
        out += payload

    if args.list or not args.output:
        for (dev, ep), n in sorted(stats.items()):
            print(f"device {dev:3d}  endpoint {ep:2d} OUT  {n:10d} bytes")
        if not stats:
            print("no bulk-OUT transfers found", file=sys.stderr)
            return 1
        if not args.output:
            return 0
    if len(stats) > 1 and args.device is None:
        print("warning: several devices/endpoints present; use --device", file=sys.stderr)
    args.output.write_bytes(bytes(out))
    print(f"wrote {len(out)} bytes to {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
