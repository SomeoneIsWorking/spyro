#!/usr/bin/env python3
"""The BRANCH that `func_80039AA8` (the random emitter) takes, on both cores, at a chosen tick.

WHY THIS EXISTS. `tools/probe_tick_divergence.py --rand-calls` found the first divergence in the whole
Spyro 1 demo simulation: the update ENDING at g_GameTick 41 ran 25 `rand()` calls on the product and
27 on the console, and 37 updates before it agreed exactly. Per-call-site counts from the console's
own PC observer put the console's 27 as 2 (`Random` from a `ra` of 0x80037EBC) + 16 (four consecutive
`jal rand` in level_11/func_level_11_800892C4.s:700-720) + 9 (three consecutive `jal rand` in
level_11/func_level_11_80088098.s:650-668), and the granularity says only group A can lose exactly 2.
`Random` is `RandRange` at 0x80037EA0, whose body is `min + rand() % (max-min+1)` (verified against
the image: `jal 0x8006272C` at 0x80037EB4, `subu $s0,$s0,$s1` / `addiu $s0,$s0,1` / `div` at
0x80037EBC..0x80037EC4). So a record TAKEN AT 0x80037EA0 carries `ra` = the caller's resume address,
which is the call site + 4 -- that is what separates the six `jal RandRange` sites of
`func_80039AA8` from every other `Random` caller in the game, with no inference at all.

The emitter's own inputs AGREE on the two cores at tick 40 (`g_Spyro.m_Position` first differs at
tick 556), so this is not arithmetic on a differing input: it is a different branch, decided by a
word not yet named. This probe exists to name that word by MEASUREMENT:

  * `--observe` at the tick, arming up to four PCs. 0x80037EA0 gives `ra` -> the exact call site and
    `a0`/`a1` -> the two config bytes; 0x80039AA8 gives the emitter's `a0` (Moby*) and `a1` (config*)
    so the config block is a concrete address rather than a guess; 0x80039C68 is the instruction
    right after `jal func_80039398`, so `v0` is that function's return -- the `s1` the whole
    `s1 != 0` / `s1 == 0` split hangs on. Chosen because the observer takes at most four PCs and a
    function that is RE-ENTERED cannot be observed with `return: true` (CONSOLE.md: "Reentry while a
    return is pending is explicitly unsupported and makes the census incomplete"), and the emitter is
    re-entered once per moby.
  * `--find-instruction ADDR` scans ALL of main RAM on BOTH cores for `jal ADDR` and for `ADDR` as a
    big-endian DATA pointer. The decompiled listing carries no level-11 caller of `func_80039AA8`
    -- `grep -rn 39AA8 external/spyro-1/asm` names only twelve other overlays -- so the caller is
    either indirect or simply not in the tree, and an overlay is resident in RAM, so the instruction
    is findable even when the listing is not.
  * `--read ADDR:BYTES --read-at TICK[,TICK...]` reads an ARBITRARY guest range on both cores at
    chosen ticks and marks every differing byte, which is how the config block named by an observe
    record becomes a per-word side-by-side table at ticks 40 and 41.

The negative is the point. The counts print with the observer's own `scanned` denominator, the RAM
scan prints how many bytes it scanned and how many matched, and a run that finds no differing byte in
the config block says so with the range it actually read -- a zero from an instrument that read
nothing is not a zero.

    uv run --frozen python tools/probe_emitter_predicate.py --selftest
    uv run --frozen python tools/probe_emitter_predicate.py --find-instruction 0x80039AA8 --ticks 45
    uv run --frozen python tools/probe_emitter_predicate.py --ticks 45 --observe-at 41 \\
        --observe 0x80037EA0,0x80039AA8,0x80039C68,0x80039CD0:0x80075AC0:4
    uv run --frozen python tools/probe_emitter_predicate.py --ticks 45 \\
        --read 0x80075AC0:0x20 --read-at 40,41
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(ROOT / "external" / "psxport" / "tools" / "oracle"))

import compare
import compare_cores
import drive
import oracle_spyro1_demo as route
import probe_tick_divergence as probe

OUT_DIR = ROOT / "scratch" / "oracle" / "probe"

# `func_80037EA0` = `RandRange`. Address and body are read out of the provisioned image, not taken
# from the brief: tools/probe_guest_disasm.py --address 0x80037EA0 --count 16 decodes `jal 0x8006272C`
# (rand) at 0x80037EB4 with `ra` 0x80037EBC, then `subu $s0,$s0,$s1` / `addiu $s0,$s0,1` /
# `div $zero,$v0,$s0` at 0x80037EBC..0x80037EC4 -- `min + rand() % (max-min+1)`.
RANDRANGE = 0x80037EA0

# The ONE `jal RandRange` the console observer actually attributed in the update ending at tick 41, with
# the site's own source, so the measurement and the listing are in the same place.
#
#   func_level_11_8007DA78.s:6465   0x80083740  lw    $s0, 0x0($s3)      s0 = Moby->m_Props
#   func_level_11_8007DA78.s:6538   0x80083854  lui   $v0, %hi(D_80075794)
#   func_level_11_8007DA78.s:6539   0x80083858  lw    $v0, %lo(D_80075794)($v0)
#   func_level_11_8007DA78.s:6541   0x80083860  beqz  $v0, 0x80083910     GATE 1: D_80075794 != 0
#   func_level_11_8007DA78.s:6543   0x80083868  lw    $v0, 0x0($s0)      v0 = m_Props[0]
#   func_level_11_8007DA78.s:6545   0x80083870  bnez  $v0, 0x800838C8     GATE 2: m_Props[0] == 0
#   func_level_11_8007DA78.s:6547   0x80083878  addiu $a0, $zero, 0x3
#   func_level_11_8007DA78.s:6548   0x8008387C  jal   RandRange           THE SITE; ra = 0x80083884
#   func_level_11_8007DA78.s:6549   0x80083880    addiu $a1, $zero, 0x6   (its delay slot: a1 = 6)
#   func_level_11_8007DA78.s:6550   0x80083884  sw    $v0, 0x0($s0)      m_Props[0] = -1 (the latch)
#
# and the arm is reached only for substate 0: `jtbl_level_11_8007AC34` index 0 is `.L80083854_`
# (func_level_11_8007DA78.s:105), and the dispatch is `lbu $v1, 0x48($s3); sltiu $v0,$v1,9;
# sll $v0,$v1,2; lw $v0, jtbl($v0); jr $v0` at 0x80083828..0x8008384C. So the whole predicate is
# `Moby[0x48] == 0 AND D_80075794 != 0 AND m_Props[0] == 0`, and it is a ONE SHOT: the site stores
# m_Props[0] = -1, after which `bnez` at 0x80083870 is always taken and 0x800838C8 stores -1 back.
SITE_RANDRANGE = 0x8008387C
SITE_RA = 0x80083884
D_80075794 = 0x80075794
D_800757F4 = 0x800757F4
D_800756C4 = 0x800756C4             # g_DeltaTime, written per moby at 0x8007DB48
G_DELTA_TIME = 0x800756CC
MOBY_SUBSTATE = 0x48                # the jump-table index at 0x80083830
MOBY_PROPS_PTR = 0x00               # Moby->m_Props (include/moby.h:44 `void *m_Props`)
MOBY_FLAGS_42 = 0x42                # bits 0 and 1, stored to D_800757F4 / D_80075794 at 0x8007DB3C/54
MOBY_CLASS = 0x36
MOBY_STRIDE = 0x58
MOBY_ALIVE_LIMIT = 0x80            # the guest's own liveness test at 0x8007DB18
G_LEVEL_MOBYS = 0x80075828          # game.sbss.s: g_LevelMobys, the pool's low bound
G_DYN_MOBYS = 0x80075890            # game.sbss.s: g_DynMobys, the pool's high bound
RAM_END = 0x80200000


# `func_80039AA8` (asm/nonmatchings/moby_helpers/func_80039AA8.s), its six `jal RandRange` sites and
# the resume address each one produces. A record taken at RANDRANGE carries `ra` = site + 4, so this
# table is the ONLY thing needed to attribute a call to a source line, and the mapping is arithmetic
# on the listing rather than a judgement.
EMITTER = 0x80039AA8
EMITTER_SITES = {
    0x80039B34: "0x80039B30 RandRange(s0[0x11], s0[0x12]); sibling `jal rand` at 0x80039B38 reports "
                "ra=0x80039B3C, so 0x80039B30 and 0x80039B80 are the same `s0[0x17] == 0` block",
    0x80039B84: "0x80039B80 RandRange(s0[0x0F], s0[0x10]) (same s0[0x17] == 0 block as 0x80039B30)",
    0x80039C7C: "0x80039C78 RandRange(0x40, 0xC0), reached only when s5 != 0 AND s1 != 0",
    0x80039C98: "0x80039C94 RandRange(s0[0x0F], s0[0x10]), reached only when s5 != 0 AND s1 != 0",
    0x80039D88: "0x80039D84 RandRange(-0x30, 0x30), reached only when s1 == 0",
    0x80039DA4: "0x80039DA0 RandRange(s0[0x0F], s0[0x10]), reached only when s1 == 0",
    SITE_RA: f"0x{SITE_RANDRANGE:08X} RandRange(3, 6) in level 11's substate-0 arm -- THE SITE the "
             f"console reached in the update ending at tick 41; m_Props[0] is the tested latch",
}

# The emitter's own registers at ENTRY (asm/nonmatchings/moby_helpers/func_80039AA8.s:5-11):
# `addu $s3, $a0, $zero` and `addu $s0, $a1, $zero`, so on arrival a0 is the Moby* and a1 the config*.
GPR = {0: "zero", 1: "at", 2: "v0", 3: "v1", 4: "a0", 5: "a1", 6: "a2", 7: "a3",
       8: "t0", 9: "t1", 10: "t2", 11: "t3", 12: "t4", 13: "t5", 14: "t6", 15: "t7",
       16: "s0", 17: "s1", 18: "s2", 19: "s3", 20: "s4", 21: "s5", 22: "s6", 23: "s7",
       24: "t8", 25: "t9", 26: "k0", 27: "k1", 28: "gp", 29: "sp", 30: "fp", 31: "ra"}

# Instructions whose arrival answers one specific question, so a record is labelled with the question
# rather than a bare address. Every entry cites the listing line it comes from.
PC_QUESTIONS = {
    RANDRANGE: "RandRange(0x80037EA0) entry: ra names the CALL SITE, a0/a1 are the two range words",
    EMITTER: "func_80039AA8(0x80039AA8) entry: a0 = Moby*, a1 = config* (s3 = a0, s0 = a1)",
    0x80039C68: "0x80039C68, the instruction after `jal func_80039398`: v0 is its return, copied to s1 "
                "at 0x80039C64; `beqz $s1, 0x80039CC0` is the whole s1 != 0 / s1 == 0 split",
    0x80039CD0: "0x80039CD0, the delay slot of `jal OctDistance` at 0x80039CC8: a0 is the distance "
                "from g_Spyro that `slti $v0, $a0, 0x44C` at 0x80039CD4 tests (s1 == 0 path only)",
    0x80039C70: "0x80039C70 `beqz $s5, 0x80039E6C` -- the s5 gate in front of 0x80039C78/0x80039C94",
    0x80039C50: "0x80039C50, the a2/a3 setup for `jal func_80039398` at 0x80039C5C (s0[0x0E] twice)",
    0x80039C1C: "0x80039C1C `addu $s5, $zero, $zero` -- RotateMobyToAngle returned 0, so s5 == 0 and "
                "0x80039C78/0x80039C94 are UNREACHABLE for this call",
    0x80039C20: "0x80039C20, first instruction after that gate: s1 != 0 is established here",
}


def _signed(value: int) -> int:
    return value - (1 << 32) if value & 0x80000000 else value


def _read_word(core, address: int) -> int:
    return int.from_bytes(core.read(address, 4), "little")


def _report_pool_census(native, console, tick: int, block: bool = False) -> None:
    """The emitter's WHOLE predicate, evaluated on both cores for EVERY live moby in the pool.

    The predicate, from the listing, has no global in it once it is read per moby:

        0x80083830  sltiu $v0, $v1, 9 ; jr through jtbl_level_11_8007AC34[$v1]   -> Moby[0x48] == 0
        0x80083868  lw    $v0, 0x0($s0)  with s0 = Moby->m_Props                 -> m_Props[0] == 0
        0x80083870  bnez  $v0, 0x800838C8
        0x8007DB34  andi  $a0, $v0, 0x2  (v0 = Moby[0x42]) ; 0x8007DB3C sw -> D_80075794
        0x80083860  beqz  $v0, 0x80083910                                      -> (Moby[0x42] & 2) != 0

    D_80075794 is written from the moby being processed, so the gate sees THAT moby's `Moby[0x42] & 2`
    and not the value parked at the end of the update. Reading the global at a park -- which is what a
    first reading of the branch suggests -- measures the LAST live moby instead and is the wrong word.
    Both are printed, and the global is labelled as the last-moby residue so the two cannot be confused.

    The pool is walked in the guest's own 0x58 stride over [g_LevelMobys, g_DynMobys) in ONE read per
    core, so the whole census is a bounded number of round trips rather than one per slot. Every slot
    is compared: rows are printed when the two cores disagree about any field OR when the predicate is
    satisfiable on either core, and the denominators are printed with the filter's own counts."""
    spans = {}
    for core in (native, console):
        level = _read_word(core, G_LEVEL_MOBYS)
        dynamic = _read_word(core, G_DYN_MOBYS)
        spans[core.name] = (level, dynamic)
    print(f"[probe] MOBY POOL CENSUS at g_GameTick {tick}: the emitter's predicate "
          f"Moby[0x48] == 0 AND (Moby[0x42] & 2) != 0 AND m_Props[0] == 0, over "
          f"[g_LevelMobys, g_DynMobys) in the guest's own 0x{MOBY_STRIDE:02X} stride")
    for name, (level, dynamic) in spans.items():
        print(f"[probe]   {name}: g_LevelMobys=0x{level:08X} g_DynMobys=0x{dynamic:08X} -> "
              f"{(dynamic - level) // MOBY_STRIDE} slot(s)")
    if spans[native.name] != spans[console.name]:
        print("[probe]   the two pools do not start and end at the same addresses, so slot N is not "
              "the same moby on both sides; this census is REFUSED rather than compared")
        return
    base, dynamic = spans[native.name]
    span = dynamic - base
    raw = {core.name: core.read(base, span) for core in (native, console)}
    slots = span // MOBY_STRIDE
    globals_now = {name: {address: _read_word(core, address) for address in
                          (D_80075794, D_800757F4, D_800756C4, G_DELTA_TIME)}
                   for name, core in ((native.name, native), (console.name, console))}
    for address, label in ((D_80075794, "D_80075794 = the LAST live moby's (Moby[0x42] & 2), the "
                                         "residue the gate leaves behind"),
                           (D_800757F4, "D_800757F4 = the LAST live moby's (Moby[0x42] & 1)"),
                           (D_800756C4, "D_800756C4 = g_DeltaTime, rewritten per moby at 0x8007DB48"),
                           (G_DELTA_TIME, "g_DeltaTime")):
        values = {name: words[address] for name, words in globals_now.items()}
        print(f"[probe]   {label} (0x{address:08X}): "
              + " / ".join(f"{name} {value} (0x{value:X})" for name, value in values.items())
              + ("  [AGREE]" if len(set(values.values())) == 1 else "  [DIFFER]"))

    def fields(core_name: str, index: int) -> tuple[int, int, int, int]:
        offset = index * MOBY_STRIDE
        block = raw[core_name][offset:offset + MOBY_STRIDE]
        return (block[MOBY_SUBSTATE], block[MOBY_FLAGS_42],
                int.from_bytes(block[MOBY_CLASS:MOBY_CLASS + 2], "little", signed=True),
                int.from_bytes(block[MOBY_PROPS_PTR:MOBY_PROPS_PTR + 4], "little"))

    def latch(core, props: int) -> int | None:
        return _read_word(core, props) if RAM_END > props >= 0x80000000 else None

    live = 0
    candidates = 0
    differing = 0
    printed = 0
    for index in range(slots):
        native_fields = fields(native.name, index)
        console_fields = fields(console.name, index)
        if native_fields == console_fields and native_fields[0] != 0:
            continue
        # Moby[0x48] < 0x80 is the guest's own liveness test (0x8007DB18), and MobyAlloc's free-list
        # markers -1/-2/-3 all read >= 0x80 there, so a slot failing it is not a moby being updated.
        if native_fields[0] < MOBY_ALIVE_LIMIT or console_fields[0] < MOBY_ALIVE_LIMIT:
            live += 1
        native_reaches = native_fields[0] == 0 and (native_fields[1] & 2) != 0
        console_reaches = console_fields[0] == 0 and (console_fields[1] & 2) != 0
        mismatch = [label for label, n, c in zip(("substate", "flags42", "class", "m_Props"),
                                                native_fields, console_fields) if n != c]
        if native_reaches or console_reaches:
            candidates += 1
        if mismatch:
            differing += 1
        if not mismatch and not (native_reaches or console_reaches):
            continue
        block = base + index * MOBY_STRIDE
        native_latch = latch(native, native_fields[3]) if native_reaches or mismatch else None
        console_latch = latch(console, console_fields[3]) if console_reaches or mismatch else None
        native_fires = native_reaches and native_latch == 0
        console_fires = console_reaches and console_latch == 0
        verdict = ("BOTH cores would call RandRange" if native_fires and console_fires else
                   "ONLY the console would call RandRange" if console_fires else
                   "ONLY the product would call RandRange" if native_fires else
                   "neither fires: " + ("latch non-zero on both" if native_reaches and console_reaches
                                        else "substate/flag gate fails on the other core"))
        printed += 1
        print(f"[probe]     slot {index:>5} Moby 0x{block:08X}: substate[0x48] "
              f"{native_fields[0]}/{console_fields[0]} flags[0x42] 0x{native_fields[1]:02X}/"
              f"0x{console_fields[1]:02X} class[0x36] {native_fields[2]}/{console_fields[2]} "
              f"m_Props 0x{native_fields[3]:08X}/0x{console_fields[3]:08X} latch[m_Props[0]] "
              f"{_word_or_none(native_latch)}/{_word_or_none(console_latch)} -> {verdict}"
              + (f"; differing field(s): {', '.join(mismatch)}" if mismatch else ""))
        # The WHOLE 0x58 block, byte by byte, because a field-level report cannot say whether 0x42 was
        # written on its own or as part of a wider store -- and that is exactly what names the writer.
        # Off by default: with many --read-at ticks it buries the per-tick verdict this mode exists for.
        if not block:
            continue
        offset = index * MOBY_STRIDE
        native_block = raw[native.name][offset:offset + MOBY_STRIDE]
        console_block = raw[console.name][offset:offset + MOBY_STRIDE]
        differing_bytes = [position for position in range(MOBY_STRIDE)
                           if native_block[position] != console_block[position]]
        print(f"[probe]       differing byte offset(s) in the 0x{MOBY_STRIDE:02X} block: "
              + (", ".join(f"0x{position:02X}" for position in differing_bytes)
                 if differing_bytes else "none"))
        for position in range(0, MOBY_STRIDE, 4):
            native_word = int.from_bytes(native_block[position:position + 4], "little")
            console_word = int.from_bytes(console_block[position:position + 4], "little")
            mark = "**" if native_word != console_word else "  "
            print(f"[probe]       {mark} +0x{position:02X} native {native_word:08X} "
                  f"console {console_word:08X}")
    print(f"[probe]   {slots} pool slot(s) read on each core; {live} live (Moby[0x48] < 0x"
          f"{MOBY_ALIVE_LIMIT:02X}) on at least one core; {candidates} slot(s) pass the substate+flag "
          f"gate on at least one core; {differing} slot(s) differ in any field; {printed} row(s) "
          f"printed")
    print("[probe]   a row reading 'ONLY the console would call RandRange' IS a missing call: the "
          "site stores m_Props[0] = -1 at 0x80083884, so a moby that satisfies the whole predicate "
          "calls RandRange exactly once in the whole level and never again")


