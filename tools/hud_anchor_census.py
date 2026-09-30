#!/usr/bin/env python3
"""hud_anchor_census.py — every HUD/UI element's horizontal position at 4:3 and at 16:9.

WHY A PER-ELEMENT CENSUS AND NOT A SCREENSHOT. "The 16:9 HUD looks anchored" is not a measurement,
and neither is "the two pictures differ". A capture cannot say WHICH element moved, by how much, or
whether it moved because the policy wanted it to. Worse, a 3D scene fills the frame, so a HUD that
drifted 86 px into the picture is still a plausible-looking picture. This tool reads the port's own
`uihud` channel, which prints one line per HUD element per present with its AUTHORED x/width, its
DRAWN x/width, the class it was placed as, and the frame it was placed into — and it compares two
runs of the SAME route that differ in exactly one settings field (`aspect`).

THE THREE FAILURES, each of which reads as a passing picture:

  * a STRETCHED element — its drawn width differs from its authored width at either aspect. The
    policy never resizes an element, so a width difference is a producer that bypassed it;
  * a MOVED CENTRED element — a `centred` element whose distance to the frame's centre changed.
    Centring is what the framework already did, so this can only break if something re-centred twice;
  * an EDGE element that is not at the widened edge — a `left-edge` element that moved, or a
    `right-edge` element whose distance to the RIGHT edge changed.

and the invariant that guards all three: at 4:3 every element's drawn x EQUALS its authored x, on
every class, with no exceptions to allow. That is the same identity the 4:3 picture depends on, and
it is checked from the product's own output rather than asserted.

`--selftest` builds two synthetic logs over the same six elements — one correctly anchored, one with
a stretched centred element and a right-edge element placed by the centring rule — and shows both
answers. A census that has only ever printed PASS is a census that cannot fail.

Usage:
  hud_anchor_census.py                       drive both aspects and report
  hud_anchor_census.py --selftest            the two synthetic answers
  hud_anchor_census.py --narrow-log A --wide-log B    report over two captured logs
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# The pair. Both legs have fps60=0 and differ in `aspect` and nothing else, so a difference in a
# HUD element's position cannot be attributed to the interpolation path.
NARROW_SETTINGS = ROOT / "tools" / "fps60_control_settings.ini"
WIDE_SETTINGS = ROOT / "tools" / "wide_only_control_settings.ini"

# `element=<name> index=<n> anchor=<class> authored=(<x>, <w>) drawn=(<x>, <w>) frame=<a>-><d> offset=<n>`
PLACED = re.compile(
    r"\[uihud[^\]]*\] element=(?P<name>\S+) index=(?P<index>\d+) anchor=(?P<anchor>\S+) "
    r"authored=\((?P<ax>-?\d+), (?P<aw>-?\d+)\) "
    r"drawn=\((?P<dx>-?\d+), (?P<dw>-?\d+)\) frame=(?P<fa>\d+)->(?P<fd>\d+) offset=(?P<off>-?\d+)"
)
# `element=<name> index=<n> anchor=<class> correction=<n> frame=<a>-><d>`
CORRECTION = re.compile(
    r"\[uihud[^\]]*\] element=(?P<name>\S+) index=(?P<index>\d+) anchor=(?P<anchor>\S+) "
    r"correction=(?P<shift>-?\d+) frame=(?P<fa>\d+)->(?P<fd>\d+)"
)
REFUSED = re.compile(r"\[uihud[^\]]*\] REFUSED element=(?P<name>\S+) index=(?P<index>\d+).*?reason=(?P<reason>\S+)")

LEFT = "left-edge"
CENTRED = "centred"
RIGHT = "right-edge"
CLASSES = (LEFT, CENTRED, RIGHT)


@dataclass
class Element:
    name: str
    index: int
    anchor: str
    authored: tuple[int, int]
    drawn: tuple[int, int]
    frame: tuple[int, int]
    offset: int


@dataclass
class Reading:
    elements: dict[tuple[str, int], Element] = field(default_factory=dict)
    refusals: list[str] = field(default_factory=list)
    # Projected HUD parts (the `g_Hud` Mobys) report a correction to the widened projection rather
    # than a placed box: (anchor, shift, frame), LAST value per element.
    corrections: dict[tuple[str, int], tuple[str, int, tuple[int, int]]] = field(default_factory=dict)
    frames: set[tuple[int, int]] = field(default_factory=set)

    @property
    def count(self) -> int:
        return len(self.elements)


def read_log(path: Path) -> Reading:
    """Every placement the product reported in one log, LAST value per element.

    LAST, not first: a HUD element is re-placed every present, and the counts are what the run
    reported, so the first line is a sample from the middle of the level rather than a fact about
    the element. Refusals are collected rather than dropped: a census that silently ignored a
    refused element would report the surviving ones and read as complete.
    """
    reading = Reading()
    for line in path.read_text(errors="replace").splitlines():
        match = PLACED.search(line)
        if match:
            key = (match["name"], int(match["index"]))
            reading.elements[key] = Element(
                name=match["name"],
                index=int(match["index"]),
                anchor=match["anchor"],
                authored=(int(match["ax"]), int(match["aw"])),
                drawn=(int(match["dx"]), int(match["dw"])),
                frame=(int(match["fa"]), int(match["fd"])),
                offset=int(match["off"]),
            )
            reading.frames.add((int(match["fa"]), int(match["fd"])))
            continue
        match = CORRECTION.search(line)
        if match:
            frame = (int(match["fa"]), int(match["fd"]))
            reading.corrections[(match["name"], int(match["index"]))] = (
                match["anchor"], int(match["shift"]), frame)
            reading.frames.add(frame)
            continue
        match = REFUSED.search(line)
        if match:
            refusal = f"{match['name']}[{match['index']}]: {match['reason']}"
            if refusal not in reading.refusals:
                reading.refusals.append(refusal)
    return reading


def margin(frame: tuple[int, int]) -> int:
    return (frame[1] - frame[0]) // 2


def check_element(narrow: Element, wide: Element) -> list[str]:
    """The three failures, as sentences naming the element and the numbers."""
    problems: list[str] = []
    tag = f"{narrow.name}[{narrow.index}] ({narrow.anchor})"
    if narrow.anchor != wide.anchor:
        problems.append(f"{tag}: class differs between aspects ({narrow.anchor} vs {wide.anchor})")
    if narrow.authored != wide.authored:
        problems.append(f"{tag}: authored x/width differs between aspects {narrow.authored} vs {wide.authored}")
    # THE 4:3 IDENTITY. No exceptions: an element that legitimately moved at 4:3 would be a
    # different defect, and allowing it here would hide it.
    if narrow.drawn[0] != narrow.authored[0]:
        problems.append(
            f"{tag}: 4:3 moved the element, drawn x={narrow.drawn[0]} != authored x={narrow.authored[0]}"
        )
    if narrow.drawn[1] != narrow.authored[1]:
        problems.append(
            f"{tag}: 4:3 changed the width, drawn w={narrow.drawn[1]} != authored w={narrow.authored[1]}"
        )
    # NOTHING STRETCHES.
    for leg, element in (("4:3", narrow), ("16:9", wide)):
        if element.drawn[1] != element.authored[1]:
            problems.append(
                f"{tag}: STRETCHED at {leg}, drawn w={element.drawn[1]} != authored w={element.authored[1]}"
            )
    if narrow.drawn[1] != wide.drawn[1]:
        problems.append(f"{tag}: width changed between aspects {narrow.drawn[1]} -> {wide.drawn[1]}")
    if narrow.anchor == LEFT:
        if wide.drawn[0] != narrow.drawn[0]:
            problems.append(
                f"{tag}: left-edge element moved, {narrow.drawn[0]} -> {wide.drawn[0]} "
                f"(its inset from the widened left edge is not the authored one)"
            )
    elif narrow.anchor == RIGHT:
        near = narrow.frame[0] - (narrow.drawn[0] + narrow.drawn[1])
        far = wide.frame[1] - (wide.drawn[0] + wide.drawn[1])
        if near != far:
            problems.append(
                f"{tag}: right-edge inset changed, {near} -> {far} (authored right inset is not preserved)"
            )
    elif narrow.anchor == CENTRED:
        near = (narrow.drawn[0] * 2 + narrow.drawn[1]) - narrow.frame[0]
        far = (wide.drawn[0] * 2 + wide.drawn[1]) - wide.frame[1]
        if near != far:
            problems.append(
                f"{tag}: centred element moved off the centre, offset {near // 2} -> {far // 2} px"
            )
    else:
        problems.append(f"{tag}: unknown anchor class")
    return problems


def expected_correction(anchor: str, frame: tuple[int, int]) -> int | None:
    """What the widened projection's own centring has to be corrected by, per class."""
    return {LEFT: -margin(frame), CENTRED: 0, RIGHT: margin(frame)}.get(anchor)


