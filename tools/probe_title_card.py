#!/usr/bin/env python3
"""probe_title_card.py — recover the title card's own clock from the IMAGE, then measure it LIVE.

WHY THIS EXISTS. The operator's report is that Start does not skip the "IN THE WORLD OF DRAGONS..."
card. Retail implements that skip itself (external/spyro-1/src/overlays/titlescreen.c:100-106): while
the card is up, `g_CutsceneLayout->m_CurrentTick += 2` runs every update, and once the tick passes
300 a held PAD_START or PAD_CROSS fast-forwards it to 1170, which is past the 1169 gate that ends
the card and hands the screen to substate 3.

So the question is not "is a skip missing" but "which precondition of the GUEST'S OWN condition is
false". `probe_title_card_skip.py` already established two of them live: the press reaches
`g_Pad.m_Held` as 0xffff0800 (0x0800 IS PAD_START, bit 11), and `m_SubState` is 2. The third is the
tick, and nobody had read it -- which is why this file exists, and why the address had to be
recovered from the bytes rather than taken on trust.

TWO HALVES, AND THE LINE BETWEEN THEM IS THE POINT
--------------------------------------------------
  STATIC  reads only `scratch/assets/spyro1/SCUS_942.28`. It derives the MODULE MAP first, because a
          guest address is not an identity in this title: many WAD modules share one load address
          (spyro/AGENTS.md). From the map it recovers `g_CutsceneLayout` and the stage-13 dispatch
          target, and it prints what it scanned, what it matched, and what it did NOT.
  LIVE    reads guest words through the port's own REPL. It writes NOTHING into guest memory.

WHAT MAKES THE STATIC HALF TRUSTWORTHY
--------------------------------------
A scan that can only print one answer is not an instrument, and a zero from a partial scan is not an
absence. This file's first version got the module map wrong -- it read a `lui` immediate as a low
displacement, so the arena came out 0x80082A40 instead of 0x8007AA38 -- and the cross-check it
carried did NOT catch it, because both of its reads went through the same wrong decoder. Two reads
through one decoder is one read. So the checks here are of three different kinds:

  1. A KNOWN-ANSWER CONTROL the wrong decode cannot satisfy. The derived bss range must CONTAIN
     `g_LoadStage` (0x80075864), an address three independent sources record and which is a bss
     global. The wrong arena put bss at 0x8007D647..0x80082A40, which contains it too -- so the
     containment test is stated AND the arena is additionally required to lie inside the guest's
     2 MiB RAM and above the end of the main image, and its size is printed so a reader can
     compare it with the product's own crt0 line.
  2. THE DECOMPILED LISTING as a second decoder. Every `lui`+`sw` pair the decomp's own .s listing
     prints is compared against what this file decodes from the same address, and the agree /
     disagree counts are printed. A byte-order or offset error cannot survive that.
  3. A NEGATIVE THE SCAN MUST PRODUCE, for the live half: `retail_skip_condition` names all five
     preconditions, so a dead one cannot be read as a live one. See `--selftest`.

Usage:
  uv run --frozen python tools/probe_title_card.py --static
  uv run --frozen python tools/probe_title_card.py [--hold-frames 240] [--samples 24]
  uv run --frozen python tools/probe_title_card.py --selftest
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

import guest_globals  # noqa: E402
from drive import ROOT as DRIVE_ROOT, Port, disc_path, environment  # noqa: E402

EXE = ROOT / "scratch" / "assets" / "spyro1" / "SCUS_942.28"
DECOMP = ROOT / "external" / "spyro-1"

# g_CutsceneLayout, recovered from the image by this file's own scan; see static_report().
G_CUTSCENE_LAYOUT = 0x80075680
# CutsceneLayout.m_CurrentTick is the first int (external/spyro-1/include/cutscene.h:20-27).
CUTSCENE_CURRENT_TICK = 0
# TitlescreenUpdate's own gate values, from titlescreen.c:100-116.
TICK_STARTS_SKIPPABLE = 300
TICK_SKIP_TARGET = 1170
TICK_CARD_ENDS = 1169
TICK_SLOW_ARM = 1100
# g_TitlescreenState: m_Mode +0, m_State +4, m_Tick +8, m_SubTick +0xC, m_SubState +0x10.
G_TITLESCREEN = guest_globals.kTitlescreenState
TS_MODE = 0x00
TS_SUBSTATE = 0x10
# g_Pad: m_Down +0, m_Released +4, m_Held +8, m_Type +0xC (include/gamepad.h:53-58).
G_PAD = guest_globals.kPad
PAD_DOWN = 0x00
PAD_HELD = 0x08
PAD_TYPE = 0x0C
PAD_START = 1 << 11
PAD_CROSS = 1 << 6
CONTROLLER_TYPE_DPAD = 2

GS_TITLE_SCREEN = 13
TSM_INIT = 0
TSM_MENU = 1
CARD_SUBSTATE = 2
# g_LoadStage (main.c). A bss global, so the derived bss range must contain it.
G_LOAD_STAGE = guest_globals.kLoadStage
GUEST_RAM_END = 0x80200000

# Addresses three INDEPENDENT sources already record. The static scan must reproduce all of them, or
# it is not entitled to report its own answer.
KNOWN_ANSWERS = {
    "g_Gamestate": 0x800757D8,
    "g_TitlescreenState": 0x80078D78,
    "g_Pad": 0x80077378,
    "g_LoadStage": 0x80075864,
}

# Every listing line the decomp prints as `/* FILEOFF VRAMADDR INSNWORD */ mnemonic ...`. Overlay and
# .data lines are excluded: overlays are a different image reusing these addresses, and a data
# directive is in the assembler's byte order, not the image's.
LISTING = re.compile(
    r"/\*\s*([0-9A-Fa-f]+)\s+([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})\s*\*/\s+(\S+)\s*(.*)$")
DATA_DIRECTIVES = (".word", ".space", ".byte", ".half", ".dword", ".asciz", ".ascii", ".short")


# ── image access ────────────────────────────────────────────────────────────────────────────────


class Image:
    """The authenticated executable.

    A PS-X EXE stores instructions little-endian and data big-endian, so the wrong half of that
    choice is not a crash -- it is a plausible listing of garbage. Every decode in this file goes
    through `word`, and `selftest` requires the decomp's own listing to agree with it.
    """

    def __init__(self, path: Path):
        self.path = path
        self.raw = path.read_bytes()
        magic = self.raw[:8]
        if magic != b"PS-X EXE":
            raise SystemExit(f"probe_title_card: {path} is not a PS-X EXE (magic {magic!r})")
        self.load, self.text_size = struct.unpack_from("<II", self.raw, 0x18)
        self.text_offset = 0x800
        self.end = self.load + self.text_size
        if self.text_offset + self.text_size > len(self.raw):
            raise SystemExit("probe_title_card: header.text_size runs past the file")

    @property
    def words(self) -> int:
        return self.text_size // 4

    def in_image(self, address: int) -> bool:
        return self.load <= address < self.end

    def index(self, address: int) -> int:
        if not self.in_image(address):
            raise SystemExit(f"probe_title_card: 0x{address:08X} is outside the main image")
        return address - self.load

    def word(self, address: int) -> int:
        at = self.text_offset + self.index(address)
        return int.from_bytes(self.raw[at:at + 4], "little")

    def data_word_be(self, address: int) -> int:
        at = self.text_offset + self.index(address)
        return struct.unpack_from(">I", self.raw, at)[0]


def op(word: int) -> int:
    return word >> 26


def rs(word: int) -> int:
    return (word >> 21) & 0x1F


def rt(word: int) -> int:
    return (word >> 16) & 0x1F


def imm(word: int) -> int:
    return word & 0xFFFF


def simm(word: int) -> int:
    value = imm(word)
    return value - 0x10000 if value & 0x8000 else value


def hi_lo(upper: int, lower: int) -> int:
    """`lui $r, U` then `addiu $r, $r, L` -- the pair that builds a 32-bit constant.

    `lui`'s immediate is the UPPER half. Reading it as a low displacement is the bug this file's
    first version had, and it produced a plausible, wrong arena, so the operation is named once here
    instead of being open-coded at each of its four call sites.
    """
    return ((upper & 0xFFFF) << 16) + lower


def mnemonic(word: int) -> str:
    names = {
        0x00: "special", 0x01: "regimm", 0x02: "j", 0x03: "jal", 0x04: "beq", 0x05: "bne",
        0x06: "blez", 0x07: "bgtz", 0x08: "addi", 0x09: "addiu", 0x0A: "slti", 0x0B: "sltiu",
        0x0C: "andi", 0x0D: "ori", 0x0E: "xori", 0x0F: "lui", 0x20: "lb", 0x21: "lh",
        0x22: "lwl", 0x23: "lw", 0x24: "lbu", 0x25: "lhu", 0x28: "sb", 0x29: "sh", 0x2A: "swc1",
        0x2B: "sw", 0x2C: "lwc1", 0x2D: "ldc1", 0x2E: "sdc1",
    }
    return names.get(op(word), f"op{op(word):02X}")


# ── the static half ─────────────────────────────────────────────────────────────────────────────


def module_map(image: Image) -> dict[str, int]:
    """Where each image lives, read out of the executable and out of the guest crt0's immediates.

    The header alone is not enough, because the region the overlays are published over is NOT part
    of this file: guest crt0 (entry 0x8005B8E0) zeroes a bss range and hands its end to InitHeap, and
    that end is the arena.

        0x8005B8E0  lui   $v0, 0x8007
        0x8005B8E4  addiu $v0, $v0, 0x5640        -> bss first word  0x80075640
        0x8005B8E8  lui   $v1, 0x8008
        0x8005B8EC  addiu $v1, $v1, -0x55c8       -> bss end (limit) 0x8007AA38
        0x8005B8F0  sw    $zero, ($v0)            ... while ($v0 < $v1) { *$v0++ = 0; }
        0x8005B8F8  sltu   $at, $v0, $v1
        0x8005B91C  lui   $a0, 0x8008
        0x8005B920  addiu $a0, $a0, -0x55c8       -> the SAME limit, as the heap base
        0x8005B968  jal   0x8005DB14               ... InitHeap(base, size)
    """
    first = [image.word(0x8005B8E0 + 4 * n) for n in range(4)]
    if op(first[0]) != 0x0F or op(first[1]) != 0x09 or op(first[2]) != 0x0F or op(first[3]) != 0x09:
        raise SystemExit("probe_title_card: crt0 bss bounds are not two lui/addiu pairs")
    bss_first = hi_lo(imm(first[0]), simm(first[1]))
    bss_end = hi_lo(imm(first[2]), simm(first[3]))
    heap = [image.word(0x8005B91C + 4 * n) for n in range(2)]
    heap_base = hi_lo(imm(heap[0]), simm(heap[1]))
    if heap_base != bss_end:
        raise SystemExit(f"probe_title_card: crt0's bss end 0x{bss_end:08X} and its InitHeap base "
                         f"0x{heap_base:08X} disagree; the module map is not established")
    return {"load": image.load, "end": image.end, "bss_first": bss_first, "bss_end": bss_end,
            "arena": bss_end}


def listing_agreement(image: Image) -> tuple[int, int, list[str]]:
    """Compare this file's decode against every MAIN-IMAGE instruction the decomp's .s files print.

    The listing prints `/* FILEOFF VRAMADDR INSNWORD */`, and its INSNWORD is the image's four bytes in
    FILE order read as a big-endian number -- `asm/42CC4.s` prints `0780013C` for a `lui $at,0x8007`
    whose little-endian instruction word is 0x3C018007. So the comparison is against the big-endian
    read, and a wrong byte order fails it outright: 68,307 words are compared and the counts go in
    the report whatever they are, because a denominator attached to a guaranteed answer is still a
    guaranteed answer.
    """
    agree = disagree = 0
    samples: list[str] = []
    for path in sorted(DECOMP.glob("asm/**/*.s")):
        for line in path.read_text(errors="replace").splitlines():
            match = LISTING.search(line)
            if not match:
                continue
            address = int(match.group(2), 16)
            printed = int(match.group(3), 16)
            if line.split("//", 1)[0].lstrip().startswith(DATA_DIRECTIVES):
                continue
            if not image.in_image(address):
                continue
            if image.data_word_be(address) == printed:
                agree += 1
            else:
                disagree += 1
                if len(samples) < 4:
                    samples.append(f"{path.relative_to(DECOMP)}:0x{address:08X} listing "
                                   f"0x{printed:08X} image 0x{image.data_word_be(address):08X}")
    return agree, disagree, samples


def abs_pairs(image: Image, low: int, high: int, kind: int) -> dict[int, list[int]]:
    """Every `lui $rX, 0x8007` + `sw|lw $rY, off($rX)` pair naming a global in [low, high).

    The window is three instructions, which is how far this compiler puts the store from the load of
    a high half. A pair further apart is a different pattern and is not counted rather than guessed
    at, and the count of `lui $rX, 0x8007` in the whole text is reported beside the result so a
    short match set can be read as short rather than as absent.
    """
    text = image.raw[image.text_offset:image.text_offset + image.text_size]
    head_op = sum(1 for at in range(0, len(text) - 4, 4)
                  if op(int.from_bytes(text[at:at + 4], "little")) == 0x0F
                  and imm(int.from_bytes(text[at:at + 4], "little")) == 0x8007)
    found: dict[int, list[int]] = {}
    for at in range(0, len(text) - 16, 4):
        head = int.from_bytes(text[at:at + 4], "little")
        if op(head) != 0x0F or imm(head) != 0x8007:
            continue
        for step in (4, 8, 12):
            tail = int.from_bytes(text[at + step:at + step + 4], "little")
            if op(tail) != kind or rs(tail) != rt(head):
                continue
            address = 0x80070000 + imm(tail)
            if low <= address < high:
                found.setdefault(address, []).append(image.load + at)
            break
    found["__lui_8007_total__"] = [head_op]  # type: ignore[index]
    return found


def recover_pointer_global(image: Image, address: int) -> dict[str, object]:
    """Confirm one main-RAM global is a POINTER written once, from the bytes.

    A pointer global has a shape a scalar does not: exactly one `lui`+`sw` writer, and then the
    CODE dereferences it -- several `lui`+`lw` reads of the same word, some of them close to the
    writer. That co-location is what distinguishes `g_CutsceneLayout = g_Buffers.m_LevelScene`
    (immediately followed by `PATCH_POINTER(g_CutsceneLayout->m_CameraData, ...)` and the Moby
    pointer loop, which read it back) from a counter that happens to have one writer.
    """
    writers = abs_pairs(image, 0x80075000, 0x80079000, 0x2B)
    readers = abs_pairs(image, 0x80075000, 0x80079000, 0x23)
    lui_total = writers.pop("__lui_8007_total__")[0]  # type: ignore[misc]
    sites = writers.get(address, [])
    nearby = [r for r in readers.get(address, []) if sites and abs(r - sites[0]) < 0x200]
    # A shape is only a signature if some other global could have matched it. Count those, so the
    # report can say how many candidates existed rather than presenting one as unique.
    contenders = sorted(
        other for other, other_sites in writers.items()
        if len(other_sites) == 1
        and len([r for r in readers.get(other, []) if abs(r - other_sites[0]) < 0x200]) >= 3)
    return {
        "lui_total": lui_total,
        "writers": sites,
        "readers": readers.get(address, []),
        "nearby": nearby,
        "contenders": contenders,
        "all_single_writer": sorted(a for a, v in writers.items() if len(v) == 1),
    }


def overlay_calls(image: Image, maps: dict[str, int]) -> tuple[list[tuple[int, int]], int]:
    """Every `jal` in the main image whose target is OUTSIDE the main image, plus the total.

    This is the only honest way to name a module call: the target is a RAM address, so on its own it
    says nothing. It is reported with the module map beside it, and the DATA false positives -- words
    in a jump table that happen to carry opcode 3 -- are COUNTED rather than silently dropped,
    because a scan that quietly prunes is the instrument this workspace has been burned by.
    """
    text = image.raw[image.text_offset:image.text_offset + image.text_size]
    total = 0
    calls = []
    for at in range(0, len(text) - 4, 4):
        word = int.from_bytes(text[at:at + 4], "little")
        if op(word) != 0x03:
            continue
        total += 1
        target = ((word & 0x03FFFFFF) << 2) | (image.load & 0xF0000000)
        if target >= image.end:
            calls.append((image.load + at, target))
    return calls, total


def gamestate_chains(image: Image) -> list[tuple[int, list[tuple[int, int, int]]]]:
    """Every `g_Gamestate` compare chain in the image, with the arm each stage value selects.

    Found by SHAPE, never by a hardcoded entry. The chain is `lui $vX,0x8007 / lw $vX,0x57D8($vX)`
    and then a run of `bne $gamestate, $value, <next compare>` whose DELAY SLOT is
    `addiu $value, $zero, N`. Each branch's FALL-THROUGH is the arm for the value already in the
    compared register, and its delay slot names the NEXT value -- so the arm's own value has to be
    carried forward, not read off the branch in front of it. The chain opens with
    `beq $gamestate, $zero`, whose TAKEN target is gamestate 0. Compares are NOT adjacent: each
    arm's body sits between one compare's delay slot and the next compare.

    ALL chains are returned, not just the first. This image has at least two -- the update dispatch
    and the draw dispatch -- and they are told apart by WHERE THEIR STAGE-13 ARM CALLS, which is a
    fact about the bytes. An earlier version of this function returned the first chain it found and
    silently reported the DRAW chain's stage 13, whose call is `0x8001E6B8` and not the overlay.
    """
    text = image.raw[image.text_offset:image.text_offset + image.text_size]

    def at(address: int) -> int:
        return int.from_bytes(
            text[address - image.load:address - image.load + 4], "little")

    def body_calls(first: int, limit: int) -> list[tuple[int, int]]:
        found = []
        address = first
        while image.in_image(address) and address < limit:
            word = at(address)
            if op(word) == 0x03:
                found.append((address, ((word & 0x03FFFFFF) << 2) | (image.load & 0xF0000000)))
            if op(word) in (0x02, 0x03):
                break
            address += 4
        return found

    chains = []
    for index in range(image.words - 8):
        base = image.load + index * 4
        if op(at(base)) != 0x0F or imm(at(base)) != 0x8007:
            continue
        if op(at(base + 4)) != 0x23 or rs(at(base + 4)) != rt(at(base)) or imm(at(base + 4)) != 0x57D8:
            continue
        stage_reg = rt(at(base + 4))
        cursor = base + 8
        while op(at(cursor)) == 0x00:
            cursor += 4
        if op(at(cursor)) != 0x04 or rs(at(cursor)) != stage_reg or rt(at(cursor)) != 0:
            continue
        compares: list[int] = []
        probe = cursor + 8
        for _ in range(96):
            if not image.in_image(probe):
                break
            branch, delay = at(probe), at(probe + 4)
            if op(branch) == 0x05 and rs(branch) == stage_reg and op(delay) == 0x09 and rs(delay) == 0:
                compares.append(probe)
                probe += 8
                continue
            probe += 4
        if len(compares) < 8:
            continue
        arms: list[tuple[int, int, int]] = []
        value = 0
        for site, target in body_calls(cursor + 4 + (simm(at(cursor)) << 2), cursor + 0x4000):
            arms.append((0, site, target))
        for position, compare in enumerate(compares):
            stop = compares[position + 1] if position + 1 < len(compares) else compare + 0x2000
            for site, target in body_calls(compare + 8, stop):
                arms.append((value, site, target))
            value = imm(at(compare + 4))
            if rt(at(compare + 4)) != rt(at(compare)):
                break
        if arms:
            chains.append((cursor, arms))
    return chains


def static_report() -> int:
    if not EXE.is_file():
        print(f"REFUSING: no provisioned image at {EXE}. Provision it with "
              f"tools/provision_title.py --title spyro1; the scan has no corpus to declare absent.")
        return 2
    image = Image(EXE)
    print(f"[static] corpus: {EXE}")
    print(f"[static] main image 0x{image.load:08X}..0x{image.end:08X}, {image.words} instruction "
          f"words, header.text_size 0x{image.text_size:X}, instructions little-endian")

    agree, disagree, samples = listing_agreement(image)
    print(f"[static] decoder control: the decomp's own .s listings carry {agree + disagree} "
          f"main-image instruction word(s) this file could compare; {agree} agree, {disagree} "
          f"disagree")
    for sample in samples:
        print(f"[static]   DISAGREE {sample}")
    if disagree:
        print("[static] REFUSING: the decode does not reproduce the decompiled listing, so nothing "
              "it decodes below is admissible.")
        return 2

    maps = module_map(image)
    inside = maps["bss_first"] <= G_LOAD_STAGE < maps["bss_end"]
    contained = maps["bss_end"] <= GUEST_RAM_END
    above = maps["bss_end"] > image.end
    print(f"[static] guest bss 0x{maps['bss_first']:08X}..0x{maps['bss_end']:08X} "
          f"({maps['bss_end'] - maps['bss_first']} bytes), from crt0 0x8005B8E0, with the same "
          f"limit read again at 0x8005B91C as the InitHeap base")
    print(f"[static]   contains g_LoadStage 0x{G_LOAD_STAGE:08X}: {inside}; inside the guest's 2 MiB "
          f"RAM: {contained}; above the main image: {above}")
    if not (inside and contained and above):
        print("[static] REFUSING: the derived bss range fails a containment test, so the arena is "
              "not established and no module address below means anything.")
        return 2
    print(f"[static] MODULE MAP: main image 0x{maps['load']:08X}..0x{maps['end']:08X}; guest bss "
          f"0x{maps['bss_first']:08X}..0x{maps['bss_end']:08X}; module arena 0x{maps['arena']:08X}")
    print(f"[static] any address at or above 0x{maps['end']:08X} is not this file: it is a module "
          f"published over the arena. A guest address alone names no module in this title.")

    shape = recover_pointer_global(image, G_CUTSCENE_LAYOUT)
    print(f"[static] `lui $rX, 0x8007` in the whole text: {shape['lui_total']}; main-RAM globals in "
          f"0x80075000..0x80079000 with exactly one lui+sw writer: "
          f"{len(shape['all_single_writer'])}")
    print(f"[static] 0x{G_CUTSCENE_LAYOUT:08X}: {len(shape['writers'])} writer(s) "
          f"{[hex(s) for s in shape['writers']]}, {len(shape['readers'])} lui+lw pointer-base read(s) "
          f"over the whole text, {len(shape['nearby'])} of them within 0x200 bytes of the writer "
          f"{[hex(s) for s in shape['nearby']]}")
    print(f"[static]   (the same three-part shape is satisfied by {len(shape['contenders'])} other "
          f"globals in that window: {[hex(c) for c in shape['contenders']]}. The shape narrows the "
          f"field; it does not by itself make the answer unique, and this file does not claim it "
          f"does -- external/spyro-1/asm/data/game.sbss.s:25-26 names the symbol over the "
          f"byte-identical image and the bytes above agree with it.)")
    if len(shape["writers"]) != 1 or len(shape["nearby"]) < 3:
        print(f"[static] REFUSING: 0x{G_CUTSCENE_LAYOUT:08X} does not have the one-writer / "
              f"co-located-reader shape, so the bytes do not support it.")
        return 2
    print(f"[static] g_CutsceneLayout = 0x{G_CUTSCENE_LAYOUT:08X}, m_CurrentTick at +"
          f"{CUTSCENE_CURRENT_TICK}")

    calls, jal_total = overlay_calls(image, maps)
    in_arena = [c for c in calls if maps["arena"] <= c[1] < maps["arena"] + 0x8000]
    print(f"[static] `jal` in the main image: {jal_total}; {len(calls)} target outside it; of those "
          f"{len(in_arena)} land in [arena, arena+0x8000). The remainder are DATA words that carry "
          f"opcode 3 and are counted here rather than dropped.")
    for site, target in in_arena:
        print(f"[static]   0x{site:08X}  jal 0x{target:08X}  (arena +0x{target - maps['arena']:X})")
    if not in_arena:
        print("[static] REFUSING: no module call at all, which is not what a reachable overlay is.")
        return 2

    chains = gamestate_chains(image)
    print(f"[static] g_Gamestate compare chains found: {len(chains)}. This image has more than one "
          f"(the update dispatch and the draw dispatch use the same shape), so all of them are "
          f"listed and they are told apart by where their STAGE-13 arm calls:")
    update_chain = None
    for start, arms in chains:
        values = sorted({a[0] for a in arms})
        thirteen = [a for a in arms if a[0] == GS_TITLE_SCREEN]
        print(f"[static]   chain at 0x{start:08X}: {len(arms)} arm/call pair(s), stage values "
              f"{values}")
        for value, site, callee in sorted(thirteen):
            where = (f"MODULE ARENA +0x{callee - maps['arena']:X}"
                     if maps["arena"] <= callee < maps["arena"] + 0x8000
                     else ("main image" if image.in_image(callee) else "outside both"))
            print(f"[static]     g_Gamestate == 13 -> 0x{site:08X}  jal 0x{callee:08X}  ({where})")
            if where.startswith("MODULE ARENA"):
                update_chain = (start, arms)
    if update_chain is None:
        print("[static] REFUSING: no chain's stage-13 arm calls into the module arena, so the "
              "overlay's own update entry is not established.")
        return 2
    start, arms = update_chain
    print(f"[static] the chain at 0x{start:08X} is the one the port dispatches per field: it is the "
          f"only chain whose stage-13 arm calls a module, and a module is only reachable from the "
          f"guest's own update (external/spyro-1/src/gamestates/update.c:1072-1077 dispatches "
          f"`TitlescreenUpdate` when `m_Mode != TSM_Demo`).")
    return 0


# ── the live half ───────────────────────────────────────────────────────────────────────────────


def clock_state(port: Port) -> dict[str, object]:
    """One sample of the card's own clock, from guest words only."""
    pointer = port.word(G_CUTSCENE_LAYOUT)
    return {
        "pointer": pointer,
        "tick": port.word(pointer + CUTSCENE_CURRENT_TICK) if pointer else None,
        "mode": port.word(G_TITLESCREEN + TS_MODE),
        "substate": port.word(G_TITLESCREEN + TS_SUBSTATE),
        "gamestate": port.gamestate(),
        "padDown": port.word(G_PAD + PAD_DOWN) & 0xFFFF,
        "padHeld": port.word(G_PAD + PAD_HELD) & 0xFFFF,
        "padType": port.word(G_PAD + PAD_TYPE),
    }


