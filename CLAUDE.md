# SpyroEngine — working rules

A native PC port of **Spyro the Dragon (PS1, SCUS_942.28)** built on the
[psxport](https://github.com/SomeoneIsWorking/psxport) static-recompilation framework
(`external/psxport`). psxport recompiles the game's MIPS code to C and supplies the PSX platform
layer; this repo supplies the game — the seam, the RE, and the native reimplementations.

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

**RE before reimplementing, and don't jump the frontier.** The cardinal sin on a port is faking a
step's output before its RE is done — it makes a broken port *look* finished and blocks the real work.
Work the step `re_frontier.py next` gives you, not a downstream one.

**Verify on real data, and distrust green.** A gate only tells you about what it actually exercised.
Two traps: (1) the native override was never installed, so both sides ran substrate and the 0-diff is
hollow; (2) the code is mode-gated and the run never entered that mode. Prove the native body *ran*
before trusting the gate. A broken instrument fails silently — uniform output ("no diff", all-zero) is
the tell; validate a tool by feeding it a case that MUST differ.

**Believe the user over your own inference.** They are observing the running system; you are inferring.

---

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
