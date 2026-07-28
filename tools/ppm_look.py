#!/usr/bin/env python3
"""ppm_look.py — summarise and convert the port's VRAM captures.

WHY. Every capture question in this project has been "is this a real frame, an empty buffer, or a
broken instrument?", and answering it by eye needs a PNG plus a few numbers. Doing that inline in a
shell one-liner each time is how the earlier all-black captures got read as "the port renders black"
instead of "this is the wrong buffer".

Reports non-black coverage and distinct-colour count per file — the pair that separates the three
cases. One colour means an EMPTY buffer or a broken read; a real frame has thousands. Converts to PNG
alongside so the image can actually be looked at rather than described.

Usage:  ppm_look.py <file.ppm> [more.ppm ...]
"""
import struct, sys, zlib, os

def read_ppm(p):
    d = open(p, 'rb').read()
    parts, i = [], 0
    while len(parts) < 4:
        while d[i:i+1].isspace(): i += 1
        j = i
        while not d[j:j+1].isspace(): j += 1
        parts.append(d[i:j]); i = j
    i += 1
    w, h = int(parts[1]), int(parts[2])
    return w, h, d[i:i+w*h*3]

def write_png(path, w, h, rgb):
    raw = b''.join(b'\x00' + rgb[y*w*3:(y+1)*w*3] for y in range(h))
    def chunk(t, data):
        return struct.pack('>I', len(data)) + t + data + struct.pack('>I', zlib.crc32(t+data) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n'
        + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
        + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))

def main():
    if len(sys.argv) < 2:
        print(__doc__); return 1
    for p in sys.argv[1:]:
        if not os.path.exists(p):
            print(f"{os.path.basename(p):22s} MISSING"); continue
        w, h, rgb = read_ppm(p)
        nz = sum(1 for i in range(0, len(rgb), 3) if rgb[i] or rgb[i+1] or rgb[i+2])
        cols = len(set(rgb[i:i+3] for i in range(0, len(rgb), 3)))
        png = p.rsplit('.', 1)[0] + '.png'
        write_png(png, w, h, rgb)
        verdict = ("EMPTY buffer or dead read" if cols <= 2
                   else "real frame" if cols > 200 else "suspicious — few colours")
        print(f"{os.path.basename(p):22s} {w}x{h}  non-black {100.0*nz/(w*h):5.1f}%  colours {cols:5d}  {verdict}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
