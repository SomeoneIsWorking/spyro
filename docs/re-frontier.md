# RE Frontier — ordered evidence chain for Spyro's dynamic runtime

Tracked by `tools/re_frontier.py`. This is the fine-grained companion to
`docs/codemap.md`: it records the binary or asset evidence required before each dynamic-runtime
boundary can be implemented and verified.

The product executes the authenticated PSX image through psxport's Lightrec runtime. It does not
generate guest source or ship a generated guest corpus. Interpreter-only execution belongs in a
separate test or diagnostic target; gameplay admits only the shared framework's bounded fallback
after JIT refusal. A mechanism trace is not fidelity evidence; a step becomes `re-verified` only after its observable result matches the real title on
real data.

Statuses: ✅ re-verified · 🟡 re-partial · 🔬 in-progress · ⬜ todo · ➖ skip-by-design · ⏸ blocked
(computed). A failure must refuse loudly rather than selecting an alternate execution method.

<!-- Machine-edited by tools/re_frontier.py add/set. Format: `## <area>` sections;
     each entry is `### <id> — <title>` followed by `- <field>: <value>` lines. -->

## runtime

### boot.provision — Authenticate the selected executable as a runtime image
- status: re-verified
- deps:
- evidence: The provisioner validates serial, PS-X EXE header, size, and hash before publishing the selected executable; Spyro 1's manifest covers 11 identity facts.
- where: tools/provision_title.py; tools/title_identity.py; titles/spyro1/executable.json
- gap: The identity gate proves bytes and provenance only; it does not prove runtime execution.
- notes: WAD resident images are loaded by the runtime and are not generated into source files.

### dynarec.executor — Execute guest instructions through per-Core Lightrec
- status: re-partial
- deps: boot.provision
- evidence: psxport exposes per-Core `dispatchGuest`, scoped `callOriginal`, typed exits, image identity, and invalidation; Spyro enters the boundary at authenticated crt0.
- where: external/psxport/runtime/cpu; game/core/guest_execution.*; game/core/main.cpp
- gap: Real Spyro media executes nonzero JIT blocks across budget yields (S008); CD synchronization, complete title dispatch, and bounded-fallback conformance remain unverified.
- notes: This is a shared-runtime prerequisite, not a title-specific code-generation task.

### dynarec.dispatch — Route native overrides and original calls by image identity
- status: re-partial
- deps: dynarec.executor
- evidence: WAD images reuse guest load addresses; the runtime API therefore carries image identity through native dispatch and scoped original calls.
- where: game/core/guest_execution.*; game/core/world_guest_execution.*; external/psxport/runtime/cpu
- gap: Prove one native override, one scoped original call, and positive plus controlled-negative invalidation across two resident images.
- notes: Guest address alone is never a valid cache or override key.

### dynarec.stage13 — Reach the 800/900 stage-13 discriminators
- status: re-verified
- deps: dynarec.dispatch
- evidence: Native/Lightrec title and save-picker observations, displayed image inspection, execution counters, and per-product-step fence counts are recorded in docs/project-state.md S009.
- where: titles/spyro1/core/spyro1_frame_driver.*; game/render/frame/frame_renderer.cpp; game/core/runtime_run.*
- gap: None for the two wiring discriminators; gameplay and motion qualification belong to dynarec.gameplay.
- notes: These are first runtime discriminators, not representative gameplay.

### dynarec.world-resume — Resume unchanged world code through the runtime
- status: re-partial
- deps: dynarec.executor
- evidence: The former world-body derivative and its call sites are absent; `WorldGuestExecution` names unchanged retail `RenderWorldChunks` at `0x800258F0` through scoped original execution.
- where: game/core/world_guest_execution.*
- gap: Execute and suspend this boundary through Lightrec, then prove resumption of the same guest CPU state after host-owned exits.
- notes: No generated body or interpreter fallback may replace this boundary.

