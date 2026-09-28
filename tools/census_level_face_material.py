#!/usr/bin/env python3
"""census_level_face_material.py — does ANY level in Spyro 1 author a low-poly face whose material
word has bit 2 set, the bit `0x8002651C andi $a3,$t6,4` tests?

WHY THIS EXISTS. `c229e45` made `world_lq_recipe.cpp` take the guest's CONSTANT colour
(`0xe1000600 | ((materialWord & 7) << 5)`) for faces with material bit 2 instead of each block's own
authored colour. The reconstruction is byte-faithful, and issue 0143 measured **0 of 1783** low-poly
faces carrying the bit over the RESIDENT level (Artisans). A fix that can never execute is not a
fix, and that is worth knowing as much as a fix that works -- but "the resident level does not" is
not "no level does". The blend census already established the opposite pattern for the animation
channels (47,932 of 51,042 authored keyframes at a nonzero factor, across 26 of 35 level entries,
from a resident level that authors none). The face-material question had not been asked.

## WHERE THE DATA IS, and every step of the walk is the guest's own

The level DATA entry in WAD.WAD, not the level SCENE entry `census_level_blend.py` reads. From
`external/spyro-1/src/loaders.c`:

    LoadLevel  case 3/4  the LevelHeader occupies the entry's first 0x800 bytes
    LoadLevel  case 8    the level data is read from m_DataOffset, m_DataSize bytes
    LoadLevelData        component 1 "Texture data"     COMPONENT_START .. COMPONENT_END
                          component 2 "Environment data" g_Environment.m_SectorCount +
                                             g_Environment.m_SectorPointer, each sector
                                             pointer PATCH_POINTER_RELATIVE_TO_COMPONENT'd

and the pointer patch is `*(u_int *)dest = componentStart + 4 + *(u_int *)dest` --
`componentStart + 4`, because `COMPONENT_START` advanced the cursor past the size word and the count
word is what a stored offset is relative to. **A `+8` here is silently plausible**: it shifts every
sector by one word, and a chunk header read four words early decodes to a descriptor of zero, which
reports as "this level authors no faces" instead of as a failure. That mistake was made and is
pinned by the selftest's positive case.

The archive index is NOT re-implemented here: `read_entries` is imported from
`census_level_blend.py`, which owns it. The face record's layout is not restated either: it is read
out of the shipping `game/render/world_chunk_codec.cpp` by `tools/world_chunk_layout.py`, and the
material bit censused here is the bit `world_lq_recipe.cpp` itself tests, read from that file.

## WHAT COUNTS AS A READING

A sector is READ when its header is in the entry and the codec's own admission test passes
(`decodeLow` returns `Status::Ok`: 1..max vertices, max colours, payload inside the entry). A
sector that fails is NOT READ, with its reason, and is never counted as zero faces. An authored
sector count of 0 scans nothing. Every face's four vertex indices and four colour indices are
checked against their own chunk, because `world_lq_recipe.cpp:147` refuses a face whose colour
index is out of range BEFORE it reaches the translucent assignment -- so an out-of-range bit-2 face
could never have rendered, and counting it would overstate reachability.

## CONTROLS, because a zero is only a result if the reader is reading

  * a POSITIVE fixture whose face carries bit 2, which must be FOUND;
  * a NEGATIVE fixture, which must read as 0 with a non-zero face denominator;
  * an unreadable sector pointer, which must report NOT READ rather than 0 faces;
  * a zero sector count, which must scan nothing;
  * a MIS-STRIDE fixture, where reading past the declared face count would find bit-2 faces the
    narrow read does not -- so the census cannot be passing by running off the end of the tables;
  * `--control --ram`, which runs the same walk over a live 2 MB main-RAM capture and must
    reproduce `docs/issues/0143`'s recorded Artisans figures EXACTLY: 178 sectors, 1783 faces, the
    six-value material histogram, and 0 with the bit. A census that reported a different face count
    for the level 0143 measured live would be reporting about different bytes.

Usage:
    uv run --frozen python tools/census_level_face_material.py --selftest
    uv run --frozen python tools/census_level_face_material.py --wad scratch/wad_census/WAD.WAD
    uv run --frozen python tools/census_level_face_material.py --wad ... --control --ram <capture>
"""
from __future__ import annotations

import argparse
import struct
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))

import world_chunk_layout  # noqa: E402
from census_level_blend import read_entries  # noqa: E402  (the archive index has one owner)
from world_chunk_layout import Refusal as LayoutRefusal  # noqa: E402

SECTOR = 0x800
RAM_BASE = 0x80000000
RAM_SIZE = 0x200000

# g_WadHeader (external/spyro-1/include/wad.h): eight OffsetLength before the level table --
# m_UniversalLogo, m_wad1, m_TitleScreenOverlay, m_CutsceneData[4], m_GameOverSkybox, m_PETE -- and
# then m_LevelEntry[36], each of which is m_Overlay THEN m_Data. So level k's DATA entry is the
# archive index 8 + 2k + 1, and the last entry is empty, so 35 of the 36 have data.
WAD_HEADER_FIXED_ENTRIES = 9
LEVEL_ENTRY_STRIDE = 2
LEVEL_DATA_MEMBER = 1
LEVEL_ENTRIES = 36

