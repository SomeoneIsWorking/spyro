#!/usr/bin/env python3
"""whatis.py — everything known about a guest address, from every source, in one answer.

WHY THIS EXISTS (workflow-first). Every diagnosis on this port reduces to the same question — "what
is 0x800xxxxx?" — and answering it means cross-referencing five sources that live in five different
places: the executable image, the overlay images, a RAM dump of the running port, the recompiler's
emitted function set, and Ghidra. Doing that by hand is slow and, more to the point, it is WRONG
often enough to matter. Two errors this session came from exactly that:

  * 0x8007CFB4 was read out of the overlay the router had last IDENTIFIED, which was not the one
    resident at the fail-fast. It showed a table-indexed load and no prologue, and produced a
    confidently wrong diagnosis ("a jump-table case label") that reached a commit. In the RESIDENT
    bytes it is an ordinary function. (C065, issue 0025.)
  * main's per-overlay entry table was extracted with a backward scan that grabbed a neighbouring
    store's lui/addiu pair, yielding 41 wrong addresses instead of 36 right ones — which made the
    entry-seed derivation look impossible for an iteration. (C067.)

Both were cross-referencing mistakes, not reasoning mistakes. So the fix is not to be more careful;
it is to make the cross-reference a command. This prints what every source says and, crucially, says
when they DISAGREE — a disagreement is the finding, and it is precisely what a human comparing files
by eye tends to miss.

Usage:
  whatis.py 0x8007CFB4                 # everything known about this address
  whatis.py 0x8007CFB4 --disasm 12     # ...plus N instructions from the authoritative source
  whatis.py --ram scratch/raw/other.bin 0x8007CFB4
"""
import argparse
import glob
import json
import os
import re
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "external", "psxport", "tools", "recomp"))
import psexe                                    # noqa: E402
from decode import decode                       # noqa: E402

EXE = "scratch/bin/spyro/SCUS_942.28"
OVL_DIR = "scratch/bin/overlays"
OVERLAYS_JSON = "game/overlays.json"
RAM_DEFAULT = "scratch/raw/miss_ram.bin"
GEN = "generated"

C = {"hdr": "\033[1;36m", "warn": "\033[1;33m", "bad": "\033[1;31m",
     "ok": "\033[32m", "dim": "\033[2m", "off": "\033[0m"}


def head(t):
    print(f"\n{C['hdr']}{t}{C['off']}")


def word_at(buf, off):
    if off < 0 or off + 4 > len(buf):
        return None
    return struct.unpack_from("<I", buf, off)[0]


def is_prologue(addr, w):
    if w is None:
        return False
    i = decode(addr, w)
    return i.op == "addiu" and i.rt == 29 and i.rs == 29 and i.simm < 0


def load_overlays():
    p = os.path.join(REPO, OVERLAYS_JSON)
    if not os.path.exists(p):
        return 0x8007AA38, []
    d = json.load(open(p))
    arena = int(d["arena_base"], 16)
    out = []
    for e in d["overlays"]:
        path = os.path.join(REPO, OVL_DIR, e["name"] + ".BIN")
        data = open(path, "rb").read() if os.path.exists(path) else b""
        out.append((e["name"], arena, data, e.get("entry_seeds", [])))
    return arena, out


def recompiled_set():
    """Addresses the recompiler actually emitted, per module, from the generated decls."""
    out = {}
    for p in glob.glob(os.path.join(REPO, GEN, "*decls.h")):
        mod = os.path.basename(p).replace("_decls.h", "").replace("rec", "MAIN")
        # MAIN emits `gen_func_<ADDR>`; overlay modules emit `ov_<tag>_gen_<ADDR>`. Matching only the
        # first form silently reported "not recompiled" for EVERY overlay function — the exact class of
        # quiet wrong answer this tool exists to stop, caught only because a known-seeded address came
        # back NO. Match the address, not one module's prefix.
        addrs = set(int(x, 16) for x in re.findall(r"\bvoid \w*?gen_?\w*?_?([0-9A-Fa-f]{8})\(", open(p).read()))
        if addrs:
            out[mod] = addrs
    return out


def main_entry_table(exe):
    """The per-overlay per-frame entries main installs into [0x80075734] (C067)."""
    out = set()
    for a in range(exe.load, exe.text_end - 4, 4):
        i = decode(a, exe.word(a))
        if i.op != "sw" or i.simm != 0x5734:
            continue
        lui = addiu = None
        for b in range(1, 6):
            q = decode(a - b * 4, exe.word(a - b * 4))
            if q.op == "addiu" and q.rt == i.rt and addiu is None:
                addiu = q
            elif q.op == "lui" and addiu is not None and q.rt == addiu.rs:
                lui = q
                break
        if lui and addiu:
            out.add(((lui.imm << 16) + addiu.simm) & 0xFFFFFFFF)
    return out


