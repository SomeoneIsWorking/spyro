#!/usr/bin/env python3
"""Per-FIELD route trace of a product run, and the first divergence between two of them.

    uv run --frozen python tools/route_trace.py compare A.log B.log
    uv run --frozen python tools/route_trace.py extract run.log --limit 20
    uv run --frozen python tools/route_trace.py ramdiff A.ram B.ram [--base 0x80000000]
    uv run --frozen python tools/route_trace.py --selftest

WHY THIS EXISTS. A change to a shared owner (the field owner both Spyro 1 and Spyro 2 deliver
through) is asked the only question that matters about the title it moved code out of: does the
title still behave the same? The route corpus answers that with an aggregate -- a mismatch count
over six routes -- which cannot say WHERE two builds stopped agreeing, and whose one input-driven
route is the same six routes that were green before the change. The attract demo is the route with
no input at all, so its path is fixed by the guest and its field timing, and a field-by-field trace
of it is a direct answer.

WHAT A TRACE IS, and why these three lines. The product already prints, per delivered field, one
`[pace]` line carrying the delivered-field index, the site that asked for it and the guest 60 Hz
counter; and, on every change of guest state, one `[skipmap]` line carrying the field number, the
load stage, the gamestate and the title screen's own mode/state/substate. Together they are the
delivery order AND the guest state at each delivery, indexed by a counter both builds increment the
same way. This tool reads those lines out of a log, normalises them, and compares two. It adds no
instrument to the product and needs no new one: the two channels are already armed by
`tools/demo_run.py --debug pace,skipmap`.

WHAT IT REFUSES, because a trace tool that reports "0 differences" for a log it could not read is
the same dead-tap shape this project keeps finding. A log with no `[pace]` line, a field index that
does not advance by one, a state line whose field number moves backwards, and a second log shorter
than the first are all refusals or reported truncations, never a pass.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

PACE = re.compile(r"vbl=(\d+) .*?site=(\S+?) quota=(\d+) counter=(-?\d+)")
STATE = re.compile(
    r"field=(\d+) start_edge=(\d) region=(\S+) load_stage=(\d+) gamestate=(\d+) "
    r"title\[mode=(\d+) state=(\d+) substate=(\d+)\]"
)
REFUSAL = "NOT IMPLEMENTED"


@dataclass(frozen=True)
class Field:
    """One delivered field: its index, the site that asked for it, and the guest counter."""

    vbl: int
    site: str
    quota: int
    counter: int


@dataclass(frozen=True)
class StateChange:
    """One guest-state change, at the field it was observed on."""

    field: int
    region: str
    load_stage: int
    gamestate: int
    title: tuple[int, int, int]


@dataclass
class Trace:
    path: Path
    fields: list[Field]
    states: list[StateChange]
    refusals: list[str]

    def last_field(self) -> int:
        return self.fields[-1].vbl if self.fields else -1


def parse(path: Path) -> Trace:
    """Read one run log. Raises ValueError naming what it could not read."""
    if not path.is_file():
        raise ValueError(f"no run log at {path}")
    fields: list[Field] = []
    states: list[StateChange] = []
    refusals: list[str] = []
    for line in path.read_text(errors="replace").splitlines():
        pace = PACE.search(line)
        if pace is not None:
            field = Field(int(pace[1]), pace[2], int(pace[3]), int(pace[4]))
            if fields and field.vbl != fields[-1].vbl + 1:
                raise ValueError(
                    f"{path}: delivered-field index jumped {fields[-1].vbl} -> {field.vbl}; "
                    "this log is not one continuous field sequence and cannot be a trace"
                )
            fields.append(field)
            continue
        state = STATE.search(line)
        if state is not None:
            change = StateChange(
                field=int(state[1]),
                region=state[3],
                load_stage=int(state[4]),
                gamestate=int(state[5]),
                title=(int(state[6]), int(state[7]), int(state[8])),
            )
            if states and change.field <= states[-1].field:
                raise ValueError(
                    f"{path}: state line at field {change.field} follows field {states[-1].field}; "
                    "the state trace is not monotonic"
                )
            states.append(change)
            continue
        if REFUSAL in line:
            refusals.append(line.split("] ", 1)[-1].strip())
    if not fields:
        raise ValueError(
            f"{path}: 0 delivered-field ([pace]) lines — rerun the route with "
            "`tools/demo_run.py --debug pace,skipmap`; an unread log is not an empty trace"
        )
    return Trace(path, fields, states, refusals)


def first_field_difference(left: Trace, right: Trace) -> tuple[int, str] | None:
    """The first delivered field whose (site, counter) differs, with what differs."""
    for index, (mine, theirs) in enumerate(zip(left.fields, right.fields)):
        if mine.vbl != theirs.vbl:
            return index, f"field index {mine.vbl} against {theirs.vbl}"
        if mine.site != theirs.site:
            return index, f"field {mine.vbl}: site {mine.site!r} against {theirs.site!r}"
        if mine.counter != theirs.counter:
            return (
                index,
                f"field {mine.vbl}: 60 Hz counter {mine.counter} against {theirs.counter}",
            )
    return None


def first_state_difference(left: Trace, right: Trace) -> tuple[int, str] | None:
    """The first guest-state change that differs, by the field it was observed on."""
    by_field_left = {change.field: change for change in left.states}
    by_field_right = {change.field: change for change in right.states}
    for field in sorted(set(by_field_left) | set(by_field_right)):
        mine = by_field_left.get(field)
        theirs = by_field_right.get(field)
        if mine is None or theirs is None:
            missing = "absent from the second run" if theirs is None else "absent from the first"
            side = mine or theirs
            assert side is not None
            return field, f"field {field}: a state change is {missing} (gamestate {side.gamestate})"
        if mine != theirs:
            return (
                field,
                f"field {field}: load_stage {mine.load_stage}/{theirs.load_stage} "
                f"gamestate {mine.gamestate}/{theirs.gamestate} "
                f"title {mine.title}/{theirs.title}",
            )
    return None


def compare(left: Trace, right: Trace) -> int:
    """Print both runs' denominators and the first divergence, and fail when they differ."""
    print(f"left  {left.path}: {len(left.fields)} fields, {len(left.states)} state changes, "
          f"{len(left.refusals)} refusal(s)")
    print(f"right {right.path}: {len(right.fields)} fields, {len(right.states)} state changes, "
          f"{len(right.refusals)} refusal(s)")
    for trace in (left, right):
        for refusal in trace.refusals:
            print(f"  refusal [{trace.path.name}] {refusal}")
    field_difference = first_field_difference(left, right)
    state_difference = first_state_difference(left, right)
    shared = min(len(left.fields), len(right.fields))
    print(f"shared prefix: {shared} delivered fields compared")
    if len(left.fields) != len(right.fields):
        longer, shorter = (left, right) if len(left.fields) > len(right.fields) else (right, left)
        print(
            f"  TRUNCATED: {longer.path.name} ran to field {longer.last_field()} and "
            f"{shorter.path.name} only to {shorter.last_field()}"
        )
    failures = 0
    if field_difference is None:
        print("  fields: identical over the shared prefix")
    else:
        failures += 1
        print(f"  fields: FIRST DIVERGENCE at index {field_difference[0]}: {field_difference[1]}")
    if state_difference is None:
        print("  state: identical at every field both runs reported a change on")
    else:
        failures += 1
        print(f"  state: FIRST DIVERGENCE at {state_difference[1]}")
    return failures


