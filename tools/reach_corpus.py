#!/usr/bin/env python3
"""Run Spyro 1's headless route corpus and report which guest functions it reaches and gates.

WHY THIS EXISTS. Issue 0147 chose what to own natively by what ONE gameplay route reached, and the
override differential gated each owned function on that same route. A function the route never calls
is neither a candidate nor gated, so a single route narrows ownership and verification together. This
runs every route in ROUTES with two instruments armed:

  * psxport's function-reach recorder (PSXPORT_REACH_REPORT, runtime/cpu/function_reach.h), which
    records every guest pc the dynarec dispatches, keyed by code image;
  * the override differential (PSXPORT_OVERRIDE_DIFF) for EVERY registered native override.

Coverage is stated as reached/total over the retail executable's statically called functions (every
`jal` target inside SCUS_942.28's text), per route and for the union. WAD overlay images have no
function list here, so their distinct reached entries are counted without a denominator, and the report
says so. Scenes no route reaches are printed as MISSING rather than left out.

A route whose reach report is absent, incomplete, or names no main-executable function FAILS: an empty
report is the recorder not running, not the route reaching nothing.

Usage:
    uv run --frozen python tools/reach_corpus.py [--routes NAME,...] [--out scratch/reach]
    uv run --frozen python tools/reach_corpus.py --selftest
"""

from __future__ import annotations

import argparse
import json
import os
import re
import struct
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "scratch/assets/spyro1/SCUS_942.28"
EXE_IMAGE_TEXT_OFFSET = 0x800
OVERRIDE_SOURCES = ("game/core", "titles/spyro1/core")
REGISTRATION = re.compile(
    r'installNativeOverride\(\s*core,\s*0x([0-9A-Fa-f]{8})u,\s*"([^"]+)"'
)


@dataclass(frozen=True)
class Route:
    name: str
    scene: str
    command: tuple[str, ...]
    # The route ends by its driver killing the product at a timeout, so its reach report is the
    # recorder's last periodic flush (complete=false), missing at most its final few entries.
    ends_by_timeout: bool = False


DRIVE = ("tools/drive.py", "gameplay")
ROUTES = (
    Route(
        "attract-demo",
        "boot, logos, title, the attract flyby and the self-playing demo",
        ("tools/demo_run.py", "--timeout", "420"),
        ends_by_timeout=True,
    ),
    Route(
        "artisans-walk",
        "Artisans homeworld, walking",
        DRIVE + ("--hold", "RIGHT", "--hold-frames", "300"),
    ),
    Route(
        "pause-menu",
        "the pause menu open in gameplay",
        DRIVE + ("--tap", "start", "--after", "120"),
    ),
    Route(
        "gem-seek", "walking to and collecting a gem", DRIVE + ("--seek-class", "83")
    ),
    Route(
        "portal-level",
        "through a homeworld portal into a level",
        DRIVE + ("--seek-portal",),
    ),
    Route(
        "skip-transitions",
        "Start on the level flyby and tally (the port's cancellation route)",
        DRIVE + ("--skip-transitions", "--seek-portal"),
    ),
)
# Overrides whose ORIGINAL body cannot run inside the product, so the differential cannot shadow them.
# Measured 2026-09-29: arming any one of these alone aborts boot at libetc VSync 0x8005DBC4, which the
# product keeps fatal (CLAUDE.md), because the original CD driver polls it for its timeouts.
UNSHADOWABLE = {
    "cd_loader": "original polls libetc VSync (fatal in the product)",
    "cd_retry_step": "original polls libetc VSync (fatal in the product)",
    "cd_stream_read": "original polls libetc VSync (fatal in the product)",
}
# Scenes the corpus must cover and no route reaches yet. Printed on every run so the gap stays visible.
MISSING_SCENES = (
    "a flight level",
    "a boss fight",
    "death and continue",
    "the save screen after a level",
)


