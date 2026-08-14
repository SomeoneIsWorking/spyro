#!/usr/bin/env python3
"""Format or verify first-party Spyro C++ sources."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import site
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOTS = (ROOT / "game", ROOT / "tests")
SOURCE_SUFFIXES = {".cpp", ".h"}


def find_clang_format() -> str:
    configured = os.environ.get("CLANG_FORMAT")
    candidates = [configured, shutil.which("clang-format")]
    candidates.append(str(Path(site.getuserbase()) / "bin" / "clang-format"))
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return candidate
    raise RuntimeError(
        "clang-format is missing; install it with "
        "`python3 -m pip install --user clang-format` or set CLANG_FORMAT"
    )


def source_files(requested: list[str]) -> list[Path]:
    if requested:
        files = [(ROOT / value).resolve() for value in requested]
        invalid = [path for path in files if not path.is_file() or path.suffix not in SOURCE_SUFFIXES]
        if invalid:
            names = ", ".join(str(path) for path in invalid)
            raise RuntimeError(f"not a supported source file: {names}")
        return sorted(files)

    return sorted(
        path
        for source_root in SOURCE_ROOTS
        for path in source_root.rglob("*")
        if path.is_file() and path.suffix in SOURCE_SUFFIXES
    )


def formatted_bytes(executable: str, path: Path) -> bytes:
    result = subprocess.run(
        [executable, "--style=file", str(path)],
        cwd=ROOT,
        check=True,
        stdout=subprocess.PIPE,
    )
    return result.stdout


def run_selftest(executable: str) -> int:
    unformatted = b"int main(){if(true)return 0;}\n"
    command = [executable, "--style=file", "--assume-filename=game/selftest.cpp"]
    formatted = subprocess.run(
        command,
        cwd=ROOT,
        check=True,
        input=unformatted,
        stdout=subprocess.PIPE,
    ).stdout
    stable = subprocess.run(
        command,
        cwd=ROOT,
        check=True,
        input=formatted,
        stdout=subprocess.PIPE,
    ).stdout
    changed = formatted != unformatted
    idempotent = stable == formatted
    print(
        f"[format] selftest changed_unformatted={str(changed).lower()} "
        f"idempotent={str(idempotent).lower()}"
    )
    return 0 if changed and idempotent else 1


def main() -> int:
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--fix", action="store_true")
    mode.add_argument("--selftest", action="store_true")
    parser.add_argument("files", nargs="*")
    args = parser.parse_args()

    try:
        executable = find_clang_format()
        if args.selftest:
            return run_selftest(executable)
        files = source_files(args.files)
    except RuntimeError as error:
        print(f"[format] REFUSED: {error}", file=sys.stderr)
        return 2

    if args.fix:
        subprocess.run(
            [executable, "--style=file", "-i", *(str(path) for path in files)],
            cwd=ROOT,
            check=True,
        )
        print(f"[format] formatted {len(files)}/{len(files)} source files")
        return 0

    unformatted = [path for path in files if path.read_bytes() != formatted_bytes(executable, path)]
    for path in unformatted:
        print(f"[format] UNFORMATTED {path.relative_to(ROOT)}")
    print(f"[format] checked {len(files)}/{len(files)} source files; unformatted={len(unformatted)}")
    return 1 if unformatted else 0


if __name__ == "__main__":
    raise SystemExit(main())
