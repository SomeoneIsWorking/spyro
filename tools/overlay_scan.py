#!/usr/bin/env python3
"""overlay_scan.py — recover the overlay set from what the RUNNING PORT loads.

WHY THIS EXISTS. Spyro's overlays are not files. They are byte ranges inside WAD.WAD that the game
streams into ONE shared arena at 0x8007AA38, swapping constantly (a single run does OVL0 -> OVL2 ->
OVL0 -> OVL3 -> ...). Nothing in the executable enumerates them: the load offset arrives in a
register at the call site. So the only authority on "which overlay exists, and where is it in the
WAD" is a run.

That made the workflow one-overlay-per-rebuild — run, read the fail-fast, find the load, hand-edit
two files, re-emit, repeat — with each round costing a full recomp and build. It also produced a
WRONG diagnosis: the last overlay the router *identified* is not the one resident at the fail-fast
(the arena is reloaded after it), so reading the missing function out of that image showed unrelated
bytes. See docs/issues/0025.

WHAT THIS DOES. Reads the `cdq` log a run already emits — every arena load names its WAD offset and
length verbatim:

    [cdq] stream: a0=37 dest=0x8007AA38 len=40960 a3=0x0502F800 ... -> moved 40960 bytes

and writes the deduplicated set to game/overlays.json, which ensure_recomp.py consumes. Nothing here
is inferred: an entry exists because a run loaded it, and its offset and length are the ones the
loader was given.

MERGES, never truncates. Each run reaches only as far as the port currently gets, so later runs
discover overlays earlier ones could not. The existing file is read first and the union written back,
so a shorter run cannot silently drop overlays a longer one found.

Usage:
  overlay_scan.py --log scratch/logs/run.log          # merge one log's loads into game/overlays.json
  overlay_scan.py --log a.log --log b.log             # several runs at once
  overlay_scan.py --run 40                            # run the port headless for N seconds, then scan
  overlay_scan.py --list                              # show the current set, no changes
"""
import argparse
import json
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "game", "overlays.json")
EXE = "scratch/bin/spyro/SCUS_942.28"

# The arena base. Not hardcoded as a guess — it is the read-only constant [0x800113A0] the loader
# reads (claim C032), and it is what GameConfig::overlaySlots[0] declares.
ARENA = 0x8007AA38

# `dest=` is the guest destination, `a3=` the WAD byte offset, `len=` the byte count. Both the
# `stream:` (level/streaming) and `loader:` (sync boot) lines carry the same fields.
LOAD_RE = re.compile(
    r"(?:stream|loader):.*?dest=0x([0-9A-Fa-f]+)\s+len=(\d+)\s+a3=0x([0-9A-Fa-f]+)")


def scan_log(path):
    """Return {(wad_offset, length)} for every load landing at the arena base."""
    found = set()
    with open(path, "r", errors="replace") as f:
        for line in f:
            m = LOAD_RE.search(line)
            if not m:
                continue
            dest, ln, off = int(m.group(1), 16), int(m.group(2)), int(m.group(3), 16)
            if dest == ARENA:
                found.add((off, ln))
    return found


def load_existing():
    if not os.path.exists(OUT):
        return []
    with open(OUT) as f:
        data = json.load(f)
    return [(int(e["wad_offset"], 16), int(e["length"])) for e in data["overlays"]]


# ── reference-name annotation ─────────────────────────────────────────────────────────────────────
# The vendored decomp (TheMobyCollective/spyro-1, external/spyro-1) publishes SHA-256 hashes of the
# named overlays it BUILDS (build/wad/<name>.ovl). A matching decomp rebuilds the retail bytes, so
# hashing the same slice of the RETAIL WAD.WAD and finding it in that table is a BYTE-VERIFIED name
# for the module — not a guess, and not a symbol name trusted on faith. 2026-08-16 measurement: 6 of
# the 12 overlays observed at runtime map this way, including every overlay that has an entry seed.
# The decomp describes 37 overlays; the retail WAD's own index (tools/wad_index.py) locates 36 of
# them byte-identically (35 levels + titlescreen) — the 37th, credits.ovl, is not a sector-aligned
# WAD entry, which is a finding in itself. The stable key stays OV_<hex> (it is the emitted module
# prefix and what seeds/claims cite); reference_name is a label for humans, absent when the retail
# WAD or the vendored table is unavailable (fresh checkout) or the bytes match nothing.
REF_SHA = os.path.join("external", "spyro-1", "sha256sum.txt")
WAD_PATH = os.path.join("scratch", "wad", "WAD.WAD")


