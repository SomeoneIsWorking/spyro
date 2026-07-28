#!/usr/bin/env bash
# gate.sh — the port's regression gate.
#
# WHY THIS EXISTS. Six guest functions are now owned natively (the CD loader, the vblank wait, the CD
# completion delivery, the BIOS event delivery, plus observation overrides). Every one of them was
# justified by a log line read by hand. That is not a gate: nothing mechanical would notice if one
# stopped firing, and this session has repeatedly shown hand-reading to be the weak link.
#
# WHAT THIS IS, STATED HONESTLY. This is a BOOT-PROGRESS gate, not the byte-exact SBS differential the
# porting playbook asks for. It cannot prove the native path matches the substrate instruction for
# instruction. What it CAN do is fail loudly when something that was working stops:
#   * an override silently not installing (the "hollow gate" the playbook warns about — both paths
#     run substrate and everything still looks fine),
#   * the CD path regressing to zero bytes moved,
#   * the boot regressing to the held-splash state,
#   * a new recomp-MISS or a refused HLE registration appearing.
# The real SBS harness remains outstanding (frontier: harness.sbs) and this does not replace it.
#
# Usage: tools/gate.sh [seconds]        (default 40)
# Exit:  0 = all checks pass, 1 = at least one regression.
set -u
cd "$(dirname "$0")/.."

SECS="${1:-40}"
OUT=scratch/gate
rm -rf "$OUT"; mkdir -p "$OUT/frames"
LOG="$OUT/run.log"

DISC="${PSXPORT_SPYRO_DISC:-}"
if [ -z "$DISC" ] && [ -f .env ]; then
  DISC="$(sed -n 's/^[[:space:]]*PSXPORT_SPYRO_DISC[[:space:]]*=[[:space:]]*//p' .env | head -1)"
fi
[ -n "$DISC" ] && [ -f "$DISC" ] || { echo "gate: no disc image (set PSXPORT_SPYRO_DISC or .env)"; exit 2; }
[ -x scratch/bin/spyro_port ] || { echo "gate: build first (cmake --build build --target spyro_port)"; exit 2; }

echo "[gate] running ${SECS}s headless…"
PSXPORT_DEBUG=cdq,ovload PSXPORT_GPU_DUMP="$OUT/frames" PSXPORT_VK_HEADLESS=1 PSXPORT_NOAUDIO=1 \
  PSXPORT_WATCHDOG=0 PSXPORT_ASSET_DIR=external/psxport PSXPORT_SPYRO_DISC="$DISC" \
  timeout -s KILL "$SECS" ./scratch/bin/spyro_port scratch/bin/spyro/SCUS_942.28 > "$LOG" 2>&1
RC=$?
# THE MOST IMPORTANT CHECK, AND THE ONE THIS GATE SPENT ITS WHOLE LIFE MISSING. `timeout -s KILL`
# swallows the child's exit status, so a port that ABORTS looks exactly like one that ran the full
# duration — and every other check here is a log/frame count that a crashed run still satisfies. This
# gate reported PASS on a segfaulting port for its entire existence, and "3781 frames" was quoted as a
# progress metric in several commits when it is in fact the frame the crash happens on (identical
# under a 20s and a 70s timeout — it is not time-bound at all).
# A healthy run is one this script had to KILL: 137. Anything else is the port dying on its own.

fail=0
chk() { # chk <name> <actual> <op> <expected>
  if [ "$2" -"$3" "$4" ]; then printf '  \033[32mPASS\033[0m %-34s %s (want %s %s)\n' "$1" "$2" "$3" "$4"
  else printf '  \033[31mFAIL\033[0m %-34s %s (want %s %s)\n' "$1" "$2" "$3" "$4"; fail=1; fi
}

