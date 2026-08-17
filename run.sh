#!/usr/bin/env bash
# Fully automated build-and-run for the Spyro the Dragon native PC port (Linux + macOS).
#
#   ./run.sh [/path/to/Spyro.chd]
#
# End to end: builds the CHD tooling (libchdr + discdump), extracts SCUS_942.28 from your disc,
# statically recompiles it to C, builds the native port, and launches it. The disc image is yours to
# provide and is never shipped — pass it as an argument, set PSXPORT_SPYRO_DISC, put it in .env, or
# drop a *.chd next to this script.
#
# Requirements (install once):
#   Linux:  cmake pkg-config SDL3-devel libzstd-devel zlib-devel python3 + a C/C++ toolchain
#   macOS:  brew install cmake pkg-config sdl3 zstd zlib python3
#
# Env knobs: PSXPORT_NOAUDIO=1 (mute), PSXPORT_NOWINDOW=1 (headless), CC=clang/gcc,
#            PSXPORT_FORCE_RECOMP=1 (always re-emit the substrate).
#            PSXPORT_NOPACE=1 (run as fast as the host can). HEADLESS IS NOT UNPACED: headless
#            means no window surface and no audio device, nothing else, so a headless run paces
#            at the game's field rate exactly like a windowed one. A gate or tool that wants
#            frames rather than real time asks for NOPACE explicitly — it is the only switch
#            that means that.
#
# no pipefail: some steps use `cmd | head -1`, where head closing early would SIGPIPE the producer
# and (under pipefail) abort the script; results are validated explicitly instead.
set -eu
cd "$(dirname "$0")"

say() { printf '\033[1;36m[run]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[run] error:\033[0m %s\n' "$*" >&2; exit 1; }

# ---- 0. toolchain -------------------------------------------------------------------------------
command -v cmake      >/dev/null || die "cmake not found"
command -v python3    >/dev/null || die "python3 not found"
command -v pkg-config >/dev/null || die "pkg-config not found"
pkg-config --exists sdl3 || die "SDL3 not found (Linux: SDL3-devel/libsdl3-dev; macOS: brew install sdl3)"
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# ---- 0a2. WHICH FRAMEWORK CHECKOUT IS THIS RUN BUILT FROM? --------------------------------------
# Default: the pinned submodule, so `git clone && ./run.sh` works standalone. Override to build
# against the workspace's framework dev clone without touching the submodule:
#
#   PSXPORT_DIR=$HOME/repo/psx/psxport ./run.sh
#
# ANNOUNCED either way, and that is the point: a binary built from in-progress framework work must
# never be mistaken for one built from the pin. Same discipline as the render-path stamp.
# external/psxport is NOT a git submodule any more (2026-08-16): it is a symlink to the workspace's
# shared framework clone when there is one — so a framework edit is live in every port at once, which
# is the point — or a private clone at psxport.pin otherwise. Establish whichever applies before we
# look at it. tools/psxport_sync.py explains the two submodule incidents that motivated the change.
python3 tools/psxport_sync.py --auto || die "could not resolve external/psxport"
PSXPORT_DIR="${PSXPORT_DIR:-external/psxport}"
[ -f "$PSXPORT_DIR/cmake/psxport.cmake" ] || die "PSXPORT_DIR=$PSXPORT_DIR is not a psxport checkout"
if [ "$PSXPORT_DIR" = "external/psxport" ]; then
  say "framework: external/psxport -> $(readlink -f external/psxport 2>/dev/null || echo '?') @ $(git -C external/psxport rev-parse --short HEAD 2>/dev/null || echo '?')$(
        [ -n "$(git -C external/psxport status --porcelain 2>/dev/null)" ] && echo ' +dirty')"
else
  say "framework: *** $PSXPORT_DIR *** (DEV CLONE $(git -C "$PSXPORT_DIR" rev-parse --short HEAD 2>/dev/null || echo '?')$(
        [ -n "$(git -C "$PSXPORT_DIR" status --porcelain 2>/dev/null)" ] && echo ' +dirty')) — NOT the recorded pin"
fi

# ---- 0b. sync git submodules (this repo's own: external/open-spyro, external/spyro-1) --------
# A plain `git pull` does NOT update submodules, so after a pull the decomp references can be
# stale. Sync them here so `git pull && ./run.sh` is self-sufficient. The framework's own nested
# vendors (external/psxport/vendor/beetle-psx, /vendor/lucent) are the SHARED clone's concern now —
# external/psxport is a symlink to it, so this repo's sync must not walk into it.
#
# ONE implementation, shared by all three ports: external/psxport/scripts/sync-submodules.sh. The
# copy that used to live here guarded only `external/psxport` for uncommitted work and then updated
# EVERY submodule (this repo also has external/open-spyro and the nested vendor/*), and it never
# said which shas it moved — so a sync that reverted a deliberately checked-out commit looked
# exactly like a no-op. See that script's header for what that cost.
#
# Bootstrap: the script lives INSIDE the framework, so on a fresh clone it does not exist until
# external/psxport has been resolved (psxport_sync.py --auto above) — which is the prerequisite.
if command -v git >/dev/null && [ -f .gitmodules ]; then
  if [ -f external/psxport/scripts/sync-submodules.sh ]; then
    bash external/psxport/scripts/sync-submodules.sh || die "submodule sync failed"
  else
    say "WARNING: external/psxport/scripts/sync-submodules.sh is absent — this repo's own "
    say "         submodules (external/open-spyro, external/spyro-1) were NOT synced."
  fi