def check_corrections(narrow: Reading, wide: Reading) -> list[str]:
    problems: list[str] = []
    for leg, reading, widened in (("4:3", narrow, False), ("16:9", wide, True)):
        for (name, index), (anchor, shift, frame) in sorted(reading.corrections.items()):
            want = expected_correction(anchor, frame) if widened else 0
            if want is None:
                problems.append(f"{name}[{index}] ({anchor}): unknown class at {leg}")
            elif shift != want:
                problems.append(f"{name}[{index}] ({anchor}): {leg} correction {shift}, expected {want}")
    return problems


def check_legs(narrow: Reading, wide: Reading) -> list[str]:
    """Properties of the two RUNS, before any element is compared.

    A refused element was left unanchored, so the legs that survive would report a clean census over
    a HUD that is half wrong; and a 'wide' leg that was not widened (or a 'narrow' leg that was)
    makes every distance comparison vacuous, because every offset is then zero whichever way it was
    computed. Both are run failures, not element failures.
    """
    problems = [f"REFUSED in the {label} leg: {refusal}"
                for label, reading in (("narrow", narrow), ("wide", wide)) for refusal in reading.refusals]
    for frame in sorted(narrow.frames):
        if frame[1] != frame[0]:
            problems.append(f"the 4:3 leg drew into a widened frame {frame}")
    for frame in sorted(wide.frames):
        if frame[1] <= frame[0]:
            problems.append(f"the 16:9 leg was not widened, frame {frame}: every offset is vacuously zero")
    return problems


