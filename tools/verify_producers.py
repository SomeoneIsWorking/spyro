#!/usr/bin/env python3
"""verify_producers.py — compare the producer keys this port SHIPS against the guest image they were
measured from, and against what a run actually reported.

WHY THIS EXISTS, and it is not a hypothetical. A `ProducerScope` key is a MEASURED CONSTANT: it names
the guest function whose drawing a native producer stands in for, and it is what joins the native leg
and the guest leg onto one row of the producer DB. Until this tool existed, that constant lived in
exactly one place — `constexpr uint32_t kGuestSpriteEmitter = 0x8007CD38u` in
game/render/fx_title_menu.cpp — and NOTHING compared it to the binary. The RE that produced it was a
Ghidra pass over a gitignored RAM snapshot, so the evidence was not even in the repo. A hand-edit, a
transposed digit, or an overlay set that moved would have shipped a plausible wrong row, and every
gate this port has would still have been green: the port draws the same picture either way, the census
still reports "1 row, 1380 prims attributed", and the row is simply keyed to the wrong function.

That is the exact defect shape PROTOCOL.md's "THE SHIPPED VALUE MUST BE COMPARED TO THE MEASURED ONE
— BY CODE, NOT BY A HUMAN'S EYES" was written for, found independently in four of five ports in one
round. This tool is fix shape #1 from that rule: THE TOOL READS THE SHIPPING FILE. It parses the
constant out of the .cpp the compiler compiles, re-derives the address from the overlay bytes the
recompiler consumes, and diffs them. One comparison; a hand-edit of either side goes red.

WHAT IS MEASURED, and why it is a derivation rather than a restatement. The recipe for a producer must
not contain the answer. So the emitter's address is NOT looked up and NOT compared word-for-word
against a stored image — it is DERIVED from a structural fingerprint of what the RE says the function
does, with the shipped constant never read until the comparison:

  1. THE FINGERPRINT. The RE (banner of fx_title_menu.cpp, step 1) says the emitter builds ONE
     POLY_FT4, whose tag word is 0x09000000. Materialising that constant costs a `lui $r, 0x0900`.
     Scan every word of the overlay image for that encoding. In OV_5B800 there is EXACTLY ONE such
     site (1 of 3584 words) — so this is a discriminator, not a filter, and the tool asserts the count
     is 1 rather than "at least 1": two sites would mean the fingerprint no longer identifies one
     function and the tool must refuse instead of picking.
  2. THE CONTAINING FUNCTION. Walk BACKWARD from the tag site to the first word that follows a
     `jr $ra` + delay slot. That is the entry of the function the fingerprint is inside, computed from
     the bytes alone.
  3. THE SHAPE CHECK. That entry must be prologue-shaped (`addiu sp, sp, -N`). A mid-function landing
     would not be.
  4. CORROBORATION, printed, not asserted as identity: how many `jal` sites in the image target it
     (56 for the emitter — every sprite in the front end is one call to it).

Only then is the shipping file parsed and the two values compared.

WHAT A NEGATIVE PRINTS, because a diagnostic that can print nothing is lying. Every failure path names
what it scanned and how many candidates it had: "scanned 3584 words of OV_5B800, 0 tag sites" is a
REFUSAL (exit 2), not a pass, and so is a missing overlay slice, a missing shipping file, or a
`ProducerScope` in game/ that this tool has NO recipe for — a new producer cannot slide in unmeasured.

THE RUNTIME HALF (--db). The static check proves the shipped key equals the measured address. It does
NOT prove the scope is reached: deleting the `ProducerScope` line entirely leaves the constant correct
and the static check green. So --db reads a producer-DB JSONL a capped run wrote and requires a row
whose key is the shipped constant with prims_native > 0. Together the two halves are red under either
sabotage — wrong key, or no scope.

Usage:
  verify_producers.py                        # static: shipped constants vs the overlay bytes
  verify_producers.py --db PATH.jsonl        # also require a run to have reported the row
  verify_producers.py --db latest            # newest scratch/producers/run-*.jsonl
  verify_producers.py --selftest             # both classes: real files GREEN, mutants RED
Exit: 0 = agree, 1 = DISAGREE (a shipped value is not the measured one), 2 = REFUSED (measured
nothing, or an unrecognised producer) — never a silent pass.
"""
import argparse
import glob
import json
import os
import re
import shutil
import struct
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OVERLAYS_JSON = os.path.join(REPO, "game", "overlays.json")
SEEDS_JSON = os.path.join(REPO, "game", "recomp_seeds.json")
OVL_DIR = os.path.join(REPO, "scratch", "bin", "overlays")
MAIN_EXE = os.path.join(REPO, "scratch", "bin", "spyro", "SCUS_942.28")

JR_RA = 0x03E00008
OP_JAL = 3
OP_LUI = 0x0F


class Refuse(Exception):
    """The tool measured NOTHING it could compare. Never reported as a pass."""


class Disagree(Exception):
    """A shipped value is not the measured one."""


