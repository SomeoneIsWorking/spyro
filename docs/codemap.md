# Codemap — SpyroEngine

This map owns placement and ownership only: which directory, namespace and class owns a
responsibility, where it lives now, and where new work belongs. Product intent is in
`docs/project-goals.md`; capability state in `docs/project-state.md`; atomic work in `docs/issues/`;
migration order in `docs/migration.md`; binary-evidence dependencies in `docs/re-frontier.md`.

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

One process. One window and one presentation device belong to `spyro::ProductHost`; every title
session presents through them and nothing session-owned touches that object. psxport owns PSX CPU
execution and services; title code must not fork Lightrec or reproduce a second cache/dispatcher.

## Directories

| Directory | Namespace | What lives there |
| --- | --- | --- |
| `game/core/` | `spyro`, `spyro2`, `spyro3` and the title-neutral sub-namespaces below | Process composition, per-Core shared context, field delivery, guest execution, CD/archive, widescreen policy, memory card, SPU facts, native leaf overrides |
| `game/render/frame/` | `spyro`, `spyro::render` | The picture: scene classification, the native leg's frame, the shared GPU/coprocessor vocabulary |
| `game/render/field/` | `spyro`, `spyro::render` and one sub-namespace per layer | The field layers, their recipes and submit chain, the guest moby walk, the drawn-half tables |
| `game/render/actor/` | `spyro::actor_*`, `spyro::paired_actor*` | The regular, secondary and paired actor layers |
| `game/render/terrain/` | `spyro::guest_terrain`, `spyro::terrain_*` | The native terrain producer: the guest drawer as passes, and its derive/preflight/publish chain |
| `game/render/world/` | `spyro::world_*`, `spyro::cyclorama_*` | The world producer, its codecs and LQ/HQ recipes, the cyclorama and menu background |
| `game/render/temporal/` | `spyro::temporal`, `spyro::<layer>_temporal` | The 60 fps endpoint lifecycle every layer shares, and its two presentation-only censuses |
| `game/render/hud/` | `spyro::ui_anchor`, `spyro::hud_*`, `spyro::field_2d_overlay*` | The screen-space 2D layer and the ONE horizontal anchoring rule |
| `game/render/scene/` | `spyro::<scene>` | The front-end and cutscene scenes |
| `titles/spyro1/core/{frame,runtime,input,moby,camera,player,hud,effect,audio,execution}/` | `spyro1`, `spyro1::native` | SCUS_942.28's frame lifecycle, field scheduler, input, moby layer, camera, player, HUD, effects, audio, execution observers, and its native leaf overrides |
| `titles/spyro2/core/`, `titles/spyro3/core/` | `spyro2`, `spyro3` | Measured boot, logo and widescreen facts, plus the two runtime objects |
| `titles/spyro{2,3}/render/` | `spyro2`, `spyro3` | The per-image HUD anchoring overrides |
| `tests/`, `titles/*/tests/` | — | Hermetic tests over the shipping owners |

---

## Who owns it

Every chain names the owner and the method at each hop. Framework classes are `psx::…` and live in
`external/psxport/`; they are named here so a hop is findable, never because they are title code.

### The frame turn

```text
spyro::ProductHost::runToEnd            one loop per entered title; also the selector's loop
  └─ spyro::TitleSession::step          honour pause, honour the run cap, then one product step
       ├─ psx::debug::DbgServer::honourPause
       └─ dc_step_frame → psx::FrameLoopShell::step
            └─ <FrameDriver>::stepFrame          the title's one finite native frame step
                 ├─ spyro1::Spyro1FrameDriver::stepFrame   (SCUS_942.28)
                 │    ├─ spyro1::BootSequence::step        until the boot prefix has returned
                 │    ├─ runGuestUpdate                   the retail main-loop iteration, draw-less
                 │    │                                   iterations chained first
                 │    ├─ spyro::render::FrameRenderer::drawFrame
                 │    └─ spyro::deliverNativeField("native-frame-tail")
                 └─ spyro::BootPrefixFrameDriver::stepFrame   (SCUS_944.25 / SCUS_944.67)
                      ├─ spyro::GuestCall::begin / resume     the retail boot prefix, across steps
                      └─ spyro::FieldOwner::deliver          one presented field per step
            └─ psx::debug::DbgServer::service        the control channel's own per-step service
```

