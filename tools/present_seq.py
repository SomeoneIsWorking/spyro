#!/usr/bin/env python3
"""present_seq.py — read a run of CONSECUTIVE present captures as a SEQUENCE, not as N pictures.

WHY THIS EXISTS. Every flicker/ghosting question in this port is about the RELATION between
consecutive presents, and the two instruments already here answer neither:

  * `non-black %` reads **93.33% on both the good and the broken class** (issue 0045). It is a
    constant of the display geometry, not a measurement of the picture.
  * `ppm_look.py` reports one file at a time. "Is present N a repeat of N-1" and "is present N
    carrying pixels that only N-2 ever drew" are questions about pairs.

So this tool reports, per present, the three numbers that separate FLAT (the composite lost the
geometry), FROZEN (nothing advances) and GHOSTING (stale pixels survive a frame that should have
repainted them):

    colours   distinct RGB triples. The FLAT class is 2 (the guest's clear fill with no geometry
              over it); a real Spyro scene is thousands. This is the instrument issue 0045's fix
              was measured with, and it is kept as the anchor so the two runs are comparable.
    d(prev)   % of pixels differing from the PREVIOUS present. Liveness on the present axis.
    d(2back)  % of pixels differing from present N-2 — i.e. the previous time THIS display buffer
              was on screen, since the guest is double-buffered and presents each drawn frame twice.

  GHOSTING TEST (`--ghost`). Ghosting from a persistent composite means: pixels that the guest did
  NOT write this frame, left over from an older frame. It shows up as a THREE-WAY relation, which is
  why a pairwise diff cannot see it: a pixel is a ghost candidate when it differs from its value one
  present ago but EQUALS its value two presents ago (i.e. it reverted to the older buffer's content
  instead of being repainted). With a moving camera and correct repainting that set is tiny and
  scattered; a real ghost is a large, spatially coherent region. Both the count and the bounding box
  are printed, because "0.4% of pixels" that all sit in one rectangle is a ghost and the same 0.4%
  scattered over the frame is dither.

WHAT A NEGATIVE PRINTS. Every run prints its denominator: how many files it was given, how many it
could read, the frame indices it actually compared, and — when a class is empty — the words "0 of N",
never a bare "(none)". Given a directory that does not exist or a set with fewer than 3 usable files
it EXITS NON-ZERO saying it compared nothing, because "no ghosting found" and "I never looked" must
not be the same output.

SELF-TEST (`--selftest`). Synthesises three 8x8 frames in which a known 3x3 block reverts to its
two-back value and asserts the ghost detector reports exactly that block. Runs in-process, no files.

Usage:
    present_seq.py <file.ppm> [more.ppm ...]      # sorted by the number in the filename
    present_seq.py --ghost <files...>             # add the three-way ghost-candidate analysis
    present_seq.py --png <files...>               # also write a .png beside each input
    present_seq.py --selftest
"""
import os
import re
import struct
import sys
import zlib


def read_ppm(path):
    """Return (w, h, rgb_bytes). Raises on anything that is not a P6 PPM — a caller must not be
    able to mistake an unreadable file for an empty picture."""
    d = open(path, 'rb').read()
    if not d.startswith(b'P6'):
        raise ValueError(f'{path}: not a P6 PPM (starts {d[:2]!r})')
    parts, i = [], 0
    while len(parts) < 4:
        while d[i:i + 1].isspace():
            i += 1
        j = i
        while not d[j:j + 1].isspace():
            j += 1
        parts.append(d[i:j])
        i = j
    i += 1
    w, h = int(parts[1]), int(parts[2])
    rgb = d[i:i + w * h * 3]
    if len(rgb) != w * h * 3:
        raise ValueError(f'{path}: truncated ({len(rgb)} of {w*h*3} bytes)')
    return w, h, rgb