### dynarec.gameplay — Prove representative interactive gameplay
- status: todo
- deps: dynarec.stage13, dynarec.world-resume
- evidence: The project goal defines a bounded interactive route with independent state comparison and native/original dispatch coverage. The no-input attract route crosses a level entry and then diverges at `g_GameTick` 556; issue 0133 names the two writers ahead of it — `Moby+0x42` is the animation-flags byte written only by `func_800522C0` (`sw $at, 0x40($t5)` at `0x800523E8`/`0x8005243C`, read out of the image), and the level's `Moby*` list at `0x80070BF4` is what both the updater and the class dispatch walk.
- where: docs/project-goals.md; docs/project-state.md; docs/issues/0133-the-attract-demo-s-recorded-input-diverges-from.md; tools/verify.py
- gap: The class-`0x71` moby at `0x80173B80` is frozen on the product while its `0x40+0x41` is 64, which `func_800522C0`'s own arithmetic says would have set the flags bit, so the product's copy of the list does not reach it. Compare timing, interrupts, memory, and relevant device state against an independent emulator; exercise WAD invalidation; prove bounded fallback admission and no interpreter-only product selector; meet the declared host frame-time budget.
- notes: Boot, logos, menus, FMV, and a clean trace are not gameplay conformance. `g_DynMobyCount` is not a `MobyAlloc` call count: the reference's 4 -> 15 jump at tick 528 executed neither `0x800524D8` nor `g_SpawnMoby`.

### delivery.host-matrix — Qualify every claimed host architecture
- status: todo
- deps: dynarec.gameplay
- evidence: Hosted CI runs one asset-free Linux x86_64 source-policy check; macOS arm64, Windows x86_64, and Android arm64 runtime jobs remain partial/missing.
- where: .github/workflows/ci.yml; docs/project-state.md
- gap: No title-specific native/Lightrec runtime or performance result exists yet for any released host; local real-data qualification and platform packaging support remain open.
- notes: CI never downloads game assets or substitutes a fake executor for missing platform support.

## titles

### picker.logo — Recover each title's own artwork for the picker panel
- status: re-partial
- deps: boot.provision
- evidence: each panel's logo is the title's OWN decoded artwork read back from its resident VRAM at runtime, never a fetched or committed asset. All three boot wordmarks are direct-colour 15-bit (transfer mode 2) and sit on the disc's untouched black card, so the card's black surround is transparent and only the wordmark survives: Spyro 1 `SCUS_942.28` Universal/Universal Interactive Studios at VRAM `84,31 348x67`; Spyro 2 `SCUS_944.25` Insomniac Games at VRAM `7,89 481x99`; Spyro 3 `SCUS_944.67` "Created and Developed by Insomniac Games" at VRAM `13,89 486x90`. Each read is accepted only when the rectangle actually holds artwork (a card before it is drawn reads back as black and is retried on a later step), because the read itself always succeeds and cannot answer that.
- where: game/core/title_logo_facts.h; game/host/panel_logo.*; titles/spyro{1,2,3}/core/spyro{1,2,3}_logo_facts.h
- gap: none of the three BOOT paths shows a SPYRO title-screen wordmark — these are the discs' publisher/developer cards, so the panels show the boot artwork, not the game's own logo.

### picker.attract-state — Know when a panel's demo has begun
- status: re-partial
- deps: dynarec.gameplay
- evidence: an unselected panel pre-rolls until its own demo is running and then freezes, so the flag that says "demo" must be measured per title rather than guessed from pixels. Spyro 1 `0x80075714` (`spyro::guest::kDemoMode`); Spyro 2 `0x80066D40`, confirmed causally — the word is zero through the boot/card phase and non-zero once the attract demo plays, and clearing it during the demo returns the disc to its "SPYRO Riptor's Rage! press start" title screen. Spyro 3 remains `AttractState::Unknown`, which falls back to settling on the first valid held picture.
- where: game/host/panel_sessions.cpp; titles/spyro{1,2}/core/spyro{1,2}_attract_facts (via `SpyroRuntime::attractState`)
- gap: Spyro 3's attract flag is not located. A live RAM sweep across `0x80068000-0x80070000` between its card and its demo found 14 changed words and none of the candidates removed the "Entering Demo Mode" caption; `state save` refuses while the memory card is closed, so boot-phase snapshots need a card-open route first. Its intermediate "Entering Demo Mode" screen also shows a demo must never be frozen there. Next experiment: capture a RAM snapshot while the card IS open (route through the card-open state), then diff against a snapshot taken inside the demo.

