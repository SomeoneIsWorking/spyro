# SpyroEngine — working rules

A multi-title native PC port for **Spyro 1, 2, and 3** built on the
[psxport](https://github.com/SomeoneIsWorking/psxport) static-recompilation framework
(`external/psxport`). psxport recompiles the game's MIPS code to C and supplies the PSX platform
layer; this repo supplies the game — the seam, the RE, and the native reimplementations.

The product is not complete until the launcher recognizes and runs all three verified executable
identities. Spyro the Dragon (`SCUS_942.28`) is implemented today. Spyro 2 (`SCUS_944.25`) has a
measured executable identity and derived crt0-boundary runtime but no verified disc/substrate/boot;
Spyro 3 (`SCUS_944.67`) has a measured executable identity and derived crt0-boundary runtime but no
verified disc/substrate/boot. Each title gets a derived runtime selected by executable serial, and
shared code is extracted only from measured common behavior. The three `titles/*/executable.json`
manifests are the sole identity-fact authority; the build generates the C++ selection catalog from
them rather than copying those facts into runtime classes.

**Read `external/psxport/docs/porting-a-new-psx-game.md` first.** It is the methodology; this file is
only what is specific to Spyro.

---

## Start here, every task

```sh
python3 tools/info.py brief <words>       # what's already proven — and does it still hold?
python3 tools/re_frontier.py next         # which RE step is actually ready to work
python3 tools/catalog.py search <symptom> # has this been hit (or ruled out) before?
```

Believe these over your instinct about what is already known. End the task by writing back: what you
proved, what you disproved, any tool you caught lying. The data lives in the repo (`docs/`), as
greppable Markdown, so it travels with the code and reaches subagents.

**Holding a guest address? `python3 tools/whatis.py 0x800xxxxx` before anything else.** It answers
from every source at once — which module's span contains it, which overlay is actually RESIDENT there
in the last RAM dump, what each candidate image says and **whether they disagree**, whether it is
recompiled, whether it is one of the 36 per-overlay entries main installs, what points at it, what
Ghidra called it, and which claim or issue already mentions it.

Do not do that cross-reference by hand. Two of this session's wrong conclusions came from exactly
that — reading an address out of an overlay image that was not the resident one (C065, issue 0025),
and a value-extraction scan that grabbed a neighbouring instruction pair (C067). Both were
cross-referencing slips, not reasoning slips, so being more careful is not the fix; running the
command is.

---

## The rules that matter most here

**Never guess a guest address.** A wrong address does not fail cleanly — it breaks boot or diverges
the byte-compare in a way that reads as a framework bug. An un-RE'd `GameConfig` field stays `0` with
an explicit TODO. Zero is honest; a plausible-looking wrong value is not. Every filled field carries
the disassembly that justifies it (see `game/core/game_config.cpp`).

**Never guess an overlay load base.** An overlay is keyed *by* its load address, so a wrong base emits
a whole module of correctly-decoded instructions at wrong addresses — every `jal` target, pointer test
and router lookup then silently wrong. Capture the real destination from a running port
(`PSXPORT_DEBUG=cd`). psxport fails fast rather than defaulting, deliberately.

**The generated substrate is sacrosanct.** Never hand-edit `generated/`. A mistranslation is fixed in
the recompiler; a missing function is fixed by adding a *seed* (below), never by patching output.

**Seeds are grown empirically, with rationale.** `game/recomp_seeds.json` lists only what discovery
cannot see. When the substrate fail-fasts with `[recomp-MISS] 0x800xxxxx`, find how the address is
reached and add it *with that explanation*. An address with no rationale is unreviewable later. Never
copy another game's seeds — they land mid-function and silently corrupt the recomp.

**No bandaids.** No magic constants or offsets, no special-casing the one failing input, no
swallowed errors, no retry-until-green, no commenting out a failing check. If the real fix is too big
right now, say so plainly and mark the stopgap `// STOPGAP: <proper fix> because <why>` — never slip a
hack in as if it were a fix.

**The picture comes from GAME STATE, never from what the GTE produced.** Two checkable rules (the
binding statement is `external/psxport/docs/workspace/PROTOCOL.md`; the word "tap" is retired because it needed adjudication
every time). (1) The shipping picture path runs **no `gen_func_*` body** — reads are fine, a producer
reads the node's own fields, and diagnostics are exempt because they answer questions rather than
produce the picture. (2) **Resolve from what SUBMITS to the GTE**: find the
`SetRotMatrix`/`SetTransMatrix`/RTPS site and take its INPUTS — the game's own pre-quantisation
values. Never read `gte_read_ctrl()`/the OT/composed GP0 and invert them to recover a transform; those
are s16-quantised, and factoring the camera back out leaves a residue that is *a function of the
camera* (measured elsewhere in this workspace: 0.13 px still, 1.53 px panning, 12/12 sign
alternations — a layer that "vibrated" with nothing in the game moving it). Dusklight lerps recorded
matrices and we may not: theirs are float values from a decomp, ours would be s16 GTE output. Same
technique, different source.

**RE before reimplementing, and don't jump the frontier.** The cardinal sin on a port is faking a
step's output before its RE is done — it makes a broken port *look* finished and blocks the real work.
Work the step `re_frontier.py next` gives you, not a downstream one.

**Verify on real data, and distrust green.** A gate only tells you about what it actually exercised.
Two traps: (1) the native override was never installed, so both sides ran substrate and the 0-diff is
hollow; (2) the code is mode-gated and the run never entered that mode. Prove the native body *ran*
before trusting the gate. A broken instrument fails silently — uniform output ("no diff", all-zero) is
the tell; validate a tool by feeding it a case that MUST differ.

**Believe the user over your own inference.** They are observing the running system; you are inferring.

### Native frame ownership

The Dusklight ownership pattern applies at the frame boundary: the framework shell owns iteration,
and the title owns one finite step plus its services. For Spyro 1, `Spyro1FrameDriver` owns the
measured logic order, `BootSequence` owns resumable boot `0x800127C0`/`0x8001286C`, and
`FieldScheduler` owns one 60 Hz field (counter, input, callback root, audio, events, presentation,
pacing, and host-turn acknowledgement). `game/core/main.cpp` composes those owners; it must not grow
a second implementation of their service order.

Guest libetc VSync `0x8005DBC4` is a fatal product-contract trap. Do not restore a success override
at helper `0x8005DD0C`; re-own the caller's wait/query through `FieldScheduler`. Generated bodies stay
intact as A/B oracles, but any diagnostic that dispatches a retained VSync caller must split that
tail first or fail fast honestly.

---

## Run the gate after a REBASE, not only after your own edits

`git rebase` during a push silently pulls in upstream psxport work that has never been gated against
*this* game. That is how a CD change (`5daf2fe4`) took the port from 18809 frames to 8 without a
single line of local code changing — see `docs/issues/0028`.

It presented as "the port got slow": a capture that used to finish started timing out. Two innocent
explanations were available (my own edit, and real external machine load) and both were wrong. What
separated them was the gate's per-check numbers — bytes-from-disc collapsing to one sector is not
something "slow" explains.

## Debugging a live port

Three things make a running port inspectable without a rebuild per question. Reach for them before
adding a probe.

```sh
PSXPORT_REPL=1 ./scratch/bin/spyro_port <exe>     # interactive: r <addr>, press <btn>, run N, dumpram
PSXPORT_SNAP_AT=1500,4000 …                       # 2 MB guest RAM at those frame boundaries
kill -USR1 <pid>                                  # …or snapshot whenever you like
PSXPORT_WWATCH=<lo>,<hi> PSXPORT_WWATCH_BT=1      # who wrote this address (pc, ra, frame, registers)
python3 tools/whatis.py 0x800xxxxx --ram scratch/raw/snap_1500.bin
```

**`PSXPORT_WWATCH` before a static scan for a writer.** Address-immediate scans miss stores that go
through a register, and that has produced a wrong answer here more than once — the sub-state writer
was invisible to two separate scans and the watchpoint found it in one run (I016).

**A crash is not required to see state.** The port dumps RAM at every recomp-MISS, but snapshots work
on a healthy run too; that is how the OT/packet-pool values were read (C073).

**Counting state transitions is not a liveness test.** A screen with no state changes can be fully
alive — the title screen was called "hung" on that basis and was animating the whole time (C071).

## Diagnostics

All diagnostics go through psxport's channel-gated logger — `PSXPORT_DEBUG=cd,gpu` (see
`external/psxport/docs/config.md`). Never scatter `printf`/`if (debug) fprintf` through the code, and
never read `getenv` directly.

