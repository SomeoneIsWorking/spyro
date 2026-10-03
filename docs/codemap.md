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
| `game/host/` | `spyro` | The process top level: catalog probe, title selector, panel sessions, one title session |
| `game/render/` | `spyro`, `spyro::render` and one sub-namespace per producer/layer | The picture: scene classification, producers, temporal (60 fps) sources, widescreen anchoring, HUD |
| `titles/spyro1/core/` | `spyro1`, `spyro1::native` | SCUS_942.28's frame driver, field scheduler, boot sequence, transition skip, observers, and its native leaf overrides |
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
psx::input::HostInput        the ONE host-input owner (external/psxport/runtime/psx/host_input.h):
                            the SDL event drain, the key state and the gamepads
  └─ psx::input::Pad::pollHostInput   the guest's pad, one per Core, active-low mask
       ├─ psx::input::Pad::setPlayerInputSuppressed   a picker panel must not see the player's pad
       └─ the guest pad buffer at the title's own layout
            └─ titles/spyro1/core/spyro1_input_phase.h  spyro1::InputPhase::of — which screen is
                                                        taking input this pad frame, the key a
                                                        recorded `.pad` replay is stored against
```

* **Movie skip / presentation skip.** `spyro::FieldOwner::presentationSkipPressed` asks whether the
  Start/Cross edge that ends a presentation-only hold is down. Only `spyro1::BootSequence` (boot
  logos) and `spyro1::TransitionSkip` decide whether such an edge *transitions*; the pad subsystem
  keeps exposing the input to later title states unchanged.
* **The window question.** `gpu_vk_windowed()` (external/psxport/runtime/psx/gpu_vk.h) is the
  renderer's answer to "is there a window", and it is passed to input explicitly rather than read
  back out of the renderer.
* **The debug control channel.** `psx::debug::DbgServer` is attached per session by
  `spyro::TitleSession::boot` and serviced once per product step by `spyro::TitleSession::step`,
  between `dc_step_frame` and the next frame. Its forced-input path is separate from the player's
  SDL path; `spyro::PickerSession::serviceInput` is the one place the picker reads the pad and the
  channel together.

### Guest draw → presentation

```text
spyro::render::FrameRenderer::drawFrame
  ├─ classifyScene()                    which guest stage selector is on screen
  ├─ [reference leg]  referenceOtWalk() the guest's own render driver, unmodified
  ├─ [native leg]     prepareScene() → frame_env nativeFrameBegin
  │                   renderScene()  → one producer per field layer
  │                                     (fx_*_submit, spyro::render::FrameRenderer::titleMenuRender,
  │                                      ::stage13Mode3Render)
  └─ the ONE presentation fence        spyro::render::PresentationOwner (beginGuestFrame /
                                       beginNativeFrame) through the framework's RenderQueue
```

* **Real field.** `spyro::PresentationOwner::guestVramIsPicture` decides whether the presented image
  is guest VRAM or the native producers' queue; boot starts with guest VRAM because Spyro's
  upload-only logos precede the title frame driver.
* **60 fps in-between.** The title's temporal product (`spyro2::Spyro2Runtime::createTemporalFramePresentation`
  → `spyro::TerrainWorldPass`, `spyro::makeTerrainWorldPass`) rebuilds a field from the guest's own
  object memory at a lerped camera; `spyro::terrain_packet_sink::submit` is where its reconstructed
  packets go, and `spyro::interp_census::Census` records what it rebuilt.
* **Widescreen.** `spyro::GuestWidescreenOwner` owns the ONE widening decision for a
  guest-projection title: the resolved plan, the horizontal centre re-asserted per field, the
  widened draw rectangle at the frame tail, and the widened horizontal margin every title-owned cull
  asks. `spyro::guest_widescreen_math` is its pure arithmetic. Native producers anchor through
  `spyro::ui_anchor` and `spyro::wide_screen_space` / `spyro::wide::drawnHorizontalInside`.
* **Field tail.** `spyro::FrameTailObserver::onFrameTail` runs BETWEEN the guest's last work and the
  field being presented — the only point at which a widened GP1 drawing rectangle survives.

### CD / streaming

```text
guest CD call → native leaf  (game/core/cd_queue.cpp: cd_loader / cd_stream_read / cd_retry_step)
  └─ spyro::context(core).archiveTransfer   spyro::ArchiveTransfer::read → archive_transfer::decide
       └─ spyro::context(core).loadLedger   spyro::load_ledger::Ledger (records, never writes)
  └─ spyro::image_publication::digest       the one SHA-256 digest-and-activate owner
