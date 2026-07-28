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

### boot.post-splash — Get past the boot splash into game init
- status: re-partial
- deps: cd.chokepoints, frame.vsync
- evidence: Root-caused. The stall is a SPIN, not slow init (claim C008): sampled mem_w32 addresses repeat at 0x801FFDB0/B4 — adjacent slots below the stack top 0x801FFFF0, i.e. one frame re-pushed — with the profile pinned to func_800163E4 <- 80016500 <- 8001250C <- 800127C0 <- main. It spins because no CD read ever delivers data.
- where: func_800163E4 (game code — low addresses are game, libraries are high in this link order)
- gap: Blocked on cd.reads: Spyro uses stock libcd (Setloc-then-read), so the LBA is not an argument to the read and psxport's cd_read(blocks,lba,buf) contract does not fit. See docs/issues/0003.
- notes: 

### boot.post-cd — Get past the post-CD stall at func_8005CBB0
- status: re-verified
- deps: cd.reads
- evidence: Root-caused and fixed. func_8005CBB0 polls BIOS event handle 0xF1000000; PSXPORT_DEBUG=ev (new framework tracer) showed it was opened on class 0xF0000009 spec 0x20. Two things were needed: the class in GameConfig::irqEventClasses, AND delivery from the vblank wait — the framework's own delivery point (native_step_frame) never runs while the guest owns its frame loop. Verified: 226 -> 436 frames and 18 distinct frame-occupancy values (was ~2), so content genuinely progresses (C023).
- where: func_8005CBB0, func_80014564
- gap: 
- notes: 


## cd

### cd.chokepoints — Identify Spyro's libcd chokepoints for the native CD path
- status: re-verified
- deps: boot.guest-main
- evidence: libcd = stock Sony bios.c v1.86 (C004). Wired, each with its SIGNATURE CONFIRMED from the recompiled body rather than the name it prints: hle.cdInitHandshake=0x800653B4 (CD_init), hle.cdDataSync=0x800655A0 (CD_datasync), cfg->cdCommand=0x80064CEC (CD_cw: a0&255 indexes the command tables, a1=param, a2=result), cfg->cdSync=0x800647A0 (CD_sync: a0 mode in r21, a1 result in r22; it polls via VSync(-1) waiting on a ready flag only a CD IRQ would set). Plus the missing game->cd.overridesInit() call. 4 plat-hle primitives installed; zero CD timeouts; a stack profile that sat in CD_sync now shows it gone.
- where: game/core/game_config.cpp CD chokepoints group (all 0 today)
- gap: 
- notes: This is the CURRENT BLOCKER: the boot reaches guest main, then spins on 'CD timeout: CD_cw:(CdlSetmode/CdlSetloc)' because no native CD override is installed and the 0x1F801800 controller model is only partial.

### cd.reads — Serve stock-libcd data reads (Setloc-tracking read path)
- status: re-partial
- deps: cd.chokepoints
- evidence: Loader identified and owned: func_80016500(a0=base LBA 37, a1=dest, a2=len, a3=byte offset), confirmed by logging every call (C019, C020). Serving it reads sector a0+a3/2048 into dest and super-calls, so the guest's own wait/bookkeeping stay intact. Real data now moves with correct per-request offsets: 2048 / 262144 / 14336 / 110592 bytes to three distinct destinations.
- where: game/core/ (new), GameConfig cd group
- gap: Frames only 218 -> 226: the reads are served but the game still does not progress visually. Content correctness is UNVERIFIED — nothing yet checks the loaded bytes against what the guest expects (no checksum observed). Next: verify content (compare a loaded region against the disc independently) and find what the guest does with it after load, since fixing the offset did not unblock it.
- notes: 

### cd.loader-content — Verify the loader writes the RIGHT bytes
- status: re-partial
- deps: cd.reads
- evidence: The loaded content is structurally valid: the words at heapBase+0x174 are all exactly 0x800-aligned sector offsets (1196/28645/26/28671), i.e. a WAD index — precisely what the archive's first sector should hold (C025). So the loader is placing plausible archive data, and the earlier 'writes wrong bytes' suspicion is NOT supported.
- where: 
- gap: Origin of the garbage call target 0x8007ABAC is still unexplained. Not the index words themselves (they are valid). More likely the guest indexes this table with a value the port has not got right, or reads a pointer the port never populated. Also unexplained: a later load logs the same region as ALL ZEROS, so something does overwrite or re-read it differently.
- notes: 


## frame

### frame.native-loop — Take over the per-frame loop (OT/packet-pool GameConfig group)
- status: todo
- deps: cd.chokepoints
- evidence: 
- where: game/core/game_config.cpp per-frame group
- gap: Needs Spyro's display init (SetDefDrawEnv/SetDefDispEnv callers) + its per-frame buffer flip RE'd. Until then the guest owns its own loop and the per-frame GameConfig group is honestly 0.
- notes: 