def _word_or_none(value: int | None) -> str:
    return "n/a" if value is None else f"{value:08X}"



def _pc_label(pc: int) -> str:
    if pc in EMITTER_SITES:
        return EMITTER_SITES[pc]
    return PC_QUESTIONS.get(pc, "observed PC")


def _report_records(records) -> None:
    """Every retained observation record, with the CALL SITE it belongs to named when the PC is a
    `RandRange` entry. The site table is the measurement: a record's `ra` is the caller's resume
    address, so a hit at 0x80039C98 IS a call to 0x80039C94 and no interpretation is involved."""
    by_site: dict[int, int] = {}
    for record in records:
        gpr = record["gpr"]
        pc = record["pc"]
        ra = gpr[31] if len(gpr) > 31 else 0
        registers = " ".join(f"{GPR.get(index, f'r{index}')}={gpr[index]:08X}"
                             for index in range(1, 8) if index < len(gpr))
        site = ""
        if pc == RANDRANGE and ra in EMITTER_SITES:
            by_site[ra] = by_site.get(ra, 0) + 1
            site = f"  <-- {EMITTER_SITES[ra]}"
        print(f"[probe]   pc=0x{pc:08X} {_pc_label(pc)}")
        print(f"[probe]     next_pc=0x{record['next_pc']:08X} instr={record['instruction']:08X} "
              f"ra={ra:08X} {registers}{site}")
        if record.get("ram"):
            print(f"[probe]     ram={record['ram']}")
    if by_site:
        print(f"[probe]   RandRange calls attributed to func_80039AA8's own sites: "
              + ", ".join(f"0x{pc:08X} x{count}" for pc, count in sorted(by_site.items())))
    others = sum(1 for record in records
                 if record["pc"] == RANDRANGE and record["gpr"][31] not in EMITTER_SITES)
    if others:
        print(f"[probe]   {others} RandRange record(s) came from a caller that is NOT one of the six "
              f"func_80039AA8 sites, so they are not attributable to the emitter; their `ra` values "
              f"are printed above")


