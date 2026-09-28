#!/usr/bin/env python3
"""Decompile named Spyro 1 guest functions to readable C, as a READING AND PORTING AID.

    uv run --frozen python tools/decomp.py --selftest
    uv run --frozen python tools/decomp.py plan
    uv run --frozen python tools/decomp.py run --role control
    uv run --frozen python tools/decomp.py run --role all --reanalyze

WHAT THIS IS, and what it is not. It recovers readable C for guest functions a person has named, so
a defect on their path can be debugged instead of guessed at. The C is NOT a shipping artifact: it
names absolute guest addresses, it does not compile, and it is written under the repository's
gitignored `scratch/`. What ships is the KNOWLEDGE -- a named struct, a named offset, a native
override that owns the recovered behaviour, and a test. This repository's execution contract
forbids an offline or install-time translation of guest code into objects, and a Ghidra project
holding a copyrighted image's decompiled bodies has no place in git either.

THE ORDER IS NOT OPTIONAL. `--role control` must pass before any new output is believed: the control
is a function whose bytes this repository already records, so a decompile that disagrees with the
record is a pipeline fault, and a pipeline fault makes every later answer worthless. `run` therefore
refuses a chain decompile that has not had a passing control in the same invocation.

Every run prints what it scanned, what it matched, and what it refused. A decompile that reports a
uniform count across its targets is telling you it is broken, not that the corpus is uniform.

DERIVED, NOT COMMITTED: `scratch/decomp/` holds the Ghidra project, the imported image, the
decompiled C, the disassembly and the manifest. None of it is tracked.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import decomp_ghidra  # noqa: E402
import decomp_image  # noqa: E402
import decomp_manifest  # noqa: E402
import decomp_targets  # noqa: E402

PROJECT_NAME = "spyro1_text"
SCRATCH = ROOT / "scratch" / "decomp"


def _targets(role: str) -> tuple:
    if role == "control":
        return decomp_targets.by_role("control")
    return decomp_targets.by_role(role)


def plan() -> int:
    """Print the work order and this repository's own control over the image, and change nothing."""
    scanned, agree, disagree = (0, 0, 0)
    try:
        image = decomp_image.ImageWindow.open()
        scanned, agree, disagree = decomp_image.control_agreement(image)
        print(f"[decomp] image {image.path.name}: {len(image.data)} byte(s), guest window "
              f"0x{decomp_image.WINDOW_FIRST:08X}..0x{decomp_image.WINDOW_FIRST + len(image.data) - decomp_image.TEXT_FILE_OFFSET:08X}, "
              f"Ghidra import base 0x{decomp_image.GHIDRA_IMPORT_BASE:08X}")
    except decomp_image.ImageRefusal as error:
        print(f"[decomp] REFUSED: {error}")
        return 2
    if scanned == 0:
        print("[decomp] REFUSED: 0 recorded instructions to check the offset formula against, so a "
              "wrong mapping could not be detected. Refusing to plan against an unverified map.")
        return 2
    print(f"[decomp] image control: {agree} of {scanned} recorded instructions agree with "
          f"file_offset = 0x{decomp_image.TEXT_FILE_OFFSET:X} + (addr - 0x{decomp_image.TEXT_LOAD_ADDRESS:08X}); "
          f"{disagree} disagree")
    if disagree:
        print("[decomp] REFUSED: the mapping disagrees with the record, so every address below is "
              "meaningless.")
        return 2
    targets = decomp_targets.by_role("all")
    print(f"[decomp] work order: {len(targets)} named target(s), "
          f"{len(decomp_targets.by_role('control'))} control and "
          f"{len(decomp_targets.by_role('chain'))} chain")
    for target in targets:
        anchors = " ".join("0x%08X" % value for value in target.anchors) or "(no anchor: layout "\
                                                                        "is the whole answer)"
        print(f"[decomp]   {target}  anchors: {anchors}")
        print(f"[decomp]     {target.reason}")
    print(f"[decomp] derived output goes to {SCRATCH.relative_to(ROOT)}/ (gitignored; the decompiled C "
          "is derived from a copyrighted image and is never committed)")
    return 0