**While a movie or a blocking CD call holds the turn.** No title code runs inside a blocking guest
call: `spyro::GuestCall::resume` turns a guest VSync into a typed `FieldBoundary` outcome and returns
to `BootPrefixFrameDriver::stepFrame`, and `spyro::ArchiveTransfer::read` completes through
`spyro::context(core).archiveTransfer.takeCompletion()` polled by the `cd_retry_step` native leaf.
The frame turn is therefore always owned by the `FrameDriver` — nothing else ever holds it. A
blocking framework movie (`psx::movie::Fmv`) is likewise not a guest call: it is entered from the
framework's own delivery path, and host input during it is the framework's
`psx::input::HostInput`, which every pumping caller shares.

### The delivery of one display field

`spyro::FieldOwner::deliver` is the ONE definition of "a display field happened", shared by every
title in the lineage. In order: park the REPL → sample the host pad → advance the display clock →
dispatch this title's guest vblank root (`spyro::FieldOwner::dispatchCallbacks`) → advance the field
counter → the title observes (`spyro::FieldObserver::onField`) → snapshot → cross the presentation
fence when the field is visible → service audio (`psx::audio::SpuAudio::frame`) → release the
host-turn token. `spyro::FieldOwner::hostTurnThunk` is the host-turn entry: one more physical field,
never a second presentation fence.

`spyro1::FieldScheduler` is the Spyro 1 `FieldObserver`: the shared sequence plus the two things
that are this title's alone — the boot-sequence window in which a Start edge is observed, and the
per-field skip map.

### Host input → guest pad buffer

```text
psx::input::HostInput        the ONE host-input owner (external/psxport/runtime/psx/input/host_input.h):
                            the SDL event drain, the key state and the gamepads
  └─ psx::input::Pad::pollHostInput   the guest's pad, one per Core, active-low mask
       ├─ psx::input::Pad::setPlayerInputSuppressed   a picker panel must not see the player's pad
       └─ the guest pad buffer at the title's own layout
            └─ titles/spyro1/core/input/spyro1_input_phase.h  spyro1::InputPhase::of — which screen is
                                                        taking input this pad frame, the key a
                                                        recorded `.pad` replay is stored against
```

* **Movie skip / presentation skip.** `spyro::FieldOwner::presentationSkipPressed` asks whether the
  Start/Cross edge that ends a presentation-only hold is down. Only `spyro1::BootSequence` (boot
  logos) and `spyro1::TransitionSkip` decide whether such an edge *transitions*; the pad subsystem
  keeps exposing the input to later title states unchanged.
* **The window question.** `gpu_vk_windowed()` (external/psxport/runtime/psx/gpu/gpu_vk.h) is the
  renderer's answer to "is there a window", and it is passed to input explicitly rather than read
  back out of the renderer.
* **The debug control channel.** `psx::debug::DbgServer` is attached per session by
  `psx::host::TitleSession::boot` and serviced once per product step by `psx::host::TitleSession::step`,
  between `dc_step_frame` and the next frame. Its forced-input path is separate from the player's
  SDL path; `psx::host::PickerSession::serviceInput` is the one place the picker reads the pad and the
  channel together.

### Guest draw → presentation

```text
spyro::render::FrameRenderer::drawFrame
  ├─ classifyScene()                    which guest stage selector is on screen
  ├─ [reference leg]  referenceOtWalk() the guest's own render driver, unmodified
  ├─ [native leg]     prepareScene() → frame_env nativeFrameBegin
  │                   renderScene()  → one producer per field layer
  │                                     (the layer submit chain, spyro::render::FrameRenderer::titleMenuRender,
  │                                      ::stage13Mode3Render)
  └─ the ONE presentation fence        spyro::render::PresentationOwner (beginGuestFrame /
                                       beginNativeFrame) through the framework's RenderQueue
```

* **Real field.** `spyro::PresentationOwner::guestVramIsPicture` decides whether the presented image
  is guest VRAM or the native producers' queue; boot starts with guest VRAM because Spyro's
  upload-only logos precede the title frame driver.
* **60 fps in-between.** Spyro 2 is on the Record path: the framework replays the guest's GP0 record
  and, per present, composes it with each producer's render (`psx::present::composeFrame`). The
  terrain drawer override 0x80023BB4 is its one producer: it saves a `guest_terrain::FieldState`
  and `guest_terrain::TerrainStateProducer` redraws the terrain from it at any t
  (`guest_terrain::Rebuild`, the drawer run over host memory); `spyro2::depth_bins` gives every
  packet its ordering-table bin before the guest flattens the table; `spyro2::FrameCut` declares
  cuts. Spyro 3's temporal product (`spyro::TerrainWorldPass`, `spyro::makeTerrainWorldPass`)
  rebuilds a field through the same `Rebuild`; `spyro::terrain_packet_sink::submit` is where its
  reconstructed packets go.