def jal_targets(exe: bytes) -> set[int]:
    """Every `jal` target inside the PS-X EXE's own text: the statically called functions."""
    if exe[:8] != b"PS-X EXE":
        raise ValueError("not a PS-X EXE")
    text_addr, text_size = struct.unpack_from("<II", exe, 0x18)
    text = exe[EXE_IMAGE_TEXT_OFFSET : EXE_IMAGE_TEXT_OFFSET + text_size]
    begin, end = text_addr, text_addr + len(text)
    targets = set()
    for (word,) in struct.iter_unpack("<I", text[: len(text) & ~3]):
        if word >> 26 == 3:
            target = 0x80000000 | ((word & 0x03FFFFFF) << 2)
            if begin <= target < end:
                targets.add(target)
    return targets


def registered_overrides(root: Path) -> dict[str, int]:
    names: dict[str, int] = {}
    for directory in OVERRIDE_SOURCES:
        for source in sorted((root / directory).glob("*.cpp")):
            for address, name in REGISTRATION.findall(source.read_text()):
                names[name] = int(address, 16)
    return names


def main_image_pcs(report: dict) -> set[int]:
    """The reached pcs of every non-WAD image: the retail executable (and nothing else loads one)."""
    exe_images = [
        image for image in report["images"] if not image["name"].startswith("WAD ")
    ]
    return {int(pc, 16) for image in exe_images for pc in image["pcs"]}


def overlay_images(report: dict) -> dict[str, int]:
    return {
        image["name"]: len(image["pcs"])
        for image in report["images"]
        if image["name"].startswith("WAD ")
    }


def run_route(
    route: Route, out: Path, overrides: dict[str, int], executable: str
) -> int:
    reach, diff = out / f"{route.name}.reach.json", out / f"{route.name}.diff.json"
    for stale in (reach, diff):
        stale.unlink(missing_ok=True)
    command = [
        "uv",
        "run",
        "--frozen",
        "python",
        *route.command,
        "--executable",
        executable,
    ]
    env_flags = {
        "PSXPORT_REACH_REPORT": str(reach),
        "PSXPORT_OVERRIDE_DIFF": ",".join(sorted(set(overrides) - set(UNSHADOWABLE))),
        "PSXPORT_OVERRIDE_DIFF_REPORT": str(diff),
    }
    if route.command[0] == "tools/drive.py":
        command += ["--log", str(out / f"{route.name}.log")]
        for key, value in env_flags.items():
            command += ["--env", f"{key}={value}"]
        environment = None
    else:
        command += ["--log", str(out / f"{route.name}.log")]
        environment = {**os.environ, **env_flags}
    with open(out / f"{route.name}.out", "w") as sink:
        code = subprocess.run(
            command,
            cwd=ROOT,
            stdout=sink,
            stderr=subprocess.STDOUT,
            env=environment,
            check=False,
        ).returncode
    return code


def summarize(
    routes: list[Route], out: Path, entries: set[int], overrides: dict[str, int]
) -> int:
    failures = 0
    union: set[int] = set()
    samples: dict[str, list[int]] = {
        name: [0, 0, 0] for name in overrides
    }  # sampled, match, mismatch
    by_address = {address: name for name, address in overrides.items()}
    reached_by_owned: dict[str, list[str]] = {name: [] for name in overrides}
    print(
        f"denominator: {len(entries)} statically called functions in SCUS_942.28 (jal targets)"
    )
    for route in routes:
        reach_path, diff_path = (
            out / f"{route.name}.reach.json",
            out / f"{route.name}.diff.json",
        )
        if not reach_path.exists():
            print(f"FAIL {route.name}: no reach report (the recorder never armed)")
            failures += 1
            continue
        report = json.loads(reach_path.read_text())
        pcs = main_image_pcs(report)
        reached = pcs & entries
        partial = not report.get("complete")
        if (partial and not route.ends_by_timeout) or not reached:
            print(
                f"FAIL {route.name}: report complete={not partial}, {len(reached)} functions reached"
            )
            failures += 1
        union |= reached
        for address in pcs & set(by_address):
            reached_by_owned[by_address[address]].append(route.name)
        overlays = overlay_images(report)
        print(
            f"{route.name:18} {len(reached):5}/{len(entries)} functions   overlays: {len(overlays)} image(s), "
            f"{sum(overlays.values())} distinct entry pc(s), no denominator   [{route.scene}]{' (partial: last flush before the timeout kill)' if partial else ''}"
        )
        if diff_path.exists():
            for key in json.loads(diff_path.read_text())["keys"]:
                if key["name"] in samples:
                    tally = samples[key["name"]]
                    tally[0] += key["sampled"]
                    tally[1] += key["match"]
                    tally[2] += key["mismatch"]
    print(
        f"{'UNION':18} {len(union):5}/{len(entries)} functions ({100.0 * len(union) / len(entries):.1f}%)"
    )
    for scene in MISSING_SCENES:
        print(f"MISSING route: {scene}")
    print(f"owned overrides: {len(overrides)}")
    for name, (sampled, match, mismatch) in sorted(samples.items()):
        routes_hit = ",".join(reached_by_owned[name]) or "-"
        verdict = "MISMATCH" if mismatch else ("ungated" if sampled == 0 else "gated")
        if name in UNSHADOWABLE:
            verdict = "excluded"
            routes_hit += f"   ({UNSHADOWABLE[name]})"
        if mismatch:
            failures += 1
        print(
            f"  {verdict:8} {name:44} 0x{overrides[name]:08X} sampled {sampled:4} match {match:4} "
            f"mismatch {mismatch}   reached on: {routes_hit}"
        )
    return failures