# ── THE SHIPPED SIDE ────────────────────────────────────────────────────────────────────────────────
# One entry per ProducerScope this port ships. `label` is the scope's own name string, which is what
# appears in the DB row, so the two sides are keyed by the same thing a reader sees in the report.
PRODUCERS = [
    {
        "label": "titlefx:spriteEmit",
        "src": "game/render/fx_title_menu.cpp",
        "const": "kGuestSpriteEmitter",
        "image": "OV_5B800",
        # The RE'd fingerprint: POLY_FT4 tag 0x09000000 -> `lui $r, 0x0900`.
        "fingerprint": ("lui $r,0x0900 (POLY_FT4 tag 0x09000000)", OP_LUI, 0x0900),
    },
    {
        "label": "spriteq:RasterizeSpritePrimQueue.screen",
        "src": "game/render/fx_sprite_queue.cpp",
        "const": "kGuestProducer",
        "image": "MAIN",
        # Hand-written renderer entry: establish 0x80077DD8 as its register-save block, then save
        # s0..s7/gp/sp/fp/ra. The shorter s0/s1/s2 prefix identifies 19 assembly renderers; the full
        # save sequence identifies exactly one. This is structural data, not the expected address.
        "entry_sequence": (
            "0x80077DD8 register-save base + sw s0..s7/gp/sp/fp/ra + next lui ra,0x8007",
            (0x3C018007, 0x24217DD8,
             0xAC300000, 0xAC310004, 0xAC320008, 0xAC33000C,
             0xAC340010, 0xAC350014, 0xAC360018, 0xAC37001C,
             0xAC3C0020, 0xAC3D0024, 0xAC3E0028, 0xAC3F002C,
             0x3C1F8007),
        ),
    },
    {
        "label": "terrain:F3G3",
        "src": "game/core/native_terrain.cpp",
        "const": "kProducerKey",
        "image": "MAIN",
        # The world/cyclorama mesh renderer. The shared 0x80077DD8 register-save prefix is identical
        # across 19 hand-written renderers (0x8004EBA8, 0x8004F4BC, 0x80050240 share it through word
        # +26), so the fingerprint must extend past the divergence: site 0x8004EBA8's word +27 is the
        # `lui at,0x8008` only it has there (the others read a vertex word). 28 words: save + the
        # a1-load/lw walk + the five ctc2 matrix loads + the divergent lui.
        "entry_sequence": (
            "0x80077DD8 register-save + lw t3..t7,0..16(a1) + ctc2 matrix + lui at,0x8008 (28 words)",
            (0x3C018007, 0x24217DD8,
             0xAC300000, 0xAC310004, 0xAC320008, 0xAC33000C,
             0xAC340010, 0xAC350014, 0xAC360018, 0xAC37001C,
             0xAC3C0020, 0xAC3D0024, 0xAC3E0028, 0xAC3F002C,
             0x8CAB0000, 0x8CAC0004, 0x8CAD0008, 0x8CAE000C,
             0x8CAF0010, 0x48CB0000, 0x48CC0800, 0x48CD1000,
             0x48CE1800, 0x48CF2000, 0x48C02800, 0x48C03000,
             0x48C03800, 0x3C018008),
        ),
    },
    {
        "label": "pairedactor:normal",
        "src": "game/render/fx_paired_actor.cpp",
        "const": "kProducerKey",
        "image": "MAIN",
        # Spyro's model renderer (r_pete). The shared 0x80077DD8 register-save prefix is identical
        # across 19 renderers, but the immediate tail differs: word +14 is `lui ra,0x8008` (the other
        # save-idiom renderers load from a1 at that offset). 15 words identify exactly one site.
        "entry_sequence": (
            "0x80077DD8 register-save + lui ra,0x8008 (15 words)",
            (0x3C018007, 0x24217DD8,
             0xAC300000, 0xAC310004, 0xAC320008, 0xAC33000C,
             0xAC340010, 0xAC350014, 0xAC360018, 0xAC37001C,
             0xAC3C0020, 0xAC3D0024, 0xAC3E0028, 0xAC3F002C,
             0x3C1F8008),
        ),
    },
    {
        "label": "actor:opaque",
        "src": "game/render/fx_actor_draw.cpp",
        "const": "kProducerKey",
        "image": "MAIN",
        # The actor list renderer shares the 14-word assembly save prefix with 18 peers. Its
        # immediate continuation loads the actor-list root, establishes gp in two steps, restores
        # the GTE depth offset and reaches the list-empty branch. The similar translucent pass at
        # 0x80020F34 matches through word +26; the branch at +27 distinguishes this owner. The
        # complete 28-word sequence occurs exactly once in the retail executable.
        "entry_sequence": (
            "0x80077DD8 register-save + actor root/gp/GTE depth setup + list-empty branch (28 words)",
            (
                0x3C018007, 0x24217DD8,
                0xAC300000, 0xAC310004, 0xAC320008, 0xAC33000C,
                0xAC340010, 0xAC350014, 0xAC360018, 0xAC37001C,
                0xAC3C0020, 0xAC3D0024, 0xAC3E0028, 0xAC3F002C,
                0x3C018007, 0x242157B0, 0x8C210000,
                0x3C1C8007, 0x279CFCF4, 0x239C1600,
                0x3C028007, 0x2442591C, 0x8C420000,
                0x00200011, 0x48C2E800, 0x8F930000, 0x8F9F0004,
                0x12600429,
            ),
        ),
    },
    {
        "label": "world:static",
        "src": "game/render/fx_world_draw.cpp",
        "const": "kProducerKey",
        "image": "MAIN",
        # RenderWorldChunks shares the same 14-word hand-written renderer save prefix. Its next
        # instruction copies a0 into ra as the renderer's persistent selector; prefix plus that
        # operation identifies exactly one entry in the retail executable.
        "entry_sequence": (
            "0x80077DD8 register-save + persistent world-selector copy (15 words)",
            (
                0x3C018007, 0x24217DD8,
                0xAC300000, 0xAC310004, 0xAC320008, 0xAC33000C,
                0xAC340010, 0xAC350014, 0xAC360018, 0xAC37001C,
                0xAC3C0020, 0xAC3D0024, 0xAC3E0028, 0xAC3F002C,
                0x209F0000,
            ),
        ),
    },
    {
        "label": "screen:fade",
        "src": "game/render/fx_screen_fade.cpp",
        "const": "kProducerKey",
        "image": "MAIN",
        # The cutscene fade entry starts with its 0x800190D4 stack frame and g_Fade colour setup.
        # This exact entry sequence occurs once in the retail MAIN image.
        "entry_sequence": (
            "0x800190D4 fade prologue + g_Fade colour setup (15 words)",
            (
                0x27BDFFD0, 0xAFB1001C, 0x00A08821, 0xAFB20020, 0x00C09021,
                0xAFB30024, 0x00E09821, 0x00043940, 0x24050001, 0xAFB00018,
                0x3C108007, 0x8E1057B0, 0x00003021, 0xAFBF0028, 0xAFA00010,
            ),
        ),
    },
    {
        "label": "fieldhud:collectables",
        "src": "game/render/fx_field_collectables.cpp",
        "const": "kProducerKey",
        "image": "MAIN",
        # The FIELD collectables entry starts with the flight-level guard, shaded-moby queue walk,
        # and HUD root setup. This exact entry sequence occurs once in the retail MAIN image.
        "entry_sequence": (
            "0x80019300 collectables flight guard + queue/HUD setup (16 words)",
            (
                0x3C028007, 0x8C425690, 0x27BDFFA8, 0xAFBF0050, 0xAFB3004C,
                0xAFB20048, 0xAFB10044, 0x14400082, 0xAFB00040, 0x3C118007,
                0x263120F4, 0x8E220000, 0x00000000, 0x10400007, 0x00000000,
                0x26310004,
            ),
        ),
    },
    {
        "label": "screen:border",
        "src": "game/render/fx_screen_border.cpp",
        "const": "kProducerKey",
        "image": "MAIN",
        # The field border entry starts with the enabled/height/delta-time reads and its two
        # stack-frame branches. This exact entry sequence occurs once in the retail MAIN image.
        "entry_sequence": (
            "0x80018F30 border enabled/height/delta-time setup (18 words)",
            (
                0x3C028007, 0x8C42570C, 0x27BDFFD8, 0xAFBF0020, 0xAFB3001C,
                0xAFB20018, 0xAFB10014, 0x10400018, 0xAFB00010, 0x3C038007,
                0x8C6356C0, 0x00000000, 0x28620016, 0x10400007, 0x00000000,
                0x3C028007, 0x8C4256CC, 0x00000000,
            ),
        ),
    },
    {
        "label": "spriteq:world-shaded",
        "src": "game/render/fx_field_shaded_queue.cpp",
        "const": "kProducerKey",
        "image": "MAIN",
        # The shaded queue's retained entry uses the common renderer save block followed by the
        # queue's local stack setup. This exact entry sequence occurs once in the retail MAIN image.
        "entry_sequence": (
            "0x80022A2C shaded queue renderer save + stack setup (18 words)",
            (
                0x3C018007, 0x24217DD8, 0xAC300000, 0xAC310004, 0xAC320008,
                0xAC33000C, 0xAC340010, 0xAC350014, 0xAC360018, 0xAC37001C,
                0xAC3C0020, 0xAC3D0024, 0xAC3E0028, 0xAC3F002C, 0x3C1F8007,
                0x27FFFCF4, 0x23FF2400, 0x3C1D8007,
            ),
        ),
    },
    {
        "label": "actor:secondary",
        "src": "game/render/fx_secondary_actor.cpp",
        "const": "kProducerKey",
        "image": "MAIN",
        # The secondary actor renderer shares the regular actor prefix, then takes its distinct
        # list-empty branch. The full 28-word entry sequence occurs once in the retail image.
        "entry_sequence": (
            "0x80020F34 secondary actor save/list-empty prefix (28 words)",
            (
                0x3C018007, 0x24217DD8, 0xAC300000, 0xAC310004, 0xAC320008,
                0xAC33000C, 0xAC340010, 0xAC350014, 0xAC360018, 0xAC37001C,
                0xAC3C0020, 0xAC3D0024, 0xAC3E0028, 0xAC3F002C, 0x3C018007,
                0x242157B0,
                0x8C210000, 0x3C1C8007, 0x279CFCF4, 0x239C1600, 0x3C028007,
                0x2442591C, 0x8C420000, 0x00200011, 0x48C2E800, 0x8F930000,
                0x8F9F0004, 0x1260068E, 0x3274FF00,
            ),
        ),
    },
    {
        "label": "field:environment",
        "src": "game/render/fx_field_environment.cpp",
        "const": "kProducerKey",
        "image": "MAIN",
        # The FIELD environment path enters the shared world renderer with a camera-relative
        # selector load. This exact entry sequence occurs once in the retail MAIN image.
        "entry_sequence": (
            "0x800258F0 world renderer + camera-relative selector load (18 words)",
            (
                0x3C018007, 0x24217DD8, 0xAC300000, 0xAC310004, 0xAC320008,
                0xAC33000C, 0xAC340010, 0xAC350014, 0xAC360018, 0xAC37001C,
                0xAC3C0020, 0xAC3D0024, 0xAC3E0028, 0xAC3F002C, 0x209F0000,
                0x3C018007, 0x24216DD0, 0x8C2A0014,
            ),
        ),
    },
]


