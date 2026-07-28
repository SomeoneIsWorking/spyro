# Codemap — what's where, what's done, what's missing

The orientation map: consult it at the START of a task to avoid re-deriving structure, and update it
in the SAME commit that lands or changes a subsystem. A stale map is worse than none — and a
subsystem is marked done only when VERIFIED on real data, never to look better.

Companions: `docs/re-frontier.md` (ordered RE steps: real vs hack), `docs/issues/` (what's been tried
and ruled out), `docs/info/` (claims + instruments ledgers).

**Status vocabulary:** ✅ verified on real data · 🟡 partial (gap named) · 🔬 in progress ·
⬜ not started · ❓ unresolved question · ➖ not applicable to this game

---

## The two halves

| | |
|---|---|
| `external/psxport/` | The PSX-generic framework (submodule): the MIPS→C recompiler, the runtime substrate, GTE/SPU/MDEC/CD/GPU backends, SDK HLE, the SBS differential harness, the SDL_GPU renderer. **Not ours** — fix framework bugs upstream, keep it game-agnostic. |
| `game/`, `tools/`, `generated/` | This port: the seam, the RE, the provisioning, the recompiled substrate. |

---

## Subsystems

| subsystem | where | status | notes |
|---|---|---|---|
| Disc → executable provisioning | `tools/ensure_recomp.py`, `run.sh` | ✅ | Extracts `SCUS_942.28` via psxport's `discdump`; hash-checks the generated set against exe + recompiler sources + seed file, so every machine builds an identical substrate. |
| Static recompilation | `game/recomp_seeds.json` → `generated/` | ✅ | Seed file holds ONE entry — the genuine fn-pointer target `0x80024054`. Spyro's GTE code dispatches via computed-offset jumps (`jr base+idx*2^k`, no address table); that whole family is now handled by a recogniser IN the recompiler emitting mid-function labels, not by seeds (C054/C055). Seeding such a target splits the enclosing function and corrupts the recomp even when the address is right — C051. |
| Framework seam — config | `game/core/game_config.cpp` | 🟡 | Boot group fully derived (claim C001). Per-frame OT/packet-pool, scheduler, CD and pad groups are honestly `0` — un-RE'd, not forgotten. |
| Framework seam — hooks | `game/core/game_hooks.cpp` | 🟡 | Only `bootInit` + `registerOverrides` implemented; the rest deliberately null (Phase 0 runs everything on the substrate). |
| Framework seam — recomp registry | `game/core/recomp_register.cpp` | ✅ | Wires `main_dispatch`/`rec_func_index`/override setter. Overlay setters null. |
| Process entry | `game/core/main.cpp` | ✅ | Installs the seam, brings up GTE/MDEC/SPU/GPU/threads, loads the exe, boots via `dc_boot_init`. |
| Build | `CMakeLists.txt`, `cmake/spyro_port.cmake` | ✅ | `spyro_port` = `game/**` + `generated/**` linked against `libpsxport`. |
| Boot: crt0 → guest `main()` | — | ✅ | Verified by backtrace (claim C002). |
| CD sync/command path | `game/core/game_config.cpp` `hle` + CD groups | ✅ | Stock Sony libcd (`bios.c` v1.86); primitives identified by the name each prints. Wired: `CD_init` `0x800653B4`, `CD_datasync` `0x800655A0`, `CD_cw` `0x80064CEC` (signature confirmed from the body: `a0&255`→command tables, `a1`=param, `a2`=result). Plus the `game->cd.overridesInit()` call, absent at first, without which the whole cd* group never installed. **Zero CD timeouts at boot.** |
| Boot splash rendering | — | ✅ | SCE splash draws + fades in (8 frames; nonzero pixels 0.7%->2.9%, 320x240 then 512x240). Proves the whole chain: guest frame loop -> GP0 stream -> native renderer -> present. |
| Past the splash | `game/core/cd_queue.cpp` | ✅ | **Resolved.** Was a spin in `func_800163E4` <- the loader `0x80016500`, waiting on a CD read that never delivered. Fixed by owning the loader natively (it reads `start = lba + arg_off/2048` and copies sectors to the destination) and delivering the completion. Boot went 436 → 3781 frames (claim C028). |
| CD *reads* delivering bytes | `game/core/cd_queue.cpp`, `docs/issues/0003` | ✅ | **Two** read primitives, both served natively: `0x80016500` (11 call sites, sync boot loader) and `0x80016698` (19 call sites, the streaming/level primitive — C047). Missing the second meant level reads were acked with no data. 3.7 MB moved per run. | Spyro links STOCK libcd: `CdlSetloc` sets position, then the read transfers — so the LBA is **not** an argument, and psxport's `cd_read(blocks,lba,buf)` contract does not fit. Read path: `func_80065DBC` (`CdRead: Shell open/retry`, keeps only a0), `func_800659F0` (`sector error`). Setloc tracking now lands in the framework (`Cd::setloc_lba`), verified: the guest seeks LBA 37 = `WAD.WAD`. The transfer path is still unwired — wiring `cdReadPrim` to a `(mode, buf)` function would corrupt guest memory. |
| ~~CD reads (old row)~~ | — | ⬜ | Untested. Commands ACK, which is NOT the same as a read returning correct data; the boot doesn't yet get far enough to need one. `cdReadPrim`/`cdFileLoad`/`cdAsyncRead` still 0, as are `cdSync`/`cdReadSync` (their handlers write 8 bytes at `a1` per the *public* CdSync/CdReadSync contract, while Spyro's `CD_sync` `0x800647A0` is the internal primitive — signature unconfirmed). |
| VSync / vblank timebase | `game/core/vsync.cpp` | ✅ | Counter `0x800749E0` (frozen — no IRQ increments it). The wait helper `0x8005DD0C` is overridden to advance it, presenting+pacing one frame per vblank. Chosen over overriding `VSync` itself so VSync's own return value + GPU polling stay on the recompiled body. Verified: counter advances `+1` per wait across 16 waits. |
| Native frame loop | — | ⬜ | Guest owns its own loop today; needs Spyro's display init + buffer flip RE'd. |
| Renderer / input / audio | — | ⬜ | Runs as recompiled guest code through the framework's PSX backends. Nothing owned natively. Logo screens render legibly but with colour speckling + horizontal truncation (issue 0016). Input: the guest never reads the pad at all (C035), so it must come from the BIOS/HLE layer. |
| Differential (SBS) harness | — | ⬜ | Phase 0 of the playbook wants this up *before* owning any function. Not wired — stated plainly because six functions are already owned without it. `tools/gate.sh` is a boot-PROGRESS gate and does not substitute for it. |
| Regression gate | `tools/gate.sh` | 🟡 | 10 checks. **Currently RED, honestly**: the port aborts at frame 3781 (issue 0015). The gate previously reported PASS on a crashing port for its whole existence — it ran under `timeout -s KILL`, which swallows the exit status, and every other check was a count a crashed run still satisfies (instrument I007). The exit-code check is now first. |
| RE tooling | `tools/callsite_args.py`, `tools/wad_index.py`, `tools/callgraph.py` | ✅ | `callsite_args.py` recovers argument VALUES at every static call site by abstract-interpreting a straight-line window (I006). `wad_index.py` enumerates `WAD.WAD`'s index and scores entries for code content (I005). `callgraph.py` answers "does A reach B" over direct calls (I009) — blind to `jalr`, and says so on every negative. |
| Code overlays | `game/recomp_seeds.json`, `tools/wad_index.py` | 🟡 | Answered: they live inside `WAD.WAD` and are loaded into a **single shared staging arena at `0x8007AA38`** (read-only constant `[0x800113A0]`, claim C032) — not one base per overlay. Loader arg `a0` is the overlay index (`0x25`=37). **TWO overlays now share the arena slot and the router distinguishes them by signature** (C048). OVL0 (`WAD.WAD +0x5B800`, 14336 bytes) and OVL1 (`+0xB83800`, 65536 bytes, index entry 11 — the first LEVEL overlay) are both extracted, recompiled and wired into the router's arena slot (identified by content signature at load time; gate check 8). **36 code overlays located** — entry 2 plus the odd entries 9..77, alternating code/data per level (C033). Their LOAD BASES are unknown and not recoverable from their own bytes (C034: no internal direct calls to triangulate from); one observed load per level settles each. `docs/issues/0013` is resolved — psxport's router already models a shared arena. |
| Boot stub (SCEA) | — | ➖ | Spyro boots `SCUS_942.28` directly — no stub stage exists. |
| Cooperative stage scheduler | — | ➖ | No overlay/stage split of the kind psxport's `SchedBody` hooks describe. |

