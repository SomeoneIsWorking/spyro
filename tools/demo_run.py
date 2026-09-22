#!/usr/bin/env python3
"""Run the product with no input at all and let the attract demo play itself.

WHY THIS EXISTS. Issue 0128 is a user-reported crash before gameplay that nine agent observations
failed to reproduce. Every one of them used `tools/drive.py`, which takes its own route into
`GS_Playing` and stops there. The crash is not on that route: leaving the pad alone lets the title's
attract demo play itself into a crowded scene, and the scene layers refuse on arms nobody had
reached. The route costs about five minutes and needs no input, so the only reason it was never run
is that there was no command for it.

This is not a gate. It observes, and what it produces is a log and the frame the product reached.
Gate on CTest and the runtime counters, never on a play-through.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

import drive

ROOT = drive.ROOT


def report(log: Path, headline: str) -> None:
    """Print what the run reached, whether it stopped on its own or outlived its timeout.

    Reaching the end of the clock with nothing refused is the OUTCOME THIS ROUTE IS FOR, so it must
    not print like a tool failure. It did for one run: the product survived 900 seconds and 101,970
    drawn fields where it used to die at frame 21,034, and the only line on the terminal was
    `[demo] REFUSED: ... timed out`.
    """
    lines = log.read_text().splitlines() if log.is_file() else []
    reached = [line for line in lines if "[snap] frame" in line]
    refusals = [line for line in lines if "NOT IMPLEMENTED" in line]
    size = log.stat().st_size if log.is_file() else 0
    print(f"[demo] {headline} log={log} bytes={size} lines={len(lines)}")
    print(f"[demo] frame snapshots: {len(reached)}; native-render refusals: {len(refusals)}")
    for line in refusals[-1:] + reached[-1:]:
        print(f"[demo]   {line}")
    if not reached and not refusals:
        print("[demo] nothing refused and nothing snapshotted on this route")


def demo_run(executable: Path, binary: Path, log: Path, channels: str, timeout: int) -> int:
    """Launch the product headless, silent and unpaced, and wait for it to stop on its own."""
    if not executable.is_file():
        raise drive.Refusal(f"no product at {executable}: build it first")
    if not binary.is_file():
        raise drive.Refusal(
            f"no guest executable at {binary}: provision it with tools/provision_title.py"
        )
    environment = drive.environment(drive.disc_path())
    # The REPL is the one knob this run must not inherit from the driver: nothing is going to speak
    # to it, and a product waiting for a prompt would never start the demo.
    environment.pop("PSXPORT_REPL", None)
    environment["PSXPORT_LOG_FILE"] = str(log)
    if channels:
        environment["PSXPORT_DEBUG"] = channels
    log.parent.mkdir(parents=True, exist_ok=True)
    log.write_text("")
    try:
        completed = subprocess.run(
            [str(executable), str(binary)],
            cwd=ROOT,
            env=environment,
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired:
        report(log, f"outlived its {timeout}s clock — still running when it was killed;")
        return 0
    report(log, f"stopped on its own: exit={completed.returncode}")
    return completed.returncode


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--executable", default="build/bin/spyro_port")
    parser.add_argument("--binary", default="scratch/assets/spyro1/SCUS_942.28")
    parser.add_argument("--log", default="scratch/logs/demo_run.log")
    parser.add_argument("--debug", default="", help="PSXPORT_DEBUG channels for this run")
    parser.add_argument("--timeout", type=int, default=900, help="seconds before the run is killed")
    args = parser.parse_args(argv)
    try:
        return demo_run(
            ROOT / args.executable, ROOT / args.binary, ROOT / args.log, args.debug, args.timeout
        )
    except (OSError, drive.Refusal) as error:
        print(f"[demo] REFUSED: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