def run(role: str, reanalyze: bool, timeout: int) -> int:
    """Import/analyze if needed, decompile the target set, then decide admissibility."""
    try:
        image = decomp_image.ImageWindow.open()
    except decomp_image.ImageRefusal as error:
        print(f"[decomp] REFUSED: {error}")
        return 2
    scanned, agree, disagree = decomp_image.control_agreement(image)
    print(f"[decomp] image control: {agree} of {scanned} recorded instructions agree; {disagree} "
          "disagree")
    if scanned == 0 or disagree:
        print("[decomp] REFUSED: the guest-address mapping is not verified against this "
              "repository's record, so no address below can be trusted.")
        return 2

    targets = _targets(role)
    project_dir = SCRATCH / PROJECT_NAME
    session = decomp_ghidra.GhidraRun(project_dir=project_dir, project_name=PROJECT_NAME,
                                      image=image, timeout=timeout)
    runner = decomp_ghidra.SubprocessRunner()
    try:
        if reanalyze or not session._project_files():
            print(f"[decomp] importing {image.path.name} at 0x{decomp_image.GHIDRA_IMPORT_BASE:08X} "
                  f"and auto-analyzing ({PROCESSOR_LABEL}); cache pinned to "
                  f"{(project_dir / 'ghidra-tmp').relative_to(ROOT)}")
            output = session.import_and_analyze(runner)
            for line in output.splitlines():
                if "decomp-postscript" in line or "ERROR" in line:
                    print(f"[decomp]   {line.strip()}")
        manifest_path, outdir = session.decompile(targets, runner)
    except decomp_ghidra.GhidraRefusal as error:
        print(f"[decomp] REFUSED: {error}")
        return 2

    try:
        entries, document = decomp_manifest.load(manifest_path)
    except decomp_manifest.ManifestRefusal as error:
        print(f"[decomp] REFUSED: {error}")
        return 2
    verdict = decomp_manifest.admit(entries, document, outdir, targets)
    print(f"[decomp] manifest: {verdict.summary()}")
    print(f"[decomp] decompiled C and disassembly: {outdir.relative_to(ROOT)}/")
    for target in targets:
        key = "0x%08X" % target.address
        entry = next((row for row in entries if row.address == target.address), None)
        if entry is None:
            print(f"[decomp]   {key} {target.name}: NOT SERVED -- named above as missing")
            continue
        if key in verdict.admitted:
            print(f"[decomp]   ADMITTED {key} {entry.name}: {entry.instr_count} instruction(s), "
                  f"{entry.body_lines} line(s) of C, anchors "
                  + " ".join("0x%08X" % value for value in target.anchors) + " all present")
        else:
            print(f"[decomp]   REFUSED  {key} {entry.name}: {verdict.refused.get(key, 'unnamed')}")
    for key, reason in sorted(verdict.refused.items()):
        if key in ("SHORT READ", "NORETURN GUARD INERT"):
            print(f"[decomp]   RUN-LEVEL REFUSAL {key}: {reason}")
    if not verdict.ok:
        print("[decomp] NOT every decompile is admissible, so nothing above is evidence. A truncated "
              "or absent body is worse than no body, because it will be trusted.")
        return 1

    # The chain may only be believed once a control passed IN THIS INVOCATION. A green control from
    # a previous run is not a green control now.
    if role in ("chain", "all"):
        control_admitted = all("0x%08X" % target.address in verdict.admitted
                               for target in decomp_targets.by_role("control")
                               if any(entry.address == target.address for entry in entries))
        if not control_admitted:
            print("[decomp] REFUSED: a chain decompile without a passing control in the same run is "
                  "not evidence. Fix the pipeline before believing the chain.")
            return 1
    print("[decomp] every requested target is admissible. The C is a reading aid, not a buildable "
          "artifact; what ships is the named layout and the native override built against it.")
    return 0


