#!/usr/bin/env python3
"""probe_hud_glyph.py — WHAT THE GUEST'S HUD GLYPH RECORDS CONTAIN, read from a live run.

WHY THIS EXISTS. `game/render/hud_text_builder` lays a caption into the guest's descending
`g_HudMobys` arena and is tested, but nothing in the port CONSUMES the arena, so the pause menu drew
its panel and its border and no captions. A consumer cannot be written from a struct declaration: three
facts decide its shape, and all three are answers about the RUNNING image rather than about the
decompilation.

  1. HOW A GLYPH'S `m_Class` BECOMES GEOMETRY. Decompiling the guest's shaded-Moby renderer
     `0x80022A2C` out of SCUS_942.28 gives, at 0x80022D48-0x80022D60, `lhu $a0,0x36($fp)` /
     `sll $a0,$a0,2` / `lui $s6,0x8007; addiu $s6,$s6,0x6378` / `add $s6,$s6,$a0` / `lw $s6,($s6)`:
     the class indexes the SHARED model table at 0x80076378 by four. There is no class-to-UV table;
     the glyph's shape AND its vertex colours come out of that one model record. The table is BUILT
     AT RUNTIME — 0x80076378 is past the end of the 417,792-byte executable — so it cannot be read
     out of the file at all, and this probe is the only way to see what a letter's model contains.

  2. WHERE A HUD GLYPH LANDS. The same body has two arms. The culled arm projects a moby through the
     camera matrix. The unculled arm, taken when bit 7 of the 16-bit word at `moby+0x50` is clear
     (0x80022B20 `lhu` / 0x80022B34 `sll ...,24` / 0x80022B38 `bltz`), writes a FIXED rotation matrix
     (0x80022D64-0x80022D88: 0x1000, 0, 0x0A00, 0, 0x1000 into CR0..CR4), a translation of
     (0, 0, `m_Position.z>>1`) (0x80022D3C, then 0x80022DBC-0x80022DC4 into CR5..CR7) and — the part
     that decides the producer's shape — the moby's X AND Y into the GTE's SCREEN OFFSET
     (0x80022D24/0x80022D28 `sll ...,16` then 0x80022D2C/0x80022D30 `ctc2` into CR24/CR25, which
     psxport's own GTE calls OFX and OFY). So the HUD is POSITIONED IN SCREEN SPACE and SCALED BY
     DEPTH: it is neither an OT screen-space primitive nor a world-projected actor, and a producer
     that treats it as either one is wrong by construction. This probe prints every record's
     position and depth, and the live rotation words, so the claim is measured.

  3. WHICH LIST 0x801A2CA0 IS. The shaded-queue head is 0x800720F4 (`g_SonyImage + 0x2400`); the HUD
     arena is `g_Buffers.m_HudOTStart - 0x1C200`, in the transient pool, and the builders walk it
     DOWNWARD. This probe prints the arena cursor, the arena end, the pool base, and the shaded
     queue's first words with their denominators, because "the queue head" and "the pool base" are
     different addresses and reading one as the other is a category error, not an off-by-one.

WHAT A NEGATIVE PRINTS. Every read declares what it asked for and what it was served. A single `rw`
is capped at `psx::control::kMaxControlReadWords` (64) by the framework's own limit, so a read that
wants more MUST loop; a probe that sends one request and reads the answer as complete is the exact
defect that limit's own comment documents happening to `tools/probe_moby_list.py` on this title. This
one loops, counts its requests, checks the address the answer ECHOES, and REFUSES on a short answer
rather than padding it with zeros.

WHAT THIS RUN MEASURED, AND THE ONE THING IT DID NOT RESOLVE
--------------------------------------------------------------
Measured on a real pause-menu frame (product log `PSXPORT_DEBUG=render`, 117 menu frames, `captions=5`
every frame of page 0, one distinct text line for the whole run):

  * `0x80018880` LEAVES THE ARENA EMPTY. The product logs `arenaCursor == arenaEnd == 0x801A39B0`
    immediately after dispatching it, and that is not a failed write: the guest's own body
    (external/spyro-1 src/gamestates/draw.c:240) walks `g_HudMobys++` as it copies, so the cursor is
    back at the far end by design. A producer that reads the arena AFTER that call sees nothing; it
    has to read the shaded queue, or read the arena before the copy.
  * THE CAPTIONS ARE BEING BUILT. 17 Moby records are appended to the shaded queue, at 0x801A2CA0 and
    then every 0x58 bytes, measured live. So the missing text is NOT a builder failure and NOT an
    empty arena.
  * `0x801A2CA0` IS A MOBY IN THE TRANSIENT POOL, not a queue head. `g_Buffers.m_HudOTStart` read
    0x801BFBB0, minus 0x1C200, is 0x801A39B0 -- 3344 bytes above the queue's first record. The queue
    HEAD is 0x800720F4. Naming the pool base "the queue head" is what made an earlier arm look for a
    HUD list in the shaded queue.
  * THE DEPTHS ALTERNATE between the string's own z and `spacing.z` (4352 and 5120 observed), which
    is `layoutCaption`'s narrow-glyph rule, so the records are caption glyphs and the two rows at
    y=165 and y=147 are the main page's fourth and third items.

NOT RESOLVED, and it blocks a producer that reads guest RAM: the live record carries its class as a
u16 at **+0x34** and 0x7F at **+0x44** and 0x02 at **+0x4C**, while the guest's own text builder
`0x80017FE4` (decompiled from this image at build/decomp-hud/80017fe4.c) writes 0x36 / 0x47 / 0x4F /
0xFF-at-0x50, and `0x80022D48 lhu $a0,0x36($fp)` READS the class at 0x36. A pool scan found ZERO
letter-class records at (0x36, 0x50) and ZERO at (0x34, 0x4D) over 139 slots, so the offset question
is not answered by "both layouts agree" -- there are no records at all in the scanned window that
match either. Until that is settled, a producer that reads `m_Class` from guest RAM would read 0 for
every caption glyph and look up model 0 in the table, which is not a model. Recorded here rather than
papered over, because the next step is to re-read one record at four consecutive word addresses and
settle whether the transport, the builder, or the field table is the one that is wrong.

    uv run --frozen python tools/probe_hud_glyph.py
    uv run --frozen python tools/probe_hud_glyph.py --records 3
    uv run --frozen python tools/probe_hud_glyph.py --selftest
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

import drive  # noqa: E402  the maintained driver owns the launch environment and the route

# ── Addresses. Each is an address the guest's own code names, and the source is in the module doc. ──
G_GAMESTATE = 0x800757D8
G_HUD_MOBY_CURSOR = 0x80075710  # hud_text_builder's kHudMobyCursor; g_HudMobys
G_HUD_MOBY_END = 0x800756FC  # D_800756FC, the far end both builders walk up to
G_SHADED_QUEUE = 0x800720F4  # g_SonyImage.m_ShadedMobys
G_MODEL_TABLE = 0x80076378  # 0x80022D50/0x80022D54
G_MOBY_POOL_BASE = 0x800785F0  # g_Buffers.m_HudOTStart
G_HUD_ARENA_OFFSET = 0xFFFE3E00  # -0x1C200: g_HudMobys = g_Buffers.m_HudOTStart - 0x1C200
G_CAMERA_ROTATION = 0x80076DF8  # the words 0x80022A2C spills to 0x1f8003e0 on entry
G_LIGHT_ENTRY = 0x8006E3D8  # 0x80022B00's table: one word per moby+0x4C >> 22
G_LIGHT_PAIR = 0x8006E44C  # 0x80022B00's other table: the pair, at moby+0x4C >> 21

MOBY_SIZE = 0x58
MOBY_CLASS = 0x36
MOBY_LIGHT_WORD = 0x4C
MOBY_ROTATION = 0x44
MOBY_DEPTH_OFFSET = 0x47
MOBY_SPECULAR_TYPE = 0x4F
MOBY_RENDER_RADIUS = 0x50

MAX_READ_WORDS = 64  # psx::control::kMaxControlReadWords

# The guest's own MobyClass values, from external/spyro-1 include/moby.h, so a class read here is
# NAMED rather than a bare number.
CLASS_NAMES = {
    75: "EXCLAMATION_MARK",
    76: "LETTER_APOSTROPHE",
    260: "NUMBER_0",
    269: "NUMBER_9",
    272: "PERCENT",
    277: "SLASH",
    278: "QUESTION_MARK",
    317: "PLUS",
    321: "CARET",
    327: "PERIOD",
    426: "LETTER_A",
    471: "HUD_GEM_CHEST",
    506: "HUD_DRAGON",
}


class Refusal(Exception):
    pass


class Reader:
    """A `rw` reader that honours the framework's 64-word cap instead of truncating silently."""

    _WORDS = re.compile(r"\[repl\] ([0-9A-F]{8}):((?: [0-9A-F]{8})+)")
    _READY = re.compile(r"\[repl\] frame=(\d+) ready")

    def __init__(self, send, lines):
        self._send = send
        self._lines = lines() if callable(lines) else lines
        self.requests = 0
        self.words_read = 0

    def words(self, address: int, count: int) -> list[int]:
        """Read `count` words at `address`, looping over the transport's cap, refusing a short read."""
        if count < 0:
            raise Refusal(f"refused a negative read of {count} words at 0x{address:08X}")
        out: list[int] = []
        while len(out) < count:
            step = min(MAX_READ_WORDS, count - len(out))
            self.requests += 1
            self._send(f"rw {address + 4 * len(out):X} {step}")
            served = self._await(address + 4 * len(out), step)
            out.extend(served)
        self.words_read += len(out)
        return out

    def _await(self, address: int, step: int) -> list[int]:
        for line in self._lines:
            ready = self._READY.search(line)
            if ready:
                raise Refusal(
                    f"the port reported a frame boundary (frame={ready.group(1)}) instead of the "
                    f"{step} word(s) asked for at 0x{address:08X}"
                )
            match = self._WORDS.search(line)
            if match is None:
                continue
            # THE ANSWER MUST BE FOR THE ADDRESS THAT WAS ASKED. The port's stdout still holds the
            # answer to the PREVIOUS read, and a reader that takes the first word-list it sees
            # silently reads one request late. This run read a stale answer that way once and the
            # record fields disagreed with the hex dump of the very same words; the echo is checked
            # so that cannot happen again silently.
            echoed = int(match.group(1), 16)
            if echoed != address:
                raise Refusal(
                    f"asked for 0x{address:08X} and the port answered for 0x{echoed:08X}; the "
                    f"earlier answer was NOT the one requested"
                )
            served = [int(word, 16) for word in match.group(2).split()]
            if len(served) != step:
                raise Refusal(
                    f"`rw 0x{address:08X} {step}` was asked for {step} words and served "
                    f"{len(served)}; the other {step - len(served)} were NOT fetched"
                )
            return served
        raise Refusal(f"the port closed its output before answering a read at 0x{address:08X}")


