#!/usr/bin/env bash
# gate.sh — the port's regression gate.
#
# WHY THIS EXISTS. Overrides are installed by hand and justified by a log line read by hand. That is
# not a gate: nothing mechanical would notice if one stopped firing, and hand-reading has repeatedly
# been the weak link here.
#
# HOW MUCH THIS PORT ACTUALLY OWNS, stated plainly because the old text overstated it: 20 of the 21
# installed overrides SUPER-CALL the recompiled body — they observe, and the guest code still does the
# work. The CD and pad ones are platform-level SUPPLY (they provide what the hardware would have, then
# run the guest body). Genuinely native bodies: the vblank wait, and rand() 0x8006272C. See C075.
#
# WHAT THIS IS, STATED HONESTLY. This is a BOOT-PROGRESS gate, not the byte-exact SBS differential the
# porting playbook asks for. It cannot prove the native path matches the substrate instruction for
# instruction. What it CAN do is fail loudly when something that was working stops:
#   * an override silently not installing (the "hollow gate" the playbook warns about — both paths
#     run substrate and everything still looks fine),
#   * the CD path regressing to zero bytes moved,
#   * the boot regressing to the held-splash state,
#   * a new recomp-MISS or a refused HLE registration appearing.
# What it now ALSO does is byte-exact: every natively-owned body is re-verified against the recompiled
# body it replaced, on its first 8 calls, every run (PSXPORT_NDIFF). That is the real gate on native
# ownership. A whole-run SBS differential is still worth having for cross-function drift, but it is no
# longer the prerequisite for owning a function.
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
# BUILD FIRST, and refuse to run on a stale binary. Twice in one session an upstream psxport rebase
# grew GameConfig, this port's initializer broke, and the gate happily ran the PREVIOUS binary and
# reported its numbers — once into a commit message. A gate that measures a build that does not exist
# is worse than no gate, and "I ran cmake and read the error COUNT" is how it happened both times.
if ! cmake --build build --target spyro_port -j"$(nproc)" > "$OUT/build.log" 2>&1; then
  echo "gate: BUILD FAILED — refusing to measure a stale binary. Last errors:"
  grep -iE "error" "$OUT/build.log" | head -5 | sed -n 's/^/    /p'
  exit 2
fi
[ -x scratch/bin/spyro_port ] || { echo "gate: no binary after a successful build?"; exit 2; }
# Pin the binary we are about to measure. The gate takes minutes and is naturally run in the
# background, so a parallel rebuild can replace scratch/bin/spyro_port MID-RUN — and then every
# number below describes a mixture of two builds with nothing saying so. That happened (issue 0026):
# an upstream rebase landed, the port was rebuilt, and the in-flight gate had to be killed rather
# than quoted. Same failure class as the exit-status bug above — measuring something other than what
# the reader thinks. Refuse to report rather than block: a lock would stop legitimate parallel work,
# whereas this only stops a false conclusion being drawn from it.
BIN_ID_BEFORE=$(stat -c '%s:%Y' scratch/bin/spyro_port)

echo "[gate] running ${SECS}s headless…"
PSXPORT_DEBUG=cdq,ovload,gpu,ndiff PSXPORT_GPU_DUMP="$OUT/frames:5" PSXPORT_VK_HEADLESS=1 PSXPORT_NOAUDIO=1 \
  PSXPORT_NDIFF=8 \
  PSXPORT_WATCHDOG=15 PSXPORT_ASSET_DIR=external/psxport PSXPORT_SPYRO_DISC="$DISC" \
  timeout -s KILL "$SECS" ./scratch/bin/spyro_port scratch/bin/spyro/SCUS_942.28 > "$LOG" 2>&1
RC=$?
BIN_ID_AFTER=$(stat -c '%s:%Y' scratch/bin/spyro_port 2>/dev/null || echo gone)
if [ "$BIN_ID_AFTER" != "$BIN_ID_BEFORE" ]; then
  echo "gate: THE BINARY WAS REPLACED MID-RUN ($BIN_ID_BEFORE -> $BIN_ID_AFTER)."
  echo "      Something rebuilt scratch/bin/spyro_port while this gate was measuring it, so these"
  echo "      numbers would describe a mixture of two builds. Refusing to report — re-run the gate"
  echo "      once the other build has settled. See docs/issues/0026."
  exit 2
fi
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