def read_shipped(prod, src_override=None):
    """Parse the constant out of the file the COMPILER compiles. Also confirms the constant is the one
    actually handed to a ProducerScope in that file — a constant nothing passes to a scope would be a
    value this tool verifies and the port does not use."""
    path = src_override or os.path.join(REPO, prod["src"])
    if not os.path.isfile(path):
        raise Refuse(f"{prod['label']}: shipping file {path} does not exist — verified NOTHING")
    text = open(path, encoding="utf-8").read()
    m = re.search(r"constexpr\s+uint32_t\s+" + re.escape(prod["const"]) + r"\s*=\s*(0x[0-9A-Fa-f]+)",
                  text)
    if not m:
        raise Refuse(f"{prod['label']}: no `constexpr uint32_t {prod['const']} = 0x…` in "
                     f"{os.path.relpath(path, REPO)} ({len(text.splitlines())} lines scanned) — the "
                     f"shipped value could not be read, so nothing was compared")
    # STRIP COMMENTS FIRST. Measured while sabotage-testing this tool: with the scope line COMMENTED OUT
    # the raw text still matched, and the static half reported OK for a producer that no longer declares
    # itself. This file's banner is 100 lines of prose naming the scope, so scanning raw text is scanning
    # documentation. Comments are stripped in the same order a compiler sees them (block, then line).
    code = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    code = re.sub(r"//[^\n]*", "", code)
    scopes = re.findall(r"ProducerScope\s+\w+\s*\(([^;]*?)\)\s*;", code, re.S)
    used = [s for s in scopes if prod["const"] in s]
    if not used:
        raise Refuse(f"{prod['label']}: {prod['const']} is defined but handed to no ProducerScope in "
                     f"{os.path.relpath(path, REPO)} ({len(scopes)} scope construction(s) found) — "
                     f"this tool would be verifying a constant the port does not use")
    if not any(re.search(r'"' + re.escape(prod["label"]) + r'"', s) for s in used):
        raise Refuse(f"{prod['label']}: the ProducerScope taking {prod['const']} does not carry that "
                     f"label — the DB row and this recipe would be keyed by different names")
    return int(m.group(1), 16), len(used)


