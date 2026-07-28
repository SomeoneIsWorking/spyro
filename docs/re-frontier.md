# RE Frontier — the ordered RE dependency chain toward a faithful BL2

Tracked by `tools/re_frontier.py` (consult it FIRST; update it in the SAME commit
that changes a step). This is the fine-grained companion to `docs/codemap.md`:
the codemap says *what subsystem exists*, this says *which ordered RE step is
real reverse-engineering vs a hack that jumped ahead*.

**Hard rule (no hacks / no fallbacks):** a `⛔ hack` status is DEBT, never an
acceptable resting state. It marks a shortcut standing in for absent RE and MUST
be removed as its real mechanism lands. `re_frontier.py hacks` is the debt list;
`re_frontier.py next` tells you the next RE-ready step.

**`re-verified` MEANS FAITHFUL to the real target — not "the mechanism runs."** A
step is `re-verified` only when its OUTPUT matches the real game/binary (look /
sound / behavior) on real data. An internal trace ("bytecode reached the call
site", "N rows attached") is a mechanism check, NOT faithfulness — if it runs but
the result doesn't match the real target, it is `re-partial` with the
faithfulness gap named. The user observes the running system; that observation
overrides any internal trace.

**Fail fast & loud:** a failure must surface loudly, never silently fall back —
unless the fallback IS intended behavior of the real target being reproduced.

Statuses: ✅ re-verified · 🟡 re-partial (honest gap) · 🔬 in-progress ·
⛔ hack (debt, must remove) · ⬜ todo · ➖ skip-by-design · ⏸ blocked (computed).

<!-- Machine-edited by tools/re_frontier.py add/set. Format: `## <area>` sections;
     each entry is `### <id> — <title>` followed by `- <field>: <value>` lines. -->

## boot

### boot.provision — Extract SCUS_942.28 from the disc + recompile it to C
- status: re-verified
- deps: 
- evidence: tools/ensure_recomp.py runs discdump + emit.py end to end; 621 functions emitted, hash-checked against the exe + recompiler sources + seed file. SYSTEM.CNF confirms BOOT=cdrom:\SCUS_942.28.
- where: tools/ensure_recomp.py, game/recomp_seeds.json
- gap: 
- notes: Spyro is a SINGLE executable: no \BIN\*.BIN code overlays, all data in WAD.WAD. Structurally simpler than psxport's Tomba!2 reference consumer.

### boot.crt0 — GameConfig crt0/boot group from the real crt0
- status: re-verified
- deps: boot.provision
- evidence: See claim C001: disassembly of entry 0x8005B8E0 maps 1:1 onto psxport's generic crt0_setup().
- where: game/core/game_config.cpp
- gap: 
- notes: 

### boot.guest-main — Reach the guest's own main() as recompiled code
- status: re-verified
- deps: boot.crt0
- evidence: See claim C002: headless backtrace shows main -> gen_func_80012204 -> ...800127C0 -> ...8001250C -> ...80016500 -> ...800163E4.
- where: game/core/game_hooks.cpp spyro_bootInit
- gap: 
- notes: 


## cd

### cd.chokepoints — Identify Spyro's libcd chokepoints for the native CD path
- status: re-partial
- deps: boot.guest-main
- evidence: libcd = stock Sony bios.c v1.86 (C004). Wired + confirmed by the running system: hle.cdInitHandshake=0x800653B4 (CD_init), hle.cdDataSync=0x800655A0 (CD_datasync), cfg->cdCommand=0x80064CEC (CD_cw, signature confirmed from the body). Plus the missing game->cd.overridesInit() call. Result: ZERO CD timeouts at boot (C005).
- where: game/core/game_config.cpp CD chokepoints group (all 0 today)
- gap: Commands now ACK, but that is NOT the same as reads returning correct bytes — no disc read has been verified to deliver correct data yet, because the boot does not get far enough to need one. cdReadPrim/cdFileLoad/cdAsyncRead remain 0. cdSync + cdReadSync also still 0: their handlers write 8 bytes at a1 as the PUBLIC CdSync/CdReadSync contract, while Spyro's CD_sync (0x800647A0) is the internal bios.c primitive whose signature is unconfirmed.
- notes: This is the CURRENT BLOCKER: the boot reaches guest main, then spins on 'CD timeout: CD_cw:(CdlSetmode/CdlSetloc)' because no native CD override is installed and the 0x1F801800 controller model is only partial.


## frame

### frame.native-loop — Take over the per-frame loop (OT/packet-pool GameConfig group)
- status: todo
- deps: cd.chokepoints
- evidence: 
- where: game/core/game_config.cpp per-frame group
- gap: Needs Spyro's display init (SetDefDrawEnv/SetDefDispEnv callers) + its per-frame buffer flip RE'd. Until then the guest owns its own loop and the per-frame GameConfig group is honestly 0.
- notes: 

### frame.vsync — Reimplement VSync faithfully and register it
- status: re-partial
- deps: cd.chokepoints
- evidence: libetc VSync (func_8005DBC4) delegates to a wait helper (0x8005DD0C) whose loop condition is [0x800749E0] < a0 — the vblank counter, frozen because no IRQ increments it. Overridden game-side in game/core/vsync.cpp: advance the counter toward the target, presenting+pacing one frame per vblank. Chose the HELPER over VSync itself so VSync's own return value and GPU polling keep running on the real recompiled body — nothing library behaviour gets reimplemented, only the missing hardware timebase.
- where: game/core/vsync.cpp; hle window 1 [0x8005B000,0x80063000) covers libetc
- gap: Boot now advances further than ever (SDL_GPU device comes up) then SIGSEGVs — docs/issues/0002. Only one wait has fired so far (target=0, 0 frames advanced), so present() has not actually been exercised yet; the pacing path is UNVERIFIED. Backtrace is truncated to 2 frames and needs a debug/ASan build to localise.
- notes: 


## harness

### harness.sbs — Stand up the differential (SBS) harness against an oracle
- status: todo
- deps: frame.native-loop
- evidence: 
- where: 
- gap: Phase 0 of the playbook wants the byte-compare harness up BEFORE owning any function. Not yet wired for Spyro.
- notes: 


## recomp

### recomp.overlays — Determine whether Spyro loads code overlays, and recompile them if so
- status: todo
- deps: boot.guest-main
- evidence: 
- where: tools/ensure_recomp.py (would need a WAD.WAD step), game/recomp_seeds.json (overlay_bases)
- gap: UNRESOLVED (docs/issues/0001). Public decomp projects describe 37 overlays; the disc has no per-overlay files, so they would be inside WAD.WAD or read by raw LBA. The recomp currently covers ONLY the resident executable.
- notes: Settle from a RUNNING port: PSXPORT_DEBUG=cd logs each load destination and an unresolved call fail-fasts with its address. Do not guess a base — a wrong overlay base emits a whole module at wrong addresses, which is garbage rather than an error.