### spyro2.identity — Preserve Spyro 2's independent executable facts
- status: re-partial
- deps: boot.provision
- evidence: `SCUS_944.25` has its own manifest, serial, entry, size, and digest facts.
- where: titles/spyro2/executable.json; titles/spyro2/core/spyro2_runtime.*
- gap: Disc provenance and dynamic product execution remain unverified.
- notes: Spyro 2 implementation waits for Spyro 1's representative gameplay gate.

### spyro3.identity — Preserve Spyro 3's independent executable facts
- status: re-partial
- deps: boot.provision
- evidence: `SCUS_944.67` has its own manifest, serial, entry, size, and digest facts. It now also executes: `tools/title_route.py --title spyro3` reaches the guest's playing state from its own title screen (observed states 0,11,5,6,5,6,5,6,5,3,0; arrival at 4890 fields) and the guest's own position words move under pad input (walk 1405 units, jump z 18513 -> 18710), with 0 faults and 0 interpreter fallback.
- where: titles/spyro3/executable.json; titles/spyro3/core/spyro3_runtime.*
- gap: Disc provenance remains unverified; no level words are recorded for this title, so the route proves MOVEMENT but not which level it is in.
- notes: Spyro 3 implementation waits for Spyro 1's representative gameplay gate.

### spyro3.widescreen — Widen Spyro 3's own guest projection
- status: re-partial
- deps: spyro3.identity
- evidence: every site was measured in Spyro 3's own image, not copied from Spyro 2: `SetGeomOffset` leaf `0x8005D35C`, whose sole CR26 publication is `0x8005955C` (that one writes the projection distance H and is deliberately NOT intercepted), plus four inline CR24/CR25 restatements at `0x80033EF8`, `0x80033EFC`, `0x80034B30`, `0x80034B38` that the per-field re-assertion carries. Authored window 512x240, OFX 256, OFY 120, H `0x155`. The shared owner latches from the guest's own published display mode and reports `aspect=16:9 presentation 684x240 (margin 86px), projection 512x240 (margin 86px, centre X 342)`, and a presented in-level capture shows the widened scene with the player unchanged in size.
- where: titles/spyro3/core/spyro3_widescreen_facts.h; game/core/guest_widescreen_owner.*
- gap: the single `SetGeomOffset` call the route makes is INCOMPARABLE under the override differential ("original path performed platform-service @0x8005D35C"), so the leaf is not yet shown equal to retail; it is also called only once in the whole route.

