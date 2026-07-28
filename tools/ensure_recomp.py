#!/usr/bin/env python3
"""ensure_recomp.py — the single, hash-checked recompilation step.

ONE entry point that guarantees the statically-recompiled substrate in generated/ is PRESENT and
matches a deterministic hash of its INPUTS. run.sh calls only this; all recomp provisioning lives
here rather than scattered through the shell script.

What it does, in order:
  1. Resolve the disc image (CLI arg > $PSXPORT_SPYRO_DISC > .env > *.chd drop-in — mirrors run.sh).
  2. Extract SCUS_942.28 from the disc via psxport's `discdump`.
  3. Compute the recomp IDENTITY = emit.py's RECOMP_VERSION + a hash of the INPUTS (the executable +
     the recompiler module sources + OUR SEED FILE). If the stored identity matches, the on-disk
     version stamp matches, and the generated set is complete, do nothing. Otherwise re-run emit.py.

Spyro is structurally simpler than psxport's reference consumer (Tomba!2): the disc holds a SINGLE
executable and no \\BIN\\*.BIN code overlays, so there is no overlay extraction step and no boot stub
to recompile separately. SCUS_942.28 is both the boot target named in SYSTEM.CNF and the whole game;
everything else on the disc is data (WAD.WAD) or the bundled Crash demo (S0/, PETEXA*.STR), neither
of which the recompiler touches.

Usage: python3 tools/ensure_recomp.py [/path/to/disc.chd]
Env:   PSXPORT_SPYRO_DISC (disc path), PSXPORT_DISCDUMP (discdump binary override),
       PSXPORT_FORCE_RECOMP=1 (ignore the hash and always re-emit).
Exit:  0 on success, non-zero with a diagnostic on any failure.
"""
import hashlib
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Recompiler module sources — a change to any of these changes the emitted C, so they are hash inputs.
RECOMP_DIR = "external/psxport/tools/recomp"
RECOMP_SRCS = [f"{RECOMP_DIR}/emit.py", f"{RECOMP_DIR}/decode.py", f"{RECOMP_DIR}/psexe.py"]

# Our own recompiler seeds (emit.py --seeds): addresses discovery cannot see. A game fact, so we own
# it — the framework ships none. It is a recomp INPUT, hence hashed alongside the recompiler sources.
SEEDS = "game/recomp_seeds.json"

EXE_DISC_PATH = "SCUS_942.28"          # its path on the disc (root dir); also the SYSTEM.CNF BOOT target
EXE = "scratch/bin/spyro/SCUS_942.28"
GEN_DIR = "generated"
GEN_MAIN = "generated/spyro_rec.c"
HASH_FILE = "generated/.recomp.hash"
VERSION_FILE = "generated/.recomp_version"


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


def input_hash():
    """SHA-256 over the executable + the recompiler sources + our seed file."""
    h = hashlib.sha256()

    def feed(label, path):
        h.update(label.encode())
        with open(path, "rb") as f:
            h.update(f.read())

    feed("SCUS_942.28", os.path.join(ROOT, EXE))
    for src in RECOMP_SRCS + [SEEDS]:
        feed(src, os.path.join(ROOT, src))
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
    say("recompiling SCUS_942.28 -> C (the execution substrate)…")
    cmd = [sys.executable, os.path.join(ROOT, f"{RECOMP_DIR}/emit.py"),
           os.path.join(ROOT, EXE), os.path.join(ROOT, GEN_MAIN),
           "--seeds", os.path.join(ROOT, SEEDS)]
    if subprocess.run(cmd).returncode != 0:
        die("emit.py failed")


def main():
    disc = resolve_disc(sys.argv)
    say(f"disc: {disc}")
    discdump = find_discdump()
    extract_exe(discdump, disc)

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
