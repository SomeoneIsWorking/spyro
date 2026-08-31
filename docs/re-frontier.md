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
- evidence: tools/ensure_recomp.py first stages SYSTEM.CNF + SCUS_942.28 through the serial manifest publisher, then runs discdump + emit.py end to end; 621 functions emitted and hash-checked against exe + recompiler sources + seed file. Real Spyro 1 media matched 11/11 identity facts.
- where: tools/provision_title.py, tools/title_identity.py, tools/ensure_recomp.py, game/recomp_seeds.json
- gap:
- notes: Spyro 1 is a single boot executable; its code overlays are ranges inside WAD.WAD rather than root-directory executable files. The title-aware publisher closes issue 0078 before recomp cache reuse.

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
- where: titles/spyro1/core/spyro1_runtime.cpp Spyro1Runtime::bootInit
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
- status: re-verified
- deps: cd.chokepoints
- evidence: C019,C020,C106. Both game-level loaders now copy the exact archive range and publish the measured guest transfer globals directly. The 2026-08-27 real boot completed seven synchronous and five asynchronous loads, reached stage 13, and exited at its requested cap without entering retained libcd or guest VSync.
- where: game/core/cd_queue.cpp; game/core/game_config.cpp CD group
- gap: DONE at the game-level loader boundary. Retained bodies remain compiled as A/B oracles but are not product continuations.
- notes:

### cd.loader-content — Verify the loader writes the RIGHT bytes
- status: re-partial
- deps: cd.reads
- evidence: The loaded content is structurally valid: the words at heapBase+0x174 are all exactly 0x800-aligned sector offsets (1196/28645/26/28671), i.e. a WAD index — precisely what the archive's first sector should hold (C025). So the loader is placing plausible archive data, and the earlier 'writes wrong bytes' suspicion is NOT supported.
- where:
- gap: Origin of the garbage call target 0x8007ABAC is still unexplained. Not the index words themselves (they are valid). More likely the guest indexes this table with a value the port has not got right, or reads a pointer the port never populated. Also unexplained: a later load logs the same region as ALL ZEROS, so something does overwrite or re-read it differently.
- notes:

### cd.pc-owned-stock-libcd — OWN Spyro's loader natively — do NOT move the CD path down to hardware-level handlers
- status: re-verified
- deps: boot.post-cd
- evidence: C074,C106
- where: game/core/cd_queue.cpp; game/core/game_config.cpp cd group
- gap: DONE at the game-owned layer selected by the user: both loader paths serve disc bytes and publish their measured transfer bookkeeping natively. Their retained bodies remain available only as A/B oracles; neither is a product continuation into libcd. tools/own_candidates.py derives 27 current ndiff_run owners; further bottom-up function ownership is tracked only by own.non-leaf. The unrelated low-level observation probes still super-call their retained bodies and fail forward into the mandatory VSync trap if unexpectedly reached.
- notes: The old hardware-handler direction and harness blocker were stale. Moving down would run more guest libcd rather than make the port more PC-driven; issue 0004 records the user-selected game-loader boundary. Per-call NDIFF is already the accepted verification seam for native owners (harness.sbs/I019), and C106 is the real-data loader evidence.

## frame

### frame.native-loop — Take over the per-frame loop (OT/packet-pool GameConfig group)
- status: re-partial
- deps: cd.chokepoints
- evidence: C068,C069,C073,C150,C151; plus, THIS step: main() 0x80012204 disassembled (its epilogue 0x8001228C-0x8001229C is UNREACHABLE, so it never returns); the loop was first reproduced natively in the former game/core/frame_loop.cpp and PROVEN to run — PSXPORT_FNTRACE ra field is 0x80012238/0x80012284 (the guest's own call sites) with the loop off and DEAD0000 (the port's top-level sentinel) with it on, same first frame 436 and same call counts. Cadence measured twice, on two different builds: 21551 presents/vblanks against 11333 calls of 0x8001ED5C over 75s (pre-pace-parity binary), and 28218 against 15053 over 60s with PSXPORT_NOPACE=1 on the current one — 1.86-1.90 vblanks per drawn frame either way, which is the number the present-ownership design has to respect.
- where: titles/spyro1/core/spyro1_frame_driver.*; titles/spyro1/core/spyro1_boot_sequence.*; titles/spyro1/core/spyro1_field_scheduler.*; game/core/main.cpp; game/render/render_frame.cpp
- gap: The exercised native product path is runtime-owned and issue 0087 is resolved: a real 800-field run completed boot, reached stage 13, advanced exactly one framework presentation fence per finite host step, and exited 0. Wider scene coverage and the retained reference-renderer tail remain separate frontier items, so this step stays re-partial rather than claiming whole-game equivalence.
- notes: C225. Generated boot bodies remain intact for A/B. The product does not dispatch guest main 0x80012204 or boot 0x800127C0. Zero-field asset/finalization transitions are folded into adjacent visible boot steps rather than emitted as fake frames. Gameplay retains update 0x8003385C; native rendering uses the title seam.