def extract(trace: Trace, limit: int) -> None:
    for field in trace.fields[:limit]:
        print(f"field {field.vbl:8} site={field.site:22} counter={field.counter}")
    for change in trace.states[:limit]:
        print(
            f"field {change.field:8} load_stage={change.load_stage} gamestate={change.gamestate} "
            f"title={change.title} region={change.region}"
        )
    print(f"[trace] {len(trace.fields)} fields, {len(trace.states)} state changes, "
          f"{len(trace.refusals)} refusal(s) from {trace.path}")


def ramdiff(left: Path, right: Path, base: int, limit: int) -> int:
    """Word-by-word guest RAM comparison of two `PSXPORT_GRAMDUMP` captures.

    The field trace says WHICH field two runs stopped agreeing on; this says whether the whole of
    guest memory agrees at one field, which is the claim a state trace cannot make on its own. Both
    captures must be the same size: a truncated dump compared against a full one would report a
    million "differences" that are really one missing file.
    """
    if not left.is_file() or not right.is_file():
        absent = left if not left.is_file() else right
        print(f"REFUSED: no RAM capture at {absent}", file=sys.stderr)
        return 2
    if left.stat().st_size != right.stat().st_size:
        print(
            f"REFUSED: {left.name} is {left.stat().st_size} bytes and {right.name} is "
            f"{right.stat().st_size}; two captures of one route must be the same size",
            file=sys.stderr,
        )
        return 2
    mine = left.read_bytes()
    theirs = right.read_bytes()
    differing: list[int] = []
    for offset in range(0, len(mine), 4):
        if mine[offset : offset + 4] != theirs[offset : offset + 4]:
            differing.append(offset)
    print(
        f"compared {len(mine) // 4} guest words of {left.name} against {right.name}: "
        f"{len(differing)} differ"
    )
    for offset in differing[:limit]:
        mine_word = int.from_bytes(mine[offset : offset + 4], "little")
        their_word = int.from_bytes(theirs[offset : offset + 4], "little")
        print(
            f"  0x{base + offset:08X}: {mine_word:08X} against {their_word:08X}"
        )
    if len(differing) > limit:
        print(f"  ... {len(differing) - limit} more")
    return 1 if differing else 0