### frame.vsync — Reimplement VSync faithfully and register it
- status: re-verified
- deps: cd.chokepoints
- evidence: libetc VSync (func_8005DBC4) delegates to a wait helper (0x8005DD0C) whose condition is [0x800749E0] < a0 — the vblank counter, frozen with no IRQ to increment it. Overridden game-side (game/core/vsync.cpp): advance the counter toward the target, presenting+pacing one frame per vblank. Chose the HELPER over VSync itself so VSync's own return value and GPU polling still run on the real recompiled body. VERIFIED on a real run: counter advances (target=7 -> counter=7 (+1 frames), target=8 -> counter=8 (+1 frames)) across 16 waits, no crash, SDL_GPU device + headless renderer up.
- where: game/core/vsync.cpp; hle window 1 [0x8005B000,0x80063000) covers libetc
- gap: 
- notes: 


## harness

### harness.sbs — Stand up the differential (SBS) harness against an oracle
- status: todo
- deps: frame.native-loop
- evidence: 
- where: 
- gap: Phase 0 of the playbook wants the byte-compare harness up BEFORE owning any function. Not yet wired for Spyro.
- notes: 

### harness.gate — Boot-progress regression gate (tools/gate.sh)
- status: re-verified
- deps: boot.post-cd
- evidence: tools/gate.sh runs the port headless and asserts seven measurable properties: frames >=300, DISTINCT frame occupancies >=8 (catches a regression to a held screen, which frame count alone cannot — it was 218 for a static splash), loader invocations, bytes actually read from disc, completions delivered, and zero recomp-misses / refused HLE registrations. Caught a real recomp-MISS on its FIRST run that manual log reading had missed.
- where: 
- gap: This is a BOOT-PROGRESS gate, not the byte-exact SBS differential the playbook asks for; it cannot prove the native path matches the substrate instruction-for-instruction. harness.sbs remains outstanding.
- notes: 


## recomp

### recomp.overlays — Determine whether Spyro loads code overlays, and recompile them if so
- status: re-partial
- deps: boot.guest-main
- evidence: Route (b) VALIDATED (C031): overlay load bases are readable from the static image. At call site 0x80012924 the loader's dest is loaded from [0x800113A0], and that word in SCUS_942.28 is 0x8007AA38 — exactly the base observed from the running port. The loader has 8 static call sites, so bases can be recovered without exercising 35 gameplay paths. Index enumeration (C029) already gives offsets/lengths.
- where: tools/ensure_recomp.py (would need a WAD.WAD step), game/recomp_seeds.json (overlay_bases)
- gap: OVL0 is recompiled and wired into the router's arena slot; the framework side is DONE (issue 0013 resolved — psxport already models a shared arena, identified by content signature). 36 code overlays are now LOCATED in WAD.WAD: entry 2 plus odd entries 9..77, alternating code/data per level, confirmed by both an opcode-share score and a structural prologue/jr-ra test that agree on all 79 entries (C033, tools/wad_index.py). What blocks recompiling them is their LOAD BASE, and C034 shows it is NOT recoverable statically: they contain zero internal direct calls (no jal targets above text_end) so there is nothing to triangulate from, and their embedded constants spread over ~1.6MB. Do NOT assume the arena 0x8007AA38 because OVL0 lands there. The settling observation is one line: PSXPORT_DEBUG=cdq logs a3 (WAD byte offset) next to dest, so reaching a level names that level's base outright. So the real next step is GETTING PAST THE TITLE SCREEN — pad input — not more overlay analysis.
- notes: Settle from a RUNNING port: PSXPORT_DEBUG=cd logs each load destination and an unresolved call fail-fasts with its address. Do not guess a base — a wrong overlay base emits a whole module at wrong addresses, which is garbage rather than an error.


## input

### input.pad — Deliver pad input — the guest cannot produce it itself
- status: todo
- deps: boot.post-cd
- evidence: C035
- where: game/core/game_config.cpp pad group; psxport PlatformHle pad path
- gap: C035 (exhaustive, not sampled): Spyro's game code has ZERO JOY-register accesses and ZERO pad-library calls across the resident text and all 36 code overlays, and the linked libapi pad chain (InitPAD/StartPAD/StopPAD/PAD_init) is dead — its head has no callers and is never address-taken. No code jumps directly to a BIOS vector either, so the trampoline census is complete. Therefore input CANNOT be made to appear by running more guest code; the BIOS/HLE layer has to supply it. GameConfig's pad group (padSlot0Buf/padSlot1Buf/padDriverFn/padSlotPtrTable) is all zero, so psxport currently delivers nothing. OPEN QUESTION and the actual next step: find WHERE the guest reads its button state from, given it never asks the hardware for it. Candidates to check against the binary, in order: the BIOS kernel's own pad buffer read as ordinary memory; a vblank handler installed via HookEntryInt 0x8005E4F8 (2 callers); or the scratchpad 0x1F800000 region. Do NOT wire a padSlot0Buf address until it is derived — a guessed guest address is the failure this port refuses.
- notes: 