* **Widescreen.** `spyro::GuestWidescreenOwner` owns the ONE widening decision for a
  guest-projection title: the resolved plan, the horizontal centre re-asserted per field, the
  widened draw rectangle at the frame tail, and the widened horizontal margin every title-owned cull
  asks. On Record (Spyro 2) the centre and draw rectangle stay retail's and the canvas holds the
  margins. `spyro::guest_widescreen_math` is its pure arithmetic. Native producers anchor through
  `spyro::ui_anchor` and `spyro::wide_screen_space` / `spyro::wide::drawnHorizontalInside`.
* **Field tail.** `spyro::FrameTailObserver::onFrameTail` runs BETWEEN the guest's last work and the
  field being presented — the only point at which a widened GP1 drawing rectangle survives.

Spyro 2 Record producers (census line `producer census at shutdown`):

| guest function | override | object | elements |
|---|---|---|---|
| 0x80023BB4 terrain drawer | `spyro::guest_terrain` passes, registered in `spyro2_render_facts.h`; render `TerrainStateProducer` | the drawer call (A0) | none: every packet of the call carries the dispatcher's object; the saved state is `FieldState` (camera angles and position, packet budget, captured sector visibility) |

The ordering-table bin of every packet, terrain or not, is assigned by the flatten override
0x8001B2A8 (`titles/spyro2/render/spyro2_depth_bins.*`) before the guest merges the bins.

The packet pools both parities allocate from lie between the words 0x80069998 and 0x8006B2A4
(`Spyro2Runtime::packetPoolWindows_`).

### CD / streaming

```text
guest CD call → native leaf  (game/core/cd_queue.cpp: cd_loader / cd_stream_read / cd_retry_step)
  └─ spyro::context(core).archiveTransfer   spyro::ArchiveTransfer::read → archive_transfer::decide
  └─ psx::code_module::digest               psxport's one SHA-256 digest-and-activate owner
```

Stock framework `CdRead` landings are published by `psx::code_module::publishStockReadLanding`
(psxport `runtime/psx/core/guest_code_module.*`), which each title runtime calls from `stockCdReadLanded`.

### Audio

```text
psx::audio::SpuAudio                      the framework SPU owner, per Core
  ├─ service once per delivered field     spyro::FieldOwner::deliver → spu_audio.frame()
  ├─ title leaves                         spyro1::native:: (spu_* overrides) bind the retail
  │                                        key-status, voice-attribute, pitch, register and
  │                                        transfer-mode routines to this image
  └─ panel muting                         psx::audio::SpuAudio::setOutputEnabled — only the selected
                                           picker panel is audible
```

### The debug / control channel

```text
psx::debug::DbgServer         per session, attached in spyro::TitleSession::boot
  ├─ DbgServer::service       once per product step, in spyro::TitleSession::step
  ├─ DbgServer::honourPause   before the step
  └─ psx::debug::repl         stepped from tools/drive.py, tools/live_play.py, tools/title_route.py
```

---

## Ownership

### `game/core/` — namespace `spyro`

