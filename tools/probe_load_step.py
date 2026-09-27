#!/usr/bin/env python3
"""Is the product's `g_LoadStage` one step BEHIND the console at `demo_level_load`, or is the barrier
sampling one `LoadLevel` call apart inside the same guest iteration?

WHY THIS FILE EXISTS
--------------------
`tools/oracle_compare.py --policy demo` reports, at its `demo_level_load` checkpoint:

    load_stage (decisive): native 01 console 02

while every OTHER declared range at that checkpoint matches -- gamestate 13, title (mode 3, state 1,
sub_state 5), game_tick 0, level 11. Two readings are available and they need different owners:

  A. The product's loader advances the state machine differently. The fix belongs in a native
     override of the guest's load stepper.
  B. `g_LoadStage` is a step index whose value is only STABLE between `LoadLevel` calls, and the
     barrier can land inside a call. The two cores then run the SAME code and the two numbers are the
     same loader sampled at two points a few thousand instructions apart. No override is correct here;
     the value is simply not comparable at a barrier that can land mid-call.

`docs/project-state.md` asserts (A). This file exists to settle it by measurement instead, and the
three facts that settle it are read out of the IMAGE, not out of `external/spyro-1/asm`, which is a
`nonmatchings` reconstruction and is wrong in places.

WHAT THE IMAGE SAYS (every word below is disassembled from SCUS_942.28, and `--selftest` re-reads the
bytes and fails if any of them changed)
------------------------------------------------------------------------
`LoadLevel` is 0x80015370. `g_LoadStage` is 0x80075864, so `%hi` = 0x8007 and `%lo` = **+0x5864**
(positive -- 0x80075864 - 0x80070000; guessing 0x8584 addresses a different word and finds nothing).

    0x80015374  8C425864  lw   $v0, 0x5864($v0)   ; g_LoadStage
    0x80015398  28420002  slti $v0, $v0, 2         ; g_LoadStage < 2 ?
    0x8001539C  14404001  bnez $v0, 0x800153F0     ; so stage 1 SKIPS the CD gate
    0x800153F4  8C635864  lw   $v1, 0x5864($v1)   ; g_LoadStage, re-read for the dispatch
    0x800153FC  2C62000E  sltiu $v0, $v1, 0xE      ; 0..13
    0x80015410  8C220A88  lw   $v0, 0xA88($at)    ; jump table at 0x80010A88
    0x80015418  00004000  jr   $v0

The jump table's 14 entries are stored in the image's INSTRUCTION byte order, so each must be
byte-swapped to be read: case 1 -> 0x800155B4, case 2 -> 0x800155E4.

    0x800155B4  0C015ACA  jal  0x80056B28          ; case 1: KillSoundsAndMusic(0)
    0x800155C0  8C425864  lw   $v0, 0x5864($v0)   ; g_LoadStage
    0x800155D8  24420001  addiu $v0, $v0, 1
    0x800155E0  AC225864  sw   $v0, 0x5864($at)   ; g_LoadStage = 2
    0x800155E4  3C028007  lui  $v0, 0x8007         ; case 2 STARTS HERE -- no branch
    0x800155F4  1C69010C  jal  0x8005A470          ; SetOverlayPointers()
    0x8001569C  A659000C  jal  0x80016698          ; CDLoadAsync(overlay)
    0x800156A8  8C425864  lw   $v0, 0x5864($v0)
    0x800156B0  24420001  addiu $v0, $v0, 1
    0x800156AC  0A5800ED  j    0x800163B4          ; -> the shared tail, delay slot adds 1
    0x800163B4  3C010007  lui  $at, 0x8007
    0x800163B8  AC225864  sw   $v0, 0x5864($at)   ; g_LoadStage = 3
    0x800163BC  8FBF0078  lw   $ra, 0x78($sp)     ; the "blocked" exit, skips the store

**0x800155E4 is not a branch.** Case 1 falls straight through into case 2, so ONE `LoadLevel` call
entered at stage 1 writes 2 at 0x800155E0 and 3 at 0x800163B8 before it returns, and 2 exists only
BETWEEN those two stores -- inside `SetOverlayPointers` and a `CDLoadAsync`, thousands of
instructions wide. A sample that reads 2 was taken inside that call; a sample that reads 1 was taken
before 0x800155E0 in the same call, or at the end of the `TSS_Setup` iteration that wrote 1.

So the question is exactly: does each core execute 0x800155E0 and 0x800163B8, and where does each
core's field boundary fall relative to them. That is what this file measures, on both cores, with the
route's own `advance` and the route's own predicate, so its samples ARE the comparator's samples.

THE MEASUREMENT, AND WHAT CAN GO WRONG WITH IT
---------------------------------------------
  * CONSOLE: the reference's own read-only PC observer, armed BEFORE the title overlay enters demo
    mode on four PCs -- `LoadLevel`'s entry, case 1's stage store, the shared tail's stage store, and
    **case 0's entry, which the demo level load provably cannot reach** because `TSS_Setup` writes
    stage 1 for `TSD_DemoLevel`. The unreachable target is the negative control: the observer must
    report it unmatched WITH its `scanned` denominator, so "it fired" is not an answer the instrument
    was always going to give. Ranges carry `g_LoadStage` and the whole 24-byte `g_TitlescreenState`,
    so each record says what the load state was at that instant.
  * PRODUCT: the framework's translated-store observer (`PSXPORT_STORE_OBSERVE`) on the same three
    STORE instruction addresses plus the same unreachable one. It matches the guest PC of a translated
    store, so arming it on a DATA address would be a guaranteed miss; these are instruction addresses
    read out of the image above.
  * PER-FIELD SERIES: after the park, both cores are sampled every field for `--window` fields with
    the route's `advance(1)`, printing the stage, the title state, the field counter and the level id.
    The barrier's own reading is the first row; the rest shows where each core went next.

CONTROLS, both of which have bitten this repository before
----------------------------------------------------------
  1. The run REFUSES to report a verdict unless the state under test was entered: it refuses unless
     `g_LoadStage` was observed at BOTH 1 and 2 on the core that read 2, and unless the title
     overlay's `m_Mode` was 3 and `m_State` was 1 at the park. A run that never loaded anything
     proves nothing, and 0xFFFFFFFF is non-zero, so "non-zero" is not the test.
  2. The observer's own `scanned`/`matched`/`retained`/`dropped` counters are printed verbatim, and
     the product's store-observer end-of-run report is copied out of the log with its own
     `jit_instructions` denominator. A silent arming is a failure, not a clean result.

    uv run --frozen python tools/probe_load_step.py --selftest
    uv run --frozen python tools/probe_load_step.py --window 12
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(ROOT / "external" / "psxport" / "tools" / "oracle"))

import compare  # noqa: E402
import compare_cores  # noqa: E402
import drive  # noqa: E402
import guest_globals  # noqa: E402
import oracle_spyro1_demo as route  # noqa: E402

OUT_DIR = ROOT / "scratch" / "oracle" / "probe_load_step"
EXE = ROOT / "scratch" / "assets" / "spyro1" / "SCUS_942.28"

G_LOAD_STAGE = guest_globals.kLoadStage      # 0x80075864
G_TITLESCREEN = guest_globals.kTitlescreenState  # 0x80078D78
G_GAMESTATE = guest_globals.kGamestate
G_LEVEL_ID = guest_globals.kLevelId
G_LEVEL_TICKS = guest_globals.kLevelTicks
G_GAME_TICK = guest_globals.kGameTick

TEXT_OFF = 0x800
TEXT_LOAD = 0x80010000

# The three instructions whose execution the transition 1 -> 2 -> 3 rests on, and the one that
# cannot execute on this route. Read out of the image; `--selftest` re-reads every word.
LOADLEVEL_ENTRY = 0x80015370
CASE0_ENTRY = 0x80015420        # `jal KillSoundsAndMusic` in case 0 -- stage is 1 on entry, so
                                # the demo level load never dispatches here. THE NEGATIVE CONTROL.
STAGE_STORE_CASE1 = 0x800155E0  # `sw $v0, 0x5864($at)` -- case 1's inline increment, writes 2
STAGE_STORE_TAIL = 0x800163B8   # `sw $v0, 0x5864($at)` -- the shared tail, writes 3
JUMP_TABLE = 0x80010A88

TSS_LOADING = 1
TSM_DEMO = 3
STAGE_NOT_LOADING = 0xFFFFFFFF


# ---------------------------------------------------------------------------------------------
# The image facts, read from bytes rather than from a listing.
# ---------------------------------------------------------------------------------------------

def image_word(data: bytes, address: int) -> int:
    return struct.unpack_from("<I", data, TEXT_OFF + (address - TEXT_LOAD))[0]


def _printed(data: bytes, address: int) -> str:
    """The instruction word in the form `tools/probe_guest_disasm.py` prints it: the RAW FILE BYTES
    in order. `struct.pack(">I", little_endian_word)` reverses them, and a byte-reversed word is
    still a plausible-looking hex string -- so this prints the bytes, not a re-encoding of a decode."""
    off = TEXT_OFF + (address - TEXT_LOAD)
    return data[off:off + 4].hex().upper()


def is_stage_store(data: bytes, address: int) -> bool:
    """True when `address` is a `sw` of a 32-bit register to 0x80075864 built as %hi + %lo.

    The two fields that identify it live in different places in the two instructions, which is the
    whole reason this is a function and not an equality test on a hex string: a `sw` names its BASE
    register in bits 25..21, while a `lui` names its register in bits 20..16 and requires bits
    25..21 to be ZERO. So the base is found in the store and the matching `lui` is looked for behind
    it, within the 11-instruction window the guest's own code-loading idiom uses.
    """
    word = image_word(data, address)
    if (word >> 26) != 0x2B:  # sw
        return False
    if (word & 0xFFFF) != 0x5864:
        return False
    base = (word >> 21) & 0x1F
    for back in range(1, 12):
        lui = image_word(data, address - 4 * back)
        if (lui >> 26) == 0x0F and (lui >> 21) & 0x1F == 0 and ((lui >> 16) & 0x1F) == base \
                and (lui & 0xFFFF) == 0x8007:
            return True
    return False


def jump_table_entry(data: bytes, index: int) -> int:
    """One entry of LoadLevel's dispatch table. The entries are INSTRUCTION words stored in the
    image's little-endian instruction order, so reading them big-endian yields 0xB4550180 for case 1
    instead of 0x800155B4 -- a value that is not code and is the reason a table read has to say so."""
    off = TEXT_OFF + (JUMP_TABLE - TEXT_LOAD) + 4 * index
    return struct.unpack_from("<I", data, off)[0]


def image_facts(data: bytes) -> dict:
    return {
        "loadlevel_entry_his_stage": image_word(data, 0x80015374),
        "loadlevel_entry_lows_stage": image_word(data, LOADLEVEL_ENTRY),
        "case1_entry": jump_table_entry(data, 1),
        "case2_entry": jump_table_entry(data, 2),
        "case1_falls_through": image_word(data, 0x800155E4),
        "case1_entry_is_jal": (image_word(data, LOADLEVEL_ENTRY) >> 26) == 0x03,
    }


CASE2_ENTRY = 0x800155E4       # where case 1 lands
CASE2_TAIL_JUMP = 0x800156AC   # `j 0x800163B4`, the shared tail, with the increment in its delay slot
CASE2_END = 0x800156B0         # last instruction of case 2


def stage_store_sites(data: bytes, low: int, high: int) -> list[int]:
    """Every store to g_LoadStage in [low, high), in address order."""
    return [address for address in range(low, high, 4) if is_stage_store(data, address)]


def increment_before_store(data: bytes, store: int, load: int, addiu: int) -> bool:
    """True when the register stored at `store` is `$v0`, and `$v0` was last set by
    `addiu $v0, $v0, 1` at `addiu` with `g_LoadStage` loaded into it at `load` -- i.e. the store
    writes `stage + 1`, which is what makes the written value 2 on the case-1 path and 3 on the
    shared tail's."""
    stored = (image_word(data, store) >> 16) & 0x1F
    loaded = (image_word(data, load) >> 16) & 0x1F
    add = image_word(data, addiu)
    is_addiu = (add >> 26) == 0x09
    add_dst, add_src, add_imm = (add >> 16) & 0x1F, (add >> 21) & 0x1F, add & 0xFFFF
    return (stored == 2 and loaded == 2 and is_addiu and add_dst == 2 and add_src == 2
            and add_imm == 1)


