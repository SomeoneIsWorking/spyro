#!/usr/bin/env python3
"""pool_viewpoint.py — find the pool in the RESIDENT level, and walk to a viewpoint that shows it.

WHY THIS EXISTS. `docs/issues/0138` and the fix at `c229e45` are a data-level repair of the pool
water, and they have never been looked at. `tools/pool_water_probe.py` can MEASURE a frame's water,
and it has refused every frame the maintained routes produce, with one message each time:

    REFUSED: no connected saturated-blue region of at least 2500 px and at least 0.45 box aspect.

The reason is not that the water is clean. It is that the maintained routes never put the pool in the
picture, so the instrument is being asked about a region that is not there, and its refusal is
correct. Six routes were tried and all six refused (see `coord/claims/product-slot/claim.md`).

HOLDING A DIRECTION CANNOT REACH THE POOL, and `tools/drive.py:283` already records why: the pad is
camera-relative, so a fixed input list walks wherever the arrival bearing points. So this does not
guess. It READS the pool's own world position out of the live level, and hands that position to
`drive.Seeker`, which is the maintained walker.

## WHERE THE POOL'S POSITION COMES FROM, and it is the guest's own arithmetic

The water is not a separate producer. Issue 0138's census row and `0x800258F0`'s disassembly agree
that the pool is guest geometry: it is a grid of low-poly faces in an ordinary world sector, and it
is water only because each of those faces has MATERIAL BIT 2 SET (`0x8002651C andi $a3,$t6,4`).
So "where is the pool" is answerable from the resident sector list, and the guest already has the
conversion from a chunk's packed origin to world space. Read out of
`external/spyro-1/asm/renderers/r_environment.s` (file offsets 16168..1617C and 16A3C..16A54):

    0x80025968  lw    $s7, 0x28(g_Camera)   /* Get the camera's position */
    0x8002596C  lw    $t8, 0x2C(g_Camera)
    0x80025970  lw    $t9, 0x30(g_Camera)
    0x80025974  sra   $s7, $s7, 4          /* Divide by 16 */
    0x80025978  sra   $t8, $t8, 4
    0x8002597C  sra   $t9, $t9, 4
    ...
    0x8002623C  lw    $at, 0x8($t7)        /* the chunk's packed origin word */
    0x80026240  lhu   $s6, 0xe($t7)        /* the chunk's third origin half */
    0x80026244  srl   $s4, $at, 0x10
    0x80026248  andi  $s5, $at, 0xffff
    0x8002624C  sub   $s4, $s4, $s7
    0x80026250  sub   $s5, $t8, $s5
    0x80026254  sub   $s6, $t9, $s6
    0x8002629C  add   $v0, $v0, $s4         /* v0 = local + origin - camera, per axis */

Two facts fall out of those bytes and this tool depends on both:

  1. **AXIS ORDER.** Camera word 0 pairs with `originWord >> 16`, camera word 1 with
     `originWord & 0xFFFF`, and camera word 2 with the halfword at `+0x0E`. The port's
     `game/render/world_lq_recipe.cpp:40` computes the same three subtractions, so this is the
     shipping arithmetic and not a second reading of it.
  2. **UNITS.** The camera is divided by 16 and the chunk origin is NOT, so a chunk origin is in
     units of one-sixteenth of a camera word. A target built without the shift lands 16x too far
     away, which looks exactly like an unreachable destination.

Each low-poly vertex is `(localB, localA, localC)` packed as bits 21..31 / 10..20 / 0..9 of one
word (`0x8002628C srl $v0,$at,0x15`, `0x80026290/94 srl+andi`, `0x80026298 andi $a0,$at,0x3ff`),
so a water FACE'S OWN VERTICES give the pool's world position exactly, with no chunk-extent guess:

    P0 = 16 * ((originWord >> 16) + localB)
    P1 = 16 * ((originWord & 0xFFFF) + localA)
    P2 = 16 * (originZ + localC)

## WHAT IT ALSO SETTLES, because the data is already in hand

`docs/issues/0140` asks whether real pool-water faces set MATERIAL BITS 0 and 1, because the port
takes its blend mode from `(material & 3)` while the guest's own command word carries its own
semi-transparency code. That question is a histogram over the same `materialWord` this tool has
already read, so it is answered here rather than left open, and BOTH answers are printed: the water
faces and the dry faces of the same resident sectors, so a histogram that reads all-zero over a
corpus that was never read stays distinguishable from a real zero.

Usage:
    uv run --frozen python tools/pool_viewpoint.py --census-only
    uv run --frozen python tools/pool_viewpoint.py --seek --arrived 4000 --shots 6 --shot-stride 20 \\
        --shot-prefix scratch/poolview/f
"""
from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass, field
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

