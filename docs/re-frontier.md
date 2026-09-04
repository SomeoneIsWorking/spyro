# RE Frontier — ordered evidence chain for Spyro's dynamic runtime

Tracked by `tools/re_frontier.py`. This is the fine-grained companion to
`docs/codemap.md`: it records the binary or asset evidence required before each dynamic-runtime
boundary can be implemented and verified.

The product executes the authenticated PSX image through psxport's Lightrec runtime. It does not
generate guest source or ship a generated guest corpus. An interpreter may exist only in a separate
test or diagnostic target and is not a gameplay fallback. A mechanism trace is not fidelity
evidence; a step becomes `re-verified` only after its observable result matches the real title on
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
- gap: The frozen Lightrec backend and synthetic framework contract are linked and verified; real Spyro media has not yet proven nonzero translated blocks, title dispatch, or product-link interpreter exclusion.
- notes: This is a shared-runtime prerequisite, not a title-specific code-generation task.

### dynarec.dispatch — Route native overrides and original calls by image identity
- status: re-partial
- deps: dynarec.executor
- evidence: WAD images reuse guest load addresses; the runtime API therefore carries image identity through native dispatch and scoped original calls.
- where: game/core/guest_execution.*; game/core/world_guest_execution.*; external/psxport/runtime/cpu
- gap: Prove one native override, one scoped original call, and positive plus controlled-negative invalidation across two resident images.
- notes: Guest address alone is never a valid cache or override key.

### dynarec.stage13 — Reach the 800/900 stage-13 discriminators
- status: todo
- deps: dynarec.dispatch
- evidence: Retired-route records identify the 800-field title path and 900-field mode-2 save-picker path; those records are wiring evidence only.
- where: titles/spyro1/core/spyro1_runtime.*; game/core/guest_execution.*; target psxport executor
- gap: Reproduce both routes with nonzero Lightrec blocks, native scene owners active, exactly one presentation fence per host field, and fatal guest VSync.
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
- evidence: The project goal defines a bounded interactive route with independent state comparison and native/original dispatch coverage.
- where: docs/project-goals.md; docs/project-state.md; tools/verify.py
- gap: Compare timing, interrupts, memory, and relevant device state against an independent emulator; exercise WAD invalidation; prove no interpreter in the gameplay product; meet the declared host frame-time budget.
- notes: Boot, logos, menus, FMV, and a clean trace are not gameplay conformance.

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
