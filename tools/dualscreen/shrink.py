#!/usr/bin/env python3
"""Downscale an RGB/RGBA 8-bit PNG by an integer factor (box filter). Usage: shrink.py in.png out.png [factor]"""
import struct
import sys
import zlib


def read_png(path):
    data = open(path, "rb").read()
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", chunk[:10])
            channels = {2: 3, 6: 4}[ctype]
        elif kind == b"IDAT":
            idat += chunk
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = w * channels
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = prev[i]
            c = prev[i - channels] if i >= channels else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return w, h, channels, rows


def main():
    src, dst = sys.argv[1], sys.argv[2]
    k = int(sys.argv[3]) if len(sys.argv) > 3 else 2
    w, h, ch, rows = read_png(src)
    ow, oh = w // k, h // k
    out = bytearray()
    for y in range(oh):
        out.append(0)
        block = rows[y * k:(y + 1) * k]
        for x in range(ow):
            for c in range(3):
                s = 0
                for r in block:
                    for dx in range(k):
                        s += r[(x * k + dx) * ch + c]
                out.append(s // (k * k))
    png = b"\x89PNG\r\n\x1a\n"
    for kind, body in ((b"IHDR", struct.pack(">IIBBBBB", ow, oh, 8, 2, 0, 0, 0)),
                       (b"IDAT", zlib.compress(bytes(out), 6)), (b"IEND", b"")):
        png += struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    open(dst, "wb").write(png)


main()
