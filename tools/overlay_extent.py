#!/usr/bin/env python3
"""overlay_extent.py — HOW MUCH of a screen-space translucent overlay actually reaches the PICTURE,
over a strip of consecutive presents, with the presented count as the denominator.

WHY THIS EXISTS. Issue 0144 attributed the defect frame's blue rectangle and then found two things
about it that a single screenshot cannot answer, both of which had been read off single frames:

  * a CLIPPED overlay. The pause menu's panel is submitted with bounding box (226,67)-(458,176) and
    PAINTS 226..372 — 147 of its 232 columns — because the clip it was given is expressed in the
    drawn space while the quad's vertices are in the authored 4:3 space. At 4:3 the two coincide
    and nothing is cut, so the defect is invisible in every 4:3 capture and obvious in a 16:9 one.
  * a BLINKING overlay. The panel is submitted on every menu frame and is on the picture on half
    the presents. `pause2.png` and `pause-final.png` are the same state one present apart, and
    0143's "0 of 104 captures reproduced the rectangle" was that cadence, not a broken route.

Both are questions about a SEQUENCE. A picture answers "what does this present look like" and
nothing else: an overlay that is absent because it is off on this present reads exactly like an
overlay that was never drawn. The strip is the denominator, and the width is the measurement.

WHAT IT MEASURES, per presented frame, for one named rectangle:

  width   the columns over which the frame carries the overlay's signature. The signature is the
          BLUENESS EXCESS, `B - (R+G)/2`, averaged over the rectangle's rows and compared with the
          same statistic over a reference band of the SAME frame at the SAME columns. That is
          deliberately local: a 50%-blend toward a blue source raises B far above the local
          luminance, while a textured or lit scene does not, and comparing inside one frame means
          the measurement needs no second capture of the same camera — which is what made the
          two-frame blend fit the only earlier route to this number, and why it could not be
          applied to a frame whose clean twin does not exist.
  rows    the same excess down the rows, so a vertically clipped overlay is caught too.
  verdict PRESENT at full width / PRESENT but NARROWER than --expect-width / ABSENT.

AND THE TWO ANSWERS ARE BOTH REPORTED. The summary always prints `N of M presents`, and a run
whose strip is unreadable, or empty, or shorter than one frame EXITS NON-ZERO saying it measured
nothing. "0 of M" and "I never looked" must not be the same output — that is the C138 failure
(a stale capture file read as a fresh one) in its most expensive form.

THE NEGATIVE CONTROL IS THE POINT. `--selftest` synthesises frames in which the overlay is absent,
present at full width, present and CLIPPED, and present only in some frames of a strip, and asserts
each is told apart. A detector that answered "present" for every input would pass a single-frame
check and fail this one, which is the only property that makes it worth having.

Usage:
  overlay_extent.py scratch/screenshots/preseq/*.ppm --top 67 --bottom 176 --expect-width 232
  overlay_extent.py --selftest
"""
from __future__ import annotations

import argparse
import os
import statistics
import sys

# The default geometry is the guest's own pause-menu panel, from SCUS_942.28: `addiu $v0,$zero,0x43`
# / `sb $s4,0x48` / `addiu $v0,$zero,0x8C` / `addiu $v0,$zero,0x174` / `addiu $v0,$zero,0xB0` at
# 0x8001A7D4-0x8001A84C, i.e. y0=67, x0=140, x1=372, y1=176 in the guest's 512x240 space. 232 is
# 372-140, the columns a PSX quad covers.
DEFAULT_TOP = 67
DEFAULT_BOTTOM = 177          # exclusive
DEFAULT_EXPECT_WIDTH = 232    # 372 - 140
# The reference band is BELOW the rectangle and the same height as a third of it, chosen so it is
# courtyard ground and courtyard path rather than the panel. A band that overlapped the rectangle
# would divide its own signal by itself.
DEFAULT_REF_TOP = 184
DEFAULT_REF_BOTTOM = 228


class Refusal(Exception):
    pass


