#!/usr/bin/env python3
"""Refuse a native override module whose guest-address constants the retail code never computes.

WHY THIS EXISTS. A native override names guest globals and tables as constants. The worker that
writes one reads them off a disassembly where each address is a `lui` of the high half plus a
sign-extended 16-bit immediate, and a negative immediate is easy to add as unsigned. Measured
2026-09-29 (issue 0148): advance_body_animation_with_transitions carried 0x8007C470 where retail's
`lui 0x8007; addiu -0x3B90` is 0x8006C470. It read level-overlay bytes as animation numbers and passed
the override differential, because the sampled calls never took that branch; the attract demo then
crashed in the renderer. The differential cannot see a branch its route never takes. This check needs
no route.

THE RULE. For every override module under game/core/ and titles/spyro1/core/, every guest address constant
(0x80xxxxxx, excluding the registered entry points themselves) must lie within ACCESS_WINDOW bytes above
an address that some `lui` (alone, or with an `addiu`/`ori`/load/store) inside one of that module's overridden
functions computes. The functions are decoded straight from the provisioned retail executable.

Usage:
    uv run --frozen python tools/override_constants.py [module.cpp ...]
    uv run --frozen python tools/override_constants.py --selftest
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "scratch/assets/spyro1/SCUS_942.28"
MODULE_DIRS = (ROOT / "game/core", ROOT / "titles/spyro1/core")
TEXT_FILE_OFFSET = 0x800
ACCESS_WINDOW = (
    0x80  # a constant may name a field a short distance past the computed base
)
MAX_FUNCTION_BYTES = 0x2000
CONSTANT = re.compile(r"0x(80[0-9A-Fa-f]{6})u")
REGISTRATION = re.compile(r"installNativeOverride\(\s*core,\s*0x([0-9A-Fa-f]{8})u")
LOADS_STORES = {
    0x20,
    0x21,
    0x22,
    0x23,
    0x24,
    0x25,
    0x26,
    0x28,
    0x29,
    0x2A,
    0x2B,
    0x2E,
    0x32,
    0x3A,
}
JR, SPECIAL, REGIMM = 0x08, 0x00, 0x01
BRANCHES = {0x04, 0x05, 0x06, 0x07}


class Image:
    def __init__(self, exe: bytes) -> None:
        if exe[:8] != b"PS-X EXE":
            raise ValueError("not a PS-X EXE")
        self.base, size = struct.unpack_from("<II", exe, 0x18)
        self.text = exe[TEXT_FILE_OFFSET : TEXT_FILE_OFFSET + size]

    def word(self, address: int) -> int:
        offset = address - self.base
        if not 0 <= offset <= len(self.text) - 4:
            raise ValueError(f"0x{address:08X} is outside the executable's text")
        return struct.unpack_from("<I", self.text, offset)[0]


def simm(word: int) -> int:
    value = word & 0xFFFF
    return value - 0x10000 if value & 0x8000 else value


def computed_addresses(image: Image, entry: int) -> set[int]:
    """Addresses built by lui+immediate pairs from `entry` to its last `jr $ra` past every branch."""
    high: dict[int, int] = {}
    produced: set[int] = set()
    furthest = entry
    pc = entry
    while pc < entry + MAX_FUNCTION_BYTES:
        word = image.word(pc)
        op, rs, rt = word >> 26, (word >> 21) & 31, (word >> 16) & 31
        if op == 0x0F:
            high[rt] = (word & 0xFFFF) << 16
            produced.add(high[rt])
        elif op == 0x09 and rs in high:
            produced.add((high[rs] + simm(word)) & 0xFFFFFFFF)
        elif op == 0x0D and rs in high:
            produced.add(high[rs] | (word & 0xFFFF))
        elif op in LOADS_STORES and rs in high:
            produced.add((high[rs] + simm(word)) & 0xFFFFFFFF)
        if op in BRANCHES or op == REGIMM:
            furthest = max(furthest, pc + 4 + simm(word) * 4)
        if op == SPECIAL and word & 0x3F == JR and rs == 31 and pc >= furthest:
            return produced
        pc += 4
    raise ValueError(
        f"no final jr $ra within 0x{MAX_FUNCTION_BYTES:X} bytes of 0x{entry:08X}"
    )


def unexplained(source: str, image: Image) -> tuple[list[int], int, int]:
    entries = {int(address, 16) for address in REGISTRATION.findall(source)}
    produced: set[int] = set()
    for entry in entries:
        produced |= computed_addresses(image, entry)
    constants = {int(value, 16) for value in CONSTANT.findall(source)} - entries
    bad = sorted(
        k
        for k in constants
        if not any(0 <= k - base < ACCESS_WINDOW for base in produced)
    )
    return bad, len(constants), len(entries)


def check(paths: list[Path], image: Image) -> int:
    failures = 0
    for path in paths:
        bad, constants, entries = unexplained(path.read_text(), image)
        if entries == 0:
            continue
        verdict = "FAIL" if bad else "ok"
        print(
            f"[constants] {verdict} {path.name}: {constants} constant(s) over {entries} override(s); "
            f"unexplained: {', '.join(f'0x{k:08X}' for k in bad) or 'none'}"
        )
        failures += bool(bad)
    return failures


def selftest() -> int:
    header = bytearray(TEXT_FILE_OFFSET)
    header[:8] = b"PS-X EXE"
    code = [
        (0x0F << 26) | (2 << 16) | 0x8007,  # lui v0, 0x8007
        (0x09 << 26)
        | (2 << 21)
        | (2 << 16)
        | 0xC470,  # addiu v0, v0, -0x3B90 -> 0x8006C470
        (0x0F << 26) | (3 << 16) | 0x8008,  # lui v1, 0x8008
        (0x23 << 26)
        | (3 << 21)
        | (4 << 16)
        | 0x8AD0,  # lw a0, -0x7530(v1) -> 0x80078AD0
        0x03E00008,  # jr ra
        0,
    ]
    text = b"".join(struct.pack("<I", w) for w in code)
    struct.pack_into("<II", header, 0x18, 0x80010000, len(text))
    image = Image(bytes(header) + text)
    registration = 'spyro::installNativeOverride(core, 0x80010000u, "f", f);\n'
    cases = [
        ("sign-extended table accepted", "constexpr auto k = 0x8006C470u;", []),
        (
            "unsigned-immediate table refused",
            "constexpr auto k = 0x8007C470u;",
            [0x8007C470],
        ),
        ("field past a computed base accepted", "constexpr auto k = 0x80078AD4u;", []),
    ]
    failures = 0
    for label, constant, expected in cases:
        got, _, _ = unexplained(registration + constant, image)
        if got != expected:
            failures += 1
            print(f"FAIL {label}: got {[hex(k) for k in got]}")
    print(f"selftest: {len(cases) - failures} of {len(cases)} cases")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("modules", nargs="*", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    if not EXE.is_file():
        print(
            f"REFUSED: {EXE.relative_to(ROOT)} is not provisioned (tools/provision_title.py)",
            file=sys.stderr,
        )
        return 2
    modules = args.modules or sorted(
        path for directory in MODULE_DIRS for path in directory.glob("native_*.cpp")
    )
    return 1 if check(modules, Image(EXE.read_bytes())) else 0


if __name__ == "__main__":
    raise SystemExit(main())
