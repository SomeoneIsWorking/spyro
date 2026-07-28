# SpyroEngine

A **native PC port of Spyro the Dragon (PS1)** built on the
[psxport](https://github.com/SomeoneIsWorking/psxport) static-recompilation framework.

psxport statically recompiles the game's MIPS R3000A machine code into C and runs it on a native
platform layer — so the result is a PC program, not an emulator. This repo is the *game* half: the
Spyro-specific seam, the reverse-engineering, and the native reimplementations that progressively
take ownership of that recompiled substrate, each one gated byte-exact against the code it replaces.

> **You supply the disc.** No game code, assets, executable or disc image is included or
> distributed. Everything the build needs is extracted at build time from your own legally-obtained
> copy of the game.

---

## Status — honest

**Phase 0: the port boots, runs the game's own `main()` on a restored vblank timebase, renders the
boot splash, loads assets off the disc, and advances into changing content.** It is not yet playable —
nothing is owned natively beyond the CD/event seams, and gameplay is unverified.

| | state |
|---|---|
| Disc → executable provisioning | ✅ hash-checked, reproducible (`tools/ensure_recomp.py`) |
| Static recompilation | ✅ 621 functions from `SCUS_942.28` |
| Build + link (`spyro_port`) | ✅ |
| crt0 / boot (`GameConfig` boot group) | ✅ derived from the real crt0, not guessed |
| Reaches the guest's `main()` as recompiled code | ✅ verified by backtrace |
| CD sync/command path | ✅ `CD_init`, `CD_datasync`, `CD_cw`, `CD_sync` wired — every signature confirmed from the body |
| CD *reads* returning data | 🔬 root-caused: the override point is too low — own the game's loader, not libcd ([`0010`](docs/issues/0010-the-override-point-is-too-low-own-the-game-s-loa.md)) |
| **Renders the boot splash** | ✅ SCE splash draws and fades in over 8 frames |
| Past the splash into game init | ✅ boot advances; content changes across 436 frames (18 distinct occupancies) |
| BIOS event delivery | ✅ class `0xF0000009` delivered per-frame from the vblank wait |
| VSync / vblank timebase | ✅ counter `0x800749E0` restored; guest's own frame loop runs, presents and paces |
| Native frame loop, renderer, input | ⬜ not started |
| Differential (SBS) harness | ⬜ not started |
| Code overlays | ❓ **unresolved** — see [`docs/issues/0001`](docs/issues/0001-whether-spyro-loads-code-overlays-and-from-where.md) |

Progress is tracked in the repo, not in a changelog:
`tools/re_frontier.py next` says which RE step is ready, `tools/info.py brief <words>` says what has
already been proven (and whether it still holds), and `tools/catalog.py search <symptom>` says what
has already been tried.

---

## Requirements

- **Linux:** `cmake`, `pkg-config`, SDL3, libzstd, zlib, `python3`, a C/C++ toolchain
- **macOS:** `brew install cmake pkg-config sdl3 zstd zlib python3`
- A Vulkan-capable GPU + drivers

## Running

```sh
git clone --recursive https://github.com/<you>/SpyroEngine.git
cd SpyroEngine
./run.sh /path/to/'Spyro the Dragon (USA).chd'
```

`run.sh` does everything end to end: builds the CHD tooling, extracts `SCUS_942.28` from your disc,
statically recompiles it to C, builds the port, and launches it. Alternatively set
`PSXPORT_SPYRO_DISC`, copy `.env.example` to `.env`, or drop a `*.chd` in the repo root — the
resolution order is *CLI arg > env var > `.env` > drop-in*.

Useful knobs: `PSXPORT_NOAUDIO=1`, `PSXPORT_NOWINDOW=1` (headless), `PSXPORT_FORCE_RECOMP=1`,
`PSXPORT_DEBUG=cd` (channel-gated diagnostics; see psxport's `docs/config.md`).

---

## Layout

```
game/           the Spyro-specific code
  core/         the framework seam: GameConfig (guest addresses), GameHooks, the recomp registry, main()
  recomp_seeds.json   our recompiler seeds — addresses discovery can't see (a GAME fact, see below)
generated/      the recompiled substrate (git-ignored; rebuilt from your disc)
external/psxport   the PSX-generic framework (submodule)
tools/          provisioning + the project's information system
docs/           codemap, RE frontier, issues, claims/instruments ledgers, references
```

## How Spyro differs from psxport's reference consumer

psxport was extracted from a *Tomba! 2* port, so its reference consumer shapes some of the framework.
Spyro differs in ways that matter:

- **One executable, no boot stub.** `SYSTEM.CNF` boots `cdrom:\SCUS_942.28` directly; there is no
  SCEA stub that `LoadExec`s a separate `MAIN.EXE`, so there is no stub stage to recompile or run.
- **No per-overlay disc files.** The disc tree is `SYSTEM.CNF`, `SCUS_942.28`, `WAD.WAD`,
  `SOURCE/`, `S0/` (a bundled Crash demo) and the `PETEXA*.STR` streams. Whether code overlays exist
  *inside* `WAD.WAD` is an open question — see above.
- **No cooperative stage/task scheduler** of the kind the framework's `SchedBody` hooks describe.

These differences are why most of `GameHooks` is deliberately null here and several `GameConfig`
groups are honestly `0`: an un-RE'd address is left zero with a `TODO` rather than filled with a
plausible value, because a wrong guest address breaks boot in a way that looks like a framework bug.

## The seed file

The recompiler discovers functions from the binary (entry point, pointer scans, then the direct-`jal`
call graph). What it cannot see — functions reached only through a function pointer, or entered at a
runtime-computed re-entry point — is listed in `game/recomp_seeds.json`, because *which* addresses
those are is a fact about this specific executable. (psxport ships none: a foreign game's seeds land
mid-function and silently corrupt the recomp.) Grow the file empirically — when the substrate
fail-fasts with `[recomp-MISS] 0x800xxxxx`, add that address *with the rationale for how it is
reached*.

## Contributing

The working rules are in [`CLAUDE.md`](CLAUDE.md). In short: reverse-engineer before reimplementing;
no bandaids (no magic constants, no guessed addresses, no swallowed errors); gate native code
byte-exact against the substrate it replaces, and never trust a green gate without proving it
actually exercised the code; and record what you prove *and* what you rule out.

## License & legal

The port code here is provided as-is for research and preservation. psxport's vendored beetle-psx
backend is GPL-2.0. **No game assets, ROMs, disc images, executables or BIOS files are included or
distributed** — supply your own legally-obtained copy.

Spyro the Dragon is a trademark of its respective rights holders. This project is not affiliated with
or endorsed by them.

## References

Public Spyro RE projects used for cross-checking (not vendored) are listed in
[`docs/references.md`](docs/references.md).
