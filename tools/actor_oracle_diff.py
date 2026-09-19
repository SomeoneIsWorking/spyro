#!/usr/bin/env python3
"""actor_oracle_diff.py — what does the native actor layer draw that retail does not, and vice versa?

The port's actor producers and retail's moby-chain walker 0x80019698 both end in GPU primitives.
`PSXPORT_ACTOR_SCENE_ORACLE=1` makes the port print both streams for the same frame; this reads one
frame out of that log and matches them by geometry, so a difference is named rather than eyeballed.

A primitive is matched on the MULTISET of its (x, y, rgb) vertices, which is invariant to the
vertex order the two sides happen to emit. Two primitives that occupy the same pixels with the same
colours are the same primitive whatever their winding.

WHAT A NEGATIVE PRINTS. Every run prints both denominators, the matched count, the unmatched
count on each side, and each native producer's submitted/matched/recoloured/unmatched counts, so
the producer an unmatched primitive came from is named rather than inferred from the painter
histogram. "0 unmatched" is therefore distinguishable from "the log had no frame in it", which
refuses by name instead.

    python3 tools/actor_oracle_diff.py scratch/logs/actororacle.log
    python3 tools/actor_oracle_diff.py scratch/logs/actororacle.log --frame -1 --show 20
"""
from __future__ import annotations

import argparse
import math
import re
import sys
from collections import Counter
from dataclasses import dataclass, field

NATIVE_REC = re.compile(
    r"native rec=(\d+) painter=0x([0-9A-F]+) nv=(\d+) semi=(\d+) tex=(\d+) ord=(-?[0-9.]+) "
    r"bin=(-?\d+) "
    r"node=0x([0-9A-F]+) "
    r"v0=(-?\d+),(-?\d+),([0-9A-F]{6}) v1=(-?\d+),(-?\d+),([0-9A-F]{6}) "
    r"v2=(-?\d+),(-?\d+),([0-9A-F]{6}) v3=(-?\d+),(-?\d+),([0-9A-F]{6})"
)
RETAIL_REC = re.compile(
    r"retail rec=(\d+) bin=(\d+) code=([0-9A-F]{2}) nv=(\d+) semi=(\d+) "
    r"v0=(-?\d+),(-?\d+),([0-9A-F]{6}) v1=(-?\d+),(-?\d+),([0-9A-F]{6}) "
    r"v2=(-?\d+),(-?\d+),([0-9A-F]{6}) v3=(-?\d+),(-?\d+),([0-9A-F]{6})"
)


@dataclass(frozen=True)
class Primitive:
    nv: int
    semi: int
    vertices: tuple[tuple[int, int, str], ...]
    code: str = "--"
    ot_bin: int = -1
    textured: int = -1
    order: float = 0.0
    node: str = ""
    # The native producer that submitted the item (its painter object). Attributing an unmatched
    # primitive to a producer is what turns "31 native-only primitives" into a work item; retail's
    # stream comes from one guest walker, so its records carry no producer.
    painter: str = ""

    def key(self, dx: int = 0) -> tuple:
        return (self.nv, tuple(sorted((x + dx, y, rgb) for x, y, rgb in self.vertices)))

    def shape_key(self, dx: int = 0) -> tuple:
        """The same primitive without its colours.

        A primitive that matches here but not on `key` occupies exactly retail's pixels with
        different vertex colours — a shading fault, not missing geometry. Separating the two is the
        whole point: "the gem is not drawn" and "the gem is drawn the wrong colour" have different
        causes and would otherwise both read as one unmatched primitive.
        """
        return (self.nv, tuple(sorted((x + dx, y) for x, y, _ in self.vertices)))


@dataclass
class Frame:
    native: list[Primitive] = field(default_factory=list)
    retail: list[Primitive] = field(default_factory=list)
    # node -> "class N scale M". A depth complaint about an anonymous guest address cannot be acted
    # on; the scale byte in particular multiplies the view translation, so it moves depth.
    instances: dict[str, str] = field(default_factory=dict)
    positions: dict[str, tuple[int, int, int]] = field(default_factory=dict)
    camera: tuple[int, int, int] | None = None