PROCESSOR_LABEL = decomp_ghidra.PROCESSOR


def selftest() -> int:
    scratch = ROOT / "scratch"
    failures = 0
    print("[decomp] decomp_image")
    failures += decomp_image.selftest()
    print("[decomp] decomp_manifest")
    failures += decomp_manifest.selftest(scratch)
    print("[decomp] decomp_ghidra")
    failures += decomp_ghidra.selftest(scratch)
    print("[decomp] decomp_targets")
    failures += _targets_selftest()
    print(f"[decomp] selftest {'FAILED' if failures else 'PASS'}: {failures} failing module(s)")
    return 1 if failures else 0


def _targets_selftest() -> int:
    """The work order must be non-empty, must be resolvable, and must REFUSE what it does not name.

    The negative is the point: a target resolver that answered an empty set for an unknown address
    would let `run` report 'nothing decompiled' for a typo, which is the workspace's signature
    confident-wrong-instead-of-absent failure.
    """
    failures = 0
    every = decomp_targets.by_role("all")
    if not every:
        print("[decomp-targets] selftest FAIL the work order is empty")
        return 1
    print(f"[decomp-targets] selftest the work order names {len(every)} target(s): "
          + ", ".join("0x%08X" % target.address for target in every))
    control = decomp_targets.by_role("control")
    if not control:
        print("[decomp-targets] selftest FAIL there is no control, so nothing can be believed")
        failures += 1
    else:
        anchors = control[0].anchors
        if not anchors:
            print("[decomp-targets] selftest FAIL the control demands no evidence, so a truncated "
                  "body would pass it")
            failures += 1
        else:
            print(f"[decomp-targets] selftest the control demands "
                  + " ".join("0x%08X" % value for value in anchors) + " be present in its body")
    for name in ("0x800258f0", "control", "chain"):
        try:
            decomp_targets.resolve(name)
            print(f"[decomp-targets] selftest resolve({name!r}) -> OK")
        except decomp_targets.TargetRefusal as error:
            print(f"[decomp-targets] selftest FAIL resolve({name!r}): {error}")
            failures += 1
    try:
        decomp_targets.resolve("0xDEADBEEF")
        print("[decomp-targets] selftest FAIL an unnamed address resolved, so the corpus is not "
              "bounded")
        failures += 1
    except decomp_targets.TargetRefusal:
        print("[decomp-targets] selftest an unnamed address is REFUSED as out of scope, not as a "
              "bad address -> OK")
    try:
        decomp_targets.by_role("nonsense")
        print("[decomp-targets] selftest FAIL an unknown role returned a set")
        failures += 1
    except decomp_targets.TargetRefusal:
        print("[decomp-targets] selftest an unknown role is REFUSED rather than returning an empty "
              "set -> OK")
    print(f"[decomp-targets] selftest {'FAILED' if failures else 'PASS'}: {failures} failure(s)")
    return 1 if failures else 0


def main(argv: list = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--selftest", action="store_true",
                        help="run every module's own selftest; needs no disc, no Ghidra and no network")
    sub = parser.add_subparsers(dest="command")
    plan_parser = sub.add_parser("plan", help="print the work order and the image control")
    plan_parser.set_defaults(handler=lambda args: plan())
    run_parser = sub.add_parser("run", help="import/analyze if needed, then decompile the work order")
    run_parser.add_argument("--role", default="all", choices=("control", "chain", "all"))
    run_parser.add_argument("--reanalyze", action="store_true",
                            help="re-import and re-analyze instead of reusing the project")
    run_parser.add_argument("--timeout", type=int, default=decomp_ghidra.DEFAULT_TIMEOUT,
                            help="seconds Ghidra may take, per phase")
    args = parser.parse_args(argv)
    if args.selftest:
        return selftest()
    if args.command == "run":
        return run(args.role, args.reanalyze, args.timeout)
    return plan()


if __name__ == "__main__":
    raise SystemExit(main())
