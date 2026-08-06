#!/usr/bin/env python3
"""whowrites.py — which code ACTUALLY writes this guest address, at runtime, in which frames?

THE RUNTIME COMPANION TO writers.py. That one answers "which stores EXIST" from the binary and is
blind to any store through a computed pointer. This one runs the port with a write-watchpoint and
answers "which stores RAN" — the question that has settled every hard bug in this project so far
(the level-load stall, the native-depth submitter, the packet writer).

WHY IT EXISTS AS A TOOL. The raw watchpoint output is unusable by hand and, worse, is actively
MISLEADING in two specific ways that cost real time before this wrapper existed:

  * `pc=` IS THE LAST FUNCTION ENTERED, not the storing instruction — it goes stale as soon as a
    call returns. It once attributed 6.3M stores to ratan2 when they were in ratan2's caller
    (instrument caveat I030). `ra=` is no better: for a leaf that never set it, it holds a value left
    over from something else, and has been observed pointing into a DATA table.
  * A watchpoint fires from the FIRST FRAME, so the first hits are boot-time memset traffic. Reading
    them and concluding anything about gameplay is the "wrong regime" mistake this project has made
    five separate times.

So this reports the INNERMOST gen_func_* from the host backtrace, which is the one reliable
attribution, and it makes the frame window explicit rather than leaving it to `head`.

Usage:
  whowrites.py 0x801AB764                       # one word, whole run
  whowrites.py 0x801AB700 0x801AB900            # a range
  whowrites.py 0x801AB764 --after 40000         # only hits at/after this gpu frame
  whowrites.py 0x801AB764 --after 3000 --before 5000 --secs 90

Every run is ~1-2 minutes because the port has to reach the frames you care about.
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


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("lo", help="guest address (e.g. 0x801AB764)")
    ap.add_argument("hi", nargs="?", help="end of range; defaults to lo+4 (one word)")
    ap.add_argument("--after", type=int, default=0, help="ignore hits before this gpu frame")
    ap.add_argument("--before", type=int, default=1 << 30, help="ignore hits at/after this gpu frame")
    ap.add_argument("--secs", type=int, default=110, help="how long to run the port")
    ap.add_argument("--top", type=int, default=8, help="how many writers to list")
    a = ap.parse_args()

    lo = int(a.lo, 16)
    hi = int(a.hi, 16) if a.hi else lo + 4
    disc = disc_path()
    if not disc or not os.path.exists(disc):
        sys.exit("no disc image (set PSXPORT_SPYRO_DISC or .env)")

    log = os.path.join(REPO, "scratch", "logs", "whowrites.log")
    os.makedirs(os.path.dirname(log), exist_ok=True)
    env = dict(os.environ,
               PSXPORT_DEBUG="wwatch",
               PSXPORT_WWATCH=f"{lo:08X},{hi:08X}",
               PSXPORT_WWATCH_BT="1",
               # PSXPORT_NOPACE: a write hunt, not a play session — run as fast as the host can.
               # Headless is paced like a windowed run now, so "fast" has to be ASKED for.
               PSXPORT_VK_HEADLESS="1", PSXPORT_NOAUDIO="1", PSXPORT_NOPACE="1",
               PSXPORT_WATCHDOG="0",
               PSXPORT_ASSET_DIR="external/psxport", PSXPORT_SPYRO_DISC=disc)
    print(f"watching [{lo:08X},{hi:08X}) for {a.secs}s …", file=sys.stderr)
    with open(log, "w") as f:
        subprocess.run(["timeout", "-s", "KILL", str(a.secs),
                        "./scratch/bin/spyro_port", "scratch/bin/spyro/SCUS_942.28"],
                       cwd=REPO, env=env, stdout=f, stderr=subprocess.STDOUT)

    hits, cur = [], None
    for line in open(log, errors="ignore"):
        m = re.match(r"\[wwatch\] f(\d+) .*store \[([0-9A-F]+)\]=([0-9A-F]+)", line)
        if m:
            cur = {"f": int(m.group(1)), "addr": m.group(2), "val": m.group(3), "fns": []}
            hits.append(cur)
            continue
        if cur is not None:
            g = re.search(r"gen_func_([0-9A-F]+)", line)
            if g:
                cur["fns"].append(g.group(1))

    win = [h for h in hits if a.after <= h["f"] < a.before and h["fns"]]
    print(f"\n{len(hits)} store(s) seen; {len(win)} in frames [{a.after}, "
          f"{'end' if a.before == 1 << 30 else a.before})")
    if not hits:
        print("\nNOTHING WROTE IT in this window. That is a claim about your INPUTS until checked:\n"
              "  * did the run REACH those frames? (add PSXPORT_DEBUG=gpu and look at the last frame)\n"
              "  * is the address right, and does it hold what you think in THIS regime?\n"
              "  * a store through a computed pointer IS caught here — but only if it executes.")
        return 0
    if not win:
        seen = sorted({h["f"] for h in hits})
        print(f"  (hits exist, but only in frames {seen[0]}..{seen[-1]} — widen --after/--before)")
        return 0

    inner = collections.Counter(h["fns"][0] for h in win)
    print("\ninnermost writer (from the host backtrace — NOT the misleading pc=/ra= fields):")
    for fn, n in inner.most_common(a.top):
        print(f"  {n:7d}  gen_func_{fn}")
    ex = win[-1]
    print(f"\nexample chain at frame {ex['f']}: " + " <- ".join(ex["fns"][:6]))
    print(f"  stored [{ex['addr']}] = {ex['val']}")
    print(f"\nfull log: {os.path.relpath(log, REPO)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