fi

# ---- 1. resolve the disc ------------------------------------------------------------------------
DISC="${1:-${PSXPORT_SPYRO_DISC:-}}"
if [ -z "$DISC" ] && [ -f .env ]; then
  DISC="$(sed -n 's/^[[:space:]]*PSXPORT_SPYRO_DISC[[:space:]]*=[[:space:]]*//p' .env | head -1)"
  [ -z "$DISC" ] && DISC="$(sed -n 's/^[[:space:]]*PSXPORT_DISC[[:space:]]*=[[:space:]]*//p' .env | head -1)"
fi
[ -z "$DISC" ] && DISC="$(ls ./*.chd 2>/dev/null | head -1 || true)"
[ -n "$DISC" ] && [ -f "$DISC" ] || die "no disc image — pass it as ./run.sh <disc.chd>, set PSXPORT_SPYRO_DISC, or drop a *.chd here"
say "disc: $DISC"

# ---- 2. build the CHD tooling (libchdr + discdump) ----------------------------------------------
# ALWAYS (re)build discdump: CMake is incremental (fast when current), and a STALE binary is a silent
# trap — it can fail to find a file on the disc and leave the recomp built from missing inputs.
say "building libchdr + discdump…"
cmake -S "$PSXPORT_DIR" -B "$PSXPORT_DIR/build" -DCMAKE_BUILD_TYPE=Release >/dev/null \
  || die "psxport cmake configure failed"
cmake --build "$PSXPORT_DIR/build" -j "$JOBS" --target discdump >/dev/null || die "discdump build failed"
DISCDUMP="$PSXPORT_DIR/build/tools/discdump"
[ -x "$DISCDUMP" ] || DISCDUMP="$PSXPORT_DIR/build/tools/discdump.exe"
[ -x "$DISCDUMP" ] || die "discdump build failed"

# ---- 3. ensure the recompiled substrate is present AND matches its input hash --------------------
# ONE step does all recomp provisioning: extract SCUS_942.28 and run emit.py, verifying the generated
# set matches a deterministic hash of the inputs (executable + recompiler sources + our seed file), so
# every machine builds a byte-identical substrate. See tools/ensure_recomp.py.
EXE=scratch/bin/spyro/SCUS_942.28
mkdir -p generated scratch/bin
PSXPORT_DISCDUMP="$DISCDUMP" python3 tools/ensure_recomp.py "$DISC" || die "recomp provisioning failed"
[ -f "$EXE" ] || die "ensure_recomp.py did not produce $EXE"

# ---- 4. build the native port -------------------------------------------------------------------
say "building the native port (CMake -j$JOBS)…"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPSXPORT_DIR="$(cd "$PSXPORT_DIR" && pwd)" >/dev/null \
  || die "cmake configure failed"
cmake --build build -j "$JOBS" --target spyro_port || die "port build failed"

# ---- 5. run -------------------------------------------------------------------------------------
say "launching Spyro the Dragon (native PC port)…"
# run.sh is the user's WINDOWED entry point, so it explicitly opts into a window. The binary itself is
# HEADLESS by default so agent/CI runs that forget the flag fail safe (no intrusive window).
if [ -n "${PSXPORT_NOWINDOW:-}" ]; then export PSXPORT_VK_HEADLESS=1; else export PSXPORT_VK_WINDOW=1; fi
# The framework's RmlUi debug/mod-overlay assets (fonts + menu.rml) ship with psxport and are loaded
# relative to PSXPORT_ASSET_DIR (the dir CONTAINING assets/). We run from the repo root, so point it
# at the framework checkout THIS BINARY WAS BUILT FROM; without this the overlay loads no fonts and
# no menu. Derived from PSXPORT_DIR rather than re-spelled, so a dev-clone build cannot silently load
# the pinned submodule's assets.
export PSXPORT_ASSET_DIR="${PSXPORT_ASSET_DIR:-$PSXPORT_DIR}"
PSXPORT_DEBUG_SERVER="${PSXPORT_DEBUG_SERVER:-1}" \
PSXPORT_SPYRO_DISC="$DISC" exec ./scratch/bin/spyro_port "$EXE"