def unpack_record(words: list[int]) -> dict[str, int]:
    """The 0x58-byte Moby, as big-endian fields — the same byte order the guest's own mem_* uses."""
    raw = struct.pack(f">{len(words)}I", *words)
    return {
        "x": struct.unpack_from(">i", raw, 0x0C)[0],
        "y": struct.unpack_from(">i", raw, 0x10)[0],
        "z": struct.unpack_from(">i", raw, 0x14)[0],
        "rotation": struct.unpack_from(">I", raw, MOBY_ROTATION)[0],
        "light_word": struct.unpack_from(">I", raw, MOBY_LIGHT_WORD)[0],
        "class": struct.unpack_from(">H", raw, MOBY_CLASS)[0],
        "depth_offset": raw[MOBY_DEPTH_OFFSET],
        "specular_type": raw[MOBY_SPECULAR_TYPE],
        "render_radius": raw[MOBY_RENDER_RADIUS],
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--executable", default="build/bin/spyro_port")
    parser.add_argument("--binary", default="scratch/assets/spyro1/SCUS_942.28")
    parser.add_argument("--log", default="scratch/logs/probe-hud-glyph.log")
    parser.add_argument("--settings", default=str(drive.SHIPPING_SETTINGS))
    parser.add_argument("--frames", type=int, default=240, help="fields to run once the menu is up")
    parser.add_argument("--records", type=int, default=4, help="models to dump in full")
    parser.add_argument("--slots", type=int, default=12, help="shaded-queue slots to read")
    parser.add_argument(
        "--scan",
        type=lambda value: int(value, 0),
        default=0x2000,
        help="bytes below the pool high-water mark to scan for builder-written records",
    )
    parser.add_argument("--selftest", action="store_true", help="hermetic read-loop check; no run")
    args = parser.parse_args()

    if args.selftest:
        return selftest()

    env = drive.environment(drive.disc_path(), Path(args.settings))
    port = drive.Port(Path(args.executable), Path(args.binary), Path(args.log), env)
    try:
        return probe(port, args)
    except Refusal as refusal:
        print(f"REFUSED: {refusal}")
        return 2
    finally:
        code = port.end()
        print(f"[probe] port exit {code}; {port.reader.requests} read request(s) issued")


def probe(port: "drive.Port", args: argparse.Namespace) -> int:
    reader = Reader(port._send, port._lines)
    port.reader = reader  # type: ignore[attr-defined]

    drive.Navigator(port).reach_gameplay()
    port.tap("start", 6)
    port.run(args.frames)
    state = reader.words(G_GAMESTATE, 1)[0]
    if state != 2:
        raise Refusal(
            f"GS_PauseMenu (gamestate 2) was not reached; gamestate={state}. Every fact below is "
            f"about the menu's own records, so it is only evidence with the menu up."
        )

    # THE ARENA IS PER-FRAME STATE, so one sample is not a measurement. 0x8001A5E0 re-arms
    # [0x80075710] and [0x800756FC] to the pool base on every menu frame, and the builders then step
    # the cursor DOWN, so an arena read at the wrong point in the frame is legitimately empty. This
    # samples `samples` frames and reports the census, because "the cursor equalled the end" is an
    # answer about ONE frame and reads identically to "no producer ever wrote here".
    samples = 12
    spans: list[int] = []
    for _ in range(samples):
        cursor = reader.words(G_HUD_MOBY_CURSOR, 1)[0]
        end = reader.words(G_HUD_MOBY_END, 1)[0]
        spans.append(end - cursor)
        port.run(1)
    nonempty = [span for span in spans if span > 0]
    print(f"[probe] === the HUD moby arena over {samples} sampled frame(s) ===")
    print(f"[probe] spans (bytes): {spans}")
    print(f"[probe] non-empty {len(nonempty)}/{samples} frames"
          + (f"; widest {max(nonempty)} bytes = {max(nonempty) // MOBY_SIZE} records"
             if nonempty else "; the arena was EMPTY on every sampled frame"))

    pool_word = reader.words(G_MOBY_POOL_BASE, 1)[0]
    pool_base = (pool_word + G_HUD_ARENA_OFFSET) & 0xFFFFFFFF
    # The widest observed arena is the one with records in it; read THAT one's cursor.
    cursor = pool_base - max(nonempty) if nonempty else pool_base
    end = pool_base
    queue = reader.words(G_SHADED_QUEUE, 4)

    print("[probe] === the two lists, and which is which ===")
    print(f"[probe] transient pool   : g_Buffers.m_HudOTStart 0x{pool_word:08X}"
          f" - 0x1C200 = 0x{pool_base:08X}   (the arena's far end, and g_HudMobys's high water)")
    print(f"[probe] HUD moby arena   : 0x{cursor:08X} .. 0x{end:08X}"
          f"   span {end - cursor} bytes = {(end - cursor) // MOBY_SIZE} records of {MOBY_SIZE}")
    print(f"[probe] shaded queue     : head 0x{G_SHADED_QUEUE:08X}  entry[0]=0x{queue[0]:08X}"
          f"  entry[1]=0x{queue[1]:08X}  entry[2]=0x{queue[2]:08X}  entry[3]=0x{queue[3]:08X}")
    steps = [queue[i + 1] - queue[i] for i in range(3) if queue[i] and queue[i + 1]]
    print(f"[probe] consecutive queue entry deltas: {steps} bytes"
          f"  (a run of {MOBY_SIZE}-byte strides is a run of Moby records, not pointers into a"
          f" different structure)")
    print(f"[probe] => pool base 0x{pool_base:08X} and queue[0] 0x{queue[0]:08X}: the two differ by"
          f" {pool_base - queue[0]} bytes, and the queue head is 0x{G_SHADED_QUEUE:08X}. A word in"
          f" the transient pool is a Moby record in the HUD arena, not a queue head.")

    # The RECORDS. The guest's own "draw these Mobys this frame" list is the shaded queue, and
    # 0x80018880 is what copies the freshly built arena into it. Reading the queue's entries is
    # therefore reading the same Moby pointers whichever way the arena stood on the sampled frame,
    # and it is the list a producer has to consume either way.
    slots = args.slots
    entries = reader.words(G_SHADED_QUEUE, slots + 1)
    chain = [word for word in entries if 0x80010000 <= word < 0x80200000]
    print(f"[probe] === the shaded queue's Moby pointers: {len(chain)} of {slots} requested slots"
          f" held a RAM address (asked {slots}, scanned {slots}) ===")
    if not chain:
        raise Refusal(
            f"none of {slots} shaded-queue slots held a RAM address, so there is no live Moby to "
            f"read; the menu's own draw handler is the only thing that fills that list"
        )
    if end - cursor > 0:
        print(f"[probe] {end - cursor} of those records are ALSO in the sampled arena range"
              f" 0x{cursor:08X}..0x{end:08X}")

    record_words: list[int] = []
    record_addresses: list[int] = []
    for address in chain:
        words = reader.words(address, MOBY_SIZE // 4)
        record_words.extend(words)
        record_addresses.append(address)
    count = len(chain)
    records = record_words
    print(f"[probe] read {len(records)} words = {count} record(s) x {MOBY_SIZE} bytes"
          f" (asked for {count * (MOBY_SIZE // 4)}, served {len(records)})")

    fields = [unpack_record(records[i * (MOBY_SIZE // 4) : (i + 1) * (MOBY_SIZE // 4)])
              for i in range(count)]
    print("[probe] === every record the queue points at ===")
    for index, record in enumerate(fields):
        print(
            f"[probe]  [{index:3d}] 0x{record_addresses[index]:08X} "
            f"class={record['class']:4d}({CLASS_NAMES.get(record['class'], '?')}) "
            f"pos=({record['x']:6d},{record['y']:6d},{record['z']:6d}) "
            f"radius=0x{record['render_radius']:02X} specularType=0x{record['specular_type']:02X} "
            f"depthOffset=0x{record['depth_offset']:02X} rot=0x{record['rotation']:08X} "
            f"light=0x{record['light_word']:08X} depth>>1={record['z'] >> 1}"
        )
    distinct = sorted({record["class"] for record in fields})
    print(f"[probe] {len(distinct)} distinct class(es) over {count} record(s): "
          f"{', '.join(f'{c}({CLASS_NAMES.get(c, chr(63))})' for c in distinct)}")

    # A record whose class is 0 is a finding, not a nuisance, so the whole 0x58 bytes go out for
    # the first few. Which byte holds what is exactly the thing a hex dump answers and a field
    # table cannot: the guest's Moby is only self-describing through the code that writes it.
    for index in range(min(3, count)):
        words = records[index * (MOBY_SIZE // 4) : (index + 1) * (MOBY_SIZE // 4)]
        raw = struct.pack(f">{MOBY_SIZE // 4}I", *words)
        print(f"[probe] record[{index}] @ 0x{record_addresses[index]:08X} raw 0x{MOBY_SIZE} bytes:")
        for row in range(MOBY_SIZE // 16):
            chunk = raw[row * 16 : row * 16 + 16]
            print(f"[probe]   +0x{row * 16:02X}  " + " ".join(f"{b:02X}" for b in chunk))

    # SCAN the pool below its high-water mark for records that LOOK like builder output. A letter
    # class (426..451) next to a 0xFF render radius is the shape `func_80017FE4` produces, and the
    # scan asks for it at BOTH candidate class offsets: the port's 0x36 and the 0x34 this run's
    # records actually carry. A count for one layout and zero for the other is a finding about the
    # layout, not about whether captions exist, so both are printed with the same denominator.
    window = args.scan
    base = pool_base - window
    blob = b"".join(struct.pack(">I", word) for word in reader.words(base, window // 4))
    if len(blob) != window:
        raise Refusal(
            f"the pool scan wanted {window} bytes and assembled {len(blob)}; the tail was NOT read"
        )
    slots_scanned = (window - MOBY_SIZE) // MOBY_SIZE + 1
    for label, class_offset, radius_offset in (
        ("port offsets (class 0x36, radius 0x50)", 0x36, 0x50),
        ("observed offsets (class 0x34, radius 0x4D)", 0x34, 0x4D),
    ):
        found = []
        for slot in range(slots_scanned):
            offset = slot * MOBY_SIZE
            klass = struct.unpack_from(">H", blob, offset + class_offset)[0]
            radius = blob[offset + radius_offset]
            if 426 <= klass <= 451 and radius == 0xFF:
                position = struct.unpack_from(">3i", blob, offset + 0x0C)
                found.append((base + offset, klass, radius, position))
        print(f"[probe] scan[{label}]: {len(found)} of {slots_scanned} {MOBY_SIZE}-byte slots in"
              f" 0x{base:08X}..0x{pool_base:08X} hold a letter class with radius 0xFF")
        for address, klass, radius, position in found[:6]:
            print(f"[probe]   0x{address:08X} class={klass} radius=0x{radius:02X}"
                  f" pos={position}")

    table = reader.words(G_MODEL_TABLE, max(max(distinct), 1) + 1)
    print(f"[probe] === class -> model at 0x{G_MODEL_TABLE:08X}; read {len(table)} entries"
          f" (asked for {max(max(distinct), 1) + 1}) ===")
    dumped = 0
    for klass in distinct:
        model = table[klass]
        print(f"[probe] class {klass:4d} -> model 0x{model:08X}")
        if not 0x80010000 <= model < 0x80200000:
            print(f"[probe]   NOT a RAM address: this table entry is not a model record pointer")
            continue
        if dumped >= args.records:
            continue
        dumped += 1
        header = reader.words(model, 5)
        vertices = header[1] & 0x7FFFFFFF
        stream = header[2]
        colours = header[3]
        print(f"[probe]   byte0=0x{header[0]:08X} shadow=0x{header[0] >> 24:02X}"
              f"  byte1=0x{header[1]:08X} primitives={header[1] & 0xFF}"
              f"  byte2=0x{header[2] >> 24:02X} byte3=0x{header[3] >> 24:02X}")
        print(f"[probe]   model+4 vertices 0x{vertices:08X}   model+8 stream 0x{stream:08X}"
              f"   model+0xC colours 0x{colours:08X}")
        if not 1 <= (header[1] & 0xFF) <= 64 or not 1 <= (header[0] & 0xFF) <= 127:
            print(f"[probe]   refused: counts outside the range the renderer accepts")
            continue
        print(f"[probe]   {header[0] & 0xFF} packed vertices = "
              f"{reader.words(vertices, (header[0] & 0xFF) * 3 // 4 + 1)[:4]} (first 4 words)")
        for p in range(header[1] & 0xFF):
            pair = reader.words(stream + p * 8, 2)
            print(f"[probe]   prim[{p}] indices=0x{pair[0]:08X} armBits={pair[0] & 3}"
                  f"  normal=0x{pair[1]:08X}")
    print(f"[probe] dumped {dumped} model(s) in full of {len(distinct)} distinct class(es)")

    rotation = reader.words(G_CAMERA_ROTATION, 8)
    print(f"[probe] === the rotation words 0x80022A2C spills on entry"
          f" (0x{G_CAMERA_ROTATION:08X}..) ===")
    print(f"[probe]   {', '.join(f'0x{word:08X}' for word in rotation)}")
    light_index = fields[0]["light_word"] >> 22
    light_pair_index = fields[0]["light_word"] >> 21
    light_entry = reader.words(G_LIGHT_ENTRY + 4 * light_index, 1)[0]
    light_pair = reader.words(G_LIGHT_PAIR + 4 * light_pair_index, 2)
    print(f"[probe] === the light the first record selects: moby+0x4C=0x{fields[0]['light_word']:08X}"
          f" -> entry>>22={light_index} -> 0x{G_LIGHT_ENTRY:08X}[{light_index}]"
          f" = 0x{light_entry:08X}")
    print(f"[probe]   -> entry>>21={light_pair_index} -> 0x{G_LIGHT_PAIR:08X}[{light_pair_index}]"
          f" = 0x{light_pair[0]:08X}, +1 = 0x{light_pair[1]:08X}")
    print(f"[probe] {reader.requests} request(s), {reader.words_read} word(s) read")
    return 0


def selftest() -> int:
    """Hermetic: the read loop's accounting, on a fake port, plus the record field layout.

    The loop is the part that can silently lie. `psx::control::kMaxControlReadWords` is 64, so a
    100-word read is two requests, and a read that asked for more than it was served has to REFUSE
    rather than return a short list the caller would read as zeros.
    """
    asked: list[str] = []
    answered = 0

    def make(short: int | None = None):
        def send(line: str) -> None:
            asked.append(line)

        def lines():
            # One line per request, forever: the reader asks again until it has the count it wanted.
            # The protocol is `rw <hex address> <decimal count>` -- an address is hex, a count is
            # decimal. Parsing the count as hex here once produced a 100-word answer to a 64-word
            # request, which the reader correctly refused: the fake was wrong, not the reader.
            nonlocal answered
            while True:
                while answered >= len(asked):
                    yield ""  # an empty line is not a word list; the reader keeps waiting
                address = int(asked[answered].split()[1], 16)
                step = int(asked[answered].split()[2], 10)
                answered += 1
                served = step if short is None else min(short, step)
                yield f"[repl] {address:08X}:" + "".join(
                    f" {address + 4 * i:08X}" for i in range(served)
                )

        return send, lines

    send, lines = make()
    reader = Reader(send, lines)
    words = reader.words(0x80000000, 100)
    if len(words) != 100:
        print(f"FAIL: 100-word read returned {len(words)}")
        return 1
    if len(asked) != 2 or "64" not in asked[0] or "36" not in asked[1]:
        print(f"FAIL: expected a 64+36 split, got {asked}")
        return 1

    asked.clear()
    answered = 0
    send, lines = make(short=10)
    reader = Reader(send, lines)
    try:
        reader.words(0x80000000, 64)
    except Refusal as refusal:
        if "NOT fetched" not in str(refusal):
            print(f"FAIL: a short read refused for the wrong reason: {refusal}")
            return 1
    else:
        print("FAIL: a 10-of-64 short read was accepted as complete")
        return 1

    raw = bytearray(struct.pack(">22I", *([0] * 22)))
    struct.pack_into(">H", raw, MOBY_CLASS, 426)
    struct.pack_into(">i", raw, 0x0C, -7)
    struct.pack_into(">i", raw, 0x10, 0x1234)
    struct.pack_into(">i", raw, 0x14, 0x1100)
    raw[MOBY_RENDER_RADIUS] = 0xFF
    got = unpack_record(list(struct.unpack(">22I", bytes(raw))))
    if (got["class"], got["x"], got["y"], got["z"], got["render_radius"]) != (426, -7, 0x1234, 0x1100, 0xFF):
        print(f"FAIL: record decode {got}")
        return 1
    # THE STALE-ANSWER CASE, which is the one that actually bit this probe: a reader that takes the
    # first word-list on the wire is one request late, and one request late still returns plausible
    # words, so only checking the echoed address catches it.
    asked.clear()
    answered = 0

    def stale_lines():
        nonlocal answered
        while True:
            while answered >= len(asked):
                yield ""
            yield "[repl] 801A0000:" + " 801A0000" * 4  # the PREVIOUS request's address
            answered += 1

    reader = Reader(lambda line: asked.append(line), stale_lines())
    try:
        reader.words(0x801A2CA0, 4)
    except Refusal as refusal:
        if "NOT the one requested" not in str(refusal):
            print(f"FAIL: a stale answer refused for the wrong reason: {refusal}")
            return 1
    else:
        print("FAIL: an answer echoing a different address was accepted")
        return 1

    print(f"selftest: 3/3 read-loop cases (a 100-word read splits 64+36; a 10-of-64 read refuses; an"
          f" answer echoing another address refuses), 1/1 Moby field-layout case")
    return 0


if __name__ == "__main__":
    sys.exit(main())
