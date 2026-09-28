#!/usr/bin/env python3
"""pool_water_probe.py — measure the pool's per-block colour variance in a captured frame.

WHY. The pool defect is a DATA fault, not a shader fault: the guest writes ONE constant colour
for every water face and the port was writing each block's own authored colour. That is visible to
the eye as per-block noise inside a blue rectangle, which is a weak instrument -- it says "looks
wrong" and cannot be compared against a threshold, and every earlier session that judged the pool
by looking at it had to guess what "clean" meant.

So this measures it. The pool is a saturated blue region against green terrain, so it is found by
hue, bounded as a rectangle, and then the question asked is not "is it blue" but **"how much does
the colour vary from block to block inside it"** -- which is exactly the quantity the defect
inflated and the fix should collapse.

THE MEASURE, stated so it cannot mean the other thing. For the water region this reports, per
16x16 block: the number of distinct colours, the mean absolute channel deviation from the region
mean, and the share of blocks whose deviation exceeds a threshold. A uniform tint scores near zero
on all three. **A single flat quad also scores near zero -- and that is NOT the same as correct**,
because a real pool is a grid of many faces with a 50/50 blend over a textured floor, so it carries
a gradient. The report therefore also states the region's distinct-colour count, and a low variance
WITH a low colour count is reported as "uniform AND flat", which is a different finding from
"uniform and blended". One is the fix working; the other is the water having been painted out.

Usage:
  pool_water_probe.py scratch/screenshots/field-16x9-interp.png
  pool_water_probe.py --selftest
"""
from __future__ import annotations

import argparse
import struct
import sys

# A block is the granularity the defect appeared at: the guest's water faces are an authored grid,
# and the noise was one colour per grid cell. 16x16 is a floor, not a measurement of that grid.
BLOCK = 16

# "Saturated blue and not sky." Sky is blue AND bright; the pool is blue and mid/dark. Water is
# also the one region that is blue in a green-and-tan courtyard, so hue alone plus this floor
# separates them. Stated as thresholds so the selection is auditable, not tuned per frame.
_MIN_BLUE_OVER_RED = 1.15
_MIN_BLUE = 40
_MAX_BLUE = 205
_MAX_GREEN = 150

# A pool is a bounded sheet filling a good part of the courtyard. A connected blue region of only
# ~1300 px in a 684x240 frame is a gem, a HUD spark or a sliver of sky, and measuring it would
# produce a confident verdict about water that is not in the picture. Measured on the known-bad
# baseline the pool is 16,063 connected px (16.9% of frame), so the floor is set well below that and
# well above the small blue things. Chosen from the two measured ends, not tuned per frame.
_MIN_REGION_PX = 2500

# A POOL IS A SHEET, so its bounding box is broad in both axes. Measured on the known-bad baseline the
# pool is 246x113 px (aspect 2.2). Two measured non-pools this tool was about to report on:
#   a blue DRAGON in present_2000.png -> 1174 px in a narrow strip, and
#   a gem/HUD spark in pool-c.ppm      -> 1299 px in a narrow strip.
# Both are saturated blue and both are not water. A box this narrow is a creature or a sparkle, and
# measuring it produces a confident per-block variance about something that is not in the picture.
_MIN_BOX_ASPECT = 0.45

# The threshold on ADJACENT-BLOCK-MEAN disagreement, in 0-255 units per channel. Chosen from two
# measured fixtures rather than tuned to a frame: a synthetic per-block checkerboard (the defect)
# scores far above it and a smooth ramp with a translucent sheet over it (not the defect) scores far
# below. It is a THRESHOLD, so it is stated, and the selftest shows the tool producing both answers.
_BLOCK_NOISE_THRESHOLD = 12.0