def _vertices(groups: list[str], count: int) -> tuple[tuple[int, int, str], ...]:
    out = []
    for i in range(count):
        x, y, rgb = groups[i * 3], groups[i * 3 + 1], groups[i * 3 + 2]
        out.append((int(x), int(y), rgb))
    return tuple(out)


INSTANCE = re.compile(
    r"native instance 0x([0-9A-F]{8}): faces=\d+ class=(\d+) state=0x[0-9A-F]+ scale=(\d+)"
    r" pos=\((-?\d+),(-?\d+),(-?\d+)\)"
)
CAMERA = re.compile(r"native side:.*camera=\((-?\d+),(-?\d+),(-?\d+)\)")


def parse(path: str) -> list[Frame]:
    frames: list[Frame] = []
    current = Frame()
    for line in open(path, encoding="utf-8", errors="replace"):
        if match := NATIVE_REC.search(line):
            g = match.groups()
            nv = int(g[2])
            current.native.append(
                Primitive(
                    nv,
                    int(g[3]),
                    _vertices(list(g[8:]), nv),
                    textured=int(g[4]),
                    order=float(g[5]),
                    ot_bin=int(g[6]),
                    node=g[7],
                    painter=g[1],
                )
            )
        elif match := RETAIL_REC.search(line):
            g = match.groups()
            nv = int(g[3])
            code = int(g[2], 16)
            current.retail.append(
                Primitive(
                    nv,
                    int(g[4]),
                    _vertices(list(g[5:]), nv),
                    g[2],
                    int(g[1]),
                    textured=1 if (code & 4) else 0,
                )
            )
        elif match := INSTANCE.search(line):
            node, held, scale, x, y, z = match.groups()
            current.positions[node] = (int(x), int(y), int(z))
            current.instances[node] = f"class {held} scale {scale}"
        elif match := CAMERA.search(line):
            current.camera = tuple(int(v) for v in match.groups())
        elif "retail side:" in line:
            # The retail summary closes a frame: both streams for it have now been printed.
            frames.append(current)
            current = Frame()
    return frames


def best_offset(frame: Frame, span: int) -> tuple[int, int]:
    """The horizontal shift that aligns the two streams, and how many primitives it matches.

    Widescreen moves the native projection centre, so the port's screen X is not retail's. Measuring
    the shift is the honest way to compare the streams; assuming zero silently reports every
    primitive as missing.
    """
    best, best_matched = 0, -1
    for dx in range(-span, span + 1):
        native_keys = Counter(p.key(dx) for p in frame.native)
        matched = 0
        for primitive in frame.retail:
            if native_keys[primitive.key()] > 0:
                native_keys[primitive.key()] -= 1
                matched += 1
        if matched > best_matched:
            best, best_matched = dx, matched
    return best, best_matched