def selftest() -> int:
    failures = 0

    def check(condition: bool, message: str) -> None:
        nonlocal failures
        if not condition:
            failures += 1
            print(f"FAIL {message}")

    def log(lines: list[str]) -> Path:
        path = Path(__file__).resolve().parent / "scratch_selftest_route_trace.log"
        path.write_text("\n".join(lines) + "\n")
        return path

    def pace(vbl: int, site: str = "nativeframe", counter: int | None = None) -> str:
        value = vbl if counter is None else counter
        return (
            f"[t] [pace] t=1.0ms vbl={vbl} pace={vbl} present={vbl} rq_unconsumed=0 | "
            f"site={site} quota=0 counter={value} rq_n=0 unconsumed=0"
        )

    def state(field: int, gamestate: int = 0) -> str:
        return (
            f"[t] [skipmap] field={field} start_edge=0 region=stage load_stage=3 "
            f"gamestate={gamestate} title[mode=0 state=0 substate=0] card_tick=4294967295 "
            "card_skippable=0 edges=0"
        )

    # 1. Two identical traces of DIFFERENT field counts compare equal over the shared prefix and
    #    say the longer one was truncated — the negative that keeps "0 differences" honest.
    long_trace = parse(log([pace(0), pace(1), state(1), pace(2)]))
    short_trace = parse(log([pace(0), pace(1), state(1)]))
    check(
        first_field_difference(long_trace, short_trace) is None,
        "identical prefixes must compare equal",
    )
    check(
        first_field_difference(short_trace, long_trace) is None,
        "comparison must be symmetric",
    )

    # 2. The other answer: one changed site is found, and the index is named.
    moved = parse(log([pace(0), pace(1, site="hostturn"), state(1)]))
    difference = first_field_difference(short_trace, moved)
    check(
        difference is not None and difference[0] == 1 and "hostturn" in difference[1],
        f"a changed site must be located, got {difference}",
    )

    # 3. A guest-state change that only one run saw is a divergence, not an absence.
    only_here = parse(log([pace(0), pace(1), state(1, gamestate=5)]))
    check(
        first_state_difference(short_trace, only_here) is not None,
        "a differing gamestate must be located",
    )
    check(
        first_state_difference(only_here, short_trace) is not None,
        "state comparison must be symmetric",
    )

    # 4. The refusals: an unread log is REFUSED, never reported as an empty trace that matches.
    for bad, why in (
        (log(["[t] [boot] nothing here"]), "a log with no field lines"),
        (log([pace(0), pace(2)]), "a log whose field index skips"),
        (log([pace(0), state(5), state(4)]), "a log whose state field moves backwards"),
    ):
        try:
            parse(bad)
        except ValueError:
            continue
        failures += 1
        print(f"FAIL {why} was accepted as a trace")

    # 5. A missing file is a refusal too.
    try:
        parse(Path("/nonexistent/route.log"))
    except ValueError:
        pass
    else:
        failures += 1
        print("FAIL a missing log was accepted as a trace")

    # 6. RAM: equal captures report no difference, one changed word is located by guest address,
    #    and a size mismatch is a refusal rather than a million phantom differences.
    ram = Path(__file__).resolve().parent / "scratch_selftest_route_trace.ram"
    ram.write_bytes(bytes(64))
    same = Path(str(ram) + ".b")
    same.write_bytes(bytes(64))
    if ramdiff(ram, same, 0x80000000, 4) != 0:
        failures += 1
        print("FAIL two equal RAM captures reported a difference")
    other = Path(str(ram) + ".c")
    edited = bytearray(bytes(64))
    edited[0x24:0x28] = (0xDEADBEEF).to_bytes(4, "little")
    other.write_bytes(bytes(edited))
    if ramdiff(ram, other, 0x80000000, 4) != 1:
        failures += 1
        print("FAIL a changed RAM word was not reported")
    truncated = Path(str(ram) + ".d")
    truncated.write_bytes(bytes(32))
    if ramdiff(ram, truncated, 0x80000000, 4) != 2:
        failures += 1
        print("FAIL a truncated RAM capture was compared instead of refused")
    if ramdiff(Path("/nonexistent/a.ram"), same, 0x80000000, 4) != 2:
        failures += 1
        print("FAIL a missing RAM capture was compared instead of refused")
    for stray in (same, other, truncated):
        stray.unlink(missing_ok=True)
    ram.unlink(missing_ok=True)

    Path(__file__).resolve().parent.joinpath("scratch_selftest_route_trace.log").unlink(
        missing_ok=True
    )
    print(f"selftest: {14 - failures} of 14 cases")
    return 1 if failures else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    subparsers = parser.add_subparsers(dest="command")
    pair = subparsers.add_parser("compare", help="the first divergence between two run logs")
    pair.add_argument("left", type=Path)
    pair.add_argument("right", type=Path)
    single = subparsers.add_parser("extract", help="print a run's field trace")
    single.add_argument("log", type=Path)
    single.add_argument("--limit", type=int, default=40)
    memory = subparsers.add_parser("ramdiff", help="word-by-word guest RAM comparison of two dumps")
    memory.add_argument("left", type=Path)
    memory.add_argument("right", type=Path)
    memory.add_argument("--base", default="0x80000000")
    memory.add_argument("--limit", type=int, default=40)
    # `--selftest` with no subcommand, so this tool registers in the same probe loop as every
    # other one in tools/.
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args(argv)

    if args.selftest or args.command is None:
        if not args.selftest:
            parser.error("give `compare LEFT RIGHT`, `extract LOG`, or --selftest")
        return selftest()
    try:
        if args.command == "compare":
            return compare(parse(args.left), parse(args.right))
        if args.command == "ramdiff":
            return ramdiff(args.left, args.right, int(args.base, 0), args.limit)
        extract(parse(args.log), args.limit)
    except ValueError as error:
        print(f"REFUSED: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
