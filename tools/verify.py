#!/usr/bin/env python3
"""Run Spyro's authoritative Clang build, complete CTest suite, and framework pin gate."""

from __future__ import annotations

import argparse
import os
import sys

import run


def verify(jobs: int) -> None:
    cc, cxx = run.preflight()
    psxport = run.sync_framework()
    run.sync_submodules(psxport)
    run.configure(run.ROOT, run.BUILD, cc, cxx, f"-DPSXPORT_DIR={psxport}")
    run.verify_clang_build(run.BUILD, "authoritative Spyro")
    run.command(["cmake", "--build", run.BUILD, "-j", str(jobs)])
    run.command(["ctest", "--test-dir", run.BUILD, "--output-on-failure"])
    run.command([sys.executable, run.ROOT / "tools/psxport_sync.py", "--check"])


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=max(1, os.cpu_count() or 1))
    args = parser.parse_args(argv)
    if args.jobs < 1:
        parser.error("--jobs must be at least 1")
    try:
        verify(args.jobs)
    except (OSError, run.Refusal) as error:
        print(f"[verify] REFUSED: {error}", file=sys.stderr)
        return 2
    print("[verify] PASS: Clang build, complete CTest, and psxport pin")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
