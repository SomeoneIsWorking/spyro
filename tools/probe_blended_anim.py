#!/usr/bin/env python3
"""probe_blended_anim.py — can a LIVE frame reach the BLENDED (GTE-interpolated) environment
animation form, and does the loaded level's authored data even contain one?

WHY THIS EXISTS. `docs/project-state.md` item S020 records, verbatim:

    "The animation's BLENDED (GTE-interpolated) form is covered hermetically only; no live frame
     has exercised it yet."

That sentence names a hole but not a question, and there are two DIFFERENT questions hiding in
it, which is why a single counter cannot answer it:

  (1) SELECTION. Does a live game update stamp a sector channel with an index whose keyframe has a
      nonzero blend factor, so the product's `world_animation::Plan::blended` counter increments?
      Only the running product can answer this: the stamp bytes are per-frame state and the guest
      retires each channel by writing 0xFF back (world_animation.cpp:264), so a frame parked
      afterwards reads the retire, not the selection.
  (2) AUTHORED DATA. Does the loaded level contain ANY keyframe with a nonzero blend factor? The
      factor is a byte in read-only level data, so a RAM dump answers it, and the answer is a
      property of the disc rather than of a route.

This tool answers (2) from a live `dumpram` capture, and it answers (1) by WATCHING the per-frame
animation-set pointers and censusing each distinct one. Both answers come with a denominator, and
an entry this tool could not read is reported as NOT READ -- never as a factor of 0. That
distinction is the whole point: a short read reported as zeros manufactures a "no blended form
exists" result out of an address it never looked at.

THE DECODED CHAIN, read out of the image (SCUS_942.28) and not out of the decompiled listing.
`tools/probe_guest_disasm.py` reports 62183 of 62183 listed main-image instructions agreeing with
`file_offset = 0x800 + (addr - 0x80010000)`, and every word quoted below was re-read from the
provisioned executable and compared against that listing's own `/* FILEOFF VRAMADDR INSNWORD */`
comment; the two copies agree. The renderer is 0x800258F0. Per channel, in order:

  0x80025BB8  sll  $at,$gp,24 ; 0x80025BC0 srl $at,$at,24   -> index = (stampWord >> 24) & 0xFF
  0x80025BBC  bltz $at,<skip>                               -> index >= 0x80 means IDLE
  0x80025BC8  lui  $v0,0x8008 ; 0x80025BCC addiu $v0,$v0,-0x7AA0   -> 0x80078560
  0x80025BD0  lw   $v0,0x14($v0)      channel 0's animation SET pointer
  0x80025BD4  sll  $at,$at,2 ; 0x80025BD8 add $at,$at,$v0 ; 0x80025BDC lw $at,($at)
                                                        -> animation = set[index]
  0x80025BE4  lbu  $v1,2($at) ; 0x80025BE8 addi $v0,$at,0xC
  0x80025BEC  sll  $v1,$v1,3   ; 0x80025BF0 add $v0,$v0,$v1  -> keyframe = anim+12+anim[2]*8

  ** THE BLEND DECISION **
  0x80025BF4  lbu  $a1,4($v0)     word 04004590   the blend factor byte
  0x80025C00  bgtz $a1,0x80025C3C word 0E00A01C   factor > 0 selects BLENDED
                                                        factor == 0 falls through to the
                                                        straight word-copy loop at 0x80025C1C

  ** WHAT BLENDED WRITES **
  0x80025C3C  sll  $a1,$a1,4 ; 0x80025C40 mtc2 $a1,C2_IR0    IR0 = factor << 4
  0x80025CC4  INTPL   word 1100984A     channel 0 interpolates two packed 11/11/10 vertices
  0x80025CF0  mfc2 $t0,C2_MAC1 / 0x80025CF4 MAC2 / 0x80025CF8 MAC3, repacked at 0x80025CFC-0x80025D08

The other three channels repeat the shape at +0x3C bytes each, and their decision sites are:

  channel | set slot load | factor load | `bgtz` decision | IR0 write | GTE command
  --------+---------------+-------------+------------------+-----------+------------
  0 LQ vtx | 0x80025BD0    | 0x80025BF4  | 0x80025C00       | 0x80025C40| INTPL 0x80025CC4
  1 LQ col | 0x80025D2C    | 0x80025D50  | 0x80025D5C       | 0x80025DD8| DPCS  0x80025E38
  2 HQ vtx | 0x80025E74    | 0x80025E98  | 0x80025EA4       | 0x80025EF0| INTPL 0x80025F80
  3 HQ col | 0x80025FE4    | 0x80026008  | 0x80026014       | 0x800260B4| DPCS  0x80026134,
                                                                          0x8002617C

So the form is selected by ONE byte: `ram8(keyframe + 4)`, where `keyframe = animation + 12 +
ram8(animation + 2) * 8`. Any nonzero value takes the GTE path with `IR0 = byte << 4`, i.e. a
weight in 12.4 fixed point -- which is exactly `world_animation.cpp`'s `ir0 = header.factor << 4`
and its `header.factor == 0 ? direct : blended` at world_animation.cpp:197 and :225.

Usage:
    uv run --frozen python tools/probe_blended_anim.py --selftest
    uv run --frozen python tools/probe_blended_anim.py --ram scratch/bin/blend_capture.bin
    uv run --frozen python tools/probe_blended_anim.py --live --watch-frames 600
"""

