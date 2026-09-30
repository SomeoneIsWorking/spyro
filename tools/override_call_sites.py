#!/usr/bin/env python3
"""Re-derive every nested guest call an override makes from the retail `jal` it stands for.

WHY THIS EXISTS. A native override that calls a guest function does not execute a `jal`: it hands
the address to `dispatchGuest`, which runs the callee with `core.r[31]` as the return address. That
register is whatever the override last left there, so an override's nested calls ran their callees
with the OVERRIDE's caller address in `$ra` — a value the retail body never leaves behind, and one a
callee is free to keep. Measured 2026-10-01 on the attract route with every call shadowed
(docs/issues/0150): `camera_collision_update` differed from retail in two bytes at 0x80077E04, the
`$ra` word `func_8004BE4C` spills to a global save area that outlives the call, and
`allocate_particle_slot` exited with `$ra` 0x8008A5C0 where retail's `jal rand` at 0x80053598 left
0x800535A0. The fix is `spyro::callGuestJumpedFrom(core, owner, jalSite, callee, ...)`, so the
address a human writes at a call site is now a guest address standing for one retail instruction,
and this gate is what keeps it standing for THAT instruction.

THE RULE. For every `spyro::callGuestJumpedFrom` call site in an override module, the `jalSite`
argument — a literal, a file constant, or one field of a constant table of addresses — must resolve
to addresses that are each a `jal` inside one of the module's overridden functions, and the `jal` at
every one of those addresses must target the value the `callee` argument resolves to. A site one
instruction off, a callee belonging to a different `jal`, a site from another function and a table
whose rows disagree are four different ways to be wrong, and all four are refused. A `<name>.<field>`
argument resolves against every address table in the module by its field name, because a table
reaches the helper through a parameter; every row of that table is checked, so one wrong row in a
three-pass table is caught rather than averaged away.

The functions are decoded straight from the provisioned retail executable, and a missing, unreadable
or undecodable one is a refusal, never a pass. This reads call sites, not guest data addresses —
tools/override_constants.py owns those.

Usage:
    uv run --frozen python tools/override_call_sites.py [module.cpp ...]
    uv run --frozen python tools/override_call_sites.py --selftest
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
MAX_FUNCTION_BYTES = 0x2000
HELPER = "spyro::callGuestJumpedFrom"

COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.DOTALL)
REGISTRATION = re.compile(r"installNativeOverride\(\s*core,\s*0x([0-9A-Fa-f]{8})u")
LITERAL = re.compile(r"\b0[xX]([0-9A-Fa-f]{8})[uUlL]*\b")
NUMBER = re.compile(r"\b([0-9]+)[uUlL]*\b")
IDENTIFIER = re.compile(r"\b([A-Za-z_][A-Za-z_0-9]*)\b")
CONSTANT = re.compile(
    r"constexpr\s+(?:std::)?(?:u?int(?:8|16|32)_t|auto)\s+([A-Za-z_][A-Za-z_0-9]*)\s*=\s*"
    r"(0[xX][0-9A-Fa-f]{8}|[0-9]+)[uUlL]*\s*;"
)
STRUCT = re.compile(r"struct\s+([A-Za-z_][A-Za-z_0-9]*)\s*\{([^}]*)\}\s*;")
MEMBER = re.compile(r"\b(?:std::)?u?int(?:8|16|32)_t\s+([A-Za-z_][A-Za-z_0-9]*)\s*;")
TABLE = re.compile(
    r"constexpr\s+(?P<type>[A-Za-z_][A-Za-z_0-9]*)\s+(?P<name>[A-Za-z_][A-Za-z_0-9]*)"
    r"(?:\[(?P<count>\d+)\])?\s*=\s*\{(?P<body>[^{}]*(?:\{[^{}]*\}[^{}]*)*)\}\s*;",
    re.DOTALL,
)
# The qualifier is optional because a call inside `spyro1::native` resolves it by enclosing scope,
# and a gate whose pattern missed those would report a module with zero call sites rather than fail.
CALL = re.compile(
    r"(?:spyro::)?"
    + re.escape(HELPER.removeprefix("spyro::"))
    + r"\(\s*\*c,\s*(\"[^\"]*\"|[A-Za-z_][A-Za-z_0-9]*),\s*([^,]+?),\s*([^,\n]+?),",
    re.DOTALL,
)
TABLE_FIELD = re.compile(r"\b[A-Za-z_][A-Za-z_0-9]*\.(?P<field>[A-Za-z_][A-Za-z_0-9]*)\b")

JAL, SPECIAL, JR, RS = 0x03, 0x00, 0x08, 31
BRANCHES = {0x04, 0x05, 0x06, 0x07}
REGIMM = 0x01


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


def function_calls(image: Image, entry: int) -> dict[int, int]:
    """`{address: target}` for every `jal` in `entry`'s body, from the entry to its final `jr $ra`."""
    calls: dict[int, int] = {}
    furthest = entry
    pc = entry
    while pc < entry + MAX_FUNCTION_BYTES:
        word = image.word(pc)
        op, rs = word >> 26, (word >> 21) & 31
        if op == JAL:
            calls[pc] = ((pc + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
        if op in BRANCHES or op == REGIMM:
            sign = word & 0xFFFF
            offset = sign - 0x10000 if sign & 0x8000 else sign
            furthest = max(furthest, pc + 4 + offset * 4)
        if op == SPECIAL and word & 0x3F == JR and rs == RS and pc >= furthest:
            return calls
        pc += 4
    raise ValueError(f"no final jr $ra within 0x{MAX_FUNCTION_BYTES:X} bytes of 0x{entry:08X}")


def strip_code(source: str) -> str:
    """Source with comments blanked out, so an address named in prose is never read as an argument."""
    return COMMENT.sub(lambda match: " " * (match.end() - match.start()), source)


def address_tables(code: str) -> tuple[dict[str, list[int]], list[str], set[str]]:
    """`({field: [address per row]}, problems, table names)` for the module's address tables."""
    members = {name: MEMBER.findall(body) for name, body in STRUCT.findall(code)}
    fields: dict[str, list[int]] = {}
    problems: list[str] = []
    tables: set[str] = set()
    for match in TABLE.finditer(code):
        table_type = match.group("type")
        if table_type not in members:
            continue
        tables.add(match.group("name"))
        rows = [
            [int(value, 16) for value in LITERAL.findall(row)]
            for row in re.findall(r"\{([^{}]*)\}", match.group("body"))
        ]
        rows = [row for row in rows if row]
        names = members[table_type]
        declared = match.group("count")
        if declared is not None and len(rows) != int(declared):
            problems.append(
                f"table {match.group('name')} declares {declared} row(s) and writes {len(rows)}"
            )
        # Each row is one instance of the struct, so a member's address is that member's column.
        for column, name in enumerate(names):
            for row in rows:
                if column < len(row):
                    fields.setdefault(name, []).append(row[column])
    return fields, problems, tables


def resolve(
    expression: str, constants: dict[str, int], fields: dict[str, list[int]], tables: set[str]
) -> tuple[list[int], str]:
    """`(addresses, reason)` for one call-site argument: a literal, a constant, or a table field.

    `reason` is empty when the argument resolved, and otherwise names what is missing, because a
    refusal that says only "no address" is the shape of a gate that cannot say why it failed.
    """
    text = expression.strip()
    field = TABLE_FIELD.fullmatch(text)
    if field:
        name = field.group("field")
        if name in fields:
            return list(fields[name]), ""
        return [], f"no table defines the field {name}" if fields else "no address table in the module"
    bare = LITERAL.fullmatch(text) or NUMBER.fullmatch(text)
    if bare:
        return [int(bare.group(1), 16)], ""
    if text in constants:
        return [constants[text]], ""
    if text in tables:
        return [], f"table {text} has no per-field addresses"
    return [], f"{text} names no constant or address"


def audit(code: str, image: Image) -> tuple[list[str], int, int, int]:
    entries = sorted({int(address, 16) for address in REGISTRATION.findall(code)})
    calls: dict[int, int] = {}
    for entry in entries:
        try:
            calls.update(function_calls(image, entry))
        except ValueError as error:
            return [str(error)], 0, 0, len(entries)
    constants = {
        name: int(value, 16) if value.lower().startswith("0x") else int(value)
        for name, value in CONSTANT.findall(code)
    }
    fields, problems, table_names = address_tables(code)
    failures = list(problems)
    sites_seen = 0
    for owner, site_expression, callee_expression in CALL.findall(code):
        sites_seen += 1
        sites, site_reason = resolve(site_expression, constants, fields, table_names)
        callees, callee_reason = resolve(callee_expression, constants, fields, table_names)
        label = f"{owner} -> {callee_expression.strip()}"
        if site_reason:
            failures.append(f"{label}: site {site_expression.strip()}: {site_reason}")
            continue
        if not sites:
            failures.append(f"{label}: site {site_expression.strip()} resolves to no address")
            continue
        if callee_reason or len(set(callees)) != 1:
            failures.append(
                f"{label}: callee {callee_expression.strip()} is not one address"
                + (f" ({callee_reason})" if callee_reason else "")
            )
            continue
        callee = callees[0]
        for site in sites:
            if site not in calls:
                failures.append(f"{label}: 0x{site:08X} is not a `jal` in an overridden function")
            elif calls[site] != callee:
                failures.append(
                    f"{label}: the `jal` at 0x{site:08X} targets 0x{calls[site]:08X}, "
                    f"not the callee 0x{callee:08X}"
                )
    return failures, sites_seen, len(calls), len(entries)


def selftest() -> int:
    header = bytearray(TEXT_FILE_OFFSET)
    header[:8] = b"PS-X EXE"
    words = [
        (0x0F << 26) | (2 << 16) | 0x8007,  # 0x80010000 lui v0, 0x8007
        (0x03 << 26) | ((0x8001778C >> 2) & 0x03FFFFFF),  # 0x80010004 jal VecSub
        0,  # 0x80010008 delay slot
        (0x03 << 26) | ((0x80017700 >> 2) & 0x03FFFFFF),  # 0x8001000C jal VecCopy
        0,  # 0x80010010 delay slot
        (0x03 << 26) | ((0x80017908 >> 2) & 0x03FFFFFF),  # 0x80010014 jal other
        0,  # 0x80010018 delay slot
        (0x03 << 26) | ((0x8001778C >> 2) & 0x03FFFFFF),  # 0x8001001C jal VecSub again
        0,  # 0x80010020 delay slot
        (0x03 << 26) | ((0x80017700 >> 2) & 0x03FFFFFF),  # 0x80010024 jal VecCopy again
        0,  # 0x80010028 delay slot
        0x03E00008,  # 0x8001002C jr ra
        0,
    ]
    text = b"".join(struct.pack("<I", word) for word in words)
    struct.pack_into("<II", header, 0x18, 0x80010000, len(text))
    image = Image(bytes(header) + text)

    prologue = (
        '#include "native_execution.h"\n'
        "constexpr std::uint32_t kName = 0x80010000u;\n"
        "constexpr std::uint32_t kVecSub = 0x8001778Cu;\n"
        "constexpr std::uint32_t kVecCopy = 0x80017700u;\n"
        "constexpr std::uint32_t kOther = 0x80017908u;\n"
        "struct Sites { std::uint32_t sub; std::uint32_t copy; };\n"
    )
    body = (
        "void body(Core *c) {{\n"
        '  spyro::installNativeOverride(core, 0x80010000u, "body", body);\n'
        "  spyro::callGuestJumpedFrom(*c, kName, {site}, {callee}, 1u);\n"
        "}}\n"
        "{table}"
    )
    table_two_rows = (
        "constexpr Sites kSites[2] = {{0x80010004u, 0x8001000Cu}, {0x8001001Cu, 0x80010024u}};\n"
    )
    cases = [
        ("the exact jal accepted", body.format(site="0x80010004u", callee="kVecSub", table=""), []),
        (
            "a constant naming the jal accepted",
            body.format(site="kSubSite", callee="kVecSub", table="constexpr std::uint32_t kSubSite = 0x80010004u;\n"),
            [],
        ),
        (
            "a jal's return address refused",
            body.format(site="0x8001000Cu", callee="kVecSub", table=""),
            ["0x8001000C"],
        ),
        (
            "the wrong callee refused",
            body.format(site="0x80010004u", callee="kVecCopy", table=""),
            ["0x80017700"],
        ),
        (
            "a site outside the overridden function refused",
            body.format(site="0x80020004u", callee="kVecSub", table=""),
            ["0x80020004"],
        ),
        (
            "every row of a table checked",
            body.format(site="sites.sub", callee="kVecSub", table=table_two_rows),
            [],
        ),
        (
            "one wrong row in a table refused",
            body.format(site="sites.sub", callee="kVecSub", table=table_two_rows).replace(
                "{0x8001001Cu", "{0x80010014u"
            ),
            ["0x80010014"],
        ),
        (
            "a table row count disagreeing with its declaration refused",
            body.format(site="sites.sub", callee="kVecSub", table=table_two_rows).replace(
                "kSites[2]", "kSites[3]"
            ),
            ["declares 3 row"],
        ),
        (
            "a field the table does not define refused",
            body.format(site="sites.magnitude", callee="kVecSub", table=table_two_rows),
            ["no table defines the field magnitude"],
        ),
        (
            "a call site that names no address refused",
            body.format(site="jalSite", callee="kVecSub", table=""),
            ["names no constant or address"],
        ),
        (
            "an address named in a comment is prose",
            body.format(site="0x80010004u /* 0x8001000Cu */", callee="kVecSub", table=""),
            [],
        ),
        (
            "a string-literal owner is still a call site",
            body.format(site="0x80010004u", callee="kVecSub", table="").replace(
                "*c, kName,", '*c, "body",'
            ),
            [],
        ),
        ("a module with no override is skipped", "int main() { return 0; }\n", []),
    ]
    failures = 0
    scratch = ROOT / "scratch/override_call_sites_selftest.cpp"
    scratch.parent.mkdir(parents=True, exist_ok=True)
    for name, source, needles in cases:
        scratch.write_text(prologue + source)
        reported, sites, calls, entries = audit(strip_code(scratch.read_text()), image)
        scratch.unlink(missing_ok=True)
        ok = bool(reported) == bool(needles) and all(
            any(needle in line for line in reported) for needle in needles
        )
        failures += not ok
        print(
            f"{'ok  ' if ok else 'FAIL'} {name}: {sites} call site(s), {calls} `jal`, "
            f"{entries} override(s), reported {len(reported)}"
        )
        for line in reported:
            print(f"          {line}")
    print(f"selftest: {len(cases) - failures} of {len(cases)} cases")
    return failures


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("modules", nargs="*", type=Path)
    parser.add_argument("--selftest", action="store_true")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return 1 if selftest() else 0

    if not EXE.exists():
        print(
            f"REFUSED: {EXE} is absent; provision the Spyro 1 executable "
            f"(tools/provision_title.py) or this gate checks nothing and says so"
        )
        return 2
    try:
        image = Image(EXE.read_bytes())
    except (ValueError, struct.error) as error:
        print(f"REFUSED: {EXE}: {error}")
        return 2

    paths = arguments.modules or sorted(
        path
        for directory in MODULE_DIRS
        if directory.is_dir()
        for path in directory.glob("*.cpp")
    )
    failures = 0
    scanned = 0
    for path in paths:
        code = strip_code(path.read_text())
        reported, sites, calls, entries = audit(code, image)
        if entries == 0:
            continue
        scanned += 1
        verdict = "FAIL" if reported else "ok"
        print(
            f"[call-sites] {verdict} {path.name}: {sites} nested call site(s) over {entries} "
            f"override(s), {calls} `jal` decoded; wrong: {len(reported)}"
        )
        for line in reported:
            print(f"          {line}")
        failures += bool(reported)
    if scanned == 0:
        print("REFUSED: no override module with a registration was scanned; matched 0")
        return 2
    print(f"[call-sites] {scanned} module(s) scanned, {failures} failing")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
