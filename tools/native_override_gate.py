#!/usr/bin/env python3
"""Gate one new native override: scoped change, clean code, green tests, and a differential match.

This is the acceptance gate for an automated (swarm) override job, and it runs inside that job's git
worktree. A change passes only when all of these hold, in this order:

  1. SCOPE. The worktree differs from HEAD, and only under game/, titles/, tests/ or CMakeLists.txt.
  2. BUILD. The product and its tests build with Clang against the workspace's psxport.
  3. QUALITY. Every changed C++ file is clang-format clean, under the 1,200-line structure limit, and
     clang-tidy clean; the repository source policy passes; every guest address constant in a changed
     override module is one the retail code computes (tools/override_constants.py).
  4. TESTS. The CTest suite passes, except the `slow` label: those drive the console oracle, whose
     activity lock admits one run per machine, so concurrent job gates would refuse each other. The
     operator's tools/verify.py runs the whole suite once on the combined tree before landing.
  5. BEHAVIOUR. A headless gameplay run (tools/drive.py) with PSXPORT_OVERRIDE_DIFF armed for the
     named override samples at least one comparable call, and every sampled call matches the guest
     body it replaces (psxport tools/port/override_differential_gate.py).

Usage (from the job worktree):
    uv run --frozen python tools/native_override_gate.py <override-name>
    uv run --frozen python tools/native_override_gate.py --selftest
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ALLOWED_ROOTS = ("game/", "titles/", "tests/")
ALLOWED_FILES = ("CMakeLists.txt",)
CPP_SUFFIXES = (".cpp", ".h")
LINE_LIMIT = 1200
BUILD = (
    ROOT / "build"
)  # the maintainer build dir, which some probe selftests locate by name
REPORT = ROOT / "scratch/override-gate/differential.json"


class GateFailure(RuntimeError):
    """One named gate step refused the change."""


def main_checkout() -> Path:
    """The repository that owns this worktree, where the provisioned disc image and .env live."""
    common = subprocess.run(
        [
            "git",
            "-C",
            str(ROOT),
            "rev-parse",
            "--path-format=absolute",
            "--git-common-dir",
        ],
        capture_output=True,
        text=True,
        check=True,
    ).stdout.strip()
    return Path(common).parent


def changed_paths() -> list[str]:
    status = subprocess.run(
        ["git", "-C", str(ROOT), "status", "--porcelain", "--untracked-files=all"],
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    return sorted(
        line[3:].split(" -> ")[-1] for line in status.splitlines() if line.strip()
    )


def out_of_scope(paths: list[str]) -> list[str]:
    return [p for p in paths if not (p.startswith(ALLOWED_ROOTS) or p in ALLOWED_FILES)]


# Gitignored inputs the main checkout holds and a fresh job worktree does not: the framework link,
# the provisioned disc images, the .env that names them, and the BIOS the oracle tools find beside
# the repository root.
PROVISIONED_INPUTS = ("external/psxport", "scratch/assets", ".env", "../SCPH1001.BIN")


def link_provisioned_inputs(checkout: Path) -> None:
    """Give a job worktree the main checkout's provisioned inputs, so its tests see the same tree."""
    for relative in PROVISIONED_INPUTS:
        link = (
            (ROOT / relative).resolve()
            if relative.startswith("..")
            else ROOT / relative
        )
        source = checkout / relative
        if link.exists() or link.is_symlink():
            continue
        if not source.exists():
            raise GateFailure(f"provisioning: {source} is missing in the main checkout")
        link.parent.mkdir(parents=True, exist_ok=True)
        link.symlink_to(source.resolve())