# FRAMES comes from the LOG, not from counting dumped files, because the dump is now SAMPLED
# (PSXPORT_GPU_DUMP=dir:20). Counting files would divide the real number by the sample interval and
# silently move the threshold.
#
# Sampling was not just about the gate's own runtime: writing a PPM per frame cost the PORT roughly
# half its speed. Measured over the same 40s run:
#     every frame  19003 frames  6.6 GB  21 distinct occupancies
#     every 5      40186 frames  2.8 GB  11
#     every 20     54151 frames  963 MB   7   <- FAILS the >=8 occupancy check
# :5 is the choice because it clears the EXISTING threshold with margin. :20 is faster still but
# misses short-lived states, and lowering the threshold to suit it would weaken a check that exists to
# tell "the boot advances through content" from "one screen is being re-presented" — it read 218
# frames / almost no distinct occupancies for a held splash.
# Any frames-presented figure recorded BEFORE this change was measured on a port carrying the full
# per-frame dump and is NOT comparable with one after it.
FRAMES=$(grep -oE '^\[gpu\] frame [0-9]+' "$LOG" 2>/dev/null | awk '{print $3}' | tail -1)
FRAMES=${FRAMES:-0}
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
# Is the guest still SUBMITTING geometry late in the run? This replaces an earlier check that counted
# non-black pixels in the last quarter of the PPM dump, which was measuring the wrong buffer:
# PSXPORT_GPU_DUMP reads s_vram, and under vk_path() every polygon goes to the VK rasterizer and never
# touches s_vram. Only uploads/fills land there. So a PPM dump shows a game's upload-based front-end
# and then reads as PERMANENTLY BLACK the moment real geometry rendering starts — which is exactly
# backwards as a health signal (instrument I008). Prim counts come from the guest's submissions and do
# not care which backend draws them.
LATEPRIMS=$(grep -oE 'frame [0-9]+: [0-9]+ prims' "$LOG" 2>/dev/null | awk '{split($2,a,":"); f=a[1]; p=$3; if (f>mx) mx=f; n++; fr[n]=f; pr[n]=p} END{c=0; for(i=1;i<=n;i++) if (fr[i] > mx*0.75 && pr[i] > 0) c++; print c+0}')
LOADS=$(grep -c 'loader:' "$LOG" 2>/dev/null; true)
MOVED=$(grep -o 'moved [0-9]* bytes' "$LOG" 2>/dev/null | awk '{s+=$2} END{print s+0}')
COMPL=$(grep -c 'delivered CD completion' "$LOG" 2>/dev/null; true)
MISS=$(grep -c 'recomp-MISS' "$LOG" 2>/dev/null; true)
REFUSED=$(grep -c 'REFUSED' "$LOG" 2>/dev/null; true)
# The overlay router must IDENTIFY the overlay resident in the arena slot, not merely see a load.
# Without GameConfig::overlaySlots[0] the slot lookup returns -1 and no identity is ever recorded —
# yet everything still boots, because dispatch falls back to a full signature scan. That is precisely
# the "hollow" failure this gate exists to catch, so assert on the named match, not on the load.
#
# Count DISTINCT overlays identified, by name pattern rather than one hardcoded name. The previous
# form grepped for the literal "OVL0" and so silently reported 0 the moment the overlays were renamed
# (they are now OV_<wad-offset>, see tools/overlay_scan.py) — a check pinned to one name tests the
# name, not the router.
OVID=$(grep -oE 'ovload.*slot 0 <- [A-Za-z0-9_]+' "$LOG" 2>/dev/null | awk '{print $NF}' | sort -u | wc -l; true)
# THE COMPLEMENT, AND THE MORE USEFUL SIGNAL: an arena load the router could NOT match to any known
# overlay. That is what "the game loads an overlay we have not extracted" looks like, and it is the
# thing that precedes a fail-fast inside it. The old check could not see it at all — it only counted
# one overlay's successes, so a run could identify OVL0 and then load three unknown overlays and still
# pass. Zero is the requirement; when it trips, run tools/overlay_scan.py to pick up the new ones.
OVUNMATCHED=$(grep -c 'ovload.*none/unmatched' "$LOG" 2>/dev/null; true)
NDIFF=$(grep -c 'DIVERGES from the recompiled body' "$LOG" 2>/dev/null; true)
# THE FRAME-PROGRESS WATCHDOG, which this gate spent its life with switched OFF. That is how a port
# that wedged at frame 4531 and never presented again PASSED: `frames presented >= 300` counts frames
# from before the stall, and every other check here is a log/frame total that a wedged run still
# satisfies. The stall was found by hand, days later (issue 0034).
#
# 15s, not the 3s default: this runs headless under whatever else the machine is doing, and a slow
# frame under load is not a hang. A real wedge is infinite, so no honest run comes close to 15s.
STUCK=$(grep -c 'watchdog. STUCK' "$LOG" 2>/dev/null; true)
# Count the PASSING verifications too. "0 divergences" is also what a run with no native bodies at
# all reports, and those two states must not look identical — that is the hollow-gate trap this file
# already warns about, in a new place.
NDIFFOK=$(grep -c 'matches the recompiled body exactly' "$LOG" 2>/dev/null; true)

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
chk "frames submitting prims (last 25%)" "$LATEPRIMS" ge 1
chk "CD loader invocations"      "$LOADS"    ge 3
chk "bytes loaded from disc"     "$MOVED"    ge 100000
chk "CD completions delivered"   "$COMPL"    ge 3
chk "recomp misses"              "$MISS"     eq 0
chk "frame-progress watchdog trips" "$STUCK"  eq 0
if [ "${STUCK:-0}" -gt 0 ]; then
  echo "        the port stopped presenting frames — top of the stuck stack:"
  grep -A6 'watchdog. STUCK' "$LOG" | grep -oE 'gen_func_[0-9A-F]+' | head -4 | sed -n 's/^/          /p'