---

## Where is X? (quick index)

- **The guest's entry point / crt0** → `0x8005B8E0` (PS-EXE entry); mirrored by psxport's
  `crt0_setup()` in `external/psxport/runtime/recomp/native_boot.cpp`
- **The guest's `main()`** → `0x80012204`, tail-called by crt0; entered via
  `spyro_bootInit` in `game/core/game_hooks.cpp`
- **Every Spyro guest address we've committed to** → `game/core/game_config.cpp` (each with its
  derivation)
- **libcd** (stock Sony `bios.c` v1.86; RCS id at `0x80011EB8`) → internal primitives, each
  identified by the name it prints: `CD_sync` `0x800647A0` · `CD_ready` `0x80064A20` ·
  `CD_cw` `0x80064CEC` · `CD_init` `0x800653B4` · `CD_datasync` `0x800655A0`.
  String table: `0x80011C98-0x80011F50`. The HLE window is `[0x80063000,0x80066000)`.
- **How to add a missing recompiled function** → `game/recomp_seeds.json` (never patch `generated/`)
- **What arguments a call site passes** → `tools/callsite_args.py --target 0x<callee>`; `?` means
  computed, not constant — an honest miss, not a zero
- **How to disassemble a region** → `external/psxport/tools/disasm.py <ramdump> <start> <end>`; build
  a RAM image by laying the PS-EXE text at its load address (`0x80010000`)
- **Why a `GameConfig` field is 0** → it is un-RE'd; the comment above it says what to RE
- **What's already been proven / ruled out** → `tools/info.py brief`, `tools/catalog.py search`
- **What to work on next** → `tools/re_frontier.py next`

---

## Known framework warts (upstream, not ours to paper over)

- `RecompRegistry` names *per-overlay* override setters (`ov_a00_set_override`,
  `ov_game_set_override`) — Tomba!2-shaped members in a game-agnostic struct. Spyro passes `nullptr`.
- `GameHooks::SchedBody` enumerates Tomba!2's stage bodies by name.
- `GameConfig::guestMemset_gen` expects a game-specific fast-path body.

None of these block Spyro; they are recorded so nobody mistakes them for something Spyro must fill in.
