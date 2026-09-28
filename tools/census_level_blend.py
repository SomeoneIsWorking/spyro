#!/usr/bin/env python3
"""census_level_blend.py — does ANY level in Spyro 1 author a nonzero environment-animation blend
factor, and how many?

WHY THIS EXISTS. `docs/issues/0134-no-live-frame-exercises-the-blended-environment-animation.md`
censorsed the BLENDED form's authored trigger for two levels reached by a live route and recorded
`24 of 24 keyframes carry blend factor 0` for Artisans. That is a census of ONE keyframe per
animation -- the one the runtime index byte `animation[2]` selects at the instant of the capture.
It is not a census of what the level AUTHORED. This tool censuses the whole authored keyframe
table of every animation, in every level data entry in the archive, with denominators.

The blend decision, read out of the image (SCUS_942.28) and not out of a listing: the renderer
`0x800258F0` takes `keyframe = animation + 12 + animation[2] * 8` (0x80025BE4/0x80025BE8/0x80025BEC/
0x80025BF0), then `0x80025BF4 lbu $a1,4($v0)` reads the factor and `0x80025C00 bgtz $a1` is the
whole decision. So the authored form of an animation is its 8-byte-strided keyframe table starting
at +12, and its length is bounded by the record's OWN `base offset` word at +8:
`(base - 12) / 8` slots. A count of 0 must scan nothing; a slot outside the entry is NOT READ.

THE ARCHIVE FORMAT, from `external/spyro-1/src/loaders.c` (`LoadLevelScene`), not guessed:

    component := [size:u32][count:u32][count x field:u32]      // size covers the size word too
    record    := field + (address of the count word)           // PATCH_POINTER_RELATIVE_TO_COMPONENT
    COMPONENT_END: next component starts at componentStart + size

and the seven animation components are read back to back, in THIS order:

    1 texture animations   2 scrolling textures   3 HighPoly (channel 2)   4 LowPoly (channel 0)
    5 collision animations 6 HighColor (channel 3) 7 LowColor (channel 1)

so members 3, 4, 6, 7 of a seven-component run are the renderer's four channels. That ordering is
the attribution rule, and it is checked rather than assumed: `--control` cross-checks a live RAM
capture's four set pointers and counts against the static parse of the same entry.

WHAT IS COUNTED, TWICE, ON PURPOSE:

  selected   one keyframe per animation, at the index the record's own byte 2 carries -- this is
             0134's metric, kept so the two numbers can be compared like for like.
  authored   EVERY keyframe slot in the record, bounded by the record's base-offset word. This is
             what "the level authors a blend factor" actually asks, and it is strictly larger.

Usage:
    uv run --frozen python tools/census_level_blend.py --selftest
    uv run --frozen python tools/census_level_blend.py --wad scratch/wad_census/WAD.WAD
    uv run --frozen python tools/census_level_blend.py --wad ... --control --ram <live capture>
"""
from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
SECTOR = 0x800

# The four channels as the renderer's four repeated arms number them (0x80025BD0 / 0x80025D2C /
# 0x80025E74 / 0x80025FE4), and the file-order member of the seven-component run that holds each.
CHANNEL_NAME = ("LowPoly (LQ vertices)", "LowColor (LQ colours)",
                "HighPoly (HQ vertices)", "HighColor (HQ colours)")
CHANNEL_RUN_MEMBER = {2: 3, 0: 4, 3: 6, 1: 7}   # run member (1-based) -> channel
RUN_MEMBERS = 7
# The guest's own index ceiling: `sll/srl $at,$gp,24` then `bltz` retires any index >= 0x80, so a
# count above that cannot be indexed by any byte a sector can stamp. A count above it is REFUSED,
# never silently scanned and never read as zero.
INDEX_CEILING = 0x80
KEYFRAME_STRIDE = 8
KEYFRAME_TABLE_OFFSET = 12
BASE_OFFSET_SLOT = 8
SIZE_SLOT = 6
SELECTED_INDEX_SLOT = 2
FACTOR_BYTE = 4
SOURCE_A_BYTE = 5
SOURCE_B_BYTE = 6
FLAGS_BYTE = 1
# `lbu $v1,1($t5); andi $v1,$at,2; bgtz $v1` at 0x8002A724/0x8002A72C/0x8002A730: the update loop
# SKIPS an animation whose byte 1 has bit 1 set, so its index byte never advances. Reported, never
# used to decide a factor.
PAUSE_FLAG = 0x02

RAM_BASE = 0x80000000
RAM_SIZE = 0x200000
G_ENVIRONMENT_ANIMATIONS = 0x80078560
STRUCT_COUNT_SLOT = (0x10, 0x18, 0x20, 0x28)
STRUCT_POINTER_SLOT = (0x14, 0x1C, 0x24, 0x2C)


# ---------------------------------------------------------------------------------------------
# The archive
# ---------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Entry:
    index: int
    offset: int
    length: int

    @property
    def end(self) -> int:
        return self.offset + self.length