def report(frame: Frame, show: int, dx: int) -> int:
    native_by_key: dict[tuple, list[Primitive]] = {}
    for p in frame.native:
        native_by_key.setdefault(p.key(dx), []).append(p)
    native_keys = Counter(p.key(dx) for p in frame.native)
    matched = 0
    retail_only: list[Primitive] = []
    pairs: list[tuple[Primitive, Primitive]] = []
    # Which native producer each primitive came from: how many it submitted and how each one ended
    # up. A bare "native only: 31" is not actionable, while "0x80022A2C: 26 submitted, 2 unmatched"
    # names the arm to read.
    by_producer: dict[str, Counter] = {}
    for primitive in frame.native:
        by_producer.setdefault(primitive.painter, Counter())["submitted"] += 1
    for primitive in frame.retail:
        if native_keys[primitive.key()] > 0:
            native_keys[primitive.key()] -= 1
            native = native_by_key[primitive.key()].pop(0)
            pairs.append((primitive, native))
            by_producer.setdefault(native.painter, Counter())["matched"] += 1
            matched += 1
        else:
            retail_only.append(primitive)
    native_only = sum(native_keys.values())

    # Second pass over what is still unmatched, ignoring colour, so a miscoloured primitive is
    # reported as miscoloured rather than counted again as missing geometry.
    leftover_native = [p for p in frame.native if native_keys[p.key(dx)] > 0]
    for p in leftover_native:
        native_keys[p.key(dx)] -= 1
        by_producer.setdefault(p.painter, Counter())["unmatched"] += 1
    shape_keys = Counter(p.shape_key(dx) for p in leftover_native)
    recoloured: list[tuple[Primitive, Primitive]] = []
    still_missing: list[Primitive] = []
    native_by_shape: dict[tuple, list[Primitive]] = {}
    for p in leftover_native:
        native_by_shape.setdefault(p.shape_key(dx), []).append(p)
    for primitive in retail_only:
        shape = primitive.shape_key()
        if shape_keys[shape] > 0:
            shape_keys[shape] -= 1
            native = native_by_shape[shape].pop(0)
            recoloured.append((primitive, native))
            counts = by_producer.setdefault(native.painter, Counter())
            counts["unmatched"] -= 1
            counts["recoloured"] += 1
        else:
            still_missing.append(primitive)
    retail_only = still_missing

    print(f"native x offset   : {dx:+d} (applied to the native stream before matching)")
    print(f"native primitives : {len(frame.native)}")
    print(f"retail primitives : {len(frame.retail)}")
    print(f"matched           : {matched}")
    print(f"retail only       : {len(retail_only)}")
    print(f"native only       : {native_only}")
    report_depth(pairs, frame.instances, frame.positions, frame.camera)
    print(f"  of which same shape, different colour : {len(recoloured)}")
    print(f"  of which absent from the native stream: {len(retail_only)}")
    print("\nretail primitives by primitive code (a producer that emits one code shape cannot be")
    print("absent here without its pass having declined every record):")
    for code, count in Counter(p.code for p in frame.retail).most_common():
        print(f"  code {code}: {count}")
    print("\nnative primitives by producer (submitted / matched / recoloured / unmatched):")
    for painter, counts in sorted(by_producer.items()):
        print(f"  0x{painter or '--------'}: {counts['submitted']} / {counts['matched']} / "
              f"{counts['recoloured']} / {counts['unmatched']}")
    if leftover_native and show:
        print(f"\nfirst {min(show, len(leftover_native))} native-only primitives "
              "(producer, instance, position, vertices):")
        for primitive in leftover_native[:show]:
            verts = " ".join(f"{x},{y},{rgb}" for x, y, rgb in primitive.vertices)
            print(f"  0x{primitive.painter} node={primitive.node}"
                  f" {frame.instances.get(primitive.node, 'unknown')}"
                  f" pos={frame.positions.get(primitive.node, '?')}"
                  f" semi={primitive.semi} tex={primitive.textured} ord={primitive.order:.6f} {verts}")
    if recoloured:
        print(f"\nfirst {min(show, len(recoloured))} recoloured primitives (retail -> native):")
        for retail_p, native_p in recoloured[:show]:
            r = ",".join(rgb for _, _, rgb in retail_p.vertices)
            n = ",".join(rgb for _, _, rgb in native_p.vertices)
            print(f"  code={retail_p.code} bin={retail_p.ot_bin} semi={retail_p.semi}"
                  f" retail={r} native={n}")
    if not retail_only:
        print("\nno retail primitive is missing from the native stream")
        return 0

    print("\nretail-only by primitive code:")
    for code, count in Counter(p.code for p in retail_only).most_common():
        print(f"  code {code}: {count}")
    print("retail-only by texture:")
    for textured, count in Counter(p.textured for p in retail_only).most_common():
        print(f"  textured={textured}: {count}")
    print("native stream by texture:")
    for textured, count in Counter(p.textured for p in frame.native).most_common():
        print(f"  textured={textured}: {count}")
    print("retail-only by semi-transparency:")
    for semi, count in Counter(p.semi for p in retail_only).most_common():
        print(f"  semi={semi}: {count}")
    print("retail-only by OT bin (top 10):")
    for ot_bin, count in Counter(p.ot_bin for p in retail_only).most_common(10):
        print(f"  bin {ot_bin}: {count}")
    print(f"\nfirst {min(show, len(retail_only))} retail-only primitives:")
    for primitive in retail_only[:show]:
        verts = " ".join(f"{x},{y},{rgb}" for x, y, rgb in primitive.vertices)
        print(f"  code={primitive.code} bin={primitive.ot_bin} semi={primitive.semi} {verts}")
    return 0