from __future__ import annotations

import argparse
import struct
import subprocess
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
RAM_BASE = 0x80000000
RAM_SIZE = 0x200000
G_ENVIRONMENT_ANIMATIONS = 0x80078560
CHANNELS = 4
# Each channel's pointer slot is preceded by its OWN authored count, in the same struct
# (external/spyro-1/include/environment.h:27-57, whose field order is what the image's four
# 8-byte-strided loads walk):
#
#   +0x10 m_LowPolyAnimationCount   +0x14 m_LowPolyAnimations    <- channel 0, LQ vertices
#   +0x18 m_LowColorAnimationCount  +0x1C m_LowColorAnimations   <- channel 1, LQ colours
#   +0x20 m_HighPolyAnimationCount  +0x24 m_HighPolyAnimations   <- channel 2, HQ vertices
#   +0x28 m_HighColorAnimationCount +0x2C m_HighColorAnimations  <- channel 3, HQ colours
#
# So the table length is AUTHORED and the honest denominator is that count, not a guess. An earlier
# revision of this file scanned the guest's 0x80-index ceiling and reported 115 of 128 channel-0
# indices as "NOT READ" -- which was over-reading: those words are not table entries at all, they
# are whatever follows the 12-entry table. Reading the count is what turned that 512-index
# denominator into 24.
CHANNEL_COUNT_SLOT = (0x10, 0x18, 0x20, 0x28)
CHANNEL_NAME = ("LowPoly (LQ vertices)", "LowColor (LQ colours)",
                "HighPoly (HQ vertices)", "HighColor (HQ colours)")
# The guest's own gate: index 0x80..0xFF means idle (bltz after the 24-bit shift/srl pair). A count
# above that ceiling cannot be indexed by any byte a sector can stamp, so the two bound each other.
INDEX_CEILING = 0x80
# Header -> keyframe stride, as the arms read it at 0x80025BE4/0x80025BF0 and world_animation.cpp:83.
KEYFRAME_STRIDE = 8


class Ram:
    """A read-only 2 MB main-RAM view. Every read is bounds-checked, and an address outside the
    dump is a distinct outcome from a zero -- that distinction is the tool's main obligation."""

    def __init__(self, blob: bytes):
        self.blob = blob
        self.size = len(blob)

    def holds(self, address: int, length: int) -> bool:
        return (RAM_BASE <= address and address + length <= RAM_BASE + self.size)

    def r8(self, address: int) -> int:
        return self.blob[address - RAM_BASE]

    def r16(self, address: int) -> int:
        return struct.unpack_from("<H", self.blob, address - RAM_BASE)[0]

    def r32(self, address: int) -> int:
        return struct.unpack_from("<I", self.blob, address - RAM_BASE)[0]

    def set_pointer(self, channel: int) -> int:
        """The one load the image performs per channel: 0x14 + channel*8."""
        return self.r32(G_ENVIRONMENT_ANIMATIONS + 0x14 + channel * 8)

    def authored_count(self, channel: int) -> int:
        """The authored entry count, from the word immediately before the channel's pointer slot."""
        return self.r32(G_ENVIRONMENT_ANIMATIONS + CHANNEL_COUNT_SLOT[channel])

    def all_set_pointers(self) -> tuple[int, ...]:
        return tuple(self.set_pointer(c) for c in range(CHANNELS))

    def all_counts(self) -> tuple[int, ...]:
        return tuple(self.authored_count(c) for c in range(CHANNELS))


