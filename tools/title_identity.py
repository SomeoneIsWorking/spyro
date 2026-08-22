#!/usr/bin/env python3
"""Verify one Spyro title's serial, hashes, and PS-X EXE header against its manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import struct
from dataclasses import dataclass

ROOT = pathlib.Path(__file__).resolve().parent.parent


class Refused(Exception):
    """The input cannot support an executable-identity claim."""


@dataclass(frozen=True)
class ExecutableIdentity:
    name: str
    size: int
    sha1: str
    sha256: str
    entry: int
    gp: int
    text_address: int
    text_size: int
    stack_address: int
    stack_offset: int
    markers: tuple[str, ...]


def _hex(value: object, field: str) -> int:
    if not isinstance(value, str):
        raise Refused(f"manifest field {field} must be a hexadecimal string")
    try:
        return int(value, 16)
    except ValueError as error:
        raise Refused(f"manifest field {field} is not hexadecimal: {value!r}") from error


def load_manifest(slug: str, *, root: pathlib.Path = ROOT) -> dict[str, object]:
    path = root / "titles" / slug / "executable.json"
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise Refused(f"cannot read manifest {path}: {error}") from error
    required = {
        "title",
        "region",
        "serial",
        "executable",
        "file_size",
        "sha1",
        "sha256",
        "header",
        "region_markers",
    }
    missing = sorted(required - manifest.keys())
    if missing:
        raise Refused(f"manifest {path} is missing {', '.join(missing)}")
    return manifest


def expected(manifest: dict[str, object]) -> dict[str, object]:
    header = manifest["header"]
    markers = manifest["region_markers"]
    if not isinstance(header, dict):
        raise Refused("manifest field header must be an object")
    if (
        not isinstance(markers, list)
        or not markers
        or not all(isinstance(marker, str) for marker in markers)
    ):
        raise Refused("manifest field region_markers must be a non-empty string list")
    return {
        "name": manifest["executable"],
        "size": manifest["file_size"],
        "sha1": manifest["sha1"],
        "sha256": manifest["sha256"],
        "entry": _hex(header.get("entry"), "header.entry"),
        "gp": _hex(header.get("gp"), "header.gp"),
        "text_address": _hex(header.get("text_address"), "header.text_address"),
        "text_size": _hex(header.get("text_size"), "header.text_size"),
        "stack_address": _hex(header.get("stack_address"), "header.stack_address"),
        "stack_offset": _hex(header.get("stack_offset"), "header.stack_offset"),
        "markers": tuple(markers),
    }


def measure(path: pathlib.Path, markers: tuple[str, ...]) -> ExecutableIdentity:
    try:
        data = path.read_bytes()
    except OSError as error:
        raise Refused(f"cannot read executable {path}: {error}") from error
    if len(data) < 0x800 or data[:8] != b"PS-X EXE":
        raise Refused(f"{path} is not a valid PS-X EXE")
    entry, gp, text_address, text_size = struct.unpack_from("<4I", data, 0x10)
    stack_address, stack_offset = struct.unpack_from("<2I", data, 0x30)
    if text_size == 0 or not text_address <= entry < text_address + text_size:
        raise Refused(
            f"invalid PS-X EXE range: entry=0x{entry:08X}, "
            f"text=[0x{text_address:08X},0x{text_address + text_size:08X})"
        )
    found = tuple(marker for marker in markers if marker.encode("ascii") in data)
    return ExecutableIdentity(
        path.name,
        len(data),
        hashlib.sha1(data).hexdigest(),
        hashlib.sha256(data).hexdigest(),
        entry,
        gp,
        text_address,
        text_size,
        stack_address,
        stack_offset,
        found,
    )


def check(manifest: dict[str, object], path: pathlib.Path, *, verbose: bool = True) -> list[str]:
    want = expected(manifest)
    measured = measure(path, want["markers"])
    actual = vars(measured)
    failures = [
        f"{field}: manifest={value!r}, executable={actual[field]!r}"
        for field, value in want.items()
        if value != actual[field]
    ]
    if verbose:
        print(
            f"measured {measured.name}: {measured.size} bytes, sha1={measured.sha1}, "
            f"sha256={measured.sha256}"
        )
        if failures:
            for failure in failures:
                print(f"MISMATCH: {failure}")
        else:
            print(f"MATCH: 11/11 identity facts agree for {manifest['title']}")
        print("blind spot: identity/header only; this does not prove disc provenance or boot")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--title", choices=("spyro1", "spyro2"), required=True)
    parser.add_argument("--exe", type=pathlib.Path, required=True)
    args = parser.parse_args()
    try:
        return 1 if check(load_manifest(args.title), args.exe) else 0
    except Refused as error:
        print(f"REFUSED: {error}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