# ---------------------------------------------------------------------------------------------
# Classification of a sample. Pure, so --selftest can exercise it without a product.
# ---------------------------------------------------------------------------------------------

def classify_sample(stage: int, title_mode: int, title_state: int) -> str:
    """Where a single reading of g_LoadStage was taken, given the title overlay's own state.

    The two facts that make this decidable come from the image: case 1's store of 2 at 0x800155E0 is
    followed at 0x800155E4 by a `lui` and NOT by a branch, and the shared tail overwrites it with 3
    at 0x800163B8 before `LoadLevel` returns. So a reading of 1 or 2 is a reading taken INSIDE a
    LoadLevel call, and which one says which side of 0x800155E0 the sampler was on.
    """
    if stage == STAGE_NOT_LOADING:
        return "sentinel 0xFFFFFFFF: not loading, so this is not a load-stage reading at all"
    if title_mode != TSM_DEMO:
        return (f"title mode {title_mode} is not TSM_Demo(3), so the demo flyby's loader is not the "
                "code running; this reading is not about the demo level load")
    if title_state == 0:
        return ("title state 0 is TSS_Setup, whose iteration is what WRITES stage 1; a reading of "
                f"{stage} here is at or before that write")
    if title_state == TSS_LOADING:
        if stage == 1:
            return ("stage 1 while TSS_Loading: only reachable BEFORE 0x800155E0, i.e. inside the "
                    "first LoadLevel call of this iteration or at the end of the TSS_Setup iteration "
                    "that wrote 1")
        if stage == 2:
            return ("stage 2 while TSS_Loading: 2 is written at 0x800155E0 and overwritten at "
                    "0x800163B8 before the call returns, so this reading was taken INSIDE case 2 of "
                    "that same call")
        return f"stage {stage} while TSS_Loading: past case 2, so this is a later LoadLevel call"
    return f"stage {stage} with title state {title_state}: the loading iteration has already finished"