| File | Namespace | Owner | Responsibility |
| --- | --- | --- | --- |
| `main.cpp` | — | `main` | Process entry: zero-argument selector, or one executable. Composes only. |
| `spyro_context.{h,cpp}` | `spyro` | `spyro::Context`, `spyro::context` | The per-Core context every subsystem's state is published through, and the only way an override reaches a title object from a `Core`. |
| `spyro_runtime.{h,cpp}` | `spyro` | `spyro::SpyroRuntime` | Engine-lineage runtime root: image identity, title, logo facts, the run-complete report. |
| `title_runtime_registry.{h,cpp}` | `spyro` | `runtimeFor`, `SpyroCatalog` | Which title runtime owns which serial, and Spyro's catalog for the framework's multi-title host (`psx::host::TitleCatalog`). |
| `title_logo.{h,cpp}` | `spyro` | `extractPanelLogo`, `titleLogoGateOpen` | Reads a title's own wordmark out of its live VRAM for its picker panel. |
| `title_logo_facts.h` | `spyro` | `TitleLogoFacts` | Where a disc's own wordmark lands in VRAM and how it is encoded. |
| `field_owner.{h,cpp}` | `spyro` | `FieldOwner`, `FieldRequest`, `FieldOwnerFacts`, `FieldObserver`, `FrameTailObserver`, `FieldCadence` | The one delivery sequence every title's display field goes through, and the per-Core accessor. |
| `vblank_irq.h` | `spyro` | `hasPendingEnabledVblank` | The one PSX register fact no title can own: is a VBlank edge pending and enabled. |
| `deliverNativeField` | `spyro` | `FieldOwner` | One field delivered by a title-owned native tail. |
| `boot_prefix_frame_driver.{h,cpp}` | `spyro` | `BootPrefixFrameDriver`, `BootPrefixFacts` | One product step of a title whose retail executable IS its boot: a finite guest call, one presented field per step, two separate stall bounds. |
| `guest_execution.{h,cpp}` | `spyro` | `GuestExecution`, `reportExecutionResult` | Root guest continuation: preserve the root return address and committed PC across budget yields. |
| `world_guest_execution.{h,cpp}` | `spyro` | `WorldGuestExecution` | Unchanged retail world work resumed with bounded host-service exits. |
| `native_execution.h` | `spyro` | `PreservedReturnAddress`, `GuestFrameScope`, `callGuestJumpedFrom` | The `$ra` a nested guest call runs with, and the frame-boundary guard around one. |
| `native_leaf/vec/gte/angle/rand/util.cpp` | `spyro` | `registerNative*` | The image-scoped verified native leaf overrides, one installer per decomp source file. |
| `spyro_game.h` | `spyro` | — | The guest-boundary surface: the installers above, the CD queue installer, and the terrain producer's guest entry points. |
| `cd_queue.cpp` | `spyro` | `registerCdQueue` | The title's cooperative CD loader leaves and the completion delivery. |
| `archive_transfer.{h,cpp}`, `archive_transfer_contract.h` | `spyro`, `spyro::archive_transfer` | `ArchiveTransfer`, `archive_transfer::decide` | Bounded atomic WAD transfer with per-Core completion; the refusal decision is one pure function. |
| `content_identity.{h,cpp}` | `spyro` | `sha256` | The runtime SHA-256 owner for WAD bytes. |
| `guest_widescreen_owner.{h,cpp}` | `spyro` | `GuestWidescreenOwner`, `GuestWidescreenFacts`, `ProjectionSite` | The one widening decision for a guest-projection title, and every place the guest restates it. |
| `guest_widescreen_math.h` | `spyro::guest_widescreen_math` | pure functions | The horizontal arithmetic of that widening, with no Core and no GTE. |
| `guest_projection_owner.h` | `spyro` | `GuestProjectionOwner` | The per-Core seam a title's own projection owner publishes into. |
| `wide_clip_plan.h` | `spyro::wide` | the one plane rule | How a title widens a horizontal cull or screen-rect limit. |
| `guest_magnitude.{h,cpp}` | `spyro::guest_magnitude` | `normalize`, `scaled`, `lzcr` | The one normalise/`D_80074B84` tail shared by libgte, the tracer producer and the per-face colour program. |
| `guest_globals.h`, `guest_gp.h` | `spyro::guest` | address constants | The guest globals this title's code names. |
| `memcard_operations.h`, `memcard_event_stack.h` | `spyro` | `MemcardOperationPlan`, `MemcardEventPushPlan` | The title's memory-card operations, as plans the guest logic executes. |
| `spu_hardware_init.h`, `spu_pio_upload.h`, `text_sprites.h` | `spyro` | facts | Title facts for SPU power-on and the screen text glyph vocabulary. |
| `actor_mesh_scratch.h` | `spyro` | `ActorMeshScratchLayout` | Where one actor mesh decode keeps its scratch. |
| `spyro_gate_debug.{h,cpp}` | `spyro::gate_debug` | `GateInfo`, `inspectGate`, `teleportToGate` | The title's debug option: inspect and enter a gate. |

### The title selector

The selector, panel sessions, title sessions and the run cap live in the framework as `psx::host`
(`external/psxport/docs/title-host.md`). Spyro supplies only its catalog (`SpyroCatalog`), its
runtimes' `panelLogo`/`reportRun` overrides, and `main.cpp` composing
`psx::host::ProductHost` with that catalog.

### `game/render/` — namespace `spyro` (and `spyro::render` for the picture owner)

`game/render/` is eight directories, one per subsystem. Every directory is a PUBLIC include
directory, so a module is included by its unique basename (`"actor_producer.h"`), never by a path.
A new module goes in the directory of the subsystem it belongs to; nothing lands in `game/render/` itself.

| Directory | Owns |
| --- | --- |
| `frame/` | The picture itself: classification, the native leg's frame open/close, and the shared GPU/coprocessor vocabulary every producer submits through. |
| `field/` | The ten field layers, their recipes and their submit chain, plus the guest moby walk and the drawn-half tables. |
| `actor/` | The regular, secondary and paired actor layers, from model decode to queue emit. |
| `terrain/` | The native terrain producer: the guest drawer reimplemented as passes, plus its derive/preflight/publish chain. |
| `world/` | The world producer and the cyclorama/menu background: the sector corpus, its codecs, the LQ/HQ recipes and the sky geometry. |
| `temporal/` | The 60 fps reconstruction lifecycle shared by every layer, and its two presentation-only censuses. |
| `hud/` | The screen-space 2D layer, the guest's HUD layout and text, and the ONE horizontal anchoring rule. |
| `scene/` | The front-end and cutscene scenes, each with its pure recipe and its native scene owner. |