def report_depth(
    pairs: list[tuple[Primitive, Primitive]],
    instances: dict[str, str],
    positions: dict[str, tuple[int, int, int]],
    camera: tuple[int, int, int] | None,
) -> None:
    """Does the native depth agree with the OT bin retail sorted the same primitive into?

    Matching on pixels says two primitives cover the same area in the same colours; it says nothing
    about which one wins where they overlap. Retail's answer is the OT bin; the port's answer is the
    normalized per-vertex depth. Every ordered pair whose retail bins differ is checked, so a
    depth-only fault cannot hide behind a perfect geometry match.

    The sign relating the two scales is MEASURED rather than assumed: whichever orientation the
    port's depth normalization uses, one of them must hold for nearly every pair, and asserting the
    wrong one would report a faithful frame as 94% broken. The minority count under the measured
    orientation is the real fault count, and it is printed with its denominator either way.
    """
    ranked = [(r.ot_bin, n.order, n.node) for r, n in pairs]
    comparable = same_sign = 0
    for i in range(len(ranked)):
        for j in range(i + 1, len(ranked)):
            (bin_i, ord_i, node_i), (bin_j, ord_j, node_j) = ranked[i], ranked[j]
            # Two faces of ONE instance are excluded: retail orders those by submission inside a
            # bin, not by the bin, so a difference there is not evidence of anything.
            if node_i == node_j or bin_i == bin_j or ord_i == ord_j:
                continue
            comparable += 1
            if (bin_i > bin_j) == (ord_i > ord_j):
                same_sign += 1
    print("\ndepth agreement over matched primitives:")
    print(f"  comparable ordered pairs : {comparable}")
    if comparable == 0:
        print("  (no pair had both a differing OT bin and a differing native depth)")
        return
    rising = same_sign >= comparable - same_sign
    disagreeing = comparable - same_sign if rising else same_sign
    print(f"  measured orientation     : a larger OT bin means a "
          f"{'larger' if rising else 'smaller'} native depth")
    print(f"  disagreeing with retail  : {disagreeing}")
    print(f"  disagreement rate        : {100.0 * disagreeing / comparable:.2f}%")
    _report_authored_bins(pairs)
    worst = sorted(
        ((abs(r.ot_bin - r2.ot_bin), r, n, r2, n2)
         for idx, (r, n) in enumerate(pairs)
         for (r2, n2) in pairs[idx + 1:]
         if n.node != n2.node and r.ot_bin != r2.ot_bin and n.order != n2.order
         and ((r.ot_bin > r2.ot_bin) == (n.order > n2.order)) != rising),
        key=lambda e: -e[0],
    )[:8]
    for span, r, n, r2, n2 in worst:
        def describe(node: str) -> str:
            """The instance's identity plus its true distance from the camera.

            Retail's OT bin and the port's depth are two OPINIONS; the world positions are the
            fact. Printing the distance is what lets a disagreement be attributed rather than
            merely counted -- whichever side disagrees with the geometry is the one at fault.
            """
            text = instances.get(node, "unknown")
            position = positions.get(node)
            if position is None or camera is None:
                return text
            offset = [position[i] - camera[i] for i in range(3)]
            return f"{text} dist {int(math.sqrt(sum(v * v for v in offset)))}"

        first = describe(n.node)
        second = describe(n2.node)
        print(f"  bins {r.ot_bin} vs {r2.ot_bin} (span {span}) but native depth "
              f"{n.order:.6f} vs {n2.order:.6f} "
              f"({n.node} {first} / {n2.node} {second})")



