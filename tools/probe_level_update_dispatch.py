#!/usr/bin/env python3
"""Name and measure the per-level update dispatch: how a level's update function is reached, and
whether the product reaches it.

WHY THIS EXISTS. `docs/issues/0133` concluded, from a validated-looking store-observer report, that
"the level overlay's update function is never entered" and that the guest's moby list is therefore
never filled. That conclusion was an artefact of what the instrument matches. `PSXPORT_STORE_OBSERVE`
compares `target.guestPc != guestPc` inside `LightrecExecutor::Impl::observeStore`, so its targets are
the PCs OF STORE INSTRUCTIONS. The 0133 runs armed it on DATA addresses — `0x800700F4`,
`0x80077868`, `0x80077870`, `0x80077880` — and got "MATCHED NONE of 116,056,872 executed JIT
instruction(s)". A data address can never equal a store instruction's PC, so that line is a tautology:
it would print the same for a word that changes every frame. The framework now says so
in the knob's own help text ("these are the PCs OF store instructions, NOT the guest words they write",
`config.cpp`), and its report row was changed to name the armed value as a `store PC` and to print
`EXECUTED: N store(s)`, so an armed data address can no longer be read as a hit.

THE DISPATCH IS NOT COMPUTED AND NOT A POINTER TABLE. It is a plain function pointer:

  * WRITER — `SetOverlayPointers`, a `switch (g_LevelId)` whose 100-entry jump table lives in the main
    image. It stores the level's update function into the global `g_UpdateMoby` with
    `lui`/`addiu`/`sw`. There are 43 such stores, one per implemented level; the other 57 level ids
    share the tail at 0x8005B6E0. Nothing here is address-reusing-WAD-sensitive: the writer is in the
    MAIN image and the values are the level overlay's fixed addresses.
  * READER — every `jalr` on a register just loaded from `g_UpdateMoby`. There are four, and only one
    of them is on the path this port actually takes.

Everything below is DERIVED from the provisioned executable and CHECKED against the committed
decompilation, so a wrong derivation fails here instead of becoming a story:

  * `g_UpdateMoby`'s address comes from `external/spyro-1/asm/data/game.sbss.s`;
  * the dispatch sites come from scanning the image for the `lw` at that displacement and the `jalr`
    that consumes the loaded register;
  * the per-level map comes from the image's own jump table plus the `lui`/`addiu` pair feeding each
    store, and every level is compared with `external/spyro-1/src/overlay_pointers.c`.

THE PARK IS NOT AN OBSERVATION POINT, and this tool says so instead of printing a zero. The guest
fills its moby update list inside the level update and consumes it there, then the DRAW pass clears
the same region: `func_8002B9CC` calls the C `memset` with `$a0 = 0x8006FCF4` and `$a2 = 0x1C00`
(verified by disassembly, and reproduced faithfully by this repo's
`spyro::field_scene_recipe::applyEnvironment`). The list base `0x800700F4` is `+0x400` inside that
region, so a read taken at a frame boundary is expected to be zero on BOTH cores and proves nothing
about the update. This tool therefore measures the update from INSIDE it, by arming the store
observer on the filler's own unconditional first store and on the consumer's flush store, which is
only reachable inside the list walk.

    uv run --frozen python tools/probe_level_update_dispatch.py --selftest
    uv run --frozen python tools/probe_level_update_dispatch.py
    uv run --frozen python tools/probe_level_update_dispatch.py --settle 200
"""

from __future__ import annotations

import argparse
import re
import runpy
import struct
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
DECOMP = ROOT / "external" / "spyro-1"
EXE = ROOT / "scratch" / "assets" / "spyro1" / "SCUS_942.28"
sys.path.insert(0, str(TOOLS))

# The file->VRAM formula and its verification belong to probe_guest_disasm.py, which already owns
# them, names the listing files it excludes and why, and reports a per-file disagreement instead of
# averaging one away. Re-deriving them here would be a second implementation of a claim that has
# already been wrong twice, so this tool USES that one and refuses if it does not verify.
DISASM = runpy.run_path(str(TOOLS / "probe_guest_disasm.py"))
TEXT_FILE_OFFSET = DISASM["TEXT_FILE_OFFSET"]
TEXT_LOAD_ADDRESS = DISASM["TEXT_LOAD_ADDRESS"]