def _find_instruction(core, target: int) -> tuple[list[int], list[int]]:
    """Every main-RAM word that is a `jal target`, and every big-endian word equal to `target`.

    The instruction encoding is computed, not looked up: `jal`'s imm26 is `(target - base) >> 2` with
    `base = (pc + 4) & 0xF0000000`, and for every caller in a PSX image `base` is 0x80000000, so the
    word is fixed. That is why the scan can be a byte search for four little-endian bytes rather than a
    disassembly of two megabytes, and why the DATA half is the big-endian form -- a PSX executable
    stores instructions little-endian and data big-endian, which is the same convention split
    tools/probe_guest_disasm.py checks its own formula against."""
    immediate = (target - 0x80000000) >> 2
    if immediate < 0 or immediate > 0x03FFFFFF:
        raise SystemExit(f"REFUSED: 0x{target:08X} is not reachable by `jal` from a PSX image")
    instruction = 0x0C000000 | immediate
    data = struct.pack(">I", target)
    raw = core.read(probe.RAM_BASE, probe.RAM_BYTES)
    calls = [probe.RAM_BASE + 4 * index
             for index in range(0, probe.RAM_WORDS)
             if struct.unpack_from("<I", raw, 4 * index)[0] == instruction]
    pointers = [probe.RAM_BASE + index
                for index in range(0, probe.RAM_BYTES - 3)
                if raw[index:index + 4] == data]
    return calls, pointers


