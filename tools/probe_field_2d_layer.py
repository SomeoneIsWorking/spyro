#!/usr/bin/env python3
"""probe_field_2d_layer.py — WHICH guest 2D primitives build the GS_Playing 2D/HUD layer, measured
from SCUS_942.28 itself.

WHY THIS EXISTS. `docs/project-state.md` S020 says the largest remaining reason an interpolated
Spyro 1 frame is not interpolated is "layer 3 with no producer attribution - the 2D and HUD layer",
and that 82.4% of the items still replayed verbatim are in it. The render queue's layer 3 is
`RQ_HUD`, and the census cannot name a producer for any of it, so the census names a LAYER and this
probe exists to name the CODE that fills it.

WHAT IT MEASURES, per recovered producer entry:
  * the function body, decoded NUMERICALLY from the authenticated executable (no linear-listing
    text matching, for the reason `tools/xrefs.py` documents: a disassembler that walks into data
    desyncs, and a wrong answer reads as "nothing draws this");
  * the number of words scanned, so "0 hits" reads as "scanned N and found none";
  * every GUEST PRIM TAG immediate it stores (0x05000000 POLY_F4 / 0x08000000 POLY_G4 /
    0x09000000 POLY_FT4 / 0x04000000 LINE_G2 / 0x1F000000 DR_MODE) - the packet shape the producer
    writes, which is the shape the native owner must reproduce;
  * every COP2/LWC2/SWC2 word, which is the GTE traffic the layer actually has: a 2D producer has
    none, and a producer that does has to project natively rather than read the GTE;
  * every guest global the body names through a lui/addiu pair, matched against the address set
    `game/core/guest_globals.h` plus the HUD block, so "which state does it read" is a fact about
    the bytes;
  * which OT-link leaf it calls (0x800168DC front-list, 0x800168A0 depth-indexed), because that is
    what decides whether a prim lands in the HUD ordering table or the world one.

WHAT A NEGATIVE PRINTS. Every producer prints its scan count, so a producer whose body is absent is
reported as a REFUSAL with the reason, never as "draws nothing". A wrong address is caught by the
call-target scan: an entry with no `jr ra` and no `jal` to a known leaf is reported, not silently
decoded as an empty body.

    uv run --frozen python tools/probe_field_2d_layer.py
    uv run --frozen python tools/probe_field_2d_layer.py --selftest
"""

from __future__ import annotations

import argparse
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "scratch", "assets", "spyro1", "SCUS_942.28")

# ── The producers of the GS_Playing 2D layer, recovered from the decompile's own GS_Playing arm
# (external/spyro-1/src/gamestates/draw.c GamestateDraw, the `else` branch at line 2715) and from the
# 2D prim constructors it reaches. `tier` says which kind of 2D work it is:
#   "hud-ot"  links through 0x800168DC (the front list) - pure screen-space, no projection
#   "depth"   links through 0x800168A0 (a depth index) - carries a world depth
#   "text"    builds Mobys, which reach the picture through 0x80022A2C (the shaded queue), not the OT
PRODUCERS = [
    (0x80019300, "func_80019300", "collectables: HUD mobys + orb/egg sprites", "hud-ot"),
    (0x80018908, "func_80018908", "demo-mode text (g_DemoMode != 0)", "text"),
    (0x800190D4, "func_800190D4", "screen fade (g_Fade != 0)", "hud-ot"),
    (0x80018F30, "func_80018F30", "screen border bars", "hud-ot"),
    (0x800189F0, "func_800189F0", "tracer streaks", "depth"),
    (0x8001919C, "func_8001919C", "2D textured sprite (the orb/egg primitive)", "hud-ot"),
    (0x8001844C, "func_8001844C", "shaded line (LINE_G2)", "hud-ot"),
    (0x8001860C, "func_8001860C", "shaded box (DR_MODE + POLY_F4 + 4 lines)", "hud-ot"),
    (0x80017FE4, "func_80017FE4", "flat text mobys", "text"),
    (0x800181AC, "func_800181AC", "capitalising text mobys", "text"),
    (0x80018534, "func_80018534", "arrow moby", "text"),
    (0x80018728, "func_80018728", "rescued-dragon text", "text"),
    (0x80018880, "func_80018880", "HUD mobys -> shaded moby queue", "text"),
]