def retail_skip_condition(sample: dict[str, object]) -> str:
    """EXACTLY titlescreen.c:102-109, in retail's own order, naming the FIRST clause that fails.

    The order matters: retail tests `m_CurrentTick < 1100` first, so a tick already past 1100 skips
    the whole block. A report that only gave the final boolean could not say WHICH clause decided,
    and a report that cannot say which clause decided cannot be read as the other question.
    """
    if not sample["pointer"]:
        return "g_CutsceneLayout is NULL, so m_CurrentTick is not readable and the branch cannot fire"
    tick = sample["tick"]
    if tick is None:
        return "m_CurrentTick was not readable"
    if not tick < TICK_SLOW_ARM:
        return f"m_CurrentTick={tick} is not < {TICK_SLOW_ARM}, so retail's block is skipped entirely"
    if sample["substate"] != CARD_SUBSTATE:
        return (f"m_SubState={sample['substate']} is not {CARD_SUBSTATE}, so retail's card arm is "
                f"not taken")
    if tick < TICK_STARTS_SKIPPABLE:
        return (f"m_CurrentTick={tick} is below the {TICK_STARTS_SKIPPABLE} gate, so retail will "
                f"not skip yet")
    if not sample["padHeld"] & (PAD_START | PAD_CROSS):
        return (f"m_CurrentTick={tick} passed {TICK_STARTS_SKIPPABLE} but m_Held="
                f"0x{sample['padHeld']:04X} carries no START/CROSS")
    return f"ALL CLAUSES TRUE: m_CurrentTick={tick}, m_SubState=2, m_Held=0x{sample['padHeld']:04X}"