```

Stock framework `CdRead` landings are published by `spyro::publishStockReadLanding`
(`game/core/stock_read_publication.*`), which each title runtime declares.

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
| `spyro_runtime.{h,cpp}` | `spyro` | `spyro::SpyroRuntime` | Engine-lineage runtime root: image identity, title, logo facts, attract state. |
| `title_runtime_registry.{h,cpp}` | `spyro` | `runtimeFor` | Which title runtime owns which serial. |
| `title_selection.{h,cpp}` | `spyro` | `spyro::SelectionResult` | Choosing a title from a `PSXPORT_TITLES` slug list. |
| `title_logo_facts.h` | `spyro` | `TitleLogoFacts` | Where a disc's own wordmark lands in VRAM and how it is encoded. |
| `field_owner.{h,cpp}` | `spyro` | `FieldOwner`, `FieldRequest`, `FieldOwnerFacts`, `FieldObserver`, `FrameTailObserver`, `FieldCadence` | The one delivery sequence every title's display field goes through, and the per-Core accessor. |
| `vblank_irq.h` | `spyro` | `hasPendingEnabledVblank` | The one PSX register fact no title can own: is a VBlank edge pending and enabled. |
| `deliverNativeField` | `spyro` | `FieldOwner` | One field delivered by a title-owned native tail. |
| `runtime_run.{h,cpp}` | `spyro` | `RuntimeRun` | The delivered-field cap and the graceful end of a run. |
| `boot_prefix_frame_driver.{h,cpp}` | `spyro` | `BootPrefixFrameDriver`, `BootPrefixFacts` | One product step of a title whose retail executable IS its boot: a finite guest call, one presented field per step, two separate stall bounds. |
| `spyro_guest_call.{h,cpp}` | `spyro` | `GuestCall` | One finite guest call resumed across product steps: captured return address, frame-boundary exit, RAM-safe `$r[31]` refusal. |
| `guest_execution.{h,cpp}` | `spyro` | `GuestExecution`, `reportExecutionResult` | Root guest continuation: preserve the root return address and committed PC across budget yields. |
| `world_guest_execution.{h,cpp}` | `spyro` | `WorldGuestExecution` | Unchanged retail world work resumed with bounded host-service exits. |
| `native_execution.h` | `spyro` | `PreservedReturnAddress`, `GuestFrameScope`, `callGuestJumpedFrom` | The `$ra` a nested guest call runs with, and the frame-boundary guard around one. |
| `native_leaf/vec/gte/angle/rand/util.cpp` | `spyro` | `registerNative*` | The image-scoped verified native leaf overrides, one installer per decomp source file. |
| `spyro_game.h` | `spyro` | — | The guest-boundary surface: the installers above, the CD queue installer, and the terrain producer's guest entry points. |
| `cd_queue.cpp` | `spyro` | `registerCdQueue` | The title's cooperative CD loader leaves and the completion delivery. |
| `archive_transfer.{h,cpp}`, `archive_transfer_contract.h` | `spyro`, `spyro::archive_transfer` | `ArchiveTransfer`, `archive_transfer::decide` | Bounded atomic WAD transfer with per-Core completion; the refusal decision is one pure function. |
| `image_publication.{h,cpp}` | `spyro::image_publication` | `digest` | The one SHA-256 digest-and-activate owner for guest images. |
| `stock_read_publication.{h,cpp}` | `spyro` | `publishStockReadLanding` | Every framework stock `CdRead` landing, published through the image owner. |
| `load_ledger.{h,cpp}` | `spyro::load_ledger` | `Ledger`, `IssuerSite`, `Operation` | One diagnostic record per CD read: site, range, destination, payload identity, stage, cost. Reads and logs; never writes guest state. |
| `content_identity.{h,cpp}` | `spyro` | `sha256`, `ExecutableIdentity` source | The runtime SHA-256 owner for executable and WAD bytes. |
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

### `game/host/` — namespace `spyro`

| File | Namespace | Owner | Responsibility |
| --- | --- | --- | --- |
| `product_host.{h,cpp}` | `spyro` | `ProductHost` | The process top level, and the owner of the one window and one presentation device. |
| `title_session.{h,cpp}` | `spyro` | `TitleSession` | One boot-to-exit run of one title as a STEPPABLE owner; destruction is the whole teardown. |
| `title_availability.{h,cpp}` | `spyro` | `TitleAvailabilityProbe`, `TitleAvailability` | Which catalogued titles are provisioned and authenticated right now. |
| `picker_session.{h,cpp}` | `spyro` | `PickerSession` | The selector's own frame: one guest advancing, one panel confirmed into a live session. |
| `panel_sessions.{h,cpp}` | `spyro` | `PanelSessions` | The picker's live sessions and the rules about which of them runs. |
| `picker_layout.{h,cpp}` | `spyro` | `PickerLayout`, `PanelLayout`, `PanelRect`, `SourceCrop` | Panel geometry and the COVER crop — pure, and the only part a hermetic test can decide. |
| `picker_composite.{h,cpp}` | `spyro` | `PickerComposite` | The selector's window frame: panels as quads, dividers, caption bands, screen text. |
| `picker_content.{h,cpp}` | `spyro` | `PickerContent` | The labels and artwork the panels advertise. |
| `picker_runtime.{h,cpp}` | `spyro` | `PickerRuntime` | The host `Game` and composite the selector frame runs on. |
| `panel_logo.{h,cpp}` | `spyro` | `PanelLogo` | A title's own logo, drawn into its panel. |

### `game/render/` — namespace `spyro` (and `spyro::render` for the picture owner)

| Group | Namespace | Responsibility |
| --- | --- | --- |
| `render.h`, `render_frame.cpp`, `scene.cpp` | `spyro::render` | `FrameRenderer`: one frame's picture — classify the guest's stage selector, then either walk the guest's own OT or compose the native producers. `Scene`, `StageArm`, `FieldLayer` and the stage selectors are its vocabulary. |
| `presentation_owner.{h,cpp}` | `spyro` | `PresentationOwner`: per-Game statement of which producer owns the next present. |
| `frame_env.{h,cpp}` | `spyro` | The native leg's frame open/close and display environment. |
| `fx_*.{h,cpp}` | owner namespaces | The guest-facing producer entry points: one file per field layer or front-end scene (`fx_field_cyclorama`, `fx_field_environment`, `fx_field_particles`, `fx_field_collectables`, `fx_field_shadow`, `fx_field_tracers`, `fx_moby_shadow`, `fx_world_draw`, `fx_actor_draw`, `fx_field_player_actor`, `fx_paired_actor`, `fx_sprite_queue`, `fx_title_menu`, `fx_screen_fade`, `fx_screen_border`, `fx_spyro_flame`, `fx_glow_sparkle`, `fx_dragon_burst`, `fx_dragon_scene`, `fx_field_actor_composition`). |
| `field_model_chain.{h,cpp}`, `field_moby_lists.{h,cpp}` | `spyro` | The layer-by-layer submission chain and the moby list build the field arm owns. |
| `native_terrain.cpp` | `spyro` | The terrain producer's guest entry points (a selector and two `SHORTMATRIX` pointers). |
| `terrain_scene/recipe/submitter/emit`, `terrain_packet_sink` | `spyro::terrain_*` | The terrain producer's corpus read, pure derivation, queue plan, one derive/preflight/publish owner, and the in-between's packet destination. |
| `guest_terrain_*.{h,cpp}` | `spyro::guest_terrain` | The native terrain drawer of this engine family: one file per pass plus the frame state, memory, mesh, polygon, split, fog and screen owners. |
| `terrain_world_pass.{h,cpp}` | `spyro` | The in-between field: the guest's own terrain drawer run again at a lerped camera, over host memory shaped like guest RAM. |
| `guest_camera_builder.{h,cpp}` | `spyro::guest_camera` | The guest's own camera builder, re-run for an in-between rather than interpolated from its packed output. |
| `guest_moby_visibility.cpp`, `guest_moby_{frustum,gte,rotation}.h` | `spyro::guest_moby`, `…_frustum`, `…_gte`, `…_rotation` | Native per-frame moby culling, with the pure frustum arithmetic, GTE vocabulary and rotation composition split out. |
| `guest_render_globals.h` | `spyro::guest_render_globals` | The guest globals the moby walk and the terrain drawer share. |
| `world_source*`, `world_chunk_codec`, `world_material_codec`, `world_animation` | `spyro::world_*` | The world producer's owned unprojected sector occurrences and the codecs that decode them. |
| `world_scene_{capture,builder,prepare,submitter}`, `world_recipe` | `spyro::world_scene`, `…_prepare`, `…_submitter`, `spyro::world_recipe` | The world producer's guest read, scene build, prepare, submission, and pure face derivation. |
| `world_{lq,hq}_recipe`, `world_hq_refinement`, `world_projection_math` | `spyro::world_*_recipe`, `spyro::world_projection_math` | LQ/HQ/refinement recipes and the projection arithmetic they share. |
| `actor_recipe_capture`, `actor_model_codec`, `actor_prefix_builder`, `actor_transform_math`, `actor_scene_builder` | `spyro::actor_*` | Actor model decode, prefix building, transform math and scene build. |
| `actor_emit`, `actor_submission`, `actor_stage`, `actor_face_submitter`, `actor_billboard_face`, `actor_draw_recipe`, `actor_global_order`, `actor_ot_coalescer` | `spyro::actor_*` | The regular actor layer's route to the render queue: emit, preflight, publish, and the shared vocabulary. |
| `secondary_actor_{scene,recipe,emit}` | `spyro::secondary_actor_*` | The secondary layer's compose/preflight/publish owner. |
| `paired_actor_{pose,decode,depth,color_fade}`, `paired_actor_temporal_evidence` | `spyro::paired_actor`, `spyro::paired_actor_depth`, `spyro::paired_actor_color_fade` | Spyro's own paired actor: pose decode, depth, colour fade, and the temporal evidence. |
| `field_shaded_queue_{scene,recipe,emit,submitter}`, `shaded_moby_light` | `spyro::field_shaded_queue_*`, `spyro::shaded_light` | The world-shaded sprite queue: scene input, recipe, derive/preflight/publish, submission, and the GTE lighting program. |
| `cyclorama_*`, `menu_lighting`, `menu_panel_submit`, `menu_world_pass` | `spyro::cyclorama_*`, `spyro::menu_*` | Sky geometry, portals and masks, plus the menu's own background and panel. |
| `field_environment_*`, `field_collectables_recipe`, `field_shadow_*`, `field_tracers_recipe`, `field_particles_*`, `sparkle_*`, `glow_*`, `moby_shadow_*` | `spyro::field_*`, `spyro::sparkle_*`, `spyro::glow_*`, `spyro::moby_shadow_*` | The remaining field layers: environment, collectables, shadows, tracers, particles, sparkles, glows, moby shadows. |
| `field_scene_recipe`, `cutscene_scene_recipe`, `stage13_scene_recipe`, `title_menu_{recipe,state}`, `pause_menu_{recipe,scene}`, `fairy_menu_{recipe,scene}`, `level_transition_*`, `dragon_*`, `demo_text_scene` | `spyro::<scene>_recipe` and owners | The front-end and cutscene scenes, each with its pure recipe and its native scene owner. |
| `field_2d_overlay*`, `hud_text_builder`, `hud_layout`, `hud_draw_context` | `spyro::field_2d_overlay*`, `spyro::hud_text`, `spyro::hud_layout`, `spyro::hud_draw_context` | The screen-space 2D layer, the guest's two text builders, the HUD block's layout, and the widget-in-progress seam. |
| `ui_anchor.{h,cpp}`, `wide_screen_space.{h,cpp}`, `sector_visibility.{h,cpp}`, `moby_shadow_list.{h,cpp}` | `spyro::ui_anchor`, `spyro::wide_screen_space`, `spyro::sector_visibility`, `spyro::moby_shadow_list` | The ONE horizontal anchoring rule for screen-space UI, the horizontal policy for world geometry, and the two drawn-half tables. |
| `temporal_pair.h`, `instance_pairing.h`, `actor_pairing.{h,cpp}`, `temporal_scene.{h,cpp}` | `spyro::temporal`, `spyro::instance_pairing`, `spyro::actor_pairing`, `spyro` | The two-endpoint lifecycle every temporal source shares, the instance pairing that feeds it, and the scene admission scratch. |
| `actor_temporal`, `secondary_actor_temporal`, `field_shaded_queue_temporal`, `terrain_temporal`, `world_temporal`, `field_2d_overlay.h` (`History`) | `spyro::<layer>_temporal` | One endpoint pair per layer, each with its own identity rule over the payload that layer draws. |
| `interp_census.{h,cpp}`, `margin_object_census.{h,cpp}`, `producer_refusal.h`, `draw_area.h`, `scene_painter_order`, `painter_submission_preflight` | `spyro::interp_census`, `spyro::margin_object_census`, `spyro` | Presentation-only accounting, the per-class drawn-reach census, the refusal vocabulary, the draw-destination check, and the painter order shared by producers. |
| `gpu_packet_decode.{h,cpp}`, `guest_gte.{h,cpp}`, `guest_trig.{h,cpp}`, `gte_color_ops.h`, `particle_sine_table.h` | `spyro::gpu_packet_decode`, `spyro::guest_gte`, `spyro::guest_trig`, `spyro::gte_color` | The packet and coprocessor vocabulary the producers share. |
| `projection_stream.{h,cpp}`, `scene_camera_inputs.h`, `actor_scene_oracle.{h,cpp}`, `world_scene_oracle.{h,cpp}`, `world_scene_capture.{h,cpp}` | `spyro` | The projection sampler, the camera inputs, and the independent record oracles. |

### `titles/spyro1/core/` — namespace `spyro1` (leaves in `spyro1::native`)

| File | Namespace | Owner | Responsibility |
| --- | --- | --- | --- |
| `spyro1_runtime.{h,cpp}` | `spyro1` | `Spyro1Runtime` | `SCUS_942.28` image policy and the composition of its native leaf overrides. |
| `spyro1_frame_driver.{h,cpp}` | `spyro1` | `Spyro1FrameDriver` | One product step: boot prefix, the retail update, the picture, the frame tail. |
| `spyro1_field_scheduler.{h,cpp}` | `spyro1` | `FieldScheduler` | Spyro 1's field vocabulary over the shared owner: the boot window, the skip map, audio service. |
| `spyro1_boot_sequence.{h,cpp}` | `spyro1` | `BootSequence` | The native finite boot, and the ownership of the first presentation-only holds. |
| `spyro1_transition_skip.{h,cpp}` | `spyro1` | `TransitionSkip` | Cancelling a presentation-only transition on Start/Cross by taking its guest owner's own terminal transition. |
| `spyro1_press_latch.h` | `spyro1` | `PressLatch` | A boot fade/loader press held to the next hold. |
| `spyro1_input_phase.{h,cpp}` | `spyro1` | `InputPhase` | Which screen is taking input this pad frame — the key a recorded `.pad` frame is stored against. |
| `spyro1_frame_policy.h`, `spyro1_logo_facts.h` | `spyro1` | facts | Frame bounds and the disc's own logo. |
| `stage_update_observer.{h,cpp}` | `spyro1` | `StageUpdateObserver` | Opt-in read-only camera/player samples after the outer guest update returns. |
| `handoff_store_observer.{h,cpp}` | `spyro1` | `HandoffStoreObserver` | Opt-in pre/post RAM and tick snapshots at the New Game handoff PCs. |
| `native_*.{h,cpp}` | `spyro1::native` | one module per decomp source file | The image-scoped native leaves: camera, player animation and physics, moby helpers/lists/transform/collision/allocator, HUD collectables, environment light, effect state, level globals and initialization, particles, pause menu, pixel fade, cutscene, draw setup, gamepad, glow and sparkle pools, shaded moby queue, shared models, sound position, audio key state, and the SPU registers/voice/pitch/transfer-mode/key-status/common-attr/state/callback leaves. |

### `titles/spyro2/`, `titles/spyro3/`

| File | Namespace | Owner | Responsibility |
| --- | --- | --- | --- |
| `spyro{2,3}_runtime.{h,cpp}` | `spyro2`, `spyro3` | `Spyro2Runtime`, `Spyro3Runtime` | `SCUS_944.25` / `SCUS_944.67` identity, HLE plan, CD callback layout, boot-prefix frame driver, temporal product, logo and attract state, widescreen answer. |
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
- A Spyro 1 frame, field, input, audio, or lifecycle transition → `titles/spyro1/core/`.
- A semantic draw responsibility → one cohesive `game/render/` recipe/builder/submitter owner; the
  scene composer only orders owners.
- A runtime WAD identity or load/unload observation → the shared executor image tracker; title-specific
  archive semantics stay in `game/core/archive_transfer.*`.
- A diagnostic → an oracle/capture module that cannot mutate or submit the shipping picture.
- A capability change → `docs/project-state.md`; an atomic task/finding → `docs/issues/`; a binary
  dependency step → `docs/re-frontier.md`.
