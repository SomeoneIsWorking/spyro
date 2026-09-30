#!/usr/bin/env python3
"""boot_run.py — run a Spyro 2 or Spyro 3 boot headless and report what it did, with denominators.

    uv run --frozen python tools/boot_run.py --title spyro2
    uv run --frozen python tools/boot_run.py --title spyro3 --fallback-limit 100000000
    uv run --frozen python tools/boot_run.py --selftest

WHY THIS EXISTS. Both titles boot their retail prefix through Lightrec and then stop, and "where they
stop" is the whole finding: the loader's CD completion (psxport issue 0143), then whatever follows. The
tool that measured it lived in an untracked scratch directory and is gone, so each measurement was
re-derived by hand from a log. This runs the built `spyro_port` once, headless and silent, and prints one
report: fields, product steps, presentation fences, translated and executed blocks, executed
instructions, faults, fallback blocks and instructions by reason, the CD reads the guest issued against
the ready-callback completions queued and delivered, and the named stop or resume address.

The environment is `drive.py`'s (which takes the framework's launch policy: offscreen, silent,
unpaced, the tracked shipping settings), minus the interactive REPL a boot run has no use for. The
log parser is `boot_log.py`, the only reader of the port's own lines.

REFUSALS, each with a distinct exit status and a name: the built port, the title's executable image and
its disc are all required (2). A run whose log names neither a resume nor a stop is reported as NOT
IDENTIFIED and exits 1, because a report of zeros about a run nobody could read is the failure this tool
exists not to produce. `--require-cd-match` makes a delivery count that differs from the read count
exit 1 as well.

`--fallback-limit N` raises the Lightrec fallback budget. Spyro 2 needs it today, because a shared
self-modifying-block false positive at 0x8005FFFC (Spyro issue 0092 section 4) refuses the printf
emitter; the report says the budget was raised, because a run under it is diagnostic and not product
evidence. The log goes to `scratch/boot/<title>.log`, overwritten each run.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import boot_log  # noqa: E402
import drive  # noqa: E402

ROOT = drive.ROOT
EXIT_OK, EXIT_UNREADABLE_OR_MISMATCH, EXIT_REFUSED = 0, 1, 2

# Channels the report's CD denominators are read from. `cd` logs each CdRead, `cdirq` each queued and
# delivered completion; without them the report could only say "no reads seen".
BOOT_CHANNELS = "cd,cdirq"


@dataclass(frozen=True)
class Title:
    label: str
    image: str
    disc_variable: str


TITLES: Mapping[str, Title] = {
    "spyro2": Title("Spyro 2 (SCUS_944.25)", "scratch/assets/spyro2/SCUS_944.25", "PSXPORT_SPYRO2_DISC"),
    "spyro3": Title("Spyro 3 (SCUS_944.67)", "scratch/assets/spyro3/SCUS_944.67", "PSXPORT_SPYRO3_DISC"),
}


class Refusal(RuntimeError):
    """The requested run cannot be performed honestly."""


Runner = Callable[..., subprocess.CompletedProcess]


def boot_environment(
    title: Title, disc: str, fallback_limit: int | None, frames: int | None = None
) -> dict[str, str]:
    env = drive.environment(None)
    env.pop("PSXPORT_REPL", None)  # a boot run reads no commands; the REPL would wait for stdin
    env[title.disc_variable] = disc
    env["PSXPORT_DEBUG"] = ",".join(filter(None, (env.get("PSXPORT_DEBUG"), BOOT_CHANNELS)))
    if fallback_limit is not None:
        env["PSXPORT_LIGHTREC_FALLBACK_BLOCK_LIMIT"] = str(fallback_limit)
    if frames is not None:
        env["PSXPORT_NATIVE_FRAMES"] = str(frames)
    return env


def resolve_inputs(title: Title, executable: Path, root: Path, disc: str | None) -> tuple[Path, Path]:
    if not executable.is_file():
        raise Refusal(f"the built port is missing: {executable} — build target spyro_port first")
    image = root / title.image
    if not image.is_file():
        raise Refusal(f"{title.label} is not provisioned: {image} is missing — run tools/provision_title.py")
    if not disc:
        raise Refusal(f"no disc for {title.label}: set {title.disc_variable} (environment or .env)")
    return executable, image


def run_boot(
    title: Title,
    executable: Path,
    image: Path,
    env: Mapping[str, str],
    log: Path,
    timeout: int,
    runner: Runner = subprocess.run,
) -> str:
    """Launch the port once and return its combined output, also written to `log`. The child is reaped by
    `subprocess.run`'s own timeout handling, never by name."""
    log.parent.mkdir(parents=True, exist_ok=True)
    try:
        completed = runner(
            [str(executable), str(image)],
            env=dict(env),
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
            check=False,
        )
        output = completed.stdout.decode("utf-8", "replace")
    except subprocess.TimeoutExpired as expired:
        output = (expired.stdout or b"").decode("utf-8", "replace")
        log.write_text(output)
        raise Refusal(f"{title.label} did not end within {timeout}s; the partial log is {log}") from expired
    log.write_text(output)
    return output