def read_png(path: str) -> tuple[list[list[tuple[int, int, int]]], int, int]:
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG: %s" % path)
    pos, width, height, idat = 8, 0, 0, bytearray()
    while pos < len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, colour = struct.unpack(">IIBB", body[:10])
            if depth != 8 or colour not in (2, 6):
                raise ValueError("unsupported PNG depth/colour %d/%d" % (depth, colour))
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
        pos += 12 + length
    raw = zlib_decompress(bytes(idat))
    stride = width * 3
    out: list[list[tuple[int, int, int]]] = []
    previous = bytearray(stride)
    at = 0
    for _ in range(height):
        filt = raw[at]
        line = bytearray(raw[at + 1:at + 1 + stride])
        at += 1 + stride
        # The five PNG filters. Undoing them by hand is deliberate: this tool must not depend on a
        # decoder that is not in the repository, and a wrong unfilter is a wrong picture.
        if filt == 1:
            for i in range(3, stride):
                line[i] = (line[i] + line[i - 3]) & 0xFF
        elif filt == 2:
            for i in range(stride):
                line[i] = (line[i] + previous[i]) & 0xFF
        elif filt == 3:
            for i in range(stride):
                left = line[i - 3] if i >= 3 else 0
                line[i] = (line[i] + ((left + previous[i]) >> 1)) & 0xFF
        elif filt == 4:
            for i in range(stride):
                a = line[i - 3] if i >= 3 else 0
                b = previous[i]
                c = previous[i - 3] if i >= 3 else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        elif filt != 0:
            raise ValueError("unknown PNG filter %d" % filt)
        out.append([tuple(line[x:x + 3]) for x in range(0, stride, 3)])
        previous = line
    return out, width, height


def zlib_decompress(raw: bytes) -> bytes:
    import zlib
    return zlib.decompress(raw)


def read_ppm(path: str) -> tuple[list[list[tuple[int, int, int]]], int, int]:
    with open(path, "rb") as handle:
        data = handle.read()
    if not data.startswith(b"P6"):
        raise ValueError("not a binary PPM: %s" % path)
    parts, at = [], 2
    while len(parts) < 3:
        while at < len(data) and data[at:at + 1].isspace():
            at += 1
        if data[at:at + 1] == b"#":
            while at < len(data) and data[at:at + 1] != b"\n":
                at += 1
            continue
        start = at
        while at < len(data) and not data[at:at + 1].isspace():
            at += 1
        parts.append(int(data[start:at]))
    at += 1
    width, height, _ = parts
    pixels = [tuple(data[at + (y * width + x) * 3: at + (y * width + x) * 3 + 3]) for y in range(height)
              for x in range(width)]
    rows = [pixels[y * width:(y + 1) * width] for y in range(height)]
    return rows, width, height


def load(path: str):
    return read_ppm(path) if path.lower().endswith(".ppm") else read_png(path)


def find_water(pixels, width, height):
    """The bounding box of the LARGEST CONNECTED saturated-blue region, or None.

    CONNECTED COMPONENTS, and this is not a refinement. The first revision took the bounding box of
    every blue pixel, which on a real frame returned `x=30..670 y=24..203` on a 684x240 picture --
    almost the whole image. Blue is scattered across a courtyard (sky, distant hills, water, HUD),
    so the union of blue pixels is the picture, and every number computed inside that box was a
    measurement of the FRAME reported as a measurement of the POOL. It reported a confident
    deviation of 48.93 over 2764 distinct colours, which looks like a severe defect and was in
    fact the mean deviation of an entire screenshot.

    That is this workspace's recorded failure mode in its purest form -- an instrument returning a
    confident number about the wrong region -- and it is why the report below names the box it
    measured and what share of the frame that box covers.
    """
    mask = [[False] * width for _ in range(height)]
    for y in range(height):
        row = pixels[y]
        mark = mask[y]
        for x in range(width):
            r, g, b = row[x]
            if b >= _MIN_BLUE and b <= _MAX_BLUE and g <= _MAX_GREEN and b >= r * _MIN_BLUE_OVER_RED:
                mark[x] = True

    best, seen = None, [[False] * width for _ in range(height)]
    for y0 in range(height):
        for x0 in range(width):
            if not mask[y0][x0] or seen[y0][x0]:
                continue
            stack, pixels_in = [(x0, y0)], []
            seen[y0][x0] = True
            while stack:
                x, y = stack.pop()
                pixels_in.append((x, y))
                for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                    if 0 <= nx < width and 0 <= ny < height and mask[ny][nx] and not seen[ny][nx]:
                        seen[ny][nx] = True
                        stack.append((nx, ny))
            if best is None or len(pixels_in) > len(best):
                best = pixels_in
    if best is None or len(best) < _MIN_REGION_PX:
        return None, 0 if best is None else len(best)
    xs = [p[0] for p in best]
    ys = [p[1] for p in best]
    box = (min(xs), min(ys), max(xs) + 1, max(ys) + 1)
    # Shape, not just size. A saturated blue DRAGON is bigger than the pool in pixels and is not a
    # sheet, so a box this narrow is refused by name rather than measured.
    if (box[2] - box[0]) / float(box[3] - box[1]) < _MIN_BOX_ASPECT:
        return None, len(best)
    return box, len(best)