### frame.vsync — Reimplement VSync faithfully and register it
- status: re-partial
- deps: cd.chokepoints
- evidence: Measured libetc VSync entry 0x8005DBC4 and helper 0x8005DD0C. GameConfig adapter HLE now declares 0x8005DBC4 as the mandatory fatal trap. The former helper success override is removed. FieldScheduler owns counter 0x800749E0, pad, callback root, audio, BIOS events, presentation-fence service and host-turn acknowledgement. A pending enabled VBlank with an installed HookEntryInt context is delivered by the existing IRQ/custom-exit path once; host-only fields retain direct root delivery. The real 4,255-frame `issue89_newgame_route.pad` clears the former `17 -> 19` counter failure and reaches the later native-render boundary instead. Boot replaces its own queries and waits directly; both game-level CD loaders own their transfer state and do not dispatch the retained libcd chains. The first authorized live product run reached the trap through PsyQ libgpu timeout arm 0x80062090; its measured pair 0x80062090/0x800620C4 and globals 0x80074B7C/0x80074B80 now bind to the framework's synchronous-GPU timeout owners. C225 then exercised the corrected 800-field product route through native stage 13 with no guest-VSync or presentation-fence violation.
- where: game/core/game_config.cpp hle.vsyncTrap; titles/spyro1/core/spyro1_field_scheduler.*; titles/spyro1/core/spyro1_boot_sequence.*; game/core/vsync.cpp callback/clock registration only
- gap: Product runtime proof is complete for the exercised boot/stage-13 route (issue 0087). The 4,255-frame continuation now refuses at stage selector 2 / GS_PauseMenu (retained arm 0x8001A40C), which needs its own native scene owner. The retained diagnostic renderer is still a FAMILY split, not one outer-function edit: the executable has 22 direct resident callers / 82 static call sites to 0x8005DBC4. The remaining renderer owners are 0x8001ED5C plus stage arms 0x8001A050, 0x8001A40C, 0x8001C694, 0x8001CA38, 0x8001CFDC, 0x8001D718, 0x8001E24C, 0x8001E6B8, 0x8001E9C8, and 0x8001EB80; each reached reference arm carries its own display wait/tail. Splitting only 0x8001ED5C would still trap inside the selected arm.
- notes: The old re-verified helper-HLE design is retired because it let guest code own product frame time. Static denominator: `tools/callgraph.py --calls-into 0x8005DBC4 --depth 1` and `tools/callsite_args.py --target 0x8005DBC4 --args a0 --window 12`; the latter distinguishes the 0/−1 wait/query sites and printed all 82.

