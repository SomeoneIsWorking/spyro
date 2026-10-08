#!/usr/bin/env python3
"""determinism_check.py — prove two runs of the same binary on the same inputs are the same run.

    uv run --frozen python tools/determinism_check.py --title spyro2
    uv run --frozen python tools/determinism_check.py --title spyro3 --fields 1500
    uv run --frozen python tools/determinism_check.py --selftest

WHAT IT MEASURES. The framework's `fielddigest` channel (psxport `runtime/psx/debug/field_digest.h`) prints one
line per display field: the emulated CPU tick, a hash of all 2 MB of main RAM, I_STAT, I_MASK and the pad.
Nothing host-derived is in it. This tool launches the title headless twice with identical inputs, reads
both digests, and refuses unless every field of one equals the same field of the other. When they differ it
names the FIRST field and the columns that differ, which is the divergence point and the only place to look.

WHY IT EXISTS (docs/issues/0169). Spyro 3's route reached gameplay at field 4740 on two runs and 4890 on a
third, Spyro 2's at 3340 once and 3190 after. The cause was not host time: the memory card is an input the
guest reads at boot and a file the run writes, and every run shared one. A digest of two Spyro 2 runs agreed
for fields 0..244 and differed at 245, where the guest first reads the card. This is the check that would have
named that field the first time.

THE NEGATIVE. A check that has only ever printed PASS has not shown it can fail. After the two identical legs
the tool runs a THIRD whose only difference is one pad tap at `PERTURB_FIELD`; it must diverge, and not before
that field. A tool whose perturbed leg still matches is refused as blind. Needs the disc and the built port;
the comparison logic is exercised without either by `--selftest` (a ctest).
"""

from __future__ import annotations

import argparse
import re
import sys
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from title_profile import Refusal

EXIT_OK, EXIT_NOT_DETERMINISTIC, EXIT_REFUSED = 0, 1, 2

DEFAULT_FIELDS = 1200  # past the boot logos and the first card reads of both titles
PERTURB_FIELD = 300  # the pad tap of the negative leg; late enough that both titles are past their first card read
CHUNK = 10  # fields per REPL round trip, the resolution every route samples at
CHANNEL_ENVIRONMENT = {"PSXPORT_DEBUG": "fielddigest"}

_LINE = re.compile(
    r"\[fielddigest\] field=(\d+) ticks=(\d+) ram=([0-9a-f]{16}) i_stat=([0-9a-f]+) i_mask=([0-9a-f]+) pad=([0-9a-f]{4})"
)
_COLUMNS = ("ticks", "ram", "i_stat", "i_mask", "pad")


@dataclass(frozen=True)
class FieldState:
    field: int
    ticks: int
    ram: str
    i_stat: str
    i_mask: str
    pad: str


@dataclass(frozen=True)
class Divergence:
    field: int
    columns: tuple[str, ...]  # which columns differ; a length mismatch is `("length",)`


def parse_digest(text: str) -> list[FieldState]:
    """Every `fielddigest` line of a run log, in order. Refuses a log with none: a run whose channel never
    printed would compare equal to anything, which is the failure this tool exists to rule out."""
    states = [
        FieldState(int(m[1]), int(m[2]), m[3], m[4], m[5], m[6]) for m in _LINE.finditer(text)
    ]
    if not states:
        raise Refusal("no [fielddigest] line in the run log: the channel was not on (PSXPORT_DEBUG=fielddigest) "
                      "or the port does not carry it, so there is nothing to compare")
    if [s.field for s in states] != list(range(len(states))):
        raise Refusal("the [fielddigest] fields are not 0..N-1 in order; the log is truncated or interleaved")
    return states


def first_divergence(left: Sequence[FieldState], right: Sequence[FieldState],
                     columns: Sequence[str] = _COLUMNS) -> Divergence | None:
    """The first field at which the runs differ in any of `columns`, or None when they are the same run."""
    for a, b in zip(left, right):
        differing = tuple(name for name in columns if getattr(a, name) != getattr(b, name))
        if differing:
            return Divergence(a.field, differing)
    if len(left) != len(right):
        return Divergence(min(len(left), len(right)), ("length",))
    return None


def _describe(divergence: Divergence | None) -> str:
    if divergence is None:
        return "identical"
    return f"diverge at field {divergence.field} in {', '.join(divergence.columns)}"


def judge(baseline: Sequence[FieldState], repeat: Sequence[FieldState], perturbed: Sequence[FieldState],
          perturb_field: int) -> list[str]:
    """The reasons the check fails; empty means it passed. Separate from the live legs so the selftest can
    show it fail for each reason."""
    failures: list[str] = []
    repeated = first_divergence(baseline, repeat)
    if repeated is not None:
        failures.append(f"two identical runs differ: {_describe(repeated)}")
    # The pad column echoes the tap itself, so only a difference in guest RAM shows the digest can see
    # the guest reacting to an input, which is what a match between the identical legs must be worth.
    seen = first_divergence(baseline, perturbed, ("ram",))
    if seen is None:
        failures.append(f"the perturbed leg (a pad tap at field {perturb_field}) left guest RAM identical to "
                        "the baseline: the digest is blind, so a match above means nothing")
    elif seen.field < perturb_field:
        failures.append(f"the perturbed leg's RAM diverged at field {seen.field}, before its only difference at "
                        f"field {perturb_field}: something other than the input differs between runs")
    return failures