@dataclass
class Entry:
    """One table slot, classified. `factor is None` means NOT READ, which is not zero."""

    index: int
    state: str  # "null" | "readable" | "unreadable"
    animation: int = 0
    keyframe: int = 0
    keyframe_index: int = 0
    factor: int | None = None
    size: int = 0
    source_a: int = 0
    source_b: int = 0
    why: str = ""


@dataclass
class Channel:
    index: int
    set_pointer: int
    authored: int
    entries: list[Entry] = field(default_factory=list)

    @property
    def scanned(self) -> int:
        return len(self.entries)

    @property
    def read(self) -> list[Entry]:
        return [e for e in self.entries if e.state == "readable"]

    @property
    def unreadable(self) -> int:
        return sum(1 for e in self.entries if e.state == "unreadable")

    @property
    def null(self) -> int:
        return sum(1 for e in self.entries if e.state == "null")

    @property
    def nonzero(self) -> list[Entry]:
        return [e for e in self.read if e.factor not in (0, None)]

    @property
    def nonzero_reachable(self) -> list[Entry]:
        """Nonzero factors at entries the product's own decode could take.

        `world_animation::checkStride` refuses a zero or mis-strided payload size before it ever
        looks at the factor, so a nonzero factor behind one of those entries is unreachable by
        construction and must not be counted as a route to the BLENDED form. This is the split that
        keeps an incidental byte pattern from being reported as reachability.
        """
        return [e for e in self.nonzero if e.why == ""]


def decode_entry(ram: Ram, channel: int, set_pointer: int, index: int) -> Entry:
    """Decode one animation-set table slot, following only the loads the image performs.

    `readable` here means every word the arm actually dereferences was inside the dump. It is
    deliberately looser than the product's refusal rules (world_animation.cpp refuses on a zero or
    mis-strided size, and on an unreadable payload), because this tool's question is "what blend
    factor does this authored keyframe carry", not "would the product accept it". The two are
    reported separately: `product_would_accept` says which of these the native decode could take.
    """
    entry = Entry(index=index, state="unreadable")
    table_entry = set_pointer + index * 4
    if not ram.holds(table_entry, 4):
        entry.why = "table_entry_outside_dump"
        return entry
    animation = ram.r32(table_entry)
    entry.animation = animation
    if animation == 0:
        entry.state = "null"
        entry.why = "null_animation_pointer"
        return entry
    # 0x80025BE4/0x80025BE8/0x80025BEC/0x80025BF0: keyframe = anim + 12 + anim[2]*8
    if not ram.holds(animation, 12):
        entry.why = f"animation_outside_dump(0x{animation:08X})"
        return entry
    keyframe_index = ram.r8(animation + 2)
    keyframe = animation + 12 + keyframe_index * KEYFRAME_STRIDE
    if not ram.holds(keyframe, 8):
        entry.why = f"keyframe_outside_dump(0x{keyframe:08X})"
        return entry
    size = ram.r16(animation + 6)
    base = animation + ram.r32(animation + 8)
    stride = 8 if channel == 3 else 4
    if size == 0 or size % stride != 0:
        entry.why = f"product_refuses_stride(size={size},stride={stride})"
    elif not ram.holds(base + ram.r8(keyframe + 5) * size, size):
        entry.why = "source_a_outside_dump"
    entry.keyframe = keyframe
    entry.keyframe_index = keyframe_index
    entry.size = size
    entry.factor = ram.r8(keyframe + 4)  # 0x80025BF4: THE blend factor
    entry.source_a = base + ram.r8(keyframe + 5) * size
    entry.source_b = base + ram.r8(keyframe + 6) * size
    entry.state = "readable"
    return entry