#### `game/render/frame/` — the picture

| Module | Namespace | Responsibility |
| --- | --- | --- |
| `frame_renderer.{h,cpp}`, `frame_renderer_frame.cpp` | `spyro::render` | `FrameRenderer`: one frame's picture — classify the guest's stage selector, then either walk the guest's own OT or compose the native producers. A stage with no native recipe, or a producer that refused its input, is reported in full and handed to `guest_scene`, never ended as a process abort. |
| `guest_scene.{h,cpp}` | `spyro::render` | The GUEST renderer as the fallback owner for an unported scene: runs the guest's own render arm, unmodified, through the runtime executor and reports the typed exit it stopped at. The arm's DISPLAY TAIL is not its scene — `frameBegin`/`frameEnd` own that — so the seam is the arm's first field wait. |
| `scene.{h,cpp}` | `spyro::render` | `Scene`, `SceneOwner`, `StageArm`, `FieldLayer` and the stage selectors: the field arm's layer list is the native-renderer backlog, declared once here. |
| `presentation_owner.{h,cpp}` | `spyro` | `PresentationOwner`: per-Game statement of which producer owns the next present. |
| `frame_env.{h,cpp}` | `spyro` | The native leg's frame open/close and display environment. |
| `guest_actor_pool.h` | `spyro::guest_actor_pool` | SCUS_942.28's actor pool: the cursor, the end, and one record's size. Four producers read or move it, so it is stated once here rather than as a literal in each. |
| `draw_area.h`, `producer_refusal.h`, `scene_painter_order.*`, `painter_submission_preflight.*` | `spyro` | The draw-destination check, the refusal vocabulary, and the painter order and preflight shared by producers. |
| `gpu_packet_decode.{h,cpp}`, `guest_trig.{h,cpp}`, `gte_color_ops.h`, `projection_stream.{h,cpp}`, `scene_camera_inputs.h` | `spyro::gpu_packet_decode`, `spyro::guest_trig`, `spyro::gte_color`, `spyro` | The packet and coprocessor vocabulary and the projection sampler the producers share. |

#### `game/render/field/` — the field layers

| Module | Namespace | Responsibility |
| --- | --- | --- |
| `field_model_chain.{h,cpp}`, `field_moby_lists.{h,cpp}` | `spyro` | The layer-by-layer submission chain and the moby list build the field arm owns. |
| `field_actor_composition`, `field_player_actor` | `spyro` | The field arm's actor composition and Spyro's own field model. |
| `field_cyclorama`, `cyclorama_*` (see `world/`) | `spyro::field_cyclorama` | The field arm's sky producer entry point. |
| `field_environment{,_recipe,_scene}`, `field_collectables{,_recipe}`, `field_shadow{,_recipe,_submitter}`, `field_tracers{,_recipe}`, `field_particles{,_recipe}`, `field_particle_endpoint`, `field_particle_*_submitter`, `particle_sine_table.h`, `face_light_{environment,program}` | `spyro::field_*`, `spyro::face_light_*` | The remaining field layers and their derivation. |
| `sparkle_*`, `glow_*`, `moby_shadow{,_list,_recipe,_submitter}`, `spyro_flame{,_matrix,_recipe,_submitter}` | `spyro::sparkle_*`, `spyro::glow_*`, `spyro::moby_shadow_*`, `spyro::spyro_flame_*` | The half-drawn effect layers and their recipes. |
| `field_shaded_queue_{scene,recipe,emit,submitter}`, `shaded_moby_light` | `spyro::field_shaded_queue_*`, `spyro::shaded_light` | The world-shaded sprite queue and its GTE lighting program. |
| `sprite_queue.{h,cpp}` | `spyro::render` | `SpriteQueueOffsetObserver` and `emitScreenQueue`: the screen-space sprite queue every screen-space producer submits through, and the mode-3 handler that rebuilds it. |
| `guest_moby_visibility.cpp`, `guest_moby_{frustum,gte,rotation}.h`, `guest_render_globals.h`, `sector_visibility.{h,cpp}`, `moby_shadow_list.{h,cpp}` | `spyro::guest_moby*`, `spyro::guest_render_globals`, `spyro::sector_visibility`, `spyro::moby_shadow_list` | Native per-frame moby culling with its pure arithmetic, and the two drawn-half tables. |