def scan_shipped_scopes():
    """Every ProducerScope in game/, so a NEW producer with no recipe here is a refusal rather than an
    unmeasured value nothing notices. This is the check that keeps the tool honest as the port grows."""
    found = []
    for root, _dirs, files in os.walk(os.path.join(REPO, "game")):
        for fn in files:
            if not fn.endswith((".cpp", ".h")):
                continue
            p = os.path.join(root, fn)
            text = open(p, encoding="utf-8", errors="replace").read()
            code = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
            code = re.sub(r"//[^\n]*", "", code)
            # Parse through the closing `);`, not one physical line: the second producer exposed that
            # line-oriented scanning found the construction but lost its label on the next line.
            for m in re.finditer(r"\bProducerScope\s+\w+\s*\(([^;]*?)\)\s*;", code, re.S):
                lab = re.search(r'"([^"]*)"', m.group(1))
                line = code.count("\n", 0, m.start()) + 1
                found.append((os.path.relpath(p, REPO), line, lab.group(1) if lab else None))
    return found


# ── THE MEASURED SIDE ───────────────────────────────────────────────────────────────────────────────
def overlay_base(name):
    """The load base is DATA, not a guess: game/recomp_seeds.json is what keys the recompiled module's
    addresses, so measuring against any other base would measure a different program."""
    txt = re.sub(r"//[^\n]*", "", open(SEEDS_JSON, encoding="utf-8").read())
    seeds = json.loads(txt)
    for pat, base in seeds.get("overlay_base_patterns", []):
        if re.fullmatch(pat, name):
            return int(base, 16)
    b = seeds.get("overlay_bases", {}).get(name)
    if b:
        return int(b, 16)
    raise Refuse(f"{name}: no load base in game/recomp_seeds.json "
                 f"({len(seeds.get('overlay_base_patterns', []))} pattern(s), "
                 f"{len(seeds.get('overlay_bases', {}))} explicit) — cannot address its words at all")


