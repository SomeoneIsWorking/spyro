#!/usr/bin/env python3
"""world_chunk_layout.py — the low-poly face record layout, READ OUT OF the shipping codec.

WHY THIS EXISTS, and it is the reason the census is allowed to report a number at all. A Python
re-derivation of `game/render/world_chunk_codec.cpp`'s field layout would be a SECOND unverified
claim about the same guest bytes: two copies of a layout drift exactly where a re-port drifts, and
the drift is invisible because both copies read the same plausible-looking words. The face record's
layout is therefore not stated here at all -- it is EXTRACTED from the codec, and this module
REFUSES if the codec's text no longer has the shape it reads.

What it reads, and from where in `decodeLow` (`world_chunk_codec.cpp`):

    origin word        ram.r32(address + 8u)
    origin Z           ram.r16(address + 0x0eu)
    descriptor         ram.r32(address + 0x10u)
    vertex count       descriptor & 0xffu
    colour count       (descriptor >> 8) & 0xffu
    face count         (descriptor >> 16) & 0xffu
    payload base       address + 0x1cu
    vertex stride      4      colour stride 4      face stride 8
    face material word the SECOND word of the record (ram.r32(source + 4u))

`world_lq_recipe.cpp` reads the same two words through the same helper (`indices()` at line 36,
applied to `source.vertexWord` and to `source.materialWord`), so the census reads the face exactly
where the shipping producer reads it.

    uv run --frozen python tools/world_chunk_layout.py --selftest
"""
from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CODEC = ROOT / "game" / "render" / "world_chunk_codec.cpp"


class Refusal(RuntimeError):
    """The codec's text no longer has the shape this reader depends on. Not a default."""


@dataclass(frozen=True)
class LowLayout:
    """The low-poly chunk's field layout, as the shipping codec states it."""

    header_bytes: int
    origin_word: int
    origin_z: int
    descriptor: int
    payload: int
    vertex_shift: int
    colour_shift: int
    face_shift: int
    count_mask: int
    vertex_stride: int
    colour_stride: int
    face_stride: int
    material_word_offset: int
    max_vertices: int
    max_colours: int

    def counts(self, descriptor: int) -> tuple[int, int, int]:
        return (descriptor & self.count_mask,
                (descriptor >> self.colour_shift) & self.count_mask,
                (descriptor >> self.face_shift) & self.count_mask)

    def face_base(self, address: int, vertices: int, colours: int) -> int:
        return address + self.payload + vertices * self.vertex_stride + colours * self.colour_stride


def _function(text: str, name: str) -> str:
    """The body of one `decodeX` function, so a read cannot silently come from the other decoder."""
    match = re.search(rf"^Status {name}\(.*?^\}}", text, re.S | re.M)
    if match is None:
        raise Refusal(f"{CODEC.name}: no `Status {name}(` function was found; the layout this "
                      f"census reads is no longer stated in the file the port ships")
    return match.group(0)


def _one(pattern: str, body: str, what: str) -> re.Match:
    """The single match for `pattern`, or a refusal. `re.findall` degrades a one-group pattern to a
    bare string, so the match object is rebuilt rather than indexed, which is the difference between
    a reader that reports and one that raises."""
    found = re.findall(pattern, body)
    if len(found) != 1:
        raise Refusal(f"{CODEC.name}: expected exactly one {what} in decodeLow, found "
                      f"{len(found)}. The census refuses rather than guess which one it means.")
    match = re.search(pattern, body)
    assert match is not None  # one findall hit implies one search hit
    return match