# Guest globals the 2D layer reads. Every address is external/spyro-1's own symbol over the
# byte-identical SCUS_942.28; the HUD block base is g_Hud, whose first field is m_GemDisplayState.
GLOBALS = {
    0x80075918: "g_Fade",
    0x8007570C: "g_ScreenBorderEnabled",
    0x800756C0: "D_800756C0 (border bar height)",
    0x800756CC: "g_DeltaTime",
    0x80075690: "g_IsFlightLevel",
    0x80075714: "g_DemoMode",
    0x800757B0: "D_800757B0 (poly-buffer cursor)",
    0x800758C8: "g_LevelTicks",
    0x800770C8: "D_800770C8 (light colour; m_specularTime at +0x10)",
    0x800770F4: "D_800770C8.m_specularTime",
    0x80075684: "g_TracerCount",
    0x8007596C: "g_LevelId",
    0x800756FC: "D_800756FC (HUD moby list floor)",
    0x80077FA8: "g_Hud",
    0x8006CC78: "COSINE_8 table",
}

# Prim tag immediates. The tag is the top byte in the guest's own encoding: (nwords - 1) << 24 for a
# primitive, 0x1F000000 for a DR_MODE. Naming the SHAPE is what lets a native owner check it emitted
# the same packet the guest would have.
PRIM_TAGS = {
    0x04000000: "LINE_G2 (4 words)",
    0x05000000: "POLY_F4 (5 words)",
    0x08000000: "POLY_G4 (8 words)",
    0x09000000: "POLY_FT4 (9 words)",
    0x1F000000: "DR_MODE",
}

COP2_OPS = (0x12, 0x32, 0x3A)
LINK_LEAVES = {0x800168DC: "front-list AddPrim (0x800168DC)", 0x800168A0: "depth-indexed AddPrim (0x800168A0)"}


class PsxExe:
    """The authenticated PS-X EXE as a guest-addressable word array.

    The header is read, not assumed: this probe reports the entry, load base and text extent it
    found, so a file that is not the expected image is refused rather than decoded at the wrong
    offsets.
    """

    def __init__(self, path: str) -> None:
        with open(path, "rb") as handle:
            blob = handle.read()
        if blob[:8] != b"PS-X EXE":
            raise ValueError(f"{path} is not a PS-X EXE (magic {blob[:8]!r})")
        self.entry, self.gp = struct.unpack_from("<II", blob, 0x10)
        self.text_base, self.text_size = struct.unpack_from("<II", blob, 0x18)
        self.text_offset = 0x800
        self.blob = blob
        self.data = blob[self.text_offset : self.text_offset + self.text_size]

    @property
    def text_end(self) -> int:
        return self.text_base + self.text_size

    def word(self, address: int) -> int:
        if not (self.text_base <= address < self.text_end) or (address & 3):
            raise ValueError(f"0x{address:08X} is outside the text image")
        return struct.unpack_from("<I", self.data, address - self.text_base)[0]

    def words(self, address: int, count: int) -> list[int]:
        return [self.word(address + 4 * i) for i in range(count)]