def compare_barrier(native: dict, console: dict) -> dict:
    """Compare the two cores' barrier readings. `native`/`console` are dicts of the sampled words.

    The verdict names the two candidate causes rather than picking one:
      * `same-iteration-phase` -- the declared state is equal and the two stages are 1 and 2, which
        is what ONE LoadLevel call looks like from either side of 0x800155E0;
      * `loader-difference` -- anything else, which is what a loader that steps differently looks
        like, and is the only verdict an override may be written against.
    """
    declared = ("gamestate", "title_mode", "title_state", "title_sub_state", "title_tick",
                "title_sub_tick", "game_tick", "level_id")
    equal = [name for name in declared if native[name] == console[name]]
    unequal = [name for name in declared if native[name] != console[name]]
    stage_native = native["load_stage"]
    stage_console = console["load_stage"]
    inside = (native["title_state"] == TSS_LOADING == console["title_state"]
              and {stage_native, stage_console} == {1, 2})
    if inside and not unequal:
        verdict = "same-iteration-phase"
    else:
        verdict = "loader-difference"
    return {
        "verdict": verdict,
        "equal": equal,
        "unequal": unequal,
        "stage_delta": stage_console - stage_native,
        "native_where": classify_sample(stage_native, native["title_mode"], native["title_state"]),
        "console_where": classify_sample(stage_console, console["title_mode"], console["title_state"]),
    }