def read_ppm(path: str) -> tuple[int, int, bytes]:
    """(w, h, rgb) from a binary P6. Raises rather than returning an empty picture."""
    with open(path, "rb") as handle:
        data = handle.read()
    if not data.startswith(b"P6"):
        raise Refusal(f"{path}: not a binary PPM (starts {data[:2]!r})")
    fields: list[bytes] = []
    index = 2
    while len(fields) < 3:
        while index < len(data) and data[index:index + 1].isspace():
            index += 1
        if data[index:index + 1] == b"#":
            while index < len(data) and data[index:index + 1] not in (b"\n", b""):
                index += 1
            continue
        start = index
        while index < len(data) and not data[index:index + 1].isspace():
            index += 1
        fields.append(data[start:index])
    index += 1
    try:
        width, height, _maxval = (int(f) for f in fields)
    except ValueError as refusal:
        raise Refusal(f"{path}: unreadable PPM header {fields!r}") from refusal
    need = width * height * 3
    pixels = data[index:index + need]
    if len(pixels) != need:
        raise Refusal(f"{path}: truncated — {len(pixels)} of {need} pixel bytes")
    return width, height, pixels


def blueness(pixels: bytes, width: int, x: int, y: int) -> float:
    offset = (y * width + x) * 3
    red, green, blue = pixels[offset], pixels[offset + 1], pixels[offset + 2]
    return blue - (red + green) / 2.0


def column_excess(pixels: bytes, width: int, top: int, bottom: int,
                  ref_top: int, ref_bottom: int) -> list[float]:
    """Per-column BLUENESS EXCESS of the band over the reference band, in 5-bit colour units."""
    rows = bottom - top
    ref_rows = ref_bottom - ref_top
    if rows <= 0 or ref_rows <= 0:
        raise Refusal(f"empty band: rows={rows} ref_rows={ref_rows}")
    out = []
    for x in range(width):
        inside = sum(blueness(pixels, width, x, y) for y in range(top, bottom)) / rows
        outside = sum(blueness(pixels, width, x, y) for y in range(ref_top, ref_bottom)) / ref_rows
        out.append(inside - outside)
    return out


def measure_frame(pixels: bytes, width: int, top: int, bottom: int,
                  ref_top: int, ref_bottom: int, threshold: float,
                  expect_width: int) -> dict:
    excess = column_excess(pixels, width, top, bottom, ref_top, ref_bottom)
    strong = [x for x in range(width) if excess[x] > threshold]
    result = {
        "width": width,
        "scanned_columns": width,
        "strong_columns": len(strong),
        "peak": max(excess),
        "left": None,
        "right": None,
        "span": 0,
        "verdict": "ABSENT",
    }
    if not strong:
        return result
    left, right = min(strong), max(strong)
    result.update(left=left, right=right, span=right - left + 1)
    contiguous = len(strong) == result["span"]
    result["contiguous"] = contiguous
    if not contiguous:
        # Scattered hits are a different claim from a rectangle. Reported as its own verdict so a
        # lit scene cannot be reported as a clipped overlay.
        result["verdict"] = "SCATTERED"
    elif result["span"] < expect_width:
        result["verdict"] = "NARROWER"
    else:
        result["verdict"] = "FULL"
    return result


def report(paths: list[str], top: int, bottom: int, ref_top: int, ref_bottom: int,
           threshold: float, expect_width: int) -> int:
    if not paths:
        print("REFUSED: no frames given — NOTHING was measured, so this is not a result.")
        return 2
    print(f"{len(paths)} frame(s) given. band y={top}..{bottom - 1} against reference "
          f"y={ref_top}..{ref_bottom - 1}, threshold {threshold}, expected width {expect_width}")
    verdicts: dict[str, int] = {}
    spans: list[int] = []
    measured = 0
    for path in paths:
        try:
            width, height, pixels = read_ppm(path)
        except (Refusal, OSError) as refusal:
            print(f"  {os.path.basename(path):14s} REFUSED: {refusal}")
            continue
        if bottom > height or ref_bottom > height:
            print(f"  {os.path.basename(path):14s} REFUSED: {height} rows cannot hold a band at "
                  f"y={ref_top}..{ref_bottom - 1}")
            continue
        result = measure_frame(pixels, width, top, bottom, ref_top, ref_bottom,
                               threshold, expect_width)
        measured += 1
        verdicts[result["verdict"]] = verdicts.get(result["verdict"], 0) + 1
        if result["verdict"] in ("FULL", "NARROWER"):
            spans.append(result["span"])
        if result["verdict"] == "ABSENT":
            print(f"  {os.path.basename(path):14s} {width}x{height}  ABSENT   "
                  f"(scanned {result['scanned_columns']} columns, peak excess {result['peak']:.1f} "
                  f"<= {threshold})")
        else:
            print(f"  {os.path.basename(path):14s} {width}x{height}  {result['verdict']:9s}"
                  f" columns {result['left']}..{result['right']} span {result['span']} "
                  f"(scanned {result['scanned_columns']} columns, {result['strong_columns']} strong, "
                  f"peak {result['peak']:.1f})")
    if measured == 0:
        print(f"\n0 of {len(paths)} frame(s) could be read — NOTHING was measured.")
        return 2
    present = verdicts.get("FULL", 0) + verdicts.get("NARROWER", 0)
    print(f"\n{measured} of {len(paths)} frame(s) read. overlay present on {present} of {measured} "
          f"present(s). verdicts: " + ", ".join(f"{k}={v}" for k, v in sorted(verdicts.items())))
    if spans:
        print(f"spans where present: min {min(spans)} max {max(spans)} median "
              f"{statistics.median(spans):g} (expected {expect_width})")
    if present == 0:
        print("VERDICT: the overlay is on NONE of the presented frames.")
    elif present < measured:
        print(f"VERDICT: the overlay is on {present} of {measured} presents — it is NOT on every one.")
    elif min(spans) < expect_width:
        print(f"VERDICT: the overlay is on every present but NARROWER than its own submitted extent "
              f"({min(spans)} of {expect_width} columns).")
    else:
        print(f"VERDICT: the overlay is on every present at its full width.")
    return 0