def live(hold_frames: int, samples: int, wait_title: int, shots: bool) -> int:
    env = environment(disc_path())
    port = Port(DRIVE_ROOT / "build/bin/spyro_port",
                DRIVE_ROOT / "scratch/assets/spyro1/SCUS_942.28",
                DRIVE_ROOT / "scratch/logs/probe_title_card.log",
                env)
    shots_dir = "scratch/screenshots/title_card"
    try:
        advanced = 0
        state = clock_state(port)
        while state["gamestate"] != GS_TITLE_SCREEN and advanced < wait_title:
            port.run(10)
            advanced += 10
            state = clock_state(port)
        print(f"[live] gamestate={state['gamestate']} titleMode={state['mode']} "
              f"titleSubState={state['substate']} after {advanced} fields")
        if state["gamestate"] != GS_TITLE_SCREEN:
            print(f"[live] REFUSING: gamestate is {state['gamestate']}, not {GS_TITLE_SCREEN} "
                  f"(GS_TitleScreen). The card was not up, so this run says nothing about it.")
            return 2

        print(f"[live] g_CutsceneLayout (0x{G_CUTSCENE_LAYOUT:08X}) = 0x{state['pointer']:08X}"
              + ("" if state["pointer"] else "   <-- NULL"))
        if state["pointer"]:
            print(f"[live] first eight words there (m_CurrentTick, m_0x04, m_Duration, m_MobyCount, "
                  f"m_CameraData, ...): {[hex(w) for w in port.words(state['pointer'], 8)]}")

        print(f"[live] {samples} samples of the card's own clock, {port.SAMPLE_FRAMES} fields apart")
        if shots:
            port.shot(f"{shots_dir}/1-card.png")
        series = []
        for _ in range(samples):
            series.append(clock_state(port))
            port.run(port.SAMPLE_FRAMES)
        ticks = [s["tick"] for s in series]
        moved = len({t for t in ticks}) > 1
        print(f"[live] tick over {len(ticks)} samples spanning {samples * port.SAMPLE_FRAMES} "
              f"fields: {ticks[0]} -> {ticks[-1]}, "
              f"{'MOVED' if moved else 'DID NOT MOVE'} ({len(set(ticks))} distinct value(s))")
        print(f"[live] m_SubState over the same samples: {[s['substate'] for s in series]}")
        if state["padType"] < CONTROLLER_TYPE_DPAD:
            print(f"[live] FINDING: m_Type={state['padType']} and retail gates its own title "
                  f"timeout on m_Type >= {CONTROLLER_TYPE_DPAD}, so the guest is treating the pad "
                  f"as absent. That is a different defect from a missing skip and produces the same "
                  f"symptom.")

        print(f"[live] HOLD start for {hold_frames} fields, sampling the clock throughout")
        port.press("start")
        held = []
        remaining = hold_frames
        while remaining > 0:
            step = min(port.SAMPLE_FRAMES, remaining)
            port.run(step)
            remaining -= step
            held.append(clock_state(port))
        port.release("start")
        after = clock_state(port)
        if shots:
            port.shot(f"{shots_dir}/2-after-skip.png")

        print(f"[live] during the hold: tick {[s['tick'] for s in held]}")
        print(f"[live] during the hold: m_Held {[hex(s['padHeld']) for s in held]}")
        print(f"[live] during the hold: m_Down {[hex(s['padDown']) for s in held]}")
        print(f"[live] during the hold: m_SubState {[s['substate'] for s in held]}")
        print(f"[live] after: gamestate={after['gamestate']} titleMode={after['mode']} "
              f"titleSubState={after['substate']} tick={after['tick']}")
        print(f"[live] retail's own condition on the LAST held sample, clause by clause:")
        print(f"[live]   {retail_skip_condition(held[-1])}")

        saw_start = any(s["padHeld"] & (PAD_START | PAD_CROSS) for s in held)
        reached_menu = after["mode"] == TSM_MENU
        print()
        if not saw_start:
            print("VERDICT: THE PRESS NEVER REACHED g_Pad.m_Held. An input-plumbing defect, not a "
                  "missing skip.")
            return 1
        if not moved:
            print(f"VERDICT: THE CARD'S OWN CLOCK NEVER MOVED. m_CurrentTick held {ticks[0]} across "
                  f"{samples * port.SAMPLE_FRAMES} fields while substate stayed {state['substate']}, "
                  f"so the `+= 2` at titlescreen.c:100 is not running. Whatever the operator is "
                  f"looking at is not retail's title update advancing.")
            return 1
        if after["tick"] is None or after["tick"] < TICK_CARD_ENDS:
            print(f"VERDICT: THE CLOCK MOVED BUT THE CARD DID NOT END. m_CurrentTick reached "
                  f"{after['tick']} against the {TICK_CARD_ENDS} gate.")
            return 1
        if not reached_menu:
            # The card is over and the menu has not opened. That is only a PORT defect if the guest
            # cannot be given a fresh press edge at all, so ask it: release, then tap, and watch.
            print("the card is over and the menu has not opened. Asking whether the guest can be "
                  "given a FRESH press edge at all -- release, then a real tap:")
            port.release("start")
            port.run(10)
            pre_tap = clock_state(port)
            print(f"[live]   before the tap: mode={pre_tap['mode']} substate={pre_tap['substate']} "
                  f"m_Held=0x{pre_tap['padHeld']:04X} m_Down=0x{pre_tap['padDown']:04X}")
            port.press("start")
            port.run(2)
            edge = clock_state(port)
            port.release("start")
            print(f"[live]   during the tap: m_Held=0x{edge['padHeld']:04X} "
                  f"m_Down=0x{edge['padDown']:04X} substate={edge['substate']}")
            trail = []
            for _ in range(18):
                port.run(10)
                trail.append(clock_state(port))
            print(f"[live]   after the tap: mode/substate {[ (s['mode'], s['substate']) for s in trail ]}")
            final = trail[-1]
            if shots:
                port.shot(f"{shots_dir}/3-menu.png")
            if not edge["padDown"] & (PAD_START | PAD_CROSS):
                print("VERDICT: THE PORT NEVER DELIVERS A PRESS EDGE TO THE GUEST. m_Held carries the "
                      "button but m_Down is 0 even on the frame the press goes down, so every "
                      "guest test that reads m_Down -- including retail's own substate-3 handover "
                      "into the menu -- can never be satisfied. THAT is the port-side defect, and it "
                      "is independent of the card.")
                return 1
            if final["mode"] != TSM_MENU:
                print(f"VERDICT: THE EDGE ARRIVED AND THE GUEST STILL DID NOT HANDOVER. m_Down read "
                      f"0x{edge['padDown']:04X} on the tap and m_Mode ended at {final['mode']} "
                      f"with m_SubState {final['substate']}, against TSM_Menu={TSM_MENU}.")
                return 1
            print("VERDICT: A FRESH EDGE DOES REACH THE GUEST AND DOES OPEN THE MENU. So the card's "
                  "own skip is intact and the whole route works; what the operator is pressing "
                  "against is retail's own 300-tick floor plus a second-press handover. This file "
                  "reports that, and it does not claim a skip the guest does not have.")
            return 0
        print("VERDICT: THE CARD WAS SKIPPED BY THE GUEST'S OWN ROUTE AND THE MENU OPENED. Nothing "
              "in the port added a skip.")
        return 0
    finally:
        port.end()


