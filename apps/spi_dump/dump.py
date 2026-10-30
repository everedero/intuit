#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Collect the spi_dump stream into a flash image: dump.py [port] [out.bin]"""
import struct
import sys
import zlib

import serial

SIZE, BLK = 8 << 20, 4096
FRAME = 3 + 4 + BLK + 4


def parse(buf, blocks):
    """Take every valid frame out of buf, return the unparsed tail."""
    while True:
        i = buf.find(b"BLK")
        if i < 0:
            return buf[-2:]
        if len(buf) - i < FRAME:
            return buf[i:]
        f = buf[i + 3:i + FRAME]
        addr, data, (crc,) = f[:4], f[4:-4], struct.unpack("<I", f[-4:])
        a = struct.unpack("<I", addr)[0]
        if zlib.crc32(addr + data) == crc and a < SIZE and a % BLK == 0:
            blocks[a] = data
            buf = buf[i + FRAME:]
        else:
            buf = buf[i + 1:]       # false magic or damaged frame: resync


def selftest():
    blocks = {}
    good = lambda a, d: b"BLK" + struct.pack("<I", a) + d + struct.pack("<I", zlib.crc32(struct.pack("<I", a) + d))
    bad = bytearray(good(BLK, b"\x11" * BLK)); bad[100] ^= 1
    tail = parse(b"junkBLK" + good(0, b"BLK" * 1365 + b"x") + bytes(bad) + good(2 * BLK, b"\x22" * BLK)[:50], blocks)
    assert sorted(blocks) == [0] and blocks[0][:3] == b"BLK" and tail.startswith(b"BLK")


if __name__ == "__main__":
    selftest()
    port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
    out = sys.argv[2] if len(sys.argv) > 2 else "out/spi_flash.bin"
    blocks, buf = {}, b""
    with serial.Serial(port, 2000000, timeout=1) as s:
        while len(blocks) < SIZE // BLK:
            buf = parse(buf + s.read(65536), blocks)
            print(f"\r{len(blocks)}/{SIZE // BLK} blocks", end="", flush=True)
    with open(out, "wb") as f:
        f.write(b"".join(blocks[a] for a in range(0, SIZE, BLK)))
    print(f"\nwrote {out}")