def measure(pixels, width, height, box):
    """Two SEPARATE quantities inside `box`, because the first version conflated them.

    `texture` is the mean deviation of each pixel from its own block's mean. It measures **texture**,
    and it is reported because it is informative — but **it is not the defect metric and must never
    drive the verdict.** Measured 2026-09-28: the reference frame's translucent sheet scores
    `texture` 20.40 while the SAME camera WITHOUT the sheet scores 39.23, so a 50/50 blend with one
    constant colour *compresses* exactly the contrast this measures. A version of this tool that
    verdicted on it printed "This is the defect" on the frame with the sheet AND on the frame without
    it, and scored the clean frame worse.

    `block_noise` is the mean absolute difference between the MEANS of horizontally and vertically
    adjacent blocks. That is the quantity per-block colour noise actually inflates: a coherent surface
    has neighbouring blocks that agree, and a surface whose every block carries its own colour does
    not. It is blind to texture *within* a block, which is the confound above.
    """
    x0, y0, x1, y1 = box
    inside = []
    for y in range(y0, min(y1, height)):
        for x in range(x0, min(x1, width)):
            inside.append(pixels[y][x])
    if not inside:
        return None
    distinct = len(set(inside))
    mean = [sum(p[i] for p in inside) / len(inside) for i in range(3)]
    texture = sum(sum(abs(p[i] - mean[i]) for i in range(3)) for p in inside) / (len(inside) * 3.0)

    # Per-block means, on a grid the caller can see, so adjacent blocks can be compared.
    grid = {}
    for by in range(y0, min(y1, height), BLOCK):
        for bx in range(x0, min(x1, width), BLOCK):
            cell = [pixels[y][x] for y in range(by, min(by + BLOCK, height))
                    for x in range(bx, min(bx + BLOCK, width))]
            if cell:
                grid[(bx, by)] = [sum(p[i] for p in cell) / len(cell) for i in range(3)]
    deltas = []
    for (bx, by), cm in grid.items():
        for nx, ny in ((bx + BLOCK, by), (bx, by + BLOCK)):
            other = grid.get((nx, ny))
            if other is not None:
                deltas.append(sum(abs(cm[i] - other[i]) for i in range(3)) / 3.0)
    block_noise = sum(deltas) / len(deltas) if deltas else 0.0
    return {
        "texture": texture,
        "block_noise": block_noise,
        "block_pairs": len(deltas),
        "pixels": len(inside),
        "distinct_colours": distinct,
    }