def load_reference_names():
    """{sha256: short name} from the vendored decomp's build manifest. Empty when absent."""
    path = os.path.join(REPO, REF_SHA)
    if not os.path.exists(path):
        return {}
    out = {}
    for line in open(path):
        m = re.match(r"(\w+)\s+(\S+)", line.strip())
        if not m:
            continue
        h, rel = m.group(1), m.group(2)
        short = rel.rsplit("/", 1)[-1].removesuffix(".ovl")
        out[h] = short
    return out


def reference_names_for(entries):
    """{wad_offset: reference_name} for entries whose retail-WAD slice matches a named decomp
    overlay. Returns {} when the retail WAD is not on disk (a fresh checkout) — the stable OV_<hex>
    key carries the set then, exactly as before."""
    wad_path = os.path.join(REPO, WAD_PATH)
    refs = load_reference_names()
    if not refs or not os.path.exists(wad_path):
        return {}
    import hashlib
    wad = open(wad_path, "rb").read()
    out = {}
    for (off, ln) in entries:
        if off + ln > len(wad):
            continue
        h = hashlib.sha256(wad[off:off + ln]).hexdigest()
        if h in refs:
            out[off] = refs[h]
    return out


def write_set(entries, seed_override=None):
    """Write the merged set, naming each overlay after its WAD offset.

    NAMES ARE DERIVED FROM THE OFFSET, NOT THE INDEX, and that is the whole point. The name is the
    emitted module's symbol prefix, the router's identity string, the key under `overlay_seeds`, and
    what claims and issues cite. Numbering by discovery order (OVL0, OVL1, ...) renames every overlay
    the moment an earlier-offset one is found — which happened immediately here: picking up two small
    loads shifted OVL0..OVL3 by one and silently pointed every existing seed at the wrong module. An
    offset-derived name cannot collide, cannot shift, and says where the bytes came from.
    reference_name (where present) is a BYTE-VERIFIED label from the vendored decomp, keyed by the
    retail-WAD slice's SHA-256 against its build manifest — never trusted on faith, and never the
    stable key. seed_override ({OV_<hex>: [addresses]}) lets the --names path preserve the seeds the
    set already carries instead of recomputing them from the EXE and BINs on disk."""
    entries = sorted(set(entries))
    table = main_entry_table()
    ref_names = reference_names_for(entries)
    if seed_override is None:
        seed_override = {}
    data = {
        "_comment": [
            "GENERATED by tools/overlay_scan.py from a running port's cdq log — do not hand-edit.",
            "Each entry is a byte range in WAD.WAD that a run was OBSERVED to stream into the shared",
            "overlay arena at 0x%08X. Offsets and lengths are the ones the loader was given, not" % ARENA,
            "inferred. Each name is DERIVED FROM ITS WAD OFFSET (OV_<hex>) so it never shifts when a",
            "new overlay is discovered — an index-based name would silently re-point existing seeds.",
            "Re-run overlay_scan.py after a run that gets further; the set is merged, never replaced.",
            "reference_name, where present, is a BYTE-VERIFIED label: the SHA-256 of this slice of the",
            "retail WAD.WAD matches a named overlay in external/spyro-1/sha256sum.txt (a matching",
            "decomp, so the bytes are identical). The stable key is still OV_<hex> — reference_name is",
            "for humans only. Measured 2026-08-16: 6 of 12 runtime overlays map; the decomp's 37th",
            "overlay (credits.ovl) is not a sector-aligned WAD entry.",
            "entry_seeds are DERIVED, not observed: main installs each overlay's per-frame entry into",
            "[0x80075734] (43 stores, 36 distinct addresses), and an entry is claimed by an overlay only",
            "if it is prologue-shaped (addiu sp,sp,-N) in THAT overlay's own bytes. All overlays share one",
            "base, so the prologue test is what stops another level's entry being seeded into this module.",
        ],
        "arena_base": "0x%08X" % ARENA,
        "entry_table_size": len(table),
        "overlays": [{"name": "OV_%X" % off, "wad_offset": "0x%X" % off, "length": ln,
                      "reference_name": ref_names.get(off),
                      "entry_seeds": seed_override.get("OV_%X" % off,
                                                       entry_seeds_for("OV_%X" % off, table))}
                     for (off, ln) in entries],
    }
    with open(OUT, "w") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
        f.write("\n")
    return data