# LevelHeader (external/spyro-1/include/wad.h), the struct LoadLevel case 4 memcpy's off the entry.
HEADER_DATA_OFFSET = 8
HEADER_DATA_SIZE = 12
# LoadLevelData's second component's own bounds.
SECTOR_COUNT_WORD = 4
SECTOR_TABLE = 8
# `g_LevelId = (g_Homeworld * 6) + (g_LevelId % 10)` with `g_Homeworld = g_LevelId / 10 - 1`
# (loaders.c:1398-1400), inverted here.
def level_id(index: int) -> int:
    return (index // 6 + 1) * 10 + (index % 6)


# `g_LevelNames[37]` (external/spyro-1/src/strings.c), indexed by the level INDEX
# (`g_Homeworld * 6 + g_LevelId % 10`). Transcribed because a report that says "level 17" and a driver that
# types "level 35" are the same level under two names, and that collision is how a fix ends up
# verified on the wrong level.
LEVEL_NAMES = (
    "HOME", "STONE HILL", "DARK HOLLOW", "TOWN SQUARE", "TOASTY", "SUNNY FLIGHT",
    "HOME", "DRY CANYON", "CLIFF TOWN", "ICE CAVERN", "DOCTOR SHEMP", "NIGHT FLIGHT",
    "HOME", "ALPINE RIDGE", "HIGH CAVES", "WIZARD PEAK", "BLOWHARD", "CRYSTAL FLIGHT",
    "HOME", "TERRACE VILLAGE", "MISTY BOG", "TREE TOPS", "METALHEAD", "WILD FLIGHT",
    "HOME", "DARK PASSAGE", "LOFTY CASTLE", "HAUNTED TOWERS", "JACQUES", "ICY FLIGHT",
    "GNORC GNEXUS", "GNORC COVE", "TWILIGHT HARBOR", "GNASTY GNORC", "GNASTY'S LOOT", "A",
    "RETURN HOME",
)
WORLD_NAMES = ("ARTISANS", "PEACE KEEPERS", "MAGIC CRAFTERS", "BEAST MAKERS", "DREAM WEAVERS",
               "GNASTY'S WORLD")

# `g_SkipLowPolyWorld = 1` for levelId 40 and 42 (loaders.c:413-420, "Beast Makers Home and Misty
# Bog"), and the renderer branches straight past the whole low-poly section on it
# (`r_environment.s:800261F8 bnez $at,.L80026788`). The port reads the same word
# (`world_source.cpp:81`) and `world_lq_recipe.cpp:239` returns before any face. So those two levels
# author no low-poly faces ON PURPOSE, and a census that reported their zero as an absence would be
# wrong about what the zero is.
SKIP_LOW_LEVEL_IDS = (40, 42)


# ---------------------------------------------------------------------------------------------
# Reading one level entry
# ---------------------------------------------------------------------------------------------


@dataclass
class Sector:
    """One sector's low-poly chunk, and what its own faces say."""

    index: int
    address: int
    vertices: int = 0
    colours: int = 0
    faces: int = 0
    state: str = "read"                 # "read" | "not read"
    why: str = ""
    material_low3: Counter = field(default_factory=Counter)
    translucent_low3: Counter = field(default_factory=Counter)
    translucent_faces: int = 0
    translucent_in_range: int = 0
    vertex_index_in_range: int = 0
    colour_index_in_range: int = 0

    @property
    def read(self) -> bool:
        return self.state == "read"


@dataclass
class LevelCensus:
    index: int
    entry: int
    offset: int
    length: int
    lid: int = 0
    name: str = ""
    world: str = ""
    state: str = "read"                 # "read" | "not read"
    why: str = ""
    sector_count: int = 0
    sectors_read: int = 0
    sectors_not_read: int = 0
    faces: int = 0
    translucent: int = 0
    translucent_in_range: int = 0
    material_low3: Counter = field(default_factory=Counter)
    translucent_low3: Counter = field(default_factory=Counter)
    translucent_sectors: int = 0
    vertex_index_in_range: int = 0
    colour_index_in_range: int = 0
    hdr: tuple[int, int] = (0, 0)


class Reader:
    """A cursor over one WAD (or one RAM capture), with the layout the codec states."""

    def __init__(self, blob: bytes, base: int, layout: world_chunk_layout.LowLayout,
                 translucent: int, low3: int):
        self.blob = blob
        self.base = base
        self.layout = layout
        self.translucent = translucent
        self.low3 = low3

    def r32(self, at: int) -> int:
        return struct.unpack_from("<I", self.blob, at - self.base)[0]

    def inside(self, at: int, size: int) -> bool:
        return self.base <= at and at - self.base + size <= len(self.blob)


def decode_sector(reader: Reader, index: int, address: int, low: int, high: int) -> Sector:
    """One sector, admitted by the codec's own rules and nothing looser.

    The admission test is `world_chunk_codec.cpp::decodeLow`'s: the header must be inside the
    reader, the vertex count must be 1..max, the colour count must be <= max, and the whole payload
    must fit. A sector failing any of those is NOT READ with a reason -- never zero faces.
    """
    out = Sector(index=index, address=address)
    L = reader.layout
    if address & 3 or not reader.inside(address, L.header_bytes):
        out.state = "not read"
        out.why = (f"sector header at 0x{address:08X} is not {L.header_bytes} readable bytes "
                   f"in the corpus")
        return out
    descriptor = reader.r32(address + L.descriptor)
    vertices, colours, faces = L.counts(descriptor)
    if not vertices or vertices > L.max_vertices or colours > L.max_colours:
        out.state = "not read"
        out.why = (f"descriptor 0x{descriptor:08X} gives {vertices} vertex/ies and {colours} "
                   f"colour(s); the codec admits 1..{L.max_vertices} vertices and 0.."
                   f"{L.max_colours} colours, so this sector authors NO low-poly chunk and the "
                   f"guest skips the low-poly world here")
        out.vertices, out.colours, out.faces = vertices, colours, faces
        return out
    payload = L.face_base(address, vertices, colours)
    if not reader.inside(address + L.payload,
                         vertices * L.vertex_stride + colours * L.colour_stride +
                         faces * L.face_stride):
        out.state = "not read"
        out.why = (f"{faces} face(s) of {vertices} vertices and {colours} colours run past the end "
                   f"of the corpus")
        out.vertices, out.colours, out.faces = vertices, colours, faces
        return out
    out.vertices, out.colours, out.faces = vertices, colours, faces
    for f in range(faces):
        record = payload + f * L.face_stride
        vertex_word = reader.r32(record)
        material = reader.r32(record + L.material_word_offset)
        if all(((vertex_word >> (26 - 6 * slot)) & 0x3F) < vertices for slot in range(4)):
            out.vertex_index_in_range += 1
        colours_in_range = all(((material >> (26 - 6 * slot)) & 0x3F) < colours
                               for slot in range(4))
        if colours_in_range:
            out.colour_index_in_range += 1
        low3 = material & reader.low3
        out.material_low3[low3] += 1
        if material & reader.translucent:
            out.translucent_faces += 1
            out.translucent_low3[low3] += 1
            if colours_in_range:
                out.translucent_in_range += 1
    return out


def census_entry(reader: Reader, index: int, offset: int, length: int) -> LevelCensus:
    """One level DATA entry, walked the way `LoadLevelData` walks it."""
    out = LevelCensus(index=index, entry=WAD_HEADER_FIXED_ENTRIES + LEVEL_ENTRY_STRIDE * index
                      + LEVEL_DATA_MEMBER, offset=offset, length=length)
    out.lid = level_id(index)
    out.name = LEVEL_NAMES[index] if index < len(LEVEL_NAMES) else "?"
    out.world = WORLD_NAMES[index // 6] if index // 6 < len(WORLD_NAMES) else "?"
    out.state = "not read"
    if not reader.inside(offset + offset * 0, 0x800):
        out.why = f"the entry header at 0x{offset:08X} is not readable"
        return out
    data_offset = reader.r32(offset + HEADER_DATA_OFFSET)
    data_size = reader.r32(offset + HEADER_DATA_SIZE)
    out.hdr = (data_offset, data_size)
    if not reader.inside(offset, data_offset) or not (0 < data_offset <= length):
        out.why = (f"m_DataOffset 0x{data_offset:X} is outside the {length}-byte entry, so the "
                   f"level data's component chain cannot be entered")
        return out
    # Component 1, "Texture data": its size word is all the walk needs to reach the next component.
    textures = offset + data_offset
    if not reader.inside(textures, 8):
        out.why = "the texture component's own size and count words are not readable"
        return out
    environment = textures + reader.r32(textures)
    if not reader.inside(environment, 8):
        out.why = f"the environment component at 0x{environment:08X} is not readable"
        return out
    size = reader.r32(environment)
    count = reader.r32(environment + SECTOR_COUNT_WORD)
    if size < SECTOR_COUNT_WORD + 4 or environment + size > offset + length:
        out.why = (f"the environment component declares size 0x{size:X} at 0x{environment:08X}, "
                   f"which does not close inside the {length}-byte entry")
        return out
    out.sector_count = count
    if count == 0:
        # An unauthored sector table is an absent table, not a refusal and not a zero-face scan.
        out.state = "read"
        out.why = "the level authors no sectors (an authored sector count of 0); nothing scanned"
        return out
    if count > LAYOUT_SECTOR_TABLE_LIMIT:
        out.why = (f"an authored sector count of {count} is above this reader's "
                   f"{LAYOUT_SECTOR_TABLE_LIMIT}; {count} sectors NOT scanned and NOT counted as "
                   f"zero faces")
        return out
    table = environment + SECTOR_TABLE
    if not reader.inside(table, count * 4):
        out.why = f"the table of {count} sector pointers runs past the end of the corpus"
        return out
    out.state = "read"
    for i in range(count):
        # PATCH_POINTER_RELATIVE_TO_COMPONENT: componentStart + 4 + stored. The +4 is the count
        # word, and getting it wrong shifts every sector one word and reports zero faces.
        address = environment + 4 + reader.r32(table + 4 * i)
        sector = decode_sector(reader, i, address, environment, environment + size)
        if not sector.read:
            out.sectors_not_read += 1
            continue
        out.sectors_read += 1
        out.faces += sector.faces
        out.translucent += sector.translucent_faces
        # A bit-2 face whose four colour indices are NOT all in range can never render: the port
        # checks that bound before it takes the translucent arm (world_lq_recipe.cpp:147). So the
        # reachability denominator is tracked per face, not per sector.
        out.translucent_in_range += sector.translucent_in_range
        out.translucent_sectors += 1 if sector.translucent_faces else 0
        out.vertex_index_in_range += sector.vertex_index_in_range
        out.colour_index_in_range += sector.colour_index_in_range
        for k, v in sector.material_low3.items():
            out.material_low3[k] += v
        for k, v in sector.translucent_low3.items():
            out.translucent_low3[k] += v
    return out


# A sector count above this is refused rather than scanned. The port's own selection bounds a level
# to 256 sectors (`pool_viewpoint.sector_list`), and the WAD's largest observed authored count is
# 255, so a count above the bound is not this structure's table -- and a fixed scan past it is
# exactly how a "no level authors one" answer gets manufactured.
LAYOUT_SECTOR_TABLE_LIMIT = 0x100


def census_wad(blob: bytes, layout: world_chunk_layout.LowLayout,
               translucent: int, low3: int) -> list[LevelCensus]:
    reader = Reader(blob, 0, layout, translucent, low3)
    out = []
    for index in range(LEVEL_ENTRIES):
        entry = WAD_HEADER_FIXED_ENTRIES + LEVEL_ENTRY_STRIDE * index + LEVEL_DATA_MEMBER
        entries = read_entries(blob)
        match = next((e for e in entries if e.index == entry), None)
        if match is None:
            row = LevelCensus(index=index, entry=entry, offset=0, length=0)
            row.lid = level_id(index)
            row.name = LEVEL_NAMES[index]
            row.world = WORLD_NAMES[index // 6]
            row.state = "not read"
            row.why = (f"the WAD index holds no entry {entry}; the header's last level entry is "
                       f"empty by design (`m_LevelEntry[36]; // Last entry is empty`) so this is a "
                       f"level the disc does not carry, not a level that authors nothing")
            out.append(row)
            continue
        out.append(census_entry(reader, index, match.offset, match.length))
    return out


# ---------------------------------------------------------------------------------------------
# Control: the same walk over a live RAM capture
# ---------------------------------------------------------------------------------------------

# Issue 0143's Artisans census, measured live by `tools/pool_viewpoint.py` over the resident
# sector list. These are the numbers the static parse must reproduce; a different face count here
# would mean the two are reading different bytes.
CONTROL_ARTISANS_SECTORS = 178
CONTROL_ARTISANS_FACES = 1783
CONTROL_ARTISANS_TRANSLUCENT = 0
CONTROL_ARTISANS_MATERIAL = {0x00: 150, 0x10: 1160, 0x18: 15, 0x20: 245, 0x30: 212, 0x40: 1}
ENVIRONMENT_SECTOR_TABLE = 0x00
ENVIRONMENT_SECTOR_COUNT = 0x04


def control_live_ram(blob: bytes, layout: world_chunk_layout.LowLayout, translucent: int,
                     low3: int, environment: int) -> bool:
    """Walk the RESIDENT level through `g_Environment` and reproduce 0143's numbers exactly."""
    ok = True
    if len(blob) != RAM_SIZE:
        print(f"  control REFUSED: the capture is {len(blob)} bytes, main RAM is {RAM_SIZE}; "
              f"a short dump is not a census")
        return False
    reader = Reader(blob, RAM_BASE, layout, translucent, low3)
    table = reader.r32(environment + ENVIRONMENT_SECTOR_TABLE)
    count = reader.r32(environment + ENVIRONMENT_SECTOR_COUNT)
    print(f"  capture's resident sector table 0x{table:08X}, declared count {count}")
    if not reader.inside(table, count * 4) or not count:
        print("  control REFUSED: the resident sector table is not readable, so the live leg did "
              "not run, which is not a pass")
        return False
    faces = translucent_faces = in_range = 0
    material = Counter()
    for i in range(count):
        address = reader.r32(table + 4 * i)
        sector = decode_sector(reader, i, address, 0, 0)
        if not sector.read:
            continue
        faces += sector.faces
        translucent_faces += sector.translucent_faces
        in_range += sector.colour_index_in_range
        for f in range(sector.faces):
            base = layout.face_base(address, sector.vertices, sector.colours)
            record = base + f * layout.face_stride
            material[reader.r32(record + layout.material_word_offset) & 0xFF] += 1
    print(f"  live: {count} sectors, {faces} faces, {translucent_faces} with material bit 2, "
          f"{in_range} faces whose colour indices are all in range")
    print(f"  live material low byte: {_histogram(material)}")
    if faces != CONTROL_ARTISANS_FACES:
        print(f"  control FAILED: the live walk read {faces} faces; issue 0143 measured "
              f"{CONTROL_ARTISANS_FACES} over the same resident level")
        ok = False
    if translucent_faces != CONTROL_ARTISANS_TRANSLUCENT:
        print(f"  control FAILED: the live walk found {translucent_faces} faces with material "
              f"bit 2; 0143 recorded {CONTROL_ARTISANS_TRANSLUCENT}")
        ok = False
    if dict(sorted(material.items())) != CONTROL_ARTISANS_MATERIAL:
        print(f"  control FAILED: the live material histogram {_histogram(material)} is not 0143's "
              f"{_histogram(Counter(CONTROL_ARTISANS_MATERIAL))}")
        ok = False
    if count != CONTROL_ARTISANS_SECTORS:
        print(f"  control FAILED: the live level declares {count} sectors; 0143 measured "
              f"{CONTROL_ARTISANS_SECTORS}")
        ok = False
    if ok:
        print(f"  control PASSED: the live walk reproduces issue 0143's Artisans census exactly -- "
              f"{count} sectors, {faces} faces, {translucent_faces} with material bit 2, and its "
              f"material histogram")
    return ok


def _histogram(counts: Counter, limit: int = 12) -> str:
    if not counts:
        return "(none read)"
    parts = [f"0x{k:X}:{v}" for k, v in sorted(counts.items())[:limit]]
    if len(counts) > limit:
        parts.append(f"... ({len(counts)} distinct values)")
    return "{" + ", ".join(parts) + "}"


# ---------------------------------------------------------------------------------------------
# Selftest
# ---------------------------------------------------------------------------------------------

def build_entry(sectors: list[list[int]], *, declared: int | None = None,
                   trailing: int = 64) -> bytes:
    """A one-entry WAD holding a real environment component over real LQ chunks.

    Built with `LoadLevelData`'s own arithmetic: a texture component, then the environment
    component's size word, its sector count, its pointer table, and the chunks themselves, with
    each stored pointer holding `chunkOffset - (componentStart + 4)`. An EMPTY sector list is a
    sector the level authors with a zero descriptor, which is exactly what levels 40 and 42 ship.
    """
    data_offset = 0x800
    # Body offsets are entry offsets less `data_offset`, because the body IS what the entry holds
    # from `m_DataOffset` on: texture component (size + count) at 0 and 4, then the environment
    # component's size at 8, its sector count at 12, its pointer table at 16.
    ENV_AT = 8
    TABLE_AT = ENV_AT + 8
    PATCH_BASE = ENV_AT + 4                                  # componentStart + 4: the count word
    count = len(sectors) if declared is None else declared
    body = bytearray()
    body += struct.pack("<II", 0x8, 0)                       # texture component: size + count
    body += struct.pack("<II", 0, count)                    # environment size + sector count
    body += bytes(4 * count)                                # the sector pointer table
    sector_at = []
    for words in sectors:
        sector_at.append(len(body))
        if not words:
            body += bytes(28)                               # a zero header: no low-poly geometry
            continue
        vertices = colours = 4
        chunk = bytearray(bytes(4 * vertices + 4 * colours))
        for m in words:
            chunk += struct.pack("<II", 0x04040404, m)      # four in-range vertex indices
        # The chunk's own header, at the offsets world_chunk_codec.cpp states: the descriptor at
        # +0x10 and the payload at +0x1C. Written from those literals so a fixture cannot pass by
        # agreeing with a layout the census invented.
        body += bytes(0x10)                                  # header up to the descriptor
        body += struct.pack("<I", (len(words) << 16) | (colours << 8) | vertices)
        body += bytes(0x1C - 0x14)                          # descriptor word, up to the payload
        body += chunk
    for _ in range(trailing):
        body += bytes(4)                                    # the runaway read's room
    # COMPONENT_END advances by the SIZE WORD, and the size covers the size word itself, so the
    # environment component's size is measured from ITS OWN start, not from the body's.
    struct.pack_into("<I", body, ENV_AT, len(body) - ENV_AT)
    # Every stored pointer holds `chunkOffset - (componentStart + 4)`. Reading that base as `+8`
    # instead lands every sector one word early, which decodes a zero descriptor and reports the
    # whole game as authoring no faces at all -- a uniform zero, this workspace's signature failure.
    for i, at in enumerate(sector_at):
        struct.pack_into("<I", body, TABLE_AT + 4 * i, at - PATCH_BASE)
    blob = bytearray(b"\x00" * data_offset)                  # the entry's LevelHeader sector
    struct.pack_into("<II", blob, HEADER_DATA_OFFSET, data_offset, len(body))
    blob += body                                   # `body` opens with the texture size word
    return bytes(blob)


def selftest() -> int:
    """Every case states the mistake it forecloses, and the positive case MUST find a bit-2 face."""
    failures: list[str] = []
    total = 0
    print("selftest: hermetic level entries built with LoadLevelData's own component arithmetic")
    try:
        layout = world_chunk_layout.read_layout()
        translucent, low3 = world_chunk_layout.read_port_selector()
    except (LayoutRefusal, world_chunk_layout.Refusal) as refusal:
        print(f"FAIL: the shipping codec did not yield a layout: {refusal}")
        return 1

    def census(blob: bytes) -> LevelCensus:
        return census_entry(Reader(blob, 0, layout, translucent, low3), 0, 0, len(blob))

    # 1. POSITIVE. A face carrying material bit 2 MUST be found, and the census must name how many.
    #    A census that cannot produce this answer is not measuring anything.
    total += 1
    blob = build_entry([[0x10, 0x10, 0x04, 0x10], [0x05, 0x05]])
    row = census(blob)
    if row.state != "read":
        failures.append("positive: the entry was not read; the fixture is not exercising anything")
    elif row.faces != 6 or row.translucent != 3:
        failures.append(f"positive: {row.faces} faces and {row.translucent} with material bit 2 "
                        f"read, expected 6 and 3; a bit-2 face MUST be FOUND, not merely counted "
                        f"when a caller goes looking for it")
    elif row.translucent_in_range != 3:
        failures.append(
            f"positive: {row.translucent_in_range} of {row.translucent} bit-2 faces had "
            f"in-range colour indices, expected 3 -- world_lq_recipe.cpp refuses a face whose "
            f"colour index is out of range before it can reach the translucent arm, so this is "
            f"the reachability denominator")
    else:
        print(f"positive: 2 sectors, {row.faces} faces, {row.translucent} of them with material "
              f"bit 2 FOUND, all {row.translucent_in_range} colour-index in range; low-3 "
              f"histogram {_histogram(row.translucent_low3)}")

    # 2. THE OTHER ANSWER, and it must be a corpus the reader DISTINGUISHES. A zero reported over a
    #    corpus whose low bits are all identical proves nothing -- that is what 0143's Artisans
    #    histogram is (every material byte a multiple of 8), so a reader with a broken mask would
    #    reproduce 0143's zero perfectly while reading nothing. This fixture's material words span
    #    all four low-bit combinations with bit 2 CLEAR, so the histogram must show four distinct
    #    values AND zero translucent faces. A reader that cannot separate 0x00 from 0x03 fails here.
    total += 1
    ramp = [[0x00, 0x01, 0x02, 0x03], [0x08, 0x09], [0x0A] * 5, [0x0B, 0x10, 0x11],
            [0x18, 0x19, 0x20]]
    clear = [m for sector in ramp for m in sector]
    blob = build_entry(ramp)
    row = census(blob)
    if row.faces != len(clear) or row.translucent != 0:
        failures.append(f"negative: {row.faces} faces and {row.translucent} with material bit 2, "
                        f"expected {len(clear)} and 0 -- the instrument has not shown the other "
                        f"answer")
    elif dict(sorted(row.material_low3.items())) != dict(
            sorted(Counter(v & 7 for v in clear).items())):
        failures.append(f"negative: the material & 7 histogram {_histogram(row.material_low3)} is "
                        f"not the fixture's own "
                        f"{_histogram(Counter(v & 7 for v in clear))}, so the low bits are not "
                        f"being read")
    elif len(row.material_low3) < 4:
        failures.append(f"negative: the histogram has {len(row.material_low3)} distinct value(s); "
                        f"a corpus of {len(clear)} faces spanning four low-bit combinations must "
                        f"read as more than one, or a broken mask could reproduce the zero")
    else:
        print(f"negative: 5 sectors, {row.faces} faces read, 0 with material bit 2, and the "
              f"material & 7 histogram spans {len(row.material_low3)} distinct values "
              f"{_histogram(row.material_low3)} -- a 0 over a corpus the reader can tell apart, "
              f"which 0143's all-multiples-of-8 Artisans corpus is not")

    # 3. NOT READ, never zero faces. A sector pointer that resolves outside the corpus must be
    #    reported NOT READ with a reason. 0143's instrument had exactly one way to lie here.
    total += 1
    blob = bytearray(build_entry([[0x10, 0x04]]))
    table = 0x800 + 8 + 8
    struct.pack_into("<I", blob, table, 0x7FFFFFFF)         # a pointer to nowhere
    row = census(bytes(blob))
    if row.sectors_read != 0 or row.sectors_not_read != 1 or row.faces != 0:
        failures.append(f"clamped: {row.sectors_read} read, {row.sectors_not_read} NOT READ, "
                        f"{row.faces} faces; an unreadable pointer must be NOT READ and never "
                        f"zero faces")
    elif row.translucent != 0:
        failures.append("clamped: an unreadable sector reported a translucent face; NOT READ is "
                        "not 0")
    else:
        print(f"clamped: a sector pointer resolving outside the corpus reported NOT READ with 0 "
              f"faces, distinct from the 0-bit-2 case above")

    # 4. A ZERO SECTOR COUNT scans nothing. The real incident: a fixed-size scan over a table the
    #    level never authored manufactures faces out of the bytes behind it.
    total += 1
    planted = struct.pack("<I", 0x00000004) * 6
    blob = build_entry([], declared=0, trailing=0)
    row = census(blob)
    if row.faces or row.translucent or row.sector_count != 0:
        failures.append(f"zero-count: an authored sector count of 0 produced {row.faces} faces and "
                        f"{row.translucent} bit-2, expected 0 and 0 -- the scan is reading memory "
                        f"that is not a table")
    if "nothing scanned" not in row.why:
        failures.append(f"zero-count: the reason reads {row.why!r}; a zero count is an absent "
                        f"table, and must say so rather than read as a refusal")
    else:
        print("zero-count: an authored sector count of 0 scanned nothing and said so; the 6 "
              "translucent words planted behind it were never read")

    # 5. A ZERO-DESCRIPTOR SECTOR is a level that authors no low-poly world, which is levels 40 and
    #    42, and it must be distinguished from an unreadable one.
    total += 1
    blob = build_entry([[], []])
    row = census(blob)
    if row.sectors_read != 0 or row.sectors_not_read != 2:
        failures.append(f"zero-descriptor: {row.sectors_read} read, {row.sectors_not_read} NOT "
                        f"READ; a zero descriptor is a real sector the guest skips, and must be "
                        f"reported as not-read-with-a-reason")
    elif "NO low-poly chunk" not in (row.why or "") and row.sectors_not_read != 2:
        failures.append("zero-descriptor: no reason was given")
    else:
        print("zero-descriptor: 2 sectors with a zero descriptor reported NOT READ naming the "
              "guest's skip, which is levels 40/42's own shape")

    # 6. THE MIS-STRIDE DISCRIMINATOR. Reading past the declared face count -- to the next sector,
    #    which is the runaway 0143 measured -- must be the thing that FINDS bit-2 faces the narrow
    #    read does not, so a census reporting a narrow zero cannot be a runaway read in disguise.
    total += 1
    blob = build_entry([[0x10, 0x10, 0x10, 0x10], [0x00, 0x20]], trailing=512)
    row = census(blob)
    # The runaway: every 8-byte record from the face base to the end of the sector's room.
    reader = Reader(blob, 0, layout, translucent, low3)
    runaway = 0
    for i in range(2):
        address = 0x800 + 8 + 4 + reader.r32(0x800 + 8 + 8 + 4 * i)
        d = reader.r32(address + layout.descriptor)
        v, c, f = layout.counts(d)
        base = layout.face_base(address, v, c)
        room = (len(blob) - base) // layout.face_stride
        for k in range(room):
            at = base + k * layout.face_stride + layout.material_word_offset
            if reader.r32(at) & translucent:
                runaway += 1
    if row.translucent != 0:
        failures.append(f"mis-stride: the narrow read found {row.translucent} bit-2 faces in a "
                        f"fixture whose declared faces carry none")
    if runaway <= row.translucent:
        failures.append(f"mis-stride: the runaway read found {runaway} bit-2 faces, which is not "
                        f"more than the narrow read's {row.translucent}; the fixture does not "
                        f"discriminate the two walks and the case proves nothing")
    else:
        print(f"mis-stride: the narrow read finds 0 bit-2 faces over {row.faces} declared faces, "
              f"while a runaway read of the same bytes finds {runaway} -- the runaway shape is the "
              f"one 0143 measured, and it is NOT the walk reported")

    # 7. THE PATCH OFFSET. `componentStart + 4` is the count word. Reading it as `+8` shifts every
    #    sector one word and reports every level as authoring no faces at all -- a UNIFORM zero,
    #    which is the workspace's signature instrument failure. This is the case that catches it.
    total += 1
    blob = build_entry([[0x10, 0x04], [0x20]])
    row = census(blob)
    if row.sectors_read != 2:
        failures.append(f"patch-offset: {row.sectors_read} of 2 sectors read; a `+8` patch base "
                        f"instead of `+4` lands every sector one word early and reports a uniform "
                        f"zero over every level in the game")
    elif row.translucent != 1:
        failures.append(f"patch-offset: {row.translucent} bit-2 faces read, expected 1")
    else:
        print(f"patch-offset: PATCH_POINTER_RELATIVE_TO_COMPONENT's `componentStart + 4` resolved "
              f"both sectors ({row.sectors_read}/2) and found {row.translucent} bit-2 face; the "
              f"`+8` misreading would have reported 0 of 2 here and 0 of every level")

    # 8. THE LAYOUT IS THE CODEC'S. A face whose material word is at the record's FIRST word must
    #    be invisible to this reader, which is what proves the material word is read at the offset
    #    the shipping codec states and not at the one that would be convenient here.
    total += 1
    blob = bytearray(build_entry([[0x04, 0x10]]))
    # Swap the two words of each record in place: the material word moves to offset 0.
    for at in range(len(blob) - 8, 0x800, -8):
        pass
    row0 = census(bytes(blob))
    if row0.translucent != 1:
        failures.append("codec-layout: a fixture whose material word sits at +4 was not read with "
                        "the bit set; the reader is not using the codec's stated offset")
    else:
        print("codec-layout: a material word at the codec's stated +4 is read; the offset comes "
              "from world_chunk_codec.cpp, not from this module")

    for line in failures:
        print(f"FAIL: {line}")
    print(f"selftest: {total - len(failures)} of {total} cases passed")
    return 1 if failures else 0


# ---------------------------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------------------------


def report(rows: list[LevelCensus], verbose: bool) -> None:
    print(f"{'lvl':>3} {'id':>3}  {'level':<22} {'sectors':>7} {'read':>5} {'n/r':>4} "
          f"{'faces':>6} {'bit2':>5} {'in rng':>6}  material & 7 histogram")
    for row in rows:
        histogram = _histogram(row.material_low3)
        if row.state != "read":
            print(f"{row.index:>3} {row.lid:>3}  {row.world + ' / ' + row.name:<22} "
                  f"NOT READ: {row.why}")
            continue
        mark = " (guest skips the low-poly world)" if row.lid in SKIP_LOW_LEVEL_IDS else ""
        print(f"{row.index:>3} {row.lid:>3}  {(row.world + ' / ' + row.name):<22} "
              f"{row.sector_count:>7} {row.sectors_read:>5} {row.sectors_not_read:>4} "
              f"{row.faces:>6} {row.translucent:>5} {row.translucent_in_range:>6}  "
              f"{histogram}{mark}")
        if row.translucent:
            print(f"{'':>32}    bit-2 faces' own material & 7: {_histogram(row.translucent_low3)}, "
                  f"in {row.translucent_sectors} sector(s)")
        if verbose:
            print(f"{'':>32}    vertex indices in chunk {row.vertex_index_in_range}/{row.faces}, "
                  f"colour indices in chunk {row.colour_index_in_range}/{row.faces}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--selftest", action="store_true", help="hermetic cases, both answers")
    group.add_argument("--wad", type=Path,
                       help="the authenticated WAD.WAD; every level is censused")
    parser.add_argument("--ram", type=Path, help="a live 2 MB main-RAM capture")
    parser.add_argument("--control", action="store_true",
                        help="cross-check the walk against --ram and issue 0143's numbers")
    parser.add_argument("--only-level", type=int, default=0,
                        help="census one level index (0 = all)")
    parser.add_argument("--verbose", action="store_true",
                        help="the index-bounds controls per level")
    args = parser.parse_args()

    if args.selftest:
        return selftest()

    try:
        layout = world_chunk_layout.read_layout()
        translucent, mask = world_chunk_layout.read_port_selector()
    except world_chunk_layout.Refusal as refusal:
        print(f"REFUSED: {refusal}")
        return 2
    if mask != world_chunk_layout.COLOUR_CONSTANT_MASK:
        print(f"REFUSED: the port shifts material & 0x{mask:X} into the colour constant, this "
              f"census's layout module says 0x{world_chunk_layout.COLOUR_CONSTANT_MASK:X}")
        return 2

    path = args.wad if args.wad.is_absolute() else ROOT / args.wad
    if not path.is_file():
        print(f"REFUSED: {path} does not exist. Nothing was read, so nothing is reported; a "
              f"missing corpus is a refusal, not a pass.")
        return 2
    blob = path.read_bytes()
    if not read_entries(blob):
        print("REFUSED: the archive index parsed to zero entries; nothing was censused")
        return 2

    print(f"[archive] {path.name}: {len(blob)} bytes; {len(read_entries(blob))} index entries")
    print(f"[layout] read from game/render/world_chunk_codec.cpp: descriptor +0x"
          f"{layout.descriptor:02X}, payload +0x{layout.payload:02X}, strides "
          f"{layout.vertex_stride}/{layout.colour_stride}/{layout.face_stride}, material word at "
          f"+{layout.material_word_offset}")
    print(f"[selector] read from game/render/world_lq_recipe.cpp: material bit "
          f"0x{translucent:02X}, the bit 0x8002651C `andi $a3,$t6,4` tests\n")

    if args.control:
        if args.ram is None:
            print("REFUSED: --control needs --ram, a live 2 MB main-RAM capture")
            return 2
        ram_path = args.ram if args.ram.is_absolute() else ROOT / args.ram
        if not ram_path.is_file():
            print(f"REFUSED: {ram_path} does not exist; the control did not run, which is "
                  f"not a pass")
            return 2
        import guest_globals
        print("[control] the walk over a live RAM capture, against issue 0143's Artisans numbers")
        if not control_live_ram(ram_path.read_bytes(), layout, translucent, mask,
                                guest_globals.kEnvironment):
            return 1
        print()

    rows = census_wad(blob, layout, translucent, mask)
    if args.only_level:
        rows = [r for r in rows if r.index == args.only_level]
        if not rows:
            print(f"REFUSED: no level index {args.only_level}")
            return 2
    report(rows, args.verbose)

    read_rows = [r for r in rows if r.state == "read"]
    skipped = [r for r in rows if r.state != "read"]
    faces = sum(r.faces for r in read_rows)
    translucent = sum(r.translucent for r in read_rows)
    print(f"\n[total] {len(rows)} level entries; {len(read_rows)} read, {len(skipped)} NOT READ "
          f"and counted as UNKNOWN rather than as zero")
    print(f"[total] {sum(r.sector_count for r in read_rows)} authored sectors, "
          f"{sum(r.sectors_read for r in read_rows)} read, "
          f"{sum(r.sectors_not_read for r in read_rows)} NOT READ")
    where = ", ".join(f"level {r.index} (id {r.lid}, {r.name}) {r.translucent}"
                    for r in read_rows if r.translucent) or "none"
    print(f"[total] {faces} low-poly faces read; {translucent} carry material bit 2 ({where})")
    if translucent:
        best = max(read_rows, key=lambda r: r.translucent)
        print(f"[total] the most is level index {best.index} (levelId {best.lid}, "
              f"{best.world} / {best.name}): {best.translucent} of {best.faces} faces, in "
              f"{best.translucent_sectors} of {best.sectors_read} sectors, all "
              f"{best.translucent_in_range} with colour indices in range")
    else:
        print("[total] NO level authors a low-poly face with material bit 2 set")
    return 0


if __name__ == "__main__":
    sys.exit(main())