def _report_authored_bins(pairs) -> None:
    """Whether the port's OWN authored bin agrees with retail's, independent of depth.

    The disagreement rate above compares retail's OT bin against the port's submitted per-vertex
    DEPTH, so it cannot say which of two very different bugs it is measuring. Every actor producer
    also passes an authored replay position (`scene_painter_order::...(otBin, ...)`), and that bin is
    directly comparable with retail's. Matching bins mean the recipe computed retail's answer and
    something downstream -- the ordering rule, or the depth buffer -- overrode it. Differing bins
    mean the recipe itself is wrong. Only one of those is worth chasing in the recipe.

    The negative is designed first: a run whose producers author no replay position at all reports
    that in those words and reports nothing else, because "0 disagreements" over 0 comparisons would
    otherwise be indistinguishable from agreement.
    """
    authored = [(r, n) for r, n in pairs if n.ot_bin >= 0]
    print("\n  authored bin vs retail bin (independent of depth):")
    if not authored:
        print(f"    NO authored replay position on any of the {len(pairs)} matched primitive(s) --")
        print("    every one logged bin=-1, so this comparison made ZERO comparisons and says")
        print("    nothing about agreement. Either the producers do not author a position or the")
        print("    oracle is not reading it; check RqItem::painter_replay before reading on.")
        return
    equal = sum(1 for r, n in authored if n.ot_bin == r.ot_bin)
    print(f"    compared {len(authored)} of {len(pairs)} matched primitive(s) that carry one")
    print(f"    identical to retail : {equal}  ({100.0 * equal / len(authored):.2f}%)")
    off = [(n.ot_bin - r.ot_bin, r, n) for r, n in authored if n.ot_bin != r.ot_bin]
    if not off:
        print("    every authored bin matches retail, so the recipe is NOT the fault here;")
        print("    look at the ordering rule and the depth buffer instead.")
        return
    deltas = Counter(d for d, _, _ in off)
    print(f"    differing           : {len(off)}   most common deltas (native - retail):")
    for delta, count in deltas.most_common(6):
        print(f"      {delta:+d}: {count}")
    for delta, r, n in sorted(off, key=lambda e: -abs(e[0]))[:5]:
        print(f"      worst {n.node} painter=0x{n.painter} native bin {n.ot_bin} "
              f"vs retail {r.ot_bin} ({delta:+d})")



