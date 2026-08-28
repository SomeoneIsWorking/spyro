# SpyroEngine — Spyro 1, 2, and 3

A multi-title native PC port for the original PlayStation **Spyro trilogy**, built on the
[psxport](https://github.com/SomeoneIsWorking/psxport) static-recompilation framework.

The repository is complete only when one launcher selects and runs all three games by verified
executable identity. Spyro the Dragon (`SCUS_942.28`) is the current implemented target. Spyro 2
(`SCUS_944.25`) now has a verified executable identity and derived runtime/crt0 boundary, but no
verified disc, generated substrate, boot, or rendering. Spyro 3 (`SCUS_944.67`) has the same measured
identity/crt0 boundary and honest no-substrate refusal. The gameplay/rendering status below still
describes Spyro 1 only.

psxport statically recompiles the game's MIPS R3000A machine code into C and runs it on a native
platform layer — so the result is a PC program, not an emulator. This repo is the *game* half: the
Spyro-specific seam, the reverse-engineering, and the native reimplementations that progressively
take ownership of that recompiled substrate, each one gated byte-exact against the code it replaces.

> **You supply the disc.** No game code, assets, executable or disc image is included or
> distributed. Everything the build needs is extracted at build time from your own legally-obtained
> copy of the game.

---

## Status — honest

**The port boots through its title-owned frame/field path and renders the reached title and save
picker natively at 16:9 without guest VSync.** A real-disc New Game transition identified stage 14
as the next boundary; its complete native cutscene recipe now builds, but still needs an isolated
runtime and visual check. Gameplay and the remaining scene arms remain incomplete.

| | state |
|---|---|
| Disc → executable provisioning | 🟡 `spyro1`/`spyro2`/`spyro3` selected explicitly and identity-first; only the real Spyro 1 disc is verified |
| Static recompilation | ✅ 1,110 functions from the current `SCUS_942.28` executable/overlay set |
| Build + link (`spyro_port`) | ✅ |
| crt0 / boot (`GameConfig` boot group) | ✅ derived from the real crt0, not guessed |
| Reaches the guest's `main()` as recompiled code | ✅ verified by backtrace |
| CD sync/command path | ✅ `CD_init`, `CD_datasync`, `CD_cw`, `CD_sync` wired — every signature confirmed from the body |
| CD *reads* returning data | ✅ both game loader primitives serve disc bytes natively |
| **Renders the boot splash** | ✅ SCE splash draws and fades in over 8 frames |
| Past the splash into game init | ✅ boot advances; 3781 frames, 18 distinct occupancies, zero recomp misses |
| BIOS event delivery | ✅ class `0xF0000009` delivered per-frame from the vblank wait |
| VSync / vblank timebase | ✅ title `FieldScheduler` owns the counter/services; real capped product runs satisfy the frame contract while guest VSync remains fatal |
| Derived runtime + native frame loop | ✅ finite Spyro 1 driver is selected before `Game/Core` and drives real boot/title runs without dispatching guest main |
| Spyro 2 bring-up | 🔬 `Spyro2Runtime` owns measured `SCUS_944.25` crt0 facts and refuses at the first unverified execution boundary |
| Spyro 3 bring-up | 🔬 `Spyro3Runtime` owns measured `SCUS_944.67` crt0 facts and refuses at the first unverified execution boundary |
| Native renderer coverage | 🟡 stage-13 title/save flow is coherent; stage-14 cutscene recipe builds but is not yet live-verified |
| Widescreen | 🟡 player aspect control and 16:9 stage-13 producers are verified; stage-14 uses wide geometry/fade but awaits visual proof |
| Temporal lerp / 60fps | 🟡 Spyro 1 exposes the temporal product; the reached paired actor is verified on the new loop, while other scenes remain unowned |
| Native input | 🟡 pad packets are serviced by the title field scheduler and drive the real card/save-picker flow; level controls remain to be reached |
| Differential (SBS) harness | ⬜ not started |
| Code overlays | ✅ OVL0 extracted from `WAD.WAD` and recompiled (1 of ~37 located) |

Progress is tracked in the repo, not in a changelog:
`tools/re_frontier.py next` says which RE step is ready, `tools/info.py brief <words>` says what has
already been proven (and whether it still holds), and `tools/catalog.py search <symptom>` says what
has already been tried.

---

## Requirements

- `uv`, CMake, Git, pkg-config, SDL3, libzstd, zlib, OpenSSL, and a compatible C/C++ toolchain
- On a missing native dependency, `run.sh` names the exact Homebrew, DNF, APT, winget, or vcpkg
  command for the detected platform; it never installs privileged packages itself.
- A Vulkan-capable GPU + drivers

## Running

```sh
git clone --recursive https://github.com/<you>/SpyroEngine.git
cd SpyroEngine
./run.sh /path/to/'Spyro the Dragon (USA).chd'

# The other serials already have separate identity/runtime paths, but still refuse before Game
# until their generated substrates exist:
./run.sh --title spyro2 /path/to/'Spyro 2 - Ripto (USA).chd'
./run.sh --title spyro3 /path/to/'Spyro - Year of the Dragon (USA).chd'
./run.sh --help
```

`run.sh` is the stable entry point; it delegates the build policy to `tools/run.py`, which builds the
CHD tooling, verifies the selected serial from fresh media, builds the port, and launches that exact
executable. Spyro 1 additionally recompiles `SCUS_942.28` to C; Spyro 2/3 currently stop at their
explicit no-substrate runtime boundary. Alternatively set
`PSXPORT_SPYRO_DISC`, copy `.env.example` to `.env`, or drop a `*.chd` in the repo root — the
resolution order is *CLI arg > env var > `.env` > drop-in*.

The launcher enters through the frozen `uv.lock` environment and passes that same Python interpreter
to CMake, provisioning, and code generation. `./run.sh --prepare-only` performs the same provisioning
and build without starting the game. The player launcher uses isolated `scratch/build/player` and
`scratch/build/player-tools` trees, builds only `spyro_port`, and never runs the CTest/developer
verification suite.

`-h` and `--help` print usage and exit successfully before dependency, framework, disc, asset,
provisioning, build, or launch discovery. The built `spyro_port` executable accepts the same help
spellings before it inspects its executable argument.

Useful knobs for direct diagnostic runs: `PSXPORT_NOAUDIO=1`, `PSXPORT_FORCE_RECOMP=1`,
`PSXPORT_DEBUG=cd` (channel-gated diagnostics; see psxport's `docs/config.md`).

---

## Layout

```
game/           shared Spyro-lineage components and the current Spyro 1 implementation
  core/         SpyroRuntime lineage root, measured legacy facts, substrate registry, frame loop
  render/       semantic graphics producers and render orchestration
titles/         per-title identity and status (Spyro 1/2/3)
game/recomp_seeds.json   recompiler seeds — addresses discovery cannot see
generated/      the recompiled substrate (git-ignored; rebuilt from your disc)
external/psxport   the PSX-generic framework (workspace symlink or pinned private clone)
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

Runtime behavior is owned through inheritance: `SpyroRuntime : GameRuntime` is the lineage root,
and each serial has a final derived runtime. Only `Spyro1Runtime` binds the residual `GameConfig`
and `GameHooks` compatibility views; `Spyro2Runtime` and `Spyro3Runtime` do not inherit or reuse
them. Un-RE'd fact
fields remain honestly `0`: a plausible but wrong guest address breaks boot in a way that looks like
a framework bug.

## The seed file

The recompiler discovers functions from the binary (entry point, pointer scans, then the direct-`jal`
call graph). What it cannot see — functions reached only through a function pointer, or entered at a
runtime-computed re-entry point — is listed in `game/recomp_seeds.json`, because *which* addresses
those are is a fact about this specific executable. (psxport ships none: a foreign game's seeds land
mid-function and silently corrupt the recomp.) Grow the file empirically — when the substrate
fail-fasts with `[recomp-MISS] 0x800xxxxx`, add that address *with the rationale for how it is
reached*.

## Contributing

The working rules are in [`AGENTS.md`](AGENTS.md). In short: reverse-engineer before reimplementing;
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