def report(narrow: Reading, wide: Reading) -> int:
    print(f"narrow leg: {narrow.count} elements, frames {sorted(narrow.frames) or 'none reported'}")
    print(f"wide leg:   {wide.count} elements, frames {sorted(wide.frames) or 'none reported'}")
    if narrow.count + len(narrow.corrections) == 0 or wide.count + len(wide.corrections) == 0:
        print(
            "VERDICT: NO CENSUS — one leg reported no HUD element at all. "
            "A census with one leg empty is not a passing census; it is a run that never reached "
            "the screen under test (or an un-armed PSXPORT_DEBUG=uihud channel)."
        )
        return 1
    if set(narrow.elements) != set(wide.elements):
        only_narrow = sorted(set(narrow.elements) - set(wide.elements))
        only_wide = sorted(set(wide.elements) - set(narrow.elements))
        print(f"VERDICT: ELEMENT SETS DIFFER — only in 4:3: {only_narrow}; only in 16:9: {only_wide}")
        return 1

    print()
    header = f"{'element':28} {'class':10} {'authored':>12} {'4:3':>12} {'16:9':>12} {'margin':>7}"
    print(header)
    print("-" * len(header))
    problems: list[str] = check_legs(narrow, wide)
    for key in sorted(narrow.elements):
        near, far = narrow.elements[key], wide.elements[key]
        print(
            f"{near.name + '[' + str(near.index) + ']':28} {near.anchor:10} "
            f"{str(near.authored):>12} {str(near.drawn):>12} {str(far.drawn):>12} "
            f"{margin(far.frame):>7}"
        )
        problems.extend(check_element(near, far))
    problems.extend(check_corrections(narrow, wide))

    # WHAT WAS NOT EXERCISED, stated rather than implied: a PASS over centred elements only says
    # nothing about the two edge classes, and the route decides which classes exist.
    drawn = [element.anchor for element in narrow.elements.values()]
    drawn += [anchor for anchor, _, _ in narrow.corrections.values()]
    print()
    print("classes exercised: " + ", ".join(f"{cls}={drawn.count(cls)}" for cls in CLASSES))
    for cls in CLASSES:
        if drawn.count(cls) == 0:
            print(f"  NOTE: no {cls} element reached the screen on this route; that class is unmeasured")
    print()
    if problems:
        print(f"VERDICT: FAIL — {len(problems)} problem(s) over {narrow.count} elements")
        for problem in problems:
            print(f"  {problem}")
        return 1
    print(
        f"VERDICT: PASS — {narrow.count} elements: 4:3 identity, no width changed, every class's "
        f"own distance preserved at 16:9 ({sorted(wide.frames)})"
    )
    return 0


