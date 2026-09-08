# Codemap — SpyroEngine

This map owns placement only: which subsystem owns a responsibility, where it lives now, and where
new work belongs. Product intent is in `docs/project-goals.md`; capability state in
`docs/project-state.md`; atomic work in `docs/issues/`; migration order in `docs/migration.md`; and
binary-evidence dependencies in `docs/re-frontier.md`.

## Architecture

```text
run.sh -> locked Python launcher -> authenticated title image
                                      |
                                      v
                         psxport per-Core Lightrec executor
                         | dispatch | exits | invalidation |
                                      |
                         selected Spyro title runtime
                           |                    |
                 image-scoped native leaves   scoped original call
                           |                    | through Lightrec
                           +---------+----------+
```

`game/core/` contains process composition, root continuation, and lineage policy.
`titles/spyro2/` holds Spyro 2 identity, runtime policy, and retained bootstrap semantics.
`SpyroRuntime` owns only proven address-free lineage policy. Each final title runtime owns its
executable identity, image-aware addresses, lifecycle, and capability policy. psxport owns PSX CPU
execution and services; title code must not fork Lightrec or reproduce a second cache/dispatcher.

## Ownership

| Subsystem | Responsibility | Current / target location | Entry point | Deep doc |
| --- | --- | --- | --- | --- |
| Player launcher | Frozen Python environment, dependency refusal, title selection, authentication, product build and launch | `run.sh`, `bootstrap.py`, `tools/run.py` | `tools/run.py::main` | `docs/migration.md` |
| Title identity | Serial, PS-X EXE header, size, hashes, labels, and environment keys; one runtime SHA-256 owner for executable and WAD bytes | `titles/spyro*/executable.json`, `tools/title_identity.py`, `tools/generate_title_catalog.py`, `game/core/content_identity.*` | title catalog loader; `spyro::sha256` | `docs/project-state.md` |
| Runtime image provisioning | Extract and authenticate the selected executable without emitting guest bodies | `tools/provision_title.py`; title manifests remain fact authority | `provision_title.provision` | `docs/migration.md` |
| PSX guest executor | Per-`Core` Lightrec instance, CPU/device synchronization, code cache, bounded exits, and invalidation | `external/psxport/runtime/cpu/`; no title-local executor | `psx::cpu::dispatchGuest` | `docs/migration.md` |
| Runtime dispatch | Complete image identity, native overrides, scoped original calls, and override-change invalidation | `external/psxport/runtime/cpu/native_dispatch.*` | `dispatchGuest`, `callOriginal` | `docs/migration.md` |
| Runtime observation | Per-instance delivered-field cap, graceful completion after the product fence, and executor telemetry | `game/core/runtime_run.*`, `game/core/main.cpp` | `RuntimeRun`, `reportRuntimeRun` | `docs/project-state.md` |
| Root guest continuation | Preserve the root return address and committed PC across bounded Lightrec budget yields; propagate other exits | `game/core/guest_execution.*` | `GuestExecution::step` | `docs/migration.md` |
| Lineage runtime | Address-free executable/capability defaults and title-runtime registry | `game/core/spyro_runtime.*`, `game/core/title_runtime_registry.*` | `SpyroRuntime`, title runtime factory | `AGENTS.md` |
| Spyro 1 runtime | `SCUS_942.28` image policy and image-scoped verified native leaf overrides; unchanged guest gameplay through Lightrec | `titles/spyro1/core/spyro1_runtime.*`, `game/core/native_{rand,leaf,vec,gte,angle,util}.cpp` | `Spyro1Runtime::registerOverrides` | `docs/re-frontier.md` |
| Spyro 2 runtime | `SCUS_944.25` image policy and explicit missing JIT execution boundary | `titles/spyro2/core/spyro2_runtime.*` | `Spyro2Runtime` | `docs/re-frontier.md` |
| Spyro 3 runtime | `SCUS_944.67` identity and explicit unimplemented boundary | `titles/spyro3/core/` | `Spyro3Runtime` | `docs/re-frontier.md` |
| Frame lifecycle | Finite native boot and JIT update steps; field delivery owns simulated display time, callbacks and cadence; presentation waits for already-delivered fields | `titles/spyro1/core/spyro1_{boot_sequence,field_scheduler,frame_driver,runtime}.*` | `Spyro1FrameDriver`, `FieldScheduler::deliver`, `Spyro1Runtime::pacePresentation` | `docs/re-frontier.md` |
| Transition skipping | Cancel a presentation-only transition screen on Start/Cross by taking its guest owner's own terminal transition; boot logos own theirs separately | `titles/spyro1/core/spyro1_transition_skip.*`, `spyro1_boot_sequence.*` | `spyro1::classify`, `TransitionSkip::observe`, `BootSequence::leaveFirstPresentationHold` | `docs/findings/start-skip-map.md` |
| Resumable world execution | Execute unchanged retail world work through Lightrec and return bounded host-service exits | `game/core/world_guest_execution.*` | `WorldGuestExecution::resume` | `docs/migration.md` |
| Disc/archive service | Title loader ABI/state publication, bounded atomic WAD transfer, byte-content image identity and per-Core completion; framework memory writes own executable invalidation | `game/core/cd_queue.cpp`, `game/core/archive_transfer.*`; composed by `SpyroContext` | `spyro_register_cd_queue`, `ArchiveTransfer::read` | `docs/issues/0091-native-cd-stream-can-acknowledge-a-short-archive.md` |
| Input and memory card | Field pad delivery and title pad-buffer layout; guest card logic uses framework services | `titles/spyro1/core/spyro1_field_scheduler.*`, `spyro1_runtime.*`; framework pad/card owners | `FieldScheduler::deliver`, `guestPadBufferLayout` | `docs/project-state.md` |
| Audio | Per-field service of the framework SPU owner | `titles/spyro1/core/spyro1_field_scheduler.*` | `FieldScheduler::deliver` | `docs/project-state.md` |
| Render orchestration | Scene classification, native layer composition, explicit reference entry, and presentation fence | `game/render/scene.cpp`, `render_frame.cpp`, `render.h` | `SpyroRenderer::drawFrame` | `docs/re-frontier.md` |
| Presentation ownership | Per-instance choice between guest VRAM and native scene presentation | `game/render/presentation_owner.*`; draw/display environment lifecycle in `frame_env.*` | `spyro_presentation_owner`, `nativeFrameBegin`, `nativeFrameEnd` | `docs/project-state.md` |
| Stage-13 title/save scenes | Front-end state decode, menu sprites, screen actor queue, and backdrop composition | `game/render/title_menu_*`, `stage13_scene_recipe.*`, `fx_title_menu.cpp`, `fx_sprite_queue.cpp` | `SpyroRenderer::titleMenuRender`, `stage13Mode3Render` | `docs/re-frontier.md` |
| Stage-14 cutscene | Actor/world/cyclorama/fade composition and cutscene-local presentation policy | `game/render/cutscene_scene_recipe.*`, `render_frame.cpp` plus producer peers | `SpyroRenderer::prepareScene`, `renderScene` | `docs/re-frontier.md` |
| FIELD scene | Ordered collectable, actor, shadow, environment, cyclorama, particle, fade, border, and tracer composition | `game/render/field_scene_recipe.*`, `field_moby_lists.*`, `fx_field_*`, `render_frame.cpp` | `SpyroRenderer::renderScene` | `docs/re-frontier.md` |
| World geometry | Owned unprojected sector occurrences, camera/projection/material capture and authored resource spans; admitted draw state; one endpoint and sampled raw-view classification/projection path; LQ/HQ/refinement recipes, animation and queue submission | `game/render/world_source.*`, `world_scene_builder.*`, other `world_*`, `fx_world_draw.*`, `fx_field_environment.*` | `world_scene::capture`, `world_scene::build(const Source&)`, `world_scene::sample`, `spyro_world_submit`, `spyro_field_environment_submit` | `docs/findings/world-semantic-oracle.md` |
| Actor geometry | Model decode, transform/projection, acceptance, shadow-state publication, and regular/secondary/shaded submission | `game/render/actor_*`, `secondary_actor_*`, `field_shaded_queue_*`, `fx_actor_draw.*`, `fx_field_actor_composition.*` | `spyro_actor_submit`, `spyro_field_actor_composition_submit` | `docs/re-frontier.md` |
| Paired player geometry | Layer pose decoding, transform construction, primitive/material resolution, and compatible temporal replay | `game/render/paired_actor_pose.*`, `paired_actor_depth.*`, `paired_actor_decode.*`, `fx_paired_actor.*`, `paired_actor_temporal_evidence.*` | `spyro_paired_actor_submit`, `spyro_paired_actor_rebuild_endpoint` | `docs/re-frontier.md` |
| Temporal scene integration | Per-game source admission scratch, dynamic producer participation, camera/frame provenance, world resource residency history and shared-presenter reconstruction/rotation | `game/render/temporal_scene.*`, `world_temporal.*`, `scene_camera_inputs.h` | `spyro_temporal_scene_begin`, `spyro_temporal_scene_prepare`, `spyro_temporal_scene_source`, `world_temporal::History` | `docs/project-state.md` |
| Cyclorama/portals | Source sky geometry, aperture projection, near/mid mesh, masks, and queue submission | `game/render/native_terrain.cpp`, `cyclorama_*`, `fx_field_cyclorama.*` | `spyro_terrain_submit`, `spyro_field_cyclorama_submit` | `docs/re-frontier.md` |
| Actor OT coalescing | Local bucket spans to shared world bins, including empty buckets and the near clamp; regular record keys and paired source depth | `game/render/actor_ot_coalescer.*`, `actor_global_order.*`, `paired_actor_depth.*`; paired submission in `fx_paired_actor.cpp` | `actor_ot_coalescer::map`, `actor_global_order::build`, `paired_actor_depth::derive` | `docs/findings/paired-actor-world-order.md` |
| Painter ordering | Title draw-order keys and queue admission shared by native producers | `game/render/scene_painter_order.*`, `painter_submission_preflight.*`; framework `RenderQueue` owns admission policy | `scene_painter_order`, `painter_submission::preflight` | `docs/re-frontier.md` |
| Historical render evidence | Durable claims, issues, instrument records, and independent semantic record comparison | `docs/info/`, `docs/issues/`, `game/render/world_scene_oracle.*`, `world_scene_capture.*` | `tools/info.py brief`, `world_scene_oracle::compare` | `docs/info/instruments/` |
| Build composition | Sole runtime product, framework linkage, and separation of player versus maintainer builds | `CMakeLists.txt` | `spyro_port` | `docs/migration.md` |
| Hermetic and runtime verification | Focused production-boundary tests plus reusable input replays | `tests/`, `titles/*/tests/`, `replays/` | CTest and project verifier | `docs/project-state.md` |
| External RE references | Read-only public decompilation references used only to cross-check names and structure | `external/open-spyro/`, `external/spyro-1/` | reference source lookup | `docs/references.md` |
| Project registries | Goals, state, ownership, issues, migration, RE frontier, claims, and instruments | `docs/` | `tools/info.py brief` | `AGENTS.md` |
| Shared framework | Lightrec executor, PSX services, test harnesses, SDL_GPU renderer, and title-neutral native seams | `external/psxport/` resolved checkout | framework runtime seam | framework `AGENTS.md` |

## Where does new work go?

- Decoder/lowering, Lightrec integration, cache, invalidation, or bounded executor exits →
  `external/psxport/`.
- A title serial, hash, load range, or runtime image fact → `titles/<title>/` and its manifest-backed
  runtime policy.
- A Spyro 1 frame, field, input, audio, or lifecycle transition → `titles/spyro1/core/`.
- A semantic draw responsibility → one cohesive `game/render/` recipe/builder/submitter owner; the
  scene composer only orders owners.
- A runtime WAD identity or load/unload observation → the shared executor image tracker; title-specific
  archive semantics stay in the title CD/archive owner.
- A diagnostic → an oracle/capture module that cannot mutate or submit the shipping picture.
- A capability change → `docs/project-state.md`; an atomic task/finding → `docs/issues/`; a binary
  dependency step → `docs/re-frontier.md`.