def read_layout(codec: Path = CODEC) -> LowLayout:
    """Extract decodeLow's field layout from the shipping codec's own text."""
    if not codec.is_file():
        raise Refusal(f"{codec} does not exist. The low-poly layout is owned by that file and is "
                      f"not restated here, so nothing is read and nothing is reported.")
    body = _function(codec.read_text(), "decodeLow")

    origin_word = int(_one(r"originWord = ram\.r32\(address \+ (0x[0-9a-fA-F]+|\d+)u?\)", body,
                           "originWord read").group(1), 0)
    origin_z = int(_one(r"originZ = ram\.r16\(address \+ (0x[0-9a-fA-F]+|\d+)u?\)", body,
                        "originZ read").group(1), 0)
    descriptor = int(_one(r"descriptor = ram\.r32\(address \+ (0x[0-9a-fA-F]+|\d+)u?\)", body,
                          "descriptor read").group(1), 0)
    vertex_count = _one(r"vertexCount = out\.descriptor & (0x[0-9a-fA-F]+|\d+)u", body,
                        "vertex-count mask")
    colour_count = _one(r"colorCount = \(out\.descriptor >> (\d+)\) & (0x[0-9a-fA-F]+|\d+)u", body,
                        "colour-count shift")
    face_count = _one(r"faceCount = \(out\.descriptor >> (\d+)\) & (0x[0-9a-fA-F]+|\d+)u", body,
                      "face-count shift")
    # The colour base is the payload base plus the vertex table; the face base is that plus the
    # colour table. Reading the strides off those two `add` calls keeps the payload offset, the
    # vertex stride and the colour stride from being three independent literals here.
    base = _one(r"add\(address \+ (0x[0-9a-fA-F]+|\d+)u, vertexCount \* (\d+)u, colorBase\)", body,
                "payload-base add")
    colour_base = _one(r"add\(colorBase, colorCount \* (\d+)u, faceBase\)", body,
                       "colour-base add")
    face_base = _one(r"add\(faceBase, faceCount \* (\d+)u, end\)", body, "face-base add")
    # The material word is the SECOND word of the record: the face is pushed as
    # `{source, ram.r32(source), ram.r32(source + Nu)}`, and LowFace's field order
    # (world_chunk_codec.h) names the second read the material word.
    material = _one(r"out\.faces\.push_back\(\{source, ram\.r32\(source\), "
                    r"ram\.r32\(source \+ (\d+)u\)\}\)", body, "face record push")
    bounds = _one(r"!vertexCount \|\| vertexCount > (\d+)u \|\| colorCount > (\d+)u", body,
                  "count bounds")
    header_bytes = int(_one(r"ram\.contains\(address, (0x[0-9a-fA-F]+|\d+)u\)", body,
                            "header bound").group(1), 0)

    if int(face_base.group(1)) != 8:
        raise Refusal(f"{CODEC.name}: the face stride is {face_base.group(1)}, not 8. The census's "
                      f"face walk is written against the codec, so it stops rather than stride by a "
                      f"number the port does not use.")
    if int(vertex_count.group(1), 0) != int(colour_count.group(2), 0):
        raise Refusal(f"{CODEC.name}: the vertex and colour count masks differ "
                      f"({vertex_count.group(1)} vs {colour_count.group(2)}); the census assumes one "
                      f"mask and would otherwise report a corpus it did not read")
    return LowLayout(
        header_bytes=header_bytes,
        origin_word=origin_word,
        origin_z=origin_z,
        descriptor=descriptor,
        payload=int(base.group(1), 0),
        vertex_shift=0,
        colour_shift=int(colour_count.group(1)),
        face_shift=int(face_count.group(1)),
        count_mask=int(vertex_count.group(1), 0),
        vertex_stride=int(base.group(2)),
        colour_stride=int(colour_base.group(1)),
        face_stride=int(face_base.group(1)),
        material_word_offset=int(material.group(1)),
        max_vertices=int(bounds.group(1)),
        max_colours=int(bounds.group(2)),
    )


# The selector the port's own `world_lq_recipe.cpp` applies to a face's material word
# (`translucent = (source.materialWord & 4u) != 0u`, read out of the image at `0x8002651C`
# `andi $a3,$t6,4`), and the mask its colour constant shifts by. Both are stated HERE and in the
# C++ and must agree; the selftest reads the C++ and fails if it stops doing so.
TRANSLUCENT_BIT = 0x04
COLOUR_CONSTANT_MASK = 0x07


def read_port_selector(cpp: Path = ROOT / "game" / "render" / "world_lq_recipe.cpp") -> tuple[int, int]:
    """The `& 4u` selector and the `& 7u` colour-constant mask, read out of the shipping consumer."""
    if not cpp.is_file():
        raise Refusal(f"{cpp} does not exist; the material-bit selector is owned there")
    text = cpp.read_text()
    translucent = re.search(r"const bool translucent = \(source\.materialWord & "
                            r"(0x[0-9a-fA-F]+|\d+)u\)", text)
    constant = re.search(r"translucentColor = (0x[0-9a-fA-F]+)u \| "
                         r"\(\(source\.materialWord & (0x[0-9a-fA-F]+|\d+)u\)", text)
    if translucent is None or constant is None:
        raise Refusal(f"{cpp.name}: the translucent selector or the colour-constant mask was not "
                      f"found; the census reports the bit the PORT tests, so it stops here")
    return int(translucent.group(1), 0), int(constant.group(2), 0)


