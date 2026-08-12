#!/usr/bin/env python3
"""ensure_recomp.py — the single, hash-checked recompilation step.

ONE entry point that guarantees the statically-recompiled substrate in generated/ is PRESENT and
matches a deterministic hash of its INPUTS. run.sh calls only this; all recomp provisioning lives
here rather than scattered through the shell script.

What it does, in order:
  1. Resolve the disc image (CLI arg > $PSXPORT_SPYRO_DISC > .env > *.chd drop-in — mirrors run.sh).
  2. Extract SCUS_942.28 via psxport's `discdump`, plus WAD.WAD, and slice the overlays out of it.
  3. Compute the recomp IDENTITY = emit.py's RECOMP_VERSION + a hash of the INPUTS (the executable +
     the recompiler module sources + OUR SEED FILE). If the stored identity matches, the on-disk
     version stamp matches, and the generated set is complete, do nothing. Otherwise re-run emit.py.

Spyro differs from psxport's reference consumer (Tomba!2) in where its overlays live. The disc holds
a SINGLE executable and no \\BIN\\*.BIN files, but the game is NOT overlay-free: its overlays sit
INSIDE WAD.WAD and are loaded into the heap, which is why the disc's file tree shows none. Confirmed
from the binary — four hardcoded jals call addresses above the resident text end (docs/issues/0001).
So step 2 also extracts WAD.WAD and slices each overlay out of it (see OVERLAYS); their load bases
live in game/recomp_seeds.json. SCUS_942.28 is the boot target named in SYSTEM.CNF; S0/ and
PETEXA*.STR are the bundled Crash demo and are not touched.

Usage: python3 tools/ensure_recomp.py [/path/to/disc.chd]
Env:   PSXPORT_SPYRO_DISC (disc path), PSXPORT_DISCDUMP (discdump binary override),
       PSXPORT_FORCE_RECOMP=1 (ignore the hash and always re-emit).
Exit:  0 on success, non-zero with a diagnostic on any failure.
"""
import hashlib
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Recompiler module sources — a change to any of these changes the emitted C, so they are hash inputs.
# WHICH framework checkout the recompiler comes from is the same decision CMake already makes: PSXPORT_DIR
# selects it and defaults to the vendored submodule, so a bare clone still provisions standalone. Hardcoding
# the submodule meant the substrate could ONLY be regenerated from the recorded pin, so in-progress
# framework work on the recompiler was unverifiable end-to-end — and worse, this tool would hash a DIFFERENT
# emit.py than the one being edited and report "up to date". That cost two false "up to date" results in
# Tomba2Engine (fixed there 2026-08-12); this is the same latent defect in this repo.
PSXPORT_DIR = os.environ.get("PSXPORT_DIR", "external/psxport")
RECOMP_DIR = f"{PSXPORT_DIR}/tools/recomp"
RECOMP_SRCS = [f"{RECOMP_DIR}/emit.py", f"{RECOMP_DIR}/decode.py", f"{RECOMP_DIR}/psexe.py"]

# Our own recompiler seeds (emit.py --seeds): addresses discovery cannot see. A game fact, so we own
# it — the framework ships none. It is a recomp INPUT, hence hashed alongside the recompiler sources.
SEEDS = "game/recomp_seeds.json"

EXE_DISC_PATH = "SCUS_942.28"          # its path on the disc (root dir); also the SYSTEM.CNF BOOT target

# Overlays. Spyro's are not separate disc files: they live INSIDE WAD.WAD and are loaded into the
# heap (docs/issues/0001). Each entry is (name, byte offset into WAD.WAD, length); the matching load
# base lives in game/recomp_seeds.json under overlay_bases, because a base is what keys a module's
# addresses and must never be guessed.
WAD_DISC_PATH = "WAD.WAD"
WAD = "scratch/wad/WAD.WAD"
OVL_DIR = "scratch/bin/overlays"
# (tag, byte offset into WAD.WAD, length). Every entry needs a load base in game/recomp_seeds.json.
#
# NOT HAND-MAINTAINED. Spyro's overlays are byte ranges inside WAD.WAD that the game streams into one
# shared arena, and nothing in the executable enumerates them — the offset arrives in a register at
# the call site. So the authority is a RUN: tools/overlay_scan.py reads the `cdq` log's arena loads
# and writes game/overlays.json, which is what this reads. Each run reaches only as far as the port
# currently gets, so the set grows as the port does; overlay_scan merges rather than replaces.
OVERLAYS_JSON = "game/overlays.json"