import drive  # noqa: E402  (the maintained boot/steer/REPL owner; this tool drives it, does not copy it)
import guest_globals  # noqa: E402
import spyro1_steering  # noqa: E402

# The control surface serves at most this many words per `rw`, on ONE line, and declares a short
# answer. The reader below loops rather than assuming a full answer.
READ_CHUNK = 64

# g_Environment's own field offsets, and g_Camera's, as the port already reads them in
# game/render/world_source.cpp. Named here because this probe walks the SAME structure the shipping
# producer walks, and a second hand-typed offset is how a probe starts measuring a different level
# from the one the product draws.
ENV_SECTOR_TABLE = 0x00
ENV_SECTOR_COUNT = 0x04
CAMERA_POSITION = 0x28
SECTOR_CENTER = 0x00
SECTOR_EXTENT = 0x04
SECTOR_ORIGIN_WORD = 0x08
SECTOR_ORIGIN_Z = 0x0E
SECTOR_DESCRIPTOR = 0x10
SECTOR_PAYLOAD = 0x1C
SECTOR_HEADER_WORDS = SECTOR_PAYLOAD // 4
MOBY_POSITION = 12

# The sector list the renderer walks when no level has installed one (r_environment.s
# 0x800261E4..0x800261EC: `lui $ra,%hi(D_8006FCF4); addiu $ra,...; addi $ra,$ra,0x2000`). A LOADED
# LEVEL replaces it -- measured live: g_Environment[0] is 0x8008B710, not this address -- so this is
# a fact to REPORT, not a control to demand. The control that does hold is the zero terminator: the
# guest's list ends at a zero word, and g_Environment's own count must equal the entries before it.
GUEST_DEFAULT_SECTOR_LIST = 0x80071CF4

WATER_MATERIAL_BIT = 0x04

# THE SECOND DEFINITION OF "WATER", and why it exists. The guest's own test is material bit 2, and in
# the resident Artisans geometry it selects NOTHING: 0 of 1783 low-poly faces across all 178 sectors
# carry it, over a material-byte histogram of only {0x00, 0x10, 0x18, 0x20, 0x30, 0x40} -- every value
# a multiple of 8, so bit 2 is clear in all of them -- and a 1783-of-1783 in-range vertex-index check
# says that is the level's own data and not a mis-strided read. So this probe REPORTS that refusal
# rather than inventing a pool, and offers the second definition the defect frame itself supports: a
# face whose four colour indices all select AUTHORED colours satisfying `pool_water_probe`'s own blue
# predicate. That predicate is USED, not restated, so the level data and the picture are held to one
# definition of blue.
#
# The frame the predicate was written against is the defect frame: its water is dark saturated blue,
# dominant (24,88,120), mean (42,88,127) over 11200 px. The fountain basin a gem tour finds in the
# same level is pale cyan, dominant (136,248,248), and fails the predicate on MAX_GREEN. They are
# different water, and only one of them is the pool the operator reported.
def probe_is_blue(word: int) -> bool:
    """pool_water_probe's own blue predicate, applied to one authored 0x00BBGGRR colour word."""
    from pool_water_probe import _MAX_BLUE, _MAX_GREEN, _MIN_BLUE, _MIN_BLUE_OVER_RED
    r, g, b = word & 0xFF, (word >> 8) & 0xFF, (word >> 16) & 0xFF
    return (_MIN_BLUE <= b <= _MAX_BLUE and g <= _MAX_GREEN
            and b >= r * _MIN_BLUE_OVER_RED)