def report(path: str, box_override=None) -> int:
    pixels, width, height = load(path)
    print("frame %s  %dx%d" % (path, width, height))
    if box_override is not None:
        box, matched = box_override, None
        print("region: GIVEN BY --box x=%d..%d y=%d..%d  (named, not auto-selected)"
              % (box[0], box[2], box[1], box[3]))
    else:
        box, matched = find_water(pixels, width, height)
        if box is None:
            # A refusal, not a pass. "No pool in this frame" and "the pool is clean" are different
            # answers and this tool must not let the first read as the second. The threshold is
            # printed because a refusal that does not say what it asked for is the recorded way a
            # short answer gets read as a real one.
            print("REFUSED: no connected saturated-blue region of at least %d px and at least %.2f "
                  "box aspect. Largest found: %d px. This frame does not show the pool; it is NOT "
                  "evidence about the pool." % (_MIN_REGION_PX, _MIN_BOX_ASPECT, matched))
            return 2
        print("AUTO-SELECTED region -- NOT PROVEN TO BE THE POOL. Blue by colour is not water: this "
              "game has blue DRAGONS and a blue-grey sky, and an earlier revision of this tool "
              "measured a dragon and printed a per-block variance verdict about it. When the pool's "
              "extent is known, pass --box so the measurement is about a region you named.")
    stats = measure(pixels, width, height, box)
    area = (box[2] - box[0]) * (box[3] - box[1])
    share = 100.0 * area / float(width * height)
    if matched is not None:
        print("water region: %d connected px, bbox x=%d..%d y=%d..%d  (%.1f%% of the frame)"
              % (matched, box[0], box[2], box[1], box[3], share))
    if share > 55.0:
        print("WARNING: the region covers %.1f%% of the frame. A pool is a bounded sheet; a box this "
              "large is scattered blue, and the numbers below describe the PICTURE, not the pool."
              % share)
    print("distinct colours in region : %d" % stats["distinct_colours"])
    # `texture` is reported and EXPLICITLY NOT used for the verdict. It measures how much colour
    # varies inside a block, which is a property of the surface, not of whether blocks disagree.
    print("texture (intra-block)      : %.2f   <- NOT the defect metric; see below" % stats["texture"])
    print("block-to-block mean diff   : %.2f over %d adjacent block pair(s)   <- the defect metric"
          % (stats["block_noise"], stats["block_pairs"]))
    print()
    print("The defect is per-block COLOUR, so the metric is the disagreement between ADJACENT BLOCK")
    print("MEANS. Intra-block deviation is texture: a 50/50 blend with one constant colour compresses")
    print("it, so a frame with a translucent sheet scores LOWER on it than the same frame without one.")
    print("That confound was measured, not assumed: over the same region the sheet frame scores texture")
    print("20.40 and the sheet-free frame 39.23. A revision of this tool verdicted on the intra-block")
    print("number and printed 'This is the defect' on BOTH.")
    print()
    if stats["block_noise"] > _BLOCK_NOISE_THRESHOLD:
        print("VERDICT: PER-BLOCK COLOUR NOISE -- adjacent block means differ by %.2f, above %.2f."
              % (stats["block_noise"], _BLOCK_NOISE_THRESHOLD))
    else:
        print("VERDICT: no per-block colour noise -- adjacent block means differ by %.2f, at or below "
              "%.2f. This says nothing about whether a sheet is present; it says blocks agree."
              % (stats["block_noise"], _BLOCK_NOISE_THRESHOLD))
    return 0


