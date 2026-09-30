#!/usr/bin/env python3
"""Gate one new native override: scoped change, clean code, green tests, and a differential match.

This is the acceptance gate for an automated (swarm) override job, and it runs inside that job's git
worktree. A change passes only when all of these hold, in this order:

  1. SCOPE. The worktree differs from HEAD, and only under game/, titles/, tests/ or CMakeLists.txt.
  2. BUILD. The product and its tests build with Clang against the workspace's psxport.
  3. QUALITY. Every changed C++ file is clang-format clean, under the 1,200-line structure limit, and
     clang-tidy clean; the repository source policy passes; every guest address constant in a changed
     override module is one the retail code computes (tools/override_constants.py).
  4. TESTS (skipped by --light). The CTest suite passes, except the `slow` label: those drive the console oracle, whose
     activity lock admits one run per machine, so concurrent job gates would refuse each other. The
     operator's tools/verify.py runs the whole suite once on the combined tree before landing.
  5. BEHAVIOUR. A headless run of one reach-corpus route (--route, default artisans-walk) with
     PSXPORT_OVERRIDE_DIFF armed for the named override samples at least one comparable call, and every sampled call matches the guest
     body it replaces (psxport tools/port/override_differential_gate.py).

The gate takes no machine-wide slot of its own: its caller admits the whole gate once, the way a swarm
job with heavy_gate does, or `heavy.py --kind build -- <this gate>` by hand. A nested admission from
inside an admitted gate would queue behind requests waiting on the gate's own reservation. Its
peak is the -j 4 build plus one route run (a 300-frame drive peaks near 190 MB with the differential
armed, and psxport issue 0141 caps one shadowed call's journals at 32 MiB).

Usage (from the job worktree):
    uv run --frozen python tools/native_override_gate.py <override-name>... [--route NAME] [--light]
    uv run --frozen python tools/native_override_gate.py --selftest
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

from reach_corpus import registered_overrides, route_invocation, route_named

ROOT = Path(__file__).resolve().parent.parent
ALLOWED_ROOTS = ("game/", "titles/", "tests/")
ALLOWED_FILES = ("CMakeLists.txt",)
CPP_SUFFIXES = (".cpp", ".h")
LINE_LIMIT = 1200
BUILD = (
    ROOT / "build"
)  # the maintainer build dir, which some probe selftests locate by name
REPORT = ROOT / "scratch/override-gate/differential.json"
CCACHE = shutil.which("ccache") is not None
# A route takes minutes (the walk reaches gameplay at frame ~6,360 in about 15 s unpaced; the demo
# route runs 420 s). Past this, the only explanation is an override that never returns. coreutils
# timeout enforces it and signals the route's whole process group.
ROUTE_TIMEOUT_SECONDS = 900
# coreutils timeout's exit status when the deadline fired (124) or its --kill-after SIGKILL did (137).
DEADLINE_EXIT_CODES = (124, 137)


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
    # Every job builds in its own fresh worktree; relative-path ccache keys let them share objects.
    env["CCACHE_BASEDIR"] = str(ROOT)
    env["CCACHE_NOHASHDIR"] = "1"
    return env


def step(
    name: str,
    args: list[object],
    env: dict[str, str] | None = None,
    hang_codes: tuple[int, ...] = (),
) -> None:
    """Run one gate step; an exit status in `hang_codes` means the step's own deadline fired.

    The step stays in the caller's process group, so a swarm's group kill of a timed-out worker
    still reaches it.
    """
    print(f"[gate] {name}: {' '.join(map(str, args))}", flush=True)
    code = subprocess.run(
        [str(a) for a in args], cwd=ROOT, env=env, check=False
    ).returncode
    if code in hang_codes:
        raise GateFailure(
            f"{name} exceeded {ROUTE_TIMEOUT_SECONDS} s after admission; a native override that never returns hangs its route"
        )
    if code:
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


def build(framework: Path, env: dict[str, str], light: bool) -> None:
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
            *(
                [
                    "-DCMAKE_C_COMPILER_LAUNCHER=ccache",
                    "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache",
                ]
                if CCACHE
                else []
            ),
        ],
        env,
    )
    targets = ["--target", "spyro_port"] if light else []
    step("build", ["cmake", "--build", BUILD, "-j", "4", *targets], env)


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
    modules = [
        p for p in present if p.suffix == ".cpp" and p.name.startswith("native_")
    ]
    if modules:
        step(
            "override constants",
            [sys.executable, ROOT / "tools/override_constants.py", *modules],
            env,
        )


def read_dotenv(path: Path) -> dict[str, str]:
    if not path.is_file():
        return {}
    lines = path.read_text().splitlines()
    pairs = (
        line.split("=", 1) for line in lines if "=" in line and not line.startswith("#")
    )
    return {key.strip(): value.strip() for key, value in pairs}


def run_differential(
    names: list[str],
    route_name: str,
    framework: Path,
    checkout: Path,
    base_env: dict[str, str],
) -> None:
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.unlink(missing_ok=True)
    env = dict(base_env)
    env.update(read_dotenv(checkout / ".env"))
    route = route_named(route_name)
    command, route_env = route_invocation(
        route,
        str(BUILD / "bin/spyro_port"),
        ROOT / "scratch/override-gate/route.log",
        {
            "PSXPORT_OVERRIDE_DIFF": ",".join(names),
            "PSXPORT_OVERRIDE_DIFF_REPORT": str(REPORT),
            # Every call, not the first 16 and every 64th: a rare branch hides between samples
            # (docs/issues/0148 passed 17/17 sampled and mismatched 8 of 395 when all were shadowed).
            "PSXPORT_OVERRIDE_DIFF_EVERY": "1",
        },
    )
    step(
        f"{route_name} run",
        [
            "timeout",
            "--kill-after=10",
            str(ROUTE_TIMEOUT_SECONDS),
            sys.executable,
            *(ROOT / part if part.startswith("tools/") else part for part in command),
            "--binary",
            checkout / "scratch/assets/spyro1/SCUS_942.28",
        ],
        env={**env, **(route_env or {})},
        hang_codes=DEADLINE_EXIT_CODES,
    )
    step(
        "override differential",
        [
            sys.executable,
            framework / "tools/port/override_differential_gate.py",
            REPORT,
            *(flag for name in names for flag in ("--require", name)),
            # A route that ends by a timeout kill leaves the report's last periodic flush.
            *(["--allow-incomplete"] if route.ends_by_timeout else []),
        ],
        base_env,
    )


def names_for_addresses(addresses: str) -> list[str]:
    """The names this worktree registers for each requested entry address; every one must be registered."""
    wanted = [int(a, 16) for a in addresses.split(",") if a]
    registered = {address: name for name, address in registered_overrides(ROOT).items()}
    missing = [f"0x{a:08X}" for a in wanted if a not in registered]
    if missing:
        raise GateFailure(
            f"registration: no installNativeOverride for {', '.join(missing)}"
        )
    return [registered[a] for a in wanted]


def gate(names: list[str], route: str, light: bool) -> None:
    try:
        route_named(route)
    except ValueError as unknown:
        raise GateFailure(f"route: {unknown}") from unknown
    checkout = main_checkout()
    framework = (checkout / "external/psxport").resolve()
    if not (framework / "cmake/psxport.cmake").is_file():
        raise GateFailure(f"framework: {framework} is not a psxport checkout")
    cpp = check_scope()
    link_provisioned_inputs(checkout)
    populate_submodules(checkout)
    env = gate_environment(framework)
    build(framework, env, light)
    check_quality(cpp, env)
    if not light:
        step(
            "ctest",
            [
                "ctest",
                "--test-dir",
                BUILD,
                "-j",
                "6",
                "--output-on-failure",
                "-LE",
                "slow",
            ],
            env,
        )
    run_differential(
        names=names,
        route_name=route,
        framework=framework,
        checkout=checkout,
        base_env=env,
    )
    print(f"[gate] PASS: {', '.join(names)}")


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
        "names", nargs="*", help="the registered override name(s) the job added"
    )
    parser.add_argument(
        "--addresses",
        default="",
        help="comma-separated guest entry addresses the job must override; their registered names are "
        "read from the worktree, so the worker names them",
    )
    parser.add_argument(
        "--route",
        default="artisans-walk",
        help="the reach-corpus route (tools/reach_corpus.py) that calls the override",
    )
    parser.add_argument(
        "--light",
        action="store_true",
        help="build only the product and skip CTest; for per-job swarm gates, whose applied batch then "
        "passes tools/verify.py once",
    )
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    try:
        names = args.names + names_for_addresses(args.addresses)
        if not names:
            parser.error("at least one override name or --addresses is required")
        gate(names, args.route, args.light)
    except GateFailure as failure:
        print(f"[gate] REJECTED: {failure}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