`PSXPORT_DEBUG=cd` is the highest-value channel right now: it logs every CD load and its destination,
which is how the open overlay question gets settled.

## Scratch output

Everything transient goes in the git-ignored `scratch/`, kept split by kind (`scratch/logs/`,
`scratch/bin/`, …). **Never write run artifacts to `/tmp`** — it is a RAM-backed tmpfs with a small
per-user quota on this machine, and logs/dumps fill it in a run or two, breaking all writes with
"Disk quota exceeded". Diagnose that symptom with `quota -s`, not `df`.

## Where the framework source comes from — `external/psxport` is the shared tree

`external/psxport` is **not a submodule** (2026-08-16): it is a SYMLINK to the workspace's shared
framework clone (`$PSX/psxport`) when one exists, or a private clone at this repo's `psxport.pin` on a
fresh machine. `tools/psxport_sync.py --auto` (called by `run.sh`) establishes whichever applies. So a
framework edit made through either path is the SAME directory, live in every port at once — commit and
push framework work in `psxport/`, never here. `psxport.pin` records the framework commit this game
was built and VERIFIED against; `tools/psxport_sync.py --bump` updates it, and the gate's `--check`
fails when the framework you built against is not the recorded one.

Build this game against in-progress framework work:

```sh
cmake -S . -B build -DPSXPORT_DIR=$PSX/psxport   # or just ./run.sh — it resolves external/psxport itself
```

