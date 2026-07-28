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
| Static recompilation | `game/recomp_seeds.json` → `generated/` | ✅ | 621 functions, 8 shards. Binary-only discovery (entry + pointer scans + `jal` graph); the seed file is empty so far because nothing has needed a seed yet. |
| Framework seam — config | `game/core/game_config.cpp` | 🟡 | Boot group fully derived (claim C001). Per-frame OT/packet-pool, scheduler, CD and pad groups are honestly `0` — un-RE'd, not forgotten. |
| Framework seam — hooks | `game/core/game_hooks.cpp` | 🟡 | Only `bootInit` + `registerOverrides` implemented; the rest deliberately null (Phase 0 runs everything on the substrate). |
| Framework seam — recomp registry | `game/core/recomp_register.cpp` | ✅ | Wires `main_dispatch`/`rec_func_index`/override setter. Overlay setters null. |
| Process entry | `game/core/main.cpp` | ✅ | Installs the seam, brings up GTE/MDEC/SPU/GPU/threads, loads the exe, boots via `dc_boot_init`. |
| Build | `CMakeLists.txt`, `cmake/spyro_port.cmake` | ✅ | `spyro_port` = `game/**` + `generated/**` linked against `libpsxport`. |
| Boot: crt0 → guest `main()` | — | ✅ | Verified by backtrace (claim C002). |
| CD sync primitives | `game/core/game_config.cpp` `hle` group | 🟡 | Stock Sony libcd (`bios.c` v1.86) — primitives identified by the name each prints. `CD_init` (`0x800653B4`) and `CD_datasync` (`0x800655A0`) wired; both confirmed by the boot loop they removed. `cdReadSync` left 0 deliberately (signature unconfirmed; its handler writes 8 bytes at `a1`). |
| **CD reads** | `game/core/game_config.cpp` CD group | 🔬 | **Current blocker.** `CD_cw` still times out on real commands (`CdlSetmode`, `CdlSetloc`) — needs the native read path, not a sync stub. |
| VSync | — | ⬜ | Boot now reaches `VSync: timeout`. `func_8005DD0C`. Must be reimplemented faithfully and registered game-side; `hle.vsyncTrap` must stay 0 while the guest owns its loop. |
| Native frame loop | — | ⬜ | Guest owns its own loop today; needs Spyro's display init + buffer flip RE'd. |
| Renderer / input / audio | — | ⬜ | Runs as recompiled guest code through the framework's PSX backends. Nothing owned natively. |
| Differential (SBS) harness | — | ⬜ | Phase 0 of the playbook wants this up *before* owning any function. Not wired. |
| Code overlays | — | ❓ | See `docs/issues/0001`. Decomps say 37 exist; disc has no per-overlay files. |
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
- **libcd** → `~0x80063000-0x80065000`. `func_8006397C` refs `"CdInit"`; `func_80064CEC` refs
  `"CD_cw"`/`"CD timeout"` (command-wait); `func_800647A0`, `func_80064A20`, `func_800655A0` are
  further `"CD timeout"` sites. libcd string table: `0x80011CA0-0x80011EB0`.
- **How to add a missing recompiled function** → `game/recomp_seeds.json` (never patch `generated/`)
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
