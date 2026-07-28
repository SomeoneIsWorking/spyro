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
| Framework seam — config | `game/core/game_config.cpp` | 🟡 | Boot group fully derived (claim C001). Pad group is now filled and justified (slot buffers `0x800786A0`/`0x80078E50`, driver pointer table `0x80075D48` stride 240 — C063). Per-frame OT/packet-pool, scheduler and CD groups are honestly `0` — un-RE'd, not forgotten. |
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
| Natively OWNED guest code | `game/core/native_{rand,leaf,vec}.cpp`, `tools/own_candidates.py` | 🟡 | **The ratio: 20 observation wrappers to 15 native bodies** (C075, C081). Most overrides super-call the recompiled body; the CD/pad ones are platform-level *supply*. Genuinely owned: the vblank wait plus thirteen leaves — `copy3` (136 callers), `vadd` (102), `veclen` (87, **GTE**), `vsub` (83), `angtblA` (69), `angtblB` (66), `fill` (59), `rand` (41), `zero3` (40), `copyw` (30), `mvmva` (30, **GTE**), `angdist` (26), `vsra` (24) — plus `vscale` (24, **GTE**), `isqrt` (17) — **~834 static call sites**, each byte-exact under `PSXPORT_NDIFF` (C081). The high-caller leaf queue is EXHAUSTED (best remaining: 15 callers); further ownership means non-leaf functions, whose callees must be owned first. GTE code is owned WITHOUT reimplementing the GTE: the native body does the scalar work and calls the platform's own `gte_op`/`gte_read_data`, so COP2 results match by construction. The differential has caught two real transcription errors that review did not: an unreproduced `$at` clobber, and a mis-subtracted table base. Every native body is gated byte-exact against the body it replaced on each gate run (`PSXPORT_NDIFF`, I019) — validated in both directions, and it caught a real `$at` inequivalence in the first replacement that reading the code did not. **Pick targets from the PROFILE** (`tools/prof_hot.py`, I022) when the goal is speed: guest code is only ~4.5% of CPU time (C082), the hottest guest function has just TWO static callers, and none of the 15 owned bodies appears in the profile at all — so ownership here buys correctness, not speed. Use `tools/own_candidates.py` (I020) for coverage, and not by eye — that already went wrong once: `0x8001ED5C` looks like a small buffer flip and is the whole per-frame stage dispatcher. Next: frontier `own.next-targets`. |
| Renderer / input / audio | — | ⬜ | Runs as recompiled guest code through the framework's PSX backends. Nothing owned natively. Logo screens render legibly but with colour speckling + horizontal truncation (issue 0016). **Input is WIRED and works** (C063): the game registers its own pad decoder `0x80053C68` as the VBlank callback (`VSyncCallback` `0x8005DE58`), which a no-IRQ runtime never fires, so it ran once at boot and never again. `game/core/vsync.cpp`'s vblank wait now fills the slot buffers (`Pad::serviceFrame`) and then runs that callback with the register file saved/restored as an IRQ would. Pad class `[0x80077384]` goes 0 -> 2 and the game leaves attract. C035 ("never touches the pad") is FALSIFIED — libpad reaches SIO0 through the pointer `[0x80075220]` = `0x1F801040` (C064). |
| Differential (SBS) harness | — | ⬜ | Phase 0 of the playbook wants this up *before* owning any function. Not wired — stated plainly because six functions are already owned without it. `tools/gate.sh` is a boot-PROGRESS gate and does not substitute for it. |
| Regression gate | `tools/gate.sh` | 🟡 | 10 checks. **Currently RED, honestly**: the port aborts at frame 3781 (issue 0015). The gate previously reported PASS on a crashing port for its whole existence — it ran under `timeout -s KILL`, which swallows the exit status, and every other check was a count a crashed run still satisfies (instrument I007). The exit-code check is now first. |
| RE tooling | `tools/callsite_args.py`, `tools/wad_index.py`, `tools/callgraph.py` | ✅ | `callsite_args.py` recovers argument VALUES at every static call site by abstract-interpreting a straight-line window (I006). `wad_index.py` enumerates `WAD.WAD`'s index and scores entries for code content (I005). `callgraph.py` answers "does A reach B" over direct calls (I009) — blind to `jalr`, and says so on every negative. |
| Code overlays | `tools/overlay_scan.py`, `game/overlays.json`, `tools/ensure_recomp.py` | 🟡 | They live inside `WAD.WAD` and stream into a **single shared arena at `0x8007AA38`** (read-only constant `[0x800113A0]`, C032) — not one base per overlay; the router tells residents apart by content signature. **The set is not hand-maintained**: nothing in the executable enumerates the overlays (the WAD offset arrives in a register), so `overlay_scan.py` (I011) recovers it from a run's arena loads into `game/overlays.json` and `ensure_recomp.py` slices those out. Names are WAD-offset-derived (`OV_<hex>`) so the set grows without renaming and silently re-pointing seeds. **7 overlays extracted, all identified at load, zero unmatched.** Each overlay's per-frame entry (installed into `[0x80075734]`, called indirectly at `0x80033AA4`, C066) is now **derived, not hand-added** (C067): main's own install table gives 36 distinct addresses, and one is claimed by an overlay only if it is prologue-shaped in *that overlay's* bytes — the test that stops another level's entry splitting this module, since all share one base. `ensure_recomp.py` merges the derived set with the hand-reasoned seeds into `generated/.recomp_seeds_merged.json` and hashes it into the recomp identity. **The arena is reloaded constantly, so the last IDENTIFIED overlay is not the resident one at a fail-fast** (C065) — diagnose from the automatic miss RAM dump (I012), never from the last-identified image; doing the latter produced a fully wrong diagnosis (issue 0025). 36 code entries located in the WAD index (C033); the decomps describe 37 overlays. |
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
- **ANYTHING about a guest address** → `tools/whatis.py 0x800xxxxx` — the first command to run when holding an address. Answers from every source at once (module span, which overlay is RESIDENT there in the last RAM dump, per-image agreement, recompiled?, is it one of the 36 per-overlay entries, static refs, Ghidra, existing claims/issues) and flags where sources DISAGREE. Doing this cross-reference by hand produced two wrong conclusions this session (C065, C067)
- **What's already been proven / ruled out** → `tools/info.py brief`, `tools/catalog.py search`
- **What to work on next** → `tools/re_frontier.py next`