`PSXPORT_DIR` defaults to `external/psxport`, so a bare clone of this repo still builds standalone —
keep it that way. `run.sh` announces which framework checkout a run was built from and whether it was
dirty; read that line before trusting any measurement. The full protocol (area claims, how a framework
change lands, the standing USER rules) is `external/psxport/docs/workspace/PROTOCOL.md`; the workspace
map is `external/psxport/docs/workspace/WORKSPACE.md`.

## Never commit

Disc images (`*.chd`), the extracted executable, `generated/`, or machine-specific absolute paths.
`tools/go_public.py` audits the full history for exactly these; run it before publishing.

## Spyro-specific facts worth knowing

- **One executable, no boot stub.** `SYSTEM.CNF` boots `cdrom:\SCUS_942.28` directly. There is no
  SCEA stub `LoadExec`ing a `MAIN.EXE`, so psxport's stub stage is unused.
- **crt0 is a textbook Sony crt0** at the PS-EXE entry `0x8005B8E0`, and psxport's generic
  `crt0_setup()` reproduces it instruction for instruction — which is why the boot group is fully
  derived. See claim C001.
- **The disc tree** is only `SYSTEM.CNF`, `SCUS_942.28`, `WAD.WAD`, `SOURCE/SOURCE.TRD`, `S0/*` (a
  bundled Crash demo — not Spyro code) and `PETEXA*.STR`.
- **libcd lives around `0x80063000-0x80065000`**: `func_8006397C` references the `"CdInit"` string,
  `func_80064CEC` references `"CD_cw"`/`"CD timeout"` (the command-wait). Roles still need confirming
  by reading the bodies — the string a function prints is a hint, not proof of its role.
- **Public decomps exist** and are useful for cross-checking function boundaries/names — see
  `docs/references.md`. They are references only; nothing is vendored, and where a reference and a
  measurement disagree, the measurement wins.