def read_entries(blob: bytes) -> list[Entry]:
    """The archive's first sector is a flat (offset, length) index; trailing slots are zero."""
    words = struct.unpack_from(f"<{SECTOR // 4}I", blob, 0)
    out: list[Entry] = []
    for i in range(0, len(words) - 1, 2):
        offset, length = words[i], words[i + 1]
        if offset == 0 or length == 0:
            break
        if offset % SECTOR or offset + length > len(blob):
            break
        out.append(Entry(index=i // 2, offset=offset, length=length))
    return out


# ---------------------------------------------------------------------------------------------
# One animation record and the channel that owns it
# ---------------------------------------------------------------------------------------------


@dataclass
class Record:
    """One authored environment animation. `state` distinguishes a real read from NOT READ."""

    index: int
    state: str                       # "read" | "unread"
    channel: int = -1
    offset: int = 0
    header: bytes = b""
    slots: int = 0
    size: int = 0
    selected: int = 0
    flags: int = 0
    why: str = ""
    factors: list[int] = field(default_factory=list)   # every authored slot's factor byte
    selected_factor: int | None = None                 # factor at `selected`, None when NOT READ
    extent_ok: bool = True                             # record + keyframes + 2 payloads inside entry

    @property
    def read(self) -> bool:
        return self.state == "read"

    @property
    def authored_nonzero(self) -> int:
        return sum(1 for f in self.factors if f)

    @property
    def selected_nonzero(self) -> bool:
        return self.selected_factor not in (0, None)

    @property
    def paused(self) -> bool:
        return bool(self.flags & PAUSE_FLAG)

    @property
    def product_would_accept(self) -> bool:
        """The port's own refusal rules: a zero or mis-strided payload size is refused
        (world_animation.cpp `checkStride`), and the payload must be inside the entry."""
        stride = 8 if self.channel == 3 else 4
        return self.size != 0 and self.size % stride == 0


@dataclass
class Channel:
    channel: int
    run_member: int
    component: int = 0
    count: int = 0
    count_read: bool = True
    why: str = ""
    records: list[Record] = field(default_factory=list)

    @property
    def read(self) -> list[Record]:
        return [r for r in self.records if r.read]

    @property
    def unreadable(self) -> int:
        return sum(1 for r in self.records if not r.read)

    @property
    def authored(self) -> int:
        return sum(len(r.factors) for r in self.read)

    @property
    def authored_nonzero(self) -> int:
        return sum(r.authored_nonzero for r in self.read)

    @property
    def selected_count(self) -> int:
        return sum(1 for r in self.read if r.selected_factor is not None)

    @property
    def selected_nonzero(self) -> int:
        return sum(1 for r in self.read if r.selected_nonzero)

    @property
    def reachable_nonzero(self) -> int:
        return sum(r.authored_nonzero for r in self.read if r.product_would_accept)


def decode_record(blob: bytes, entry: Entry, component: int, component_end: int,
                  channel: int, index: int, count_word_offset: int) -> Record:
    """Decode one table slot, following only the loads the guest and its loader perform.

    `read` means every byte the renderer and this census dereference was inside the entry. It is
    deliberately NOT the port's acceptance rule; that is reported separately as
    `product_would_accept`, because "the byte was nonzero" is not "a live frame can take the form".
    """
    field_at = component + 8 + 4 * index
    if field_at + 4 > entry.end:
        return Record(index=index, state="unread", channel=channel,
                      why=f"table slot at +0x{field_at - entry.offset:X} outside the entry")
    stored = struct.unpack_from("<I", blob, field_at)[0]
    # PATCH_POINTER_RELATIVE_TO_COMPONENT: *(int*)dest += componentStart + 4, and componentStart+4
    # is where the count word lives.
    offset = count_word_offset + stored
    if offset < entry.offset or offset + 12 > entry.end:
        return Record(index=index, state="unread", channel=channel, offset=offset,
                      why=f"record 0x{offset:X} outside the entry")
    header = blob[offset:offset + 12]
    base = struct.unpack_from("<I", header, BASE_OFFSET_SLOT)[0]
    size = struct.unpack_from("<H", header, SIZE_SLOT)[0]
    if base < KEYFRAME_TABLE_OFFSET + KEYFRAME_STRIDE or (base - KEYFRAME_TABLE_OFFSET) % KEYFRAME_STRIDE:
        return Record(index=index, state="unread", channel=channel, offset=offset,
                      why=f"base offset 0x{base:X} does not bound a whole number of 8-byte keyframes")
    slots = (base - KEYFRAME_TABLE_OFFSET) // KEYFRAME_STRIDE
    if offset + base > entry.end:
        return Record(index=index, state="unread", channel=channel, offset=offset,
                      why=f"keyframe table of {slots} slot(s) runs past the entry")
    table = offset + KEYFRAME_TABLE_OFFSET
    record = Record(
        index=index, state="read", channel=channel, offset=offset, header=header,
        slots=slots, size=size,
        selected=header[SELECTED_INDEX_SLOT], flags=header[FLAGS_BYTE],
    )
    factors = blob[table:table + slots * KEYFRAME_STRIDE]
    record.factors = list(factors[FACTOR_BYTE::KEYFRAME_STRIDE])
    selected = header[SELECTED_INDEX_SLOT]
    if selected >= slots:
        # The renderer would read past the table. NOT READ, never factor 0.
        return Record(index=index, state="unread", channel=channel, offset=offset,
                      slots=slots, size=size, selected=selected, flags=header[FLAGS_BYTE],
                      why=f"selected keyframe {selected} is outside the {slots} authored slot(s)")
    record.selected_factor = factors[selected * KEYFRAME_STRIDE + FACTOR_BYTE]
    stride = 8 if channel == 3 else 4
    deepest = 0
    for k in range(slots):
        frame = factors[k * KEYFRAME_STRIDE:k * KEYFRAME_STRIDE + KEYFRAME_STRIDE]
        deepest = max(deepest, frame[SOURCE_A_BYTE], frame[SOURCE_B_BYTE])
    needed = offset + base + (deepest + 1) * size
    record.extent_ok = needed <= component_end
    return record


def decode_component(blob: bytes, entry: Entry, component: int, count: int, channel: int,
                     run_member: int) -> Channel:
    """Decode one animation-set component, bounded by its OWN authored count.

    A count of 0 scans nothing. A count above the guest's 0x80 index ceiling is REFUSED and nothing
    is scanned, because no sector byte can index it; a fixed-size scan over an unauthored table is
    exactly how a "no blended form exists" answer gets manufactured.
    """
    size_word = struct.unpack_from("<I", blob, component)[0]
    end = component + size_word
    out = Channel(channel=channel, run_member=run_member, component=component, count=count)
    if count > INDEX_CEILING:
        out.count_read = False
        out.why = (f"authored count {count} exceeds the guest's 0x{INDEX_CEILING:02X} index "
                   f"ceiling; {count} entries NOT scanned and NOT counted as zero")
        return out
    if count == 0:
        return out
    if component + 8 + 4 * count > entry.end:
        out.count_read = False
        out.why = f"table of {count} entries runs past the entry; {count} entries NOT scanned"
        return out
    count_word = component + 4
    for index in range(count):
        out.records.append(decode_record(blob, entry, component, end, channel, index, count_word))
    return out


# ---------------------------------------------------------------------------------------------
# Finding the seven-component run
# ---------------------------------------------------------------------------------------------


def looks_like_component(blob: bytes, entry: Entry, at: int) -> bool:
    """The WEAK shape: a framed component that could be one of the seven.

    Deliberately weak. Members 1, 2 and 5 (texture animations, scrolling textures, collision) are
    read by the loader with the same framing but are not vertex/colour animation tables, so a
    record-level test rejects them. Requiring one here would truncate the run below seven and
    refuse every real level. What keeps this from matching every framed blob in the entry is that
    `census_entry` additionally requires four SPECIFIC members of a seven-chain to pass the
    record-level test.
    """
    if at + 8 > entry.end:
        return False
    size = struct.unpack_from("<I", blob, at)[0]
    count = struct.unpack_from("<I", blob, at + 4)[0]
    if size < 8 or size % 4 or count > INDEX_CEILING:
        return False
    return at + size <= entry.end and size >= 8 + 4 * count


def looks_like_animation_component(blob: bytes, entry: Entry, at: int) -> bool:
    """The STRONG shape: a framed component whose every entry resolves to a readable animation
    record that fits inside the component. This is what attributes a member to a channel."""
    if not looks_like_component(blob, entry, at):
        return False
    size = struct.unpack_from("<I", blob, at)[0]
    count = struct.unpack_from("<I", blob, at + 4)[0]
    if count == 0:
        # A zero-count component still has to be part of a plausible run: the loader emits all
        # seven unconditionally, so `size == 8` is the authored shape.
        return size == 8
    end = at + size
    previous = -1
    for index in range(count):
        record = decode_record(blob, entry, at, end, 0, index, at + 4)
        if not record.read or not record.extent_ok:
            return False
        if record.offset < previous:
            return False
        previous = record.offset
    return True


def component_chains(blob: bytes, entry: Entry) -> list[list[int]]:
    """Every maximal chain of consecutive, abutting framed components, any length."""
    hits = [at for at in range(entry.offset, entry.end - 7, 4)
            if looks_like_component(blob, entry, at)]
    chains: list[list[int]] = []
    current: list[int] = []
    for at in hits:
        if current:
            size = struct.unpack_from("<I", blob, current[-1])[0]
            if at == current[-1] + size:
                current.append(at)
                continue
            chains.append(current)
            current = []
        current = [at]
    if current:
        chains.append(current)
    return chains


def find_runs(blob: bytes, entry: Entry) -> list[list[int]]:
    """Seven consecutive components whose FOUR channel members are real animation tables.

    The seven is what attributes members 3/4/6/7 to the renderer's channels: the loader always
    emits all seven back to back, in the order `LoadLevelScene` reads them, so the search walks
    FORWARD by each component's own size word rather than collecting every framed blob in the
    entry -- a background scan for the shape alone finds hundreds of coincidences inside the
    level's own component bodies and truncates the real run. A chain shorter than seven is a
    REFUSAL, not a census.
    """
    out: list[list[int]] = []
    for start in range(entry.offset, entry.end - 7, 4):
        if not looks_like_component(blob, entry, start):
            continue
        chain: list[int] = []
        at = start
        for _ in range(RUN_MEMBERS):
            if at + 8 > entry.end or not looks_like_component(blob, entry, at):
                break
            chain.append(at)
            at += struct.unpack_from("<I", blob, at)[0]
        if len(chain) != RUN_MEMBERS:
            continue
        if all(looks_like_animation_component(blob, entry, chain[member - 1])
               for member in CHANNEL_RUN_MEMBER.values()):
            out.append(chain)
    return out


def census_entry(blob: bytes, entry: Entry) -> tuple[list[Channel], list[str]]:
    """Census one entry's channels. Returns the four channels and a list of refusals."""
    notes: list[str] = []
    runs = find_runs(blob, entry)
    if not runs:
        return [], [f"entry {entry.index}: no run of {RUN_MEMBERS} consecutive components whose "
                    f"channel members are animation tables was found in its {entry.length} bytes. "
                    f"Its animation data is UNKNOWN, not zero."]
    if len(runs) > 1:
        notes.append(f"entry {entry.index}: {len(runs)} candidate seven-component runs; "
                     f"censusing the first at 0x{runs[0][0]:X}")
    run = runs[0]
    by_member = {member + 1: at for member, at in enumerate(run)}
    channels: list[Channel] = []
    for channel in (0, 1, 2, 3):
        member = CHANNEL_RUN_MEMBER[channel]
        at = by_member[member]
        count = struct.unpack_from("<I", blob, at + 4)[0]
        channels.append(decode_component(blob, entry, at, count, channel, member))
    return channels, notes


def walk_members(blob: bytes, first: int, count: int = RUN_MEMBERS) -> list[int]:
    """Component offsets by following the loader's own `componentStart + size` rule.

    Used by the selftest to reach one member directly, so a case can exercise a decode-level
    refusal without depending on the locator accepting a component the decode must then reject.
    """
    out = [first]
    while len(out) < count:
        out.append(out[-1] + struct.unpack_from("<I", blob, out[-1])[0])
    return out


# ---------------------------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------------------------


@dataclass
class Totals:
    entries: int = 0
    entries_with_runs: int = 0
    entries_without_runs: int = 0
    authored_animations: int = 0
    read_animations: int = 0
    unread_animations: int = 0
    selected_read: int = 0
    selected_nonzero: int = 0
    authored_slots: int = 0
    authored_nonzero: int = 0
    reachable_nonzero: int = 0
    refused_counts: int = 0
    extents_past_component: int = 0

    def add(self, channels: list[Channel]) -> None:
        for ch in channels:
            if not ch.count_read:
                self.refused_counts += 1
                continue
            self.authored_animations += ch.count
            self.read_animations += len(ch.read)
            self.unread_animations += ch.unreadable
            self.selected_read += ch.selected_count
            self.selected_nonzero += ch.selected_nonzero
            self.authored_slots += ch.authored
            self.authored_nonzero += ch.authored_nonzero
            self.reachable_nonzero += ch.reachable_nonzero
            self.extents_past_component += sum(1 for r in ch.read if not r.extent_ok)


def report(channels: list[Channel], label: str, totals: Totals, *, verbose: bool) -> None:
    print(f"[census] {label}")
    for ch in channels:
        if not ch.count_read:
            print(f"  channel {ch.channel} {CHANNEL_NAME[ch.channel]}: REFUSED -- {ch.why}")
            continue
        if ch.count == 0:
            print(f"  channel {ch.channel} {CHANNEL_NAME[ch.channel]}: authored count 0; "
                  f"0 entries scanned (an unauthored table is not scanned at all)")
            continue
        hist = {}
        for factor in [f for r in ch.read for f in r.factors]:
            hist[factor] = hist.get(factor, 0) + 1
        top = ", ".join(f"0x{k:02X}:{v}" for k, v in sorted(hist.items())[:8])
        if len(hist) > 8:
            top += f", ... ({len(hist)} distinct values)"
        print(f"  channel {ch.channel} {CHANNEL_NAME[ch.channel]}: component@0x{ch.component:X} "
              f"authored count {ch.count}; read {len(ch.read)}, NOT READ {ch.unreadable}")
        print(f"    SELECTED (0134's metric, one keyframe per animation at the record's own index "
              f"byte): {ch.selected_count} read, {ch.selected_nonzero} at a nonzero factor")
        print(f"    AUTHORED (every 8-byte slot, bounded by the record's own base-offset word): "
              f"{ch.authored} keyframes, {ch.authored_nonzero} at a nonzero factor, "
              f"{ch.reachable_nonzero} at an entry the port's stride check would accept")
        print(f"    factor histogram: {top or '(none read)'}")
        if verbose:
            for r in ch.read:
                print(f"      [{r.index:>3}] file 0x{r.offset:08X} slots={r.slots} size={r.size} "
                      f"selected={r.selected} selected_factor="
                      f"{'NOT READ' if r.selected_factor is None else f'0x{r.selected_factor:02X}'} "
                      f"authored_nonzero={r.authored_nonzero}/{r.slots} "
                      f"flags=0x{r.flags:02X}{' PAUSED(bit1)' if r.paused else ''} "
                      f"{'EXTENT PAST COMPONENT' if not r.extent_ok else ''}")


# ---------------------------------------------------------------------------------------------
# Control: the same census against a live RAM capture, and the 0134 numbers
# ---------------------------------------------------------------------------------------------

# 0134's live census of Artisans, verbatim. This is the number the static parse must reproduce.
ARTISANS_CONTROL_COUNTS = (12, 0, 12, 0)
ARTISANS_CONTROL_SELECTED = 24
ARTISANS_CONTROL_SELECTED_NONZERO = 0


def ram_counts(blob: bytes) -> tuple[int, ...]:
    return tuple(struct.unpack_from("<I", blob, G_ENVIRONMENT_ANIMATIONS + slot - RAM_BASE)[0]
                 for slot in STRUCT_COUNT_SLOT)


def ram_pointers(blob: bytes) -> tuple[int, ...]:
    return tuple(struct.unpack_from("<I", blob, G_ENVIRONMENT_ANIMATIONS + slot - RAM_BASE)[0]
                 for slot in STRUCT_POINTER_SLOT)


def anchor_capture(blob: bytes, wad: bytes) -> int | None:
    """Find the RAM capture's LowPoly record 0 inside the archive, by content.

    The window is the record's 12-byte header AND its keyframe bytes, not the keyframes alone: a
    level's LowPoly and HighPoly copies of one animation share their keyframe table byte for byte
    and differ only in the header's payload-size word, so a keyframes-only window matches twice and
    a census keyed to the wrong copy is a census of nothing. Uniqueness is required, and more than
    one hit is a refusal rather than a choice.
    """
    set_pointer = ram_pointers(blob)[0]
    if not (RAM_BASE <= set_pointer < RAM_BASE + len(blob) - 0x1000):
        return None
    animation = struct.unpack_from("<I", blob, set_pointer - RAM_BASE)[0]
    if not (RAM_BASE <= animation < RAM_BASE + len(blob) - 0x2000):
        return None
    window = blob[animation - RAM_BASE:animation - RAM_BASE + 12 + 512]
    if len(window) < 12 + 512:
        return None
    hits = []
    start = 0
    while True:
        found = wad.find(window, start)
        if found < 0:
            break
        hits.append(found)
        start = found + 1
        if len(hits) > 4:
            return None
    return hits[0] if len(hits) == 1 else None


def control(wad_blob: bytes, entries: list[Entry], ram: bytes) -> bool:
    """Prove the static decode against a live capture, then against 0134's recorded numbers.

    Two independent things have to hold, and either one failing turns this red:
      1. the capture's four authored counts and set pointers must be the ones the static parse of
         the SAME entry reports, at the file offsets the capture implies; and
      2. the selected-keyframe metric must reproduce 0134's 12/0/12/0 and 24-at-factor-0.
    """
    ok = True
    if len(ram) != RAM_SIZE:
        print(f"  control REFUSED: the capture is {len(ram)} bytes, main RAM is {RAM_SIZE}; "
              f"a short dump is not a census")
        return False
    counts = ram_counts(ram)
    pointers = ram_pointers(ram)
    anchor = anchor_capture(ram, wad_blob)
    if anchor is None:
        print("  control REFUSED: the capture's LowPoly record-0 keyframe bytes did not match "
              "exactly one place in the archive, so this census cannot be keyed to a level")
        return False
    entry = next((e for e in entries if e.offset <= anchor < e.end), None)
    if entry is None:
        print("  control REFUSED: the capture's data is not inside any archive entry")
        return False
    print(f"  capture anchors at archive 0x{anchor:X}, inside entry {entry.index} "
          f"(0x{entry.offset:X}..0x{entry.end:X})")
    print(f"  capture's authored counts (ch0/ch1/ch2/ch3): {'/'.join(str(c) for c in counts)}")
    print(f"  capture's set pointers:                      "
          + " ".join(f"0x{p:08X}" for p in pointers))
    channels, _ = census_entry(wad_blob, entry)
    if not channels:
        print("  control FAILED: the static parse found no seven-component run in the entry the "
              "capture anchors in, so it cannot be compared with the capture at all")
        return False
    # Sharper than the totals: the capture's own LowPoly set pointer, carried across by the same
    # constant that carries its record 0, must land on the component the static parse calls
    # LowPoly. This is what tests the ATTRIBUTION rule (member 4 of seven is channel 0) instead of
    # only the counts, which a run found at the wrong place could still match.
    set_ram = pointers[0]
    animation_ram = struct.unpack_from("<I", ram, set_ram - RAM_BASE)[0]
    # The set table sits 8 bytes into its component -- a size word then the count word -- so the
    # component the capture implies is the set table's own address less 8.
    implied_component = anchor + (set_ram - animation_ram) - 8
    got_component = channels[0].component
    if implied_component != got_component:
        print(f"  control FAILED: the capture's LowPoly set maps to archive "
              f"0x{implied_component:X} but the static parse's member 4 -- the component it calls "
              f"LowPoly -- is at 0x{got_component:X}; the channel attribution is wrong")
        ok = False
    else:
        print(f"  control: the capture's LowPoly set maps to archive 0x{got_component:X}, exactly "
              f"the static parse's member 4; the attribution rule holds")
    if anchor not in {r.offset for r in channels[0].read}:
        print(f"  control FAILED: the anchored record at 0x{anchor:X} is not one the static parse "
              f"reaches from the component it calls LowPoly")
        ok = False
    static_counts = tuple(ch.count for ch in channels)
    print(f"  static parse's authored counts:              "
          f"{'/'.join(str(c) for c in static_counts)}")
    if static_counts != counts:
        print(f"  control FAILED: the static parse reports {static_counts} where the live capture "
              f"holds {counts}")
        ok = False
    selected = sum(ch.selected_count for ch in channels)
    nonzero = sum(ch.selected_nonzero for ch in channels)
    print(f"  static parse's SELECTED keyframes: {selected} read, {nonzero} at a nonzero factor "
          f"(0134 recorded {ARTISANS_CONTROL_SELECTED} and "
          f"{ARTISANS_CONTROL_SELECTED_NONZERO})")
    if selected != ARTISANS_CONTROL_SELECTED or nonzero != ARTISANS_CONTROL_SELECTED_NONZERO:
        print("  control FAILED: the selected-keyframe metric does not reproduce 0134's numbers")
        ok = False
    if counts != ARTISANS_CONTROL_COUNTS:
        print(f"  control NOTE: this capture is not the one 0134 censused (counts {counts}, not "
              f"{ARTISANS_CONTROL_COUNTS}); the 12/0/12/0 assertion below is skipped, not passed")
        return ok
    for index, (got, want) in enumerate(zip(static_counts, ARTISANS_CONTROL_COUNTS)):
        if got != want:
            print(f"  control FAILED: channel {index} authored count {got}, expected {want}")
            ok = False
    print(f"  control {'PASSED' if ok else 'FAILED'}: the static decode of entry {entry.index} "
          f"reproduces the live capture's four authored counts and 0134's "
          f"{ARTISANS_CONTROL_SELECTED} selected keyframes at factor 0")
    return ok


# ---------------------------------------------------------------------------------------------
# Selftest
# ---------------------------------------------------------------------------------------------

def build_archive(components: list[tuple[int, list[int]]], *, clamp: bool = False,
                  selected: int = 0, payload_size: int = 16, trailer: bytes = b"") -> bytes:
    """Build an archive whose entry 0 is a run of animation components in the LOADER'S order.

    `components` is (count, factor-per-slot) per component, member 1 first, and the layout is
    produced by the SAME arithmetic `LoadLevelScene` uses -- a size word, a count word, then the
    fields, with a record at `count word + stored` -- so a fixture cannot pass by agreeing with
    itself about a convention the census invented. `clamp` sends component member 4's second
    record outside the entry, which is the NOT READ case. `selected` is written into every record's
    index byte, which is what the SELECTED metric reads. `payload_size` is the size the record
    DECLARES; past 32 the bytes actually appended stop following it, which is how a record comes
    to claim a payload larger than the component holding it.
    """
    payload = bytearray(bytes(0x40))     # a run may not start on the entry's first bytes
    for member, (count, ramp) in enumerate(components):
        size_at = len(payload)
        fields_at = size_at + 8
        payload += struct.pack("<II", 0, count)
        payload += bytes(4 * count)
        for index in range(count):
            if clamp and member == 3 and index == 1:
                payload[fields_at + 4 * index:fields_at + 4 * index + 4] = struct.pack(
                    "<I", 0x7FFFFFFF)
                continue
            record = len(payload)
            slots = len(ramp)
            base = KEYFRAME_TABLE_OFFSET + KEYFRAME_STRIDE * slots
            index_byte = selected if selected < slots else 0
            payload += bytes([index & 0xFF, 0x00, index_byte, 0x01])   # +0 id, +1 flags, +2 index, +3 tick
            payload += struct.pack("<H", 0)                          # +4
            payload += struct.pack("<H", payload_size)              # +6 payload size
            payload += struct.pack("<I", base)                       # +8 base offset
            for slot in range(slots):
                payload += struct.pack("<I", 0) + bytes([ramp[slot], 0, 1, 0])
            payload += bytes(payload_size if payload_size <= 32 else 32) * 2  # A and B
            payload[fields_at + 4 * index:fields_at + 4 * index + 4] = struct.pack(
                "<I", record - (size_at + 4))
        payload[size_at:size_at + 4] = struct.pack("<I", len(payload) - size_at)
    payload += trailer
    blob = bytearray(SECTOR)
    blob[0:8] = struct.pack("<II", SECTOR, len(payload))
    blob += payload
    return bytes(blob)


RAMP = [0, 4, 8, 12, 17, 21, 25, 29, 34, 38, 42, 46]      # the shape Artisans authors
# The loader's order: texture, scrolling, HighPoly(2), LowPoly(0), collision, HighColor(3),
# LowColor(1). `ramp` goes on the four channels; the others are absent.
def run_for(count: int, ramp: list[int]) -> list[tuple[int, list[int]]]:
    """A run with the ramp on all four channels, members 1/2/5 absent."""
    return [(0, []), (0, []), (count, ramp), (count, ramp), (0, []), (count, ramp), (count, ramp)]


def selftest() -> int:
    """Every case states the mistake it forecloses, and each is a fixture the census must READ.

    A case that can pass on a fixture the census refused proves nothing, so each positive asserts
    the read count as well as the answer.
    """
    failures: list[str] = []
    print("selftest: hermetic census of a synthetic archive, in the loader's own component order")

    # 1. POSITIVE, the answer this investigation needs: a nonzero factor must be FOUND and located.
    ramp = list(RAMP)
    blob = build_archive(run_for(3, ramp))
    entries = read_entries(blob)
    channels, _ = census_entry(blob, entries[0])
    if not channels:
        failures.append("positive: the seven-component run was not found; the fixture is not "
                        "exercising the census")
    else:
        counts = tuple(ch.count for ch in channels)
        if counts != (3, 3, 3, 3):
            failures.append(f"positive: authored counts {counts}, expected (3, 3, 3, 3) for a run "
                            f"built in the loader's order with all four channels authored")
        for ch in channels:
            if ch.count and len(ch.read) != ch.count:
                failures.append(f"positive: channel {ch.channel} read {len(ch.read)} of "
                                f"{ch.count}; a zero verdict from an unread fixture is no answer")
        total = sum(ch.authored for ch in channels)
        nonzero = sum(ch.authored_nonzero for ch in channels)
        if total != 4 * 3 * len(ramp):
            failures.append(f"positive: {total} authored keyframes read, expected "
                            f"{4 * 3 * len(ramp)}")
        if nonzero != 4 * 3 * (len(ramp) - 1):
            failures.append(f"positive: {nonzero} nonzero factors found, expected "
                            f"{4 * 3 * (len(ramp) - 1)}; a nonzero factor must be FOUND, not just "
                            f"counted when a caller looks for it")
        selected_nonzero = sum(ch.selected_nonzero for ch in channels)
        if selected_nonzero != 0:
            failures.append("positive: the SELECTED metric reported a nonzero factor; each "
                            "fixture record's index byte is 0 and slot 0's factor is 0")
        print(f"positive: 4 channels x 3 animations x {len(ramp)} authored slots -> {nonzero} "
              f"nonzero factors FOUND and located, {total} slots read, SELECTED 0 nonzero")

    # 2. NEGATIVE, the other answer: a level whose every authored factor is 0 must read as 0.
    blob0 = build_archive(run_for(2, [0] * len(RAMP)))
    channels0, _ = census_entry(blob0, read_entries(blob0)[0])
    if not channels0:
        failures.append("negative: the run was not found")
    else:
        total0 = sum(ch.authored for ch in channels0)
        nonzero0 = sum(ch.authored_nonzero for ch in channels0)
        if total0 != 4 * 2 * len(RAMP) or nonzero0 != 0:
            failures.append(f"negative: {total0} slots read and {nonzero0} nonzero, expected "
                            f"{4 * 2 * len(RAMP)} and 0 -- the instrument has not shown the other "
                            f"answer")
        print(f"negative: the same shape with every factor 0 -> {total0} slots read, 0 nonzero; "
              f"the instrument can answer 0")

    # 3. ZERO COUNT scans NOTHING. The real incident: a fixed 128-index scan read 6 fabricated
    #    entries out of a table Artisans never authored, 3 carrying a nonzero factor byte.
    planted = b"\x13\x00\x00\x00" * 6      # the fabricated nonzero bytes a fixed scan finds
    blobz = build_archive([(0, [])] * RUN_MEMBERS, trailer=planted)
    channels_z, _ = census_entry(blobz, read_entries(blobz)[0])
    if not channels_z:
        failures.append("zero-count: the all-zero run was not found")
    else:
        scanned = sum(len(ch.records) for ch in channels_z)
        read = sum(len(ch.read) for ch in channels_z)
        if scanned != 0 or read != 0:
            failures.append(f"zero-count: {scanned} entries scanned ({read} read) from authored "
                            f"counts that are all 0; the scan is reading memory that is not a table")
        if any(ch.why for ch in channels_z):
            failures.append("zero-count: a zero count produced a refusal reason; it is not a "
                            "refusal, it is an absent table")
        planted_at = blobz.find(planted)
        if any(r.offset and planted_at <= r.offset < planted_at + len(planted)
               for ch in channels_z for r in ch.records):
            failures.append("zero-count: an entry resolved onto the planted nonzero bytes")
        print(f"zero-count: seven authored counts of 0 -> {scanned} entries scanned, 0 nonzero, "
              f"and the 6 nonzero bytes planted right after the run were never read")

    # 4. NOT READ, never factor 0. A short read reported as zeros is how a "no blended form exists"
    #    answer gets manufactured, so the clamp case must be a distinct outcome -- and the census
    #    must additionally REFUSE the whole entry rather than report the three good channels.
    blobc = build_archive(run_for(3, ramp), clamp=True)
    entryc = read_entries(blobc)[0]
    lowpoly = walk_members(blobc, entryc.offset + 0x40)[CHANNEL_RUN_MEMBER[0] - 1]
    clamped_channel = decode_component(blobc, entryc, lowpoly, 3, 0, CHANNEL_RUN_MEMBER[0])
    clamped = [r for r in clamped_channel.records if not r.read]
    if len(clamped_channel.records) != 3:
        failures.append("clamped: the component did not scan its own authored count of 3")
    if len(clamped) != 1:
        failures.append(f"clamped: {len(clamped)} NOT READ, expected exactly 1; the clamp must "
                        f"cost exactly the one record it breaks")
    for r in clamped:
        if r.selected_factor is not None or r.factors:
            failures.append("clamped: an unreadable entry reported a factor; NOT READ is not 0")
        if not r.why:
            failures.append("clamped: an unreadable entry carries no reason")
    read_here = [r for r in clamped_channel.records if r.read]
    for r in read_here:
        if r.why:
            failures.append("clamped: a READ entry carries a refusal reason")
    if sum(r.authored_nonzero for r in read_here) != 2 * (len(ramp) - 1):
        failures.append("clamped: the two readable records did not report their nonzero factors, "
                        "so the refusal is not costing only the broken record")
    channels_c, notes_c = census_entry(blobc, entryc)
    if channels_c:
        failures.append("clamped: the entry was censused despite a broken component; a partial "
                        "run has no channel attribution and must be refused")
    elif not notes_c or "UNKNOWN" not in notes_c[0]:
        failures.append("clamped: the entry was refused without saying its data is UNKNOWN")
    print(f"clamped: 1 record resolving outside the entry reported NOT READ with no factor, the "
          f"2 alongside it reported their nonzero factors, and the end-to-end census REFUSED the "
          f"entry ({len(component_chains(blobc, entryc))} weak chains in the entry)")

    # 5. A count above the guest's index ceiling is REFUSED, not scanned. No sector byte can stamp
    #    an index >= 0x80, so a table that long is not this structure's table.
    blob_r = bytearray(build_archive(run_for(3, ramp)))
    entryr = read_entries(bytes(blob_r))[0]
    member4 = walk_members(bytes(blob_r), entryr.offset + 0x40)[CHANNEL_RUN_MEMBER[0] - 1]
    blob_r[member4 + 4:member4 + 8] = struct.pack("<I", 0x81)
    over = decode_component(bytes(blob_r), entryr, member4, 0x81, 0, CHANNEL_RUN_MEMBER[0])
    if over.count_read or over.records:
        failures.append("ceiling: an authored count of 0x81 was scanned; it exceeds the guest's "
                        "own index ceiling and cannot be indexed by any sector byte")
    elif not over.why:
        failures.append("ceiling: the refusal carries no reason")
    else:
        zero = decode_component(bytes(blob_r), entryr,
                                walk_members(bytes(blob_r), entryr.offset + 0x40)[0], 0, 0, 1)
        if not zero.count_read or zero.records or zero.why:
            failures.append("ceiling: an authored count of 0 must scan nothing AND carry no "
                            "refusal reason; the two are different outcomes")
        print(f"ceiling: an authored count of 0x81 was refused with 0 entries scanned; a count of "
              f"0 was scanned as 0 entries with no refusal -- two different outcomes")

    # 6. THE CONTROL NUMBERS THEMSELVES, hermetically: 12/0/12/0 with 24 selected keyframes at
    #    factor 0, and the authored tables below them. This is 0134's shape, and it pins both
    #    metrics at once so neither can drift alone.
    blob_a = build_archive([(0, []), (0, []), (12, RAMP), (12, RAMP), (0, []), (0, []), (0, [])])
    channels_a, _ = census_entry(blob_a, read_entries(blob_a)[0])
    if not channels_a:
        failures.append("artisans-shape: the run was not found")
    else:
        counts_a = tuple(ch.count for ch in channels_a)
        if counts_a != ARTISANS_CONTROL_COUNTS:
            failures.append(f"artisans-shape: authored counts {counts_a}, expected "
                            f"{ARTISANS_CONTROL_COUNTS}")
        sel = sum(ch.selected_count for ch in channels_a)
        sel_nz = sum(ch.selected_nonzero for ch in channels_a)
        auth_a = sum(ch.authored for ch in channels_a)
        if sel != ARTISANS_CONTROL_SELECTED or sel_nz != ARTISANS_CONTROL_SELECTED_NONZERO:
            failures.append(f"artisans-shape: {sel} selected keyframes ({sel_nz} nonzero), "
                            f"expected {ARTISANS_CONTROL_SELECTED} "
                            f"({ARTISANS_CONTROL_SELECTED_NONZERO})")
        if auth_a != ARTISANS_CONTROL_SELECTED * len(RAMP):
            failures.append(f"artisans-shape: {auth_a} AUTHORED keyframes, expected "
                            f"{ARTISANS_CONTROL_SELECTED * len(RAMP)}; the two metrics are "
                            f"pinned together so a change to the slot stride cannot move one "
                            f"without the other")
        print(f"artisans-shape: counts {'/'.join(str(c) for c in counts_a)}, SELECTED {sel} at "
              f"{sel_nz} nonzero, AUTHORED {auth_a} slots with "
              f"{sum(ch.authored_nonzero for ch in channels_a)} nonzero")

    # 7. A run shorter than seven is a REFUSAL, not a census. Six components cannot be attributed
    #    to channels, and an instrument that quietly censused four of them would be inventing the
    #    attribution it needs.
    blob_s = build_archive(run_for(3, ramp)[:6])
    channels_s, notes_s = census_entry(blob_s, read_entries(blob_s)[0])
    if channels_s:
        failures.append("short-run: a six-component run was censused; the run length is what "
                        "attributes members 3/4/6/7 to the renderer's channels")
    elif not notes_s:
        failures.append("short-run: the six-component run was not even refused with a reason")
    else:
        print(f"short-run: a 6-component run -> {len(channels_s)} channels, refused: "
              f"{notes_s[0][:96]}...")

    # 8. THE SELECTED METRIC READS THE RECORD'S OWN INDEX BYTE. Without this, an instrument that
    #    hard-coded slot 0 would agree with 0134's 24-at-factor-0 forever while measuring nothing,
    #    and the whole point of reporting both metrics is that they disagree on real data.
    slot3 = 3
    blob_i = build_archive(run_for(2, RAMP), selected=slot3)
    channels_i, _ = census_entry(blob_i, read_entries(blob_i)[0])
    if not channels_i:
        failures.append("selected-index: the run was not found")
    else:
        sel = sum(ch.selected_count for ch in channels_i)
        sel_nz = sum(ch.selected_nonzero for ch in channels_i)
        auth = sum(ch.authored for ch in channels_i)
        nz = sum(ch.authored_nonzero for ch in channels_i)
        if sel != 8:
            failures.append(f"selected-index: {sel} selected keyframes read, expected 8")
        if sel_nz != 8:
            failures.append(f"selected-index: {sel_nz} of 8 selected keyframes at a nonzero "
                            f"factor; the record's own index byte is {slot3} and slot {slot3} "
                            f"carries 0x{RAMP[slot3]:02X}, so SELECTED must see it")
        if sel_nz == nz:
            failures.append("selected-index: SELECTED and AUTHORED must be different metrics; "
                            "they agreeing here means one of them is not being measured")
        print(f"selected-index: a record whose own index byte is {slot3} reports SELECTED "
              f"{sel} at {sel_nz} nonzero against AUTHORED {auth} at {nz} nonzero -- the two "
              f"metrics disagree, which is the reason both are reported")

    # 9. A record whose PAYLOAD runs past its own component is refused. The component's size word is
    #    the only thing that says where a level's animation data stops, so a record that reads past
    #    it is reading the next component's bytes -- which is how a fabricated entry gets in.
    blob_x = build_archive(run_for(2, RAMP), payload_size=4096)
    entry_x = read_entries(blob_x)[0]
    lowpoly_x = walk_members(blob_x, entry_x.offset + 0x40)[CHANNEL_RUN_MEMBER[0] - 1]
    over_channel = decode_component(blob_x, entry_x, lowpoly_x, 2, 0, CHANNEL_RUN_MEMBER[0])
    read_x = [r for r in over_channel.records if r.read]
    if not read_x:
        failures.append("past-component: the fixture has no readable record; the case is not "
                        "exercising anything")
    else:
        past = [r for r in read_x if not r.extent_ok]
        if len(past) != 2:
            failures.append(f"past-component: {len(past)} of {len(read_x)} records ran past their "
                            f"component, expected 2; a 4096-byte payload cannot fit")
        locator_survives = bool(find_runs(blob_x, entry_x))
        print(f"past-component: a 4096-byte payload puts {len(past)} of {len(read_x)} records past "
              f"their component, the locator rejects the run "
              f"({'accepted it' if locator_survives else 'as it must'}), and their factors are "
              f"still reported with the extent flagged rather than silently accepted")

    total = 9
    for line in failures:
        print(f"FAIL: {line}")
    print(f"selftest: {total - len(failures)} of {total} cases passed")
    return 1 if failures else 0


# ---------------------------------------------------------------------------------------------


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--selftest", action="store_true",
                       help="hermetic cases, including the nonzero and the zero answer")
    group.add_argument("--wad", type=Path,
                       help="the authenticated WAD.WAD; every entry is censused")
    parser.add_argument("--ram", type=Path,
                        help="a live 2 MB main-RAM capture, for --control")
    parser.add_argument("--control", action="store_true",
                        help="cross-check the static decode against --ram and 0134's numbers")
    parser.add_argument("--only-entry", type=int, default=0,
                        help="census one archive entry by index (0 = all)")
    parser.add_argument("--verbose", action="store_true", help="one line per animation record")
    args = parser.parse_args()

    if args.selftest:
        return selftest()

    path = args.wad if args.wad.is_absolute() else ROOT / args.wad
    if not path.is_file():
        print(f"REFUSED: {path} does not exist. Nothing was read, so nothing is reported; a "
              f"missing corpus is a refusal, not a pass.")
        return 2
    blob = path.read_bytes()
    entries = read_entries(blob)
    print(f"[archive] {path.name}: {len(blob)} bytes, {len(entries)} index entries\n")
    if not entries:
        print("REFUSED: the archive index parsed to zero entries; nothing was censused")
        return 2

    if args.control:
        if args.ram is None:
            print("REFUSED: --control needs --ram, a live 2 MB main-RAM capture")
            return 2
        ram_path = args.ram if args.ram.is_absolute() else ROOT / args.ram
        if not ram_path.is_file():
            print(f"REFUSED: {ram_path} does not exist; the control did not run, which is not a "
                  f"pass")
            return 2
        print("[control] the static decode against a live capture and against 0134's numbers")
        ok = control(blob, entries, ram_path.read_bytes())
        if not ok:
            return 1

    totals = Totals()
    scanned: list[tuple[Entry, list[Channel], list[str]]] = []
    for entry in entries:
        if args.only_entry and entry.index != args.only_entry:
            continue
        channels, notes = census_entry(blob, entry)
        totals.entries += 1
        for note in notes:
            print(f"[note] {note}")
        if channels:
            totals.entries_with_runs += 1
            scanned.append((entry, channels, notes))
        else:
            totals.entries_without_runs += 1
        totals.add(channels)
        if channels:
            report(channels, f"entry {entry.index} (WAD 0x{entry.offset:X}, {entry.length} bytes)",
                   totals, verbose=args.verbose)

    print(f"\n[total] {totals.entries} archive entries scanned; "
          f"{totals.entries_with_runs} carried a seven-component environment-animation run; "
          f"{totals.entries_without_runs} carried none and are counted as UNKNOWN, not as zero")
    print(f"[total] authored animations: {totals.authored_animations} across "
          f"{totals.entries_with_runs} level(s); {totals.read_animations} records read, "
          f"{totals.unread_animations} NOT READ, {totals.refused_counts} channel count(s) refused")
    print(f"[total] SELECTED metric (0134's): {totals.selected_read} keyframes read, "
          f"{totals.selected_nonzero} at a nonzero blend factor")
    print(f"[total] AUTHORED metric (every 8-byte slot in every record): {totals.authored_slots} "
          f"keyframes read, {totals.authored_nonzero} at a nonzero blend factor, "
          f"{totals.reachable_nonzero} of those at an entry the port's stride check accepts")
    if totals.authored_nonzero:
        print("\n[levels with a nonzero authored blend factor]")
        for entry, channels, _ in scanned:
            rows = [ch for ch in channels if ch.authored_nonzero]
            if not rows:
                continue
            print(f"  entry {entry.index} (WAD 0x{entry.offset:X}, {entry.length} bytes):")
            for ch in rows:
                first = next((r for r in ch.read if r.authored_nonzero), None)
                detail = ""
                if first is not None:
                    detail = (f"; first at index {first.index}, file 0x{first.offset:X}, "
                              f"keyframe slot {first.factors.index(next(f for f in first.factors if f))}, "
                              f"factor 0x{next(f for f in first.factors if f):02X}")
                print(f"    channel {ch.channel} {CHANNEL_NAME[ch.channel]}: authored {ch.count}, "
                      f"{ch.authored} slots, {ch.authored_nonzero} nonzero{detail}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