# ── per-overlay entry seeds ───────────────────────────────────────────────────────────────────────
# Every level overlay has ONE per-frame entry, installed into the global [0x80075734] and called
# indirectly from main's stage tick at 0x80033AA4 (`lw v0,[0x80075734] ; jalr v0`). Nothing static
# points at it, so `jal` discovery cannot see it and each one otherwise surfaces as its own fail-fast
# — a full recomp+build+run per overlay.
#
# But main DOES name them all. A per-level setup routine stores each overlay's entry into that global,
# one arm per level, as a plain `lui/addiu` pair:
#     lui   v0, HI ; addiu v0, v0, LO ; lui at, 0x8007 ; sw v0, 0x5734(at)
# 43 store sites yielding 36 distinct addresses — which matches the 36 code overlays located in the
# WAD index (C033) and the ~37 the public decomps describe.
#
# THE SET ALONE IS NOT ENOUGH, and this is the part that matters. Every overlay loads at the SAME
# arena base, so another level's entry is a perfectly valid-looking address inside THIS overlay's
# span; seeding all 36 into every module would land 35 of them mid-function and split real code —
# exactly the corruption recomp_seeds.json exists to prevent. So an entry is claimed by an overlay
# only if it is PROLOGUE-SHAPED (`addiu sp,sp,-N`) in that overlay's OWN bytes. That test is what
# separates them, and it separates them cleanly: each level overlay claims exactly one, and the
# small data-only reads claim none.
ENTRY_GLOBAL_OFF = 0x5734          # sw rX, 0x5734(at) — the [0x80075734] store
EXE_PATH = "scratch/bin/spyro/SCUS_942.28"


def _decoder():
    sys.path.insert(0, os.path.join(REPO, "external", "psxport", "tools", "recomp"))
    import psexe                                        # noqa: E402
    from decode import decode                           # noqa: E402
    return psexe, decode


def main_entry_table():
    """The addresses main installs into [0x80075734], read out of its own code."""
    psexe, decode = _decoder()
    exe = psexe.load(os.path.join(REPO, EXE_PATH))
    out = set()
    for a in range(exe.load, exe.text_end - 4, 4):
        i = decode(a, exe.word(a))
        if i.op != "sw" or i.simm != ENTRY_GLOBAL_OFF:
            continue
        lui = addiu = None
        for b in range(1, 6):                           # the pair sits just above the store
            q = decode(a - b * 4, exe.word(a - b * 4))
            if q.op == "addiu" and q.rt == i.rt and addiu is None:
                addiu = q
            elif q.op == "lui" and addiu is not None and q.rt == addiu.rs:
                lui = q
                break
        if lui and addiu:
            out.add(((lui.imm << 16) + addiu.simm) & 0xFFFFFFFF)
    return sorted(out)


def entry_seeds_for(name, table):
    """Which of `table` are prologue-shaped inside this overlay's own image."""
    _, decode = _decoder()
    path = os.path.join(REPO, "scratch", "bin", "overlays", name + ".BIN")
    if not os.path.exists(path):
        return []
    b = open(path, "rb").read()
    hits = []
    for t in table:
        off = t - ARENA
        if off < 0 or off + 4 > len(b):
            continue
        i = decode(t, int.from_bytes(b[off:off + 4], "little"))
        if i.op == "addiu" and i.rt == 29 and i.rs == 29 and i.simm < 0:
            hits.append("0x%08X" % t)
    return hits