### frame.own-render-driver — Own the per-frame RENDER DRIVER 0x8001ED5C — the actual native-graphics seam
- status: re-partial
- deps: frame.native-loop
- evidence: C151,C158,C162,C167,C177
- where: game/render/render_frame.cpp SpyroRenderer::drawFrame; game/render/frame_env.* native display head/tail; titles/spyro1/core/spyro1_field_scheduler.* field service
- gap: Native product rendering owns the measured display tail, one temporal commit, issue 0086's mode-2 save picker, and the stage-14 / `GS_Cutscene` recipe grounded in retained arm `0x8001E9C8`. Its shared actor/world/cyclorama owners, cutscene clear colour, `0x14000` world distance, and fade producer `0x800190D4` were observed in a coherent early 684-wide stage-14 picture, but C228 is falsified as proof of transition completion because that run was manually ended. The retained reference/GTE leg still dispatches 0x8001ED5C, whose VSync call hits the mandatory trap; split that diagnostic renderer tail without restoring a VSync success path. The corrected substrate reaches stage 0 without the former collision crash. FIELD now runs its reached authored sequence of collectables, regular actors, the visible normal Spyro model, composed secondary/shaded actors, environment, cyclorama, type-0/type-2 particles, fade, border, and tracers; Moby/Spyro shadow and flame/glow/sparkle effect arms, other scene arms, and the remaining variants are still unowned. The controlled route with left-input replay and gate-0 probe rendered 875 FIELD player groups and finished cleanly after 1,821 reconciled logic frames with zero dropped layers; it also passed the source-backed visible-portal mask/near family and type-2 particles. The regular actor snapshot is Ready after selecting Plain's binary `+28/+32` command/colour pair: 175 scanned, 14 records, 423 candidates, 211 rejects, and 212 faces. The secondary/shaded composition now owns the adjacent calls with batch painter admission and shared shadow-cursor rebasing; a real route ran 3,700 presented fields / 1,910 reconciled logic frames with no refusal, valid-empty secondary output, and roughly 110–120 shaded faces per FIELD frame. Full actor/shadow/effect visual and oracle parity remains open. The environment snapshot is Ready at selection 17 / distance `0x28000`: 86 sectors, 1,376 candidates, 1,039 rejected, and 413 final faces, with read-only preparation over the culling word and all 7,168 edge-work bytes. Issue 0077's later retained-world oracle remains required before wiring it. The same Artisans snapshot has five logically active cyclorama records, but every aperture projects to zero screen-crossing edges, so all five are valid-empty and the main-sky recipe is Ready. Issues 0093 and 0097's visible gate-route boundaries are crossed; the compiled `0x80050240` recipe and family submitter remain ready for a future mid-distance portal frame. The shaded queue snapshot separately preserves 93 total / 52 valid-mesh / 3 visible records, covers mesh IDs 83/84 plus lighting offsets 8/16, and derives 23 ready Gouraud faces from 54 visible candidates; external-vertex colour generation and nonzero primitive variants remain atomic refusals.
- notes: The five renderers this repo has already named all hang below this function: EmitActorDrawList 0x8001F798 and EmitSecondaryActorPrimitives 0x80020F34 via the stage-0 layer 0x80019698; EmitStaticActorMeshList 0x8004EBA8 via the stage-4/5 arm 0x8001CA38; RenderWorldChunks 0x800258F0 via the stage-14 arm 0x8001E9C8; RasterizeSpritePrimQueue 0x80022A2C via the stage-15 arm 0x8001EB80 (tools/callgraph.py --from 0x8001ED5C --to <addr>).
  Actor-chain groundwork now treats 0x800521C0→0x8001F158→0x8001F798 as one ownership unit keyed by
  0x8001F798. A 32-call live checkpoint census joins all 3,021 observed packet-family sites to the
  immutable 0x38-byte record cursor and independently parses the five-family-capable packet span;
  the reached corpus contains G4/GT4/G3/GT3 and no FT4/semi/raw; only durable record index 0 emitted.
  The source oracle now correctly treats A as candidate epochs and B as a positive subset of the
  same epochs (while separately classifying B's terminator-path firings). Immutable source/scratch/depth/colour inputs independently reproduced
  every final-pool payload: 3,021/3,021 exact, spanning all reached families and direct/quad-first/
  quad-second triangle origins, with named XY and colour corruptions rejected by the same comparator.
  Explicit epoch lifecycle negatives reject missing/duplicate subset classifiers, changed source or
  record, and stale family reuse. An independent acceptance evaluator now matched all 6,464 reached
  candidates across 32 calls: 3,021 emitted, 3,443 rejected, exact family/origin/source cursor and
  zero pool/payload mismatch. The live branch census reached 1,908 NCLIP rejects and 1,535 zero-area
  rejects; outcode/skip/depth/FT4/semi/raw were zero. Outcode/skip/depth have hermetic coverage only;
  FT4 is explicitly refused and raw behavior remains unmodeled.
  A separate pre-global pass now proves numeric local-bin insertion and per-bin FIFO order for all
  3,021 reached packets over all 18,432 scanned slots, including both empty-bin insertion and
  nonempty append, with bin/link corruption negatives and zero cycles/duplicates/out-of-range links.
  The post checkpoint now matches a pure exact global-splice simulator with zero word mismatches,
  including every traversed global pair and scanned local pair (changed or unchanged), local clears,
  tag patches and roots. Untouched global-tail/local-word corruption negatives prove that coverage.
  Live calls reached one
  populated plus one no-local record and empty global slots only; hermetic shipping fixtures cover
  preexisting-global append, two groups, base bounce, and corrupt/no-local cases. Multiple populated
  records and live preexisting-global/bounce remain corpus gaps. Live evidence for outcode, skip,
  final-depth and the unreached FT4/raw arms is still required before any native producer. The next
  implementation dependency is now exact: own/oracle `0x8001F798`'s pre-candidate projection prefix
  (`0x8001F7FC..0x8001FFF8`). `0x8001F158` creates records but not the scratch XY/status/depth values;
  using the current A-checkpoint snapshot for shipping would be a guest-compute fallback.
  One bounded `prefix` census now observes setup/count, all six exact codec selector PCs, and color
  expected/actual classification together in the same generated invocation. Its exact observer
  accounting removes the former cross-run join problem. Far reject, terminator and outcode remain
  explicitly unobserved, and no projection prefix is yet owned.
  The combined 500-present run produced 32/32 PASS with 64 setups, 6,240 vertices, every selector
  site nonzero, both full-word and s16 choices, and 64/64 color classifications with zero mismatch
  (45 direct-high, 19 positive-blend). Plain and negative-fog remained zero in that earlier corpus;
  Plain is now grounded by the FIELD snapshot while negative-fog remains unowned.
  Production implementation stops at the reached PositiveBlend color path: it uses DPCS
  (`0x4A780010`, not NCDS), paired vertices use INTPL, and the optional `0x8001F940` source/color
  expansion remains unowned. Framework `15d6de6f` now supplies a thread-local explicit-state vendor
  oracle for any GTE op, but shipping presentation must not replay GTE work. The pure RTPS endpoint
  dependency is now owned once in psxport `native_projection` and consumed by the
  existing paired actor; it retains exact integer output and raw pre-saturation view coordinates, but
  explicitly is not a temporal recipe. Game-local `actor_model_codec` now also owns the reached
  full-word/s16 stream progression plus semantic INTPL pose and PositiveBlend DPCS color operations;
  the paired actor delegates both equivalent INTPL uses to that same helper. The remaining dependency
  is the caller-specific record/model deep copy, transform/setup and projection-loop join;
  NegativeBlend, optional expansion and other uncovered branches remain explicit refusals. At that
  historical checkpoint no ActorRecipe or native 1F798 submission existed; the current regular
  actor owner and issue 0094 supersede that placement while preserving those refusal gates.
  Phase 3's pure immutable `actor_prefix_builder` now includes the negative-header arm and atomic
  boundary: 31/31 calls pass the fresh 500-present oracle, including 21 clip-mode records/3,297
  vertices with exact status scratch words and zero live common-status rejections. `classifyCall`
  accepts only complete `Ok`/`VisibilityRejected` record sets and
  refuses empty or unsupported sets before a future queue mutation. High colors and primitive words
  remain capture-only. The pure `actor_draw_recipe` now performs the next composition step without
  submitting: one 32-call payload pass joined all 6,464 immutable candidate inputs and 3,021 ordered
  faces to the independent final packet pool, and a separate same-invocation OT pass joined the same
  6,464 candidates/3,021 faces to 18,432 local slots and 5,076 compared splice words with zero
  mismatch. Semi/FT4/malformed/bin-range inputs atomically refuse and valid empty is explicit. The
  next REAL step is queue/planner preflight plus one native PainterObject submission while retaining
  the guest leg as reference; RQ submission and guest suppression are still absent, and unreached
  FT4/semi/raw remain refusal gates.

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

### input.start-skip-sequences — Start skips logos, loading overlays, and scripted sequences
- status: re-partial
- deps: input.pad
- evidence: C110; issue 0027; generated `0x800127C0`; docs/findings/start-skip-map.md
- where: game/core/boot_skip.{h,cpp}; game/core/cd_queue.cpp `lp_800127C0`; game/core/vsync.cpp `deliver_field`; `PSXPORT_DEBUG=bootskip,skipmap`
- gap: BOOT COMPLETE, BROADER STEP PARTIAL. Boot uses a fresh edge to advance only its exact 0xD2 presentation clock. Title/attract keeps its legitimate guest transition. Stage mode 14 is now classified as recorded/demo playback and ALREADY handles Start/Cross in guest `0x800331AC` by accelerating its cursor toward the natural completion writer `0x8002D440`; adding a native transition would duplicate it. The level-transition owner now consumes a fresh Start edge to hide the tally at the exact guest boundary 417 while preserving CD loading and guest transition state; it does not skip portal traversal. Observed stage13/sub3 phases are required streaming/I/O, not a presentation hold, and remain unmodified. NEXT: find an independently timed post-load overlay or another scripted-sequence family, then trace its natural completion writer. Never pulse Start across modes 0/2 gameplay, where it means UI/pause.
- notes: `skipmap` has a negative denominator every 600 fields and reports every edge/state transition uncapped. Boot classification uses the dynamic lifetime of `0x800127C0`, not a frame threshold.

### input.controllable-gameplay — Hold digital input and move Spyro in a live field
- status: re-verified
- deps: input.pad, frame.native-loop
- evidence: Issue 0089; real SCUS_942.28; Clang product, controlled idle-vs-Left replay, the native gate route, and the corrected 2026-08-29 action matrix. Four isolated directional holds produce distinct nonzero target directions and position changes; Cross reaches `m_State=5` and rising `m_airTime`; Square reaches `m_State=0xB`; Circle sets `g_SpyroFlame+0x98=1`. Seven gameplay-action runs exit 0 with no native-render refusal or fatal. The final gate route continued through the wired stage-0 sequence for 1,820 reconciled logic frames with zero dropped layers and no refusal, including the visible portal mask/near family and type-2 particles.
- where: game/core/native_gameplay.{h,cpp}; titles/spyro1/core/spyro1_runtime.cpp; titles/spyro1/core/spyro1_frame_driver.cpp; tests/test_native_gameplay.cpp
- gap: The controlled route now renders the normal three-layer Spyro model through FIELD's `0x80023AC4` owner and continues through the wired stage-0 producers, including the visible portal mask/near family and issue 0097's type-2 particle family, with 1,821 reconciled logic frames and zero dropped layers. The regular actor owner now stages the source-backed Moby shadow list at `0x800724F4` and publishes its cursor at `0x80075F00` after actor admission. A snapshot-isolated retained capture measured zero Moby-shadow packets but 16 Spyro-shadow packets from `0x80059A48`, including exact point `SZ` values, non-negative OT buckets, and complete guest link chains; an eight-capture route produced 128 packets with no graph error. The native Spyro-shadow owner now derives and queues that 16-face fan; its first face matches the retained anchor/point/depth capture exactly. Moby shadow `0x80059F8C`, flame/glow/sparkle effects, full visual/oracle parity, and remaining variants are still open. Start/Select reach pause/inventory state but their native menu picture owners still refuse explicitly. Portal traversal remains a separate level-transition gap.
- notes: The native override super-calls gen_func_8003D3B8, preserves analog/release behavior, and only corrects the source-defined digital target selection from m_Held. The 2026-08-29 action matrix additionally witnessed retained jump (`m_State=5`), charge (`m_State=0xB`), and flame activation (`g_SpyroFlame+0x98=1`) on isolated raw-pad holds. PSXPORT_GAMEPLAY_PROBE is diagnostic-only and presents the previous frame while retaining real logic, collision, input, and field delivery. It now preserves the retail two-field logic quota by deferring the first of two fields and presenting once on the second. FIELD player visibility follows the source `g_IsSpyroHidden` gate at `0x80075814`; its authored replay phase is after regular and secondary actors so it can share the world queue without mixed-policy refusal. The secondary/shaded actors now use a combined source-order owner with batch painter admission; shadows/effects, full visual/oracle parity, and portal traversal remain open. The gate teleport remains diagnostic-only: it writes the source-backed node position, not level or transition state.

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
- status: re-verified
- deps:
- evidence: C125,C126,C143,C145,C146,C198,C201,C203,C204
- where: external/psxport/tools/recomp/emit.py vertex_pz_stores; runtime/recomp/gte_beetle.cpp gte_hold_pz/gte_record_pz/gte_copy_pz; proj_prim.cpp
- gap: MECHANISM RE-VERIFIED, COVERAGE IS NOT. Where a primitive's vertices resolve, they resolve exactly: sampled frames reach 210/210 prims with miss=0, and the rendered image is unchanged from before depth was enabled (C126), which is the correct result — the game's own painter order is still right for its own camera, so real depth only changes the picture once the camera moves or widens.
- notes: RESOLVED 2026-08-19 (C204, issue 0067): per-primitive depth coverage is 63.60% of 3,483,268 prims, 85.30% of vertex-depth lookups resolved — up from 2.10%/6.41% — with the picture BYTE-IDENTICAL (f6001 md5 b6223ab7) and the gate 13 PASS / 709529 native producer prims unchanged. The blocker was NEVER this game's renderers: ProjPrim keyed entries by guest address alone, which forced entry lifetime down to one buffer flip, because a recycled packet-pool slot would otherwise be served the depth of the vertex that used to occupy it. Guarding each entry by the WORD it was recorded against makes a reused address unable to alias, so retention went to 8 generations (framework 2de90164 + tests/test_proj_prim_stale.cpp, shown RED first). Ruled out on the way, each of which looked like the answer: owning the world renderer (changed coverage by NOTHING), the recompiler pz tap not firing (fires 16.7M times; the one real gap on the clip arm at 0x8002631C is worth +43 prims of 3.48M), and the buffer-to-buffer carry not running (runs 20.5M times, carries 10M). READ THE STALE COUNT WITH THE COVERAGE NUMBER: with the guard compiled out the same run reads 70.53%, and those extra points are prims whose recorded word no longer matches memory — the previous attempt at longer lifetimes bought 6.9%->23% the same way and depth-culled the player character. Measure with I051; I041 stays distrusted. NEXT: the 2D/3D discriminator now has its signal, so widescreen re-centering (C143) and the uncovered-margin strip (issue 0039) are unblocked — but 63.60% was measured on the REFERENCE leg, which computes depth without ordering by it, so an unchanged picture there is not evidence the depths are CORRECT. The native leg must reach the field (issue 0065) for that.

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

## ownership

### own.next-targets — Own more guest functions natively, each gated by PSXPORT_NDIFF
- status: re-verified
- deps: harness.sbs
- evidence: C075,C080,C207,I019,I020,I021
- where: game/core/native_rand.cpp is the pattern; game/core/ observation wrappers are the candidates
- gap:
- notes: PHASE DONE: the high-caller LEAF work is complete. tools/own_candidates.py now derives 27 owned bodies from the live ndiff_run sites; C081's historical count of 15 is superseded. Four are GTE bodies, owned without reimplementing the GTE — scalar logic native, COP2 via the platform's gte_op/gte_read_data/gte_write_ctrl, integer divide via cpu_div (C++ division is UB exactly where MIPS defines behaviour: /0 and INT_MIN/-1). Its best remaining LEAF has 15 callers, down from 136, so this seam has given what it has to give. CAVEAT on exhausted: the caller count is STATIC, so indirect calls are invisible and a low count is not proof of coldness — a runtime call histogram is the honest measurement. Continuing is tracked by own.non-leaf.

### own.non-leaf — Own NON-LEAF functions, bottom-up from the leaves already owned
- status: in-progress
- deps: own.next-targets
- evidence: C082,C207,C209,C210,C211,C213,C224,C230,I019,I020,I028
- where: game/core/native_printf.cpp; game/core/native_actor_mesh_scratch.cpp; game/core/native_spu_pio_upload.cpp; game/core/spu_pio_upload.h; game/core/native_spu_hardware_init.cpp; game/core/spu_hardware_init.h; game/core/native_text_sprites.cpp; game/core/text_sprites.h; game/core/native_memcard_event_stack.cpp; game/core/memcard_event_stack.h; game/core/native_memcard_operations.cpp; game/core/memcard_operations.h; game/core/recomp_register.cpp; tools/own_candidates.py --ready-nonleaf
- gap: SEVENTH STEP VERIFIED. The dependency-ready libmcrd request starters MemCardExist 0x8006635C and MemCardAccept 0x800665B8 are now owned together as one async-operation boundary. Their 31-instruction bodies share the idle/busy transaction and have only the already-owned printf and event-stack children. Current-binary FNTRACE reached MemCardAccept twice, first at frame 280, and MemCardExist 346 times, first at frame 287; `scratch/logs/spyro-memcard-operations-ndiff-20260828-final.log` matched native calls 1-2 and 1-4 respectively against their retained generated bodies with no divergence. The focused memcard test covers the operation codes and callback addresses alongside the event-stack layout. C230 and issue 0096 record the compared surface and falsifier. The registry now wires the generated raw-slot getter so FNTRACE can preserve existing owners. Remaining candidates must be ranked and observed before the eighth step.
- notes: C082 puts all guest code at only 4.5-4.9% of host CPU, so further ownership buys correctness and architecture, not speed. Pick from tools/prof_hot.py for measured speed work and from the static queue for coverage. FNTRACE and an owned override cannot be armed on the same address because both use its one override slot; C207, C209, C210, C211, and C213 therefore cite separate reach and equality runs. NDIFF does not snapshot host-only device/framework state; C211 records the SPU blind surface rather than laundering the exact compared-state result into a whole-device equivalence claim.

## perf

### perf.diagnostics-overhead — The logger costs ~6% of CPU with logging switched OFF
- status: re-verified
- deps:
- evidence: C083,C084,C085,C088,C089,I022,I023
- where: lucent (external, the user's own library); external/psxport/runtime/recomp/cfg.cpp; Core::wwatch_check
- gap:
- notes: DONE, and STOP HERE unless a CPU-bound workload appears. Three fixes landed, all measured: lucent channel_enabled 6.06%->0.33% (fixed in the shared library); the per-store watch hooks ~4.9% (inlined armed test); the generation-counter chain ~6% (trackStore -> cfg_dbg_generation -> bootstrap_once). Frames 16508 -> ~18700, seventh overlay reached. BUT C089 is the finding that should govern what happens next: the LAST ~6% bought no measurable throughput at all, and the run reaches an identical point either way. ~29% of samples sit outside the binary (driver/loader) and do not shrink. Further micro-optimisation of this workload is optimising something that is not the constraint. TWICE in this work a fast path silently never engaged (lucent's early return, trackStore's statics vs members) and BOTH were invisible to reading and obvious to re-profiling — never accept an optimisation on the strength of the diff.

## render

### render.native-presentation-base — Select guest-VRAM preservation per frame owner
- status: re-verified
- deps: frame.own-render-driver
- evidence: Issue 0080; real SCUS_942.28; psxport bc8c8897; fresh present-stage PIDs 548759/545600/546633. Standard boot presents 30/300 are real 252/16216-color SCEA/Universal uploads. Native stage-13 present 700 is a real 2738-color 16:9 frame and 2961-color 4:3 control; both scaled eight-row guard bands are exactly one color black with mean zero. Full hashes/commands/logs are in scratch/screenshots/spyro-vram-policy-20260824/capture-metadata.txt.
- where: game/render/presentation_owner.*; game/render/render_frame.cpp; titles/spyro1/core/spyro1_runtime.*; external/psxport/runtime/recomp/guest_vram_composite_policy.h
- gap:
- notes: SpyroPresentationOwner defaults to guest VRAM before the frame driver exists and is published before each reference/native seam can present. Spyro1Runtime exposes that per-Game state through the required GameRuntime virtual; Spyro 2/3 fail loudly rather than inherit an unverified answer. psxport rebuilds the persistent composite whenever ownership changes and restores a whole guest-VRAM upload when ownership returns to the guest. The prior authored-painter draw-area clip remains independently green (`outside=0000`, `inside=001F`), and deferred native fields no longer recapture the consumed queue.

### render.own-geometry-family — Own the hand-written assembly geometry renderers (the gate for widescreen AND 60fps)
- status: re-partial
- deps: gpu.native-depth
- evidence: C127,C129,C130,C136,C137,C138,C139,C140,C141,C176,C177,C198,C199,C201,C203,C229; current post-framework stage-0 replay scratch/logs/spyro-replay-post-framework-field-20260828.log exits rc=0 after 10000 presented fields with 5057 reconciled frames, zero dropped layers, and particles:type0 activity; focused particle/tracer recipes pass.
- where: 0x8004EBA8 (cyclorama, owned byte-exact) · 0x800258F0 RenderWorldChunks (C215 corrects C198: 0x800258F0..0x8002A6F4, ~5000 insns, selection/cull+animate / LQ+HQ direct and medium/near refinement / adaptive packet replacement) · + 17 more sharing the fixed-area register-save idiom at D_80077DD8
- gap: Native stage-0 composition now reaches collectables, regular actors, the visible normal Spyro model, the source-grounded Spyro shadow fan, composed secondary/shaded actors, environment, cyclorama, type-0/type-2 particles, fade, border, tracers, and the source-backed visible-portal mask without a render refusal on the controlled route. Faithful visual/oracle comparison is still missing; Moby shadow and other actor effect arms, mid-distance portal mesh coverage, blended world animation, other particle types, tracer variants, and the retained guest-renderer tail remain open. Gate-0 teleport is source-backed and reaches a visible portal: the near 0x8004F4BC recipe preserves three aperture edges and produces 94 clipped triangles from the real snapshot; its family-batched native submitter and the 0x8004FEA0 mask submitter now have focused queue admission/publication coverage. Issue 0097's type-2 family is admitted and the controlled route continues past it.
- notes: The field producer order is wired in game/render/render_frame.cpp. The framework line-list path is an explicit primitive owner, not a triangle approximation. The current audio-field trace covers 1,200 NTSC fields with exactly 882,882 expected/queued samples, each field rendering 735 or 736 samples into a valid 44.1 kHz stereo WAV. SBS now compares exact per-field PCM reports after isolating the Beetle output ring in each SPU state and rebinding the active state before each core step; the real 120-frame oracle run produced 240 reports with no audio mismatch. This is audio-field parity, not complete oracle/visual parity: the run retains known non-audio boot/state divergence. A paced audio run after the shared CDC MODE_SF routing fix is non-silent and 20.02 seconds for 1200 VBlanks, with 239 selected file-1/channel-4 XA sectors and no ring-full report; the previous back-pressure came from decoding interleaved unselected channels. RE 2026-08-28: 0x80050BD0 is DrawActors; 0x8004F4BC is the near dynamic mesh family, 0x80050240 is the fade-band family, and 0x8004FEA0 is a separate fog/mask pass. Its two source-defined full-screen POLY_F3 triangles now use the prepared aperture half-planes and a dedicated painter key; the controlled route now passes the mask/near and type-2 boundaries. The FIELD player owner applies the source hidden gate and an authored paired-actor phase; secondary/shaded actors use a combined source-order owner with batch painter admission; the remaining shadow/effect arms still need ownership and independent visual/oracle comparison.

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

## spyro2

### spyro2.identity — Identify SCUS_944.25 and provision it without Spyro 1 cache conflation
- status: re-partial
- deps:
- evidence: The measured 358400-byte executable matches 11/11 manifest facts (SHA-1 42633ff8cf1b43c49c5fe23d00eca6eb2703828b; SHA-256 7b54002789ab379e0d3a6b49d7b50078b9c9c2fed589f41ff9878ebb68fe0bc1; entry 0x8005478C). Both-answer provisioning tests preload a Spyro 1 cache and prove a requested Spyro 2 operation stages and validates SYSTEM.CNF before publishing.
- where: titles/spyro2/executable.json, tools/title_identity.py, tools/provision_title.py
- gap: No Spyro 2 CHD was available, so SYSTEM.CNF/disc provenance has not been measured on real Spyro 2 media.
- notes: SCUS_942.28 and SCUS_944.25 have separate title specs, environment keys, manifests, and cache destinations.

### spyro2.crt0 — Own Spyro 2 executable image and stop at the first unverified execution boundary
- status: re-partial
- deps: spyro2.identity
- evidence: Exact SCUS_944.25 emission from the 358400-byte manifest-matched executable discovers 683 resident functions in 9 translation units with zero foreign or overlay seeds, including crt0 0x8005478C, libcInit 0x8005ABD8, game main 0x80011ADC, boot prefix 0x80011E9C, display bootstrap 0x80011BBC, and libetc VSync 0x80058EDC. Ghidra and emitted bodies agree on the persistent main/boot/display frames and expose three display field waits: direct returns 0x80011BD8 and 0x80011CE0 plus nested clear-helper return 0x8004C49C. The title owner transcribes their exact non-timing effects while one framework presentation fence replaces each wait. DrawSync 0x800557E4 and GPU timeout arm/check 0x80057880/0x800578B4 are synchronous title overrides with retained supers. Live PID 3564943 presented all three fields without guest VSync; all three captures logged 0/691200 non-black pixels and were visually inspected as uniform black. It selected NTSC 59.940 Hz, returned to 0x80011EB4, and stopped at 0x80011B1C. The exact-319d30b6 Clang product and focused fence/timing contract pass.
- where: titles/spyro2/recomp_seeds.json; tools/ensure_spyro2_substrate.py; titles/spyro2/core/spyro2_recomp_register.*; titles/spyro2/core/spyro2_frame_driver.*; titles/spyro2/core/spyro2_display_bootstrap.*; titles/spyro2/core/spyro2_gpu_sync.*; titles/spyro2/core/spyro2_runtime.*; titles/spyro2/core/main.cpp; cmake/spyro_port.cmake
- gap: Issue 0092 owns the post-display boot-prefix initialization and loader chain. The later loader reaches 0x80077374 outside resident executable text and needs binary-derived source/base/payload evidence before dispatch.
- notes: The dedicated spyro2_port link is required because Spyro 1 and Spyro 2 generated bodies export identical func_<guest-address> names with different implementations. Retained bodies 0x80011BBC, 0x8004C484, DrawSync, and the GPU timeout leaves stay generated for comparison; the product dispatches none of their waits, and PlatformHlePlan keeps 0x80058EDC fatal over [0x80058EDC,0x80059054). Issue 0090 resolved the shared null-legacy-CD seam without adding title compatibility data.

## runtime

### title.runtime-selection — Select exact executable identity and derived runtime before Game
- status: re-verified
- deps:
- evidence: C221; selection CTest covers exact, unsupported, mutated and renamed inputs; real SCUS_942.28 boots and real SCUS_944.67 reaches the no-substrate refusal before Game
- where: game/core/title_selection.*; game/core/title_runtime_registry.*; game/core/main.cpp; titles/spyro*/core/*_runtime.*; titles/spyro*/executable.json; tools/generate_title_catalog.py
- gap: DONE for exact USA executable selection. Only Spyro 1 has an executable substrate; Spyro 2/3 deliberately refuse before Game.
- notes: Each derived runtime owns title behavior and substrate installation/refusal. The JSON manifests are the single executable-identity authority and generate the C++ catalog. No GameConfig discriminator or fallback to Spyro 1 exists.

## spyro3

### spyro3.identity — Identify USA Spyro 3 executable without title conflation
- status: re-partial
- deps:
- evidence: A fresh independent check of the supplied 380928-byte SCUS_944.67 matches 11/11 manifest facts: SHA-1 31ad35fd03539910b5b3d9309ae52f5acaf3612e, SHA-256 cb819ee78c556d403779309859cb08a7111331f624759bc1bc380946261bb26e, entry 0x80059444. The shipping crt0 extractor independently resolves all 8 fields and computes InitHeap(a0=0x800742D4,a1=0x18B528); direct decode confirms the post-libc jal at 0x800594E0 targets game main 0x8001200C.
- where: titles/spyro3/executable.json; tools/title_identity.py; tools/provision_title.py
- gap: No Spyro 3 CHD was available, so SYSTEM.CNF/disc provenance remains unmeasured.
- notes: SCUS_944.67 has its own manifest, environment key, and cache destination.

### spyro3.crt0 — Own Spyro 3 executable image and stop at the first unverified execution boundary
- status: re-partial
- deps: spyro3.identity
- evidence: Shipping psxport crt0_extract resolves 8/8 SCUS_944.67 boot-group fields and crt0_plan; decode identifies libcInit 0x8005F63C and game main 0x8001200C. A binary-only emitter pass discovers 639 resident functions, including crt0 0x80059444 and main 0x8001200C, with no title seed file or Spyro 1 overlay input. Static RE identifies libetc VSync at 0x8005956C: it calls helper 0x800596E4, whose timeout arm constructs the executable's unique `VSync: timeout` string at 0x8001165C; 36 direct callers pass the expected 0, -1, and 2 modes. The Clang runtime test checks its typed image and absence of Spyro 1 legacy views.
- where: game/core/spyro_runtime.*; titles/spyro3/core/spyro3_runtime.*; tests/test_spyro3_runtime.cpp
- gap: The generated SCUS_944.67 set is an ignored RE artifact only; no shipping target installs it and no differential execution exists. Selection still refuses before Game. A future title-owned frame loop must declare 0x8005956C as the mandatory fatal guest-VSync trap, not a success HLE.
- notes: Spyro3Runtime inherits SpyroRuntime directly and binds no Spyro 1 compatibility config/hooks.