EXE = "scratch/bin/spyro/SCUS_942.28"
GEN_DIR = "generated"
GEN_MAIN = "generated/spyro_rec.c"
HASH_FILE = "generated/.recomp.hash"
VERSION_FILE = "generated/.recomp_version"


def load_overlays():
    """Read the observed overlay set. Falls back to empty (not to a guess) if the file is absent —
    a missing set means "nobody has run overlay_scan yet", and emitting a guessed one would put a
    whole module at a wrong offset, which is unrecoverable-looking garbage rather than an error."""
    if not os.path.exists(OVERLAYS_JSON):
        say(f"no {OVERLAYS_JSON} — run tools/overlay_scan.py --run 40 to record which overlays the "
            f"port actually loads. Proceeding with none.")
        return []
    with open(OVERLAYS_JSON) as f:
        data = json.load(f)
    global OVERLAY_ENTRIES
    OVERLAY_ENTRIES = {e["name"]: e.get("entry_seeds", []) for e in data["overlays"]}
    return [(e["name"], int(e["wad_offset"], 16), int(e["length"])) for e in data["overlays"]]


OVERLAYS = None          # filled by main() once load_overlays() can report through say()
OVERLAY_ENTRIES = {}     # name -> derived per-frame entry seeds (from game/overlays.json)


def say(msg):
    sys.stderr.write(f"\033[1;36m[ensure-recomp]\033[0m {msg}\n")


def die(msg):
    sys.stderr.write(f"\033[1;31m[ensure-recomp] error:\033[0m {msg}\n")
    sys.exit(1)


def recomp_version():
    """The RECOMP_VERSION constant in psxport's emit.py (read textually so we don't import the whole
    recompiler for one string). The explicit, machine-independent staleness knob: bumping it forces
    every box to regenerate, catching a stale-but-self-consistent generated/ that a content hash
    alone would miss."""
    src = open(os.path.join(ROOT, f"{RECOMP_DIR}/emit.py")).read()
    m = re.search(r'^RECOMP_VERSION\s*=\s*"([^"]+)"', src, re.M)
    if not m:
        die("could not read RECOMP_VERSION from psxport's emit.py")
    return m.group(1)


def resolve_disc(argv):
    """CLI arg > $PSXPORT_SPYRO_DISC > .env (PSXPORT_SPYRO_DISC|PSXPORT_DISC) > *.chd drop-in."""
    disc = argv[1] if len(argv) > 1 and argv[1] else os.environ.get("PSXPORT_SPYRO_DISC", "")
    if not disc and os.path.isfile(os.path.join(ROOT, ".env")):
        env = open(os.path.join(ROOT, ".env")).read()
        for key in ("PSXPORT_SPYRO_DISC", "PSXPORT_DISC"):
            m = re.search(rf"^\s*{key}\s*=\s*(.+?)\s*$", env, re.M)
            if m:
                disc = m.group(1)
                break
    if not disc:
        chds = sorted(p for p in os.listdir(ROOT) if p.lower().endswith(".chd"))
        if chds:
            disc = os.path.join(ROOT, chds[0])
    if not disc or not os.path.isfile(disc):
        die("no disc image — pass it as ./run.sh <disc.chd>, set PSXPORT_SPYRO_DISC, or drop a *.chd here")
    return disc


def find_discdump():
    cand = os.environ.get("PSXPORT_DISCDUMP", "")
    if cand and os.access(cand, os.X_OK):
        return cand
    for p in ("external/psxport/build/tools/discdump", "external/psxport/build/tools/discdump.exe",
              "build/tools/discdump", "build/tools/discdump.exe"):
        full = os.path.join(ROOT, p)
        if os.access(full, os.X_OK):
            return full
    die("discdump not built — run.sh builds it before calling ensure_recomp.py "
        "(cmake --build external/psxport/build --target discdump)")