def run_port(secs):
    disc = os.environ.get("PSXPORT_SPYRO_DISC", "")
    if not disc:
        env_path = os.path.join(REPO, ".env")
        if os.path.exists(env_path):
            for line in open(env_path):
                m = re.match(r"\s*PSXPORT_SPYRO_DISC\s*=\s*(.+)", line)
                if m:
                    disc = m.group(1).strip()
                    break
    if not disc or not os.path.exists(disc):
        sys.exit("overlay_scan: no disc image (set PSXPORT_SPYRO_DISC or .env)")
    log = os.path.join(REPO, "scratch", "logs", "overlay_scan.log")
    os.makedirs(os.path.dirname(log), exist_ok=True)
    # PSXPORT_NOPACE: this is a scan, not a play session — run as fast as the host can. Headless is
    # paced like a windowed run now (they are one program), so "fast" has to be ASKED for.
    env = dict(os.environ, PSXPORT_DEBUG="cdq", PSXPORT_VK_HEADLESS="1", PSXPORT_NOAUDIO="1",
               PSXPORT_NOPACE="1", PSXPORT_WATCHDOG="0", PSXPORT_ASSET_DIR="external/psxport",
               PSXPORT_SPYRO_DISC=disc)
    with open(log, "w") as f:
        subprocess.run(["timeout", "-s", "KILL", str(secs), "./scratch/bin/spyro_port", EXE],
                       cwd=REPO, env=env, stdout=f, stderr=subprocess.STDOUT)
    return log


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", action="append", default=[], help="a run log to scan (repeatable)")
    ap.add_argument("--run", type=int, metavar="SECS", help="run the port headless first, then scan")
    ap.add_argument("--names", action="store_true",
                    help="re-annotate the CURRENT set with byte-verified reference names, no scan;"
                         " entry_seeds are PRESERVED, not recomputed")
    ap.add_argument("--list", action="store_true", help="print the current set and exit")
    a = ap.parse_args()

    if a.list:
        cur = load_existing()
        ref_names = reference_names_for(cur)
        print(f"{len(cur)} overlay(s) in {os.path.relpath(OUT, REPO)}:")
        for (off, ln) in cur:
            ref = ref_names.get(off)
            print(f"  OV_{off:X}  WAD +0x{off:X}  {ln} bytes"
                  + (f"  ({ref})" if ref else ""))
        return 0

    logs = list(a.log)
    if a.run:
        logs.append(run_port(a.run))
    if a.names:
        if not os.path.exists(OUT):
            sys.exit("overlay_scan: --names needs an existing game/overlays.json")
        with open(OUT) as f:
            keep = {e["name"]: e.get("entry_seeds", []) for e in json.load(f)["overlays"]}
        write_set(load_existing(), seed_override=keep)
        named = {o["wad_offset"]: o.get("reference_name")
                 for o in json.load(open(OUT))["overlays"] if o.get("reference_name")}
        print(f"{len(keep)} overlay(s) re-annotated; "
              f"{len(named)} byte-verified reference name(s) from external/spyro-1/sha256sum.txt")
        for off, name in sorted(named.items()):
            print(f"  OV_{int(off, 16):X}  WAD +{off}  {name}")
        return 0
    if not logs:
        ap.error("give --log, --run, --names or --list")

    before = set(load_existing())
    found = set()
    for p in logs:
        if not os.path.exists(p):
            sys.exit(f"overlay_scan: no such log: {p}")
        f = scan_log(p)
        print(f"  {p}: {len(f)} arena load(s)")
        found |= f
    if not found:
        # An empty scan is ambiguous — no arena loads happened, or the log lacks the cdq channel.
        # Say which is possible rather than writing an empty set over a good one.
        print("overlay_scan: NO arena loads in any log. Either the run never loaded an overlay, or "
              "it was not run with PSXPORT_DEBUG=cdq (the loads are only logged on that channel). "
              "Nothing written.", file=sys.stderr)
        return 1

    merged = before | found
    new = merged - before
    write_set(merged)
    print(f"\n{len(merged)} overlay(s) -> {os.path.relpath(OUT, REPO)}"
          f"  ({len(new)} new this scan)")
    for off, ln in sorted(new):
        print(f"  NEW  WAD +0x{off:X}  {ln} bytes")
    if new:
        print("\nRe-run tools/ensure_recomp.py to emit the new module(s).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