#### `game/render/actor/` — the actor layers

| Module | Namespace | Responsibility |
| --- | --- | --- |
| `actor_producer`, `actor_recipe_capture`, `actor_model_codec`, `actor_prefix_builder`, `actor_transform_math`, `actor_scene_builder` | `spyro::actor_*` | The regular actor layer's guest entry point, model decode, prefix building, transform math and scene build. |
| `actor_emit`, `actor_submission`, `actor_stage`, `actor_face_submitter`, `actor_billboard_face`, `actor_draw_recipe`, `actor_global_order`, `actor_ot_coalescer` | `spyro::actor_*` | The layer's route to the render queue: emit, preflight, publish. |
| `secondary_actor_{scene,recipe,emit}` | `spyro::secondary_actor_*` | The secondary layer's compose/preflight/publish owner. |
| `paired_actor.{h,cpp}`, `paired_actor_pose`, `paired_actor_decode`, `paired_actor_depth`, `paired_actor_color_fade`, `paired_actor_temporal_evidence`, `paired_actor_projection` | `spyro::paired_actor`, `spyro::paired_actor_projection`, `spyro::paired_actor_depth`, `spyro::paired_actor_color_fade` | The shared vocabulary of Spyro's own paired actor: the delta codec and `/16` blend, the projection three callers share, the depth rule, the colour-fade table transform, and the temporal evidence. `paired_actor.cpp` itself is the four-line composition — two pose forwarders and the two producer call sites. |
| `paired_actor_producer` | `spyro::paired_actor_producer` | One live invocation of 0x80023AC4: decode the pose, read the guest's stream and material tables, apply the colour fade, capture the frame. Every step that cannot be satisfied refuses by name. |
| `paired_actor_temporal` | `spyro::paired_actor` | The 60 fps half: the two captured endpoints, whether a pair may be rebuilt, the rebuild, and the per-frame lifecycle deciding which endpoint is which. `emitCapturedEndpoint` is the ONE emit both halves use. |
| `paired_actor_selftest.cpp` | `spyro::paired_actor` | The hermetic checks over the pose codec, the projection, and the endpoint rules — the shipping rules, not a restatement of them. |

#### `game/render/terrain/` — the native terrain producer

| Module | Namespace | Responsibility |
| --- | --- | --- |
| `native_terrain.cpp` | `spyro` | The producer's guest entry points (a selector and two `SHORTMATRIX` pointers). |
| `terrain_{recipe,scene,submitter,emit}`, `terrain_packet_sink` | `spyro::terrain_*`, `spyro::terrain_packet_sink` | Corpus read, pure derivation, queue plan, one derive/preflight/publish owner, and the packet destination. |
| `guest_terrain_*` (classify, coarse, detail, far, fine, fog, translucent, split, resplit, polygon, mesh, screen, passes, drawer, frame, memory, facts) | `spyro::guest_terrain` | The native terrain drawer of this engine family: one file per pass plus the frame state, memory and mesh owners. |
| `terrain_rebuild.{h,cpp}` | `spyro::guest_terrain` | The drawer run over host memory at a camera between two of the guest's (`Rebuild`), the camera lerp (`cameraBetween`), visibility union and the packet arena window; both in-between owners use it. |
| `terrain_state_producer.{h,cpp}` | `spyro::guest_terrain` | Spyro 2's terrain `StateProducer`: `FieldState` capture after the real draw and the render that emits the rebuilt packets into their bins. |
| `guest_camera_builder.{h,cpp}` | `spyro::guest_camera` | The guest's own camera builder, re-run for an in-between rather than interpolated from its packed output. |

#### `game/render/world/` — the world producer and the background

| Module | Namespace | Responsibility |
| --- | --- | --- |
| `world_producer` | `spyro` | The producer's guest entry point. |
| `world_source*`, `world_chunk_codec`, `world_material_codec`, `world_animation` | `spyro::world_*` | The owned unprojected sector occurrences and the codecs that decode them. |
| `world_scene_{builder,prepare,submitter}`, `world_recipe` | `spyro::world_scene*`, `spyro::world_recipe` | Scene build, prepare, submission and pure face derivation. |
| `world_{lq,hq}_recipe`, `world_hq_refinement`, `world_projection_math` | `spyro::world_*_recipe`, `spyro::world_projection_math` | LQ/HQ/refinement recipes and the projection arithmetic they share. |
| `cyclorama_{scene,portal_mesh,mask}_{recipe,submitter}`, `menu_world_pass`, `menu_lighting.*`, `menu_panel_submit` | `spyro::cyclorama_*`, `spyro::menu_*` | Sky geometry, portals and masks, plus the menu's own background and panel. |