FRAMES=$(ls "$OUT/frames" 2>/dev/null | grep -c '\.ppm$'; true)
# Distinct frame occupancies: the difference between "the boot advances through content" and "one
# screen is being re-presented". Frame COUNT alone cannot tell those apart — it was 218 for a held
# splash — so this is the check that actually catches a regression to a stuck boot.
DISTINCT=$(python3 - "$OUT/frames" <<'PY' 2>/dev/null || echo 0
import sys, glob, os
seen=set()
for f in sorted(glob.glob(os.path.join(sys.argv[1], "*.ppm"))):
    d=open(f,'rb').read()
    i=0; t=0
    while t<4 and i<len(d):
        while i<len(d) and d[i:i+1].isspace(): i+=1
        while i<len(d) and not d[i:i+1].isspace(): i+=1
        t+=1
    seen.add(sum(1 for b in d[i+1:] if b))
print(len(seen))
PY
)
# Content in the LAST QUARTER of the run. "distinct occupancies >= 8" was satisfied entirely by the
# first 600 frames and then sat through 3346 BLACK ones without complaint — a boot that renders its
# logos and then draws nothing looked identical to a healthy run. Asserting on LATE frames is what
# distinguishes "the game is running" from "the game rendered a logo once".
LATE=$(python3 - "$OUT/frames" <<'PY2' 2>/dev/null || echo 0
import sys, glob, os
fs = sorted(glob.glob(os.path.join(sys.argv[1], "*.ppm")))
n = 0
for f in fs[int(len(fs) * 0.75):]:
    d = open(f, 'rb').read()
    i = t = 0
    while t < 4 and i < len(d):
        while i < len(d) and d[i:i+1].isspace(): i += 1
        while i < len(d) and not d[i:i+1].isspace(): i += 1
        t += 1
    if sum(1 for b in d[i+1:] if b) > 0: n += 1
print(n)
PY2
)
LOADS=$(grep -c 'loader:' "$LOG" 2>/dev/null; true)
MOVED=$(grep -o 'moved [0-9]* bytes' "$LOG" 2>/dev/null | awk '{s+=$2} END{print s+0}')
COMPL=$(grep -c 'delivered CD completion' "$LOG" 2>/dev/null; true)
MISS=$(grep -c 'recomp-MISS' "$LOG" 2>/dev/null; true)
REFUSED=$(grep -c 'REFUSED' "$LOG" 2>/dev/null; true)
# The overlay router must IDENTIFY the overlay resident in the arena slot, not merely see a load.
# Without GameConfig::overlaySlots[0] the slot lookup returns -1 and no identity is ever recorded —
# yet everything still boots, because dispatch falls back to a full signature scan. That is precisely
# the "hollow" failure this gate exists to catch, so assert on the named match, not on the load.
OVID=$(grep -c 'ovload.*slot 0 <- OVL0' "$LOG" 2>/dev/null; true)

echo "[gate] checks:"
if [ "$RC" -eq 137 ]; then
  printf '  \033[32mPASS\033[0m %-34s %s\n' "port still running at timeout" "killed by gate (rc=137)"
else
  printf '  \033[31mFAIL\033[0m %-34s %s\n' "port still running at timeout" "port exited on its own (rc=$RC)"
  grep -m1 -A2 'FATAL\|FAULT' "$LOG" | sed -n 's/^/        /p'
  fail=1
fi
chk "frames presented"          "$FRAMES"   ge 300
chk "distinct frame occupancies" "$DISTINCT" ge 8      # >2 means content moves, not a held screen
chk "frames with content (last 25%)" "$LATE"    ge 1
chk "CD loader invocations"      "$LOADS"    ge 3
chk "bytes loaded from disc"     "$MOVED"    ge 100000
chk "CD completions delivered"   "$COMPL"    ge 3
chk "recomp misses"              "$MISS"     eq 0
chk "refused HLE registrations"  "$REFUSED"  eq 0
chk "overlay identified in slot 0"  "$OVID"     ge 1

# Ledger self-consistency. Not a runtime property, but this is the one thing that runs every
# iteration, and a contradictory ledger (a refutation recorded without flipping the claim it kills)
# is silently served as fact by every later `info.py brief`. Cheap, so it rides along here.
if ! python3 tools/info.py check > "$OUT/info.txt" 2>&1; then
  printf '  \033[31mFAIL\033[0m %-34s %s\n' "info ledger self-consistent" "$(grep -c INCONSISTENT\\\|'NO FALSIFIER' "$OUT/info.txt"; true) problem(s)"
  sed -n 's/^/        /p' "$OUT/info.txt" | grep -E 'INCONSISTENT|NO FALSIFIER'
  fail=1
else
  printf '  \033[32mPASS\033[0m %-34s %s\n' "info ledger self-consistent" "ok"
fi

if [ "$fail" -eq 0 ]; then echo "[gate] PASS"; else echo "[gate] FAIL — see $LOG"; fi
exit "$fail"
