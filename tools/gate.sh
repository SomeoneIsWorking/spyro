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
PSXPORT_DEBUG=cdq PSXPORT_GPU_DUMP="$OUT/frames" PSXPORT_VK_HEADLESS=1 PSXPORT_NOAUDIO=1 \
  PSXPORT_WATCHDOG=0 PSXPORT_ASSET_DIR=external/psxport PSXPORT_SPYRO_DISC="$DISC" \
  timeout -s KILL "$SECS" ./scratch/bin/spyro_port scratch/bin/spyro/SCUS_942.28 > "$LOG" 2>&1

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
LOADS=$(grep -c 'loader:' "$LOG" 2>/dev/null; true)
MOVED=$(grep -o 'moved [0-9]* bytes' "$LOG" 2>/dev/null | awk '{s+=$2} END{print s+0}')
COMPL=$(grep -c 'delivered CD completion' "$LOG" 2>/dev/null; true)
MISS=$(grep -c 'recomp-MISS' "$LOG" 2>/dev/null; true)
REFUSED=$(grep -c 'REFUSED' "$LOG" 2>/dev/null; true)

echo "[gate] checks:"
chk "frames presented"          "$FRAMES"   ge 300
chk "distinct frame occupancies" "$DISTINCT" ge 8      # >2 means content moves, not a held screen
chk "CD loader invocations"      "$LOADS"    ge 3
chk "bytes loaded from disc"     "$MOVED"    ge 100000
chk "CD completions delivered"   "$COMPL"    ge 3
chk "recomp misses"              "$MISS"     eq 0
chk "refused HLE registrations"  "$REFUSED"  eq 0

if [ "$fail" -eq 0 ]; then echo "[gate] PASS"; else echo "[gate] FAIL — see $LOG"; fi
exit "$fail"