def _run_leg(name: str, executable: Path, log: Path, fields: int, tap_at: int | None) -> list[FieldState]:
    import drive
    import title_route

    _, port = title_route.open_port(name, executable, log, CHANNEL_ENVIRONMENT)
    try:
        spent = 0
        while spent < fields:
            if tap_at is not None and spent == tap_at:
                port.tap("start", 6)
            port.run(min(CHUNK, fields - spent))
            spent += CHUNK
    except drive.Refusal as refusal:
        raise Refusal(str(refusal)) from refusal
    finally:
        port.end()
    return parse_digest(log.read_text())


def run_live(name: str, executable: Path, fields: int, log_dir: Path) -> int:
    import drive

    if fields <= PERTURB_FIELD + CHUNK:
        raise Refusal(f"--fields {fields} leaves no room after the perturbation at field {PERTURB_FIELD}")
    (drive.ROOT / log_dir).mkdir(parents=True, exist_ok=True)
    legs = {}
    for label, tap_at in (("baseline", None), ("repeat", None), ("perturbed", PERTURB_FIELD)):
        legs[label] = _run_leg(name, executable, drive.ROOT / log_dir / f"{name}.{label}.log", fields, tap_at)
        print(f"  {label}: {len(legs[label])} fields digested")
    failures = judge(legs["baseline"], legs["repeat"], legs["perturbed"], PERTURB_FIELD)
    print(f"{name}: baseline vs repeat {_describe(first_divergence(legs['baseline'], legs['repeat']))}; "
          f"baseline vs perturbed {_describe(first_divergence(legs['baseline'], legs['perturbed']))}; "
          f"first guest RAM difference {_describe(first_divergence(legs['baseline'], legs['perturbed'], ('ram',)))}")
    for failure in failures:
        print(f"FAILED: {failure}", file=sys.stderr)
    return EXIT_NOT_DETERMINISTIC if failures else EXIT_OK


def _state(field: int, ram: str = "0" * 16, pad: str = "ffff") -> FieldState:
    return FieldState(field, 1000 * field, ram, "1", "d", pad)


def _run_of(length: int, flipped_from: int | None = None) -> list[FieldState]:
    return [_state(f, ram=("1" if flipped_from is not None and f >= flipped_from else "0") * 16)
            for f in range(length)]


def _selftest() -> int:
    ok = True

    def expect(label: str, condition: bool) -> None:
        nonlocal ok
        print(f"  {'ok  ' if condition else 'FAIL'} {label}")
        ok = ok and condition

    base = _run_of(100)
    expect("identical runs have no divergence", first_divergence(base, _run_of(100)) is None)
    drift = first_divergence(base, _run_of(100, flipped_from=40))
    expect("a RAM difference is named at its first field", drift == Divergence(40, ("ram",)))
    expect("a truncated run is a divergence at its end", first_divergence(base, _run_of(90)) == Divergence(90, ("length",)))
    expect("a tick difference alone is a divergence",
           first_divergence(base, [_state(f) if f != 7 else FieldState(7, 1, "0" * 16, "1", "d", "ffff") for f in range(100)])
           == Divergence(7, ("ticks",)))
    expect("a pad difference alone is a divergence",
           first_divergence(base, [_state(f, pad="fffe" if f == 3 else "ffff") for f in range(100)]) == Divergence(3, ("pad",)))

    perturbed = _run_of(100, flipped_from=60)
    expect("a deterministic pair with a visible perturbation passes", judge(base, _run_of(100), perturbed, 50) == [])
    expect("first_divergence can be limited to one column",
           first_divergence(base, perturbed, ("pad",)) is None and first_divergence(base, perturbed, ("ram",)).field == 60)
    expect("two runs that drift fail", len(judge(base, _run_of(100, flipped_from=40), perturbed, 50)) == 1)
    blind = judge(base, _run_of(100), _run_of(100), 50)
    expect("a perturbed leg that matches is refused as blind", len(blind) == 1 and "blind" in blind[0])
    echo_only = [_state(f, pad="fffe" if f == 50 else "ffff") for f in range(100)]
    echoed = judge(base, _run_of(100), echo_only, 50)
    expect("a perturbed leg that only echoes the tap in the pad column is refused as blind",
           len(echoed) == 1 and "blind" in echoed[0])
    early = judge(base, _run_of(100), _run_of(100, flipped_from=20), 50)
    expect("a perturbed leg that diverges before its perturbation is refused", len(early) == 1 and "before" in early[0])

    text = "\n".join(f"[2026-10-01T00:00:00.000Z] [fielddigest] field={s.field} ticks={s.ticks} ram={s.ram} "
                     f"i_stat={s.i_stat} i_mask={s.i_mask} pad={s.pad}" for s in base)
    expect("a log round-trips through the parser", parse_digest(text) == base)
    for label, bad in (("a log with no digest lines", "[boot] nothing here\n"),
                       ("a log with a missing field", text.replace("field=5 ", "field=500 "))):
        try:
            parse_digest(bad)
        except Refusal:
            expect(f"{label} is refused", True)
        else:
            expect(f"{label} is refused", False)
    print("determinism_check selftest " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--title", choices=("spyro2", "spyro3"))
    parser.add_argument("--fields", type=int, default=DEFAULT_FIELDS)
    parser.add_argument("--executable", type=Path, default=Path("build/bin/spyro_port"))
    parser.add_argument("--log-dir", type=Path, default=Path("scratch/determinism"))
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args(argv)
    if args.selftest:
        return _selftest()
    if not args.title:
        parser.error("--title is required")
    try:
        return run_live(args.title, args.executable, args.fields, args.log_dir)
    except Refusal as refusal:
        print(f"REFUSED: {refusal}", file=sys.stderr)
        return EXIT_REFUSED


if __name__ == "__main__":
    raise SystemExit(main())
