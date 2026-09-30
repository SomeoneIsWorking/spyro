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
import tempfile
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


def scene(name: str) -> tuple[str, ...]:
    """A scene route: drive.py's own scene driver, and where it writes what it proved.

    The proof file is not a convenience. A scene route that never reached its state writes NO file,
    so the corpus can require one and fail by name instead of reading a log sentence and hoping the
    run got there.
    """
    return ("tools/drive.py", "gameplay", "--scene", name, "--scene-proof",
            f"{{out}}/{name}.proof.json")


SCENES = ("flight-level", "boss-level", "death-respawn", "save-fairy")
# What each of those scenes is, for the gap line. The names are the ROUTE names, so a reader can
# match the line to `--routes`. The gap itself is COMPUTED from the proofs the routes wrote, not
# from this table: a scene with no route and a scene whose route failed look identical in a hand-
# kept list, and telling those apart is the whole reason a scene writes a proof file.
SCENE_DESCRIPTIONS = {
    "flight-level": "a flight level",
    "boss-level": "a boss fight",
    "death-respawn": "death and continue",
    "save-fairy": "the in-game save screen",
}

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
    Route("flight-level", "Sunny Flight, a flight level, through its homeworld portal",
          scene("flight-level")),
    Route("boss-level", "Toasty, a boss level, through its homeworld portal",
          scene("boss-level")),
    Route("death-respawn", "Spyro walked off the island into the guest's own death plane",
          scene("death-respawn")),
    Route("save-fairy", "the fairy's in-game save, through to the memory-card write",
          scene("save-fairy")),
)
# Overrides whose ORIGINAL body cannot run inside the product, so the differential cannot shadow them.
# Measured 2026-09-29: arming any one of these alone aborts boot at libetc VSync 0x8005DBC4, which the
# product keeps fatal (CLAUDE.md), because the original CD driver polls it for its timeouts.
UNSHADOWABLE = {
    "cd_loader": "original polls libetc VSync (fatal in the product)",
    "cd_retry_step": "original polls libetc VSync (fatal in the product)",
    "cd_stream_read": "original polls libetc VSync (fatal in the product)",
}
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


def route_named(name: str) -> Route:
    for route in ROUTES:
        if route.name == name:
            return route
    raise ValueError(
        f"unknown route {name!r}; routes: {', '.join(r.name for r in ROUTES)}"
    )


def route_invocation(
    route: Route, executable: str, log: Path, env_flags: dict[str, str], out: Path
) -> tuple[list[str], dict[str, str] | None]:
    """The argv and environment that run `route` with `env_flags` reaching the product.

    drive.py forwards product variables with --env; demo_run.py passes its own environment through.
    `{out}` in a route command is where that route's own artefacts go, so a scene route's proof
    lands beside the reports it is judged with instead of in a fixed name that two routes would
    share.
    """
    command = [part.replace("{out}", str(out)) for part in route.command]
    command += ["--executable", executable, "--log", str(log)]
    if route.command[0] == "tools/drive.py":
        for key, value in env_flags.items():
            command += ["--env", f"{key}={value}"]
        return command, None
    return command, {**os.environ, **env_flags}


def run_route(
    route: Route, out: Path, overrides: dict[str, int], executable: str
) -> int:
    reach, diff = out / f"{route.name}.reach.json", out / f"{route.name}.diff.json"
    for stale in (reach, diff, out / f"{route.name}.proof.json"):
        stale.unlink(missing_ok=True)
    command, environment = route_invocation(
        route,
        executable,
        out / f"{route.name}.log",
        {
            "PSXPORT_REACH_REPORT": str(reach),
            "PSXPORT_OVERRIDE_DIFF": ",".join(
                sorted(set(overrides) - set(UNSHADOWABLE))
            ),
            "PSXPORT_OVERRIDE_DIFF_REPORT": str(diff),
        },
        out,
    )
    with open(out / f"{route.name}.out", "w") as sink:
        code = subprocess.run(
            ["uv", "run", "--frozen", "python", *command],
            cwd=ROOT,
            stdout=sink,
            stderr=subprocess.STDOUT,
            env=environment,
            check=False,
        ).returncode
    return code


def read_proof(out: Path, name: str) -> dict | None:
    """What a scene route proved, or None when it proved nothing.

    The file is written by the scene only after it reached its target (tools/drive.py), so its
    ABSENCE is the failure signal and there is no half-written state to interpret. A file that is
    present but unreadable, or that names a different scene, is a refusal rather than a pass: a
    stale proof from a previous run is exactly the kind of clean-looking artefact that survives a
    route that stopped working.
    """
    path = out / f"{name}.proof.json"
    if not path.exists():
        return None
    try:
        proof = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as refusal:
        raise ValueError(f"{path.name} is present but unreadable: {refusal}") from refusal
    if proof.get("scene") != name or not proof.get("target"):
        raise ValueError(
            f"{path.name} names scene {proof.get('scene')!r} and target {proof.get('target')!r}, "
            f"not {name!r}"
        )
    return proof