def submodule_commits() -> dict[str, str]:
    """Each submodule path this tree records, with its gitlink commit."""
    listing = subprocess.run(
        ["git", "-C", str(ROOT), "ls-files", "--stage"],
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    commits = {}
    for line in listing.splitlines():
        meta, path = line.split("\t", 1)
        mode, commit, _ = meta.split()
        if mode == "160000":
            commits[path] = commit
    return commits


def populate_submodules(checkout: Path) -> None:
    """Check out each empty submodule at its gitlink, sharing the main checkout's objects (no fetch)."""
    for path, commit in submodule_commits().items():
        target = ROOT / path
        if any(target.iterdir()):
            continue
        target.rmdir()
        step(
            "submodule " + path,
            [
                "git",
                "clone",
                "--quiet",
                "--shared",
                "--no-checkout",
                checkout / path,
                target,
            ],
        )
        step(
            "submodule " + path,
            ["git", "-C", target, "checkout", "--quiet", "--detach", commit],
        )


def gate_environment(framework: Path) -> dict[str, str]:
    """This process's environment plus the shared checkouts found from the framework's REAL path.

    Repository tools locate shared/ by walking up from their own directory, which misses it from a
    nested worktree.
    """
    env = dict(os.environ)
    env["RE_HARNESS_DIR"] = str(framework.parent.parent / "shared/re-harness")
    return env


def step(name: str, args: list[object], env: dict[str, str] | None = None) -> None:
    print(f"[gate] {name}: {' '.join(map(str, args))}", flush=True)
    if subprocess.run(
        [str(a) for a in args], cwd=ROOT, env=env, check=False
    ).returncode:
        raise GateFailure(f"{name} failed")


def check_scope() -> list[Path]:
    paths = changed_paths()
    if not paths:
        raise GateFailure(
            "scope: the worktree is identical to HEAD; there is nothing to gate"
        )
    stray = out_of_scope(paths)
    if stray:
        allowed = ", ".join(ALLOWED_ROOTS + ALLOWED_FILES)
        raise GateFailure(f"scope: changes outside {allowed}: {', '.join(stray)}")
    print(f"[gate] scope: {len(paths)} changed path(s): {', '.join(paths)}")
    return [ROOT / p for p in paths if p.endswith(CPP_SUFFIXES)]


def build(framework: Path, env: dict[str, str]) -> None:
    step(
        "configure",
        [
            "cmake",
            "-S",
            ROOT,
            "-B",
            BUILD,
            "-G",
            "Ninja",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DBUILD_TESTING=ON",
            "-DCMAKE_C_COMPILER=clang",
            "-DCMAKE_CXX_COMPILER=clang++",
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
            f"-DPSXPORT_DIR={framework}",
            # The framework's own sibling, found from its REAL path: psxport's resolver walks up from
            # the symlinked external/psxport, which misses shared/ from a nested worktree.
            f"-DPSXPORT_LIGHTREC_DIR={framework.parent.parent / 'shared/lightrec'}",
            f"-DPython3_EXECUTABLE={sys.executable}",
        ],
        env,
    )
    step("build", ["cmake", "--build", BUILD, "-j", "6"], env)


def check_quality(cpp: list[Path], env: dict[str, str]) -> None:
    present = [p for p in cpp if p.is_file()]
    for path in present:
        lines = len(path.read_text().splitlines())
        if lines > LINE_LIMIT:
            relative = path.relative_to(ROOT)
            raise GateFailure(
                f"structure: {relative} has {lines} lines (limit {LINE_LIMIT})"
            )
    if present:
        step("format", [sys.executable, ROOT / "tools/format.py", "--check", *present])
        units = [p for p in present if p.suffix == ".cpp"]
        if units:
            step("clang-tidy", ["clang-tidy", "-p", BUILD, "--quiet", *units], env)
    step("source policy", [sys.executable, ROOT / "tools/source_policy.py"], env)
    modules = [p for p in present if p.suffix == ".cpp" and p.name.startswith("native_")]
    if modules:
        step("override constants", [sys.executable, ROOT / "tools/override_constants.py", *modules], env)


def read_dotenv(path: Path) -> dict[str, str]:
    if not path.is_file():
        return {}
    lines = path.read_text().splitlines()
    pairs = (
        line.split("=", 1) for line in lines if "=" in line and not line.startswith("#")
    )
    return {key.strip(): value.strip() for key, value in pairs}


def run_differential(
    name: str, framework: Path, checkout: Path, base_env: dict[str, str]
) -> None:
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.unlink(missing_ok=True)
    env = dict(base_env)
    env.update(read_dotenv(checkout / ".env"))
    step(
        "gameplay run",
        [
            "heavy.py",
            "--kind",
            "run",
            "--",
            sys.executable,
            ROOT / "tools/drive.py",
            "--executable",
            BUILD / "bin/spyro_port",
            "--binary",
            checkout / "scratch/assets/spyro1/SCUS_942.28",
            "--log",
            ROOT / "scratch/override-gate/drive.log",
            "--env",
            f"PSXPORT_OVERRIDE_DIFF={name}",
            "--env",
            f"PSXPORT_OVERRIDE_DIFF_REPORT={REPORT}",
            "--hold",
            "RIGHT",
            "--hold-frames",
            "300",
            "gameplay",
        ],
        env=env,
    )
    step(
        "override differential",
        [
            sys.executable,
            framework / "tools/port/override_differential_gate.py",
            REPORT,
            "--require",
            name,
        ],
        base_env,
    )


def gate(name: str) -> None:
    if shutil.which("heavy.py") is None:
        raise GateFailure(
            "heavy.py is not on PATH; the gameplay run must take a machine-wide run slot"
        )
    checkout = main_checkout()
    framework = (checkout / "external/psxport").resolve()
    if not (framework / "cmake/psxport.cmake").is_file():
        raise GateFailure(f"framework: {framework} is not a psxport checkout")
    cpp = check_scope()
    link_provisioned_inputs(checkout)
    populate_submodules(checkout)
    env = gate_environment(framework)
    build(framework, env)
    check_quality(cpp, env)
    step(
        "ctest",
        ["ctest", "--test-dir", BUILD, "-j", "6", "--output-on-failure", "-LE", "slow"],
        env,
    )
    run_differential(name, framework, checkout, env)
    print(f"[gate] PASS: {name}")


def selftest() -> int:
    cases = [
        (["game/core/native_x.cpp"], []),
        (["CMakeLists.txt", "tests/test_x.cpp"], []),
        (["tools/drive.py"], ["tools/drive.py"]),
        (["external/psxport/x.cpp", "titles/spyro1/a.h"], ["external/psxport/x.cpp"]),
        (["docs/codemap.md"], ["docs/codemap.md"]),
    ]
    failures = 0
    for paths, expected in cases:
        got = out_of_scope(paths)
        if got != expected:
            failures += 1
            print(f"FAIL scope {paths}: got {got}, want {expected}")
    print(f"selftest: {len(cases) - failures} of {len(cases)} scope cases")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "name", nargs="?", help="the registered override name the job added"
    )
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    if not args.name:
        parser.error("an override name is required")
    try:
        gate(args.name)
    except GateFailure as failure:
        print(f"[gate] REJECTED: {failure}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