#### `game/render/temporal/` — the 60 fps lifecycle

| Module | Namespace | Responsibility |
| --- | --- | --- |
| `temporal_pair.h`, `instance_pairing.h`, `temporal_scene.{h,cpp}` | `spyro::temporal`, `spyro::instance_pairing`, `spyro` | The two-endpoint lifecycle every temporal source shares, the instance pairing that feeds it, and the scene admission scratch. |
| `actor_temporal`, `secondary_actor_temporal`, `field_shaded_queue_temporal`, `terrain_temporal`, `world_temporal` | `spyro::<layer>_temporal` | One endpoint pair per layer, each with its own identity rule over the payload that layer draws. |
| `terrain_world_pass.{h,cpp}` | `spyro` | Spyro 3's in-between field: captures the field's camera and visibility, runs `guest_terrain::Rebuild` at a lerped camera and replays its packets through the GPU funnel. |
| `actor_pairing.{h,cpp}` | `spyro::actor_pairing` | Pairs actor instances across the two endpoints. |

#### `game/render/hud/` — the screen-space 2D layer

| Module | Namespace | Responsibility |
| --- | --- | --- |
| `ui_anchor.{h,cpp}` | `spyro::ui_anchor` | The ONE horizontal anchoring rule for screen-space UI: `Anchor::{LeftEdge, Centred, RightEdge}`, and the frame every title's HUD is related against. |
| `field_2d_overlay{,_recipe}.{h,cpp}` | `spyro::field_2d_overlay*` | The screen-space 2D layer and its pure recipe. |
| `hud_text_builder`, `hud_layout.h`, `hud_draw_context.h` | `spyro::hud_text`, `spyro::hud_layout`, `spyro::hud_draw_context` | The guest's two text builders, the HUD block's layout, and the widget-in-progress seam. |
| `screen_{fade,border}{,_recipe}.{h,cpp}` | `spyro::screen_fade*`, `spyro::screen_border*` | The full-screen fade and border producers. |
| `wide_screen_space.{h,cpp}` | `spyro::wide_screen_space` | The horizontal policy for world geometry. |

#### `game/render/scene/` — the front-end and cutscene scenes

| Module | Namespace | Responsibility |
| --- | --- | --- |
| `field_scene_recipe`, `stage13_scene_recipe`, `stage13_text_run`, `cutscene_scene_recipe` | `spyro::<scene>_recipe`, `spyro::stage13_text_run` | The pure recipe behind each scene's native owner, and stage 13's caption run — which string, how far along it is, and how a string becomes a row of mobys in the actor pool. |
| `demo_text_scene`, `title_menu`, `title_menu_{recipe,state}`, `pause_menu_{recipe,scene}`, `fairy_menu_{recipe,scene}`, `level_transition_{scene,tally_recipe}` | `spyro::<scene>` and owners | Each front-end scene's native owner and its own state. |
| `dragon_{burst,scene}_producer`, `dragon_{burst,scene}_recipe` | `spyro::dragon_*` | The dragon reward's burst star and scene producers. |


### `titles/spyro1/core/` — namespace `spyro1` (leaves in `spyro1::native`)

Ten concept directories, each a PUBLIC include directory, mirroring `game/`'s subsystems. A module is
included by its unique basename (`"native_camera.h"`), never by a path.

| Directory | Owns |
| --- | --- |
| `frame/` | The frame lifecycle: the driver, the frame policy and logo facts, the draw environment, the boot sequence, the transition skip and press latch, the stage observer. |
| `runtime/` | The image policy and the composition of its native leaves, and the field vocabulary the boot window, skip map and audio service share. |
| `input/` | Which screen takes input this pad frame, and the pad itself. |
| `moby/` | The moby layer: allocator, lists, transform, helpers, collision and shade, the shaded queue, shared models, environment light, level globals. |
| `camera/` | The guest camera owner. |
| `player/` | Player physics, player animation, and cutscene playback. |
| `hud/` | HUD collectables, the pause menu, the glow and sparkle pools, the pixel fade. |
| `effect/` | Effect state and particle allocation. |
| `audio/` | Sound position, audio key state, and the eight SPU leaves. |
| `execution/` | The random range, the level initialization, and the handoff store observer. |