---

## Performance — where the time actually goes

Measured, not assumed (`PSXPORT_PROF=1` + `tools/prof_hot.py`, I022):

- **Guest code is only ~5-6% of CPU** (C082). Native ownership buys correctness and architecture,
  not speed — pursue it for that reason, and pick speed targets from the profile instead.
- **The diagnostics layer cost ~10% while doing nothing.** lucent's `channel_enabled` was 6.06% with
  logging switched OFF (a mutex plus a `std::string` per call) — fixed in lucent itself, now 0.33%
  (C084). Every guest store called five out-of-line hooks; `cw_check` + `wwatch_check` were ~4.9% of
  pure call overhead, now inlined away (C085).
- **Result: +12.6% frames in a fixed run, and more of the game reached** — bytes from disc 11.1 →
  13.2 MB, a seventh overlay identified inside the gate's 40s (C087, variance-checked over 3 runs).
- **Then it stopped translating.** A further ~6% removed (`cfg_dbg_generation`, `OtAttr::trackStore`)
  bought NO measurable frames (C089). The run reaches an identical point either way. ~29% of samples
  are outside the binary (driver/loader) and do not shrink, so this workload is likely no longer bound
  by our CPU — unproven. **Do not start another micro-optimisation round without a workload that is
  demonstrably CPU-bound in the code being changed.**
- **Reading a profile: percentages self-rebase.** After a fix, untouched entries' shares RISE because
  the denominator shrank. Only end-to-end throughput sizes a win (C086).
- Still unexamined: `cfg_dbg_generation` (~3.4%) and three of the five per-store hooks.

## Known framework warts (upstream, not ours to paper over)

- `RecompRegistry` names *per-overlay* override setters (`ov_a00_set_override`,
  `ov_game_set_override`) — Tomba!2-shaped members in a game-agnostic struct. Spyro passes `nullptr`.
- `GameHooks::SchedBody` enumerates Tomba!2's stage bodies by name.
- `GameConfig::guestMemset_gen` expects a game-specific fast-path body.

None of these block Spyro; they are recorded so nobody mistakes them for something Spyro must fill in.