def summarize(
    routes: list[Route], out: Path, entries: set[int], overrides: dict[str, int]
) -> int:
    failures = 0
    union: set[int] = set()
    reached_each: dict[str, set[int]] = {}
    proofs: dict[str, dict] = {}
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
        reached_each[route.name] = reached
        for address in pcs & set(by_address):
            reached_by_owned[by_address[address]].append(route.name)
        if diff_path.exists():
            for key in json.loads(diff_path.read_text())["keys"]:
                if key["name"] in samples:
                    tally = samples[key["name"]]
                    tally[0] += key["sampled"]
                    tally[1] += key["match"]
                    tally[2] += key["mismatch"]
        overlays = overlay_images(report)
        try:
            proof = read_proof(out, route.name)
        except ValueError as refusal:
            print(f"FAIL {route.name}: {refusal}")
            failures += 1
            proof = None
        if proof is not None:
            proofs[route.name] = proof
        print(
            f"{route.name:18} {len(reached):5}/{len(entries)} functions   overlays: {len(overlays)} image(s), "
            f"{sum(overlays.values())} distinct entry pc(s), no denominator   [{route.scene}]"
            f"{' (partial: last flush before the timeout kill)' if partial else ''}"
        )
        if proof is not None:
            print(
                f"{'':18} proved in {proof['frames']} field(s): {proof['target']}"
                + (f"  [{proof['detail']}]" if proof.get("detail") else "")
            )
    # What each route contributes that NOTHING ELSE in this run reaches. A union is a sum, so
    # without this a new route that only re-walks the hub looks like the same coverage.
    for route in routes:
        others: set[int] = set()
        for name, reached in reached_each.items():
            if name != route.name:
                others |= reached
        added = reached_each.get(route.name, set()) - others
        verdict = f"+{len(added)} function(s) no other route in this run reaches"
        if route.name not in proofs and route.name in SCENES:
            verdict = "NO PROOF FILE: the scene did not reach its target"
            failures += 1
        print(f"{route.name:18} {verdict}")
    print(
        f"{'UNION':18} {len(union):5}/{len(entries)} functions ({100.0 * len(union) / len(entries):.1f}%)"
    )
    for name in SCENES:
        if name in proofs:
            print(f"covered scene: {name} — {proofs[name]['target']}")
        else:
            print(f"MISSING route: {name} — {SCENE_DESCRIPTIONS[name]}")
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


def _selftest_proof_reader(directory: Path) -> int:
    """The proof reader, on every answer it can give.

    Three, and the third is the one that matters: absent (the failure signal), present, and a file
    left behind by a different scene. Without the last two, a proof from an earlier corpus run is
    indistinguishable from this run's, which is the whole reason the proof is a file rather than a
    sentence in a log.
    """
    failures = 0
    if read_proof(directory, "boss-level") is not None:
        failures += 1
        print("FAIL read_proof invented an absent proof")
    (directory / "boss-level.proof.json").write_text(
        json.dumps({"scene": "boss-level", "target": "g_LevelId=14", "frames": 4200})
    )
    if (read_proof(directory, "boss-level") or {}).get("frames") != 4200:
        failures += 1
        print("FAIL read_proof did not read a present proof")
    for label, text in (
        ("a proof left by another scene",
         json.dumps({"scene": "flight-level", "target": "g_IsFlightLevel=1", "frames": 1})),
        ("an unreadable proof", "{ not json"),
    ):
        (directory / "boss-level.proof.json").write_text(text)
        try:
            read_proof(directory, "boss-level")
        except ValueError as refusal:
            print(f"  refuses {label}: {refusal}")
        else:
            failures += 1
            print(f"FAIL read_proof accepted {label}")
    (directory / "boss-level.proof.json").unlink()
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
    # The scene routes the corpus judges on, and the route table that names them. A scene with no
    # ROUTE and a scene whose route failed are otherwise the same MISSING line.
    if set(SCENES) != set(SCENE_DESCRIPTIONS):
        failures += 1
        print("FAIL the scene list and the scene descriptions disagree")
    if sorted(name for name, r in ((r.name, r) for r in ROUTES) if r.name in SCENES) != sorted(SCENES):
        failures += 1
        print("FAIL a scene has no route in ROUTES")
    for route in ROUTES:
        if route.name in SCENES and "{out}" not in " ".join(route.command):
            failures += 1
            print(f"FAIL scene route {route.name} does not say where to write its proof")
    with tempfile.TemporaryDirectory(prefix="reach-corpus-selftest") as scratch:
        directory = Path(scratch)
        failures += _selftest_proof_reader(directory)
    print(f"selftest: {5 - failures} of 5 cases")
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