def _report_find(native, console, target: int) -> None:
    print(f"[probe] CALLER SCAN for 0x{target:08X} over all of main RAM on both cores "
          f"({probe.RAM_BYTES} bytes, {probe.RAM_WORDS} words per core)")
    results = {}
    for core in (native, console):
        calls, pointers = _find_instruction(core, target)
        results[core.name] = (calls, pointers)
        print(f"[probe]   {core.name}: {len(calls)} `jal 0x{target:08X}` word(s) at "
              + (", ".join(f"0x{address:08X}" for address in calls) if calls else "none")
              + f"; {len(pointers)} big-endian 0x{target:08X} DATA pointer(s) at "
              + (", ".join(f"0x{address:08X}" for address in pointers) if pointers else "none"))
    if results[native.name][0] == results[console.name][0] == []:
        print("[probe]   NO direct `jal` to that address in either core's main RAM, so the caller is "
              "NOT a direct call: it is reached through a pointer, or the address is not reached at "
              "all in this reached state. The DATA half above is the next question, and it is "
              "reported either way rather than dropped")
    for name, (calls, pointers) in results.items():
        core = native if name == native.name else console
        for address in calls:
            window = max(probe.RAM_BASE, address - 16)
            raw = core.read(window, 24)
            print(f"[probe]   {name}: 16 bytes before and 8 after the call at 0x{address:08X}: "
                  + raw.hex(" ").upper() + f"  (the `jal` word itself is at +0x10)")


