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
                 native lifecycle/services   guest original call
                           |                    | through Lightrec
                           +---------+----------+
                                     |
                         native semantic render owners
```

`SpyroRuntime` owns only proven address-free lineage policy. Each final title runtime owns its
executable identity, image-aware addresses, lifecycle, and capability policy. psxport owns PSX CPU
execution and services; title code must not fork Lightrec or reproduce a second cache/dispatcher.

## Ownership

| Subsystem | Responsibility | Current / target location | Entry point | Deep doc |
| --- | --- | --- | --- | --- |
| Player launcher | Frozen Python environment, dependency refusal, title selection, authentication, product build and launch | `run.sh`, `bootstrap.py`, `tools/run.py`; target removes offline translation | `tools/run.py::main` | `docs/migration.md` |
| Title identity | Serial, PS-X EXE header, size, hashes, labels, and environment keys | `titles/spyro*/executable.json`, `tools/title_identity.py`, `tools/generate_title_catalog.py` | title catalog loader | `docs/project-state.md` |
| Runtime image provisioning | Extract and authenticate the executable and WAD images without emitting guest bodies | target provisioning owner beneath `tools/`; existing title manifests remain fact authority | target runtime-image provision entry | `docs/migration.md` |
| PSX guest executor | Per-`Core` Lightrec instance, CPU/device synchronization, code cache, bounded exits, and invalidation | target `external/psxport/runtime/`; no title-local executor | target framework executor interface | `docs/migration.md` |
| Runtime dispatch | Complete image identity, native overrides, scoped original calls, and override-change invalidation | target psxport dispatcher with title registration in `titles/spyro*/core/` | target executor call boundary | `docs/migration.md` |
| Lineage runtime | Address-free executable/capability defaults and title-runtime registry | `game/core/spyro_runtime.*`, `game/core/title_runtime_registry.*` | `SpyroRuntime`, title runtime factory | `AGENTS.md` |
| Spyro 1 runtime | `SCUS_942.28` lifecycle, measured policies, native owners, and override registration | `titles/spyro1/core/`, residual compatibility seams in `game/core/` | `Spyro1Runtime`, `Spyro1FrameDriver` | `docs/re-frontier.md` |
| Spyro 2 runtime | `SCUS_944.25` boot policy and measured display-bootstrap owner | `titles/spyro2/core/` | `Spyro2Runtime`, `Spyro2FrameDriver` | `docs/re-frontier.md` |
| Spyro 3 runtime | `SCUS_944.67` identity and explicit unimplemented boundary | `titles/spyro3/core/` | `Spyro3Runtime` | `docs/re-frontier.md` |
| Frame lifecycle | Finite title step, boot sequence, 60 Hz fields, callback root, input, audio, events, presentation and pacing | `titles/spyro1/core/spyro1_frame_driver.*`, `spyro1_boot_sequence.*`, `spyro1_field_scheduler.*` | `Spyro1FrameDriver::stepFrame`, `FieldScheduler` | `docs/re-frontier.md` |
| Resumable world execution | Execute unchanged retail world work through Lightrec and return bounded host-service exits | target executor integration replacing `game/core/world_body.inc` | target world resume boundary | `docs/migration.md` |
| Disc/archive service | Title loader semantics and authenticated WAD transfer; framework CD controller remains title-neutral | `game/core/cd_queue.cpp`, target runtime-image tracker in psxport | title loader overrides | `docs/issues/0003-spyro-uses-stock-libcd-reads-setloc-then-read-th.md` |
| Input and memory card | Pad packets, event-stack semantics, and native card operations | `titles/spyro1/core/spyro1_field_scheduler.*`, `game/core/native_memcard_*` | scheduler input service; card overrides | `docs/project-state.md` |
| Audio | Per-field SPU/XA service and title-owned hardware/upload seams | `titles/spyro1/core/spyro1_field_scheduler.*`, `game/core/native_spu_*` | field audio service | `docs/project-state.md` |
| Render orchestration | Choose the native scene owner, compose layers, and commit exactly one presentation | `game/render/render_frame.cpp`, `game/render/scene.cpp`, `game/render/frame_env.*` | `SpyroRenderer::drawFrame` | `docs/re-frontier.md` |
| Presentation ownership | Select upload-only guest-VRAM picture versus native scene picture per game instance | `game/render/presentation_owner.*`, `game/core/spyro_context.*` | `SpyroPresentationOwner` | `docs/project-state.md` |
| Stage-13 title/save scenes | State decode, bounded command recipes, native sprite submission, and diagnostic oracle | `game/render/title_menu_*`, `stage13_scene_recipe.*`, `fx_title_menu.cpp`, `fx_sprite_queue.cpp` | stage-13 scene composer | `docs/re-frontier.md` |
| Stage-14 cutscene | Actor/world/cyclorama/fade composition and cutscene-local presentation policy | `game/render/cutscene_scene_recipe.*` plus shared producer peers | cutscene scene composer | `docs/re-frontier.md` |
| FIELD scene | Ordered collectable, actor, shadow, environment, cyclorama, particle, fade, border, and tracer composition | `game/render/field_scene_recipe.*` and `fx_field_*` peers | FIELD scene composer | `docs/re-frontier.md` |
| World geometry | Immutable source capture, codec, projection, LQ/HQ/refinement recipes, animation, submitter, and oracle | `game/render/world_*` | `WorldSceneBuilder`, world submitter | `docs/findings/world-semantic-oracle.md` |
| Actor geometry | Model decode, transform/projection, acceptance, draw recipe, painter order, and submission | `game/render/actor_*`, `secondary_actor_*`, `fx_*actor*` | actor scene builders/submitters | `docs/re-frontier.md` |
| Cyclorama/portals | Sky, aperture projection, near/mid mesh families, mask, and painter submission | `game/render/cyclorama_*`, `fx_field_cyclorama.*` | cyclorama scene composer | `docs/re-frontier.md` |
| Render diagnostics | Snapshot-backed oracles and reachability probes that never own the shipping picture | `game/core/*oracle*`, `game/render/*oracle*`, capture modules | diagnostic-specific entry | `docs/info/instruments/` |
| Build composition | Product targets, framework linkage, and separation of player versus maintainer builds | `CMakeLists.txt`, `cmake/` | title CMake definitions | `docs/migration.md` |
| Hermetic and runtime verification | Focused production-boundary tests plus reusable input replays | `tests/`, `titles/*/tests/`, `replays/` | CTest and project verifier | `docs/project-state.md` |
| External RE references | Read-only public decompilation references used only to cross-check names and structure | `external/open-spyro/`, `external/spyro-1/` | reference source lookup | `docs/references.md` |
| Project registries | Goals, state, ownership, issues, migration, RE frontier, claims, and instruments | `docs/` | `tools/info.py brief` | `AGENTS.md` |
| Offline-translation retirement | Delete emitted corpora, emission-only seeds, generated dispatch/tests, and provisioning/build hooks after representative gameplay | current `generated/`, `game/recomp_seeds.json`, `tools/ensure_recomp.py`, `game/core/recomp_register.cpp`, `game/core/world_body.inc`; target is absence | migration landing step | `docs/migration.md` |
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
