#!/usr/bin/env python3
"""depth_cov.py — how much of the frame actually has native per-vertex depth?

WHY A TOOL AND NOT A grep. The ndepth channel prints two different things per sampled frame, and
reading either one alone has produced a wrong conclusion in this project:

    [ndepth fN] real-depth(3D) prims=A  OT-band(2D) prims=B  3D%=..     <- per PRIMITIVE
        projprim(vtx) records=R  lookups hit=H miss=M                   <- per VERTEX

  * The summary is SAMPLED (every 60th frame). Dumping an arbitrary frame and reading is3d=0 while
    sampled frames sit at 100% is the "wrong regime" mistake, made five times here already.
  * Reading the FIRST few lines reads boot frames, which have no 3D at all. `grep -m3` did exactly
    that and reported records=0 for a run whose level frames were recording 224/frame.
  * The per-PRIMITIVE number is all-or-nothing: is3d requires EVERY vertex of a primitive to
    resolve, so work that takes a primitive from 2/3 vertices to 3/3 shows up, and work that takes
    it from 0/3 to 2/3 is invisible. Three correct emitter fixes once looked like "no change" for
    exactly this reason. The per-VERTEX hit rate is what moves in small steps.

So this reports BOTH, over the whole run, and says which one you should be watching.

Usage:
  depth_cov.py scratch/logs/run.log            # summarise a log you already have
  depth_cov.py --run                           # run the port with the right channel, then summarise
  depth_cov.py --run --secs 90
"""
import argparse
import collections
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def disc_path():
    d = os.environ.get("PSXPORT_SPYRO_DISC", "")
    if not d:
        env = os.path.join(REPO, ".env")
        if os.path.exists(env):
            for line in open(env):
                m = re.match(r"\s*PSXPORT_SPYRO_DISC\s*=\s*(.+)", line)
                if m:
                    d = m.group(1).strip()
                    break
    return d


def run_port(secs):
    disc = disc_path()
    if not disc or not os.path.exists(disc):
        sys.exit("no disc image (set PSXPORT_SPYRO_DISC or .env)")
    log = os.path.join(REPO, "scratch", "logs", "depth_cov.log")
    os.makedirs(os.path.dirname(log), exist_ok=True)
    env = dict(os.environ, PSXPORT_DEBUG="ndepth", PSXPORT_VK_HEADLESS="1", PSXPORT_NOAUDIO="1",
               PSXPORT_WATCHDOG="0", PSXPORT_ASSET_DIR="external/psxport", PSXPORT_SPYRO_DISC=disc)
    print(f"running {secs}s …", file=sys.stderr)
    with open(log, "w") as f:
        subprocess.run(["timeout", "-s", "KILL", str(secs),
                        "./scratch/bin/spyro_port", "scratch/bin/spyro/SCUS_942.28"],
                       cwd=REPO, env=env, stdout=f, stderr=subprocess.STDOUT)
    return log


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log", nargs="?", help="a log captured with PSXPORT_DEBUG=ndepth")
    ap.add_argument("--run", action="store_true", help="run the port first")
    ap.add_argument("--secs", type=int, default=130)
    a = ap.parse_args()

    log = run_port(a.secs) if a.run else a.log
    if not log:
        sys.exit("give a log path or --run")

    frames, cur = [], None
    rec = hit = miss = 0
    for line in open(log, errors="ignore"):
        m = re.search(r"\[ndepth f(\d+)\] real-depth\(3D\) prims=(\d+)\s+OT-band\(2D\) prims=(\d+)", line)
        if m:
            cur = (int(m.group(1)), int(m.group(2)), int(m.group(3)))
            frames.append(cur)
            continue
        q = re.search(r"records=(\d+)\s+lookups hit=(\d+) miss=(\d+)", line)
        if q:
            rec += int(q.group(1)); hit += int(q.group(2)); miss += int(q.group(3))

    if not frames:
        sys.exit(f"{log}: no ndepth summaries — was PSXPORT_DEBUG=ndepth set, and did the run reach "
                 f"frames with geometry?")

    active = [f for f in frames if f[1] + f[2] > 0]
    p3 = sum(f[1] for f in active); p2 = sum(f[2] for f in active)
    band = collections.Counter(
        "100%" if f[1] and not f[2] else ("0%" if not f[1] else "partial") for f in active)

    print(f"\n{len(frames)} sampled frames, {len(active)} carrying primitives\n")
    print("PER PRIMITIVE (all-or-nothing: every vertex must resolve)")
    print(f"  real depth {p3}   OT band {p2}   ->  {100.0*p3/max(1,p3+p2):5.1f}%")
    print(f"  frames: {band.get('100%',0)} at 100%, {band.get('partial',0)} partial, "
          f"{band.get('0%',0)} at zero")
    print("\nPER VERTEX (moves in small steps — watch THIS while extending taps)")
    tot = hit + miss
    print(f"  recorded {rec}   lookups {tot}   hit {hit}   miss {miss}   ->  "
          f"{100.0*hit/max(1,tot):5.1f}% resolved")
    if rec > tot * 4 and tot:
        print(f"\n  NOTE: {rec} depths recorded against only {tot} lookups. A large excess means the")
        print( "  port is recording at addresses nothing draws from — a STAGING buffer, not the")
        print( "  packet. More taps will not help; the copy INTO the packet is what is missing.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
