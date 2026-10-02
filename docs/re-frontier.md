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
- where: titles/spyro1/core/spyro1_frame_driver.*; game/render/render_frame.cpp; game/core/runtime_run.*
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
- evidence: `SCUS_944.67` has its own manifest, serial, entry, size, and digest facts.
- where: titles/spyro3/executable.json; titles/spyro3/core/spyro3_runtime.*
- gap: Disc provenance and dynamic product execution remain unverified.
- notes: Spyro 3 implementation waits for Spyro 1's representative gameplay gate.

### spyro2.moby-visibility — Own Spyro 2's moby visibility walk natively
- status: re-verified
- deps: spyro2.identity
- evidence: FUN_80043858 (811 insns) is native: the framework override differential (PSXPORT_OVERRIDE_DIFF, every call) matched retail on 4074 of 4074 calls at 4:3 over Glimmer, the attract demos and the page-turn loader (interpolated and mirrored rotations), RAM, GTE and v0/v1 included; 684/684 again after its globals moved to spyro2_render_globals.h. It walks the 0x58-byte moby records at *0x80066F14 and is called only from the object half of the frame draw FUN_8004C534, ahead of the moby drawers FUN_80044504, FUN_80046FD8 and the close-moby drawer FUN_800499D4; the terrain is FUN_80023BB4, called from FUN_8004C4FC
- where: titles/spyro2/render/spyro2_moby_*
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
- evidence: FUN_80023BB4 (5487 insns, every pass: classify 80023C0C, detail 80024534, translucent 80025434, coarse split 80025CC8, fine split 80026C74, GPU-size re-split 80028504, far 80028B14) is native with no guest hand-off; override differential at 4:3, every call: 0 mismatches over the Glimmer route (683/684, 1 incomparable interrupt), the long Glimmer route (1060/1062, 2 incomparable) and 60000 attract-demo fields (12886/12886). Its horizontal outcodes test [-margin, 512+margin) at 16:9, so the ground fills both margins in presented captures
- where: titles/spyro2/render/spyro2_terrain_*
- gap: the re-split pass (GT3/GT4 too large for the GPU) is not shown to have run on the routes measured. FUN_80046FD8's OFX/OFY = 256/120 restore at LAB_80047EA8 changes nothing visible at 16:9 (patched live to 342 and to 0 in Glimmer: frames identical to the unpatched timeline)
