#!/usr/bin/env python3
"""Run Spyro's authoritative Clang build, complete CTest suite, and framework pin gate."""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

import run


def verify_clang_build(build: Path) -> None:
    compiler_files = sorted((build / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake"))
    if not compiler_files or not any(
        'CMAKE_CXX_COMPILER_ID "Clang"' in path.read_text(errors="replace")
        for path in compiler_files
    ):
        raise run.Refusal(f"the configured maintainer build in {build} is not using Clang")


def verify_source_policy() -> None:
    """Run the asset-free source-policy gate used by hosted CI.

    The native runtime links the frozen psxport/Lightrec dependency, but this mode deliberately
    proves only source policy and its negative selftests; it never fabricates a guest executor or
    claims title gameplay coverage.
    """
    source_policy = run.ROOT / "tools/source_policy.py"
    run.command([sys.executable, source_policy, "--selftest"])
    run.command([sys.executable, source_policy])


def verify(jobs: int) -> None:
    compiler_options = run.preflight()
    psxport = run.sync_framework()
    run.sync_submodules(psxport)
    run.configure(
        run.ROOT,
        run.MAINTAINER_BUILD,
        compiler_options,
        f"-DPSXPORT_DIR={psxport}",
        build_testing=True,
    )
    verify_clang_build(run.MAINTAINER_BUILD)
    run.command(["cmake", "--build", run.MAINTAINER_BUILD, "-j", str(jobs)])
    run.command(
        ["ctest", "--test-dir", run.MAINTAINER_BUILD, "--output-on-failure"]
    )
    run.command([sys.executable, run.ROOT / "tools/psxport_sync.py", "--check"])


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=max(1, os.cpu_count() or 1))
    parser.add_argument(
        "--source-policy",
        action="store_true",
        help="run the asset-free source-policy gate without building psxport",
    )
    args = parser.parse_args(argv)
    if args.jobs < 1:
        parser.error("--jobs must be at least 1")
    try:
        verify_source_policy() if args.source_policy else verify(args.jobs)
    except (OSError, run.Refusal) as error:
        print(f"[verify] REFUSED: {error}", file=sys.stderr)
        return 2
    if args.source_policy:
        print("[verify] PASS: asset-free source policy")
    else:
        print("[verify] PASS: Clang build, complete CTest, and psxport pin")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