def _report_range(native, console, tick: int, address: int, size: int) -> None:
    """One arbitrary guest range on both cores, per word and per byte, at one tick."""
    native_raw = native.read(address, size)
    console_raw = console.read(address, size)
    print(f"[probe] RANGE 0x{address:08X}..0x{address + size:08X} ({size} bytes) at g_GameTick {tick}")
    differing = [index for index in range(size) if native_raw[index] != console_raw[index]]
    print(f"[probe]   {len(differing)} of {size} byte(s) differ"
          + (": offsets " + ", ".join(f"0x{index:02X}" for index in differing[:32])
             + (" ..." if len(differing) > 32 else "") if differing else " -- the two cores agree"))
    for offset in range(0, size, 4):
        if offset + 4 > size:
            break
        native_word = int.from_bytes(native_raw[offset:offset + 4], "little")
        console_word = int.from_bytes(console_raw[offset:offset + 4], "little")
        mark = "  " if native_word == console_word else "**"
        print(f"[probe]   {mark} +0x{offset:02X} native {native_word:08X} ({_signed(native_word)}) "
              f"console {console_word:08X} ({_signed(console_word)})")


def _parse_reads(text: str) -> list[tuple[int, int]]:
    if not text:
        return []
    out = []
    for item in text.split(","):
        if not item.strip():
            continue
        address, _, size = item.partition(":")
        if not size:
            raise SystemExit(f"REFUSED: --read wants ADDR:BYTES, got {item!r}")
        out.append((int(address, 0), int(size, 0)))
    return out


