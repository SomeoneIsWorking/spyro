#!/usr/bin/env python3
"""shot.py — get a PNG of what the game actually drew, at a frame you choose.

WHY THIS IS NOT ONE COMMAND ALREADY. Two of the port's capture paths look right and lie:

  * `PSXPORT_SHOT_AT` / REPL `shot` capture [s_disp_x, s_disp_y]. In this port that returns a
    two-colour flat fill for frames the game renders perfectly (instruments I032, I033). It is not
    obviously broken — the colour even changes between frames — which is what makes it expensive.
  * `PSXPORT_GPU_DUMP` reads s_vram, which the Vulkan rasterizer never touches, so it reads as
    permanently black the moment real geometry starts (instrument I008).

The one capture that has never lied here is the REPL's full-VRAM dump: both framebuffers in a single
1024x512 image, so you can SEE which one holds the frame instead of trusting an origin. This wraps
that, and crops the framebuffer for you.

AND IT PICKS AN ODD FRAME. This port submits ~1600 primitives on odd frames and ZERO on even ones
(double buffered). Every early capture attempt in this project landed on an even frame and produced
a picture of nothing. Asking for an even frame here bumps it by one and says so.

Usage:
  shot.py 46501                        # -> scratch/screenshots/f46501.png (the framebuffer)
  shot.py 46501 --full                 # the whole 1024x512 VRAM, texture atlas included
  shot.py 46501 --secs 240
"""
import argparse
import os
import re
import struct
import subprocess
import sys
import zlib

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def disc_path():
    d = os.environ.get("PSXPORT_SPYRO_DISC", "")
    if not d:
        env = os.path.join(REPO, ".env")
        if os.path.exists(env):
            for line in open(env):
                m = re.match(r"\s*PSXPORT_SPYRO_DISC\s*=\s*(.+)", line)
                if m:
                    d = m.group(1).strip()
                    break
    return d


def read_png(path):
    d = open(path, "rb").read()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit(f"{path}: not a PNG")
    i, idat, w = 8, b"", None
    while i < len(d):
        ln = struct.unpack(">I", d[i:i + 4])[0]
        typ, dat = d[i + 4:i + 8], d[i + 8:i + 8 + ln]
        if typ == b"IHDR":
            w, h, bd, ct = struct.unpack(">IIBB", dat[:10])
        elif typ == b"IDAT":
            idat += dat
        i += 12 + ln
    raw = zlib.decompress(idat)
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ct]
    bpp = ch * bd // 8
    rows, prev, pos = [], bytearray(w * bpp), 0
    for _ in range(h):
        f = raw[pos]; pos += 1
        line = bytearray(raw[pos:pos + w * bpp]); pos += w * bpp
        for x in range(len(line)):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 255
        rows.append(bytes(line)); prev = line
    return w, h, ch, rows


def write_png(path, w, h, ch, rows):
    raw = b"".join(b"\x00" + r for r in rows)
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n"
                           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("frame", type=int)
    ap.add_argument("--full", action="store_true", help="keep the whole 1024x512 VRAM")
    ap.add_argument("--width", type=int, default=512,
                    help="framebuffer width to crop (default 512 = this game's 4:3 frame). A WIDER "
                         "aspect renders wider — 684 at 16:9, 896 at 21:9 — and cropping to 512 shows "
                         "the left part of a wider picture, which reads as a shifted, cropped, broken "
                         "render. That nearly got recorded as a renderer fault; it was this crop.")
    ap.add_argument("--secs", type=int, default=240)
    a = ap.parse_args()

    frame = a.frame
    if frame % 2 == 0:
        frame += 1
        print(f"frame {a.frame} is EVEN and submits no primitives in this port — using {frame}",
              file=sys.stderr)

    disc = disc_path()
    if not disc or not os.path.exists(disc):
        sys.exit("no disc image (set PSXPORT_SPYRO_DISC or .env)")
    out = os.path.join(REPO, "scratch", "screenshots")
    os.makedirs(out, exist_ok=True)
    vram = os.path.join(out, f"vram_f{frame}.png")

    env = dict(os.environ, PSXPORT_REPL="1", PSXPORT_VK_HEADLESS="1", PSXPORT_NOAUDIO="1",
               PSXPORT_WATCHDOG="0", PSXPORT_ASSET_DIR="external/psxport", PSXPORT_SPYRO_DISC=disc)
    pre_mtime = os.path.getmtime(vram) if os.path.exists(vram) else None
    script = f"run {frame}\nvram {os.path.relpath(vram, REPO)}\nquit\n"
    print(f"running to frame {frame} …", file=sys.stderr)
    subprocess.run(["timeout", "-s", "KILL", str(a.secs),
                    "./scratch/bin/spyro_port", "scratch/bin/spyro/SCUS_942.28"],
                   cwd=REPO, env=env, input=script, text=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not os.path.exists(vram):
        sys.exit(f"no VRAM dump produced — did the run reach frame {frame} within {a.secs}s?")
    if pre_mtime is not None and os.path.getmtime(vram) == pre_mtime:
        sys.exit(f"{vram} was NOT rewritten by this run — it is a PREVIOUS capture. Refusing to "
                 f"hand it back: copying this file would silently mislabel one run's picture as "
                 f"another's, which is exactly how the mute map (C138) recorded three renderers "
                 f"wrongly and stood for a day.")

    w, h, ch, rows = read_png(vram)
    if a.full:
        print(os.path.relpath(vram, REPO))
        return 0
    # The framebuffers are the left half: 512x240 at y=0 and y=240. Keep whichever holds the frame,
    # measured rather than assumed — the empty one is a fresh clear and has almost no distinct colours.
    fbw = min(a.width, w)
    # CHOOSE THE BUFFER USING ONLY THE 4:3 REGION. Columns beyond it are the TEXTURE ATLAS, not
    # framebuffer, and atlas data is far more colour-varied than any rendered scene — so scoring the
    # full wide width lets the atlas decide which buffer "has the frame" and it picks the wrong one.
    # The 4:3 columns are always framebuffer, so they are the honest discriminator.
    pick_w = min(512, w)
    def variety(y0):
        s = set()
        for y in range(y0, min(y0 + 240, h)):
            r = rows[y]
            for x in range(0, pick_w, 4):
                s.add(r[x * ch:x * ch + 3])
        return len(s)
    top, bot = variety(0), variety(240)
    y0 = 0 if top >= bot else 240
    crop = [rows[y][0:fbw * ch] for y in range(y0, min(y0 + 240, h))]
    png = os.path.join(out, f"f{frame}.png")
    write_png(png, fbw, len(crop), ch, crop)
    print(f"{os.path.relpath(png, REPO)}   (buffer at y={y0}; {top} vs {bot} distinct colours)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