def main(argv: Sequence[str] | None = None, *, runner: Runner = subprocess.run, root: Path = ROOT) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--title", choices=sorted(TITLES))
    parser.add_argument("--executable", type=Path, default=root / "build/bin/spyro_port")
    parser.add_argument("--fallback-limit", type=int, default=None)
    parser.add_argument(
        "--frames",
        type=int,
        default=None,
        help="stop after this many delivered fields (PSXPORT_NATIVE_FRAMES): a run length, reported as a cap",
    )
    parser.add_argument("--timeout", type=int, default=400)
    parser.add_argument("--require-cd-match", action="store_true")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args(argv)
    if args.selftest:
        return selftest()
    if args.title is None:
        parser.error("--title is required")
    title = TITLES[args.title]
    try:
        executable, image = resolve_inputs(title, args.executable, root, drive.disc_path(title.disc_variable))
        env = boot_environment(title, drive.disc_path(title.disc_variable) or "", args.fallback_limit, args.frames)
        log = root / "scratch/boot" / f"{args.title}.log"
        output = run_boot(title, executable, image, env, log, args.timeout, runner)
    except Refusal as refusal:
        print(f"boot_run: REFUSED: {refusal}", file=sys.stderr)
        return EXIT_REFUSED
    report = boot_log.parse(output)
    print(boot_log.render(report, title=title.label, fallback_budget=args.fallback_limit))
    print(f"  log: {log}")
    if not report.end_named:
        return EXIT_UNREADABLE_OR_MISMATCH
    if args.require_cd_match and not report.cd_matches:
        return EXIT_UNREADABLE_OR_MISMATCH
    return EXIT_OK


# ---- selftest ------------------------------------------------------------------------------------------
# Every case is one the tool must answer DIFFERENTLY from its neighbour: a run that matched and one that
# did not, a run that completed and one that aborted, and each refusal. A tool that printed the same
# report for all of them would pass a selftest that only asked it to run.

_SPYRO3_BASELINE = """\
[t] [cd] CdRead 1 sector(s) x 2048 bytes from LBA 500 -> 0x8006D264 (mode 0x80)
[t] [frameloop:error] Spyro 3's boot prefix ran 480 step(s) without returning and delivered 483 field(s) in total (step bound 480); it is polling rather than waiting -- ending the run at resume 0x800504F0
[t] [runtime] run complete: fields=484 product_steps=481 presentation_fences=481 translated_blocks=429 executed_blocks=23373822 executed_instructions=135585657 cache_hits=1 cache_misses=2 host_dispatches=506 invalidations=9 faults=0
[t] [executor] Lightrec fallback telemetry [run-complete]: executor_calls=966 executed_blocks=1 fallback_blocks=0 fallback_instructions=0 reasons{compilation_failed=0,self_modifying_code=0} refused_fallback_blocks=0 refused_reasons{compilation_failed=0,self_modifying_code=0}
"""

_SPYRO2_STOPPED = """\
[t] [cdirq] CD interrupt armed: I_MASK 0x009 -> 0x00D
[t] [cd] CdRead 1 sector(s) x 2048 bytes from LBA 500 -> 0x8006D264 (mode 0x80)
[t] [cdirq] stock CdRead of 1 sector(s) queued its data-ready completion (I_STAT=0x004)
[t] [cdirq] CD data-ready -> callback 0x8001379C (slot 0x800663B8, controller status 0x02, a0=2 a1=0) — 1 of 1 owed completions delivered
[t] [cd] CdRead 37 sector(s) x 2048 bytes from LBA 570 -> 0x8006D264 (mode 0x80)
[t] [cdirq] stock CdRead of 37 sector(s) queued its data-ready completion (I_STAT=0x004)
[t] [cdirq] CD data-ready -> callback 0x8001379C (slot 0x800663B8, controller status 0x02, a0=2 a1=0) — 2 of 2 owed completions delivered
[t] [frameloop:error] Spyro 2 stopped: phase=boot step=1 call 0x80011E9C active=0 turns=4 cycles=127234 exit=ambiguous code-image identity at guest pc=0x80077374 ra=0x80011F14 sp=0x801FFFE0 fields=3 presents=0
"""