def load_image(name, image_dir=None):
    if name == "MAIN":
        path = os.path.join(image_dir, "MAIN.BIN") if image_dir else MAIN_EXE
        if not os.path.isfile(path):
            raise Refuse(f"MAIN: no PS-X EXE at {path}. It is produced by tools/ensure_recomp.py "
                         "from the user's disc (gitignored). NOTHING was measured; this is not a pass.")
        raw = open(path, "rb").read()
        if len(raw) < 0x800 or raw[:8] != b"PS-X EXE":
            raise Refuse(f"MAIN: {path} is not a PS-X EXE ({len(raw)} bytes scanned)")
        base, size = struct.unpack_from("<II", raw, 0x18)
        if size == 0 or size % 4 or 0x800 + size > len(raw):
            raise Refuse(f"MAIN: header declares base={base:#010x}, text={size} bytes, but file is "
                         f"{len(raw)} bytes — no complete word corpus")
        words = list(struct.unpack_from("<%dI" % (size // 4), raw, 0x800))
        return words, base, path
    d = image_dir or OVL_DIR
    path = os.path.join(d, name + ".BIN")
    if not os.path.isfile(path):
        raise Refuse(f"{name}: no overlay slice at {path}. It is produced by "
                     f"tools/ensure_recomp.py from WAD.WAD (gitignored, ~110 MB) — run a build first. "
                     f"NOTHING was measured; this is not a pass.")
    raw = open(path, "rb").read()
    if len(raw) % 4:
        raise Refuse(f"{name}: {len(raw)} bytes is not a whole number of words")
    words = list(struct.unpack("<%dI" % (len(raw) // 4), raw))
    return words, overlay_base(name), path


def measure(prod, image_dir=None):
    """Derive the producer's guest address from the image bytes ALONE. The shipped constant is not read
    here and must not be — that is the entire point."""
    name = prod["image"]
    words, base, path = load_image(name, image_dir)
    where = f"{name} ({len(words)} words at {base:#010x}, {os.path.relpath(path, REPO)})"
    if "entry_sequence" in prod:
        desc, seq = prod["entry_sequence"]
        sites = [i for i in range(0, len(words) - len(seq) + 1)
                 if tuple(words[i:i + len(seq)]) == seq]
        if len(sites) != 1:
            raise Refuse(f"{prod['label']}: entry fingerprint {desc} matched {len(sites)} site(s) "
                         f"in {where}; exactly 1 is required. "
                         + (f"sites: {[hex(base + 4 * i) for i in sites[:8]]}" if sites else
                            "0 sites means the fingerprint no longer describes this image — "
                            "measured nothing; refusing."))
        site = sites[0]
        entry = base + 4 * site
        callers = sum(1 for w in words
                      if (w >> 26) == OP_JAL and ((((w & 0x3FFFFFF) << 2) | 0x80000000) == entry))
        return {
            "addr": entry, "image": name, "words": len(words), "base": base,
            "fingerprint_at": entry, "fingerprint": desc,
            "body_words": None, "callers": callers, "where": where,
        }
    desc, op, imm = prod["fingerprint"]
    sites = [i for i, w in enumerate(words) if (w >> 26) == op and (w & 0xFFFF) == imm]
    if len(sites) != 1:
        raise Refuse(f"{prod['label']}: fingerprint {desc} matched {len(sites)} site(s) in {where}; "
                     f"exactly 1 is required for it to IDENTIFY a function. "
                     + (f"sites: {[hex(base + 4 * i) for i in sites[:8]]}" if sites else
                        "0 sites means the fingerprint no longer describes this image — the overlay "
                        "set moved, or the RE was wrong. Measured nothing; refusing."))
    site = sites[0]
    # Walk back to the first word after a `jr $ra` + delay slot: the entry of the containing function.
    j = site
    while j >= 2 and words[j - 2] != JR_RA:
        j -= 1
    if j < 2:
        raise Refuse(f"{prod['label']}: walked back {site - j + 1} word(s) from the fingerprint at "
                     f"{base + 4 * site:#010x} to the START of {where} without crossing a `jr $ra` — "
                     f"no function boundary, so no entry could be derived")
    entry = base + 4 * j
    pro = words[j]
    if (pro >> 16) != 0x27BD or not (pro & 0x8000):
        raise Refuse(f"{prod['label']}: derived entry {entry:#010x} in {where} is not prologue-shaped "
                     f"(word {pro:#010x}, wanted `addiu sp,sp,-N` = 0x27BDxxxx with the immediate "
                     f"negative) — the backward walk landed mid-function; refusing rather than "
                     f"reporting an address")
    callers = sum(1 for w in words
                  if (w >> 26) == OP_JAL and ((((w & 0x3FFFFFF) << 2) | 0x80000000) == entry))
    return {
        "addr": entry, "image": name, "words": len(words), "base": base,
        "fingerprint_at": base + 4 * site, "fingerprint": desc,
        "body_words": None, "callers": callers, "where": where,
    }


# ── THE RUNTIME HALF ────────────────────────────────────────────────────────────────────────────────
def latest_jsonl():
    runs = sorted(glob.glob(os.path.join(REPO, "scratch", "producers", "run-*.jsonl")))
    if not runs:
        raise Refuse("--db latest: no scratch/producers/run-*.jsonl exists. The DB is written by a "
                     "CAPPED run (PSXPORT_NATIVE_FRAMES=n); an uncapped run is killed by signal and "
                     "writes nothing (C169). Measured nothing.")
    return runs[-1]


def check_db(path, want):
    """Require the SHIPPING PATH to have fired: a row keyed at the shipped constant, with prims."""
    if path == "latest":
        path = latest_jsonl()
    if not os.path.isfile(path):
        raise Refuse(f"--db {path}: no such file — measured nothing")
    rows, totals = [], None
    for ln in open(path, encoding="utf-8"):
        ln = ln.strip()
        if not ln:
            continue
        rec = json.loads(ln)
        if rec.get("type") == "totals":
            totals = rec
        else:
            rows.append(rec)
    keys = {int(r["key"], 16): r for r in rows if "key" in r}
    print(f"  run report {os.path.relpath(path, REPO)}: {len(rows)} row(s)"
          + (f", prims_seen {totals['prims_seen']}, attributed {totals['prims_attributed']}, "
             f"unscoped-native {totals['unscoped_native']}" if totals else ", NO totals record"))
    for lbl, addr in want:
        r = keys.get(addr)
        if r is None:
            raise Disagree(f"{lbl}: the run reported NO row keyed {addr:#010x}. Rows present: "
                           f"{sorted(hex(k) for k in keys) or '(none)'}. The constant is right and the "
                           f"scope did not fire — a deleted/never-reached ProducerScope looks exactly "
                           f"like this and the static check cannot see it.")
        if int(r.get("prims_native", 0)) <= 0:
            raise Disagree(f"{lbl}: row {addr:#010x} exists but prims_native="
                           f"{r.get('prims_native')} — a 0-prim row is indistinguishable from a "
                           f"producer that never drew, which is the shape the scope exists to avoid")
        print(f"  RAN     {lbl:<22} {addr:#010x}  prims_native {r['prims_native']}  "
              f"frames {r['frames']} (f{r['first_frame']}..f{r['last_frame']})  "
              f"pc_name {r.get('pc_name')!r}")


# ── THE COMPARISON ──────────────────────────────────────────────────────────────────────────────────
def run_check(src_overrides=None, image_dir=None, db=None, quiet=False, db_expect=None):
    src_overrides = src_overrides or {}
    scopes = scan_shipped_scopes()
    known = {p["label"] for p in PRODUCERS}
    unknown = [s for s in scopes if s[2] not in known]
    if not quiet:
        print(f"[verify-producers] {len(scopes)} ProducerScope site(s) in game/, "
              f"{len(PRODUCERS)} measurement recipe(s)")
    if unknown:
        raise Refuse("ProducerScope site(s) with NO measurement recipe in this tool: "
                     + "; ".join(f"{f}:{l} label={lab!r}" for f, l, lab in unknown)
                     + ". A producer key is a measured constant; one this tool cannot re-derive is "
                       "unverified, so it refuses rather than reporting the others as a pass.")
    want = []
    for prod in PRODUCERS:
        shipped, nsites = read_shipped(prod, src_overrides.get(prod["label"]))
        m = measure(prod, image_dir)
        agree = shipped == m["addr"]
        status = "AGREE" if agree else "DISAGREE"
        if not quiet:
            print(f"  {status:<8} {prod['label']:<22} shipped {shipped:#010x} "
                  f"({prod['src']} {prod['const']}, {nsites} scope site(s))  "
                  f"measured {m['addr']:#010x} from {m['where']}")
            print(f"           via {m['fingerprint']} at {m['fingerprint_at']:#010x} "
                  f"-> back to the enclosing `jr $ra`-delimited entry; {m['callers']} jal site(s) "
                  f"in the image target it")
        if not agree:
            raise Disagree(f"{prod['label']}: the port SHIPS {shipped:#010x} but the image MEASURES "
                           f"{m['addr']:#010x}. Either the constant was hand-edited or the overlay "
                           f"moved. The picture this port draws is unaffected — the DB row would "
                           f"simply be keyed to the wrong guest function, which is why nothing else "
                           f"catches this.")
        want.append((prod["label"], shipped))
    if db:
        # --db-expect narrows WHICH producers a run must have fired. Default: all of them. A run only
        # reaches the stages its content sits in — the native leg reaches the title (stage 13), so the
        # field producers (spriteq, pairedactor) cannot fire there — and demanding an unreachable
        # producer is a false FAIL, not a regression. The gate passes the labels its run can reach;
        # the sabotage case (a deleted ProducerScope) is caught because the DELETED label is still in
        # the expected set and the DB will lack its row.
        want = [w for w in want if not db_expect or w[0] in db_expect]
        check_db(db, want)
    return want


# ── SELFTEST: BOTH CLASSES, ON THE SHIPPING PATH ────────────────────────────────────────────────────
def selftest():
    """Positive: the real files agree. Negatives: a mutated SHIPPED value must DISAGREE, and a mutated
    IMAGE must REFUSE. Both mutants go through run_check() — the same function the gate calls — so this
    tests the shipping path rather than a helper beside it."""
    fails = []

    def expect(name, fn, want, because=None):
        """`because` is a substring the failure message MUST contain. Without it a negative can pass for
        the WRONG REASON and read as coverage it does not have — measured: with the shipped constant
        sabotaged, the two --db mutants below both went DISAGREE on the constant and never exercised the
        row check at all, while still printing `ok`."""
        try:
            fn()
            got = "PASS"
        except Disagree as e:
            got = "DISAGREE"
            detail = str(e)
        except Refuse as e:
            got = "REFUSE"
            detail = str(e)
        else:
            detail = "(agreed)"
        ok = got == want and (because is None or because in detail)
        why = "" if ok or because is None or because in detail else f"  [WRONG REASON: wanted {because!r}]"
        print(f"  {'ok  ' if ok else 'FAIL'} {name:<44} want {want:<8} got {got:<8} "
              f"{detail.splitlines()[0][:150]}{why}")
        if not ok:
            fails.append(name)

    expect("real files agree", lambda: run_check(quiet=True), "PASS")

    prod = PRODUCERS[0]
    with tempfile.TemporaryDirectory(dir=os.path.join(REPO, "scratch")) as td:
        # NEGATIVE 1 — the shipped constant is wrong by one nibble.
        src = os.path.join(REPO, prod["src"])
        text = open(src, encoding="utf-8").read()
        mut = os.path.join(td, "mut.cpp")
        open(mut, "w", encoding="utf-8").write(
            re.sub(r"(constexpr\s+uint32_t\s+" + re.escape(prod["const"]) + r"\s*=\s*)0x[0-9A-Fa-f]+",
                   r"\g<1>0x8007CD30", text, count=1))
        expect("mutant: shipped constant off by 8",
               lambda: run_check(src_overrides={prod["label"]: mut}, quiet=True), "DISAGREE",
               because="SHIPS 0x8007cd30")

        # NEGATIVE 2 — the constant is deleted entirely.
        mut2 = os.path.join(td, "mut2.cpp")
        open(mut2, "w", encoding="utf-8").write(
            text.replace("constexpr uint32_t " + prod["const"], "constexpr uint32_t kOther_", 1))
        expect("mutant: constant renamed away",
               lambda: run_check(src_overrides={prod["label"]: mut2}, quiet=True), "REFUSE",
               because="no `constexpr uint32_t")

        # NEGATIVE 3 — THE MEASUREMENT ITSELF. Blank the fingerprint word in a copy of the image: the
        # tool must refuse ("0 sites"), which proves the derivation reads the bytes and would not have
        # produced its answer from anywhere else.
        #
        # NOT wrapped in expect(): this load is the SELFTEST'S OWN CORPUS, and a missing corpus is not
        # a mutant result. Measured 2026-08-12 (operator verification of I048): with
        # scratch/bin/overlays/OV_5B800.BIN moved aside, `--selftest` died with an uncaught Refuse
        # TRACEBACK — non-zero by accident of Python's exit code rather than by design, and printing a
        # stack trace where the plain invocation prints a refusal. I048 recorded "REFUSES with exit 2 if
        # absent" for the invocation the GATE DOES NOT USE. Re-raised as Refuse so main() turns it into
        # the same exit 2 with the same sentence.
        words, base, path = load_image(prod["image"])
        desc, op, imm = prod["fingerprint"]
        idxs = [i for i, w in enumerate(words) if (w >> 26) == op and (w & 0xFFFF) == imm]
        mimg = os.path.join(td, "img")
        os.makedirs(mimg)
        raw = bytearray(open(path, "rb").read())
        for i in idxs:
            struct.pack_into("<I", raw, 4 * i, 0)
        open(os.path.join(mimg, prod["image"] + ".BIN"), "wb").write(bytes(raw))
        expect("mutant: fingerprint word blanked in image",
               lambda: run_check(image_dir=mimg, quiet=True), "REFUSE",
               because="matched 0 site(s)")

        # NEGATIVE 4 — a DUPLICATED fingerprint must also refuse, not pick one.
        mimg2 = os.path.join(td, "img2")
        os.makedirs(mimg2)
        raw2 = bytearray(open(path, "rb").read())
        # Copy the fingerprint word somewhere harmless-looking (a word that is currently 0).
        spare = next((i for i, w in enumerate(words) if w == 0 and i not in idxs), None)
        if spare is None:
            print("  ok   mutant: duplicated fingerprint            SKIPPED (no zero word to reuse)")
        else:
            struct.pack_into("<I", raw2, 4 * spare, words[idxs[0]])
            open(os.path.join(mimg2, prod["image"] + ".BIN"), "wb").write(bytes(raw2))
            expect("mutant: fingerprint duplicated in image",
                   lambda: run_check(image_dir=mimg2, quiet=True), "REFUSE",
                   because="matched 2 site(s)")

        # NEGATIVE 5 — THE RUNTIME HALF. A report with no row for the shipped key must DISAGREE, which
        # is what a deleted ProducerScope produces.
        empty = os.path.join(td, "empty.jsonl")
        open(empty, "w", encoding="utf-8").write(
            '{"type":"totals","prims_seen":1380,"prims_attributed":0,"gp0_anon":0,"span_miss":0,'
            '"span_no_fn":0,"guest_origin":0,"unscoped_native":1380,"overflow":0,"rows":0}\n')
        expect("mutant: run report has 0 rows", lambda: run_check(db=empty, quiet=True), "DISAGREE",
               because="reported NO row keyed")

        zero = os.path.join(td, "zero.jsonl")
        open(zero, "w", encoding="utf-8").write(
            '{"key":"0x8007CD38","kind":"guest","pc_name":"titlefx:spriteEmit","prims_guest":0,'
            '"prims_native":0,"frames":1,"first_frame":0,"last_frame":0}\n')
        expect("mutant: run row has 0 native prims", lambda: run_check(db=zero, quiet=True),
               "DISAGREE", because="prims_native=0")

    # The MAIN-image producer gets the same two classes of negative. Keeping this separate from the
    # legacy overlay block makes it explicit that adding a recipe is unfinished until ITS bytes and
    # ITS shipped constant have both demonstrated the other answer.
    prod = PRODUCERS[1]
    with tempfile.TemporaryDirectory(dir=os.path.join(REPO, "scratch")) as td:
        src = os.path.join(REPO, prod["src"])
        text = open(src, encoding="utf-8").read()
        mut = os.path.join(td, "mut-main.cpp")
        open(mut, "w", encoding="utf-8").write(
            re.sub(r"(constexpr\s+uint32_t\s+" + re.escape(prod["const"]) + r"\s*=\s*)0x[0-9A-Fa-f]+",
                   r"\g<1>0x80022A24", text, count=1))
        expect("MAIN mutant: shipped constant off by 8",
               lambda: run_check(src_overrides={prod["label"]: mut}, quiet=True), "DISAGREE",
               because="SHIPS 0x80022a24")

        # Stage every recipe's corpus because run_check deliberately checks ALL shipped producers.
        shutil.copy2(MAIN_EXE, os.path.join(td, "MAIN.BIN"))
        for p in PRODUCERS:
            if p["image"] != "MAIN":
                shutil.copy2(os.path.join(OVL_DIR, p["image"] + ".BIN"),
                             os.path.join(td, p["image"] + ".BIN"))
        words, base, path = load_image("MAIN", td)
        _desc, seq = prod["entry_sequence"]
        sites = [i for i in range(len(words) - len(seq) + 1)
                 if tuple(words[i:i + len(seq)]) == seq]
        if len(sites) != 1:
            raise Refuse(f"MAIN selftest corpus: entry sequence matched {len(sites)} site(s), wanted 1")

        raw = bytearray(open(path, "rb").read())
        struct.pack_into("<I", raw, 0x800 + 4 * sites[0], 0)
        open(os.path.join(td, "MAIN.BIN"), "wb").write(raw)
        expect("MAIN mutant: entry sequence blanked",
               lambda: run_check(image_dir=td, quiet=True), "REFUSE",
               because="matched 0 site(s)")

        # Restore, then duplicate the WHOLE sequence into a zero run. Duplicating one word would not
        # exercise a sequence discriminator and could pass for the wrong reason.
        raw = bytearray(open(MAIN_EXE, "rb").read())
        spare = next((i for i in range(len(words) - len(seq) + 1)
                      if i != sites[0] and all(w == 0 for w in words[i:i + len(seq)])), None)
        if spare is None:
            raise Refuse("MAIN selftest corpus has no zero run long enough to duplicate the entry "
                         "sequence — the ambiguity negative could not run")
        struct.pack_into("<%dI" % len(seq), raw, 0x800 + 4 * spare, *seq)
        open(os.path.join(td, "MAIN.BIN"), "wb").write(raw)
        expect("MAIN mutant: entry sequence duplicated",
               lambda: run_check(image_dir=td, quiet=True), "REFUSE",
               because="matched 2 site(s)")

    print(f"[selftest] {len(fails)} FAIL(s)" + ("" if not fails else ": " + ", ".join(fails)))
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--db", metavar="PATH|latest",
                    help="also require a producer-DB JSONL to carry the row (the shipping path fired)")
    ap.add_argument("--db-expect", metavar="LABELS",
                    help="with --db, only require these producer label(s) to have fired "
                         "(comma-separated). Default: all. Use when a run reaches only some stages — "
                         "the native leg reaches the title, so field producers cannot fire there and "
                         "must not be demanded.")
    ap.add_argument("--selftest", action="store_true", help="run both classes and fail if either does")
    a = ap.parse_args()
    if a.selftest:
        # A missing corpus must REFUSE with the same exit 2 and the same sentence as the plain
        # invocation. Before this, --selftest let the Refuse escape as a traceback (see NEGATIVE 3).
        try:
            rc = selftest()
        except Refuse as e:
            print("[selftest] REFUSED — the selftest's own corpus is missing, so the mutants were "
                  "never built and NOTHING was tested:\n  " + str(e))
            return 2
        if rc == 0:
            print("[selftest] PASS — the real files agree AND every mutant is caught")
        return rc
    db_expect = {x.strip() for x in a.db_expect.split(",") if x.strip()} if a.db_expect else None
    try:
        run_check(db=a.db, db_expect=db_expect)
    except Disagree as e:
        print("[verify-producers] DISAGREEMENT:\n  " + str(e))
        return 1
    except Refuse as e:
        print("[verify-producers] REFUSED — measured nothing:\n  " + str(e))
        return 2
    print("[verify-producers] OK — every shipped producer key equals the address measured from the "
          "guest image" + (" and a run reported its row" if a.db else
                           " (static only; pass --db to require a run to have reported the row)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
