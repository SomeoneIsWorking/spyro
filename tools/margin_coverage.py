#!/usr/bin/env python3
"""margin_coverage.py — how much of a widened capture's margins carry a picture, with denominators.

WHY THIS TOOL. "The 16:9 shot looks wider" is not a measurement, and neither is a whole-frame non-black
percentage: a widened canvas the projection fills with the SAME picture re-centred scores identically
to one that reveals new content. Issue 0149's picture acceptance is about the MARGINS — the columns
that exist only because the aspect setting widened the canvas — so the number is per side, carries
its own denominator, and is comparable to a capture taken before the change.

THE BANDS COME FROM THE NATIVE WIDTH AND THE OWNER'S ANCHOR, NOT FROM THE PICTURE. The obvious
alternative — take the margin as the black columns outside the non-black bounding box — is
DEGENERATE: the bounding box is defined by the drawn pixels, so its margins are black BY
CONSTRUCTION and a widening that revealed a full frame of new content would report 0.0% on both
sides forever. The band has to be a property of the WIDENING POLICY, and the policy is an input.

`--anchor left` (the default) is Spyro's, and it is measured rather than assumed: in the 4:3 capture
`scratch/aspect/base43.ppm` the drawn columns span x=0..511, and in the widened
`scratch/aspect/base169.ppm` they span x=0..683, so the port places the guest's 512-column box at
the LEFT edge and the widening adds columns [512, 684). `--anchor centre` is the other policy any
PSX title in this workspace might use, and it is here so the same tool can measure a centred owner
without the definition silently changing under it.

THE POLICY IS STILL CROSS-CHECKED AGAINST THE PICTURE, never trusted alone. The drawn bounding box
and its `band_start` are printed beside the margin census: a `band_start` of 0 says the picture
reached the new left edge, and a positive one says the same picture was re-centred into a bigger box
(the Tekken-3-card failure, which a non-black margin count alone will not catch on its own). When
the two disagree — margins full of content but a band that did not move — the report says so
instead of choosing a flattering reading.

FOUR FAILURE SHAPES ARE GUARDED, because each reads as a passing measurement:
  * a capture not wider than the native width has no margin — REFUSED, not reported as 0;
  * an all-black widened capture has no drawn box — REFUSED, not reported as a black margin;
  * a missing capture is a refusal, never a silent skip;
  * a margin that is one flat colour across its whole height is a CARD, not a picture, and is
    reported as `uniform=True` so a re-centred or letterboxed band cannot pass as a widening.
A margin the tool calls black is one the presenter's own coverage metric would also call undrawn:
the threshold is `BLACK_LEVEL = 8`, the value `ctr_widescreen_pair.py` uses for the same reason.

Usage:
  margin_coverage.py BASELINE.ppm CANDIDATE.ppm ...
  margin_coverage.py --anchor centre --native-width 512 SHOT.ppm
  margin_coverage.py --selftest
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ppm_look import read_ppm  # noqa: E402  (the decoder the rest of the project's tools already use)

BLACK_LEVEL = 8  # ctr_widescreen_pair.BLACK_LEVEL; the presenter's own coverage threshold.


class Refusal(Exception):
    pass


class Side:
    """One side of the widened canvas, counted against a denominator."""

    def __init__(self, name: str, x0: int, x1: int, height: int, non_black: int, distinct: int,
                 uniform: bool, columns: list[int]):
        self.name = name
        self.x0, self.x1 = x0, x1
        self.columns = x1 - x0
        self.pixels = self.columns * height
        self.non_black = non_black
        self.distinct = distinct
        self.uniform = uniform
        self.per_column = columns

    @property
    def share(self) -> float:
        return 0.0 if self.pixels == 0 else self.non_black / self.pixels

    def line(self) -> str:
        return (
            f"{self.name:<5} x[{self.x0:>4},{self.x1:>4}) {self.columns:>4} col(s) "
            f"{self.non_black:>7}/{self.pixels:<7} non-black ({self.share * 100:6.2f}%), "
            f"{self.distinct:>5} distinct colour(s), uniform={self.uniform}"
        )


class Census:
    def __init__(self, path: str, width: int, height: int, left: Side, right: Side, band_start: int,
                 band_end: int, colours: int, drawn_pixels: int):
        self.path, self.width, self.height = path, width, height
        self.left, self.right = left, right
        self.band_start, self.band_end = band_start, band_end
        self.colours, self.drawn_pixels = colours, drawn_pixels

    @property
    def band(self) -> int:
        return self.band_end - self.band_start

    @property
    def margin_pixels(self) -> int:
        return self.left.non_black + self.right.non_black


def _bands(width: int, native: int, anchor: str) -> tuple[int, int, int, int]:
    """(guest_x0, guest_x1, margin_x0, margin_x1) for the widening policy."""
    extra = width - native
    if anchor == "left":
        return 0, native, native, width
    lead = extra // 2
    return lead, lead + native, 0, width  # centre: margins are [0, lead) and [lead+native, width)


def census(ppm_path: Path, native: int, anchor: str) -> Census:
    width, height, rgb = read_ppm(str(ppm_path))
    if width == 0 or height == 0:
        raise Refusal(f"{ppm_path.name}: {width}x{height} is an empty image; NOT CENSUSED")
    if width <= native:
        raise Refusal(
            f"{ppm_path.name} is {width}px wide, not wider than the native {native}: it has no margin, "
            "so a margin count here would be a green zero"
        )

    per_column = [0] * width
    distinct_all: set[bytes] = set()
    drawn_pixels = 0
    for y in range(height):
        row = y * width * 3
        for x in range(width):
            i = row + x * 3
            px = rgb[i:i + 3]
            distinct_all.add(px)
            if px[0] > BLACK_LEVEL or px[1] > BLACK_LEVEL or px[2] > BLACK_LEVEL:
                per_column[x] += 1
                drawn_pixels += 1
    if drawn_pixels == 0:
        raise Refusal(
            f"{ppm_path.name}: 0 of {width * height} pixels above BLACK_LEVEL={BLACK_LEVEL}; the buffer "
            "is blank, so its margins are undefined — NOT a margin of 0"
        )

    drawn = [x for x in range(width) if per_column[x] > 0]
    band_start, band_end = drawn[0], drawn[-1] + 1

    def measure(name: str, x0: int, x1: int) -> Side:
        if x1 <= x0:
            return Side(name, x0, x0, height, 0, 0, False, [])
        distinct: set[bytes] = set()
        reference: list[bytes] | None = None
        uniform = True
        for x in range(x0, x1):
            column = [rgb[(y * width + x) * 3:(y * width + x) * 3 + 3] for y in range(height)]
            distinct.update(column)
            if reference is None:
                reference = column
            elif column != reference:
                uniform = False
        return Side(name, x0, x1, height, sum(per_column[x0:x1]), len(distinct), uniform,
                    per_column[x0:x1])

    guest_x0, guest_x1, ml_x0, ml_x1 = _bands(width, native, anchor)
    if anchor == "left":
        left, right = measure("left", 0, guest_x0), measure("right", guest_x1, width)
    else:
        lead = guest_x0
        left, right = measure("left", ml_x0, lead), measure("right", guest_x1, width)
    return Census(ppm_path.name, width, height, left, right, band_start, band_end,
                  len(distinct_all), drawn_pixels)


CLASS_ORDER = ("sector", "moby", "particle", "glow", "shadow")


class ClassTally:
    """One class's answer, parsed out of a run's census report."""

    def __init__(self, name: str, drawn: int, outside: int, min_x, max_x):
        self.name, self.drawn, self.outside = name, drawn, outside
        self.min_x, self.max_x = min_x, max_x

    @property
    def ever_drawn(self) -> bool:
        return self.drawn > 0

    def line(self) -> str:
        reach = f"[{self.min_x},{self.max_x}]" if self.ever_drawn else "never drawn"
        return (f"{self.name:<9} drawn={self.drawn:<9} outside={self.outside:<9} reach={reach}")


def read_class_census(path: Path) -> dict[str, ClassTally]:
    """Parse the per-class census report a run writes.

    Refuses rather than answering with a zero for a report that names fewer classes than the
    closed set: an absent class is then distinguishable from a class the widening never reached,
    which is the whole reason the report enumerates all of them.
    """
    text = path.read_text()
    if "margin-census" not in text:
        raise Refusal(f"{path}: not a margin-census report (no 'margin-census' header)")
    tallies: dict[str, ClassTally] = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) < 4 or not parts[0].isalpha():
            continue
        name = parts[0]
        if name not in CLASS_ORDER:
            continue
        drawn = int(parts[1].split("=")[1])
        outside = int(parts[2].split("=")[1])
        reach = parts[3].split("=")[1]
        if reach == "never":
            tallies[name] = ClassTally(name, drawn, outside, None, None)
        else:
            lo, hi = reach.strip("[]").split(",")
            tallies[name] = ClassTally(name, drawn, outside, int(lo), int(hi))
    missing = [c for c in CLASS_ORDER if c not in tallies]
    if missing:
        raise Refusal(f"{path}: report names {len(tallies)}/{len(CLASS_ORDER)} classes, "
                      f"missing {','.join(missing)} — a partial report cannot answer the question")
    return tallies


def compare_class_census(control: dict[str, ClassTally],
                         widened: dict[str, ClassTally]) -> list[str]:
    """The 4:3 run is the CONTROL. A class that widens must move; a class that does not is named."""
    lines = [f"  {'class':<9} {'control(4:3) drawn/outside':<26} {'widened(16:9) drawn/outside':<28} verdict"]
    for name in CLASS_ORDER:
        c, w = control[name], widened[name]
        if not w.ever_drawn:
            verdict = "NEVER DRAWN — this class never reached the screen in either run"
        elif w.outside == 0 and c.outside == 0:
            verdict = "no object left the guest window at either aspect"
        elif w.outside > c.outside:
            verdict = f"WIDENED (+{w.outside - c.outside} objects past 512)"
        elif w.outside == c.outside:
            verdict = "unchanged — the widening did not reach this class"
        else:
            verdict = f"NARROWED (-{c.outside - w.outside}) — investigate"
        lines.append(f"  {name:<9} {c.drawn:>9}/{c.outside:<16} {w.drawn:>9}/{w.outside:<16} {verdict}")
    return lines


def _write_ppm(path: Path, width: int, height: int, paint) -> None:
    buf = bytearray(width * height * 3)
    paint(buf, width, height)
    path.write_bytes(f"P6\n{width} {height}\n255\n".encode() + bytes(buf))


def _selftest() -> int:
    """Both answers. A margin counter never shown RED is not an instrument."""
    import tempfile

    failures: list[str] = []

    def check(cond: bool, message: str) -> None:
        if not cond:
            failures.append(message)

    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)

        # 1. POSITIVE — the shipping shape: a left-anchored 512-column picture in a 684-wide canvas
        #    whose right 172 columns carry varied content. This must come out NON-ZERO, because the
        #    tool that only ever says "0" cannot measure a widening.
        def grown(b, w, h):
            for y in range(h):
                for x in range(w):
                    i = (y * w + x) * 3
                    v = 30 + ((x * 7 + y * 13) % 200) if x < 512 else 40 + ((x * 5 + y * 3) % 200)
                    b[i], b[i + 1], b[i + 2] = v, v, v

        _write_ppm(tmp / "grown.ppm", 684, 240, grown)
        c = census(tmp / "grown.ppm", 512, "left")
        check(c.left.columns == 0, f"a left-anchored widening reported a {c.left.columns}px left margin")
        check(c.right.columns == 172, f"right margin is {c.right.columns} columns, expected 172")
        check(c.right.non_black == 172 * 240,
              f"right margin counted {c.right.non_black} non-black of {172 * 240}")
        check(c.band_start == 0, f"a picture reaching the left edge reported band_start={c.band_start}")
        check(not c.right.uniform, "a varying right margin was reported uniform (a card reads like this)")

        # 2. NEGATIVE — a widened canvas with a BLACK margin: the canvas grew and the projection
        #    revealed nothing. A margin count that cannot report this as 0 has lost the ability to
        #    detect a failed widening, which is the case this whole tool exists to catch.
        def blank_margin(b, w, h):
            for y in range(h):
                for x in range(512):
                    i = (y * w + x) * 3
                    b[i] = b[i + 1] = b[i + 2] = 30 + ((x * 7 + y * 13) % 200)

        _write_ppm(tmp / "blank.ppm", 684, 240, blank_margin)
        c = census(tmp / "blank.ppm", 512, "left")
        check(c.right.columns == 172 and c.right.non_black == 0,
              f"a black 172-column margin counted {c.right.non_black} non-black")
        check(c.band == 512, f"a blank-margin capture reported a {c.band}px band, expected 512")
        check(c.band_start == 0, f"band_start reported as {c.band_start}, expected 0")

        # 2b. The CENTRED policy's own failure: the same 4:3 picture re-centred in a wide box, read
        #     with `--anchor centre`. Both margins are pure black and the band is the narrow width.
        def pillar(b, w, h):
            for y in range(h):
                for x in range(256, 768):
                    i = (y * w + x) * 3
                    b[i] = b[i + 1] = b[i + 2] = 30 + ((x * 7 + y * 13) % 200)

        _write_ppm(tmp / "pillar.ppm", 1024, 240, pillar)
        c2 = census(tmp / "pillar.ppm", 512, "centre")
        check(c2.left.columns == 256 and c2.right.columns == 256,
              f"the centre anchor reported margins L{c2.left.columns}/R{c2.right.columns}")
        check(c2.left.non_black == 0 and c2.right.non_black == 0,
              "the centre anchor counted black margins as content")
        check(c2.band == 512 and c2.band_start == 256,
              f"a re-centred picture reported band {c2.band} at {c2.band_start}, expected 512 at 256")

        # 3. THE CARD — a margin that is one flat colour across its full height. It is NON-BLACK, so
        #    a naive count would score it as a widening; `uniform` is what separates it.
        def card(b, w, h):
            for y in range(h):
                for x in range(w):
                    i = (y * w + x) * 3
                    v = 30 + ((x * 7 + y * 13) % 200) if x < 512 else 128
                    b[i], b[i + 1], b[i + 2] = v, v, v

        _write_ppm(tmp / "card.ppm", 684, 240, card)
        c = census(tmp / "card.ppm", 512, "left")
        check(c.right.non_black == 172 * 240 and c.right.uniform,
              f"a flat card margin read as {c.right.non_black} non-black, uniform={c.right.uniform}")

        # 4. GREEN-ZERO GUARDS: a capture with no margin, and a blank widened capture, are refusals.
        _write_ppm(tmp / "narrow.ppm", 512, 240, lambda b, w, h: b.__setitem__(
            slice(0, len(b)), bytes([30, 60, 90]) * (w * h)))
        for name, why in (("narrow.ppm", "a 4:3 capture has no margin"),
                          ("black.ppm", "an all-black capture has no drawn box")):
            if name == "black.ppm":
                _write_ppm(tmp / name, 684, 240, lambda b, w, h: None)
            try:
                census(tmp / name, 512, "left")
                failures.append(f"{name} was censused instead of refused ({why})")
            except Refusal:
                pass
        try:
            census(tmp / "absent.ppm", 512, "left")
            failures.append("a missing capture was censused instead of refused")
        except (Refusal, OSError):
            pass

        # 5. THE PER-CLASS CENSUS, both answers. The pixel census above cannot see a class whose
        #    objects were redrawn without a single pixel changing colour, so the per-class path
        #    needs its own RED case as well as its GREEN one.
        def report(rows) -> str:
            out = "[margin-census] objects drawn past the guest's 512-column window\n"
            for row in rows:
                out += f"  {row}\n"
            return out

        all_classes = [f"{n:<9} drawn=0       outside=0       reach=never drawn" for n in CLASS_ORDER]
        (tmp / "census_green.txt").write_text(report([
            f"{n:<9} drawn={d:<8} outside={o:<8} reach=[{lo},{hi}]"
            for n, d, o, lo, hi in (
                ("sector", 40000, 120, -12, 683),
                ("moby", 9000, 310, -40, 700),
                ("particle", 300, 18, 505, 690),
                ("glow", 40, 6, 520, 672),
                ("shadow", 700, 25, 498, 681),
            )
        ]))
        (tmp / "census_control.txt").write_text(report([
            f"{n:<9} drawn={d:<8} outside={o:<8} reach=[{lo},{hi}]"
            for n, d, o, lo, hi in (
                ("sector", 40000, 0, 3, 511),
                ("moby", 9000, 0, 2, 511),
                ("particle", 300, 0, 300, 511),
                ("glow", 40, 0, 300, 511),
                ("shadow", 700, 0, 300, 511),
            )
        ]))
        widened = read_class_census(tmp / "census_green.txt")
        control = read_class_census(tmp / "census_control.txt")
        check(set(widened) == set(CLASS_ORDER),
              f"the census read {len(widened)} classes, expected {len(CLASS_ORDER)}")
        check(widened["moby"].outside == 310, f"moby outside read {widened['moby'].outside}")
        check(widened["sector"].min_x == -12, f"sector min_x read {widened['sector'].min_x}")
        check(widened["sector"].max_x == 683, f"sector max_x read {widened['sector'].max_x}")
        verdicts = "\n".join(compare_class_census(control, widened))
        check(verdicts.count("WIDENED") == 5,
              f"the matched pair reported {verdicts.count('WIDENED')} widened classes, expected 5")

        # 5b. RED — a 16:9 run whose classes all report outside=0. Every class ran, so this is not
        #     a missing-report case: the widening simply never reached a producer. The tool must
        #     say so rather than reporting a healthy picture.
        (tmp / "census_zero.txt").write_text(report([
            f"{n:<9} drawn={d:<8} outside=0       reach=[{lo},{hi}]"
            for n, d, lo, hi in (("sector", 40000, 0, 511), ("moby", 9000, 1, 511),
                                 ("particle", 300, 200, 511), ("glow", 40, 300, 511),
                                 ("shadow", 700, 300, 511))
        ]))
        zero = read_class_census(tmp / "census_zero.txt")
        zero_verdicts = "\n".join(compare_class_census(control, zero))
        check("WIDENED" not in zero_verdicts,
              "a run with no object past 512 was scored as reaching its producers")
        check(zero_verdicts.count("no object left the guest window") == 5,
              f"a zero-outside run reported "
              f"{zero_verdicts.count('no object left the guest window')} inert classes, expected 5")

        # 5c. RED — a report naming only some of the closed class set is a REFUSAL, not a partial
        #     answer. Reading it as "the missing classes drew nothing" would invent a measurement.
        (tmp / "census_partial.txt").write_text(report([
            f"{n:<9} drawn=10        outside=2        reach=[500,600]" for n in ("sector", "moby")
        ]))
        try:
            read_class_census(tmp / "census_partial.txt")
            failures.append("a report naming 2 of 5 classes was read instead of refused")
        except Refusal:
            pass
        (tmp / "census_empty.txt").write_text(report(all_classes))
        empty = read_class_census(tmp / "census_empty.txt")
        check(all(not empty[n].ever_drawn for n in CLASS_ORDER),
              "a never-drawn report claimed a class had drawn")
        try:
            read_class_census(tmp / "narrow.ppm")
            failures.append("a PPM was read as a census report instead of refused")
        except (Refusal, OSError, UnicodeDecodeError):
            pass

    for f in failures:
        print(f"FAIL margin_coverage selftest: {f}", file=sys.stderr)
    print(f"margin_coverage selftest: {'PASS' if not failures else 'FAIL'} "
          f"(13 cases, {len(failures)} failed)")
    return 1 if failures else 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("captures", nargs="*", type=Path)
    ap.add_argument("--native-width", type=int, default=512,
                    help="the guest's own 4:3 draw width (the PSX framebuffer's 512 columns)")
    ap.add_argument("--anchor", choices=("left", "centre"), default="left",
                    help="where the widening places the guest's box; Spyro's owner is `left`")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--class-census", nargs=2, type=Path, metavar=("CONTROL", "WIDENED"),
                    help="the 4:3 control report and the 16:9 report from a matched pair of runs")
    args = ap.parse_args(argv)

    if args.selftest:
        return _selftest()

    if args.class_census:
        control_path, widened_path = args.class_census
        try:
            control = read_class_census(control_path)
            widened = read_class_census(widened_path)
        except (Refusal, OSError) as exc:
            print(f"  REFUSED: {exc}", file=sys.stderr)
            return 2
        print(f"margin_coverage: per-class drawn reach, control={control_path.name} "
              f"widened={widened_path.name}, {len(CLASS_ORDER)} classes")
        for line in compare_class_census(control, widened):
            print(line)
        reached = [n for n in CLASS_ORDER if widened[n].ever_drawn and widened[n].outside > 0]
        never = [n for n in CLASS_ORDER if not widened[n].ever_drawn]
        print(f"  classes with objects drawn past 512 at 16:9: {len(reached)}/{len(CLASS_ORDER)}"
              f"{' [' + ','.join(reached) + ']' if reached else ''}")
        if never:
            print(f"  classes that never drew at either aspect: {','.join(never)}")
        if not reached:
            print("  VERDICT: REFUSED as a proof of widening — no class drew an object past 512",
                  file=sys.stderr)
            return 1
        return 0

    if not args.captures:
        print("margin_coverage.py: no capture given; scanned 0 captures", file=sys.stderr)
        return 2

    print(f"margin_coverage: native={args.native_width} anchor={args.anchor} "
          f"BLACK_LEVEL={BLACK_LEVEL}, scanned {len(args.captures)} capture(s)")
    censuses: list[Census] = []
    for shot in args.captures:
        try:
            c = census(shot, args.native_width, args.anchor)
        except (Refusal, OSError) as exc:
            print(f"  REFUSED: {exc}", file=sys.stderr)
            return 2
        censuses.append(c)
        print(f"  {c.path} {c.width}x{c.height}: drawn band x[{c.band_start},{c.band_end}) "
              f"= {c.band}px, {c.drawn_pixels}/{c.width * c.height} px drawn, "
              f"{c.colours} distinct colour(s)")
        print(f"    {c.left.line()}")
        print(f"    {c.right.line()}")
        if c.left.columns and c.right.columns and not (c.left.non_black or c.right.non_black):
            print("    VERDICT: widened canvas, both margins black — the 4:3 picture in a bigger box")

    if len(censuses) >= 2:
        base = censuses[0]
        print(f"  versus {base.path}:")
        for c in censuses[1:]:
            print(f"    {c.path}: band {base.band} -> {c.band} ({c.band - base.band:+d}px), "
                  f"left margin {base.left.non_black}/{base.left.pixels} -> "
                  f"{c.left.non_black}/{c.left.pixels} non-black, "
                  f"right margin {base.right.non_black}/{base.right.pixels} -> "
                  f"{c.right.non_black}/{c.right.pixels} non-black, "
                  f"margins carrying a picture {base.margin_pixels} -> {c.margin_pixels} px "
                  f"({c.margin_pixels - base.margin_pixels:+d})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