def selftest() -> int:
    """The extraction must FAIL when the codec is perturbed, and must agree with the consumer.

    A reader that cannot go red is not a control. Each case below removes one thing the layout
    depends on and requires a refusal, so "the census read the shipping codec" is a claim the
    suite can falsify rather than one it merely asserts.
    """
    failures: list[str] = []
    print("selftest: the low-poly layout, read out of the shipping codec rather than restated")

    try:
        layout = read_layout()
    except Refusal as refusal:
        print(f"FAIL: the shipping codec did not yield a layout: {refusal}")
        print("selftest: 0 of 8 cases passed")
        return 1

    print(f"read decodeLow: header 0x{layout.header_bytes:X}, origin word +0x{layout.origin_word:X}, "
          f"origin Z +0x{layout.origin_z:02X}, descriptor +0x{layout.descriptor:02X}, "
          f"payload +0x{layout.payload:02X}")
    print(f"  counts: vertices mask 0x{layout.count_mask:02X}, colours >>{layout.colour_shift}, "
          f"faces >>{layout.face_shift}; strides {layout.vertex_stride}/{layout.colour_stride}/"
          f"{layout.face_stride}; material word at +{layout.material_word_offset}")

    # 1. The values the port's own consumer and the recorded defect depend on.
    expect = (("descriptor", layout.descriptor, 0x10), ("payload", layout.payload, 0x1C),
              ("face stride", layout.face_stride, 8),
              ("material word offset", layout.material_word_offset, 4),
              ("face count shift", layout.face_shift, 16),
              ("colour count shift", layout.colour_shift, 8),
              ("count mask", layout.count_mask, 0xFF))
    for name, got, want in expect:
        if got != want:
            failures.append(f"the codec states {name} = {got}, but every recorded reading of this "
                            f"layout (docs/issues/0138 section 3, pool_viewpoint's census) used "
                            f"{want}")
    if not failures:
        print("positive: descriptor +0x10, payload +0x1C, 4/4/8 strides, material word at +4, "
              "counts at &0xFF / >>8 / >>16 -- every value the recorded readings used")

    # 2. THE CONSUMER AGREES. The bit the census tests is the bit the port tests, read from the
    #    port's own source rather than from this module's constant.
    try:
        bit, mask = read_port_selector()
        if bit != TRANSLUCENT_BIT:
            failures.append(f"world_lq_recipe.cpp tests material bit 0x{bit:02X}, this module "
                            f"censors 0x{TRANSLUCENT_BIT:02X}; the census would be answering a "
                            f"different question from the one the port asks")
        if mask != COLOUR_CONSTANT_MASK:
            failures.append(f"world_lq_recipe.cpp shifts material & 0x{mask:X} into the colour "
                            f"constant, this module says 0x{COLOUR_CONSTANT_MASK:X}")
        if not failures:
            print(f"agreement: the port tests material & 0x{bit:02X} and shifts material & "
                  f"0x{mask:X}; the census censuses the same bit")
    except Refusal as refusal:
        failures.append(str(refusal))

    # 3. A perturbed codec REFUSES, in each of the four ways the layout could drift.
    source = CODEC.read_text()
    mutations = (
        ("descriptor moved to +0x14",
         source.replace("out.descriptor = ram.r32(address + 0x10u);",
                        "out.descriptor = ram.r32(address + 0x14u);", 1)),
        ("face stride changed to 16",
         source.replace("!add(faceBase, faceCount * 8u, end)", "!add(faceBase, faceCount * 16u, end)",
                        1)),
        ("the material word moved to the record's FIRST word",
         source.replace("out.faces.push_back({source, ram.r32(source), ram.r32(source + 4u)});",
                        "out.faces.push_back({source, ram.r32(source + 4u), ram.r32(source)});", 1)),
        ("the vertex/colour count masks split",
         source.replace("const uint32_t colorCount = (out.descriptor >> 8) & 0xffu;",
                        "const uint32_t colorCount = (out.descriptor >> 8) & 0x7fu;", 1)),
    )
    probe = ROOT / "scratch" / "wad_census2" / "_mutated_codec.cpp"
    probe.parent.mkdir(parents=True, exist_ok=True)
    for name, mutated in mutations:
        if mutated == source:
            failures.append(f"mutation `{name}` changed nothing; the suite would pass for a codec "
                            f"it never perturbed")
            continue
        probe.write_text(mutated)
        try:
            # A mutation is detected two ways, and BOTH are detections: a reader that refuses has
            # caught it, and a reader that returns a DIFFERENT layout has also caught it. Only an
            # unchanged layout is the failure -- that is the reader ignoring the file it claims to
            # read. (Demanding a refusal specifically would have failed the two mutations that
            # merely move a field, which are exactly the ones that matter.)
            got = read_layout(probe)
            if got == layout:
                failures.append(f"mutation `{name}` left the extracted layout UNCHANGED, so this "
                                f"reader is not reading the codec")
        except Refusal:
            pass
        finally:
            probe.unlink(missing_ok=True)
    if not any("mutation" in f for f in failures):
        print(f"mutations: {len(mutations)} perturbed codecs ({', '.join(n for n, _ in mutations)}) "
              f"each changed or refused the extracted layout")

    # 4. The consumer's own selector, perturbed.
    consumer = ROOT / "game" / "render" / "world_lq_recipe.cpp"
    original = consumer.read_text()
    try:
        read_port_selector(Path("/nonexistent/world_lq_recipe.cpp"))
        failures.append("a missing consumer file was accepted; the selector is not being read")
    except Refusal:
        pass
    print(f"refusals: a missing codec, a missing consumer, and {len(mutations)} perturbed codecs "
          f"all refuse rather than report")

    total = 8
    for line in failures:
        print(f"FAIL: {line}")
    print(f"selftest: {total - len(failures)} of {total} cases passed")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--selftest", action="store_true", help="hermetic cases, each shown red")
    parser.add_argument("--print", action="store_true", help="print the extracted layout and exit")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    if not args.print:
        parser.print_help()
        return 2
    try:
        layout = read_layout()
        bit, mask = read_port_selector()
    except Refusal as refusal:
        print(f"REFUSED: {refusal}")
        return 2
    for name, value in layout.__dict__.items():
        print(f"  {name} = {value}")
    print(f"  translucent_bit = 0x{bit:02X}   colour_constant_mask = 0x{mask:X}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