# Guest symbols, read out of the committed decompilation rather than typed here. The decomp is this
# repository's registry; a hardcoded hex address with no owner is exactly what the project's working
# discipline forbids.
SYMBOL_SOURCES = {
    "g_UpdateMoby": (DECOMP / "asm" / "data" / "game.sbss.s", r"dlabel\s+g_UpdateMoby"),
    "g_LevelId": (DECOMP / "asm" / "data" / "game.sbss.s", r"dlabel\s+g_LevelId"),
    "func_80051FEC": (DECOMP / "asm" / "moby_lists.s", r"glabel\s+func_80051FEC"),
    "func_800522C0": (DECOMP / "asm" / "moby_lists.s", r"glabel\s+func_800522C0"),
}

# Structural constants, each with the reason it is a constant and not a guess. The guest builds the
# list cursor as `lui $t6, 0x8007` / `addiu $t6, $t6, -0x30C` / `addi $t6, $t6, 0x400`, so the list
# base is derived from that SHAPE below rather than typed: it is 0x80070000 - 0x30C + 0x400.
LIST_CURSOR_HIGH = 0x8007
LIST_CURSOR_BIAS = 0x30C
LIST_CURSOR_STEP = 0x400
LIST_BASE = ((LIST_CURSOR_HIGH << 16) - LIST_CURSOR_BIAS + LIST_CURSOR_STEP) & 0xFFFFFFFF
# Instruction opcodes this tool decodes by hand, so they are named rather than spelled inline.
OP_LUI = 0x0F
OP_ADDI = 0x08
OP_ADDIU = 0x09
OP_LW = 0x23
OP_SW = 0x2B
OP_JAL = 0x03
OP_JALR = 0x00
JALR_FUNC = 0x09  # `jalr $rs`, which links through $ra
JR_FUNC = 0x08    # `jr $rs`, which does not
# The switch's table is indexed by `g_LevelId` and this title's enum is 0..99, so the table is 100
# entries. The count is CHECKED against the image rather than assumed, and a candidate base whose
# entries are not all code addresses inside this image is rejected.
SWITCH_TABLE_ENTRIES = 100
# MIPS register numbers, so the cursor check names the register instead of spelling a bare 6 (which
# is $a2, and would have matched a completely different instruction).
REG_T6 = 14
REG_T5 = 13
REG_A2 = 6
# `func_8002B9CC` is the guest's own clear of the region the moby list lives in, and the tool reads
# its base and length OUT of the image rather than carrying them here: a second copy of a guest
# constant is how two answers to "what does this clear" start to disagree. The address is the entry
# `guest_clear_region` starts decoding from.
GUEST_CLEAR_FUNC = 0x8002B9CC

PSX_GAMESTATE_PLAYING = 0
# The control channel caps one `rw` at 64 words (dbg_server.cpp caps it and returns the truncated
# count WITHOUT saying so), so a request for more silently looks complete. Chunk here.
MAX_CHANNEL_WORDS = 64
MAX_LIST_WORDS = 64