# ---------------------------------------------------------------------------------------------
# Selftest. Two halves, and the first half is the one that can fail.
# ---------------------------------------------------------------------------------------------

def selftest() -> int:
    failures: list[str] = []
    checks = 0

    def expect(condition: bool, label: str) -> None:
        nonlocal checks
        checks += 1
        if not condition:
            failures.append(label)

    if not EXE.exists():
        print(f"[loadstep] REFUSED: the provisioned image {EXE} is absent, and every claim this tool "
              f"makes is a claim about that image's bytes. 0 of the image checks were run; a pass "
              f"here would be vacuous.")
        return 2
    data = EXE.read_bytes()

    # HALF ONE: the bytes. If SCUS_942.28 ever stops matching these, the file's whole reading of the
    # guest is void, and the tool says so instead of measuring against a stale story.
    facts = image_facts(data)
    expect(facts["case1_entry"] == 0x800155B4,
           f"jump table case 1 is 0x{facts['case1_entry']:08X}, not 0x800155B4")
    expect(facts["case2_entry"] == 0x800155E4,
           f"jump table case 2 is 0x{facts['case2_entry']:08X}, not 0x800155E4")
    expect(facts["loadlevel_entry_his_stage"] == 0x8C425864,
           f"0x80015374 is not `lw $v0, 0x5864($v0)` "
           f"({_printed(data, 0x80015374)}); %lo(g_LoadStage) is not 0x5864 any more")
    expect(is_stage_store(data, STAGE_STORE_CASE1),
           f"0x800155E0 ({_printed(data, STAGE_STORE_CASE1)}) is not a store to g_LoadStage")
    expect(is_stage_store(data, STAGE_STORE_TAIL),
           f"0x800163B8 ({_printed(data, STAGE_STORE_TAIL)}) is not a store to g_LoadStage")
    expect(not is_stage_store(data, CASE0_ENTRY),
           f"0x80015420 ({_printed(data, CASE0_ENTRY)}) is a store to g_LoadStage, so it cannot serve "
           f"as the unreachable negative control")

    # The claim this whole file rests on, as three separate byte checks rather than one:
    #   1. case 1 stores `stage + 1`, so entering at stage 1 it writes 2;
    #   2. case 1 falls through into case 2 (0x800155E4 is not a control transfer);
    #   3. case 2's own body writes NOTHING to g_LoadStage, and its exit is the shared tail, which
    #      also stores `stage + 1` -- so 2 is overwritten with 3 before LoadLevel returns and is
    #      only ever visible BETWEEN 0x800155E0 and 0x800163B8.
    at_155e4 = image_word(data, CASE2_ENTRY)
    expect((at_155e4 >> 26) not in (0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07),
           f"0x800155E4 ({_printed(data, CASE2_ENTRY)}) is a control transfer, so case 1 does NOT "
           f"fall through into case 2 and stage 2 IS a resting value; the whole reading of this file "
           f"changes")
    expect(increment_before_store(data, STAGE_STORE_CASE1, 0x800155C0, 0x800155D8),
           f"0x800155E0 does not store `g_LoadStage + 1` (load 0x{0x800155C0:08X}="
           f"{_printed(data, 0x800155C0)}, addiu 0x{0x800155D8:08X}={_printed(data, 0x800155D8)}), so "
           f"the value it writes on the stage-1 path is not established")
    inner = stage_store_sites(data, CASE2_ENTRY, CASE2_END)
    expect(not inner,
           f"case 2's body [{CASE2_ENTRY:#010x},{CASE2_END:#010x}) also writes g_LoadStage at "
           f"{[_printed(data, a) for a in inner]}, so stage 2 is not confined to the window between "
           f"0x800155E0 and the shared tail")
    jump = image_word(data, CASE2_TAIL_JUMP)
    jump_target = ((jump & 0x03FFFFFF) << 2) | ((CASE2_TAIL_JUMP + 4) & 0xF0000000)
    expect(jump_target == 0x800163B4,
           f"0x800156AC targets 0x{jump_target:08X}, not the shared tail at 0x800163B4")
    expect(increment_before_store(data, STAGE_STORE_TAIL, 0x800156A8, CASE2_END),
           f"the shared tail does not store `g_LoadStage + 1` (load 0x{0x800156A8:08X}="
           f"{_printed(data, 0x800156A8)}, addiu 0x{CASE2_END:08X}={_printed(data, CASE2_END)}), so "
           f"the value it overwrites 2 with is not established")
    expect(stage_store_sites(data, LOADLEVEL_ENTRY, CASE2_ENTRY) == [STAGE_STORE_CASE1],
           f"the only store to g_LoadStage in case 1 is not 0x{STAGE_STORE_CASE1:08X}")

    # HALF TWO: the classification, on cases with known answers. The one that matters is the pair
    # the route actually produces: same declared state, stage 1 against stage 2. It must come out
    # `same-iteration-phase`, and a genuine step difference must NOT.
    def sample(stage: int, mode: int = TSM_DEMO, state: int = TSS_LOADING, **rest) -> dict:
        row = {"load_stage": stage, "gamestate": 13, "title_mode": mode, "title_state": state,
               "title_sub_state": 5, "title_tick": 5, "title_sub_tick": 3, "game_tick": 0,
               "level_id": 11}
        row.update(rest)
        return row

    inside = compare_barrier(sample(1), sample(2))
    expect(inside["verdict"] == "same-iteration-phase",
           f"stage 1 against 2 at an identical declared state is {inside['verdict']}")
    expect("BEFORE 0x800155E0" in inside["native_where"],
           f"stage 1 must be located before 0x800155E0, got {inside['native_where']!r}")
    expect("INSIDE case 2" in inside["console_where"],
           f"stage 2 must be located inside case 2, got {inside['console_where']!r}")

    # A real loader difference must NOT be laundered into the phase reading: stage 1 against 3 is
    # past case 2, which one call cannot produce.
    apart = compare_barrier(sample(1), sample(3))
    expect(apart["verdict"] == "loader-difference",
           f"stage 1 against 3 is {apart['verdict']}, which is not a loader difference")

    # Nor may a difference in the DECLARED state be hidden: at a different m_State the two samples
    # are not comparable at all, and saying `same-iteration-phase` would be a lie.
    elsewhere = compare_barrier(sample(1), sample(2, state=2))
    expect(elsewhere["verdict"] == "loader-difference",
           f"stage 2 at title state 2 is {elsewhere['verdict']}, which is not a loader difference")
    expect("title_state" in elsewhere["unequal"],
           f"the unequal declared words are {elsewhere['unequal']}, which does not name title_state")

    # The sentinel is not a load-stage reading, and a sample that never loaded must not be
    # classified as inside a call.
    expect("sentinel" in classify_sample(STAGE_NOT_LOADING, TSM_DEMO, TSS_LOADING),
           "the 0xFFFFFFFF sentinel must not be classified as a stage inside a LoadLevel call")
    expect("not TSM_Demo" in classify_sample(1, 0, TSS_LOADING),
           "a sample outside the demo flyby must say so")

    if failures:
        print(f"[loadstep] selftest FAIL: {len(failures)} of {checks} checks failed "
              f"({len(facts)} image facts, {checks - len(facts)} classification cases)")
        for label in failures:
            print(f"[loadstep]   - {label}")
        return 1
    print(f"[loadstep] selftest OK: {checks} checks -- {len(facts)} read out of {EXE.name}, "
          f"{checks - len(facts)} on the classification. The image agrees that case 1 falls through "
          f"into case 2 (0x800155E4 is {_printed(data, 0x800155E4)}, not a branch), so ONE "
          f"LoadLevel call entered at stage 1 writes 2 at 0x800155E0 and 3 at 0x800163B8 and stage 2 "
          f"exists only between them.")
    return 0


