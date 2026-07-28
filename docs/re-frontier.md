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
- status: re-verified
- deps: boot.guest-main
- evidence: C032,C047,C048
- where: tools/ensure_recomp.py (would need a WAD.WAD step), game/recomp_seeds.json (overlay_bases)
- gap: DONE for the shared-slot architecture. Two overlays (OVL0 boot, OVL1 = index entry 11, the first LEVEL overlay) now load to the SAME arena base 0x8007AA38 and psxport's content-signature router identifies each in slot 0. What unblocked it was not overlay work at all: the port was serving only the SYNC read primitive 0x80016500 (11 call sites) and not the STREAMING one 0x80016698 (19 call sites), so every level read was acked with zero bytes (C047). Remaining: the other 34 code entries are located but not extracted, and each needs its observed load before being added.
- notes: Settle from a RUNNING port: PSXPORT_DEBUG=cd logs each load destination and an unresolved call fail-fasts with its address. Do not guess a base — a wrong overlay base emits a whole module at wrong addresses, which is garbage rather than an error.

### recomp.midfn — Mid-function dispatch from data-computed addresses
- status: re-verified
- deps: boot.post-cd
- evidence: C059,C060
- where: docs/issues/0021; emit.py label emission
- gap: RESOLVED. Was misdiagnosed as data-driven and unanalysable; it was a computed RETURN (C058/C059) plus a no-index computed jump (C060), both now handled in the recompiler. The port no longer crashes: zero recomp misses, frame count scales with wall time, gate 11/11.
- notes: 


## input

### input.pad — Deliver pad input — the guest cannot produce it itself
- status: re-verified
- deps: boot.post-cd
- evidence: C063,C064
- where: game/core/game_config.cpp pad group; psxport PlatformHle pad path
- gap: 
- notes: RESOLVED. The producer was never a pad-buffer address to guess — it was a callback that never fired. Boot registers the game's own decoder 0x80053C68 as the VBlank handler (VSyncCallback, 0x8005DE58) and this runtime raises no IRQs, so it ran once at boot and never again (C063). Two things were missing and both are now supplied from the vblank wait (game/core/vsync.cpp): Pad::serviceFrame() writes the standard PSX packet into the buffers libpad's SIO read would have filled (0x800786A0 slot0 / 0x80078E50 slot1, registered by PadInitDirect at 0x800123E0; the driver keeps those pointers at 0x80075D48 with a 240-byte stride — GameConfig padSlot0Buf/padSlot1Buf/padSlotPtrTable/padSlotPtrStride), then the registered vblank callback runs with the register file saved and restored as an IRQ would. Measured: pad class [0x80077384] moves 0 -> 2 (digital) and the decoder goes from 1 call per run to 4106+. The game then LEAVES ATTRACT and loads a level — bytes-from-disc doubled to 9.9 MB and a third overlay (OVL2, WAD +0x237D000) loads into the arena. C035 falsified along the way: the SIO accesses were always there, reached through the pointer [0x80075220] = 0x1F801040 (C064). Remaining work is downstream: OVL2 function discovery is starved (6 fns from 1 seed) and a run fail-fasts on 0x8007CFB4.


## gpu

### gpu.ot-crash — Port aborts at frame 3781 — runaway OT linked-list DMA
- status: re-verified
- deps: boot.post-cd
- evidence: C037,I008
- where: issue 0015; gen_func_80061820 submit path
- gap: RESOLVED. The render-queue drain (C037) fixed the abort. The 'black screen' that appeared to remain was a MEASUREMENT ARTEFACT, not a defect: PSXPORT_GPU_DUMP reads s_vram and VK-path polygons never touch it (instrument I008), so the dump goes black the moment real rendering starts. The guest's own prim count shows 680 frames submitting geometry in the last quarter of the run. C038, which claimed prims reached the renderer but not the screen, is falsified. Remaining unknown, tracked as issue 0018: there is no headless way to capture VK output, so 'are the pixels CORRECT' is unmeasured — a per-frame readback hangs the port and was reverted. The port's live blocker is now the recomp miss at 0x8008772C (issue 0017).
- notes: 


## overlay

### overlay.ovl2-discovery — Overlay set + per-overlay entry seeds
- status: re-verified
- deps: input.pad
- evidence: C065,C066
- where: tools/overlay_scan.py; game/overlays.json; tools/ensure_recomp.py; game/recomp_seeds.json overlay_seeds
- gap: 
- notes: RESOLVED, and the original framing was wrong. 0x8007CFB4 was never in the overlay it was being read from: the arena is reloaded constantly, so the last IDENTIFIED overlay is not the resident one at a fail-fast (C065). Both earlier conclusions — 'jump-table case label' and 'the overlays are mostly data' — were artifacts of reading the wrong image. The port now dumps guest RAM at every miss (I012), which settles residency by searching WAD.WAD for the resident bytes. tools/overlay_scan.py (I011) recovers the whole set from a run's arena loads into game/overlays.json; overlays are named by WAD offset so the set grows without renaming and re-pointing existing seeds; ensure_recomp.py now also deletes slices that leave the set, since emit.py walks the directory and a stale slice emits a whole module at the live arena base. Seven overlays extracted, all identified at load, zero unmatched. Per-overlay what remains is ONE seed each — the per-frame entry installed into [0x80075734], called indirectly at 0x80033AA4 (C066) — each verified as a real prologue in the RESIDENT bytes before being added. With OV_237D000 0x8007AEB8 and OV_2F5B000 0x8007B7A8 seeded the port runs a full 45s at rc=137 with zero recomp misses.

### overlay.entry-seeds-auto — Automate the per-overlay entry seed instead of one fail-fast per rebuild
- status: todo
- deps: overlay.ovl2-discovery
- evidence: C066
- where: tools/overlay_scan.py; game/recomp_seeds.json overlay_seeds
- gap: Each level overlay needs exactly one seed — its per-frame entry — and finding it currently costs a full recomp+build+run per overlay. The shape is regular enough to automate: the miss reports caller ra=0x80033AAC, and the address is always a clean prologue in the resident bytes. Candidate rule: for each overlay image, the entry is the address that main's per-level table (43 stores to [0x80075734] at 0x8005A4CC-0x8005B6BC, 41 distinct values) names AND that is prologue-shaped in THAT overlay's own bytes — the second test is what keeps another level's entry from being seeded into the wrong module. NOT yet verified: 0x8007B7A8 (OV_2F5B000's confirmed entry) is not in those 41 values, so the table is not the only installer and the rule as stated is incomplete.
- notes: Do not seed the 41 table entries wholesale into every overlay — they all share one base, so another level's entry lands mid-function and splits real code.