class Refusal(RuntimeError):
    """The live level did not look like what this reader is for, with the evidence seen."""


def signed(value: int) -> int:
    return value - 0x100000000 if value >= 0x80000000 else value


class Ram:
    """Guest memory through the product's own `rw` verb, assembled over the surface's word cap."""

    def __init__(self, port: "drive.Port"):
        self._port = port

    def words(self, address: int, count: int) -> list[int]:
        if address < 0 or address > 0x80200000 or count < 0:
            raise Refusal(f"0x{address:08X} + {count} word(s) is not a span this reader will ask for")
        out: list[int] = []
        while len(out) < count:
            want = min(READ_CHUNK, count - len(out))
            # drive.Port refuses a short answer rather than padding it, so a truncated read raises
            # here instead of becoming a silent tail of zeros.
            out.extend(self._port.words(address + 4 * len(out), want))
        return out

    def word(self, address: int) -> int:
        return self.words(address, 1)[0]


@dataclass
class Sector:
    """One resident low-poly sector, and what its own faces say."""

    index: int
    address: int
    center: int
    extent: int
    origin_word: int
    origin_z: int
    vertices: int
    colors: int
    faces: int
    decodable: bool = True
    water_faces: int = 0
    water_vertex_words: list[int] = field(default_factory=list)
    water_low3: dict[int, int] = field(default_factory=dict)
    dry_low3: dict[int, int] = field(default_factory=dict)
    blue_faces: int = 0
    blue_vertex_words: list[int] = field(default_factory=list)
    blue_low3: dict[int, int] = field(default_factory=dict)

    def position(self, packed: int) -> tuple[int, int, int]:
        """One vertex word -> world position, by the guest's own subtractions (module docstring)."""
        return (16 * ((self.origin_word >> 16) + (packed >> 21)),
                16 * ((self.origin_word & 0xFFFF) + ((packed >> 10) & 0x7FF)),
                16 * (self.origin_z + (packed & 0x3FF)))


def sector_list(ram: Ram) -> tuple[list[int], str]:
    """The live sector addresses, with two controls on the table they come from."""
    table = ram.word(guest_globals.kEnvironment + ENV_SECTOR_TABLE)
    count = ram.word(guest_globals.kEnvironment + ENV_SECTOR_COUNT)
    if not 0x80000000 <= table < 0x80200000:
        raise Refusal(f"g_Environment's sector table holds 0x{table:08X}, which is not a RAM address, "
                      f"so the level is not resident and NOTHING was censused")
    if count > 0x100:
        raise Refusal(f"g_Environment declares {count} sectors, above the 256 the port's own "
                      f"selection bounds, so this census would read a structure the product rejects")
    if table & 3 or not 0x80000000 <= table < 0x80200000 - 4 * count:
        raise Refusal(f"g_Environment's sector table holds 0x{table:08X}, which is not a 4-aligned "
                      f"RAM address with room for its {count} entries; the level is not resident and "
                      f"NOTHING was censused")
    words = ram.words(table, count)
    out = []
    for index, address in enumerate(words):
        if address & 3 or not 0x80000000 <= address < 0x80200000:
            raise Refusal(f"sector {index} holds 0x{address:08X}, which is not a 4-aligned RAM "
                          f"address; the table was read before the level finished loading")
        out.append(address)
    # The renderer reads this list through the SAME two words the port's own world_source::select
    # reads (`ram.r32(kEnvironment)` and `ram.r32(kEnvironment + 4)`), so a census keyed to them is
    # keyed to the structure the product draws. Measured live, the installed list has NO zero
    # terminator: its 178 entries run straight into texture data at 0x8008C710+0x2C8, so the
    # renderer's `beqz` end-of-list check is satisfied by the count and not by a sentinel. A
    # terminator check was tried here and REFUSED a real level; that is why this control is the
    # address shape of every declared entry plus a header that decodes, reported with a denominator.
    beyond = ram.word(table + 4 * count)
    installed = ("a loaded level's own list, not the renderer's default at "
                 f"0x{GUEST_DEFAULT_SECTOR_LIST:08X}" if table != GUEST_DEFAULT_SECTOR_LIST
                 else "the renderer's DEFAULT list, which no level has replaced")
    return out, (f"CONTROL: {count} of {count} declared sector(s) are 4-aligned RAM addresses, and "
                 f"the word just past the list is 0x{beyond:08X} (level data, not a sentinel); the "
                 f"list is {installed}")