def write_png(path, w, h, rgb):
    raw = b''.join(b'\x00' + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(t, data):
        return (struct.pack('>I', len(data)) + t + data
                + struct.pack('>I', zlib.crc32(t + data) & 0xffffffff))

    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n'
                           + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def colours(rgb):
    return len({rgb[i:i + 3] for i in range(0, len(rgb), 3)})


def pixels_differing(a, b):
    """Number of PIXELS (not bytes) whose RGB triple differs."""
    n = 0
    for i in range(0, len(a), 3):
        if a[i:i + 3] != b[i:i + 3]:
            n += 1
    return n


def ghost_pixels(prev2, prev1, cur, w):
    """Pixels that differ from the previous present but EQUAL the one before it — the signature of
    content reverting to an older buffer instead of being repainted. Returns (count, bbox)."""
    n = 0
    x0 = y0 = 1 << 30
    x1 = y1 = -1
    for i in range(0, len(cur), 3):
        c = cur[i:i + 3]
        if c != prev1[i:i + 3] and c == prev2[i:i + 3]:
            n += 1
            p = i // 3
            x, y = p % w, p // w
            x0 = min(x0, x); x1 = max(x1, x)
            y0 = min(y0, y); y1 = max(y1, y)
    return n, (None if x1 < 0 else (x0, y0, x1, y1))


def frame_no(path):
    m = re.findall(r'(\d+)', os.path.basename(path))
    return int(m[-1]) if m else -1


def selftest():
    w = h = 8
    npx = w * h
    base = bytearray(b'\x10\x20\x30' * npx)
    # frame A: base. frame B: a 3x3 block at (2,2) repainted white. frame C: that block reverts.
    a = bytes(base)
    b = bytearray(base)
    for y in range(2, 5):
        for x in range(2, 5):
            i = (y * w + x) * 3
            b[i:i + 3] = b'\xff\xff\xff'
    b = bytes(b)
    c = bytes(base)
    n, box = ghost_pixels(a, b, c, w)
    ok = (n == 9 and box == (2, 2, 4, 4))
    print(f'[selftest] ghost_pixels -> count={n} (expect 9) bbox={box} (expect (2, 2, 4, 4))')
    # negative leg: a detector that cannot report zero is worthless, so prove it reports zero too.
    n0, box0 = ghost_pixels(a, b, b, w)
    ok = ok and n0 == 0 and box0 is None
    print(f'[selftest] no-ghost case  -> count={n0} (expect 0) bbox={box0} (expect None)')
    print('[selftest] ' + ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


def main(argv):
    want_ghost = '--ghost' in argv
    want_png = '--png' in argv
    if '--selftest' in argv:
        return selftest()
    files = [a for a in argv if not a.startswith('--')]
    if not files:
        print(__doc__)
        return 2
    files.sort(key=frame_no)

    frames, bad = [], []
    for p in files:
        try:
            frames.append((p, frame_no(p)) + read_ppm(p))
        except Exception as e:                                    # noqa: BLE001 — reported, never swallowed
            bad.append((p, str(e)))
    for p, e in bad:
        print(f'UNREADABLE {p}: {e}')
    print(f'given {len(files)} file(s); readable {len(frames)}; unreadable {len(bad)}')
    if len(frames) < 3:
        print(f'REFUSING TO REPORT: need >=3 consecutive presents to compare '
              f'(a 2-back diff and a ghost test are three-way); got {len(frames)}. '
              f'NOTHING was compared.')
        return 3

    w, h = frames[0][2], frames[0][3]
    if any(f[2] != w or f[3] != h for f in frames):
        print('REFUSING TO REPORT: the captures are not all the same size — a per-pixel diff across '
              'differing geometry is meaningless. NOTHING was compared.')
        return 3
    npx = w * h
    nums = [f[1] for f in frames]
    gaps = [b - a for a, b in zip(nums, nums[1:])]
    print(f'{w}x{h} = {npx} px · frames {nums[0]}..{nums[-1]} · '
          f'{"CONSECUTIVE" if set(gaps) == {1} else "NOT CONSECUTIVE (gaps %s)" % sorted(set(gaps))}')
    if want_png:
        for p, _, fw, fh, rgb in frames:
            write_png(os.path.splitext(p)[0] + '.png', fw, fh, rgb)
        print(f'wrote {len(frames)} PNG(s) beside the inputs')

    hdr = f'{"frame":>7} {"colours":>8} {"d(prev)%":>9} {"d(2back)%":>10}'
    if want_ghost:
        hdr += f' {"ghost%":>8}  ghost bbox'
    print(hdr)
    flat = 0
    ident_prev = 0
    ghost_rows = []
    for k, (p, n, _, _, rgb) in enumerate(frames):
        col = colours(rgb)
        if col <= 2:
            flat += 1
        dp = dq = None
        if k >= 1:
            d = pixels_differing(frames[k - 1][4], rgb)
            dp = 100.0 * d / npx
            if d == 0:
                ident_prev += 1
        if k >= 2:
            dq = 100.0 * pixels_differing(frames[k - 2][4], rgb) / npx
        row = (f'{n:>7} {col:>8} '
               f'{("%9.3f" % dp) if dp is not None else "        -"} '
               f'{("%10.3f" % dq) if dq is not None else "         -"}')
        if want_ghost:
            if k >= 2:
                g, box = ghost_pixels(frames[k - 2][4], frames[k - 1][4], rgb, w)
                ghost_rows.append((n, g, box))
                row += f' {100.0*g/npx:>8.3f}  {box if box else "-"}'
            else:
                row += f' {"-":>8}  -'
        print(row)

    print(f'\nSUMMARY over {len(frames)} present(s):')
    print(f'  FLAT (<=2 distinct colours): {flat} of {len(frames)}'
          + ('   <-- the issue-0045 broken class' if flat else ''))
    print(f'  bit-identical to previous:   {ident_prev} of {len(frames)-1} compared pairs')
    if want_ghost:
        tot = sum(g for _, g, _ in ghost_rows)
        big = [r for r in ghost_rows if r[1] * 1000 > npx]        # >0.1% of the frame
        print(f'  ghost-candidate pixels:      {tot} across {len(ghost_rows)} three-way comparisons '
              f'(mean {100.0*tot/(npx*max(1,len(ghost_rows))):.4f}% of the frame)')
        print(f'  presents with >0.1% ghost candidates: {len(big)} of {len(ghost_rows)}'
              + ('' if big else '   (none — and the detector is self-tested: --selftest)'))
        for n, g, box in big:
            print(f'      frame {n}: {g} px ({100.0*g/npx:.3f}%) bbox {box}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