def static_refs(exe, target):
    """Direct jal/j to the address, and any word in the image equal to it (a stored fn pointer)."""
    calls, ptrs = [], []
    for a in range(exe.load, exe.text_end - 4, 4):
        w = exe.word(a)
        i = decode(a, w)
        if i.kind == "jump" and getattr(i, "target", None) == target:
            calls.append(a)
        if w == target:
            ptrs.append(a)
    return calls, ptrs


def ghidra_view(addr):
    """What Ghidra made of it, from any decomp already produced under scratch/decomp/.

    Ghidra is the only source here that does real data-flow and code/data separation, so it catches
    what a pattern scan cannot — it is what showed 0x8007CFB4 to be an ordinary function with a
    0x198-byte frame while the (wrong) image being read showed no prologue at all. Produce a decomp
    with external/psxport/tools/decomp.sh; this reads whatever is already there rather than launching
    Ghidra, so the common case stays instant."""
    out = []
    for p in glob.glob(os.path.join(REPO, "scratch", "decomp", "*.c")):
        txt = open(p, errors="replace").read()
        m = re.search(r"^// =+ %08X (\S+) =+$" % addr, txt, re.M | re.I)
        if m:
            body = txt[m.end():]
            nxt = re.search(r"^// =+ [0-9A-F]{8} ", body, re.M)
            lines = (body[:nxt.start()] if nxt else body).strip().splitlines()
            out.append((os.path.relpath(p, REPO), m.group(1), len(lines)))
    return out