def selftest() -> int:
    failures = 0
    header = bytearray(0x800)
    header[:8] = b"PS-X EXE"
    struct.pack_into("<II", header, 0x18, 0x80010000, 16)
    jal_in = (3 << 26) | ((0x80010008 & 0x0FFFFFFF) >> 2)
    jal_out = (3 << 26) | ((0x80200000 & 0x0FFFFFFF) >> 2)
    text = struct.pack("<IIII", jal_in, jal_out, 0x03E00008, 0)
    got = jal_targets(bytes(header) + text)
    if got != {0x80010008}:
        failures += 1
        print(f"FAIL jal_targets: {sorted(map(hex, got))}")
    try:
        jal_targets(b"ELF" + bytes(0x900))
        failures += 1
        print("FAIL jal_targets accepted a non-PS-X EXE")
    except ValueError:
        pass
    report = {
        "images": [
            {"name": "SCUS", "pcs": ["0x80010008"]},
            {"name": "WAD SHA-256 ab", "pcs": ["0x80070000"]},
        ]
    }
    if main_image_pcs(report) != {0x80010008} or overlay_images(report) != {
        "WAD SHA-256 ab": 1
    }:
        failures += 1
        print("FAIL image split")
    source = 'spyro::installNativeOverride(core, 0x80017700u, "copy3", copy3_native);'
    if REGISTRATION.findall(source) != [("80017700", "copy3")]:
        failures += 1
        print("FAIL registration parse")
    print(f"selftest: {4 - failures} of 4 cases")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--routes", default=",".join(route.name for route in ROUTES))
    parser.add_argument("--out", type=Path, default=ROOT / "scratch/reach")
    parser.add_argument(
        "--summarize-only",
        action="store_true",
        help="re-read existing reports, run nothing",
    )
    parser.add_argument("--executable", default="build/bin/spyro_port")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    wanted = args.routes.split(",")
    unknown = sorted(set(wanted) - {route.name for route in ROUTES})
    if unknown:
        parser.error(f"unknown route(s): {', '.join(unknown)}")
    routes = [route for route in ROUTES if route.name in wanted]
    if not EXE.exists():
        print(
            f"REFUSED: {EXE.relative_to(ROOT)} is not provisioned (tools/provision_title.py)",
            file=sys.stderr,
        )
        return 2
    entries = jal_targets(EXE.read_bytes())
    overrides = registered_overrides(ROOT)
    args.out.mkdir(parents=True, exist_ok=True)
    if not args.summarize_only:
        for route in routes:
            code = run_route(route, args.out, overrides, args.executable)
            print(f"[route] {route.name}: exit {code}", flush=True)
    return 1 if summarize(routes, args.out, entries, overrides) else 0


if __name__ == "__main__":
    raise SystemExit(main())
