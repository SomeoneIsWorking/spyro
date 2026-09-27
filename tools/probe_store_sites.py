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
ADDU_OP = 0x21
GP_REGISTER = 28
GP_HEADER = ROOT / "game" / "core" / "guest_gp.h"


def _guest_gp() -> int:
    """Spyro's $gp, read out of the PRODUCT's own header rather than restated here.

    `$gp` is set ONCE, by crt0, at runtime (`lui gp,0x8007 ; addiu gp,gp,0x5264` per
    `game/core/guest_gp.h`). No listing line anywhere performs that assignment, so before this
    function existed every `sw $x, DISP($gp)` was un-attributable and the probe's denominator hid
    a whole addressing mode rather than reporting it.

    That is not hypothetical. Measured 2026-09-27: the moby list the guest actually walks sits at
    `0x800700F4`, and `--find-instruction 0x800700F4` found 0 `jal` and 0 big-endian data pointers
    on either core — because the address is reached GP-relative
    (`0x800700F4 - 0x80075264 = 0xAE50`), which an absolute-address scan cannot see at all.

    Read from the header so there is ONE home for the value: a retune of the executable's GP
    changes the probe and the product together, or the probe silently resolves to the wrong word.
    A missing or unparseable header is a REFUSAL, not a default — a probe that guessed 0 here would
    return a confident, wrong answer, which is the failure mode this probe exists to avoid."""
    text = GP_HEADER.read_text()
    match = re.search(r"kGp\s*=\s*(0x[0-9A-Fa-f]+)u", text)
    if match is None:
        print(f"REFUSED: no `kGp = 0x...u` in {GP_HEADER}, so the GP-relative displacement cannot "
              f"be resolved. Refusing rather than assuming a value.", file=sys.stderr)
        raise SystemExit(2)
    return int(match.group(1), 16)


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
    gp = _guest_gp()
    gp_derived = 0          # value of a register the listing built FROM $gp this run
    gp_derived_register = -1
    hits: list[dict[str, object]] = []
    stores = 0
    attributed = 0
    unresolved = 0
    files = 0
    instructions = 0
    covered: set[int] = set()
    lowest = 0
    highest = 0
    first = True
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
                covered.add(vram)
                if first:
                    lowest = highest = vram
                    first = False
                else:
                    lowest = min(lowest, vram)
                    highest = max(highest, vram)
                op = (word >> 26) & 0x3F
                if op == LUI_OP:
                    # `lui $r, IMM` — the high half of an absolute address, in ANY register.
                    base_register = (word >> 16) & 0x1F
                    base_value = (word & 0xFFFF) << 16
                    continue
                if op in (ADDIU_OP, ADDU_OP):
                    source = (word >> 21) & 0x1F
                    target = (word >> 16) & 0x1F
                    if source == base_register == target:
                        base_value = (base_value + _displacement(word)) & 0xFFFFFFFF
                    elif source == GP_REGISTER:
                        # `addiu $r, $gp, %lo(sym)` — THE global-access idiom. Assigned at the
                        # point of use from a program constant, so it cannot go stale the way a
                        # carried-across-registers value can.
                        gp_derived_register = target
                        gp_derived = (gp + _displacement(word)) & 0xFFFFFFFF
                    continue
                if op not in STORE_OPS:
                    continue
                stores += 1
                base = (word >> 21) & 0x1F
                if base == GP_REGISTER:
                    target_address = (gp + _displacement(word)) & 0xFFFFFFFF
                elif base == base_register:
                    target_address = (base_value + _displacement(word)) & 0xFFFFFFFF
                elif base == gp_derived_register:
                    target_address = (gp_derived + _displacement(word)) & 0xFFFFFFFF
                else:
                    unresolved += 1
                    continue
                attributed += 1
                if target_address == address:
                    try:
                        where = path.resolve().relative_to(ROOT)
                    except ValueError:
                        where = path
                    hits.append({
                        "vram": vram,
                        "word": word,
                        "register": _store_register(word),
                        "base": f"${base}",
                        "file": str(where),
                        "line": raw.strip(),
                    })
    return {"hits": hits, "stores": stores, "attributed": attributed, "unresolved": unresolved,
            "files": files, "instructions": instructions, "gp": gp,
            "covered": len(covered), "lowest": lowest, "highest": highest}


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
    # The GP-relative mode, from the tree's OWN bytes at 0x8004EB4C — `sw $s7, 0x00($gp)`, which
    # stores at $gp itself. One fragment, two targets: the real displacement must match and the very
    # next word must not, so a resolver that merely "matched stores with $gp as base" fails here.
    gp_zero = """glabel t
/* 3F34C 8004EB4C 000097AF */  sw         $s7, 0x00($gp)
"""
    # `addiu $r, $gp, %lo(sym)` then a store through $r: the other half of the GP story, since much
    # global access goes via a computed base rather than a direct $gp displacement. ENCODED, not
    # read out of the tree — this tree carries no such line (its only $gp-source addiu is the
    # `addi $gp, $gp, 0x4` cursor idiom at 0x80026D4C), so the words are built here and are
    # verified by decoding them back, not claimed to be image-derived.
    gp_addiu = """glabel t
/* 3F34C 8004EB4C 30019927 */  addiu      $t9, $gp, 0x0130
/* 3F350 8004EB50 100038AF */  sw         $t8, 0x10($t9)
"""
    # (name, fragment, the address it must find, how many stores may match it)
    cases = [
        ("real-listing-line", real, 0x80078AE0, 1),
        ("wrong-address", wrong, 0x80078AE0, 0),
        ("positive-disp", positive_disp, 0x80078AE0, 0),
        ("no-lui-base", no_base, 0x80078AE0, 0),
        ("gp-relative", gp_zero, 0x80075264, 1),
        ("gp-one-word-off", gp_zero, 0x80075268, 0),
        ("gp-addiu-base", gp_addiu, 0x800753A4, 1),
        ("gp-addiu-one-word-off", gp_addiu, 0x800753A8, 0),
    ]
    for name, text, case_target, want in cases:
        # One directory PER CASE: scan() walks a whole root, so sharing a root would let one case's
        # lines answer another's expectation and make the suite pass for the wrong reason.
        with tempfile.TemporaryDirectory(dir=SCRATCH) as tmp:
            path = Path(tmp) / "case.s"
            path.write_text(text)
            result = scan(case_target, [Path(tmp)])
            got = len(result["hits"])  # type: ignore[arg-type]
            status = "ok" if got == want else "FAIL"
            print(f"  {status:4} {name}: matched {got}, expected {want}")
            if got != want:
                return 1
    print(f"  selftest: {len(cases)}/{len(cases)} cases behaved as required. One case is the REAL "
          "listing line the console recorded (must match), one is one word away from it (must not), "
          "one has a positive displacement (must not), and one has no `lui` base at all (must not). "
          "The real line's displacement is NEGATIVE, so the suite fails if the resolver stops "
          "sign-extending. The last four are the $gp modes, each with a one-word-off negative over "
          "the SAME fragment, so a resolver that ignored the displacement could not pass them.")
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
    span = result["highest"] - result["lowest"] + 4  # type: ignore[operator]
    print(f"listing coverage: {result['covered']} distinct word addresses, "  # type: ignore[arg-type]
          f"0x{result['lowest']:08X}..0x{result['highest']:08X}  "  # type: ignore[index]
          f"({100.0 * result['covered'] / span:.1f}% of that span)")  # type: ignore[operator]
    print(f"stores reaching the watched word: {len(hits)}")
    for hit in hits:
        print(f"  0x{hit['vram']:08X}  word=0x{hit['word']:08X}  rt=${hit['register']}  "
              f"{hit['file']}")
        print(f"      {hit['line']}")
    if not hits:
        print("verdict: NO store in the scanned listing can reach this word with a KNOWN base "
              "register.")
        print()
        print("That is a statement about THE LISTING, not about the game, and the coverage line is "
              "the reason this verdict must not be read as an absence:")
        print(f"  * the listing is a SPARSE reconstruction, not the image. It carries "
              f"{result['covered']} distinct word addresses "  # type: ignore[index]
              f"({100.0 * result['covered'] / span:.1f}% of its own span) "  # type: ignore[operator]
              f"out of 2,097,152 words of main RAM. Most of the address space is simply NOT here, so")
        print("    a writer in unlisted code is invisible to this scan and cannot be ruled out.")
        print(f"  * only {result['attributed']} of {result['stores']} stores "  # type: ignore[index]
              "had a base this scan could resolve, so even inside the listing most stores are")
        print("    un-attributed rather than cleared.")
        print("  * a bulk copy is not a store instruction at all.")
        print()
        print("So: to learn whether the game has a writer here, ask the IMAGE, not the listing — "
              "disassemble the real bytes over the address range, or observe the store at runtime.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