# ---------------------------------------------------------------------------------------------
# The run.
# ---------------------------------------------------------------------------------------------

def sample(core) -> dict:
    """One per-field reading, built from the ROUTE's own lens plus the two words it does not read.

    `route.observe` is the shipping observation the comparator itself takes, so every word of the
    declared state here came through the same reader and the same unpacking it uses. Adding two
    words to it is cheaper than writing a second reader, and a second reader is how two spellings of
    one field drift onto different memory -- which is what `game/core/guest_globals.h` exists to stop.
    """
    seen = route.observe(core)
    return {
        "load_stage": compare.u32(core, G_LOAD_STAGE),
        "level_ticks": compare.u32(core, G_LEVEL_TICKS),
        "gamestate": seen.gamestate,
        "title_mode": seen.title.mode,
        "title_state": seen.title.state,
        "title_tick": seen.title.tick,
        "title_sub_tick": seen.title.sub_tick,
        "title_sub_state": seen.title.sub_state,
        "level_id": seen.level,
        "game_tick": seen.game_tick,
    }


def arm_console_observer(core) -> None:
    targets = [LOADLEVEL_ENTRY, STAGE_STORE_CASE1, STAGE_STORE_TAIL, CASE0_ENTRY]
    status = core._call({
        "command": "observe",
        "targets": [{"pc": f"0x{pc:08X}", "return": False} for pc in targets],
        "ranges": [{"address": f"0x{G_LOAD_STAGE:08X}", "bytes": 4},
                   {"address": f"0x{G_TITLESCREEN:08X}", "bytes": 24}],
        "capacity": 128,
    })
    print(f"[loadstep] console observer armed on "
          f"{' '.join(f'0x{pc:08X}' for pc in targets)}: scanned={status['scanned']} "
          f"matched={status['matched']} retained={status['retained']} dropped={status['dropped']} "
          f"{status['observation']}")
    print(f"[loadstep]   0x{CASE0_ENTRY:08X} is the NEGATIVE CONTROL: LoadLevel's case 0, which this "
          f"route cannot enter because TSS_Setup writes stage 1 for TSD_DemoLevel. The observer must "
          f"report it unmatched WITH a scanned denominator, or the positive rows mean nothing.")