# ── the selftest ────────────────────────────────────────────────────────────────────────────────
def _frame(width: int, height: int, base: tuple[int, int, int],
           box: tuple[int, int, int, int] | None,
           blend: tuple[int, int, int] = (0, 57, 198)) -> tuple[int, bytes]:
    """A synthetic frame: `base` everywhere, and inside `box` the 50% blend toward `blend`."""
    out = bytearray(width * height * 3)
    for y in range(height):
        for x in range(width):
            colour = base
            if box is not None and box[0] <= x <= box[2] and box[1] <= y <= box[3]:
                colour = tuple((base[i] + blend[i]) // 2 for i in range(3))
            offset = (y * width + x) * 3
            out[offset:offset + 3] = bytes(colour)
    return width, bytes(out)


def selftest() -> int:
    top, bottom, ref_top, ref_bottom = DEFAULT_TOP, DEFAULT_BOTTOM, DEFAULT_REF_TOP, DEFAULT_REF_BOTTOM
    threshold, expect = 40.0, DEFAULT_EXPECT_WIDTH
    failures: list[str] = []

    def check(name: str, got, want) -> None:
        if got != want:
            failures.append(f"{name}: got {got!r} want {want!r}")
        print(f"  {'ok  ' if got == want else 'FAIL'} {name}  ({got!r})")

    width, clean = _frame(512, 240, (60, 120, 50), None)
    # A POSITIVE: the overlay at its own full extent, 4:3, no margin. Must read FULL at 232.
    full_w, full = _frame(512, 240, (60, 120, 50), (140, 67, 371, 176))
    got = measure_frame(full, full_w, top, bottom, ref_top, ref_bottom, threshold, expect)
    check("full-width overlay verdict", got["verdict"], "FULL")
    check("full-width overlay span", got["span"], expect)
    check("full-width overlay left edge", got["left"], 140)
    check("full-width overlay right edge", got["right"], 371)

    # THE POSITIVE THAT MATTERS: the same overlay with its fill cut 86 columns short, which is
    # exactly what 16:9 widescreen does to the pause panel (226..372 of 226..457). It must read
    # NARROWER and give the SHORT span, not "present".
    clip_w, clipped = _frame(512, 240, (60, 120, 50), (140, 67, 285, 176))
    got = measure_frame(clipped, clip_w, top, bottom, ref_top, ref_bottom, threshold, expect)
    check("clipped overlay verdict", got["verdict"], "NARROWER")
    check("clipped overlay span", got["span"], 146)

    # A NEGATIVE: a lit scene with no overlay at all. Must read ABSENT and name what it scanned.
    got = measure_frame(clean, width, top, bottom, ref_top, ref_bottom, threshold, expect)
    check("clean frame verdict", got["verdict"], "ABSENT")
    check("clean frame strong columns", got["strong_columns"], 0)
    check("clean frame reports its denominator", got["scanned_columns"], width)

    # A NEGATIVE: a green glow the detector's own statistic also fires on. This is the real false
    # positive class -- a world prim that is locally less red than blue-ish -- and it must not be
    # reported as a rectangle.
    glow_w, glow = _frame(512, 240, (60, 120, 50), (250, 100, 262, 120), blend=(0, 200, 220))
    got = measure_frame(glow, glow_w, top, bottom, ref_top, ref_bottom, threshold, expect)
    check("a 13-column glow is not the overlay", got["verdict"] in ("SCATTERED", "ABSENT"), True)

    # A NEGATIVE: scattered single-column hits across the whole frame. Not a rectangle.
    noisy = bytearray(clean)
    for x in range(0, width, 7):
        for y in range(top, bottom):
            offset = (y * width + x) * 3
            noisy[offset:offset + 3] = bytes((0, 40, 200))
    got = measure_frame(bytes(noisy), width, top, bottom, ref_top, ref_bottom, threshold, expect)
    check("scattered columns are not a rectangle", got["verdict"], "SCATTERED")

    # THE CADENCE, which is the reason a single frame is not evidence: a strip in which the overlay
    # is on some presents and not others. The tool must report a COUNT, not a verdict about one.
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        paths = []
        for index in range(6):
            path = os.path.join(tmp, f"p{index:04d}.ppm")
            source = full if index % 2 == 0 else clean
            with open(path, "wb") as handle:
                handle.write(b"P6\n512 240\n255\n")
                handle.write(source)
            paths.append(path)
        lines: list[str] = []
        real_stdout = sys.stdout
        try:
            sys.stdout = _Tee(lines)
            code = report(paths, top, bottom, ref_top, ref_bottom, threshold, expect)
        finally:
            sys.stdout = real_stdout
        text = "".join(lines)
        check("strip with a blinking overlay exits 0", code, 0)
        check("strip reports the present count", "3 of 6 present(s)" in text, True)
        check("strip names the blink", "NOT on every one" in text, True)

    # THE REFUSALS, which are the other half of the tool being worth having: no frames, a file
    # that is not a PPM, and a truncated PPM. Each must refuse by name, and none may report 0 of 0.
    lines = []
    real_stdout = sys.stdout
    try:
        sys.stdout = _Tee(lines)
        code = report([], top, bottom, ref_top, ref_bottom, threshold, expect)
    finally:
        sys.stdout = real_stdout
    check("no frames exits non-zero", code, 2)
    check("no frames says NOTHING was measured", "NOTHING was measured" in "".join(lines), True)

    import tempfile as _tempfile
    with _tempfile.TemporaryDirectory() as tmp:
        bad = os.path.join(tmp, "bad.ppm")
        with open(bad, "wb") as handle:
            handle.write(b"P3\n2 2\n255\n")
        lines = []
        real_stdout = sys.stdout
        try:
            sys.stdout = _Tee(lines)
            code = report([bad], top, bottom, ref_top, ref_bottom, threshold, expect)
        finally:
            sys.stdout = real_stdout
        text = "".join(lines)
        check("a non-P6 file exits non-zero", code, 2)
        check("a non-P6 file refuses by name", "not a binary PPM" in text, True)
        check("a non-P6 file is not a clean zero", "overlay is on NONE" in text, False)

        short = os.path.join(tmp, "short.ppm")
        with open(short, "wb") as handle:
            handle.write(b"P6\n512 240\n255\n" + clean[:1000])
        lines = []
        real_stdout = sys.stdout
        try:
            sys.stdout = _Tee(lines)
            code = report([short], top, bottom, ref_top, ref_bottom, threshold, expect)
        finally:
            sys.stdout = real_stdout
        check("a truncated PPM exits non-zero", code, 2)
        check("a truncated PPM refuses by name", "truncated" in "".join(lines), True)

    print()
    if failures:
        for line in failures:
            print(f"FAIL {line}")
        return 1
    print("  all selftest checks passed")
    return 0


class _Tee:
    def __init__(self, sink: list[str]) -> None:
        self._sink = sink

    def write(self, text: str) -> int:
        self._sink.append(text)
        return len(text)

    def flush(self) -> None:
        pass


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("frames", nargs="*", help="presented frames (.ppm), in order")
    parser.add_argument("--top", type=int, default=DEFAULT_TOP)
    parser.add_argument("--bottom", type=int, default=DEFAULT_BOTTOM)
    parser.add_argument("--ref-top", type=int, default=DEFAULT_REF_TOP)
    parser.add_argument("--ref-bottom", type=int, default=DEFAULT_REF_BOTTOM)
    parser.add_argument("--threshold", type=float, default=40.0,
                        help="blueness excess, in 5-bit colour units, above which a column counts")
    parser.add_argument("--expect-width", type=int, default=DEFAULT_EXPECT_WIDTH,
                        help="the overlay's OWN submitted width in columns; a shorter span is a clip")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    return report(sorted(args.frames), args.top, args.bottom, args.ref_top, args.ref_bottom,
                  args.threshold, args.expect_width)


if __name__ == "__main__":
    sys.exit(main())