def decode_channel(ram: Ram, channel: int) -> Channel:
    """Decode one channel's animation-set table, bounded by its OWN authored count.

    Two bounds, both authored or guest-derived: the count word in front of the pointer slot, and
    the guest's 0x80 index ceiling. The smaller wins, and which one bound it is gets reported, so a
    count of zero cannot be quietly widened into a scan of unrelated memory.
    """
    set_pointer = ram.set_pointer(channel)
    authored = ram.authored_count(channel)
    if authored > INDEX_CEILING:
        out = Channel(index=channel, set_pointer=set_pointer, authored=authored)
        out.entries = [Entry(index=i, state="unreadable",
                             why=f"authored_count {authored} exceeds the guest's 0x{INDEX_CEILING:02X} "
                                 f"index ceiling; NOT scanned, NOT zero")
                       for i in range(INDEX_CEILING)]
        return out
    out = Channel(index=channel, set_pointer=set_pointer, authored=authored)
    for index in range(authored):
        out.entries.append(decode_entry(ram, channel, set_pointer, index))
    return out


def census(ram: Ram) -> list[Channel]:
    return [decode_channel(ram, c) for c in range(CHANNELS)]


# --------------------------------------------------------------------------------------------
# Reporting. Every count carries what it was counted against, and unreadable is never zero.
# --------------------------------------------------------------------------------------------


def report(channels: list[Channel], label: str) -> int:
    """Print one census. Returns the number of REACHABLE nonzero-factor entries -- nonzero factors
    sitting behind a refusal the product would apply are counted and named, but not returned,
    because "the byte was nonzero" is not "a live frame can take the blended form"."""
    total_nonzero = 0
    total_reachable = 0
    total_authored = 0
    total_read = 0
    print(f"[census] {label}")
    for ch in channels:
        total_nonzero += len(ch.nonzero)
        total_reachable += len(ch.nonzero_reachable)
        total_authored += ch.authored
        total_read += len(ch.read)
        hist = Counter(e.factor for e in ch.read)
        hist_text = ", ".join(f"factor={k}:{v}" for k, v in sorted(hist.items())) or "(none read)"
        print(
            f"  channel {ch.index} {CHANNEL_NAME[ch.index]}: set=0x{ch.set_pointer:08X}  "
            f"denominator = the AUTHORED entry count in front of that pointer slot, {ch.authored}; "
            f"read {len(ch.read)}, null {ch.null}, NOT READ {ch.unreadable}"
            + (f"  ({ch.unreadable} authored entries were NOT fetched and are NOT zero)"
               if ch.unreadable else "  (every authored entry was read)")
        )
        print(f"    blend-factor histogram over the {len(ch.read)} read keyframe(s): {hist_text}")
        for entry in ch.nonzero:
            verdict = ("REACHABLE (product-acceptable)" if entry.why == ""
                       else f"UNREACHABLE ({entry.why})")
            print(
                f"    NONZERO factor={entry.factor} at index {entry.index}: "
                f"animation=0x{entry.animation:08X} keyframe=0x{entry.keyframe:08X} "
                f"(keyframe #{entry.keyframe_index}) size={entry.size}  {verdict}"
            )
    print(
        f"  TOTAL: {total_authored} authored environment animations across the {CHANNELS} channels; "
        f"{total_read} keyframe(s) read; {total_nonzero} with a nonzero blend factor; "
        f"{total_reachable} of those at an entry the product's decode would ACCEPT"
    )
    return total_reachable


# --------------------------------------------------------------------------------------------
# Hermetic selftest. The decoder must be shown BOTH answers on the SHIPPING path.
# --------------------------------------------------------------------------------------------