class Image:
    """The provisioned executable, with the two readers and a self-check on the file formula."""

    def __init__(self, path: Path):
        if not path.is_file():
            raise FileNotFoundError(
                f"{path} is not there. This probe needs the authenticated executable; provision it "
                f"with tools/provision_title.py --title spyro1 --discdump <discdump> <disc>."
            )
        self.path = path
        self.data = path.read_bytes()
        self.text_end = TEXT_LOAD_ADDRESS + ((len(self.data) - TEXT_FILE_OFFSET) // 4) * 4

    def file_offset(self, address: int) -> int:
        return TEXT_FILE_OFFSET + (address - TEXT_LOAD_ADDRESS)

    def word(self, address: int) -> int:
        """One 32-bit word. PSX instructions AND this title's jump tables are little-endian."""
        offset = self.file_offset(address)
        if offset < 0 or offset + 4 > len(self.data):
            raise IndexError(f"0x{address:08X} is outside {self.path.name}")
        return struct.unpack_from("<I", self.data, offset)[0]

    def opcode(self, address: int) -> int:
        return self.word(address) >> 26

    def imm(self, address: int) -> int:
        return self.word(address) & 0xFFFF

    def rt(self, address: int) -> int:
        return (self.word(address) >> 16) & 0x1F

    def rs(self, address: int) -> int:
        return (self.word(address) >> 21) & 0x1F

    def sext16(self, value: int) -> int:
        return value - (1 << 16) if value & 0x8000 else value

    def scan_instructions(self):
        for address in range(TEXT_LOAD_ADDRESS, self.text_end, 4):
            yield address, self.word(address)

    def table_is_plausible(self, base: int) -> bool:
        """Whether `base` is a jump table of SWITCH_TABLE_ENTRIES code addresses in this image.

        Without this a mis-derived base reads as a table of plausible-looking words and every level
        maps to nonsense, which is exactly the kind of confident wrong answer this tool exists to stop.
        """
        if not TEXT_LOAD_ADDRESS <= base < self.text_end:
            return False
        for index in range(SWITCH_TABLE_ENTRIES):
            entry = self.word(base + 4 * index)
            if not TEXT_LOAD_ADDRESS <= entry < self.text_end:
                return False
        return True

    def offset_check(self) -> int:
        """Run the owning tool's verification of the file formula and return its agreement count.

        The formula is a CLAIM until probe_guest_disasm.py's `verify` says otherwise, and that tool
        prints its own denominator and names any file that disagrees. This returns only the agreement
        count so the caller can refuse on zero; the detail is already on stdout by then.
        """
        return int(DISASM["verify"](self.data))


def symbol(name: str) -> int:
    """A guest address read out of the committed decompilation, not typed here.

    The listing's convention is `dlabel NAME` / `glabel NAME` on its own line and the address in the
    `/* FILEOFF VRAMADDR [INSNWORD] */` comment on the NEXT one, so the address is taken from that
    comment's SECOND group. Taking the first group would silently yield a file offset, which is a
    plausible-looking wrong answer rather than an error.
    """
    path, pattern = SYMBOL_SOURCES[name]
    regex = re.compile(pattern)
    comment = re.compile(r"/\*\s*([0-9A-Fa-f]+)\s+([0-9A-Fa-f]{8})")
    lines = path.read_text(errors="replace").splitlines()
    for index, line in enumerate(lines):
        if not regex.search(line):
            continue
        for follow in lines[index + 1:index + 4]:
            match = comment.search(follow)
            if match:
                return int(match.group(2), 16)
    raise LookupError(f"{name} not found in {path}")


def dispatch_sites(image: Image, global_address: int) -> list[dict[str, int]]:
    """Every `lw $r, disp($base)` that loads `global_address`, with the `jalr` that consumes it.

    A consumer may hold the pointer in any register and call it any number of instructions later, so
    this pairs each load with the FIRST `jalr` on the same register within a bounded window, and
    reports the window it used. Reporting the bound matters: a longer window would merge two
    unrelated `jalr`s, a shorter one would miss a call that goes through arithmetic.
    """
    low = global_address & 0xFFFF
    high = global_address >> 16
    found: list[dict[str, int]] = []
    for address, _ in image.scan_instructions():
        if image.opcode(address) != OP_LW or image.imm(address) != low:
            continue
        register = image.rt(address)
        # The `lui` that supplies the base must be the instruction that pairs with this load.
        base_low = None
        for back in range(1, 9):
            prior = address - 4 * back
            if prior < TEXT_LOAD_ADDRESS or image.opcode(prior) != OP_LUI:
                continue
            if image.imm(prior) != high:
                continue
            if image.rs(address) != image.rt(prior):
                continue
            base_low = prior
            break
        if base_low is None:
            continue
        call = None
        for ahead in range(1, 9):
            later = address + 4 * ahead
            if later >= image.text_end:
                break
            word = image.word(later)
            # `jalr $rs` encodes its target in the rs field, links through $ra, and has no rt: matching
            # on the whole word would miss every one of them.
            if word >> 21 == register and word & 0x3F == JALR_FUNC:
                call = later
                break
        found.append({
            "load": address,
            "lui": base_low,
            "register": register,
            "jalr": call if call is not None else 0,
        })
    return found


def per_level_update_map(image: Image, global_address: int) -> dict[int, dict[str, int]]:
    """`g_LevelId` -> {store PC, stored value, case body}, from the image alone.

    The stores are found by their displacement, the value each one writes by the `lui`/`addiu` pair
    twelve and eight bytes earlier, and the level id each one belongs to by the switch's own 100-entry
    jump table — whose base is read out of the function's `lui 0x8001 / lw $v0, disp($at) / jr $v0`
    sequence rather than assumed. A level id with no case body shares the tail and stores nothing,
    which is reported rather than skipped.
    """
    low = global_address & 0xFFFF
    stores: list[int] = []
    for address, _ in image.scan_instructions():
        if image.opcode(address) == OP_SW and image.imm(address) == low:
            stores.append(address)
    if not stores:
        raise LookupError(f"no `sw` to 0x{global_address:08X} in the main image")

    # A case body is named by the switch's jump table, so the mapping runs table -> first store at or
    # after it. Deriving the body from the store instead (by walking backwards for the previous store)
    # lands 0x10 past the real body start, because the body opens with two stores before this one, and
    # a table lookup against a shifted address matches nothing and looks like an absence.
    table_base = switch_table_base(image, min(stores))
    if table_base == 0:
        raise LookupError("the switch's jump table was not found, so no level id can be mapped")
    result: dict[int, dict[str, int]] = {}
    shared_tail = 0
    for level in range(SWITCH_TABLE_ENTRIES):
        target = image.word(table_base + 4 * level)
        store = next((s for s in stores if s >= target), None) if target else None
        if store is None:
            shared_tail += 1
            continue
        value = ((image.imm(store - 12) & 0xFFFF) << 16) + image.sext16(image.imm(store - 8))
        result[level] = {"body": target, "store": store, "value": value & 0xFFFFFFFF}
    result[-1] = {"body": 0, "store": 0, "value": shared_tail}
    return result


def switch_table_base(image: Image, body: int) -> int:
    """The `switch`'s jump-table address, read out of the `jr $rd` block that dispatches it.

    `jr $rd` and `jalr $rd` share an opcode, and `jr` is the one whose link field is clear, so the two
    are told apart by the func field and not by the opcode. Every candidate base is then CHECKED by
    requiring all 100 of its entries to be addresses inside this image's own text: a wrong base
    yields plausible-looking words, so a shape match alone is not enough.
    """
    for back in range(0, 4096, 4):
        address = body - back
        if address < TEXT_LOAD_ADDRESS:
            break
        word = image.word(address)
        if word >> 26 != OP_JALR or word & 0x3F != JR_FUNC:
            continue
        target_register = word >> 21
        for back2 in range(4, 80, 4):
            prior = address - back2
            if prior < TEXT_LOAD_ADDRESS:
                break
            # The pair is `lui $base, hi` then `lw $rd, disp($base)`, and the base register must match.
            if image.opcode(prior) != OP_LW or image.rt(prior) != target_register:
                continue
            for back3 in range(4, 40, 4):
                lui = prior - back3
                if lui < TEXT_LOAD_ADDRESS:
                    break
                if image.opcode(lui) != OP_LUI or image.rt(lui) != image.rs(prior):
                    continue
                candidate = ((image.imm(lui) << 16) + image.sext16(image.imm(prior))) & 0xFFFFFFFF
                if image.table_is_plausible(candidate):
                    return candidate
    return 0


def decomp_level_map() -> dict[int, int]:
    """`overlay_pointers.c`'s own table, the cross-check for the map derived from the image."""
    text = (DECOMP / "src" / "overlay_pointers.c").read_text(errors="replace")
    return {
        int(m.group(1)): int(m.group(2), 16)
        for m in re.finditer(r"g_UpdateMoby\s*=\s*func_level_(\d+)_([0-9A-Fa-f]{8})", text)
    }


def cursor_setup(image: Image, function: int) -> dict[str, int] | None:
    """Find the list-cursor materialisation and the append inside `function`.

    The list base is not a symbol the decompilation names consistently, so it is derived from the
    SHAPE the guest uses: `lui $t6, hi` / `addiu $t6, $t6, -bias` / `addi $t6, $t6, +0x400`. Both the
    filler and the consumer build exactly that, which is also what makes it a checkable claim: if the
    shape is not found in the window, this returns None and the caller refuses rather than reporting
    a count for a function it did not recognise.
    """
    window = 0x1000
    cursor = None
    append = None
    for offset in range(0, window, 4):
        address = function + offset
        if address + 12 > image.text_end:
            break
        if (image.opcode(address) == OP_LUI and image.imm(address) == LIST_CURSOR_HIGH
                and image.rt(address) == REG_T6
                and image.opcode(address + 4) == OP_ADDIU and image.rs(address + 4) == REG_T6
                and image.rt(address + 4) == REG_T6
                and image.sext16(image.imm(address + 4)) == -LIST_CURSOR_BIAS
                and image.opcode(address + 8) == OP_ADDI and image.rs(address + 8) == REG_T6
                and image.imm(address + 8) == LIST_CURSOR_STEP):
            cursor = address + 8
        if (cursor is not None and append is None and image.opcode(address) == OP_SW
                and image.imm(address) == 0 and image.rs(address) == REG_T6):
            append = address
            break
    if cursor is None or append is None:
        return None
    return {"cursor": cursor, "append": append}


def first_store(image: Image, function: int, limit: int = 0x40) -> int | None:
    """The function's first store instruction, used as the "the function was ENTERED" witness."""
    for offset in range(0, limit, 4):
        address = function + offset
        if address >= image.text_end:
            break
        if image.opcode(address) == OP_SW:
            return address
    return None


def list_walk_store(image: Image, function: int, limit: int = 0x200) -> int | None:
    """The consumer's per-moby store: the first `sw` inside the list walk, addressed off the entry.

    This is the store that only executes when the walk read a NON-NULL entry, so arming it answers
    "did the list hold anything" from INSIDE the walk rather than from a frame park after the draw pass
    has cleared the region. Its resolved destination is `list entry + 0x40`, so the observer's own
    address report recovers the moby the walk was on — a positive identity, not a count.
    """
    for offset in range(0, limit, 4):
        address = function + offset
        if address >= image.text_end:
            break
        if (image.opcode(address) == OP_SW and image.rs(address) == REG_T5
                and image.imm(address) == 0x40):
            return address
    return None


def guest_clear_region(image: Image) -> dict[str, int]:
    """`func_8002B9CC`'s own memset, read out of the image: base, length, and the call site."""
    region = {"func": GUEST_CLEAR_FUNC, "base": 0, "length": 0, "call": 0}
    for offset in range(0, 0x80, 4):
        address = GUEST_CLEAR_FUNC + offset
        if address + 8 > image.text_end:
            break
        if image.opcode(address) != OP_JAL:
            continue
        # The delay slot carries `$a2 = length`; `$a0` is materialised just before.
        length = image.imm(address + 4)
        if image.rt(address + 4) != REG_A2 or image.opcode(address + 4) != OP_ADDIU:
            continue
        base = 0
        for back in range(4, 32, 4):
            prior = address - back
            if image.opcode(prior) == OP_LUI and image.rt(prior) == 4:
                base = (image.imm(prior) << 16) + image.sext16(image.imm(prior + 4))
                break
        region.update({"base": base & 0xFFFFFFFF, "length": length, "call": address})
        break
    return region


def describe(image: Image) -> dict[str, object]:
    update_moby = symbol("g_UpdateMoby")
    level_id = symbol("g_LevelId")
    filler = symbol("func_80051FEC")
    consumer = symbol("func_800522C0")
    sites = dispatch_sites(image, update_moby)
    level_map = per_level_update_map(image, update_moby)
    cursor = cursor_setup(image, filler)
    return {
        "g_UpdateMoby": update_moby,
        "g_LevelId": level_id,
        "filler": filler,
        "sites": sites,
        "level_map": level_map,
        "cursor": cursor,
        "filler_first_store": first_store(image, filler),
        "consumer": consumer,
        "walk_store": list_walk_store(image, consumer),
        "clear": guest_clear_region(image),
    }


def report_static(image: Image, facts: dict[str, object]) -> int:
    agreed = image.offset_check()
    if agreed == 0:
        print("REFUSED: the file formula did not verify against the decompilation (see the "
              "disassembler's own report above), so nothing derived from this image may be believed. "
              "Report this rather than the numbers below.")
        return 2

    sites: list[dict[str, int]] = facts["sites"]  # type: ignore[assignment]
    print(f"\ng_UpdateMoby = 0x{facts['g_UpdateMoby']:08X}  "
          f"(external/spyro-1/asm/data/game.sbss.s), g_LevelId = 0x{facts['g_LevelId']:08X}")
    print(f"readers: {len(sites)} `lw` of that global in the main image; a `jalr` on the loaded "
          f"register was found within 8 instructions of each")
    for site in sites:
        call = site["jalr"]
        where = f"jalr 0x{call:08X}" if call else "NO jalr within 8 instructions"
        print(f"  lw 0x{site['load']:08X} -> ${site['register']}, {where}")

    level_map: dict[int, dict[str, int]] = facts["level_map"]  # type: ignore[assignment]
    shared = level_map[-1]["value"]  # type: ignore[index]
    print(f"\nSetOverlayPointers: {len(level_map) - 1} level id(s) have a case body; {shared} share the "
          f"tail and store nothing")
    decomp = decomp_level_map()
    matched = sum(1 for level, row in level_map.items() if level >= 0 and decomp.get(level) == row["value"])
    print(f"cross-check against external/spyro-1/src/overlay_pointers.c: {matched} of "
          f"{len(level_map) - 1} level(s) agree, {len(level_map) - 1 - matched} disagree; the decomp "
          f"lists {len(decomp)}")
    for level in sorted(k for k in level_map if k >= 0)[:14]:
        row = level_map[level]
        print(f"  g_LevelId {level:2d}: case body 0x{row['body']:08X}  sw @0x{row['store']:08X}  "
              f"g_UpdateMoby = 0x{row['value']:08X}")
    if len(level_map) > 15:
        print(f"  ... {len(level_map) - 15} more level(s)")

    cursor = facts["cursor"]
    print(f"\nfunc_80051FEC = 0x{facts['filler']:08X} (external/spyro-1/asm/moby_lists.s)")
    if cursor is None:
        print("  REFUSED: the list-cursor shape (lui/addiu/addi +0x400, then `sw` through it) was not "
              "found in the first 0x1000 bytes, so this function is not the one this tool claims and "
              "no count it produces may be read about the guest's list.")
        return 2
    print(f"  list cursor built at 0x{cursor['cursor']:08X}, append `sw` at 0x{cursor['append']:08X}, "
          f"first store (the entry witness) at 0x{facts['filler_first_store']:08X}")

    clear = facts["clear"]  # type: ignore[index]
    print(f"\nwhy a frame-park read of the list is blind: func_8002B9CC (0x{clear['func']:08X}) calls "
          f"memset at 0x{clear['call']:08X} with base 0x{clear['base']:08X} length 0x{clear['length']:X}")
    list_base = LIST_BASE
    if clear["length"] and clear["base"]:
        inside = clear["base"] <= list_base < clear["base"] + clear["length"]
        print(f"  the list base 0x{list_base:08X} is "
              f"{'INSIDE' if inside else 'OUTSIDE'} that region, at +0x{list_base - clear['base']:X}")
        if not inside:
            print("  REFUSED: the derived list base is not inside the guest's own clear, so the "
                  "explanation above does not hold and the zero must be reported as unexplained.")
            return 2
    return 0


def selftest() -> int:
    """Derive from the image and require the shape, the count, and BOTH answers from the decomp.

    A tool that can only print the product's answer has not been shown to work. The two answers that
    must differ are a level with a case body and a level without one, and a level id whose derived
    value matches the decompilation against one the table does not carry.
    """
    if not EXE.is_file():
        print(f"REFUSED: {EXE} is not there; the derivation is from the provisioned executable and "
              f"there is nothing to derive from. Provision it with tools/provision_title.py.")
        return 2
    image = Image(EXE)
    facts = describe(image)
    failures = 0

    def check(name: str, ok: bool, detail: str) -> None:
        nonlocal failures
        failures += 0 if ok else 1
        print(f"  {'ok  ' if ok else 'FAIL'} {name}: {detail}")

    sites: list[dict[str, int]] = facts["sites"]  # type: ignore[assignment]
    called = [s for s in sites if s["jalr"]]
    check("readers", len(called) >= 1, f"{len(sites)} `lw` of g_UpdateMoby, {len(called)} with a "
                                      f"`jalr` within 8 instructions")

    level_map: dict[int, dict[str, int]] = facts["level_map"]  # type: ignore[assignment]
    decomp = decomp_level_map()
    check("case bodies", len(level_map) - 1 == len(decomp) == 43,
          f"image {len(level_map) - 1} level(s) with a body, decomp {len(decomp)}")
    matched = sum(1 for level, row in level_map.items() if level >= 0 and decomp.get(level) == row["value"])
    check("level->update agreement", matched == len(decomp), f"{matched} of {len(decomp)} agree")
    check("a level WITH a body", 10 in level_map and level_map[10]["value"] == 0x8007D9C8,
          f"g_LevelId 10 -> 0x{level_map.get(10, {}).get('value', 0):08X}")
    check("a level WITHOUT a body", 5 not in level_map,
          f"g_LevelId 5 is {'absent (shares the tail)' if 5 not in level_map else 'PRESENT'}")
    check("a shared level id", level_map[-1]["value"] > 0, f"{level_map[-1]['value']} id(s) share the tail")

    cursor = facts["cursor"]
    check("list cursor shape", cursor is not None,
          "found" if cursor else "NOT FOUND in the filler's first 0x1000 bytes")
    check("list walk store", facts["walk_store"] is not None,
          f"0x{facts['walk_store']:08X}" if facts["walk_store"] is not None
          else "NOT FOUND: the consumer's per-moby store, so this tool cannot measure the walk")
    clear = facts["clear"]  # type: ignore[index]
    list_base = LIST_BASE
    check("guest clear contains the list", bool(clear["length"]) and clear["base"] <= list_base
          < clear["base"] + clear["length"],
          f"memset base 0x{clear['base']:08X} length 0x{clear['length']:X}, list base 0x{list_base:08X}")

    print(f"  selftest: {failures} failure(s). The two answers the tool must be able to give are a "
          f"level id WITH a case body and one WITHOUT, and a derived level->update value that agrees "
          f"with the decompilation and one the table does not carry; a tool that could only print the "
          f"product's answer would pass none of the negative cases above.")
    return 1 if failures else 0


def live(args: argparse.Namespace) -> int:
    drive = runpy.run_path(str(TOOLS / "drive.py"))
    Port, Navigator = drive["Port"], drive["Navigator"]
    environment, disc_path = drive["environment"], drive["disc_path"]

    disc = disc_path()
    if disc is None:
        print("REFUSED: no disc image found; this probe needs the real executable to reach gameplay",
              file=sys.stderr)
        return 2

    image = Image(ROOT / args.executable_binary)
    facts = describe(image)
    if report_static(image, facts) != 0:
        return 2

    level_map: dict[int, dict[str, int]] = facts["level_map"]  # type: ignore[assignment]
    cursor = facts["cursor"]
    clear_region: dict[str, int] = facts["clear"]  # type: ignore[assignment]
    assert cursor is not None

    # THE ARMING. Every target below is the PC OF A STORE INSTRUCTION, which is the only thing
    # PSXPORT_STORE_OBSERVE matches. Arming a DATA address is the mistake this tool exists to stop
    # repeating, so the targets are derived and printed above, and the data addresses are read with
    # the channel instead.
    witness = facts["filler_first_store"]
    walk = facts["walk_store"]
    assert witness is not None
    armed = [witness, cursor["append"]]
    if walk is not None:
        armed.append(walk)
    print(f"\narmed store PCs: 0x{witness:08X} (the filler's first store: it fired, so the filler was "
          f"ENTERED), 0x{cursor['append']:08X} (the filler's append: it fired, so the list was "
          f"WRITTEN)")
    if walk is not None:
        print(f"                  0x{walk:08X} (the CONSUMER's per-moby store, reachable only when the "
              f"walk read a NON-NULL list entry: it fired, so the list was not empty DURING the update. "
              f"Its destination is `entry + 0x40`, so the observer's address report names the moby.)")
    else:
        print("                  the consumer's per-moby store was NOT found, so this run cannot say "
              "whether the list held anything. The park read below is not a substitute.")

    env = environment(disc)
    env["PSXPORT_STORE_OBSERVE"] = ",".join(f"0x{t:08X}" for t in armed)
    log = ROOT / args.log
    port = Port(ROOT / args.executable, ROOT / args.binary, log, env)
    try:
        Navigator(port).reach_gameplay()
        port.mark_arrival()
        port.run(args.settle)
        level = port.word(facts["g_LevelId"])
        live_update = port.word(facts["g_UpdateMoby"])
        print(f"\nreached GS_Playing at frame {port.frame}; g_LevelId = {level}, "
              f"g_GameTick = {port.word(0x8007572C)}")
        print(f"CONTROL: g_Gamestate (0x800757D8) = {port.word(0x800757D8)} (0 = GS_Playing) and "
              f"g_LevelId = {level}; without both, the reads below prove nothing")
        if port.word(0x800757D8) != PSX_GAMESTATE_PLAYING or level not in level_map:
            print("REFUSED: the controls did not hold, so the readings below are not about gameplay.")
            return 2
        expected = level_map[level]["value"]
        verdict = "MATCHES" if live_update == expected else "DOES NOT MATCH"
        print(f"g_UpdateMoby on the product = 0x{live_update:08X}; the image's own case body for "
              f"g_LevelId {level} stores 0x{expected:08X} at 0x{level_map[level]['store']:08X} -> "
              f"{verdict}")
        if live_update != expected:
            print("  this is a real product defect: the resident overlay's update function is not "
                  "what the switch installed for this level, so the dispatch cannot reach it.")
            return 1
        print("  so the dispatch target is CORRECT; whether the function is ENTERED is answered by the "
              "store counts at exit, not by this word.")

        list_base = LIST_BASE
        window = []
        address = list_base
        while len(window) < MAX_LIST_WORDS:
            chunk = port.words(address, MAX_CHANNEL_WORDS)
            window.extend(chunk)
            if len(chunk) < MAX_CHANNEL_WORDS:
                break
            address += MAX_CHANNEL_WORDS * 4
        nonzero = [w for w in window if w]
        print(f"\nAT THIS FRAME PARK the list at 0x{list_base:08X} holds {len(nonzero)} non-zero word(s) "
              f"of {len(window)} read. THIS IS NOT A FINDING: the guest clears the whole region "
              f"(0x{clear_region['base']:08X}, 0x{clear_region['length']:X} bytes) in its DRAW pass, so "
              f"a park is expected to read zero on the console too. The store counts at exit are the "
              f"evidence.")
        print("census:", port.census_line())
    finally:
        code = port.end()
    print("\nstore-observer report, echoed from the run's own log so the evidence and the run are one "
          "artefact. `jit_instructions` is the denominator every zero needs:")
    for line in log.read_text(errors="replace").splitlines():
        if "store-observe" in line and ("report:" in line or "EXECUTED" in line
                                        or "last write" in line or "first write" in line
                                        or "MATCHED NONE" in line):
            print("  " + line.split("] ", 1)[-1])
    if walk is not None:
        print("\nHOW TO READ THE CONSUMER'S ROW. Its destination is `list entry + 0x40`, and the walk only\n"
              "reaches that store after reading a NON-NULL entry, so a fired consumer store is a POSITIVE\n"
              "answer to 'was the list empty during the update'. The reported address minus 0x40 is the moby\n"
              "the walk was on: a value in main RAM is a real moby, and one that is not is the hole. Do NOT\n"
              "read the consumer's list BASE from the filler's first write address: the filler's cursor\n"
              f"advances by 4 per append, so the base is 0x{LIST_BASE:08X} (from the three image\n"
              "instructions above) and every later write address is base + 4k.")
    return 0 if code == 0 else code


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--selftest", action="store_true",
                        help="derive the dispatch from the image and check it; drives nothing")
    parser.add_argument("--executable", default="build/bin/spyro_port")
    parser.add_argument("--executable-binary", default=str(EXE),
                        help="the provisioned SCUS_942.28 the dispatch is derived from")
    parser.add_argument("--binary", default="scratch/assets/spyro1/SCUS_942.28")
    parser.add_argument("--log", default="scratch/logs/level_update_dispatch.log")
    parser.add_argument("--settle", type=int, default=120,
                        help="frames to run after GS_Playing, so the level has had fields to load in")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    return live(args)


if __name__ == "__main__":
    raise SystemExit(main())
