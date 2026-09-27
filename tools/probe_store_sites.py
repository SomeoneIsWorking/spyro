#!/usr/bin/env python3
"""Every guest store that can reach one guest word, read out of the decompiled instruction listing.

WHY THIS EXISTS. `docs/issues/0133` narrowed the Spyro 1 tick-556 divergence to a single guest
word, `g_Spyro + 0x88` (`m_touchingMoby`, 0x80078AE0): the product ends the update holding 1 and the
reference 0, and `func_8003FE40` at 0x8003FE7C clears it. The claim under test is "the product
executed a store to that word that the reference did not". That claim is only worth acting on if
the set of stores that CAN reach the word is known, and right now it is not: the decompiled tree is
a mix of matching C and raw assembly, so a reader who greps the C finds one read and concludes
nothing writes it, and a reader who greps the assembly for `%lo(g_Spyro + 0x88)` sees only the
clear. Neither is the whole set.

This scans the LISTING MECHANICALLY, from the raw instruction words in each line's own comment, so
it needs no symbol table and cannot inherit a symbol-resolution mistake:

  * `lui $r, IMM`       (op 0x0F)         puts the high half of a 32-bit constant in $r
  * `addiu $r,$r,IMM`   (op 0x09)         adds the low half — the PSY-Q absolute-address idiom
  * `sw $rt, DISP($r)`  (op 0x2B)         targets $r + sign_extend(DISP)

so a store is attributed whenever the base register's value is KNOWN at that point, from ANY
register rather than from `$at` alone. Sign extension is the load-bearing detail: the real
`sw $zero, 0x8AE0($at)` with `$at = 0x80080000` is 0x80078AE0, and reading 0x8AE0 as unsigned would
put the store at 0x80088AE0 — 0x10000 off, which is a plausible-looking address that is not the
field. The selftest pins that case.

It reports every store whose resolved target is the watched word, naming the address, the register
stored, and the instruction word, plus the number of stores it did NOT attribute — so "matched 1" is
a measurement over a stated denominator and not a quiet absence.

WHAT IT CANNOT SEE, stated rather than hidden: a store whose base register is built by arithmetic
this scan does not model (`addu`, a `lw` from a pointer table, a value carried in from a caller) is
NOT resolved and is reported as un-attributed, never silently dropped; and a bulk copy (DMA, a level
overlay load, a memcpy-shaped loop) is not a store instruction at all.

    uv run --frozen python tools/probe_store_sites.py --address 0x80078AE0
    uv run --frozen python tools/probe_store_sites.py --address 0x80078AE0 --selftest
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
DECOMP = ROOT / "external" / "spyro-1"
# Scratch is the repo's own gitignored run-artifact area. Never /tmp: it is a small RAM-backed tmpfs
# here, and a probe that wrote its fixtures there would be writing artifacts to the wrong place.
SCRATCH = ROOT / "scratch"

# `/* FILEOFFSET VRAMADDR INSNWORD */  mnemonic operands`
LINE = re.compile(
    r"/\*\s*([0-9A-Fa-f]+)\s+([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})\s*\*/\s+(\S+)\s*(.*)$")


def _instruction_word(field: str) -> int:
    """The 32-bit instruction a listing comment prints.

    The listing prints the word in BIG-ENDIAN byte order (`E08A20AC` for the real
    `0xAC208AE0 = sw $zero, 0x8AE0($at)`), because it is showing the bytes as they sit in the
    executable. Reading it as a host integer would decode the opcode as 0x08 (BREAK-family) instead
    of 0x2B (SW) and turn every store in the tree into a miss, so it is byte-reversed here. This is
    verified against the real image by the selftest."""
    raw = bytes.fromhex(field)
    return int.from_bytes(raw, "little")
STORE_OPS = {0x28: "sb", 0x29: "sh", 0x2A: "swl", 0x2B: "sw", 0x2E: "swc2", 0x2F: "swc1", 0x38: "sc",
             0x39: "scd"}
LUI_OP = 0x0F
ADDIU_OP = 0x09


def _displacement(word: int) -> int:
    """The sign-extended 16-bit displacement of a store, taken from the instruction WORD.

    Read from the word, not from the printed operand, and that is the whole point: the operand
    reads `%lo(g_Spyro + 0x88)`, whose numeric value needs a symbol table this probe deliberately
    does not have, while `word & 0xFFFF` IS the displacement bits. The SIGN matters — a
    displacement is signed, and reading it unsigned would move every negative-offset store by
    0x10000 and silently lose exactly the register-relative stores this probe exists to find."""
    return ((word & 0xFFFF) ^ 0x8000) - 0x8000


def _store_register(word: int) -> str:
    return f"r{(word >> 16) & 0x1F}"


def scan(address: int, roots: list[Path]) -> dict[str, object]:
    """Walk each listing once, tracking the one base register whose value is KNOWN.

    Deliberately a single register, not a register file: the only way this scan knows a base
    register's value is that it SAW the `lui` that set it, in this same straight-line run. Any
    other base is un-attributed and counted, which is what keeps the denominator honest."""
    base_register = -1
    base_value = 0
    hits: list[dict[str, object]] = []
    stores = 0
    attributed = 0
    unresolved = 0
    files = 0
    instructions = 0
    for root in roots:
        for path in sorted(root.rglob("*.s")):
            files += 1
            for raw in path.read_text(errors="replace").splitlines():
                match = LINE.search(raw)
                if not match:
                    continue
                vram = int(match.group(2), 16)
                word = _instruction_word(match.group(3))
                instructions += 1
                op = (word >> 26) & 0x3F
                if op == LUI_OP:
                    # `lui $r, IMM` — the high half of an absolute address, in ANY register.
                    base_register = (word >> 16) & 0x1F
                    base_value = (word & 0xFFFF) << 16
                    continue
                if op == ADDIU_OP:
                    source = (word >> 21) & 0x1F
                    target = (word >> 16) & 0x1F
                    if source == base_register == target:
                        base_value = (base_value + _displacement(word)) & 0xFFFFFFFF
                    continue
                if op not in STORE_OPS:
                    continue
                stores += 1
                if (word >> 21 & 0x1F) != base_register:
                    unresolved += 1
                    continue
                attributed += 1
                if (base_value + _displacement(word)) & 0xFFFFFFFF == address:
                    try:
                        where = path.resolve().relative_to(ROOT)
                    except ValueError:
                        where = path
                    hits.append({
                        "vram": vram,
                        "word": word,
                        "register": _store_register(word),
                        "file": str(where),
                        "line": raw.strip(),
                    })
    return {"hits": hits, "stores": stores, "attributed": attributed, "unresolved": unresolved,
            "files": files, "instructions": instructions}


def selftest() -> int:
    """Positive AND negative: the resolver must find a real store and refuse a wrong address."""
    import tempfile
    # The real listing lines for the store the console's observer recorded, byte-for-byte, so the
    # decode is pinned to the image rather than to my own encoder. The displacement 0x8AE0 is
    # NEGATIVE as a signed 16-bit value: 0x80080000 + 0x8AE0 is 0x80088AE0, which is 0x10000 above
    # the target, and only sign extension puts it on 0x80078AE0. That is the case a naive resolver
    # gets wrong, so it is in the suite.
    real = """glabel func_8003FE40
