#!/usr/bin/env python3
"""Emit Spyro 2's resident SCUS_944.25 substrate from its verified executable."""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import subprocess
import sys

import title_identity

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_EXECUTABLE = ROOT / "scratch/bin/spyro2/SCUS_944.25"
OUTPUT_DIRECTORY = ROOT / "scratch/generated/spyro2"
SEEDS = ROOT / "titles/spyro2/recomp_seeds.json"


class Refused(RuntimeError):
    """The selected input cannot produce the measured Spyro 2 substrate."""


def generated_sources(directory: pathlib.Path) -> list[pathlib.Path]:
    manifest = directory / "rec_sources.cmake"
    if not manifest.is_file():
        raise Refused(f"emitter produced no {manifest.relative_to(ROOT)}")
    sources = [directory / name for name in re.findall(r"^\s*(\S+\.c)\s*$", manifest.read_text(), re.M)]
    missing = [path for path in sources if not path.is_file()]
    if missing:
        raise Refused(
            "emitter manifest names missing source(s): "
            + ", ".join(path.name for path in missing)
        )
    if not sources:
        raise Refused("emitter manifest contains zero translation units")
    return sources


def ensure(executable: pathlib.Path, psxport: pathlib.Path) -> int:
    if not executable.is_file():
        raise Refused(f"Spyro 2 executable is missing: {executable}")
    manifest = title_identity.load_manifest("spyro2")
    failures = title_identity.check(manifest, executable, verbose=False)
    if failures:
        raise Refused(
            f"{executable.name} disagrees with the Spyro 2 manifest on "
            f"{len(failures)} tracked fact(s)"
        )

    emitter = psxport / "tools/recomp/emit.py"
    if not emitter.is_file():
        raise Refused(f"PSXPORT_DIR={psxport} has no tools/recomp/emit.py")

    OUTPUT_DIRECTORY.mkdir(parents=True, exist_ok=True)
    output = OUTPUT_DIRECTORY / "spyro2_rec.c"
    result = subprocess.run(
        [
            sys.executable,
            str(emitter),
            str(executable),
            str(output),
            "--seeds",
            str(SEEDS),
        ],
        cwd=ROOT,
        check=False,
    )
    if result.returncode:
        raise Refused(f"Spyro 2 emitter exited {result.returncode}")

    sources = generated_sources(OUTPUT_DIRECTORY)
    declarations = OUTPUT_DIRECTORY / "rec_decls.h"
    wrappers = declarations.read_text().count("void gen_func_") if declarations.is_file() else 0
    if wrappers == 0:
        raise Refused("emitter produced zero resident function declarations")
    print(
        f"Spyro 2 substrate: {wrappers} resident function(s), "
        f"{len(sources)} translation unit(s), zero foreign/overlay seeds"
    )
    return wrappers


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=pathlib.Path, nargs="?", default=DEFAULT_EXECUTABLE)
    parser.add_argument(
        "--psxport",
        type=pathlib.Path,
        default=pathlib.Path(os.environ.get("PSXPORT_DIR", ROOT / "external/psxport")),
    )
    args = parser.parse_args(argv)
    try:
        ensure(args.executable.resolve(), args.psxport.resolve())
        return 0
    except (OSError, Refused, title_identity.Refused) as error:
        print(f"REFUSED: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
