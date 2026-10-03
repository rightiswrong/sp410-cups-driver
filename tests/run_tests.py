#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The sp410-cups-driver contributors.
"""
End-to-end tests for sp410-rastertotspl (no printer or cupsd required).

Each case: mkraster -> filter -> tools/tspl_decode.py -> assertions on the
emitted commands and on the rendered dots (exact where the input is pure
black/white, statistical where halftoning is involved).

    python3 tests/run_tests.py --filter build/sp410-rastertotspl --mkraster build/mkraster
"""

from __future__ import annotations

import argparse
import os
import pathlib
import signal
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import tspl_decode  # noqa: E402

PPD = ROOT / "ppd" / "idprt-sp410.ppd"


class Ctx:
    def __init__(self, filt: pathlib.Path, mkr: pathlib.Path, tmp: pathlib.Path):
        self.filter, self.mkraster, self.tmp = filt, mkr, tmp

    def raster(self, pattern: str):
        ras = self.tmp / f"{pattern}.ras"
        pbm = self.tmp / f"{pattern}.pbm"
        subprocess.run([str(self.mkraster), pattern, str(ras), str(pbm)], check=True)
        return ras, (read_pbm(pbm) if pbm.exists() else None)

    def run(self, ras, options="", ppd=True, env_extra=None, expect_rc=0):
        env = dict(os.environ)
        env.pop("PPD", None)
        if ppd:
            env["PPD"] = str(PPD)
        if env_extra:
            env.update(env_extra)
        p = subprocess.run([str(self.filter), "42", "tester", "title", "1", options, str(ras)],
                           capture_output=True, env=env, timeout=60)
        if p.returncode != expect_rc:
            raise AssertionError(f"filter exit {p.returncode} (want {expect_rc}):\n"
                                 + p.stderr.decode(errors="replace"))
        return p.stdout, p.stderr.decode(errors="replace")