# ── selftest ────────────────────────────────────────────────────────────────────────────────────


def selftest() -> int:
    cases = [
        ({"pointer": 0, "tick": None, "substate": 2, "padHeld": PAD_START}, "NULL"),
        ({"pointer": 0x8007AB00, "tick": 1200, "substate": 2, "padHeld": PAD_START}, "< 1100"),
        ({"pointer": 0x8007AB00, "tick": 400, "substate": 3, "padHeld": PAD_START}, "not 2"),
        ({"pointer": 0x8007AB00, "tick": 100, "substate": 2, "padHeld": PAD_START}, "below the 300"),
        ({"pointer": 0x8007AB00, "tick": 400, "substate": 2, "padHeld": 0}, "no START/CROSS"),
        ({"pointer": 0x8007AB00, "tick": 400, "substate": 2, "padHeld": 0xFFFF0800}, "ALL CLAUSES"),
    ]
    failures = 0
    for sample, want in cases:
        got = retail_skip_condition(sample)
        if want not in got:
            print(f"FAIL {sample}: got {got!r}, expected it to name {want!r}")
            failures += 1
    positive = sum(1 for s, _ in cases if "ALL CLAUSES" in retail_skip_condition(s))
    negative = len(cases) - positive
    print(f"retail_skip_condition: {len(cases)} cases, {positive} positive, {negative} negative, "
          f"{failures} failed")
    if negative == 0:
        print("FAIL the classifier has no negative case, so it cannot report a dead precondition")
        failures += 1
    # The exact word shape a real run produced must be in the table, and the classifier must name it
    # the way it names a run: uppercase hex, four digits. A sibling probe read bit 0 instead of
    # bit 11 and reported a plainly-present press as absent, so the shape is pinned here.
    if not any(f"m_Held=0x{0xFFFF0800:04X}" in retail_skip_condition(s) for s, _ in cases):
        print(f"FAIL the 0x{0xFFFF0800:04X} shape a real run produces is not covered")
        failures += 1
    if (TICK_STARTS_SKIPPABLE, TICK_SKIP_TARGET, TICK_CARD_ENDS) != (300, 1170, 1169):
        print(f"FAIL the guest's gate values drifted: {TICK_STARTS_SKIPPABLE}/{TICK_SKIP_TARGET}/"
              f"{TICK_CARD_ENDS}")
        failures += 1
    # hi_lo is the operation this file's first version got wrong. Both halves, both signs.
    for upper, lower, want in ((0x8007, 0x5640, 0x80075640), (0x8008, -0x55C8, 0x8007AA38),
                               (0x8008, 0, 0x80080000), (0x8007, 0x5680, 0x80075680)):
        if hi_lo(upper, lower) != want:
            print(f"FAIL hi_lo(0x{upper:04X}, {lower}) = 0x{hi_lo(upper, lower):08X}, want "
                  f"0x{want:08X}")
            failures += 1
    if failures == 0:
        print(f"selftest: PASS ({len(cases) + 7}/{len(cases) + 7}) -- the classifier names all five "
              f"preconditions, and hi_lo reads a lui immediate as the UPPER half in both signs")
    return 1 if failures else 0