def _parse_ticks(text: str) -> list[int]:
    if not text:
        return []
    return [int(item, 0) for item in text.split(",") if item.strip()]


def selftest() -> int:
    """Both answers for every derived value this probe trusts: the site table must be the listing's
    arithmetic, the `jal` encoding must match a word taken from the image, and the record attributor
    must both accept a known site and refuse an unknown caller."""
    # The six `jal RandRange` sites, each read out of the listing text rather than retyped.
    listing = (ROOT / "external" / "spyro-1" / "asm" / "nonmatchings" / "moby_helpers"
               / "func_80039AA8.s").read_text()
    site_lines = {}
    for line in listing.splitlines():
        for name in ("0x80039B30", "0x80039B80", "0x80039C78", "0x80039C94", "0x80039D84",
                     "0x80039DA0"):
            # The listing prints the address bare (`/* 2A330 80039B30 A8DF000C */  jal ...`), so the
            # `0x` is stripped rather than searched for.
            if f" {name[2:]} A8DF000C */  jal        RandRange" in line:
                site_lines[int(name, 16)] = line
    if len(site_lines) != 6:
        print(f"[probe] selftest FAIL: expected 6 `jal RandRange` sites in the listing, found "
              f"{len(site_lines)}")
        return 1
    for site in site_lines:
        resume = site + 4
        if resume not in EMITTER_SITES:
            print(f"[probe] selftest FAIL: site 0x{site:08X} has no table row for ra=0x{resume:08X}")
            return 1
        if EMITTER_SITES[resume].split()[0] != f"0x{site:08X}":
            print(f"[probe] selftest FAIL: table row 0x{resume:08X} names {EMITTER_SITES[resume]}, "
                  f"not 0x{site:08X}")
            return 1
    # And each row must be keyed by the word the instruction actually encodes, so a mis-typed resume
    # address cannot survive: the listing prints the instruction word beside the address.
    for site, line in site_lines.items():
        word = line.split("*/")[0].split()[-1]
        little = int.from_bytes(bytes.fromhex(word), "little")
        resume = site + 4
        if ((little & 0x03FFFFFF) << 2) + 0x80000000 != RANDRANGE:
            print(f"[probe] selftest FAIL: 0x{site:08X} encodes {little:08X}, which is not `jal "
                  f"0x{RANDRANGE:08X}`")
            return 1
        if resume not in EMITTER_SITES:
            print(f"[probe] selftest FAIL: no row for ra 0x{resume:08X}")
            return 1
    print(f"[probe] selftest: all {len(site_lines)} `jal RandRange` sites in the listing decode to "
          f"`jal 0x{RANDRANGE:08X}`, and each resume address has a table row: "
          + ", ".join(f"0x{site + 4:08X}" for site in sorted(site_lines)))

    # The scanner, on a fixture of the REAL shape: `RAM_BYTES` long, so the scan runs the same loop it
    # runs against main RAM, with a real `jal` word, a real big-endian pointer, and two decoys -- a
    # `jal` to a nearby address and the same 26-bit immediate reached from a different base.
    target = 0x80039AA8
    immediate = (target - 0x80000000) >> 2
    fixture = bytearray(probe.RAM_BYTES)
    struct.pack_into("<I", fixture, 0x10, 0x0C000000 | immediate)
    struct.pack_into(">I", fixture, 0x20, target)
    struct.pack_into("<I", fixture, 0x30, 0x0C000000 | ((0x80039AB0 - 0x80000000) >> 2))
    struct.pack_into(">I", fixture, 0x40, 0x00BADC0D)

    class _Fake:
        def read(self, address, size):
            return bytes(fixture)

    calls, pointers = _find_instruction(_Fake(), target)
    if calls != [probe.RAM_BASE + 0x10]:
        print(f"[probe] selftest FAIL: scanner found {calls}, expected one call at 0x10")
        return 1
    if pointers != [probe.RAM_BASE + 0x20]:
        print(f"[probe] selftest FAIL: scanner found pointers {pointers}, expected one at 0x20")
        return 1
    print("[probe] selftest: the RAM scanner found exactly the planted `jal` word and the planted "
          "big-endian data pointer, and ignored the `jal` to a nearby address")

    # The attributor, both answers: a known site is named, an unknown caller is reported as not the
    # emitter's. A table that accepted everything would be a check that cannot fail.
    for ra, expected in ((0x80039C98, True), (0x80039DA4, True), (0x8007E288, False)):
        got = ra in EMITTER_SITES
        if got is not expected:
            print(f"[probe] selftest FAIL: ra=0x{ra:08X} attributed={got}, expected {expected}")
            return 1
    print("[probe] selftest PASS: the site table accepts 0x80039C98 and 0x80039DA4 and refuses "
          "0x8007E288, so a zero from it is a measurement")

    # The THREE claims the moby-list census rests on, each re-read from the listing so a typo in a
    # constant cannot survive: the substate dispatch is a `sltiu ...,9` jump table on Moby[0x48],
    # index 0 is the arm that holds the site, and the arm's latch is m_Props[0] tested with `bnez`.
    listing = (ROOT / "external" / "spyro-1" / "asm" / "nonmatchings" / "overlays" / "level_11"
               / "func_level_11_8007DA78.s").read_text()
    required = (
        (f"{MOBY_SUBSTATE:02X}($s3)", "the Moby[0x48] substate read at the dispatch"),
        ("0900622C", "`sltiu $v0, $v1, 9` -- the dispatch covers substate 0..8"),
        ("0880013C", "`lui $at, %hi(jtbl_level_11_8007AC34)` -- the dispatch is a jump table"),
        ("000002AE */  sw         $v0, 0x0($s0)", "the latch store m_Props[0] = -1"),
        ("15004014 */  bnez       $v0, .Llevel_11_800838C8", "the latch test `m_Props[0] != 0`"),
        (f"2B004010 */  beqz       $v0, .Llevel_11_80083910", "the D_80075794 gate"),
        ("0000708E */  lw         $s0, 0x0($s3)", "s0 = Moby->m_Props, the latch's base"),
        ("42006292 */  lbu        $v0, 0x42($s3)", "the Moby[0x42] read behind the D_80075794 flag"),
        ("02004430 */  andi       $a0, $v0, 0x2", "D_80075794 = Moby[0x42] & 2"),
        ("945724AC */  sw         $a0, %lo(D_80075794)($at)",
         "D_80075794 = Moby[0x42] & 2, written PER MOBY at 0x8007DB3C"),
    )
    for needle, what in required:
        if needle not in listing:
            print(f"[probe] selftest FAIL: the level-11 listing does not contain {what} "
                  f"({needle!r}); the census's predicate would be a guess")
            return 1
    table = listing.split("dlabel jtbl_level_11_8007AC34", 1)[1].split(".size", 1)[0]
    first_entry = table.splitlines()[1]
    if ".L80083854_" not in first_entry:
        print(f"[probe] selftest FAIL: jtbl_level_11_8007AC34 index 0 is {first_entry.strip()!r}, not "
              f".L80083854_; substate {0} would not reach the site")
        return 1
    print(f"[probe] selftest: the level-11 listing contains all {len(required)} claims the census "
          f"rests on, and jtbl index {0} is .L80083854_, the arm that holds 0x{SITE_RANDRANGE:08X}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ticks", type=int, default=45,
                        help="main-loop iterations to step in lockstep after arrival (default 45)")
    parser.add_argument("--executable", type=Path, default=Path("build/bin/spyro_port"))
    parser.add_argument("--binary", type=Path, default=Path("scratch/assets/spyro1/SCUS_942.28"))
    parser.add_argument("--bios", type=Path, default=ROOT.parent / "SCPH1001.BIN")
    parser.add_argument("--selftest", action="store_true",
                        help="check the site table and the RAM scanner on a fixture, driving nothing")
    parser.add_argument("--observe", default="",
                        help="console PC observation over one update: PC,PC,... with a RAM range of "
                             "address:bytes; armed at --observe-at and drained after ONE update")
    parser.add_argument("--observe-at", type=int, default=0, help="the tick to arm --observe at")
    parser.add_argument("--find-instruction", type=lambda text: int(text, 0), default=0,
                        help="scan all of main RAM on both cores for `jal ADDR` and for ADDR as a "
                             "big-endian data pointer, and report where they are. Answers 'who calls "
                             "it' for a function the decompiled listing carries no caller for")
    parser.add_argument("--read", default="",
                        help="guest ranges ADDR:BYTES[,...] to read on both cores")
    parser.add_argument("--read-at", default="",
                        help="the ticks to read --read at, e.g. 40,41; printed in the order given")
    parser.add_argument("--moby-pool", action="store_true",
                        help="at every --read-at tick, evaluate the emitter's WHOLE predicate -- "
                             "Moby[0x48] == 0 AND (Moby[0x42] & 2) != 0 AND m_Props[0] == 0 -- for "
                             "every live moby in [g_LevelMobys, g_DynMobys) on BOTH cores, and print "
                             "the rows that differ or that satisfy it. Needs no observer")
    parser.add_argument("--moby-block", action="store_true",
                        help="with --moby-pool, print each reported slot's whole 0x58 block per word "
                             "plus its differing byte offsets, so a byte that moved inside a wider "
                             "store is visible rather than guessed at")
    args = parser.parse_args()

    if args.selftest:
        return selftest()

    reads = _parse_reads(args.read)
    read_at = _parse_ticks(args.read_at)
    if bool(reads or args.moby_pool) != bool(read_at):
        print("REFUSED: --read/--moby-pool and --read-at are a pair; one without the other measures "
              "nothing", file=sys.stderr)
        return 2
    observe = probe._parse_observe(args.observe) if args.observe else None
    if observe and not args.observe_at:
        print("REFUSED: --observe needs --observe-at; an unarmed observer measures nothing",
              file=sys.stderr)
        return 2

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    disc = drive.disc_path()
    if not disc:
        print("REFUSED: no disc; set PSXPORT_SPYRO_DISC in the environment or .env", file=sys.stderr)
        return 2
    environment = drive.environment(disc)
    environment.update(compare.product_env(argparse.Namespace(product_env=[])))
    product = compare.Product(ROOT / args.executable, ROOT / args.binary, environment, ROOT,
                              Path(disc))
    product = compare.fresh_card(product, OUT_DIR)

    native = compare_cores.NativeReplSession(str(product.binary), str(product.executable),
                                             product.environment, str(product.cwd),
                                             OUT_DIR / "native_emitter.log")
    console = compare_cores.ConsoleSession(ROOT / "external" / "psxport", product.disc, args.bios,
                                           "na", OUT_DIR / "console_emitter.log")
    try:
        driver = compare.Driver(route)
        settle = None
        for core in (console, native):
            used, settle = route.reach_demo_playing(driver, core, route.FIELD_BUDGET, settle)
            print(f"[probe] {core.name}: demo_playing after {used} advance steps; "
                  f"session field count {core.frames}")
        print(f"[probe] {RANDRANGE:#010x} is RandRange and its body was read out of the image by "
              f"tools/probe_guest_disasm.py; a record there carries ra = call site + 4, and the six "
              f"func_80039AA8 resume addresses are tabulated in this file")
        for index in range(args.ticks):
            if observe and probe.native_words_tick(native) + 1 == args.observe_at:
                probe._arm_observer(console, observe)
            for core in (native, console):
                core.hold(frozenset())
                route.advance(core, 1)
            tick = probe.native_words_tick(native)
            if observe and tick == args.observe_at:
                drained = console._call({"command": "observe_read"})
                status = drained["status"]
                records = drained["records"]
                print(f"[probe] console observer after the update ending at g_GameTick {tick}: "
                      f"{len(records)} record(s); scanned={status['scanned']} "
                      f"matched={status['matched']} retained={status['retained']} "
                      f"dropped={status['dropped']} pairing_errors={status['pairing_errors']} "
                      f"{status['observation']}")
                for target in status.get("targets") or []:
                    print(f"[probe]   target 0x{target['pc']:08X}: entries={target['entries']} "
                          f"returns={target['returns']}  ({_pc_label(target['pc'])})")
                _report_records(records)
                console._call({"command": "observe_off"})
            for wanted in read_at:
                if tick == wanted:
                    if args.moby_pool:
                        _report_pool_census(native, console, tick, args.moby_block)
                    for address, size in reads:
                        _report_range(native, console, tick, address, size)
        if args.find_instruction:
            _report_find(native, console, args.find_instruction)
    finally:
        for core in (native, console):
            core.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