def read_pbm(path: pathlib.Path):
    data = path.read_bytes()
    parts = data.split(b"\n", 2)
    assert parts[0] == b"P4"
    w, h = map(int, parts[1].split())
    return w, h, bytearray(parts[2][: ((w + 7) // 8) * h])


def get(bits, stride, x, y):
    return (bits[y * stride + (x >> 3)] >> (7 - (x & 7))) & 1


def assert_same_dots(label, expected, dx=0, dy=0):
    """Label must equal `expected` shifted by (dx, dy); anything outside the
    expected image must be blank."""
    ew, eh, ebits = expected
    es = (ew + 7) // 8
    bad = 0
    for y in range(label.height):
        for x in range(label.width):
            sx, sy = x - dx, y - dy
            want = get(ebits, es, sx, sy) if 0 <= sx < ew and 0 <= sy < eh else 0
            if label.get(x, y) != want:
                bad += 1
    if bad:
        raise AssertionError(f"{bad} dots differ from expected image")


def lines(stream):
    return [c.text for c in stream.commands]


def density(label, x0, x1, y0, y1):
    tot = blk = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            blk += label.get(x, y)
            tot += 1
    return blk / tot


# ---------------------------------------------------------------------------

def t_threshold_exact_k1(c):
    ras, exp = c.raster("shipping-k1")
    out, _ = c.run(ras, "Dither=Threshold")
    s = tspl_decode.parse(out)
    assert not s.warnings, s.warnings
    assert len(s.labels) == 1
    L = lines(s)
    assert L[0] == "SIZE 101.6 mm,152.4 mm", L[0]
    assert "GAP 3 mm,0 mm" in L            # PPD default
    assert "DIRECTION 0" in L and "REFERENCE 0,0" in L and "SET TEAR ON" in L
    assert not any(l.startswith(("DENSITY", "SPEED", "OFFSET")) for l in L), \
        "printer-default darkness/speed must not be sent"
    assert L[-1] == "PRINT 1,1"
    assert_same_dots(s.labels[0], exp)


def t_auto_keeps_line_art_crisp_w8(c):
    ras, exp = c.raster("shipping-w8")
    out, _ = c.run(ras, "")                # Dither=Auto from PPD
    assert_same_dots(tspl_decode.parse(out).labels[0], exp)


def t_k8_input(c):
    ras, exp = c.raster("shipping-k8")
    out, _ = c.run(ras, "Dither=Threshold")
    assert_same_dots(tspl_decode.parse(out).labels[0], exp)


def t_rgb_input_and_bands(c):
    ras, exp = c.raster("rgb")
    out, err = c.run(ras, "Dither=Threshold")
    s = tspl_decode.parse(out)
    lb = s.labels[0]
    assert_same_dots(lb, exp)
    bm = [x for x in s.commands if x.text.startswith("BITMAP")]
    assert len(bm) == 2, f"expected 2 bands (blank rows 100-119 split them), got {len(bm)}"
    assert bm[0].text.startswith("BITMAP 8,10,"), bm[0].text      # left/top trimmed
    full = ((lb.width + 7) // 8) * lb.height
    assert sum(b.payload for b in bm) < full * 0.6, "band trimming not effective"


def t_pwg_raster(c):
    ras, exp = c.raster("pwg")
    out, _ = c.run(ras, "")
    s = tspl_decode.parse(out)
    assert lines(s)[0] == "SIZE 101.6 mm,50.8 mm"
    assert_same_dots(s.labels[0], exp)


def t_auto_halftones_photos(c):
    ras, _ = c.raster("gray")
    out, _ = c.run(ras, "")
    lb = tspl_decode.parse(out).labels[0]
    strips = [density(lb, x, x + 80, 200, 400) for x in (10, 110, 210, 310)]
    assert strips == sorted(strips), f"gradient not monotonic: {strips}"
    assert 0.05 < strips[1] < 0.6 and 0.4 < strips[2] < 0.95, strips
    assert density(lb, 450, 780, 50, 150) == 1.0, "solid block must stay solid"


def t_threshold_does_not_dither(c):
    ras, _ = c.raster("gray")
    out, _ = c.run(ras, "Dither=Threshold")
    lb = tspl_decode.parse(out).labels[0]
    assert density(lb, 0, 190, 0, 600) == 0.0
    assert density(lb, 215, 406, 0, 600) == 1.0


def t_all_dither_modes(c):
    ras, _ = c.raster("gray")
    for mode in ("Atkinson", "FloydSteinberg", "Ordered"):
        out, _ = c.run(ras, f"Dither={mode}")
        lb = tspl_decode.parse(out).labels[0]
        d = density(lb, 0, 406, 0, 600)
        assert 0.38 < d < 0.62, f"{mode}: mean density {d:.3f}"
        mid = density(lb, 190, 215, 0, 600)
        assert 0.2 < mid < 0.8, f"{mode}: midtone density {mid:.3f}"


def t_threshold_level(c):
    ras, _ = c.raster("gray")
    a = tspl_decode.parse(c.run(ras, "Dither=Threshold Threshold=64")[0]).labels[0]
    b = tspl_decode.parse(c.run(ras, "Dither=Threshold Threshold=192")[0]).labels[0]
    assert a.black_dots() > b.black_dots()


def t_multi_page_sizes(c):
    ras, exp = c.raster("multi")
    out, err = c.run(ras, "Dither=Threshold")
    s = tspl_decode.parse(out)
    assert len(s.labels) == 3
    sizes = [l for l in lines(s) if l.startswith("SIZE")]
    assert sizes == ["SIZE 101.6 mm,152.4 mm", "SIZE 50.8 mm,25.4 mm",
                     "SIZE 101.6 mm,50.8 mm"], sizes
    assert s.labels[2].bitmaps == 0 and s.labels[2].black_dots() == 0
    assert_same_dots(s.labels[0], exp)
    assert err.count("PAGE: ") == 3


def t_setup_options(c):
    ras, _ = c.raster("copies")
    out, _ = c.run(ras, "Darkness=12 PrintSpeed=5 MediaTracking=BlackMark GapHeight=4 "
                        "GapOffset=1 PrintDirection=Rotate180 TearOff=False TearOffset=-2")
    L = lines(tspl_decode.parse(out))
    for want in ("BLINE 4 mm,1 mm", "DIRECTION 1", "OFFSET -2 mm", "SPEED 5",
                 "DENSITY 12", "SET TEAR OFF"):
        assert want in L, f"missing {want!r} in {L}"


def t_continuous(c):
    ras, _ = c.raster("copies")
    out, _ = c.run(ras, "MediaTracking=Continuous")
    assert "GAP 0,0" in lines(tspl_decode.parse(out))


def t_bad_options_fall_back(c):
    ras, _ = c.raster("copies")
    out, err = c.run(ras, "Darkness=99 PrintSpeed=fast Dither=Sparkly GapHeight=abc")
    L = lines(tspl_decode.parse(out))
    assert not any(l.startswith(("DENSITY", "SPEED")) for l in L)
    assert "GAP 3 mm,0 mm" in L
    assert err.count("WARNING: Ignoring") == 4, err


def t_no_ppd_builtin_defaults(c):
    ras, _ = c.raster("copies")
    out, _ = c.run(ras, "", ppd=False)
    assert "GAP 2 mm,0 mm" in lines(tspl_decode.parse(out))


def t_hardware_copies(c):
    ras, _ = c.raster("copies")
    out, err = c.run(ras, "")
    s = tspl_decode.parse(out)
    assert lines(s)[-1] == "PRINT 1,3" and s.labels[0].copies == 3
    assert "PAGE: 1 3" in err


def t_wide_page_clipped(c):
    ras, _ = c.raster("wide")
    out, err = c.run(ras, "")
    s = tspl_decode.parse(out)
    lb = s.labels[0]
    assert lines(s)[0].startswith("SIZE 108 mm,"), lines(s)[0]
    assert lb.width == 864 and "clipped" in err
    assert density(lb, 0, 864, 40, 60) == 1.0
    assert not s.warnings and not lb.warnings


def t_resample_300dpi(c):
    ras, _ = c.raster("dpi300")
    out, err = c.run(ras, "")
    s = tspl_decode.parse(out)
    lb = s.labels[0]
    assert "Resampling" in err
    assert lines(s)[0] == "SIZE 101.6 mm,50.8 mm"
    # 300 dpi rect (300..900, 150..450) -> ~ (203..609, 101..304) at 203 dpi
    assert density(lb, 206, 606, 104, 301) == 1.0
    assert density(lb, 0, 199, 0, 406) == 0.0 and density(lb, 613, 812, 0, 406) == 0.0


def t_odd_width_padding(c):
    ras, exp = c.raster("odd")
    out, _ = c.run(ras, "")
    s = tspl_decode.parse(out)
    lb = s.labels[0]
    assert lb.width == 457 and not lb.warnings, lb.warnings     # padding bits white
    assert_same_dots(lb, exp)


def t_shift(c):
    ras, exp = c.raster("rgb")
    out, _ = c.run(ras, "Dither=Threshold ShiftX=2 ShiftY=-1")
    assert_same_dots(tspl_decode.parse(out).labels[0], exp, dx=16, dy=-8)


def t_locale_independent(c):
    ras, _ = c.raster("odd")
    out, _ = c.run(ras, "", env_extra={"LC_ALL": "de_DE.UTF-8", "LANG": "de_DE.UTF-8"})
    assert lines(tspl_decode.parse(out))[0] == "SIZE 57.2 mm,31.8 mm"


def t_garbage_input_rejected(c):
    bad = c.tmp / "garbage.ras"
    bad.write_bytes(b"this is not a raster stream" * 10)
    out, err = c.run(bad, "", expect_rc=1)
    assert out == b"" and "ERROR" in err
    empty = c.tmp / "empty.ras"
    empty.write_bytes(b"")
    c.run(empty, "", expect_rc=1)


def t_truncated_input(c):
    ras, _ = c.raster("shipping-k1")
    cut = c.tmp / "cut.ras"
    cut.write_bytes(ras.read_bytes()[:50000])
    out, err = c.run(cut, "", expect_rc=1)
    assert out == b"", "a partial page must not reach the printer"
    assert "Short raster data" in err


def t_sigterm_mid_job(c):
    ras, _ = c.raster("shipping-k1")
    env = dict(os.environ, PPD=str(PPD))
    p = subprocess.Popen([str(c.filter), "1", "u", "t", "1", ""], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
    data = ras.read_bytes()
    p.stdin.write(data[:40000])
    p.stdin.flush()
    time.sleep(0.3)
    p.send_signal(signal.SIGTERM)
    # Let communicate() close stdin itself: closing it first makes
    # Python <= 3.12 raise "flush of closed file".
    out, err = p.communicate(timeout=10)
    assert p.returncode == 0, err
    assert out == b"", "canceled page must not be emitted"


def t_performance(c):
    ras, _ = c.raster("shipping-w8")
    t0 = time.monotonic()
    c.run(ras, "Dither=Atkinson")
    dt = time.monotonic() - t0
    assert dt < 2.0, f"4x6 Atkinson page took {dt:.2f}s"


def t_decoder_diff(c):
    ras, _ = c.raster("rgb")
    a = tspl_decode.parse(c.run(ras, "Dither=Threshold")[0])
    b = tspl_decode.parse(c.run(ras, "Dither=Threshold ShiftX=1")[0])
    import io
    assert tspl_decode.diff(a, a, io.StringIO()) == 0
    assert tspl_decode.diff(a, b, io.StringIO()) > 0


def _fake_printer(replies: dict):
    """Tiny TCP 'printer': records what it receives, answers queries."""
    import socket
    import threading

    srv = socket.socket()
    srv.bind(("127.0.0.1", 0))
    srv.listen(1)
    got = bytearray()

    def serve():
        conn, _ = srv.accept()
        conn.settimeout(3)
        try:
            while True:
                data = conn.recv(4096)
                if not data:
                    break
                got.extend(data)
                for q, a in replies.items():
                    if got.endswith(q):
                        conn.sendall(a)
        except OSError:
            pass
        finally:
            conn.close()
            srv.close()

    th = threading.Thread(target=serve, daemon=True)
    th.start()
    return srv.getsockname()[1], got, th


def _ctl(*args):
    return subprocess.run([sys.executable, str(ROOT / "tools/sp410ctl.py"), *args],
                          capture_output=True, text=True, timeout=20)


def t_sp410ctl_status_over_tcp(c):
    port, got, th = _fake_printer({b"\x1b!?": b"\x05"})
    p = _ctl("--host", f"127.0.0.1:{port}", "status")
    th.join(5)
    assert bytes(got) == b"\x1b!?", got
    assert p.returncode == 1 and "print head open" in p.stdout and "out of paper" in p.stdout, p.stdout


def t_sp410ctl_info_and_test_label(c):
    port, got, th = _fake_printer({b"~!T\r\n": b"SP410\r", b"~!I\r\n": b"8 437\r",
                                   b"~!@\r\n": b"12.3\r"})
    p = _ctl("--host", f"127.0.0.1:{port}", "--timeout", "1", "info")
    th.join(5)
    assert "model: SP410" in p.stdout and "code page: 8 437" in p.stdout, p.stdout
    p = _ctl("--dry-run", "test-label", "--size", "100x150mm")
    s = tspl_decode.parse(p.stdout.encode())
    assert len(s.labels) == 1 and s.labels[0].width == 800 and s.labels[0].height == 1200
    assert any(u.startswith("BARCODE") for u in s.labels[0].unrendered)


def _usbmon_pkt(ev, xfer, ep, dev, payload):
    import struct
    hdr = struct.pack("<QBBBBHbbqiiII8siiII", 1, ord(ev), xfer, ep, dev, 1, 0, 0,
                      0, 0, 0, len(payload), len(payload), b"\0" * 8, 0, 0, 0, 0)
    assert len(hdr) == 64
    return hdr + payload


def _usbpcap_pkt(info, dev, ep, xfer, payload):
    import struct
    hdr = struct.pack("<HQIHBHHBBI", 27, 1, 0, 0x09, info, 1, dev, ep, xfer, len(payload))
    return hdr + payload


def t_usbcap2prn_pcap_and_pcapng(c):
    import struct
    job = c.tmp / "job.prn"
    ras, _ = c.raster("copies")
    stream, _ = c.run(ras, "")
    chunks = [stream[i:i + 4096] for i in range(0, len(stream), 4096)]

    # classic pcap, usbmon mmapped (DLT 220), with noise: IN traffic + other device
    pkts = [_usbmon_pkt("S", 3, 0x81, 5, b"\x00")]
    for ch in chunks:
        pkts += [_usbmon_pkt("S", 3, 0x02, 5, ch), _usbmon_pkt("C", 3, 0x02, 5, b""),
                 _usbmon_pkt("S", 3, 0x01, 9, b"noise")]
    pcap = struct.pack("<IHHiIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 220)
    for p in pkts:
        pcap += struct.pack("<IIII", 0, 0, len(p), len(p)) + p
    cap = c.tmp / "usbmon.pcap"
    cap.write_bytes(pcap)
    r = subprocess.run([sys.executable, str(ROOT / "tools/re/usbcap2prn.py"), str(cap),
                        "--device", "5", "-o", str(job)], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    assert job.read_bytes() == stream

    # pcapng, USBPcap (DLT 249)
    def block(btype, body):
        pad = (-len(body)) % 4
        n = 12 + len(body) + pad
        return struct.pack("<II", btype, n) + body + b"\0" * pad + struct.pack("<I", n)

    ng = block(0x0A0D0D0A, struct.pack("<IHHq", 0x1A2B3C4D, 1, 0, -1))
    ng += block(1, struct.pack("<HHI", 249, 0, 65535))
    for ch in chunks:
        for p in (_usbpcap_pkt(0, 3, 0x01, 3, ch), _usbpcap_pkt(1, 3, 0x01, 3, b"")):
            ng += block(6, struct.pack("<IIIII", 0, 0, 0, len(p), len(p)) + p)
    cap = c.tmp / "usbpcap.pcapng"
    cap.write_bytes(ng)
    r = subprocess.run([sys.executable, str(ROOT / "tools/re/usbcap2prn.py"), str(cap),
                        "-o", str(job)], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    assert job.read_bytes() == stream


TESTS = [v for k, v in sorted(globals().items()) if k.startswith("t_")]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--filter", type=pathlib.Path, default=ROOT / "build/sp410-rastertotspl")
    ap.add_argument("--mkraster", type=pathlib.Path, default=ROOT / "build/mkraster")
    ap.add_argument("-k", help="run only tests whose name contains this")
    args = ap.parse_args()

    fails = 0
    with tempfile.TemporaryDirectory(prefix="sp410-test-") as tmp:
        ctx = Ctx(args.filter.resolve(), args.mkraster.resolve(), pathlib.Path(tmp))
        tests = [t for t in TESTS if not args.k or args.k in t.__name__]
        print(f"1..{len(tests)}")
        for i, t in enumerate(tests, 1):
            name = t.__name__[2:]
            try:
                t(ctx)
                print(f"ok {i} - {name}")
            except Exception as e:  # noqa: BLE001
                fails += 1
                print(f"not ok {i} - {name}\n  # {type(e).__name__}: {e}")
    print(f"# {len(tests) - fails}/{len(tests)} passed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