GPR_NAMES = {2: "v0", 8: "t0", 9: "t1", 25: "t9", 31: "ra"}


def drain_console_observer(core, label: str) -> list[dict]:
    """Drain the reference's observer, printing every record with the words it was asked to carry.

    The `ram` payload is the configured ranges concatenated in configuration order, so the slice
    boundaries are taken from the reply's own `ranges` list rather than from an assumption about
    order. The whole observation is `incomplete` for this run by construction: the negative control is
    never reached, and the reference reports that as a status instead of hiding it. That is why the
    per-target entry counts are printed as well -- `entries=0` against a large `scanned` is the
    negative control's answer, and it is the answer that makes the positive rows mean something.
    """
    result = core._call({"command": "observe_read"})
    status = result["status"]
    ranges = result["ranges"]
    print(f"[loadstep] observer drain at {label}: scanned={status['scanned']} "
          f"matched={status['matched']} retained={status['retained']} dropped={status['dropped']} "
          f"pairing_errors={status['pairing_errors']} {status['observation']}")
    for target in status["targets"]:
        print(f"[loadstep]   target 0x{target['pc']:08X}: entries={target['entries']} "
              f"returns={target['returns']}")
    rows: list[dict] = []
    for record in result["records"]:
        payload = bytes.fromhex(record["ram"])
        spans = {span["address"]: (payload[at:at + span["bytes"]], at)
                 for at, span in ((sum(s["bytes"] for s in ranges[:index]), span)
                                  for index, span in enumerate(ranges))}
        stage_bytes = spans.get(G_LOAD_STAGE, (b"", 0))[0]
        title_bytes = spans.get(G_TITLESCREEN, (b"", 0))[0]
        stage = int.from_bytes(stage_bytes, "little") if len(stage_bytes) == 4 else -1
        if len(title_bytes) == 24:
            mode = int.from_bytes(title_bytes[0:4], "little") & 0xFFFF
            state = (int.from_bytes(title_bytes[0:4], "little") >> 16) & 0xFFFF
            tick = int.from_bytes(title_bytes[8:12], "little")
            sub_tick = int.from_bytes(title_bytes[12:16], "little")
        else:
            mode = state = tick = sub_tick = -1
        gprs = record["gpr"]
        regs = " ".join(f"{GPR_NAMES[index]}=0x{gprs[index]:08X}" for index in sorted(GPR_NAMES)
                        if index < len(gprs))
        rows.append({"field": record["field"], "pc": record["pc"], "stage": stage, "mode": mode,
                     "state": state, "tick": tick, "sub_tick": sub_tick,
                     "instruction": record["instruction"], "v0": gprs[2] if len(gprs) > 2 else 0})
        print(f"[loadstep]   field={record['field']} pc=0x{record['pc']:08X} "
              f"insn=0x{record['instruction']:08X} load_stage={stage} title=({mode},{state}) "
              f"tick={tick} sub_tick={sub_tick} {regs}")
    return rows


STORE_LINE = re.compile(r"guest_pc=0x([0-9A-Fa-f]{8}) phase=(before|after).*seen=(\d+)")