def disc_tree(discdump, disc):
    """`discdump list <disc>` — the ISO9660 file tree, for diagnosing a failed extraction."""
    r = subprocess.run([discdump, "list", disc], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return (r.stdout or b"").decode(errors="replace")


def extract_exe(discdump, disc):
    """Pull SCUS_942.28 off the disc if not already present. Never swallows the diagnostic — a failed
    extraction is a build-breaker, and the disc tree tells us whether the name/path differs (e.g. a
    non-USA release with a different SCUS/SCES id)."""
    out = os.path.join(ROOT, EXE)
    if os.path.isfile(out):
        return out
    os.makedirs(os.path.dirname(out), exist_ok=True)
    r = subprocess.run([discdump, "get", EXE_DISC_PATH, disc, os.path.dirname(out)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    if r.returncode != 0 or not os.path.isfile(out):
        err = (r.stderr or b"").decode(errors="replace").strip()
        die(f"could not extract {EXE_DISC_PATH} from {disc}\n"
            f"  discdump: {err or '(no message)'}\n"
            f"  This port targets Spyro the Dragon (USA), whose SYSTEM.CNF boots cdrom:\\SCUS_942.28.\n"
            f"  Disc tree (discdump list) follows — check the executable's real name:\n" + disc_tree(discdump, disc))
    return out


def extract_overlays(discdump, disc):
    """Slice each overlay out of WAD.WAD. The archive is extracted once and cached (110 MB), then
    sliced — discdump works in whole files, and re-pulling it every build would be wasteful."""
    if not OVERLAYS:
        return []
    wad = os.path.join(ROOT, WAD)
    if not os.path.isfile(wad):
        os.makedirs(os.path.dirname(wad), exist_ok=True)
        say(f"extracting {WAD_DISC_PATH} (large, one-time)…")
        r = subprocess.run([discdump, "get", WAD_DISC_PATH, disc, os.path.dirname(wad)],
                           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        if r.returncode != 0 or not os.path.isfile(wad):
            die(f"could not extract {WAD_DISC_PATH}: {(r.stderr or b'').decode(errors='replace').strip()}")
    os.makedirs(os.path.join(ROOT, OVL_DIR), exist_ok=True)
    # DELETE SLICES THAT ARE NO LONGER IN THE SET. emit.py walks this DIRECTORY, not our list, so a
    # leftover .BIN from an earlier set is recompiled as if it were current — and since the overlays
    # all share one base, a stale slice emits a whole module of wrong addresses at the live arena.
    # This is not hypothetical: renaming the set (index-based -> offset-based) left the old files
    # behind and emit.py picked up both.
    want = {name + ".BIN" for name, _, _ in OVERLAYS}
    for fn in sorted(os.listdir(os.path.join(ROOT, OVL_DIR))):
        if fn.upper().endswith(".BIN") and fn not in want:
            os.remove(os.path.join(ROOT, OVL_DIR, fn))
            say(f"removed stale overlay slice {fn} (no longer in {OVERLAYS_JSON})")
    out = []
    with open(wad, "rb") as f:
        for name, off, length in OVERLAYS:
            dst = os.path.join(ROOT, OVL_DIR, name + ".BIN")
            if not os.path.isfile(dst) or os.path.getsize(dst) != length:
                f.seek(off)
                data = f.read(length)
                if len(data) != length:
                    die(f"{name}: wanted {length} bytes at WAD+0x{off:X}, got {len(data)}")
                open(dst, "wb").write(data)
            out.append(dst)
    say(f"{len(out)} overlay(s) ready in {OVL_DIR}")
    return out


MERGED_SEEDS = "generated/.recomp_seeds_merged.json"


def merged_seeds():
    """game/recomp_seeds.json + the per-overlay entry seeds DERIVED in game/overlays.json.

    Kept as two sources on purpose. recomp_seeds.json holds seeds someone REASONED about, each with
    the rationale that makes it reviewable a year later; the entry seeds are mechanically derived from
    main's own [0x80075734] install table and re-derived on every run, so hand-copying them in would
    mix a generated list into a file whose whole value is that every line was justified — and would
    reintroduce the per-overlay hand-edit this replaced.

    The merge is written to generated/ (gitignored, regenerated) so what emit.py consumed is always
    inspectable after the fact."""
    with open(os.path.join(ROOT, SEEDS)) as f:
        text = f.read()
    body = re.sub(r"^\s*//.*$", "", text, flags=re.M)      # the seed file allows // comments
    data = json.loads(body)
    ov_seeds = dict(data.get("overlay_seeds", {}))
    derived = 0
    for name, _off, _len in OVERLAYS:
        entries = OVERLAY_ENTRIES.get(name, [])
        if not entries:
            continue
        merged = sorted(set(ov_seeds.get(name, [])) | set(entries))
        ov_seeds[name] = merged
        derived += len(entries)
    data["overlay_seeds"] = ov_seeds
    out = os.path.join(ROOT, MERGED_SEEDS)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        json.dump(data, f, indent=2)
    if derived:
        say(f"{derived} derived overlay entry seed(s) merged with {SEEDS} -> {MERGED_SEEDS}")
    return out


def input_hash():
    """SHA-256 over the executable + the recompiler sources + our seed file."""
    h = hashlib.sha256()

    def feed(label, path):
        h.update(label.encode())
        with open(path, "rb") as f:
            h.update(f.read())

    feed("SCUS_942.28", os.path.join(ROOT, EXE))
    for name, off, length in OVERLAYS:      # an overlay's bytes change the emitted C
        p = os.path.join(ROOT, OVL_DIR, name + ".BIN")
        if os.path.isfile(p):
            feed("OVL:" + name, p)
    for src in RECOMP_SRCS + [SEEDS]:
        feed(src, os.path.join(ROOT, src))
    # The DERIVED entry seeds change the emitted C too, so they belong in the identity. Without this a
    # newly-derived entry would leave generated/ looking current and the seed would silently not apply.
    h.update(b"overlay_entries")
    h.update(json.dumps(OVERLAY_ENTRIES, sort_keys=True).encode())
    return h.hexdigest()


def generated_complete():
    """Complete iff the manifest exists and every TU it lists is present."""
    manifest = os.path.join(ROOT, GEN_DIR, "rec_sources.cmake")
    for f in (manifest, os.path.join(ROOT, GEN_DIR, "shard_disp.c"),
              os.path.join(ROOT, GEN_DIR, "overlay_table.c")):
        if not os.path.isfile(f):
            return False
    listed = re.findall(r"^\s*(\S+\.c)\s*$", open(manifest).read(), re.M)
    return all(os.path.isfile(os.path.join(ROOT, GEN_DIR, tu)) for tu in listed)


def run_emit():
    seeds_path = merged_seeds()
    say("recompiling SCUS_942.28 -> C (the execution substrate)…")
    cmd = [sys.executable, os.path.join(ROOT, f"{RECOMP_DIR}/emit.py"),
           os.path.join(ROOT, EXE), os.path.join(ROOT, GEN_MAIN),
           "--seeds", seeds_path]
    if OVERLAYS:
        cmd += ["--overlays", os.path.join(ROOT, OVL_DIR)]
    if subprocess.run(cmd).returncode != 0:
        die("emit.py failed")


def main():
    global OVERLAYS
    OVERLAYS = load_overlays()
    disc = resolve_disc(sys.argv)
    say(f"disc: {disc}")
    discdump = find_discdump()
    extract_exe(discdump, disc)
    extract_overlays(discdump, disc)

    os.makedirs(os.path.join(ROOT, GEN_DIR), exist_ok=True)
    version = recomp_version()
    want = version + ":" + input_hash()
    have = ""
    if os.path.isfile(os.path.join(ROOT, HASH_FILE)):
        have = open(os.path.join(ROOT, HASH_FILE)).read().strip()
    stamp = ""
    if os.path.isfile(os.path.join(ROOT, VERSION_FILE)):
        stamp = open(os.path.join(ROOT, VERSION_FILE)).read().strip()
    force = os.environ.get("PSXPORT_FORCE_RECOMP", "") not in ("", "0")

    if not force and have == want and stamp == version and generated_complete():
        say(f"recomp up to date (version {version}) — nothing to do")
        return

    if force:
        say("PSXPORT_FORCE_RECOMP set — re-emitting")
    elif stamp and stamp != version:
        say(f"recomp version changed ({stamp} -> {version}) — re-emitting")
    elif have and have != want:
        say("inputs changed — re-emitting")
    elif not have or not stamp:
        say(f"no recorded recomp identity — emitting (version {version})")
    else:
        say("generated set incomplete — re-emitting")

    run_emit()
    if not generated_complete():
        die("emit.py ran but the generated set is still incomplete")
    new_stamp = ""
    if os.path.isfile(os.path.join(ROOT, VERSION_FILE)):
        new_stamp = open(os.path.join(ROOT, VERSION_FILE)).read().strip()
    if new_stamp != version:
        die(f"emit.py stamped version {new_stamp!r} but expected {version!r} — RECOMP_VERSION out of sync")
    open(os.path.join(ROOT, HASH_FILE), "w").write(want + "\n")
    say(f"recomp current (version {version})")


if __name__ == "__main__":
    main()