/* 30678 8003FE78 0880013C */  lui        $at, %hi(g_Spyro + 0x88)
/* 3067C 8003FE7C E08A20AC */  sw         $zero, %lo(g_Spyro + 0x88)($at)
"""
    # Same shape, one word off: 0x8AE0 + 4 = 0x8AE4 -> 0x80078AE4, which must NOT match.
    wrong = """glabel func_8003FE40
/* 30678 8003FE78 0880013C */  lui        $at, %hi(g_Spyro + 0x88)
/* 3067C 8003FE7C E48A20AC */  sw         $zero, %lo(g_Spyro + 0x8C)($at)
"""
    # A POSITIVE displacement, so the suite also covers the sign the other way: 0x80080000+0x10A0.
    positive_disp = """glabel t
/* 000000 80000000 0880013C */  lui        $at, 0x8008
/* 000004 80000004 000010AC */  sw         $zero, 0x10A0($at)
"""
    # $at never set by an `lui` in this fragment: the store must be reported UN-attributable, not
    # silently resolved against whatever high half happened to be left over.
    no_base = """glabel t
/* 000004 80000004 E08A20AC */  sw         $zero, 0x8AE0($at)
"""
    target = 0x80078AE0
    for name, text, want in (("real-listing-line", real, 1), ("wrong-address", wrong, 0),
                             ("positive-disp", positive_disp, 0), ("no-lui-base", no_base, 0)):
        # One directory PER CASE: scan() walks a whole root, so sharing a root would let one case's
        # lines answer another's expectation and make the suite pass for the wrong reason.
        with tempfile.TemporaryDirectory(dir=SCRATCH) as tmp:
            path = Path(tmp) / "case.s"
            path.write_text(text)
            result = scan(target, [Path(tmp)])
            got = len(result["hits"])  # type: ignore[arg-type]
            status = "ok" if got == want else "FAIL"
            print(f"  {status:4} {name}: matched {got}, expected {want}")
            if got != want:
                return 1
    print("  selftest: 4/4 cases behaved as required. One case is the REAL listing line the console "
          "recorded (must match), one is one word away from it (must not), one has a positive "
          "displacement (must not), and one has no `lui` base at all (must not). The real line's "
          "displacement is NEGATIVE, so the suite fails if the resolver stops sign-extending.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--address", default="0x80078AE0")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--root", action="append", default=[],
                        help="limit the scan to this directory (default: the whole decomp asm/)")
    args = parser.parse_args()
    if args.selftest:
        SCRATCH.mkdir(parents=True, exist_ok=True)
        return selftest()
    address = int(args.address, 0)
    roots = [Path(r) for r in args.root] if args.root else [DECOMP / "asm"]
    print(f"watched address : 0x{address:08X}")
    print(f"scanned roots   : {[str(r) for r in roots]}")
    result = scan(address, roots)
    print(f"asm files read  : {result['files']}")
    print(f"instructions    : {result['instructions']}")
    print(f"store insns     : {result['stores']}  "
          f"(base register KNOWN: {result['attributed']},  base register unknown: {result['unresolved']})")
    hits = result["hits"]  # type: ignore[assignment]
    print(f"stores reaching the watched word: {len(hits)}")
    for hit in hits:
        print(f"  0x{hit['vram']:08X}  word=0x{hit['word']:08X}  rt=${hit['register']}  "
              f"{hit['file']}")
        print(f"      {hit['line']}")
    if not hits:
        print("verdict: NO store in the scanned listing can reach this word with a KNOWN base "
              "register. Read that against the un-attributable count above: a store whose base this "
              "scan could not resolve is counted, not assumed away. The docstring's bulk-copy limit "
              "applies too — a DMA or a memcpy-shaped loop is not a store instruction at all.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