_SPYRO3_CAPPED = """\
[t] [cd] CdRead 27 sector(s) x 2048 bytes from LBA 570 -> 0x800742D0 (mode 0x80)
[t] [cd] stock CdRead of 27 sector(s) from LBA 570 published as image 3 generation 3: 0x800742D0..0x80081AD0, SHA-256 67504482
[t] [cdirq] stock CdRead of 27 sector(s) queued its data-ready completion (I_STAT=0x004)
[t] [cdirq] CD data-ready -> callback 0x80050504 (slot 0x8006B3D0, controller status 0x02, a0=2 a1=0) -- 1 of 1 owed completions delivered
[t] [boot-native] Spyro 3's retail boot prefix returned after 336 step(s) and 546 field(s); the per-frame update now owns each step
[t] [runtime] run complete: fields=600 product_steps=400 presentation_fences=400 translated_blocks=9 executed_blocks=9 executed_instructions=9 cache_hits=1 cache_misses=2 host_dispatches=3 invalidations=4 faults=0
"""

_SPYRO2_FIELD_BOUND = """\
[t] [frameloop:error] Spyro 2's boot prefix delivered 2 field(s) in one call without returning and gave up on the guest at resume 0x800772FC (bound 64); ending the run
[t] [runtime] run complete: fields=65 product_steps=16 presentation_fences=16 translated_blocks=9 executed_blocks=9 executed_instructions=9 cache_hits=1 cache_misses=2 host_dispatches=3 invalidations=4 faults=0
"""


def selftest() -> int:
    failures: list[str] = []
    checked = 0

    def check(name: str, condition: bool) -> None:
        nonlocal checked
        checked += 1
        if not condition:
            failures.append(name)

    done = boot_log.parse(_SPYRO3_BASELINE)
    check("completed run is a named resume", done.end_kind == "resume" and done.end_pc == "0x800504F0")
    check("completed run reads its totals", (done.fields, done.faults, done.fallback_blocks) == (484, 0, 0))
    check("one read with no delivery is a MISMATCH", len(done.reads) == 1 and not done.cd_matches)

    stopped = boot_log.parse(_SPYRO2_STOPPED)
    check("aborted run is a named stop", stopped.end_kind == "stop" and stopped.end_pc == "0x80077374")
    check("aborted run has NO executed-block total, not a zero", stopped.executed_blocks is None and stopped.faults is None)
    check("two reads, two queued, two delivered is a MATCH", len(stopped.reads) == 2 and stopped.cd_matches)
    check("the stop's own counts are read", (stopped.fields, stopped.product_steps) == (3, 1))
    rendered = boot_log.render(stopped, title="t", fallback_budget=5)
    check("an unmeasured total prints as NOT MEASURED", "faults:                  NOT MEASURED" in rendered)
    check("a raised budget is named", "RAISED to 5" in rendered)

    capped = boot_log.parse(_SPYRO3_CAPPED)
    check("a completed run with no stop is a CAP, not a stop", capped.end_kind == "cap" and capped.faults == 0)
    check("the boot prefix's return is read", capped.boot_returned == (336, 546))
    check("a published read is counted with its generation", capped.published == [(570, 27, 3)])
    check("a stop never reads as published", stopped.published == [] and stopped.boot_returned is None)
    check("an unpublished run says so", "NONE seen" in boot_log.render(stopped, title="t", fallback_budget=None))
    check("a capped run renders its cap and its publication", "PRODUCT-STEP CAP" in boot_log.render(capped, title="t", fallback_budget=None))
    bound = boot_log.parse(_SPYRO2_FIELD_BOUND)
    check("a field-bound end is a named resume with its bound", (bound.end_kind, bound.end_pc) == ("resume", "0x800772FC") and "64" in bound.end_detail)

    unreadable = boot_log.parse("nothing the port ever prints\n")
    check("an unreadable log names no end", not unreadable.end_named)
    check("an unreadable log has no CD verdict", "NO READS SEEN" in boot_log.render(unreadable, title="t", fallback_budget=None))

    root = Path("/nonexistent-boot-run-selftest")
    try:
        resolve_inputs(TITLES["spyro2"], root / "no-port", root, "disc")
        check("a missing port refuses", False)
    except Refusal as refusal:
        check("a missing port is refused by name", "spyro_port" in str(refusal))
    status = main(["--title", "spyro2", "--executable", str(root / "no-port")], root=root)
    check("main exits 2 on a refusal", status == EXIT_REFUSED)

    if failures:
        print("boot_run selftest FAILED: " + "; ".join(failures), file=sys.stderr)
        return 1
    print(f"boot_run selftest: {checked} checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