def _line(name: str, index: int, anchor: str, authored: tuple[int, int],
          drawn: tuple[int, int], frame: tuple[int, int], offset: int) -> str:
    return (
        f"[uihud] element={name} index={index} anchor={anchor} "
        f"authored=({authored[0]}, {authored[1]}) drawn=({drawn[0]}, {drawn[1]}) "
        f"frame={frame[0]}->{frame[1]} offset={offset}"
    )


def synthetic(stretched: bool, path: Path, wide_leg: bool) -> None:
    """One leg over the six elements, correctly anchored or deliberately broken.

    One leg per file, because the parser keeps the LAST line per element and a file holding both
    aspects would silently answer about the wide one twice.
    """
    wide_frame = (512, 684)
    narrow_frame = (512, 512)
    shift = 86
    elements = [
        ("life-orb", 0, LEFT, (12, 8)),
        ("life-orb", 1, LEFT, (22, 8)),
        ("egg-gem", 0, RIGHT, (470, 10)),
        ("gem-tally", 0, CENTRED, (90, 28)),
        ("pause-panel", 0, CENTRED, (140, 232)),
        ("egg-gem", 1, LEFT, (63, 24)),
    ]
    lines: list[str] = []
    for name, index, anchor, (ax, aw) in elements:
        if anchor == LEFT:
            narrow_drawn, wide_drawn, offset = (ax, aw), (ax, aw), 0
        elif anchor == RIGHT:
            narrow_drawn = (ax, aw)
            wide_drawn = (ax + 2 * shift, aw)
            offset = 2 * shift
        else:
            narrow_drawn = (ax, aw)
            wide_drawn = (ax + shift, aw)
            offset = shift
        if stretched and wide_leg:
            if anchor == CENTRED:
                # A stretched centred element: the x is right and the WIDTH is not the authored one.
                wide_drawn = (wide_drawn[0], aw * wide_frame[1] // wide_frame[0])
            if anchor == RIGHT:
                # A right-edge element placed by the centring rule: its distance from the RIGHT
                # edge doubles, which is invisible in a picture and obvious in the census.
                wide_drawn = (ax + shift, aw)
        if wide_leg:
            lines.append(_line(name, index, anchor, (ax, aw), wide_drawn, wide_frame, offset))
        else:
            lines.append(_line(name, index, anchor, (ax, aw), narrow_drawn, narrow_frame, 0))
    path.write_text("\n".join(lines) + "\n")


def selftest() -> int:
    scratch = ROOT / "scratch" / "hud-anchor-selftest"
    scratch.mkdir(parents=True, exist_ok=True)
    good_n, good_w = scratch / "anchored-4x3.log", scratch / "anchored-16x9.log"
    bad_n, bad_w = scratch / "stretched-4x3.log", scratch / "stretched-16x9.log"
    # The two legs of one run are the SAME product output read at two aspects; the synthetic logs
    # are written per leg so the selftest exercises the same parser and the same checks.
    synthetic(False, good_n, wide_leg=False)
    synthetic(False, good_w, wide_leg=True)
    # Break only the 16:9 leg, exactly as a real regression would.
    synthetic(False, bad_n, wide_leg=False)
    synthetic(True, bad_w, wide_leg=True)

    def text(path: Path) -> str:
        return path.read_text()

    def leg(body: str, name: str) -> Path:
        path = scratch / name
        path.write_text(body)
        return path

    def correction_line(name: str, anchor: str, shift: int, frame: tuple[int, int]) -> str:
        return (f"[uihud] element={name} index=0 anchor={anchor} correction={shift} "
                f"frame={frame[0]}->{frame[1]}\n")

    good_corrections = (
        correction_line("hud-gem", LEFT, -86, (512, 684))
        + correction_line("hud-dragon", CENTRED, 0, (512, 684))
        + correction_line("hud-lives", RIGHT, 86, (512, 684))
    )
    narrow_corrections = "".join(
        correction_line(n, a, 0, (512, 512))
        for n, a in (("hud-gem", LEFT), ("hud-dragon", CENTRED), ("hud-lives", RIGHT))
    )
    refusal = ("[uihud:warn] REFUSED element=pause-border index=2 anchor=centred authored=(372, 0) "
               "reason=non-positive-width frame=512->684\n")
    cases = [
        # name, narrow log, wide log, must pass
        ("anchored pair", text(good_n), text(good_w), True),
        ("anchored pair with projected HUD parts",
         text(good_n) + narrow_corrections, text(good_w) + good_corrections, True),
        ("STRETCHED centred element and a right edge placed by the centring rule",
         text(bad_n), text(bad_w), False),
        ("HUD part corrected by the wrong sign",
         text(good_n) + narrow_corrections,
         text(good_w) + good_corrections.replace("correction=-86", "correction=86"), False),
        ("an element refused in the wide leg", text(good_n), text(good_w) + refusal, False),
        ("the 'wide' leg is the 4:3 run", text(good_n), text(good_n), False),
    ]
    failed = []
    for index, (label, narrow_text, wide_text, must_pass) in enumerate(cases):
        print(f"== {label} (expected {'PASS' if must_pass else 'FAIL'})")
        code = report(read_log(leg(narrow_text, f"case{index}-narrow.log")),
                      read_log(leg(wide_text, f"case{index}-wide.log")))
        print()
        if (code == 0) != must_pass:
            failed.append(label)
    if failed:
        print("SELFTEST FAILED: the census answered wrongly on: " + "; ".join(failed))
        return 1
    print(f"SELFTEST: {len(cases)} cases, both answers shown — every defect case FAILed and every "
          f"anchored case PASSed")
    return 0


def drive(leg: str, settings: Path, log: Path, shot: str) -> int:
    """One leg: the port's own driver, on the same route as the other leg.

    The route is `gameplay --tap start --after 120`: it reaches GS_Playing, opens the PAUSE MENU with
    a real pad edge, and shoots there. The pause menu is on the route because it is a HUD element
    this project can reach on demand, and a census that could only be taken on a screen the route
    happens to pass would be a census of whatever the route happened to show.
    """
    command = [
        "uv", "run", "--frozen", "python", str(ROOT / "tools" / "drive.py"), "gameplay",
        "--settings", str(settings),
        "--debug", "uihud",
        "--log", str(log),
        "--tap", "start",
        "--after", "120",
        "--shot", shot,
        "--env", "PSXPORT_WATCHDOG=60",
    ]
    print(f"[{leg}] {' '.join(command)}")
    completed = subprocess.run(command, cwd=ROOT)
    return completed.returncode


def front_end_leg(leg: str, settings: Path, log: Path, shot: str) -> int:
    """One leg of the STAGE-13 FRONT END, which the gameplay route only passes through.

    `tools/drive.py` drives all the way to GS_Playing and the stage-13 title/menu is behind it, so
    this leg drives the SAME product through the SAME REPL to the title screen and stops there. It
    reuses drive.Port rather than a second driver, because two drivers reaching the same screen by
    different routes are two routes and the comparison between the aspects would then be between
    different frames.
    """
    sys.path.insert(0, str(ROOT / "tools"))
    import drive  # noqa: PLC0415 — the product's own driver, imported for its Port

    env = drive.environment(drive.disc_path())
    env["PSXPORT_SETTINGS"] = str(settings.resolve())
    env["PSXPORT_DEBUG"] = "uihud"
    env["PSXPORT_WATCHDOG"] = "60"
    port = drive.Port(ROOT / "build" / "bin" / "spyro_port",
                      ROOT / "scratch" / "assets" / "spyro1" / "SCUS_942.28", log, env)
    reached = False
    try:
        for _ in range(120):
            port.run(20)
            if port.gamestate() == drive.GS_TITLE_SCREEN:
                reached = True
                break
        if not reached:
            print(f"[{leg}] the front-end leg never reached GS_TitleScreen in 2400 fields")
        port.shot(shot)
        port.run(1)
    finally:
        code = port.end()
    return 0 if reached else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--selftest", action="store_true", help="the two synthetic answers")
    parser.add_argument("--narrow-log", type=Path, help="report over a captured 4:3 log")
    parser.add_argument("--wide-log", type=Path, help="report over a captured 16:9 log")
    parser.add_argument("--log-dir", type=Path, default=ROOT / "scratch" / "hud-anchor",
                        help="where the two driven runs write their logs and captures")
    args = parser.parse_args()

    if args.selftest:
        return selftest()
    if args.narrow_log and args.wide_log:
        return report(read_log(args.narrow_log), read_log(args.wide_log))
    if args.narrow_log or args.wide_log:
        parser.error("--narrow-log and --wide-log go together: a difference needs both legs")

    args.log_dir.mkdir(parents=True, exist_ok=True)
    narrow_log = args.log_dir / "4x3.log"
    wide_log = args.log_dir / "16x9.log"
    narrow_front_log = args.log_dir / "4x3-front.log"
    wide_front_log = args.log_dir / "16x9-front.log"
    narrow_shot = str(args.log_dir / "gameplay-4x3.ppm")
    wide_shot = str(args.log_dir / "gameplay-16x9.ppm")
    narrow_front_shot = str(args.log_dir / "frontend-4x3.ppm")
    wide_front_shot = str(args.log_dir / "frontend-16x9.ppm")
    status = drive("4:3", NARROW_SETTINGS, narrow_log, narrow_shot)
    status |= drive("16:9", WIDE_SETTINGS, wide_log, wide_shot)
    status |= front_end_leg("4:3 front end", NARROW_SETTINGS, narrow_front_log, narrow_front_shot)
    status |= front_end_leg("16:9 front end", WIDE_SETTINGS, wide_front_log, wide_front_shot)
    if status != 0:
        print(f"REFUSED: a driven leg exited {status}; its log is kept, and a route that did not run "
              f"cannot produce a census")
        return 1
    # Both routes' logs are read into ONE reading per aspect: the census is about elements, and an
    # element that only exists on one of the two routes is still an element.
    def merged(*logs: Path) -> Reading:
        combined = Reading()
        for path in logs:
            one = read_log(path)
            combined.elements.update(one.elements)
            combined.refusals.extend(one.refusals)
            combined.frames |= one.frames
        return combined

    return report(merged(narrow_log, narrow_front_log), merged(wide_log, wide_front_log))


if __name__ == "__main__":
    sys.exit(main())