def card_tour(step: int) -> int:
    """Walk the card from its own start and screenshot every `step` ticks, printing each tick.

    WHY. The operator's report carries a screenshot of the "IN THE WORLD OF DRAGONS..." card, and
    the whole diagnosis turns on WHERE in the card that frame is. Retail ignores a press until
    `m_CurrentTick` passes 300, so knowing the tick of the frame the operator is looking at is the
    difference between "the press was inside retail's own dead window" and "the press was outside
    it and still did nothing". A tick counter alone cannot answer that; the picture has to be looked
    at, which is also the standing rule for every port in this workspace.
    """
    env = environment(disc_path())
    port = Port(DRIVE_ROOT / "build/bin/spyro_port",
                DRIVE_ROOT / "scratch/assets/spyro1/SCUS_942.28",
                DRIVE_ROOT / "scratch/logs/probe_title_card_tour.log",
                env)
    try:
        advanced = 0
        state = clock_state(port)
        while state["gamestate"] != GS_TITLE_SCREEN and advanced < 1400:
            port.run(10)
            advanced += 10
            state = clock_state(port)
        print(f"[tour] title screen at field {advanced}: substate {state['substate']} "
              f"tick {state['tick']}")
        if state["gamestate"] != GS_TITLE_SCREEN:
            print(f"[tour] REFUSING: gamestate {state['gamestate']}, not GS_TitleScreen.")
            return 2
        next_at = 0
        index = 0
        while state["substate"] == CARD_SUBSTATE and state["tick"] is not None \
                and state["tick"] < TICK_SLOW_ARM + 40:
            target = next_at + step
            while state["tick"] is not None and state["tick"] < target:
                port.run(5)
                state = clock_state(port)
            if state["substate"] != CARD_SUBSTATE:
                break
            path = f"scratch/screenshots/title_card/tour-{index:02d}-tick{state['tick']}.png"
            port.shot(path)
            print(f"[tour] tick {state['tick']:4d}  substate {state['substate']}  {path}")
            next_at = state["tick"]
            index += 1
        print(f"[tour] left substate {CARD_SUBSTATE} at tick {state['tick']} "
              f"(the card ends at {TICK_CARD_ENDS}, and retail's press gate opens at "
              f"{TICK_STARTS_SKIPPABLE})")
        return 0
    finally:
        port.end()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--static", action="store_true", help="image only; run no product")
    parser.add_argument("--card-tour", type=int, default=0,
                        help="screenshot the card every N ticks instead of pressing anything")
    parser.add_argument("--hold-frames", type=int, default=240)
    parser.add_argument("--samples", type=int, default=24)
    parser.add_argument("--wait-title", type=int, default=1400)
    parser.add_argument("--shots", action="store_true",
                        help="capture the card, the post-skip screen and the menu")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    if args.static:
        return static_report()
    if args.card_tour:
        return card_tour(args.card_tour)
    return live(args.hold_frames, args.samples, args.wait_title, args.shots)


if __name__ == "__main__":
    raise SystemExit(main())