def store_observer_rows(log: Path) -> dict:
    """Count the PRODUCT's own per-callback store-observer lines per armed store PC.

    The framework's env-driven surface emits one `lucent::debug("store-observe", ...)` line per
    observation, naming the guest PC of the translated store. The title never calls
    `store_observe_report`, so there is no end-of-run summary in the log: these lines ARE the
    measurement, and the count is per armed PC, so one PC firing cannot be read as another's.

    `LOADLEVEL_ENTRY` and `CASE0_ENTRY` are `jal`, not stores, so this surface cannot see them at all.
    That is stated rather than reported as a miss: a surface that only matches store PCs returning
    nothing for a `jal` is a property of the surface, and calling it a measurement is the exact
    confusion `docs/findings/diagnostics-that-cannot-lie.md` records.
    """
    counts: dict[str, int] = {}
    if not log.exists():
        print(f"[loadstep] product log {log} is absent, so the store-observer lines could not be "
              f"read. 0 lines were looked for in a file that does not exist, which is not a count "
              f"of 0 hits.")
        return {"__missing_log__": 1}
    total = 0
    for line in log.read_text(errors="replace").splitlines():
        match = STORE_LINE.search(line)
        if not match:
            continue
        total += 1
        key = match.group(1).upper()
        counts[key] = counts.get(key, 0) + 1
    print(f"[loadstep] product store-observer callback lines: {total} "
          f"(asked for: every line the product emitted with PSXPORT_DEBUG=store-observe)")
    for pc in (STAGE_STORE_CASE1, STAGE_STORE_TAIL):
        key = f"{pc:08X}"
        print(f"[loadstep]   store PC 0x{key}: {counts.get(key, 0)} line(s)")
    for pc in (LOADLEVEL_ENTRY, CASE0_ENTRY):
        key = f"{pc:08X}"
        print(f"[loadstep]   store PC 0x{key}: {counts.get(key, 0)} line(s), and that number is NOT a "
              f"measurement -- 0x{key} is a `jal`, this surface matches store instructions only, and "
              f"a zero here is the surface's shape rather than the guest's behaviour. The negative "
              f"control that CAN report a miss is the console observer's 0x{CASE0_ENTRY:08X} target.")
    return counts


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0],
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--window", type=int, default=12,
                        help="fields to keep sampling after each core's park (default: 12)")
    parser.add_argument("--executable", type=Path, default=Path("build/bin/spyro_port"))
    parser.add_argument("--binary", type=Path, default=Path("scratch/assets/spyro1/SCUS_942.28"))
    parser.add_argument("--bios", type=Path, default=ROOT.parent / "SCPH1001.BIN")
    parser.add_argument("--selftest", action="store_true",
                        help="read the image facts and exercise the classifier; drives nothing")
    args = parser.parse_args()
    if args.selftest:
        return selftest()

    if not EXE.exists():
        print(f"REFUSED: no provisioned image at {EXE}", file=sys.stderr)
        return 2
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    disc = drive.disc_path()
    if not disc:
        print("REFUSED: no disc; set PSXPORT_SPYRO_DISC in the environment or .env", file=sys.stderr)
        return 2

    # The product's own translated-store observer, armed on the two STORE instruction addresses the
    # image reading names, plus the unreachable PC so both sides of the run carry the same control.
    # These are instruction addresses on purpose: arming a DATA address would be a guaranteed miss
    # dressed up with a large denominator, which is the failure this repository published twice.
    store_pcs = [STAGE_STORE_CASE1, STAGE_STORE_TAIL, CASE0_ENTRY]
    environment = drive.environment(disc, TOOLS / "shipping_settings.ini")
    environment["PSXPORT_STORE_OBSERVE"] = ",".join(f"0x{pc:08X}" for pc in store_pcs)
    environment["PSXPORT_DEBUG"] = "store-observe"
    print(f"[loadstep] settings file: {TOOLS.name}/shipping_settings.ini "
          f"(PSXPORT_ASPECT={environment.get('PSXPORT_ASPECT', 'unset')}, "
          f"PSXPORT_FPS60={environment.get('PSXPORT_FPS60', 'unset')}). That file is shared and in "
          f"flight; this line is the run's own record of which configuration produced what follows.")
    print(f"[loadstep] product store observer armed on "
          f"{' '.join(f'0x{pc:08X}' for pc in store_pcs)} (instruction addresses, from the image)")

    product = compare.Product(ROOT / args.executable, ROOT / args.binary, environment, ROOT, Path(disc))
    product = compare.fresh_card(product, OUT_DIR)
    log_path = OUT_DIR / "native.log"
    native = compare_cores.NativeReplSession(str(product.binary), str(product.executable),
                                             product.environment, str(product.cwd), log_path)
    console = compare_cores.ConsoleSession(ROOT / "external" / "psxport", product.disc, args.bios,
                                           "na", OUT_DIR / "console.log")
    try:
        arm_console_observer(console)
        series: dict[str, list[dict]] = {"native": [], "console": []}
        park_index: dict[str, int] = {}
        observed: list[dict] = []
        budget = route.FIELD_BUDGET
        for index in range(budget):
            for name, core in (("console", console), ("native", native)):
                if name in park_index and len(series[name]) - park_index[name] > args.window:
                    continue
                seen = route.observe(core)
                core.hold(frozenset())
                route.advance(core, 1)
                row = sample(core)
                series[name].append(row)
                if name in park_index:
                    continue
                if index % 500 == 0:
                    print(f"[loadstep] {name} advance step {index + 1}: load_stage={row['load_stage']} "
                          f"gamestate={row['gamestate']} "
                          f"title=({row['title_mode']},{row['title_state']},"
                          f"{row['title_sub_state']}) tick={row['title_tick']} "
                          f"level={row['level_id']}")
                if route._demo_owns_title(seen):
                    # The park IS the comparator's comparison point: `Driver.drive` advances one more
                    # step at arrival before it returns, and the step above is that one.
                    park_index[name] = len(series[name]) - 1
                    print(f"[loadstep] {name} PARKED at advance step {index + 1} "
                          f"(field {core.frames}): load_stage={row['load_stage']} "
                          f"gamestate={row['gamestate']} "
                          f"title=({row['title_mode']},{row['title_state']},"
                          f"{row['title_sub_state']}) tick={row['title_tick']} "
                          f"sub_tick={row['title_sub_tick']} level={row['level_id']} "
                          f"level_ticks={row['level_ticks']} game_tick={row['game_tick']}")
                    if name == "console":
                        observed = drain_console_observer(console, "the console's own park")
            if len(park_index) == 2 and all(
                    len(series[name]) - park_index[name] > args.window for name in series):
                break
        missing = [name for name in series if name not in park_index]
        if missing:
            print(f"[loadstep] REFUSED: {missing} never satisfied the route's own `_demo_owns_title` "
                  f"within {budget} advance steps, so nothing was measured. The other core's arrival "
                  f"does not stand in for it.")
            return 2

        native_row = series["native"][park_index["native"]]
        console_row = series["console"][park_index["console"]]
        verdict = compare_barrier(native_row, console_row)
        print(f"[loadstep] declared state at the barrier -- equal: {verdict['equal']}; "
              f"UNEQUAL: {verdict['unequal'] or 'none'}")
        print(f"[loadstep] load_stage: native {native_row['load_stage']} / "
              f"console {console_row['load_stage']} (delta {verdict['stage_delta']:+d})")
        print(f"[loadstep]   native  {native_row['load_stage']}: {verdict['native_where']}")
        print(f"[loadstep]   console {console_row['load_stage']}: {verdict['console_where']}")
        print(f"[loadstep] VERDICT: {verdict['verdict']}")
        if verdict["verdict"] == "same-iteration-phase":
            print("[loadstep]   stage 1 and stage 2 are the two sides of ONE LoadLevel call: the "
                  "product's field boundary landed before 0x800155E0 and the console's after it. The "
                  "loader has not stepped differently; the value is not stable inside a call, and the "
                  "barrier compared it there.")

        print(f"[loadstep] per-field series from each core's park ({args.window} further fields):")
        print(f"[loadstep]   {'f':>3}  {'core':<8} {'stage':>5} {'gstate':>6} {'mode':>4} "
              f"{'state':>5} {'sub':>3} {'tick':>4} {'stk':>4} {'lticks':>6} {'level':>5} "
              f"{'gtick':>5}")
        for offset in range(0, args.window + 1):
            for name in ("native", "console"):
                at = park_index[name] + offset
                if at >= len(series[name]):
                    continue
                row = series[name][at]
                print(f"[loadstep]   {offset:>3}  {name:<8} {row['load_stage']:>5} "
                      f"{row['gamestate']:>6} {row['title_mode']:>4} {row['title_state']:>5} "
                      f"{row['title_sub_state']:>3} {row['title_tick']:>4} {row['title_sub_tick']:>4} "
                      f"{row['level_ticks']:>6} {row['level_id']:>5} {row['game_tick']:>5}")

        print(f"[loadstep] console observer records, in the order the reference reached them "
              f"({len(observed)} record(s); `field` is the reference's own retro_run index, and its "
              f"park is the advance step its series row 0 came from):")
        for record in observed:
            where = classify_sample(record["stage"], record["mode"], record["state"])
            print(f"[loadstep]   field {record['field']} pc=0x{record['pc']:08X} "
                  f"load_stage={record['stage']} v0=0x{record['v0']:08X} -- {where}")
        if not observed:
            print("[loadstep]   NONE. The observer was armed on three reachable PCs and reported no "
                  "record, so this run cannot say where the reference was. The empty list is the "
                  "answer, not a missing field.")
        return 0
    finally:
        for core in (console, native):
            try:
                core.close()
            except Exception:  # noqa: BLE001 - teardown must not mask the measurement
                pass
        store_observer_rows(log_path)


if __name__ == "__main__":
    raise SystemExit(main())