def _synthetic_ram(factor: int, size: int = 16, count: int = 1) -> tuple[bytes, int]:
    """Build a 2 MB RAM holding exactly one animation-set table with one authored keyframe.

    The layout is the image's: the count word in front of the slot, the slot's pointer to the
    table, table[index] -> animation; animation[2] selects the keyframe record; animation[6..7] is
    the payload size; animation[8..11] is the base offset; the keyframe record is
    animation+12+index*8 and its byte 4 is the factor.
    """
    blob = bytearray(RAM_SIZE)
    set_ptr = 0x80160000
    animation = 0x80161000
    keyframe = animation + 12
    # The image's own loads: `lw $v0, 0x14($v0)` with $v0 = g_EnvironmentAnimations (0x80078560),
    # and the count word the same struct keeps in front of it.
    struct.pack_into("<I", blob, G_ENVIRONMENT_ANIMATIONS + CHANNEL_COUNT_SLOT[0] - RAM_BASE, count)
    struct.pack_into("<I", blob, G_ENVIRONMENT_ANIMATIONS + 0x14 - RAM_BASE, set_ptr)
    struct.pack_into("<I", blob, set_ptr - RAM_BASE, animation)
    blob[animation + 2 - RAM_BASE] = 0
    struct.pack_into("<H", blob, animation + 6 - RAM_BASE, size)
    struct.pack_into("<I", blob, animation + 8 - RAM_BASE, 0)  # base == animation
    blob[keyframe + 4 - RAM_BASE] = factor
    blob[keyframe + 5 - RAM_BASE] = 0  # source A at base
    blob[keyframe + 6 - RAM_BASE] = 1  # source B at base+size
    return bytes(blob), set_ptr


def selftest() -> int:
    """Four cases, and the fourth is the one the real Artisans capture turns on. A probe that has
    only ever printed one answer is not trusted, so this asserts a POSITIVE (a nonzero factor is
    read and located), a NEGATIVE (a zero factor reads as the direct form), a TRUNCATED dump (NOT
    READ, never zero), and a REFUSED ENTRY (a nonzero factor the product's stride check rejects
    must be counted as nonzero but NOT as reachable)."""
    failures: list[str] = []

    # 1. POSITIVE: factor 0x13 must be read back, named, and located.
    blob, set_ptr = _synthetic_ram(factor=0x13)
    ram = Ram(blob)
    if ram.set_pointer(0) != set_ptr:
        failures.append(f"positive: set pointer read as 0x{ram.set_pointer(0):08X}, expected 0x{set_ptr:08X}")
    chans = census(ram)
    hits = chans[0].nonzero
    if len(chans[0].read) != 1:
        failures.append(f"positive: {len(chans[0].read)} entries read, expected exactly 1; the "
                        f"fixture is not exercising the decode and this case proves nothing")
    if len(hits) != 1:
        failures.append(f"positive: expected exactly 1 nonzero entry, got {len(hits)}")
    elif hits[0].factor != 0x13 or hits[0].index != 0:
        failures.append(f"positive: read index {hits[0].index} factor {hits[0].factor}, expected index 0 factor 19")
    elif report(chans, "selftest positive case") != 1:
        failures.append("positive: the nonzero factor was not returned as REACHABLE")
    else:
        print(f"positive: factor=0x{hits[0].factor:02X} read at index {hits[0].index} "
              f"(animation=0x{hits[0].animation:08X} keyframe=0x{hits[0].keyframe:08X}) -- as authored")

    # 2. NEGATIVE: the same layout with factor 0 must read as zero, and report no nonzero entry.
    blob0, _ = _synthetic_ram(factor=0)
    chans0 = census(Ram(blob0))
    if len(chans0[0].read) != 1:
        failures.append(f"negative: {len(chans0[0].read)} entries read, expected exactly 1; a "
                        f"zero verdict from an unread fixture is not the other answer, it is no answer")
    if chans0[0].nonzero:
        failures.append(f"negative: factor 0 reported {len(chans0[0].nonzero)} nonzero entries")
    elif report(chans0, "selftest negative case") != 0:
        failures.append("negative: the direct form was returned as reachable-blended")
    else:
        print("negative: factor=0x00 read as direct form, 0 nonzero entries -- the other answer")

    # 3. ZERO COUNT: an authored count of 0 must scan NOTHING. This is the case the live Artisans
    #    capture produced for channels 1 and 3, and it is the one that a fixed 128-index scan got
    #    wrong -- it read 6 "entries" out of a table the level never authored, three of them with a
    #    nonzero factor byte behind a zero payload size.
    blob_zero, _ = _synthetic_ram(factor=0x13, count=0)
    chans_zero = census(Ram(blob_zero))
    if chans_zero[0].scanned != 0:
        failures.append(f"zero-count: {chans_zero[0].scanned} entries scanned from an authored "
                        f"count of 0; the scan is reading memory that is not a table")
    elif chans_zero[0].nonzero:
        failures.append(f"zero-count: {len(chans_zero[0].nonzero)} nonzero entries from an "
                        f"authored count of 0")
    else:
        print(f"zero-count: authored count 0 -> {chans_zero[0].scanned} entries scanned, "
              f"0 nonzero -- an unauthored table is not scanned at all")

    # 4. TRUNCATED DUMP: a set pointer outside the dump must be NOT READ, never factor 0.
    blob2 = bytearray(_synthetic_ram(factor=0)[0])
    struct.pack_into("<I", blob2, G_ENVIRONMENT_ANIMATIONS + 0x14 - RAM_BASE, 0x00100000)
    chans2 = census(Ram(bytes(blob2)))
    ch0 = chans2[0]
    if ch0.read:
        failures.append(f"truncated: {len(ch0.read)} entries read from an unreadable set pointer")
    if ch0.unreadable != ch0.scanned or ch0.scanned == 0:
        failures.append(f"truncated: {ch0.unreadable} of {ch0.scanned} scanned indices reported "
                        f"NOT READ")
    print(f"truncated: set pointer 0x00100000 outside the dump -> {ch0.unreadable} of "
          f"{ch0.scanned} scanned indices NOT READ, 0 reported as factor 0 -- a short read "
          f"cannot become a zero")

    # 5. REFUSED ENTRY: a nonzero factor behind a payload size the product's stride check rejects
    #    is counted as nonzero and must NOT be returned as reachable. This is the exact shape the
    #    live Artisans capture produced, so it is the control that makes the reachability split
    #    trustworthy rather than a claim about it.
    blob3, _ = _synthetic_ram(factor=0x13, size=0)
    chans3 = census(Ram(blob3))
    if len(chans3[0].read) != 1:
        failures.append(f"refused: {len(chans3[0].read)} entries read, expected exactly 1; the "
                        f"fixture is not exercising the decode and this case proves nothing")
    if len(chans3[0].nonzero) != 1:
        failures.append(f"refused: expected 1 nonzero entry, got {len(chans3[0].nonzero)}")
    elif report(chans3, "selftest refused-entry case") != 0:
        failures.append("refused: an entry the product refuses was returned as REACHABLE")
    else:
        print("refused: nonzero factor behind size=0 is counted nonzero and reported UNREACHABLE")

    total = 5
    for line in failures:
        print(f"FAIL: {line}")
    print(f"selftest: {total - len(failures)} of {total} cases passed")
    return 1 if failures else 0


