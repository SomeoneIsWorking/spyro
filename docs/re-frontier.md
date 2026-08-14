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

### cd.pc-owned-stock-libcd — OWN Spyro's loader natively — do NOT move the CD path down to hardware-level handlers
- status: todo
- deps: boot.post-cd
- evidence: C074
- where: game/core/cd_queue.cpp; game/core/game_config.cpp cd group; external/psxport/runtime/recomp/cd_override.cpp
- gap: DIRECTION SETTLED; the counts in the previous version of this entry were badly stale. The user's directive is that the goal is more PC-DRIVEN, not more faithful: at the hardware layer the guest's libcd AND its loader both still run as recompiled MIPS, so moving down GIVES UP ownership. Up is the direction — replace guest bodies with native ones. STATUS as measured: 35 overrides registered, of which 15 are FULL native bodies with no super-call, each per-call differentially verified (gate: 120 verified calls, 0 divergences). Both game-level CD loaders are owned at the right layer — they serve the bytes from the disc natively, then super-call the guest's own wait/bookkeeping (C106; issues 0003/0004/0007/0008/0010/0012 all resolved). The remaining super-call wrappers are observation-only: 9 in cd_queue.cpp and 1 in vsync.cpp. (game/core/level_load_probe.cpp and its 5 probes are GONE — deleted, as this entry used to ask for; do not go looking for that file.) NEXT: ownership's frontier is non-leaf bodies (see own.non-leaf). Note a spin-loop body like func_80016500 CANNOT be validated by the per-call differential — the recompiled side's spin calls the queue service and mutates state a native body would never touch, so ndiff would report divergence for a correct reimplementation. Own its callees first.
- notes: BLOCKED ON VERIFICATION, not on effort. psxport's methodology is that each native reimplementation is gated byte-exact against the substrate it replaces (sbs.cpp); this port has never wired that harness (frontier harness.sbs, outstanding all session). Replacing a loader body natively without it is unverifiable, which CLAUDE.md's own 'verify on real data, distrust green' rule forbids. So harness.sbs is the real prerequisite for the whole native-ownership programme, and it is what to build next — not another CD layer decision.


## frame