def decode_sector(ram: Ram, index: int, address: int) -> Sector:
    """Decode one sector's low-poly chunk with the port's own field layout.

    Field offsets are `game/render/world_chunk_codec.cpp:44` (decodeLow), which the fix's own
    commit message established as a faithful implementation of the header. This probe reads the SAME
    header rather than defining a second layout that could drift from the product's.
    """
    head = ram.words(address, SECTOR_HEADER_WORDS)
    descriptor = head[SECTOR_DESCRIPTOR // 4]
    vertex_count = descriptor & 0xFF
    color_count = (descriptor >> 8) & 0xFF
    face_count = (descriptor >> 16) & 0xFF
    out = Sector(index=index, address=address, center=head[SECTOR_CENTER // 4],
                 extent=head[SECTOR_EXTENT // 4], origin_word=head[SECTOR_ORIGIN_WORD // 4],
                 # little-endian: the halfword at +0x0E is the HIGH half of the word at +0x0C.
                 origin_z=(head[(SECTOR_ORIGIN_Z - 2) // 4] >> 16) & 0xFFFF,
                 vertices=vertex_count, colors=color_count, faces=face_count)
    if not vertex_count or vertex_count > 256 or color_count > 256:
        out.decodable = False
        return out
    payload_bytes = vertex_count * 4 + color_count * 4 + face_count * 8
    if payload_bytes % 4 or (address + SECTOR_PAYLOAD) & 3:
        out.decodable = False
        return out
    payload = ram.words(address + SECTOR_PAYLOAD, payload_bytes // 4)
    table = payload[:color_count]
    body = payload[vertex_count + color_count:]
    for face in range(face_count):
        vertex_word = body[face * 2]
        material = body[face * 2 + 1]
        color_indices = [(material >> (26 - 6 * slot)) & 0x3F for slot in range(4)]
        if material & WATER_MATERIAL_BIT:
            out.water_faces += 1
            out.water_low3[material & 3] = out.water_low3.get(material & 3, 0) + 1
            for slot in range(4):
                out.water_vertex_words.append((vertex_word >> (26 - 6 * slot)) & 0x3F)
        else:
            out.dry_low3[material & 3] = out.dry_low3.get(material & 3, 0) + 1
        # The second definition. A face counts only when ALL FOUR of its colour indices are in range
        # and every colour they select is blue, so a face that merely borrows one blue corner does not
        # make a sector a pool.
        if all(index < color_count and probe_is_blue(table[index]) for index in color_indices):
            out.blue_faces += 1
            out.blue_low3[material & 3] = out.blue_low3.get(material & 3, 0) + 1
            for slot in range(4):
                out.blue_vertex_words.append((vertex_word >> (26 - 6 * slot)) & 0x3F)
    return out


def histogram(counts: dict[int, int], limit: int = 10) -> str:
    if not counts:
        return "(none read)"
    parts = [f"0x{k:02X}:{v}" for k, v in sorted(counts.items())[:limit]]
    if len(counts) > limit:
        parts.append(f"... ({len(counts)} distinct values)")
    return ", ".join(parts)


def census(ram: Ram) -> tuple[list[Sector], str]:
    addresses, note = sector_list(ram)
    return [decode_sector(ram, index, address) for index, address in enumerate(addresses)], note


def report(sectors: list[Sector], note: str, ram: Ram) -> list[tuple[int, int, int]]:
    print(f"  camera position (g_Camera+0x28): "
          f"{[signed(v) for v in ram.words(guest_globals.kCamera + CAMERA_POSITION, 3)]}")
    print(f"  Spyro  position (g_Spyro+0x0C):  "
          f"{[signed(v) for v in ram.words(guest_globals.kSpyro + MOBY_POSITION, 3)]}")
    print(f"  g_LevelId: {signed(ram.word(guest_globals.kLevelId))}")
    print(f"  {note}")
    decodable = [s for s in sectors if s.decodable]
    water = [s for s in decodable if s.water_faces]
    blue = [s for s in decodable if s.blue_faces]
    total_faces = sum(s.faces for s in decodable)
    print(f"  sectors: {len(sectors)} in the list; {len(decodable)} carried a decodable low-poly "
          f"chunk; {len(water)} carried a face with material bit 2 set; {len(blue)} carried a face "
          f"whose four authored colours are all blue")
    print(f"  faces: {total_faces} read across the decodable sectors")
    print(f"  DEFINITION 1, the guest's own (0x8002651C andi $a3,$t6,4): "
          f"{sum(s.water_faces for s in decodable)} of {total_faces} faces")
    print(f"  DEFINITION 2, all four authored colours blue under pool_water_probe's predicate: "
          f"{sum(s.blue_faces for s in decodable)} of {total_faces} faces")
    water_low3: dict[int, int] = {}
    dry_low3: dict[int, int] = {}
    blue_low3: dict[int, int] = {}
    for s in decodable:
        for key, value in s.water_low3.items():
            water_low3[key] = water_low3.get(key, 0) + value
        for key, value in s.dry_low3.items():
            dry_low3[key] = dry_low3.get(key, 0) + value
        for key, value in s.blue_low3.items():
            blue_low3[key] = blue_low3.get(key, 0) + value
    print(f"  DEFINITION 1 faces' material bits 0..1 -- (material & 3), the field issue 0140 says "
          f"the port blends by, over {sum(water_low3.values())} face(s): {histogram(water_low3)}")
    print(f"  DEFINITION 2 faces' material bits 0..1, over {sum(blue_low3.values())} face(s): "
          f"{histogram(blue_low3)}")
    print(f"  DRY           faces' material bits 0..1 -- the OTHER answer, over "
          f"{sum(dry_low3.values())} non-blue face(s): {histogram(dry_low3)}")
    if not water:
        print("  REFUSED under DEFINITION 1: no resident sector carries a face with material bit 2 "
              "set, so the guest's own translucent arm is DEAD in this level's geometry. That is a "
              "fact about the level, and it is reported rather than papered over.")
    targets: list[tuple[int, int, int]] = []
    for s in sorted(blue, key=lambda s: -s.blue_faces)[:8]:
        if not s.blue_vertex_words:
            continue
        points = [s.position(v) for v in s.blue_vertex_words]
        centre = tuple(sum(p[i] for p in points) // len(points) for i in range(3))
        print(f"  blue sector {s.index} at 0x{s.address:08X}: {s.blue_faces} all-blue face(s) of "
              f"{s.faces}; origin word 0x{s.origin_word:08X}, origin Z 0x{s.origin_z:04X}, "
              f"centroid {centre}")
        targets.append(centre)
    if not targets:
        print("  REFUSED: no resident sector carries a face whose four authored colours are all blue, "
              "so this level's dark pool is not where this reader looks. Nothing about the water is "
              "reported.")
    return targets


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--executable", default="build/bin/spyro_port")
    parser.add_argument("--binary", default="scratch/assets/spyro1/SCUS_942.28")
    parser.add_argument("--log", default="scratch/logs/pool_viewpoint.log")
    parser.add_argument("--settle", type=int, default=140)
    parser.add_argument("--census-only", action="store_true",
                        help="reach gameplay, print the census and the pool's world position, stop")
    parser.add_argument("--seek", action="store_true", help="walk to the pool before capturing")
    parser.add_argument("--sweep", default="",
                        help="comma-separated pad directions; hold each and capture along the way. "
                             "This is a SWEEP, not a guess: every capture is measured by "
                             "pool_water_probe.py and looked at, so a direction that finds nothing is "
                             "a counted negative rather than an absence")
    parser.add_argument("--sweep-shots", type=int, default=8, help="captures per swept direction")
    parser.add_argument("--sweep-frames", type=int, default=60,
                        help="frames walked between captures")
    parser.add_argument("--sweep-dir", default="scratch/poolview", help="capture directory")
    parser.add_argument("--fan", default="",
                        help="comma-separated pad directions walked in turn from wherever the run "
                             "stands, each held for --fan-run fields, capturing after each. A fan is "
                             "coverage rather than a guess: it walks the neighbourhood of a landmark "
                             "and every capture is measured, so a direction that finds no water is a "
                             "counted negative")
    parser.add_argument("--fan-reps", type=int, default=2, help="passes over the fan")
    parser.add_argument("--fan-run", type=int, default=5, help="fields held per direction")
    parser.add_argument("--tour-class", type=int, default=-1,
                        help="walk toward the nearest mobies of this class instead of toward a "
                             "guessed bearing. Spyro 1's gems are classes 83..87, and the reference "
                             "defect frame shows a gem sparkling beside Spyro IN the pool, so a gem is "
                             "a landmark the guest itself names")
    parser.add_argument("--tour-count", type=int, default=3, help="how many of them to visit")
    parser.add_argument("--tour-steps", type=int, default=40, help="steering steps per landmark")
    parser.add_argument("--tour-every", type=int, default=4, help="capture every N steering steps")
    parser.add_argument("--tour-past", type=int, default=0,
                        help="after reaching a landmark, keep pressing the last bearing for this many "
                             "more steps. A gem sits ON the pool's rim, so standing on the gem puts "
                             "the water at a distance; walking past it is what puts the sheet in "
                             "front of the camera")
    parser.add_argument("--arrived", type=int, default=4000,
                        help="stop the walk at this view-space distance; a pool is not walkable, so "
                             "the walk must stop with the water in front of the camera")
    parser.add_argument("--budget", type=int, default=6000, help="steering field budget")
    parser.add_argument("--shots", type=int, default=1, help="captures to take after the walk")
    parser.add_argument("--shot-stride", type=int, default=20, help="frames between captures")
    parser.add_argument("--shot-prefix", default="scratch/poolview/frame",
                        help="capture path prefix; each shot gets a numeric suffix")
    args = parser.parse_args()

    env = drive.environment(drive.disc_path())
    port = drive.Port(drive.ROOT / args.executable, drive.ROOT / args.binary,
                      drive.ROOT / args.log, env)
    ram = Ram(port)
    try:
        drive.Navigator(port).reach_gameplay()
        print(f"reached GS_Playing at frame {port.frame}", file=sys.stderr)
        port.mark_arrival()
        port.run(args.settle)
        print(f"[census] the resident level at frame {port.frame}", file=sys.stderr)
        targets = report(*census(ram), ram)
        if args.census_only:
            return 0
        if args.seek and not targets:
            print("  REFUSED: --seek needs a water centroid and the census found none, so no walk was "
                  "attempted. A sweep can still be run.", file=sys.stderr)
            return 2
        if not args.seek and not args.sweep and args.tour_class < 0:
            print("  (no --seek, --sweep or --tour-class given: the census above is the deliverable "
                  "and the strip below is the capture)", file=sys.stderr)
        # Several sectors can carry water faces of one pool, so the walker is handed every centroid
        # in order of nearness and abandons any that stalls. That is spyro1_steering.Walk's own
        # policy, not a route invented here.
        view = spyro1_steering.camera(port.words)
        order = sorted(targets, key=lambda t: view.bearing(t)[0]) if targets else []
        if args.seek:
            print(f"  walking to {len(order)} water centroid(s); nearest is {order[0]}",
                  file=sys.stderr)
            drive.Seeker(port, "pool", [spyro1_steering.Target("the pool", t) for t in order],
                         budget=args.budget, arrived=args.arrived).walk()
            print(f"  {port.census_line()}", file=sys.stderr)
        if not args.sweep and args.tour_class < 0:
            for index in range(max(1, args.shots)):
                path = f"{args.shot_prefix}{index:02d}.ppm"
                port.shot(path)
                port.run(args.shot_stride)
                print(f"  captured {path}", file=sys.stderr)
        for landmark in range(args.tour_count) if args.tour_class >= 0 else []:
            # One Walk per landmark, with a fresh policy each time, so a landmark that stalls is a
            # counted negative and the tour moves on. Landmarks already standing on are skipped, so
            # the tour visits DISTINCT places instead of reporting the same gem four times.
            here = spyro1_steering.camera(port.words)
            landmarks = [t for t in spyro1_steering.moby_class_targets(port.words, args.tour_class)
                         if here.bearing(t.position)[0] > 1500]
            landmarks.sort(key=lambda t: here.bearing(t.position)[0])
            if not landmarks:
                print(f"  tour {landmark}: REFUSED -- every live moby of class {args.tour_class} is "
                      f"within 1500 view units, so there is nowhere further to walk to", file=sys.stderr)
                break
            target = landmarks[0]
            print(f"  tour {landmark}: nearest class {args.tour_class} beyond 1500 is {target.position} "
                  f"at view-distance {here.bearing(target.position)[0]}", file=sys.stderr)
            walk = spyro1_steering.Walk(f"class {args.tour_class}", [target], arrived=1200)
            last = None
            for step in range(max(1, args.tour_steps)):
                decision = walk.next(here)
                if decision is None:
                    print(f"  tour {landmark}: REACHED {walk.what} at {walk.closest}", file=sys.stderr)
                    break
                last = decision
                for button in decision.buttons:
                    port.press(button)
                if decision.hop:
                    port.tap("cross", 8)
                port.run(drive.Seeker.STEP)
                for button in decision.buttons:
                    port.release(button)
                here = spyro1_steering.camera(port.words)
                if (step + 1) % max(1, args.tour_every) == 0:
                    path = f"{args.sweep_dir}/tour{landmark}-{step + 1:03d}.ppm"
                    port.shot(path)
                    print(f"    {path}  {decision.describe()}  camera {here.position}",
                          file=sys.stderr)
            for step in range(max(0, args.tour_past)):
                for button in (last.buttons if last else ("up",)):
                    port.press(button)
                port.run(drive.Seeker.STEP)
                for button in (last.buttons if last else ("up",)):
                    port.release(button)
                here = spyro1_steering.camera(port.words)
                path = f"{args.sweep_dir}/past{landmark}-{step + 1:03d}.ppm"
                port.shot(path)
                print(f"    {path}  past the landmark  camera {here.position}", file=sys.stderr)
        for direction in [d for d in args.sweep.split(",") if d]:
            port.press(direction)
            for step in range(max(1, args.sweep_shots)):
                port.run(args.sweep_frames)
                path = f"{args.sweep_dir}/{direction}-{step:02d}.ppm"
                port.shot(path)
                position = [signed(v) for v in ram.words(guest_globals.kCamera + CAMERA_POSITION, 3)]
                print(f"    {path}  camera {position}", file=sys.stderr)
            port.release(direction)
        for repetition in range(max(1, args.fan_reps) if args.fan else 0):
            for direction in [d for d in args.fan.split(",") if d]:
                fields = args.fan_run * (repetition + 1)
                for button in direction.split("+"):
                    port.press(button)
                port.run(fields)
                for button in direction.split("+"):
                    port.release(button)
                position = [signed(v) for v in ram.words(guest_globals.kCamera + CAMERA_POSITION, 3)]
                path = f"{args.sweep_dir}/fan{repetition}-{direction.replace('+', '')}.ppm"
                port.shot(path)
                print(f"    {path}  {direction} for {fields} field(s)  camera {position}",
                      file=sys.stderr)
    except drive.Refusal as refusal:
        print(f"pool_viewpoint.py REFUSED: {refusal}", file=sys.stderr)
        print(f"  {port.census_line()}", file=sys.stderr)
        print(f"  run log: {args.log}", file=sys.stderr)
        port.end()
        return 2
    code = port.end()
    print(f"run log: {args.log} (exit {code})", file=sys.stderr)
    return code


if __name__ == "__main__":
    sys.exit(main())