def docs_mentions(addr):
    """Claims / issues / frontier entries that already mention this address — read before re-deriving."""
    pat = re.compile(r"0x%08X" % addr, re.I)
    hits = []
    for root in ("docs",):
        for dirpath, _dirs, files in os.walk(os.path.join(REPO, root)):
            for fn in files:
                if not fn.endswith(".md"):
                    continue
                p = os.path.join(dirpath, fn)
                try:
                    if pat.search(open(p, errors="replace").read()):
                        hits.append(os.path.relpath(p, REPO))
                except OSError:
                    pass
    return sorted(hits)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("addr")
    ap.add_argument("--ram", default=RAM_DEFAULT, help="RAM dump to consult (default: the last miss dump)")
    ap.add_argument("--disasm", type=int, default=0, metavar="N", help="disassemble N instructions")
    a = ap.parse_args()
    addr = int(a.addr, 0)

    exe = psexe.load(os.path.join(REPO, EXE))
    arena, overlays = load_overlays()
    rec = recompiled_set()

    print(f"{C['hdr']}whatis 0x{addr:08X}{C['off']}")

    # ---- which module(s) could own it -----------------------------------------------------------
    head("WHERE IT LIVES")
    in_main = exe.load <= addr < exe.text_end
    if in_main:
        print(f"  MAIN text [0x{exe.load:08X},0x{exe.text_end:08X})  — resident, always mapped")
    owners = [(n, d) for (n, _b, d, _e) in overlays if arena <= addr < arena + len(d)]
    if owners:
        print(f"  inside the overlay arena at 0x{arena:08X} — {len(owners)} overlay(s) span it:")
        for n, d in owners:
            print(f"      {n:12s} [0x{arena:08X},0x{arena + len(d):08X})")
        print(f"  {C['warn']}ALL overlays load at the SAME base, so spanning it means nothing on its own."
              f"{C['off']}\n      Which one is RESIDENT is a runtime fact — see the RAM section below.")
    if not in_main and not owners:
        print(f"  {C['warn']}not in MAIN text and not in any known overlay's span.{C['off']}")
        print("      If it should be an overlay, run tools/overlay_scan.py — the set only contains")
        print("      overlays a run was observed to load.")

    # ---- what the RESIDENT bytes say ------------------------------------------------------------
    head("RESIDENT BYTES (the authority for anything in the arena)")
    ram_path = os.path.join(REPO, a.ram) if not os.path.isabs(a.ram) else a.ram
    resident_word = None
    resident_owner = None
    if not os.path.exists(ram_path):
        print(f"  no RAM dump at {a.ram}.")
        print("      The port writes one at every recomp-MISS. Without it, an address in the arena")
        print("      CANNOT be attributed to an overlay — that is the mistake C065 records.")
    else:
        ram = open(ram_path, "rb").read()
        resident_word = word_at(ram, addr & 0x1FFFFF)
        print(f"  {a.ram}: word = 0x{resident_word:08X}   ({decode(addr, resident_word).op})"
              f"   prologue={'YES' if is_prologue(addr, resident_word) else 'no'}")
        # Which overlay's image matches what is actually in RAM at the arena?
        matches = []
        for n, _b, d, _e in overlays:
            if not d:
                continue
            same = sum(1 for i in range(0, min(len(d), 0x400), 4)
                       if word_at(ram, (arena + i) & 0x1FFFFF) == word_at(d, i))
            matches.append((same, n, len(d)))
        matches.sort(reverse=True)
        if matches:
            best, bestn, _ = matches[0]
            total = min(matches[0][2], 0x400) // 4
            resident_owner = bestn if best > total * 0.9 else None
            tag = C['ok'] + "RESIDENT" + C['off'] if resident_owner else C['warn'] + "best guess" + C['off']
            print(f"  arena content matches {bestn} on {best}/{total} of the first words  [{tag}]")
            if not resident_owner:
                print(f"      {C['warn']}NO overlay matches well. The resident overlay is not in the set —"
                      f"{C['off']}\n      find it: search WAD.WAD for the resident bytes, then overlay_scan.py.")

    # ---- what each candidate IMAGE says, and whether they agree ---------------------------------
    if owners:
        head("PER-IMAGE VIEW (disagreement here is the finding)")
        for n, d in owners:
            w = word_at(d, addr - arena)
            mark = ""
            if resident_word is not None:
                mark = (C['ok'] + "  == resident" + C['off']) if w == resident_word \
                       else (C['bad'] + "  != RESIDENT — reading this image would mislead" + C['off'])
            print(f"  {n:12s} 0x{w:08X}  {decode(addr, w).op:6s} "
                  f"prologue={'YES' if is_prologue(addr, w) else 'no '}{mark}")

    # ---- is it recompiled ----------------------------------------------------------------------
    head("RECOMPILED?")
    found = [m for m, s in rec.items() if addr in s]
    if found:
        print(f"  {C['ok']}yes{C['off']} — emitted in: {', '.join(sorted(found))}")
    else:
        print(f"  {C['bad']}NO{C['off']} — no module emits a function at this address, so a call here")
        print("      fail-fasts. Either it is not a function entry, or its module needs a seed.")

    # ---- is it a known per-overlay entry --------------------------------------------------------
    head("PER-OVERLAY ENTRY TABLE (C067)")
    tbl = main_entry_table(exe)
    if addr in tbl:
        print(f"  {C['ok']}yes{C['off']} — main installs this into [0x80075734] "
              f"(one of {len(tbl)} distinct entries)")
        claim = [n for (n, _b, _d, e) in overlays if ("0x%08X" % addr) in e]
        print(f"      claimed by: {', '.join(claim) if claim else C['warn'] + 'NO overlay — not prologue-shaped in any image' + C['off']}")
    else:
        print(f"  no — not one of the {len(tbl)} addresses main installs as an overlay entry")

    # ---- who points at it ----------------------------------------------------------------------
    head("STATIC REFERENCES IN MAIN")
    calls, ptrs = static_refs(exe, addr)
    print(f"  direct jal/j : {len(calls)}" + ("   " + " ".join("0x%08X" % x for x in calls[:6]) if calls else ""))
    print(f"  stored words : {len(ptrs)}" + ("   " + " ".join("0x%08X" % x for x in ptrs[:6]) if ptrs else ""))
    if not calls and not ptrs:
        print(f"      {C['dim']}nothing static points here — reached by a computed/indirect path,"
              f" which is why discovery cannot see it.{C['off']}")

    # ---- Ghidra ---------------------------------------------------------------------------------
    head("GHIDRA")
    gv = ghidra_view(addr)
    if gv:
        for path, name, nlines in gv:
            print(f"  {C['ok']}function {name}{C['off']}  ({nlines} lines decompiled)   {path}")
    else:
        print(f"  {C['dim']}no decomp covering this address under scratch/decomp/.{C['off']}")
        print("      external/psxport/tools/decomp.sh import scratch/raw/miss_ram.bin <proj>")
        print("      external/psxport/tools/decomp.sh decomp <proj> scratch/decomp/x.c <lo> <hi>")

    # ---- what the project already recorded ------------------------------------------------------
    head("ALREADY RECORDED (read before re-deriving)")
    hits = docs_mentions(addr)
    print("  " + ("\n  ".join(hits) if hits else C['dim'] + "no claim, issue or frontier entry mentions it" + C['off']))

    # ---- optional disassembly from the authoritative source -------------------------------------
    if a.disasm:
        src = None
        if resident_word is not None and (owners or not in_main):
            ram = open(ram_path, "rb").read()
            src = ("RESIDENT RAM", lambda x: word_at(ram, x & 0x1FFFFF))
        elif in_main:
            src = ("MAIN image", lambda x: exe.word(x))
        if src:
            head(f"DISASSEMBLY ({src[0]})")
            for k in range(a.disasm):
                x = addr + k * 4
                w = src[1](x)
                if w is None:
                    break
                i = decode(x, w)
                print(f"  {x:08X}: {w:08X}  {i.op}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