### frame.native-loop — Take over the per-frame loop (OT/packet-pool GameConfig group)
- status: re-partial
- deps: cd.chokepoints
- evidence: C068,C069,C073,C150,C151; plus, THIS step: main() 0x80012204 disassembled (its epilogue 0x8001228C-0x8001229C is UNREACHABLE, so it never returns); the loop reproduced natively in game/core/frame_loop.cpp and PROVEN to run — PSXPORT_FNTRACE ra field is 0x80012238/0x80012284 (the guest's own call sites) with the loop off and DEAD0000 (the port's top-level sentinel) with it on, same first frame 436 and same call counts. Cadence measured twice, on two different builds: 21551 presents/vblanks against 11333 calls of 0x8001ED5C over 75s (pre-pace-parity binary), and 28218 against 15053 over 60s with PSXPORT_NOPACE=1 on the current one — 1.86-1.90 vblanks per drawn frame either way, which is the number the present-ownership design has to respect.
- where: game/core/frame_loop.cpp (PSXPORT_SPYRO_FRAME_LOOP=1); game/core/game_hooks.cpp spyro_bootInit; the render seam it calls is game/render/render_frame.cpp
- gap: OWNED: the outer loop. NOT owned: present/pace (still vsync.cpp, one per VBLANK — moving it into the loop 1:1 would HALVE the present rate, because Spyro's logic frame is ~30Hz and the display 60Hz) and the render driver 0x8001ED5C, which is the actual native-graphics seam. THE PREVIOUS TEXT OF THIS ENTRY WAS WRONG ABOUT THE DIRECTION and is corrected here: it said the blocking item was generalising psxport's per-frame GameConfig group to a per-parity-selector model. That framework change is NOT needed and must not be attempted for this reason — the group only feeds native_step_frame, which is UNREACHABLE in this port. native_step_frame is reached only from game_main (via native_crt0 <- native_boot_run <- BootStub::run) and from dc_step_frame; BootStub::run has ZERO callers in spyro (Spyro boots one executable, no SCEA stub) and dc_boot_init never returns here, so even PSXPORT_SELFTEST cannot reach it (measured: PSXPORT_SELFTEST=startgame hangs with a stack of dc_boot_init -> gen_func_80012204 -> gen_func_8001ED5C). Tomba!2 IS the port that calls BootStub::run, which is why its native renderer hangs off native_step_frame. C073 stays correct and its conclusion (leave those GameConfig fields 0) is now permanent, not provisional.
- notes: The loop is a 15-instruction shell: 0x8005B988 (empty static-ctor walk) + 0x800127C0 (boot init), then forever { [0x80075868]=0; 0x8003385C (update); step=clamp([0x80075760],2,4) -> [0x800756CC]; [0x80075868]=1; [0x80075760]=0; if ([0x8007579C]==0) 0x8001ED5C (render) }. Every gp-relative address is cross-confirmed by a symbol in a second decompiled function: 0x80075868 and 0x80075760 both appear in the vblank callback 0x80053C68 (which INCREMENTS 0x80075760 and reads 0x80075868 to decide whether to latch pad EDGES), 0x800756CC is the frame step 0x8003385C consumes, 0x8007579C is zeroed by 0x8003385C on entry. Decompiles: scratch/decomp/frameown.c (0x800127C0, 0x8003385C, 0x80053C68, 0x8005B988) and scratch/decomp/frameloop.c (0x80012204, 0x8001ED5C).

### frame.vsync — Reimplement VSync faithfully and register it
- status: re-verified
- deps: cd.chokepoints
- evidence: libetc VSync (func_8005DBC4) delegates to a wait helper (0x8005DD0C) whose condition is [0x800749E0] < a0 — the vblank counter, frozen with no IRQ to increment it. Overridden game-side (game/core/vsync.cpp): advance the counter toward the target, presenting+pacing one frame per vblank. Chose the HELPER over VSync itself so VSync's own return value and GPU polling still run on the real recompiled body. VERIFIED on a real run: counter advances (target=7 -> counter=7 (+1 frames), target=8 -> counter=8 (+1 frames)) across 16 waits, no crash, SDL_GPU device + headless renderer up.
- where: game/core/vsync.cpp; hle window 1 [0x8005B000,0x80063000) covers libetc
- gap: 
- notes: 

### frame.own-render-driver — Own the per-frame RENDER DRIVER 0x8001ED5C — the actual native-graphics seam
- status: re-partial
- deps: frame.native-loop
- evidence: C151,C158,C162,C167,C177
- where: game/render/render_frame.cpp SpyroRenderer::drawFrame() — the psx_render/native branch lives here now (moved out of frame_loop.cpp, C162); game/render/scene.cpp holds the scene identity (stage arms + FIELD layer list); the leg comes from the framework's per-Core RenderMode, PSXPORT_RENDER_PSX=1 = reference
- gap: This, not the outer loop, is where native graphics lands. 0x8001ED5C does three separable things and they should be owned in this order: (1) the OT/packet-pool reset above its stage switch, fully RE'd in C151, written GAME-SIDE against the two draw envs 0x80076EE0/0x80076F64 — do NOT route it through psxport's per-frame GameConfig group, which is unreachable here (C158) and the wrong shape (C073); (2) the display tail — DrawSync(0) 0x8005F764, the >=2-vblank throttle built from VSync 0x8005DBC4 against the stamps [0x80075950]/[0x80075954], PutDispEnv 0x80060030(env+0x5C), PutDrawEnv 0x8005FDD8(env), DrawOTag 0x8005FD64(0x80016784(0x800)). Owning THIS is what unblocks moving present/pace out of vsync.cpp, because it is where the frame's only vblank wait lives; (3) the stage arms. MEASURED FIRST TARGET: with the native branch armed the port aborts on stage 13, whose handler [0x80078D78]!=3 selects 0x8007CEE4 — an address in the OVERLAY ARENA (0x8007AA38+), not MAIN — so the first scene to port is overlay-resident and depends on which overlay is loaded. Stage 0 (the FIELD) is the gameplay arm and its 10-entry layer list is transcribed in game/render/scene.cpp kFieldLayers. MEASURED WHICH SCENES A REAL RUN REACHES (C162/I046, PSXPORT_DEBUG=scene, 20 s reference boot, 8905 drawn frames): only TWO identities occur, alternating — stage 13 (6007 frames, first) and stage 0 / FIELD (2898). So the first two producers to write are those two, in that order, and the field one can be reached today by driving in on the reference leg and flipping with the REPL `renderpsx off`. The projection a producer needs is already available at the first drawn frame: geomValid=1 ofx=256 ofy=120 H=341 (C156).
- PROGRESS 2026-08-06 (C167): parts (1) and (2) are DONE game-side, and the FIRST PRODUCER of part (3) has landed.
  * (1) the complete driver head + draw-env programming: game/render/frame_env.cpp `nativeFrameBegin` — the flip, env-owned OT/front/pool pointers, packet-count reset, transient-actor arena at pool+0x1C000, and camera-matrix call 0x80033C50 verbatim from 0x8001ED5C, then GP0 E3/E4/E5/E1/E2/E6 and the isbg fill. The former packet-only interpretation was falsified when stage-13's text builder used that arena and a null cursor produced an unmapped write.
  * (2) the display tail: `nativeFrameEnd` — the >=2-field throttle on the game's own stamps [0x80075950]/[0x80075954], spending fields through game/core/vsync.cpp's `spyro_deliver_field` (ONE definition of a field for both legs), then GP1(05) display start from the env's DISPENV. NOT ported: PutDispEnv's GP1 06/07/08 arms (the guest caches them and Spyro's two envs differ only in `disp`), and DrawSync(0) (no native DMA to drain). MEASURED WHY THIS WAS COMPULSORY: without it the native leg ran 1556 drawn frames and PRESENTED NOTHING (`PSXPORT_DEBUG=rqflush,presentskip` shows an unbroken run of flushes with no present between them); with it, 3548 presents of which 1942 REBUILD_GEOM.
  * (3) FIRST PRODUCER: game/render/fx_title_menu.cpp — stage 13's front-end sprite layer, a port of guest 0x8007CD38 driven by 0x8007CEE4's mode-0 arms. Gate in C167.
  * (4) SECOND PRODUCER, verified semantic layer only: game/render/fx_sprite_queue.cpp owns 0x8001E6B8's text construction plus the live screen-space/flat-bit01 class of 0x80022A2C. At stage-13 timer 171 it matches the guest's isolated output: 551 candidates, 232 accepted = 25 triangles + 207 quads. C177's earlier pixel claim remains falsified because those captures exercised the prior title producer. The handler still refuses shipping state 2 because its separate three-layer animated pass 0x80023AC4 remains unowned.
  * (5) THIRD PRODUCER, input/codec/projection/normal-face slice: game/render/fx_paired_actor.cpp resolves all three 0x80023AC4 animation layers and their guest /16 pose blend, then projects the 238 resolved vertices through a side-effect-free fixed-point RTPS evaluator over the invocation's explicit DR0/1 and CR0..7/24..26 snapshot. game/render/paired_actor_decode.cpp owns the pure normal-stream decoder and acceptance/content resolver. Across 384 live invocations, pose/projection remains 241/241 target RTPS and 238/238 vertices with zero mismatches; all 355 source candidates close, and emitted packet content matches source/fragment, class, XY, RGB, attributes, opcode and semitransparency. Owner-qualified checkpoints at 0x800257A0/0x800258B0 now scan all 288 local bins around the guest splice: every 172–202 packet set joins and matches its numeric bin/order, with zero topology or clearing failures, while corrupt-tail/link/bin selftests reject. This replaced the accept-all resolver, speculative GTE replay, and two invalid late-snapshot instruments. **Still open:** placement relative to a pre-existing global OT chain, native face submission, and the separate alternate/status-plane clipping arm.
- NEXT, in the abort's own order: (a) verify/model global-OT append placement, then add normal face emission and RenderQueue ownership for 0x80023AC4; dynamically reach and verify the separate alternate/status-plane clipping arm before calling the producer complete (resolved XYZ and SXY/SZ are exact); (b) stage 13 modes 1 and 2 of 0x8007CEE4 (page-driven text screens, 3-slot save screen) — RE'd but never reached by a drivable run; (c) the 3D backdrop shared by stage 13 and the FIELD: 0x800521C0, 0x8001F158, 0x8001F798, 0x800258F0, 0x8004EBA8.
- notes: The five renderers this repo has already named all hang below this function: EmitActorDrawList 0x8001F798 and EmitSecondaryActorPrimitives 0x80020F34 via the stage-0 layer 0x80019698; EmitStaticActorMeshList 0x8004EBA8 via the stage-4/5 arm 0x8001CA38; RenderWorldChunks 0x800258F0 via the stage-14 arm 0x8001E9C8; RasterizeSpritePrimQueue 0x80022A2C via the stage-15 arm 0x8001EB80 (tools/callgraph.py --from 0x8001ED5C --to <addr>).


## harness

### harness.sbs — Stand up the differential (SBS) harness against an oracle
- status: re-verified
- deps: 
- evidence: I019,C075
- where: 
- gap: 
- notes: UNBLOCKED by a different route, and the old dependency on frame.native-loop was the wrong shape. psxport's SBS harness is whole-run and its stepper (dc_step_frame) hardcodes the first consumer's addresses (GAME_ENTRY 0x8010637C, TASK0_ENTRY 0x801fe00c), so it cannot be wired for Spyro without a framework generalisation. But full-run SBS is not what validating ONE replacement needs. native_diff.cpp (PSXPORT_NDIFF=n) does a PER-CALL differential: snapshot RAM+scratchpad+registers, run the native body, rewind, run the recompiled body, compare. It is stronger for this purpose than a whole-run diff — it asks 'does this function, from THIS exact input state, produce exactly what the substrate produces' and answers on every call. Validated both directions (I019): it catches a one-byte perturbation, and it caught a real inequivalence in the first native function (an unreproduced $at clobber) that reading the code did not. The whole-run harness is still worth having eventually for cross-function drift; it is no longer the prerequisite for owning functions.

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

### gpu.upload-only-screens — Upload-only screens (logos, FMV stills) do not reach the display
- status: re-verified
- deps: 
- evidence: C099,C100,C097,C104,C105
- where: external/psxport/runtime/recomp/gpu_vk.cpp render_geom/present; game/core/game_config.cpp preserveVramBackdrop
- gap: CLOSED. These screens had TWO stacked faults. (1) render_geom's `total == 0` early return cleared s_vram_tex unconditionally, above every other backdrop control, so preserveVramBackdrop (C100) could never reach the very frames it was added for — fixed, C104, issue 0029. (2) With them visible, they were 24bpp (GP1(08) bit 4, set frames 1-436) decoded as 1555 — fixed, C105, issue 0016. Both decoders of the display region now honour the bit: the present shader and the CPU shot/readback. Verified by looking at the pixels — frame 300 renders the correct Universal Interactive Studios logo at full width. Gate 14/14.
- notes: Two independent faults on the same screens. Fix the visibility first — a 24bpp fix cannot be verified against a black screen.

### gpu.native-depth — Native per-vertex depth from the GTE tap
- status: re-partial
- deps: 
- evidence: C128,C126
- where: external/psxport/tools/recomp/emit.py vertex_pz_stores; runtime/recomp/gte_beetle.cpp gte_hold_pz/gte_record_pz/gte_copy_pz; proj_prim.cpp
- gap: MECHANISM RE-VERIFIED, COVERAGE IS NOT. Where a primitive's vertices resolve, they resolve exactly: sampled frames reach 210/210 prims with miss=0, and the rendered image is unchanged from before depth was enabled (C126), which is the correct result — the game's own painter order is still right for its own camera, so real depth only changes the picture once the camera moves or widens.
- notes: Do NOT keep adding tap rules hoping for a threshold effect. The next real gain is render.own-geometry-family.


## overlay

### overlay.ovl2-discovery — Overlay set + per-overlay entry seeds
- status: re-verified
- deps: input.pad
- evidence: C065,C066
- where: tools/overlay_scan.py; game/overlays.json; tools/ensure_recomp.py; game/recomp_seeds.json overlay_seeds
- gap: 
- notes: RESOLVED, and the original framing was wrong. 0x8007CFB4 was never in the overlay it was being read from: the arena is reloaded constantly, so the last IDENTIFIED overlay is not the resident one at a fail-fast (C065). Both earlier conclusions — 'jump-table case label' and 'the overlays are mostly data' — were artifacts of reading the wrong image. The port now dumps guest RAM at every miss (I012), which settles residency by searching WAD.WAD for the resident bytes. tools/overlay_scan.py (I011) recovers the whole set from a run's arena loads into game/overlays.json; overlays are named by WAD offset so the set grows without renaming and re-pointing existing seeds; ensure_recomp.py now also deletes slices that leave the set, since emit.py walks the directory and a stale slice emits a whole module at the live arena base. Seven overlays extracted, all identified at load, zero unmatched. Per-overlay what remains is ONE seed each — the per-frame entry installed into [0x80075734], called indirectly at 0x80033AA4 (C066) — each verified as a real prologue in the RESIDENT bytes before being added. With OV_237D000 0x8007AEB8 and OV_2F5B000 0x8007B7A8 seeded the port runs a full 45s at rc=137 with zero recomp misses.

### overlay.entry-seeds-auto — Automate the per-overlay entry seed instead of one fail-fast per rebuild
- status: re-verified
- deps: overlay.ovl2-discovery
- evidence: C066,C067
- where: tools/overlay_scan.py; game/recomp_seeds.json overlay_seeds
- gap: 
- notes: DONE. The rule works and the earlier doubt was my own bad measurement: I had reported that the confirmed entries were NOT in main's install table, which was wrong — the value-extraction scan was grabbing a neighbouring store's lui/addiu pair. Redone correctly, the table has 43 store sites yielding 36 DISTINCT addresses, matching the 36 code overlays of C033 and the ~37 the decomps describe, and every confirmed entry is in it. An address is claimed by an overlay only when it is prologue-shaped (addiu sp,sp,-N) in THAT overlay's own bytes, which is what stops another level's entry being seeded into the wrong module — all overlays share one base, so 35 of 36 would otherwise land mid-function. The test separates cleanly: each level overlay claims exactly one (OV_237D000 0x8007AEB8, OV_2F5B000 0x8007B7A8, OV_502F800 0x8007CFB4), the two small data-only reads claim none, and OV_B83800 claims two. tools/overlay_scan.py derives them into game/overlays.json; ensure_recomp.py merges them with the hand-reasoned seeds into generated/.recomp_seeds_merged.json and hashes them into the recomp identity so a newly-derived entry cannot leave generated/ looking current. The hand file's overlay_seeds is now empty by design. 0x8007CFB4 — the address that cost a whole wrong diagnosis — is now supplied automatically.


## input

### input.start-skip-sequences — Start skips logos, loading overlays, and scripted sequences
- status: re-partial
- deps: input.pad
- evidence: C110; issue 0027; generated `0x800127C0`; docs/findings/start-skip-map.md
- where: game/core/boot_skip.{h,cpp}; game/core/cd_queue.cpp `lp_800127C0`; game/core/vsync.cpp `deliver_field`; `PSXPORT_DEBUG=bootskip,skipmap`
- gap: BOOT COMPLETE, BROADER STEP PARTIAL. Boot uses a fresh edge to advance only its exact 0xD2 presentation clock. Title/attract keeps its legitimate guest transition. Stage mode 14 is now classified as recorded/demo playback and ALREADY handles Start/Cross in guest `0x800331AC` by accelerating its cursor toward the natural completion writer `0x8002D440`; adding a native transition would duplicate it. Observed stage13/sub3 phases are required streaming/I/O, not a presentation hold, and remain unmodified. NEXT: find an independently timed post-load overlay or another scripted-sequence family, then trace its natural completion writer. Never pulse Start across modes 0/2 gameplay, where it means UI/pause.
- notes: `skipmap` has a negative denominator every 600 fields and reports every edge/state transition uncapped. Boot classification uses the dynamic lifetime of `0x800127C0`, not a frame threshold.


## ownership

### own.next-targets — Own more guest functions natively, each gated by PSXPORT_NDIFF
- status: re-verified
- deps: harness.sbs
- evidence: C075,C080,C081,I019,I020,I021
- where: game/core/native_rand.cpp is the pattern; game/core/ observation wrappers are the candidates
- gap: 
- notes: PHASE DONE: the high-caller LEAF work is complete. 15 native bodies (was 1), ~834 static call sites, 600 verified calls per run at NDIFF=40, zero divergences, re-checked every gate run. Four are GTE bodies, owned without reimplementing the GTE — scalar logic native, COP2 via the platform's gte_op/gte_read_data/gte_write_ctrl, integer divide via cpu_div (C++ division is UB exactly where MIPS defines behaviour: /0 and INT_MIN/-1). own_candidates.py now hides owned functions and its best remaining LEAF has 15 callers, down from 136, so this seam has given what it has to give. CAVEAT on 'exhausted': the caller count is STATIC, so indirect calls are invisible and a low count is not proof of coldness — a runtime call histogram would be the honest next measurement. Continuing means non-leaf functions, which need their callees owned first; see own.non-leaf.

### own.non-leaf — Own NON-LEAF functions, bottom-up from the leaves already owned
- status: todo
- deps: own.next-targets
- evidence: C081,C082,I019,I022
- where: game/core/native_*.cpp; tools/own_candidates.py --all
- gap: REFRAMED BY MEASUREMENT. A host-PC profile (C082) says guest code is only 4.5-4.9% of CPU time; 95.5% is the port's own runtime. So further native ownership buys CORRECTNESS and architecture, not speed — a fine goal, but it should be pursued for that reason and not sold as performance work. It also invalidates the selection method: the hottest guest function (0x800258F0, 1.74%) has just TWO static callers, so own_candidates.py's caller ranking would never have surfaced it, and none of the 15 already-owned bodies appears in the profile at all. Pick future targets from the PROFILE (tools/prof_hot.py) when the goal is speed, and from the static queue only when the goal is coverage.
- notes: Before assuming the leaf work bought anything, MEASURE it: static caller counts ignore indirect calls, and nothing here has profiled the port. A runtime call histogram would say which of the 15 actually run hot and whether the next tier is worth owning at all.


## perf

### perf.diagnostics-overhead — The logger costs ~6% of CPU with logging switched OFF
- status: re-verified
- deps: 
- evidence: C083,C084,C085,C088,C089,I022,I023
- where: lucent (external, the user's own library); external/psxport/runtime/recomp/cfg.cpp; Core::wwatch_check
- gap: 
- notes: DONE, and STOP HERE unless a CPU-bound workload appears. Three fixes landed, all measured: lucent channel_enabled 6.06%->0.33% (fixed in the shared library); the per-store watch hooks ~4.9% (inlined armed test); the generation-counter chain ~6% (trackStore -> cfg_dbg_generation -> bootstrap_once). Frames 16508 -> ~18700, seventh overlay reached. BUT C089 is the finding that should govern what happens next: the LAST ~6% bought no measurable throughput at all, and the run reaches an identical point either way. ~29% of samples sit outside the binary (driver/loader) and do not shrink. Further micro-optimisation of this workload is optimising something that is not the constraint. TWICE in this work a fast path silently never engaged (lucent's early return, trackStore's statics vs members) and BOTH were invisible to reading and obvious to re-profiling — never accept an optimisation on the strength of the diff.


## render

### render.own-geometry-family — Own the hand-written assembly geometry renderers (the gate for widescreen AND 60fps)
- status: re-partial
- deps: gpu.native-depth
- evidence: C127,C129,C130,C136,C137,C138,C139,C140,C141,C176,C177
- where: 0x8004EBA8 (understood at instruction level), 0x800258F0 (9 vertex sites traced), + 17 more sharing the fixed-area register-save idiom
- gap: OWNERSHIP IS BACK ON WIDESCREEN'S CRITICAL PATH, but for a different and much better-defined reason than before. It is NO LONGER needed for clip bounds or OFX: those are handled by running the five contributing renderers interpreted with their bound immediates patched in guest RAM, which is bit-identical to the recompiled body (C139) and measurably recovers 9-18% more geometry per call at 16:9 (C142). What IS blocked is everything downstream of 2D-vs-3D DISCRIMINATION — psxport's 2D widen shifts the WHOLE FRAME a second time at this port's ~2.5% depth coverage (C143), and the uncovered-margin strip (issue 0039) is in the same class. That discrimination rides on per-primitive DEPTH, and depth is exactly what an OWNED body can emit and an interpreted one cannot. So own these for DEPTH; pick the target by which renderer's vertices are most needed, starting with 0x80020F34 (the ground) since it covers the most screen area.
- notes: RasterizeSpritePrimQueue 0x80022A2C is measured at its game-state input boundary across all four variants (C176). Its screen-space flat-bit01 subset now matches the guest's exact candidate/accepted face census at stage-13 timer 171; world-space records and the other three variants still need visibility, transforms, colour/depth and queue emission owned. C177 remains falsified and is not evidence for this result.

### render.projection-constants — Wire Spyro's native projection constants (libgte SetGeomOffset/SetGeomScreen)
- status: re-verified
- deps: 
- evidence: C156
- where: game/core/game_config.cpp .hle.setGeomOffset/.setGeomScreen; guest leaves 0x80062618 (CR24/CR25) and 0x80062638 (CR26); call site FUN_800127c0 at 0x80012818/0x80012824; framework side external/psxport/runtime/recomp/proj_params.cpp + sync_overrides.cpp
- gap: DONE. This is the FIRST step of a native renderer and it blocked every later one: ProjParams::geomValid() was false, so any native producer would abort in requireGeom() on its first frame. Spyro's OWN stated projection is OFX=256 OFY=120 H=341 (half of its 512x240 display) — not libgte's 160/120 and not Tomba!2's H=350; the values are read at the call site where the game states them, never out of the GTE at draw time, so this is not a tap. Measured live: geomValid()=1 from boot init, and the native handlers reproduce the recompiled bodies' entire effect (CR24=0x01000000, CR25=0x00780000, CR26=0x00000155, a0/a1 shifted in place).
- notes: BLIND SPOT to carry into the next step: two GAME functions write CR24/CR25 INLINE without calling the leaf (FUN_80022a2c, FUN_80023384). Both restore the same 256/120 pair, and each has one path that takes OFX/OFY from a per-view struct at +0xC/+0x10 — ProjParams cannot see either, since it only records what passes through 0x80062618. H is safe: the only other CR26 write in the image is libgte InitGeom's default 1000 (FUN_80062350), which the game overwrites at boot. Denominator: 6 CR24 / 6 CR25 / 2 CR26 writes in the whole substrate (main + 7 overlays), all enumerated. ALSO NOT COVERED BY ndiff: PlatformHle entries are not ndiff_run sites, so gate.sh's 'native/substrate divergences 0' does NOT exercise these two — the byte-exactness evidence is the gdb GTE-control-register comparison in C156, not the gate.

### render.native-camera — Supply Spyro's camera state to native producers
- status: re-verified
- deps: frame.own-render-driver
- evidence: C158
- where: game/core/game_hooks.cpp (`spyro_fps60ReadSceneCam`); external/psxport/runtime/recomp/game_iface.h + game_hooks_opt.cpp + fps60.cpp (framework seam)
- gap: DONE. The framework defect was fixed at psxport `a1c53d7c`: `Fps60::sceneCam` requires the game's `fps60ReadSceneCam` hook instead of interpreting Tomba!2 scratchpad offsets as universal. Spyro now supplies its persistent game camera state. Renderer `0x80022A2C` reads five packed rotation words from `0x80076DD0`, reads camera world position from `0x80076DF8`, subtracts that position from every world point, and then applies the rotation. The hook performs the algebraically identical affine transform: raw 1.3.12 `R`, `T = -(R * cameraPosition) / 4096`. `PSXPORT_SELFTEST=scenecam` protects signed matrix unpacking, the `R22` halfword, and translation sign with identity and rotated cases.
- notes: This does not read GTE control registers or transient scratchpad state. The path is ready for a native producer, but is not evidence that Spyro has such a producer yet: `sceneCam` still has zero game callers today.