| Module | Namespace | Responsibility |
| --- | --- | --- |
| `runtime/spyro1_runtime.{h,cpp}` | `spyro1` | `Spyro1Runtime`: `SCUS_942.28` image policy and the composition of its native leaf overrides. |
| `frame/spyro1_frame_driver.{h,cpp}` | `spyro1` | `Spyro1FrameDriver`: one product step — boot prefix, the retail update, the picture, the frame tail. |
| `runtime/spyro1_field_scheduler.{h,cpp}` | `spyro1` | `FieldScheduler`: Spyro 1's field vocabulary over the shared owner. |
| `frame/spyro1_boot_sequence.{h,cpp}` | `spyro1` | `BootSequence`: the native finite boot, and the ownership of the first presentation-only holds. |
| `frame/spyro1_transition_skip.{h,cpp}` | `spyro1` | `TransitionSkip`: cancelling a presentation-only transition on Start/Cross by taking its guest owner's own terminal transition. |
| `frame/spyro1_press_latch.h` | `spyro1` | `PressLatch`: a boot fade/loader press held to the next hold. |
| `input/spyro1_input_phase.{h,cpp}` | `spyro1` | `InputPhase`: which screen is taking input this pad frame — the key a recorded `.pad` frame is stored against. |
| `frame/spyro1_frame_policy.h`, `frame/spyro1_logo_facts.h` | `spyro1` | facts | Frame bounds and the disc's own logo. |
| `frame/stage_update_observer.{h,cpp}` | `spyro1` | `StageUpdateObserver`: opt-in read-only camera/player samples after the outer guest update returns. |
| `execution/handoff_store_observer.{h,cpp}` | `spyro1` | `HandoffStoreObserver`: opt-in pre/post RAM and tick snapshots at the New Game handoff PCs. |
| `<dir>/native_*.{h,cpp}` | `spyro1::native` | one module per decomp source file | The image-scoped native leaves, each named for the guest function it replaces. |

### `titles/spyro2/`, `titles/spyro3/`

| File | Namespace | Owner | Responsibility |
| --- | --- | --- | --- |
| `spyro{2,3}_runtime.{h,cpp}` | `spyro2`, `spyro3` | `Spyro2Runtime`, `Spyro3Runtime` | `SCUS_944.25` / `SCUS_944.67` identity, HLE plan, CD callback layout, boot-prefix frame driver, render path (Spyro 2 Record with its packet pools and cut; Spyro 3 temporal product), logo facts, widescreen answer. |
| `spyro2_frame_cut.{h,cpp}` | `spyro2` | `FrameCut` | Whether a sealed record is a cut: a new game state (0x800681C8) or level (0x80066F90), or a camera placement inside a scene (the camera controller's mode machine 0x80067ED0/0x80067ED4 leaving its pre-placement sub-state in mode 9, 0xB or 10), sampled when the draw that walked the record's table returns (`FrameTailObserver::onFrameDrawn`). |
| `spyro{2,3}_boot_facts.h` | `spyro2`, `spyro3` | `kBootPrefixFacts` | The measured boot prefix, update/draw pair and field-owner facts, with the instruction bytes that give each address. |
| `spyro{2,3}_logo_facts.h`, `spyro{2,3}_widescreen_facts.h` | `spyro2`, `spyro3` | facts | Where the disc's wordmark lands, and the image's measured projection sites and authored window. |
| `spyro{2,3}_render_facts.h` | `spyro2`, `spyro3` | `spyro::guest_moby::Facts`, `spyro::guest_terrain::Facts`, `guest_render_globals::Globals` | One value set per image for the shared guest moby walk and terrain drawer. |
| `spyro{2,3}_hud_anchor.{h,cpp}` | `spyro2`, `spyro3` | `hud_anchor` | The per-image HUD widget drawers, applying `spyro::ui_anchor::correction` per element class. |
| `executable.json` | — | manifest | Serial, hashes, labels and environment keys. |

---

## Where does new work go?

- Decoder/lowering, Lightrec integration, cache, invalidation, or bounded executor exits →
  `external/psxport/`.
- A title serial, hash, load range, or runtime image fact → `titles/<title>/` and its
  manifest-backed runtime policy.
- A Spyro 1 frame, field, input, audio, or lifecycle transition → the matching
  `titles/spyro1/core/` concept directory.
- A semantic draw responsibility → one cohesive `game/render/` recipe/builder/submitter owner; the
  scene composer only orders owners.
- A runtime WAD identity or load/unload observation → the shared executor image tracker; title-specific
  archive semantics stay in `game/core/archive_transfer.*`.
- A diagnostic → an oracle/capture module that cannot mutate or submit the shipping picture.
- A capability change → `docs/project-state.md`; an atomic task/finding → `docs/issues/`; a binary
  dependency step → `docs/re-frontier.md`.