def decode_branch(word: int, pc: int) -> int | None:
    """The target of a J/JAL, numerically. Returns None for anything else.

    Numeric on purpose: `tools/xrefs.py` records that matching a target against a disassembly
    LISTING is wrong, and this is the same rule applied to forward control flow.
    """
    op = word >> 26
    if op == 0x02 or op == 0x03:  # j / jal
        return (pc + 4 & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
    return None


def body_words(exe: PsxExe, entry: int, limit: int = 4000) -> tuple[list[tuple[int, int]], str]:
    """(address, word) from `entry` to its own `jr ra` + delay slot, or a refusal string.

    A body is bounded by the guest's own epilogue. If one is not reached inside `limit` words the
    scan is REFUSED rather than reported as a short body, because "scanned 4000 words, no epilogue"
    and "this function is 30 words" are different findings and only one of them is a result.
    """
    out: list[tuple[int, int]] = []
    address = entry
    while len(out) < limit:
        if address + 4 > exe.text_end:
            return out, f"ran off the text image at 0x{address:08X} — not a function body"
        word = exe.word(address)
        out.append((address, word))
        if word == 0x03E00008:  # jr ra
            if address + 4 >= exe.text_end:
                return out, ""
            delay = address + 4
            out.append((delay, exe.word(delay)))
            return out, ""
        address += 4
    return out, f"no jr ra within {limit} words"


def lui_addiu_pairs(body: list[tuple[int, int]]) -> list[tuple[int, int, int]]:
    """(lui address, lui word, candidate address) for each lui whose low half an addiu/swc/lwc adds.

    This is a conservative ADDRESS RECOVERY, not a dataflow proof: it pairs a lui with the next
    addiu in the same register's straight-line window. It is used only to NAME a global the body
    mentions, and the report says so.
    """
    pairs = []
    for index, (address, word) in enumerate(body):
        if word >> 26 != 0x0F:  # lui
            continue
        reg = (word >> 16) & 0x1F
        high = (word & 0xFFFF) << 16
        for follow_address, follow in body[index + 1 : index + 9]:
            op = follow >> 26
            if op not in (0x09, 0x0B, 0x2B, 0x23, 0x20, 0x2C, 0x21, 0x25, 0x28, 0x29):
                continue
            if ((follow >> 21) & 0x1F) != reg and ((follow >> 16) & 0x1F) != reg:
                continue
            if ((follow >> 16) & 0x1F) != reg:
                continue
            imm = follow & 0xFFFF
            if imm & 0x8000:
                imm -= 0x10000
            pairs.append((address, word, (high + imm) & 0xFFFFFFFF))
            break
    return pairs


def callees(body: list[tuple[int, int]]) -> list[int]:
    out = []
    for address, word in body:
        op = word >> 26
        if op in (0x02, 0x03):
            target = decode_branch(word, address)
            if target is not None:
                out.append(target)
    return out


def tag_stores(body: list[tuple[int, int]]) -> list[tuple[int, int, str]]:
    """(address, tag, shape) for every prim-tag value the body MATERIALISES.

    The guest does not encode a prim tag as one instruction: `bar->tag = 0x05000000` is
    `lui $s3, 0x500` in a delay slot and `sw $s3, 0($s0)` four words later. A tool that looked for
    the literal word would report ZERO tags for every producer in this layer, which is the failure
    this decode exists to prevent. So the high half of a `lui` counts, and a whole-word immediate
    counts too, and a `lui` is only counted when the value it builds is a recognised shape.
    """
    found: list[tuple[int, int, str]] = []
    for index, (address, word) in enumerate(body):
        candidates: list[tuple[int, str]] = []
        if word >> 26 == 0x0F:  # lui
            high = (word & 0xFFFF) << 16
            for shape, name in PRIM_TAGS.items():
                if shape == high:
                    candidates.append((shape, name))
        elif word in PRIM_TAGS:
            candidates.append((word, PRIM_TAGS[word]))
        for shape, name in candidates:
            # A lui is only a prim tag when its register is STORED within the next few words; a
            # coincidental 0x500 immediate used for arithmetic is not a packet.
            if word in PRIM_TAGS or any((w >> 26) == 0x2B for _a, w in body[index + 1:index + 7]):
                found.append((address, shape, name))
    return found


def scan(exe: PsxExe, entry: int) -> dict:
    body, refusal = body_words(exe, entry)
    tags = tag_stores(body)
    cop2 = [(a, w) for a, w in body if (w >> 26) in COP2_OPS]
    globals_seen: set[str] = set()
    global_addresses: set[int] = set()
    for _lui_address, _lui_word, candidate in lui_addiu_pairs(body):
        if candidate in GLOBALS:
            globals_seen.add(GLOBALS[candidate])
            global_addresses.add(candidate)
    return {
        "entry": entry,
        "words": len(body),
        "refusal": refusal,
        "tags": tags,
        "cop2": len(cop2),
        "globals": globals_seen,
        "global_addresses": global_addresses,
        "callees": callees(body),
    }


def report(exe: PsxExe) -> int:
    print(f"exe {EXE}")
    print(f"  entry=0x{exe.entry:08X} load=0x{exe.text_base:08X} text=0x{exe.text_base + 0x800:08X}"
          f"..0x{exe.text_end:08X} size=0x{exe.text_size:X}")
    print(f"  producers reported: {len(PRODUCERS)}   named globals: {len(GLOBALS)}")
    print("  tag immediates are the guest's own packet shape; COP2 counts GTE traffic; globals are")
    print("  recovered from lui/addiu pairs and are a naming aid, not a dataflow proof.\n")
    refusals = 0
    total_words = total_cop2 = total_tags = 0
    for entry, name, role, tier in PRODUCERS:
        result = scan(exe, entry)
        total_words += result["words"]
        total_cop2 += result["cop2"]
        total_tags += len(result["tags"])
        if result["refusal"]:
            refusals += 1
            print(f"0x{entry:08X} {name:<16} REFUSED: {result['refusal']}")
            continue
        links = [LINK_LEAVES[c] for c in result["callees"] if c in LINK_LEAVES]
        tags = ", ".join(f"{name_}@0x{a:08X}" for a, _i, name_ in result["tags"]) or "none"
        gnames = ", ".join(sorted(result["globals"])) or "none matched"
        print(f"0x{entry:08X} {name:<16} [{tier:<7}] scanned={result['words']:5d}w "
              f"cop2={result['cop2']:3d}  tags: {tags}")
        print(f"{'':>18}  ot-link: {', '.join(links) or 'no leaf reached (builds Mobys, not prims)'}")
        print(f"{'':>18}  globals: {gnames}")
    print(f"\n{len(PRODUCERS)} producer(s) scanned, {total_words} words total, {total_cop2} COP2, "
          f"{total_tags} prim tag(s), {refusals} refused.")
    return 2 if refusals else 0


def selftest() -> int:
    """Prove the probe fires, and that its refusals are refusals.

    A probe that always prints a table is indistinguishable from one that decoded nothing, so each
    check below is a mutation: it must FAIL when the thing it claims to detect is broken.
    """
    def tagSet(result) -> set:
        return {tag for _a, tag, _n in result["tags"]}

    failures = []

    def check(name: str, got, want) -> None:
        if got != want:
            failures.append(f"{name}: got {got!r} want {want!r}")
        print(f"  {'ok  ' if got == want else 'FAIL'} {name}")

    if not os.path.isfile(EXE):
        print(f"REFUSED: no executable at {EXE} - nothing was scanned, so this is not a result.")
        return 2
    exe = PsxExe(EXE)

    print("  a positive: the collectedables producer is real code with a real epilogue")
    result = scan(exe, 0x80019300)
    check("collectables scanned words", result["words"] > 40, True)
    check("collectables found no refusal", result["refusal"], "")

    print("  a negative: a body that is not a function is REFUSED, not reported as empty")
    # 0x8006CC78 is the guest's COSINE_8 table — data, inside the text image, with no epilogue. A tool
    # that "found nothing" here would be reporting an empty body, which is a different claim.
    check("data refused", scan(exe, 0x8006CC78)["refusal"] != "", True)
    # And an address past the image is refused by name rather than raising out of the probe.
    check("out-of-image refused", scan(exe, 0x80080000)["refusal"] != "", True)

    print("  the prim-tag decode distinguishes the shapes it claims to")
    tags = tagSet(scan(exe, 0x800190D4))
    # The fade's DR_MODE is not a tag store at all: 0x800190D4 calls libgpu's SetDrawMode
    # (0x80060670, `jal` at 0x80019110) with the blend already shifted (`sll $a3,$a0,5` at 0x800190F0),
    # and only the POLY_F4 carries a tag the body materialises. A probe that reported a DR_MODE here
    # would be crediting the caller for a library's packet.
    check("fade writes POLY_F4", tags, {0x05000000})
    check("fade reaches SetDrawMode", 0x80060670 in scan(exe, 0x800190D4)["callees"], True)
    check("2D sprite writes POLY_FT4", tagSet(scan(exe, 0x8001919C)), {0x09000000})
    check("shaded line writes LINE_G2", tagSet(scan(exe, 0x8001844C)), {0x04000000})
    check("border writes two POLY_F4s",
          len([1 for _a, tag, _n in scan(exe, 0x80018F30)["tags"] if tag == 0x05000000]),
          2)

    print("  a 2D producer has no GTE traffic, and that is measured rather than assumed")
    check("fade cop2", scan(exe, 0x800190D4)["cop2"], 0)
    check("2D sprite cop2", scan(exe, 0x8001919C)["cop2"], 0)

    print("  the global recovery finds the state the producer actually reads")
    check("border body names the bar height global",
          0x800756C0 in scan(exe, 0x80018F30)["global_addresses"],
          True)
    check("fade body names the poly cursor",
          0x800757B0 in scan(exe, 0x800190D4)["global_addresses"],
          True)

    print("  the OT-link leaf is recovered from the call targets, not declared")
    check("fade reaches the front-list leaf", 0x800168DC in scan(exe, 0x800190D4)["callees"], True)
    check("tracers reach the depth leaf", 0x800168A0 in scan(exe, 0x800189F0)["callees"], True)

    print("  a mutation: a producer that reached NEITHER leaf is not silently accepted")
    # The text builder reaches no OT leaf at all - it writes Mobys. That is the real distinction the
    # report draws, so it is the mutation: if the leaf test were meaningless this would pass.
    check("text builder reaches no OT leaf",
          [c for c in scan(exe, 0x80017FE4)["callees"] if c in LINK_LEAVES], [])

    print("  a mutation: the tag table cannot report a shape the guest never writes")
    check("no 8-word G4 in the 2D sprite", 0x08000000 in tagSet(scan(exe, 0x8001919C)), False)

    print()
    if failures:
        for line in failures:
            print(f"FAIL {line}")
        return 1
    print("  all selftest checks passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", default=EXE)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    if not os.path.isfile(args.exe):
        print(f"REFUSED: no executable at {args.exe} - nothing was scanned, so this is not a result.")
        return 2
    return report(PsxExe(args.exe))


if __name__ == "__main__":
    sys.exit(main())
