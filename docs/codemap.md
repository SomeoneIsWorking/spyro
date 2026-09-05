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
| Title identity | Serial, PS-X EXE header, size, hashes, labels, and environment keys | `titles/spyro*/executable.json`, `tools/title_identity.py`, `tools/generate_title_catalog.py` | title catalog loader | `docs/project-state.md` |
| Runtime image provisioning | Extract and authenticate the selected executable without emitting guest bodies | `tools/provision_title.py`; title manifests remain fact authority | `provision_title.provision` | `docs/migration.md` |
| PSX guest executor | Per-`Core` Lightrec instance, CPU/device synchronization, code cache, bounded exits, and invalidation | `external/psxport/runtime/cpu/`; no title-local executor | `psx::cpu::dispatchGuest` | `docs/migration.md` |
| Runtime dispatch | Complete image identity, native overrides, scoped original calls, and override-change invalidation | `external/psxport/runtime/cpu/native_dispatch.*` | `dispatchGuest`, `callOriginal` | `docs/migration.md` |
| Root guest continuation | Preserve the root return address and committed PC across bounded Lightrec budget yields; propagate other exits | `game/core/guest_execution.*` | `GuestExecution::step` | `docs/migration.md` |
| Lineage runtime | Address-free executable/capability defaults and title-runtime registry | `game/core/spyro_runtime.*`, `game/core/title_runtime_registry.*` | `SpyroRuntime`, title runtime factory | `AGENTS.md` |
| Spyro 1 runtime | `SCUS_942.28` image policy and image-scoped verified native leaf/gameplay overrides | `titles/spyro1/core/spyro1_runtime.*`, `game/core/native_{rand,leaf,vec,gte,angle,util,gameplay}.cpp` | `Spyro1Runtime::registerOverrides` | `docs/re-frontier.md` |
| Spyro 2 runtime | `SCUS_944.25` image policy and explicit missing JIT execution boundary | `titles/spyro2/core/spyro2_runtime.*` | `Spyro2Runtime` | `docs/re-frontier.md` |
| Spyro 3 runtime | `SCUS_944.67` identity and explicit unimplemented boundary | `titles/spyro3/core/` | `Spyro3Runtime` | `docs/re-frontier.md` |
| Frame lifecycle | Target finite JIT/title step, 60 Hz fields, input, audio, events, presentation and pacing | new cohesive owners under `titles/spyro1/core/` after JIT conformance | not implemented | `docs/re-frontier.md` |
| Resumable world execution | Execute unchanged retail world work through Lightrec and return bounded host-service exits | `game/core/world_guest_execution.*` | `WorldGuestExecution::resume` | `docs/migration.md` |
| Disc/archive service | Target title loader semantics and authenticated WAD transfer; framework CD controller remains title-neutral | new title owner plus psxport runtime-image tracking | not implemented | `docs/issues/0003-spyro-uses-stock-libcd-reads-setloc-then-read-th.md` |
| Input and memory card | Target pad packets, field delivery, and card-operation ownership | new cohesive owners under `titles/spyro1/core/` | not implemented | `docs/project-state.md` |
| Audio | Target per-field SPU/XA service and title-owned synchronization | new cohesive owners under `titles/spyro1/core/` | not implemented | `docs/project-state.md` |
| Render orchestration | Target native scene selection, layer composition, and exactly one presentation | new cohesive owner under `game/render/` after JIT conformance | not implemented | `docs/re-frontier.md` |
| Presentation ownership | Target exactly one JIT guest-VRAM or native scene picture per game instance | new owner after frame-boundary conformance | not implemented | `docs/project-state.md` |
| Stage-13 title/save scenes | Preserved state decode and bounded command recipes awaiting new JIT-aware composition | `game/render/title_menu_*`, `stage13_scene_recipe.*` | not attached | `docs/re-frontier.md` |
| Stage-14 cutscene | Actor/world/cyclorama/fade composition and cutscene-local presentation policy | `game/render/cutscene_scene_recipe.*` plus shared producer peers | cutscene scene composer | `docs/re-frontier.md` |
| FIELD scene | Ordered collectable, actor, shadow, environment, cyclorama, particle, fade, border, and tracer composition | `game/render/field_scene_recipe.*` and `fx_field_*` peers | FIELD scene composer | `docs/re-frontier.md` |
| World geometry | Preserved source capture, codec, projection, LQ/HQ/refinement recipes, animation, and scene building; runtime submission removed | `game/render/world_*` semantic modules | `WorldSceneBuilder` | `docs/findings/world-semantic-oracle.md` |
| Actor geometry | Preserved model decode, transform/projection, acceptance, and draw recipes; runtime submission removed | `game/render/actor_*`, `secondary_actor_*` semantic modules | actor scene builders | `docs/re-frontier.md` |
| Cyclorama/portals | Preserved sky, aperture projection, near/mid mesh, and mask recipes; runtime submission removed | `game/render/cyclorama_*` semantic modules | recipe builders | `docs/re-frontier.md` |
| Historical render evidence | Durable claims, issues, and instrument records; retired runtime oracle wrappers are absent | `docs/info/`, `docs/issues/` | `tools/info.py brief` | `docs/info/instruments/` |
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