fi
# NATIVE BODIES MUST STILL MATCH THE SUBSTRATE. Every function this port OWNS is re-verified against
# the recompiled body it replaced, on its first 8 calls, every gate run (PSXPORT_NDIFF=8 above). This
# is the regression guard for the whole native-ownership programme: a replacement that drifts — or a
# recompiler change that alters the body underneath it — fails here instead of becoming a subtle
# behavioural difference nobody attributes to the port. Zero is the only acceptable number.
chk "native/substrate divergences" "$NDIFF"   eq 0
chk "native bodies verified"       "$NDIFFOK" ge 1
chk "refused HLE registrations"  "$REFUSED"  eq 0
chk "distinct overlays identified" "$OVID"        ge 1
chk "arena loads UNMATCHED"        "$OVUNMATCHED" eq 0

# Ledger self-consistency. Not a runtime property, but this is the one thing that runs every
# iteration, and a contradictory ledger (a refutation recorded without flipping the claim it kills)
# is silently served as fact by every later `info.py brief`. Cheap, so it rides along here.
# SELF-CONSISTENCY IS THE PASS/FAIL AXIS; STALENESS IS A REPORTED BACKLOG. They are separated
# deliberately, and `--no-stale` here is not a check being switched off — the staleness numbers are
# printed unconditionally two lines down, WITH their blind-spot denominator. The reason they are not
# fatal is that the stale set is a re-verification backlog accumulated over the project's life (47 of
# 76 checkable claims when this split was made), not a property of the run being gated; a gate that
# is red every single time for reasons the run did not cause is a gate people stop reading.
#
# The old form of this block also LIED: it counted only INCONSISTENT|'NO FALSIFIER' for its FAIL
# message, so once `info.py check` grew staleness it printed "FAIL ... 0 problem(s)" and then listed
# nothing at all. A failure that names no cause is worse than no check, so the message below prints
# the actual failing lines.
if ! python3 tools/info.py check --no-stale > "$OUT/info.txt" 2>&1; then
  printf '  \033[31mFAIL\033[0m %-34s %s\n' "info ledger self-consistent" \
    "$(grep -cE 'INCONSISTENT|NO FALSIFIER|DISTRUSTED INSTRUMENT' "$OUT/info.txt"; true) problem(s)"
  sed -n 's/^/        /p' "$OUT/info.txt" | grep -E 'INCONSISTENT|NO FALSIFIER|DISTRUSTED INSTRUMENT'
  fail=1
else
  printf '  \033[32mPASS\033[0m %-34s %s\n' "info ledger self-consistent" "ok"
fi
# Claim ROT, reported every run and never silent. C099 read as a current description of the renderer
# for a week after the code under it was fixed, and nearly bought a whole native producer as a
# workaround for a one-line regression. The CANNOT-SEE count rides along because "0 stale" says
# nothing about claims that record no code dependency at all — they are UNCHECKED, not fresh.
python3 tools/info.py claim check > "$OUT/stale.txt" 2>&1 || true
num() { sed -n "s/^ *$1[ .]*\([0-9][0-9]*\).*/\1/p" "$OUT/stale.txt" | head -1; }
printf '  \033[33mNOTE\033[0m %-34s %s\n' "claim staleness (not fatal)" \
  "$(num 'stale (code moved)') stale / $(num CHECKED) checked, $(num '\*\*\* CANNOT SEE') UNCHECKED (no code dependency recorded)"

# Open 'blocker' entries, reported only when the gate PASSED. A blocker asserts the port cannot get
# past it; a passing gate says otherwise, so each one is a contradiction — either it no longer blocks
# or the gate does not cover it. WARN, never FAIL: the resolution is a human judgement and breaking
# the build on it would just teach everyone to ignore the line. This exists because issue #10 stayed
# open long after its work shipped, ranked top for its topic, and sent a session to re-derive a
# contract that was already written down.
if [ "$fail" -eq 0 ]; then
  STALE="$(python3 tools/catalog.py stale --count 2>/dev/null || echo 0)"
  if [ "${STALE:-0}" -gt 0 ]; then
    printf '  \033[33mWARN\033[0m %-34s %s\n' "open 'blocker' issues" \
      "$STALE — gate passes, so each no longer blocks or is uncovered (tools/catalog.py stale)"
  else
    printf '  \033[32mPASS\033[0m %-34s %s\n' "open 'blocker' issues" "none"
  fi
fi

if [ "$fail" -eq 0 ]; then echo "[gate] PASS"; else echo "[gate] FAIL — see $LOG"; fi
exit "$fail"