### spyro3.moby-visibility — Own Spyro 3's moby visibility walk natively
- status: re-partial
- deps: spyro3.identity
- evidence: `0x80030478` (811 insns, matching Spyro 2's `0x80043858` in size) walks the same 0x58-byte records through the same sphere-frustum logic, including the `4*x - 3*z` term. Override differential at 4:3 over the in-level route: 26/26 sampled calls match retail, 0 mismatches.
- where: game/render/guest_moby_*; titles/spyro3/render/spyro3_render_facts.h
- gap: none on this route.

### spyro3.terrain-drawer — Own Spyro 3's terrain drawer natively
- status: re-partial
- deps: spyro3.identity
- evidence: `0x80022378` (5487 insns, matching Spyro 2's `0x80023BB4`), with the sector-visibility leaf `0x8002D0D8` called from `0x800223C8`, the visible-sector count at `0x8006D078`, outcodes `0x000D0000`/`0x00E40000`/`0xFE00`/`0x02000000`, and the caller analogue at `0x8001EC24`. Override differential at 4:3 over the in-level route: 26/26 sampled calls match retail, 0 mismatches.
- where: game/render/guest_terrain_*; titles/spyro3/render/spyro3_render_facts.h
- gap: the margin objects the widening reveals are not shown to be ANIMATED from object memory; only their rendering is covered.

### spyro3.hud-anchor — Anchor Spyro 3's in-level HUD at the widened edges
- status: re-partial
- deps: spyro3.widescreen
- evidence: the HUD pass is `0x80029E48`, called from `0x8001E460` under bit `0x20`, dispatching one drawer per widget through the table at `0x8006727C` that `0x8002803C` fills; `0x800285A4` registers three groups — `FUN_8002803C(0x40, 3 widgets, init 80027E40, step 80027A60, DRAW 80029904, value *0x8006C71C)`, `(0x41, 4 widgets, init 80027E40, step 80027B0C, DRAW 80029BB0, value *0x8006C784)` and `(0x42, 5 widgets, init 80027E40, step 80027A60, DRAW 80029904, value *0x8006C660)`. Each drawer computes its X from the element's first short and emits an ICON through `0x800289C8(icon, x)` and a VALUE through `0x800291B8(value, x, y, digitWidth)`. Measured elements and classes: collectable counter `0x80067248` authored x 20 y 28 (LeftEdge), lives counter `0x8006729C` authored x 256 y 28 (Centred, authored ON 0x100), egg counter `0x800672F0` authored x 492 y 28 (RightEdge). Classification CANNOT be by return address on this title: both counters are drawn by `0x80029904` and reach the SAME two emitter call sites — measured in one frame, the icon emitter's ra is `0x800299B8` for the gem at x 20 AND the egg at x 382, and the value emitter's ra `0x800299E4` for both (x 61 and x 423) — so the element is carried down one level by `spyro::hud_draw_context::Draw`. The icon emitter is NOT corrected on every call either: `FUN_800291B8` draws its digits by CALLING `FUN_800289C8` once per glyph from `0x80029374` (return `0x80029378`), so correcting the value emitter and then those glyphs applies the margin TWICE — measured at 684, the collectable count lands at x -111 instead of -25 and is drawn off the left edge so the number is absent, and the egg's icon lands at 595 instead of 468 and is jammed against the right edge, which reads as the icon and the count having swapped sides; the value emitter therefore opens `hud_draw_context::Draw::insideValue` and the icon emitter leaves that interval alone. Presented evidence at 684x240 after the fix: collectable icon columns 21..49 against 20..50 at 4:3, its digit 61..84 against 61; egg digit 595..618 against 423..446, i.e. +172 = the full widening; icon left of count on both sides; lives centred; 4:3 capture PIXEL-IDENTICAL to the pre-change reference, 0 of 122880 pixels differing.
- where: titles/spyro3/render/spyro3_hud_anchor.*; game/render/hud/hud_draw_context.h
- gap: the meter's absence is not re-measured for this title, and widgets shown only by the `0x41`/`0x42` state functions under other game states have not been enumerated.

### spyro2.moby-visibility — Own Spyro 2's moby visibility walk natively
- status: re-verified
- deps: spyro2.identity
- evidence: FUN_80043858 (811 insns) is native: the framework override differential (PSXPORT_OVERRIDE_DIFF, every call) matched retail on 4074 of 4074 calls at 4:3 over Glimmer, the attract demos and the page-turn loader (interpolated and mirrored rotations), RAM, GTE and v0/v1 included; 684/684 again after its globals moved to spyro2_render_facts.h, and 25/25 again after the walk, its frustum, GTE, rotation and globals were lifted to the shared game/render/guest_moby_* with only per-image facts left in the title. It walks the 0x58-byte moby records at *0x80066F14 and is called only from the object half of the frame draw FUN_8004C534, ahead of the moby drawers FUN_80044504, FUN_80046FD8 and the close-moby drawer FUN_800499D4; the terrain is FUN_80023BB4, called from FUN_8004C4FC
- where: game/render/guest_moby_*; titles/spyro{2,3}/render/spyro{2,3}_render_facts.h
- gap: none

### spyro2.hud-anchor — Anchor Spyro 2's in-level HUD at the widened edges
- status: re-verified
- deps: spyro2.identity
- evidence: the HUD is FUN_80053E78 (object-draw pass bit 0x20 of FUN_800155A0), emitting 2D textured quads/sprites through FUN_800520CC(image, x, y, size): gem counter FUN_8005251C(0x8006765C, x 0x28), orb counter FUN_8005251C(0x80067664, x 0x198), right-edge meter FUN_80052D84 (x 0x200 - slide, emitter calls 80052E8C/EA0/EB4/F0C/F34), lives FUN_80052B88 (head x 0xD0). Select toggles it in Glimmer. Native entry overrides apply ui_anchor::correction to the counters' x argument and to the meter's emitter x; override differential at 4:3: 59/59 counter and 108/108 emitter calls match
- where: titles/spyro2/render/spyro2_hud_anchor.*
- gap: the meter path is not exercised on the measured route (no meter shown)

### spyro2.terrain-drawer — Own Spyro 2's terrain drawer natively
- status: re-verified
- deps: spyro2.identity
- evidence: FUN_80023BB4 (5487 insns, every pass: classify 80023C0C, detail 80024534, translucent 80025434, coarse split 80025CC8, fine split 80026C74, GPU-size re-split 80028504, far 80028B14) is native with no guest hand-off; override differential at 4:3, every call: 0 mismatches over the Glimmer route (683/684, 1 incomparable interrupt), the long Glimmer route (1060/1062, 2 incomparable) and 60000 attract-demo fields (12886/12886), and 25/25 again after the drawer was lifted to the shared game/render/guest_terrain_* with only per-image facts left in the title. Its horizontal outcodes test [-margin, 512+margin) at 16:9, so the ground fills both margins in presented captures
- where: game/render/guest_terrain_*
- gap: the re-split pass (GT3/GT4 too large for the GPU) is not shown to have run on the routes measured. FUN_80046FD8's OFX/OFY = 256/120 restore at LAB_80047EA8 changes nothing visible at 16:9 (patched live to 342 and to 0 in Glimmer: frames identical to the unpatched timeline)

### guest.temporal-fence — Present the 60fps in-between from the frame that carried geometry
- status: re-verified
- deps: guest.temporal-scopes
- evidence: `Fps60::present` only runs when the presentation fence is committed THROUGH the temporal product. `FieldOwner::deliverField` was committing it as a plain `presentation.commit`, so `fps60=1` printed its banner and changed no picture at all; and a field that captured no primitives (Spyro runs its 30 Hz scene on alternate fields) presented through the temporal product retired the paired endpoint, so the next real field had nothing to interpolate toward. The fence now goes through the temporal product on a field that captured primitives and through the plain fence on one that captured none. With that, consecutive presented captures alternate real/in-between/repeat instead of real/repeat, the census on `fps60guest` reports 35.9 % (Spyro 2) and 38.8 % (Spyro 3) of polygons paired with real interpolated vertices emitted per in-between, and guest RAM plus scratchpad stay byte-identical with fps60 on and off
- where: game/core/field_owner.cpp
- gap: a 4:3 real frame still differs by 60-74 of 122,880 pixels between fps60 on and off, all of them 1-2 px animated sparkle sprites whose phase follows the host clock; every static pixel is identical

### guest.camera-builder — Build the in-between's camera the way the guest builds its own
- status: re-verified, and in the product
- deps: spyro2.terrain-drawer, spyro3.terrain-drawer
- evidence: the five words the drawer loads into the GTE's rotation registers (SCUS_944.25 `0x80067E84`, SCUS_944.67 `0x8006DFF8`) are WRITTEN BY THE GUEST, and the thing it writes them from is three angles. `otattr` last-writer provenance on `0x80067E84..0x80067EA8` names one function for both matrices — the per-field body, SCUS_944.25 `0x800156FC`, SCUS_944.67 `0x8001E638` — and each opens with two calls before anything else: `FUN_8001C2F8(&0x80067EC8, &0x80067E98, &0x80067E84)` and `FUN_8001BDB0(&0x80067EB8, &0x80067EAC)`. The first argument is the camera STATE: three signed 16-bit ANGLES and nothing else
- what the builder does, recovered from the two bodies (which are the same routine at different addresses): it reads its two 256-entry 16-bit tables (`0x80061BD8` sine, `0x80061C58` cosine on SCUS_944.25; `0x800658A0` / `0x80065920` on SCUS_944.67) by `(angle & 0xFFF) >> 4` with the low nibble as the interpolation fraction, lays out `Ry`, `Rz` and `Rx` into the GTE's nine rotation elements at scratchpad halfwords 0, 10 and 20, multiplies them with three `MVMVA sf` column products (`FUN_80059D6C`), writes the product to `0x80067E98`, multiplies row 1 (`R21`, `R22`, `R23` — halfwords 6, 8, 10) by `0x140 >> 9`, and writes that to `0x80067E84`
- the two matrices are why the row scale matters: the classification pass's sphere tests use the UNSCALED one and the drawer uses the scaled one, and MEASURED they differ in exactly three elements (678 -> 423, 3869 -> 2418, 1160 -> 725 on SCUS_944.25, which is `(x * 0x140) >> 9`)
- the builder is bit-exact when re-run natively over the guest's own tables: 24 consecutive cameras on SCUS_944.25 and 20 on SCUS_944.67, each read in ONE control-channel reply with its own angles (read separately the two are one field apart and never agree — the guest updates the angles after it builds), and every element the GTE reads comes back equal. The one word that differs is the HIGH HALF of CR4, which holds a saturated depth register in the guest and which the GTE discards
- what it is in the product: `game/render/terrain/guest_camera_builder.{h,cpp}`. An in-between interpolates the three angles, each the short way round its 4096-step turn, and re-runs the guest's own maths over host memory. The interpolation of packed control words — per element or whole — is gone, and with it the question of which was right: the words are products of those angles, truncated back into 16 bits, so they are not values that move linearly with the camera
- what it cost, measured: with a camera that is genuinely between two of the guest's, the traversal defers the union of both endpoints' splits, and SCUS_944.67's first in-between wrote a split entry `0x30C4` above the scratch block, past its own top. The in-between's scratch window now runs to `frameTop` — the end of every range it does NOT own — instead of to the block the guest sized for one camera (terrain_world_pass.cpp)

### guest.moby-provenance — Give Spyro 2/3 moby primitives a pairable identity
- status: blocked on a decision, evidence gathered
- deps: guest.temporal-scopes, spyro3.moby-visibility
- evidence: the moby walk `0x80030478` is native and knows each record, but it does not compute the moby's screen vertices: it writes a 0x44-byte render-list entry (record at +0x40, mesh at +0x04, rotation at +0x28) and hands the list base to the guest in GTE data registers (VXY1 = scratch base - 0x3000, VZ1 = the parked parameter at `0x8006C5DC`). Spyro 3's three moby mesh drawers are `0x80031124`, `0x80033C5C` and `0x8002DDA8` (they are the three calls `FUN_8001EC5C` makes straight after the walk, one per list the walk builds; `0x8003CDA0` behind the state guard is the 2D/overlay pass, not a moby drawer). Each is ONE guest call covering the whole list, and the per-moby loop is inside it (`FUN_80031124` alone is 2153 lines of Ghidra C). So the per-moby submission boundary the walk owns does not exist: the walk's entry is written before the drawer runs, and the drawer picks up the base from a GTE register rather than an argument. This is why moby primitives never pair and are presented at their real positions
- gap: two ways forward, both real work. (a) Take the three mesh drawers native (a port of roughly 3300 lines of decompiled C, each needing its own override differential at 4:3) and open a scope of (record, mesh) around each moby's own loop, letting the existing RTPS/RTPT sampler fill in the vertices. (b) Keep the drawers guest and add a title-side attribution pass: the captured queue arrives in ordering-table order, so a pass that walks the render-list entries and attributes each queued polygon to its entry can record `ProjectedVertex`s with scope (record, mesh), epoch 0 and the vertex ordinal as the identity, using the queue item's own guest XY as the screen point. (b) needs a second projection source alongside `ProjectionProvenance`, which the framework's `GuestGeometrySceneSource` does not currently accept. Not started; the choice is the user's

### guest.sparkle-phase — Real frames must not differ with fps60 on
- status: open defect, cause narrowed but not identified
- deps: guest.temporal-fence
- evidence: at 4:3 the same route point's real present differs between `fps60=1` and `fps60=0` in 74-83 of 122,880 pixels, in 23-25 separate 1-9 pixel blobs confined to the sparkle/glint sprites; every terrain, moby and HUD pixel is identical. Both runs report `fields=5677 product_steps=3393`, and guest RAM plus scratchpad dumped at the shot point (`scratch/s3wide/s28/shot43.py`, which takes the shot and the dump together) are byte-identical, so it is neither guest state nor a different number of guest frames. Two `fps60=0` runs are byte-identical to each other, so it is fps60 and not route jitter
- what was ruled out: the port-side sparkle producer. `submitSparkles` lives in `glow_sparkle.cpp` and is reached only through `spyro_field_model_chain_submit`, which is called from `frame_renderer_frame.cpp` (Spyro 1's native render frame), `dragon_scene_producer.cpp` and `menu_world_pass.cpp`; no Spyro 2/3 path calls it, and the runtime's own `glow` debug channel emits nothing for either title. The glints in a Spyro 3 frame are therefore guest packets, not port output, and there is no host-side sparkle input to remove
- the GPU-state-bleed hypothesis is NOT supported by the code as written: `GpuVkState::frame_end` resets every per-pass accumulator (`s_tri_n`, `s_line_n`, `s_tex_n`, the painter state, the semi-transparent runs and all 2D bands) before returning, so the in-between slot does not hand a batch, a blend mode or a texture to the real slot. The framebuffer is shared between the two slots, which is the part not yet ruled in or out
- gap: unexplained. `final.ppm` comparisons and the two 4:3 preseq sets are under `scratch/s3wide/s28/`

### guest.frame-identity — Spyro 2/3 terrain vertices must keep one identity across frames
- status: root cause found, fix needs a framework seam that is not green-lit
- deps: guest.temporal-scopes
- evidence: measured directly, by dumping every identity component of the first resolved polygon of all710 presented frames of the Sunrise Spring route (`resolve()` in psxport, temporary, reverted). Of those, 324 could not be paired with the previous frame and 386 could. In the unpaired group the scope, the epoch and the V0 x/y are all IDENTICAL to the paired group; only V0 z differs, and it climbs 4179, 4180, 4181, 4182, 4183, 4184 — one per frame — while the paired group's z is constant at 2708. So a terrain vertex pairs exactly when the camera's z has not moved since the previous frame
- why: the identity is (scope, epoch, V0..V2). `guest_terrain_mesh.cpp`'s `unpack()` builds V0 as `z = ((word >> 19) & 0x1FFC) + origin.z` and `x|y = (origin.x - ((word >> 8) & 0x1FFC)) | ((origin.y - height) << 16)`, where `origin` is the sector origin RELATIVE TO THE CAMERA. So all three identity components are camera-relative by construction, and the only frame-stable handle the producer holds is the static 32-bit vertex word it unpacked from. Retail's own drawer does the same, and the 4:3 override differential (26/26 terrain-drawer calls) forbids changing it
- what is already fixed: `FieldOwner::deliver` takes a field to the temporal product only when the guest's own native scene producers ran on it (`SpyroContext::sceneProducerTicks`). Spyro's near-empty fields used to reach the presenter, which put a held copy on screen AND adopted their projection set as the pairing endpoint. Presented frames 1177 -> 713; frames with a live previous endpoint 60 % -> 99.9 % (707 of 713)
- the fix, and why it is not done: the scope key is already the title's, so the missing piece is the identity's INPUT component, which `ProjectionProvenance::record()` reads from the GTE operands. The smallest title-neutral seam is for a `Scope` to name a title-supplied identity for the vertices recorded inside it, which this producer would fill from the static vertex word it already holds. That is a new capability in psxport's temporal path, not a bug fix, and it is the operator's call. No heuristic was applied: quantising the input or matching on a tolerance would pair the wrong vertices whenever two of them sit within that tolerance