# --------------------------------------------------------------------------------------------
# Live run: watch the per-frame animation-set pointers and census each distinct one.
# --------------------------------------------------------------------------------------------


def live(args: argparse.Namespace) -> int:
    """Drive the shipping route and census the authored animation data AT the frames that matter.

    The route itself is `drive.Navigator`'s -- this wraps the `Port.run` it calls, so the guest
    advances the same frames in the same order with the same pad edges, and every one of those
    frames is observed. Nothing here decides a transition, so there is no second copy of the
    route to drift.

    WHY PER FRAME, AND WHY NOT A FRAME PARK. The environment animation fires on ONE frame: the
    guest stamps each channel's byte in the sector and the renderer retires it to 0xFF the same
    pass, so the four channel indices that were live exist only during that update. Reading the
    per-frame stamp bytes while the loop is parked reads the retire. So this does not try to read
    the selection from a park -- it reads the AUTHORED animation-set pointers, which are
    level-scoped and stable, once per frame, and captures a full RAM dump on the frames where
    those pointers change. The trigger window is short: an Artisans capture taken 1200 frames
    after arrival showed channels 1 and 3 pointing at what is not an animation table at all,
    while the product had decoded all four channels as direct on its single earlier fire.

    A dump is only censused when the dump's OWN pointers equal the pointers the REPL read one
    command earlier. That cross-check is not decoration: an earlier revision of this file read
    contiguous word PAIRS instead of the 8-byte-spaced slots, sampled two padding words that read
    as 0, and the guard is what turned that into a named refusal instead of a silent zero.
    """
    sys.path.insert(0, str(TOOLS))
    import drive  # noqa: PLC0415 -- reused so the route and the launch environment are one copy

    root = ROOT
    env = drive.environment(None)
    env["PSXPORT_SETTINGS"] = str((root / "tools" / "shipping_settings.ini").resolve())
    if args.debug:
        env["PSXPORT_DEBUG"] = args.debug
    log = root / args.log
    log.parent.mkdir(parents=True, exist_ok=True)
    port = drive.Port(
        root / args.executable,
        root / "scratch" / "assets" / "spyro1" / "SCUS_942.28",
        log,
        env,
    )
    capture = root / "scratch" / "bin" / "blend_capture.bin"
    capture.parent.mkdir(parents=True, exist_ok=True)

    stats = {"frames": 0, "dumps": 0, "refused": 0, "no_file": 0}
    seen: dict[tuple[int, ...], int] = {}
    summaries: dict[tuple[int, ...], str] = {}
    reachable: dict[tuple[int, int], str] = {}
    last_tuple: list[tuple[int, ...] | None] = [None]
    # Captured before the wrapper below replaces `port.run`, because the capture path needs one
    # real frame per `dumpram` and must not recurse into the observer.
    original_run = port.run

    def dump_and_census(key: tuple[int, ...], why: str) -> None:
        if capture.exists():
            capture.unlink()
        port._send(f"dumpram {capture.relative_to(root)}")
        # The REPL reads commands BETWEEN frames, so the capture is not written until the next
        # frame boundary. Checking the file without advancing is how an earlier revision of this
        # file reported 18 of 18 captures as "produced no file" -- the dump was always there, one
        # frame later. The guest therefore advances one extra frame per capture, which is why the
        # pointer cross-check below is mandatory rather than belt-and-braces.
        original_run(1)
        stats["dumps"] += 1
        if not capture.is_file():
            stats["no_file"] += 1
            print(f"[live] dumpram produced no file ({why}); NOT CENSUSED, and this sample is a gap")
            return
        ram = Ram(capture.read_bytes())
        own = ram.all_set_pointers()
        if own != key:
            stats["refused"] += 1
            print(
                f"[live] REFUSING a capture ({why}): the dump's own animation-set pointers are "
                + " ".join(f"0x{v:08X}" for v in own)
                + " and do not match the pointers the REPL read for this sample ("
                + " ".join(f"0x{v:08X}" for v in key)
                + "). NOT CENSUSED -- this capture does not describe the state that was sampled."
            )
            return
        chans = census(ram)
        head = " ".join(f"0x{v:08X}" for v in key)
        detail = (f"sets {head}; authored counts "
                  + " ".join(f"ch{c.index}={c.authored}" for c in chans)
                  + "; read/NOT-READ "
                  + " ".join(f"ch{c.index}={len(c.read)}/{c.unreadable}" for c in chans)
                  + "; blend factors "
                  + " ".join(
                      (",".join(f"{k}x{v}" for k, v in sorted(Counter(e.factor for e in c.read).items()))
                       or "none")
                      for c in chans)
                  + f"; REACHABLE nonzero {sum(len(c.nonzero_reachable) for c in chans)}")
        summaries[key] = detail
        for ch in chans:
            for entry in ch.nonzero_reachable:
                reachable[(ch.index, entry.index)] = (
                    f"ch{ch.index} index {entry.index} factor {entry.factor} "
                    f"animation=0x{entry.animation:08X} keyframe=0x{entry.keyframe:08X} size={entry.size}"
                )
        print(f"[live] census ({why}) at frame {port.frame}: {detail}")
        if reachable:
            print(f"[live] *** A REACHABLE NONZERO BLEND FACTOR EXISTED IN THIS CAPTURE ***")

    def observe() -> None:
        """One frame of observation: read the four set pointers, capture when they move."""
        words = port.words(G_ENVIRONMENT_ANIMATIONS + 0x14, 2 * CHANNELS)
        key = tuple(words[2 * c] for c in range(CHANNELS))
        seen[key] = seen.get(key, 0) + 1
        stats["frames"] += 1
        if key != last_tuple[0] or stats["frames"] % args.heartbeat == 0:
            why = "set pointers changed" if key != last_tuple[0] else f"heartbeat every {args.heartbeat}"
            dump_and_census(key, why)
        last_tuple[0] = key

    def run_and_observe(frames: int) -> int:
        # One guest frame per observation. The guest advances the same frames in the same order;
        # `tap`/`press` are separate REPL commands, so chunking cannot drop an input.
        for _ in range(max(1, frames)):
            result = original_run(1)
            observe()
        return result

    try:
        port.run = run_and_observe  # type: ignore[method-assign]
        drive.Navigator(port).reach_gameplay()
        if args.seek_portal:
            # The shipping steering, reused rather than copied: drive.py's own --seek-portal builds
            # this exact Seeker from its own target reader and stops on the level id changing, so
            # this route is the one the driver already exercises and there is no second copy of the
            # walk to drift. Wrapping `port.run` is what makes the crossing observable per frame.
            entering = port.word(drive.G_LEVEL_ID)
            print(f"[live] walking to a portal out of level {entering} (G_LEVEL_ID)")
            drive.Seeker(port, "portal", drive.portal_targets(port.words), arrived=0,
                         stop=lambda: port.word(drive.G_LEVEL_ID) != entering,
                         stop_is=f"left level {entering}").walk()
            print(f"[live] portal route finished in level {port.word(drive.G_LEVEL_ID)} "
                  f"at frame {port.frame}")
        for _ in range(0, max(0, args.watch_frames), 1):
            original_run(1)
            observe()
        print(
            f"[live] observed {stats['frames']} frames one at a time; "
            f"{len(seen)} distinct animation-set pointer tuple(s); "
            f"{stats['dumps']} RAM capture(s), {stats['refused']} refused on the pointer "
            f"cross-check, {stats['no_file']} produced no file"
        )
        print(
            f"[live] capture rate against the observation denominator: "
            f"{stats['dumps']} of {stats['frames']} frames captured "
            f"({100.0 * stats['dumps'] / max(1, stats['frames']):.1f}%); the rest are the SAME "
            f"pointer tuple already censused, not unobserved frames"
        )
        for key, count in seen.items():
            print(f"[live]   tuple seen on {count} of {stats['frames']} frames: {summaries.get(key, 'NOT CENSUSED')}")
        if reachable:
            print(f"[live] VERDICT: {len(reachable)} REACHABLE nonzero blend factor(s) found:")
            for line in sorted(reachable.values()):
                print(f"[live]   {line}")
        else:
            print(
                f"[live] VERDICT: 0 REACHABLE nonzero blend factors across {len(summaries)} censused "
                f"distinct pointer tuple(s). Every nonzero factor byte that WAS read sits behind an "
                f"entry the product's decode refuses, and the indices that could not be read are "
                f"reported NOT READ, never as zero."
            )
    finally:
        try:
            port._send("quit")
        except Exception:  # noqa: BLE001 -- teardown only; the verdict is already printed
            pass
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--selftest", action="store_true", help="hermetic decode checks, both answers")
    group.add_argument("--ram", type=Path, help="census one live `dumpram` capture")
    group.add_argument("--live", action="store_true", help="drive to gameplay and census what loads")
    parser.add_argument("--executable", default="build/bin/spyro_port")
    parser.add_argument("--log", default="scratch/logs/probe_blended_anim.log")
    parser.add_argument("--debug", default="fieldenv", help="PSXPORT_DEBUG channels")
    parser.add_argument("--settle", type=int, default=30, help="unused; kept so the flag set is stable")
    parser.add_argument("--watch-frames", type=int, default=600,
                        help="frames to observe one at a time AFTER the route has arrived")
    parser.add_argument("--seek-portal", action="store_true",
                        help="after arriving, walk to a portal and cross into the next level, so the "
                             "census covers a second environment's authored animation data")
    parser.add_argument("--heartbeat", type=int, default=200,
                        help="capture a dump every N observed frames even when the set pointers "
                             "have not moved, so a window that changed nothing still gets censused")
    args = parser.parse_args()

    if args.selftest:
        return selftest()
    if args.ram is not None:
        path = args.ram if args.ram.is_absolute() else ROOT / args.ram
        if not path.is_file():
            print(f"REFUSED: {path} does not exist; nothing was read, so nothing is reported")
            return 2
        blob = path.read_bytes()
        print(f"[census] {path} ({len(blob)} bytes; main RAM is {RAM_SIZE}, "
              f"a shorter file means the tail was NOT read and is NOT zero)")
        return 0 if report(census(Ram(blob)), str(path)) else 0
    return live(args)


if __name__ == "__main__":
    raise SystemExit(main())