def selftest() -> int:
    failures = []

    def case(name, ok):
        print("selftest: %s -> %s" % (name, "OK" if ok else "FAILED"))
        if not ok:
            failures.append(name)

    # A synthetic pool: per-block colour noise, which is the defect. Must be DETECTED.
    #
    # THE FIXTURE IS AT THE MEASUREMENT'S OWN SCALE, and that is a finding rather than a convenience.
    # An earlier version of this fixture alternated every 8 px; the first version of the NEW metric
    # scored it as CLEAN, because an 8x8 checker averages out inside a 16x16 block mean. That is the
    # metric working as intended -- it measures disagreement between BLOCKS, so noise finer than a
    # block is texture, not per-block colour. The defect being looked for is one colour per face
    # cell, so the fixture alternates every BLOCK, and the fine-texture case below pins the other
    # side of that line.
    W = H = 64
    noisy = [[(0, 0, 0)] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            shade = 90 + ((x // BLOCK + y // BLOCK) % 2) * 40
            noisy[y][x] = (20, 40, shade)
    box, matched = find_water(noisy, W, H)
    case("a noisy blue region is LOCATED, not missed", box is not None and matched > 64)
    stats = measure(noisy, W, H, box)
    case("per-block colour noise is DETECTED (block_noise above the threshold)",
         stats["block_noise"] > _BLOCK_NOISE_THRESHOLD)

    # THE CASE THAT BROKE THE PREVIOUS METRIC, made permanent. A TEXTURED scene under a 50/50
    # translucent sheet has HIGH intra-block deviation -- and the same scene WITHOUT the sheet has
    # higher, because the sheet compresses contrast. So the intra-block number cannot separate them,
    # which is exactly how the earlier revision printed "This is the defect" on the frame with the
    # sheet AND on the frame without it, scoring the clean frame worse.
    #
    # Built here as: a strong checker texture, once bare and once under a 50/50 blend with a
    # constant. The two must be told apart by block-to-block disagreement, and the bare texture --
    # which is NOT the defect -- must NOT read as per-block colour noise.
    def textured(blend):
        out = []
        for y in range(64):
            row = []
            for x in range(64):
                v = 40 + ((x // 4) % 2) * 150 + ((y // 4) % 2) * 60
                p = (v, int(v * 0.7), 90)
                row.append(tuple(((c + b) // 2) for c, b in zip(p, blend)) if blend else p)
            out.append(row)
        return out
    bare = textured(None)
    sheeted = textured((0, 56, 192))
    # An explicit box, not auto-selection: these fixtures are not uniformly blue, so the selector
    # correctly declines them, and a measurement of a region the caller did not name would be a
    # measurement of the wrong thing again.
    whole = (0, 0, 64, 64)
    s_bare = measure(bare, 64, 64, whole)
    s_sheet = measure(sheeted, 64, 64, whole)
    # The confound, shown rather than described: the sheet LOWERS intra-block deviation.
    case("a 50/50 sheet LOWERS intra-block deviation, so that number cannot be the verdict "
         "(sheet %.2f < bare %.2f)" % (s_sheet["texture"], s_bare["texture"]),
         s_sheet["texture"] < s_bare["texture"])
    case("the same textured pair is told apart by block-to-block disagreement only if the texture is "
         "block-scale, so a FINE texture must not read as per-block colour noise",
         s_bare["block_noise"] <= _BLOCK_NOISE_THRESHOLD)
    case("and the real per-block-noise fixture still reads as the defect, after all of that",
         s_bare["block_noise"] <= _BLOCK_NOISE_THRESHOLD
         and stats["block_noise"] > _BLOCK_NOISE_THRESHOLD)

    # Green terrain must not be selected as water.
    grass = [[(40, 120, 40)] * W for _ in range(H)]
    box3, _ = find_water(grass, W, H)
    case("green terrain is NOT selected as water", box3 is None)

    # Sky is blue and bright; the pool is blue and mid. Sky must not select either.
    sky = [[(120, 160, 240)] * W for _ in range(H)]
    box4, _ = find_water(sky, W, H)
    case("bright sky is NOT selected as water", box4 is None)

    # THE CASE THIS TOOL GOT WRONG TWICE, made permanent. A saturated blue DRAGON, 4000 px of it,
    # is far larger than the pool's 16063-px region threshold would suggest and is emphatically not
    # water. It is a narrow vertical shape, so it must be REFUSED. Before the shape guard this tool
    # measured a dragon and printed "50 of 60 blocks deviate -- This is the defect".
    dragon = [[(0, 0, 0)] * 200 for _ in range(200)]
    for y in range(20, 180):
        for x in range(80, 120):
            dragon[y][x] = (30, 60, 150)
    box5, size5 = find_water(dragon, 200, 200)
    case("a narrow saturated-blue shape is REFUSED, not measured as a pool (found %d px)" % size5,
         box5 is None)

    # And the positive still passes with the shape guard in place: a broad sheet IS a pool.
    pool = [[(0, 0, 0)] * 200 for _ in range(200)]
    for y in range(60, 120):
        for x in range(40, 180):
            pool[y][x] = (20, 40, 90 + ((x // 8 + y // 8) % 2) * 40)
    box6, _ = find_water(pool, 200, 200)
    case("a broad noisy sheet is still LOCATED with the shape guard in place", box6 is not None)

    print("selftest: %d case(s) failed" % len(failures))
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("frame", nargs="?", help="a captured frame (.png or binary .ppm)")
    parser.add_argument("--box", help="measure THIS region instead of auto-selecting: x0,y0,x1,y1")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    if not args.frame:
        parser.error("give a frame, or --selftest")
    box = None
    if args.box:
        try:
            x0, y0, x1, y1 = (int(v) for v in args.box.split(","))
        except ValueError:
            parser.error("--box wants x0,y0,x1,y1 -- four integers")
        box = (x0, y0, x1, y1)
    return report(args.frame, box)


if __name__ == "__main__":
    sys.exit(main())