def _selftest() -> int:
    """Feed the authored-bin report one case that MUST be positive and one that MUST be negative.

    A comparison that can only ever print "everything agrees" is worth nothing, and the negative
    here is the one that would lie: a capture in which no producer authors a replay position makes
    ZERO comparisons, which must not read as agreement. Both directions are asserted, against the
    shipping code path rather than a copy of it.
    """
    import io
    import contextlib

    def log(bins: tuple[int, int]) -> list[str]:
        def vtx(base: int) -> str:
            return " ".join(f"v{i}={base + i * 10},{base + i * 5},0000{i}0" for i in range(4))
        lines = []
        for rec, (ordv, binv, node, base) in enumerate(
                ((0.20, bins[0], "8016AAAA", 100), (0.10, bins[1], "8016BBBB", 300))):
            lines.append(f"native rec={rec} painter=0x80022A2C nv=4 semi=0 tex=0 "
                         f"ord={ordv:.6f} bin={binv} node=0x{node} {vtx(base)}")
        lines.append("native side: 2 prims camera=(0,0,0)")
        for rec, (binv, base) in enumerate(((40, 100), (10, 300))):
            lines.append(f"retail rec={rec} bin={binv} code=2C nv=4 semi=0 {vtx(base)}")
        lines.append("retail side: 2 prims")
        return lines

    def run(bins: tuple[int, int]) -> str:
        frame = Frame()
        for line in log(bins):
            if match := NATIVE_REC.search(line):
                g = match.groups()
                nv = int(g[2])
                frame.native.append(Primitive(nv, int(g[3]), _vertices(list(g[8:]), nv),
                                              textured=int(g[4]), order=float(g[5]),
                                              ot_bin=int(g[6]), node=g[7], painter=g[1]))
            elif match := RETAIL_REC.search(line):
                g = match.groups()
                nv = int(g[3])
                frame.retail.append(Primitive(nv, int(g[4]), _vertices(list(g[5:]), nv),
                                              g[2], int(g[1])))
        assert len(frame.native) == 2 and len(frame.retail) == 2, (
            f"fixture did not parse: {len(frame.native)} native, {len(frame.retail)} retail")
        pairs = list(zip(frame.retail, frame.native))
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            _report_authored_bins(pairs)
        return buffer.getvalue()

    positive = run((40, 12))
    negative = run((-1, -1))
    failures = []
    if "identical to retail : 1" not in positive:
        failures.append(f"positive case did not report one match:\n{positive}")
    if "native bin 12 vs retail 10 (+2)" not in positive:
        failures.append(f"positive case did not name the differing pair:\n{positive}")
    if "ZERO comparisons" not in negative:
        failures.append(f"negative case did not refuse to claim agreement:\n{negative}")
    if "identical to retail" in negative:
        failures.append(f"negative case reported an agreement rate over no data:\n{negative}")
    for failure in failures:
        print(f"SELFTEST FAILED: {failure}", file=sys.stderr)
    if failures:
        return 1
    print("selftest: the authored-bin report fires on a positive case (1 of 2 bins matching, the "
          "differing pair named) and refuses to claim agreement on a capture with no authored "
          "positions.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", help="a run log produced with PSXPORT_ACTOR_SCENE_ORACLE=1")
    parser.add_argument("--frame", type=int, default=-1, help="frame index, negative from the end")
    parser.add_argument("--show", type=int, default=10)
    parser.add_argument(
        "--dx",
        default="auto",
        help="horizontal shift applied to the native stream, or 'auto' to measure it",
    )
    parser.add_argument("--dx-span", type=int, default=200)
    parser.add_argument("--selftest", action="store_true",
                        help="prove the authored-bin report shows BOTH answers, then exit")
    args = parser.parse_args()

    if args.selftest:
        return _selftest()

    frames = [f for f in parse(args.log) if f.native or f.retail]
    if not frames:
        print(
            f"REFUSED: {args.log} contains no oracle frame. Run with "
            "PSXPORT_ACTOR_SCENE_ORACLE=1 and PSXPORT_DEBUG=actororacle.",
            file=sys.stderr,
        )
        return 1
    # A native record carries the painter that submitted it. A capture from a build that did not
    # log that field parses its retail stream and none of its native one, which would otherwise
    # report every retail primitive as missing; refuse by name instead.
    if not any(p.painter for f in frames for p in f.native):
        print(
            f"REFUSED: {args.log} has no painter= field on its native records; it predates the "
            "producer attribution and must be recaptured with the current build.",
            file=sys.stderr,
        )
        return 1
    print(f"parsed {len(frames)} oracle frame(s); reporting frame {args.frame}\n")
    frame = frames[args.frame]
    if args.dx == "auto":
        dx, matched = best_offset(frame, args.dx_span)
        print(f"measured best horizontal shift {dx:+d} matching {matched} primitive(s)\n")
    else:
        dx = int(args.dx)
    return report(frame, args.show, dx)


if __name__ == "__main__":
    raise SystemExit(main())
