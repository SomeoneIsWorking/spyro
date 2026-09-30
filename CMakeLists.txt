cmake_minimum_required(VERSION 3.21)
project(spyro_port CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PSXPORT_DIR "${CMAKE_SOURCE_DIR}/external/psxport" CACHE PATH
    "psxport framework source directory")
if(NOT EXISTS "${PSXPORT_DIR}/cmake/psxport.cmake")
  message(FATAL_ERROR
    "PSXPORT_DIR=${PSXPORT_DIR} has no cmake/psxport.cmake; resolve the pinned psxport checkout")
endif()
find_package(Git REQUIRED)
execute_process(
  COMMAND "${GIT_EXECUTABLE}" -C "${PSXPORT_DIR}" rev-parse HEAD
  RESULT_VARIABLE PSXPORT_REVISION_RESULT
  OUTPUT_VARIABLE PSXPORT_REVISION
  ERROR_QUIET
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT PSXPORT_REVISION_RESULT EQUAL 0 OR PSXPORT_REVISION STREQUAL "")
  message(FATAL_ERROR "cannot resolve the PSXPORT_DIR commit for build provenance")
endif()
file(WRITE "${CMAKE_BINARY_DIR}/psxport_resolved.txt"
     "dir = ${PSXPORT_DIR}\ncommit = ${PSXPORT_REVISION}\n")

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# ONE LOCKED PYTHON ENVIRONMENT. Every maintainer tool and test below runs on the project's own
# `uv` environment, never on whatever interpreter is first in PATH. Two tests were failing with
# `ModuleNotFoundError: No module named 'capstone'` because `find_package(Python3)` resolved a
# user-level interpreter while the dependency lives in ./.venv -- and the fix is not to install
# anything globally, it is to run the tools on the environment that declares them.
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/.venv/bin/python3")
  message(FATAL_ERROR
          "no project Python environment at ${CMAKE_SOURCE_DIR}/.venv; create it with "
          "`uv sync --frozen` rather than installing tools into a system interpreter")
endif()
set(Python3_EXECUTABLE "${CMAKE_SOURCE_DIR}/.venv/bin/python3"
    CACHE FILEPATH "Project interpreter" FORCE)

find_package(ZLIB REQUIRED)
find_package(zstd CONFIG QUIET)
if(NOT TARGET zstd::libzstd_shared)
  find_library(ZSTD_LIBRARY zstd REQUIRED)
  add_library(zstd::libzstd_shared SHARED IMPORTED)
  set_target_properties(zstd::libzstd_shared PROPERTIES IMPORTED_LOCATION "${ZSTD_LIBRARY}")
endif()
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

add_subdirectory(${PSXPORT_DIR}/vendor/beetle-psx/deps/libchdr
                 ${CMAKE_BINARY_DIR}/libchdr_build EXCLUDE_FROM_ALL)
include(${PSXPORT_DIR}/cmake/psxport.cmake)

set(SPYRO_CATALOG_HEADER ${CMAKE_BINARY_DIR}/generated/spyro_title_catalog.generated.h)
add_custom_command(
  OUTPUT ${SPYRO_CATALOG_HEADER}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/generate_title_catalog.py
          --output ${SPYRO_CATALOG_HEADER}
  DEPENDS
    ${CMAKE_SOURCE_DIR}/tools/generate_title_catalog.py
    ${CMAKE_SOURCE_DIR}/tools/title_identity.py
    ${CMAKE_SOURCE_DIR}/titles/spyro1/executable.json
    ${CMAKE_SOURCE_DIR}/titles/spyro2/executable.json
    ${CMAKE_SOURCE_DIR}/titles/spyro3/executable.json
  COMMENT "Generating executable identity catalog"
  VERBATIM)
add_custom_target(spyro_title_catalog DEPENDS ${SPYRO_CATALOG_HEADER})

set(SPYRO_RUNTIME_SOURCES
  game/core/archive_transfer.cpp
  game/core/content_identity.cpp
  game/core/cd_queue.cpp
  game/core/field_owner.cpp
  game/core/spyro_guest_call.cpp
  game/core/guest_execution.cpp
  game/core/native_angle.cpp
  game/core/guest_magnitude.cpp
  game/core/native_gte.cpp
  game/core/native_leaf.cpp
  game/core/native_rand.cpp
  game/core/native_util.cpp
  game/core/native_vec.cpp
  game/core/runtime_run.cpp
  game/core/spyro_context.cpp
  game/core/spyro_gate_debug.cpp
  game/core/spyro_runtime.cpp
  game/core/title_runtime_registry.cpp
  game/core/title_selection.cpp
  game/render/actor_billboard_face.cpp
  game/render/actor_draw_recipe.cpp
  game/render/actor_emit.cpp
  game/render/actor_pairing.cpp
  game/render/actor_stage.cpp
  game/render/actor_submission.cpp
  game/render/actor_face_submitter.cpp
  game/render/actor_global_order.cpp
  game/render/actor_ot_coalescer.cpp
  game/render/actor_model_codec.cpp
  game/render/actor_prefix_builder.cpp
  game/render/actor_recipe_capture.cpp
  game/render/actor_scene_builder.cpp
  game/render/actor_temporal.cpp
  game/render/actor_transform_math.cpp
  game/render/cutscene_scene_recipe.cpp
  game/render/face_light_environment.cpp
  game/render/face_light_program.cpp
  game/render/cyclorama_mask_recipe.cpp
  game/render/cyclorama_mask_submitter.cpp
  game/render/cyclorama_portal_mesh_recipe.cpp
  game/render/guest_trig.cpp
  game/render/hud_text_builder.cpp
  game/render/level_transition_scene.cpp
  game/render/pause_menu_recipe.cpp
  game/render/pause_menu_scene.cpp
  game/render/level_transition_tally_recipe.cpp
  game/render/cyclorama_portal_submitter.cpp
  game/render/cyclorama_scene_recipe.cpp
  game/render/field_collectables_recipe.cpp
  game/render/field_environment_recipe.cpp
  game/render/field_environment_scene.cpp
  game/render/field_moby_lists.cpp
  game/render/field_particle_quad_submitter.cpp
  game/render/field_particle_oriented_submitter.cpp
  game/render/field_particle_type2_submitter.cpp
  game/render/field_particle_type3_submitter.cpp
  game/render/field_particles_recipe.cpp
  game/render/wide_screen_space.cpp
  game/render/actor_scene_oracle.cpp
  game/render/paired_actor_color_fade.cpp
  game/render/gte_color_ops.cpp
  game/render/gpu_packet_decode.cpp
  game/render/field_scene_recipe.cpp
  game/render/field_shaded_queue_emit.cpp
  game/render/field_shaded_queue_recipe.cpp
  game/render/field_shaded_queue_scene.cpp
  game/render/field_shaded_queue_submitter.cpp
  game/render/field_shaded_queue_temporal.cpp
  game/render/field_shadow_recipe.cpp
  game/render/field_shadow_submitter.cpp
  game/render/moby_shadow_recipe.cpp
  game/render/glow_recipe.cpp
  game/render/glow_submitter.cpp
  game/render/dragon_burst_recipe.cpp
  game/render/dragon_scene_recipe.cpp
  game/render/fx_dragon_burst.cpp
  game/render/fx_dragon_scene.cpp
  game/render/field_model_chain.cpp
  game/render/projection_stream.cpp
  game/render/sparkle_recipe.cpp
  game/render/sparkle_submitter.cpp
  game/render/fx_glow_sparkle.cpp
  game/render/spyro_flame_matrix.cpp
    game/render/spyro_flame_recipe.cpp
  game/render/spyro_flame_submitter.cpp
  game/render/fx_spyro_flame.cpp
  game/render/moby_shadow_submitter.cpp
  game/render/field_tracers_recipe.cpp
  game/render/frame_env.cpp
  game/render/fx_actor_draw.cpp
  game/render/fx_field_actor_composition.cpp
  game/render/fx_field_collectables.cpp
  game/render/fx_field_cyclorama.cpp
  game/render/fx_field_environment.cpp
  game/render/fx_field_particles.cpp
  game/render/fx_field_player_actor.cpp
  game/render/fx_field_shadow.cpp
  game/render/fx_moby_shadow.cpp
  game/render/fx_field_tracers.cpp
  game/render/fx_paired_actor.cpp
  game/render/fx_screen_border.cpp
  game/render/fx_screen_fade.cpp
  game/render/fx_sprite_queue.cpp
  game/render/fx_title_menu.cpp
  game/render/fx_world_draw.cpp
  game/render/native_terrain.cpp
  game/render/painter_submission_preflight.cpp
  game/render/paired_actor_decode.cpp
  game/render/paired_actor_pose.cpp
  game/render/paired_actor_depth.cpp
  game/render/paired_actor_temporal_evidence.cpp
  game/render/temporal_scene.cpp
  game/render/terrain_emit.cpp
  game/render/terrain_recipe.cpp
  game/render/terrain_scene.cpp
  game/render/terrain_submitter.cpp
  game/render/terrain_temporal.cpp
  game/render/ui_anchor.cpp
  game/render/presentation_owner.cpp
  game/render/render_frame.cpp
  game/render/scene.cpp
  game/render/scene_painter_order.cpp
  game/render/screen_border_recipe.cpp
  game/render/screen_fade_recipe.cpp
  game/render/secondary_actor_emit.cpp
  game/render/secondary_actor_recipe.cpp
  game/render/secondary_actor_scene.cpp
  game/render/secondary_actor_temporal.cpp
  game/render/stage13_scene_recipe.cpp
  game/render/title_menu_recipe.cpp
  game/render/title_menu_state.cpp
  game/render/world_animation.cpp
  game/render/world_chunk_codec.cpp
  game/render/world_hq_recipe.cpp
  game/render/world_hq_refinement.cpp
  game/render/world_lq_recipe.cpp
  game/render/world_material_codec.cpp
    game/render/world_projection_math.cpp
  game/render/world_recipe.cpp
  game/render/world_scene_builder.cpp
  game/render/world_scene_capture.cpp
  game/render/world_scene_oracle.cpp
  game/render/margin_object_census.cpp
  game/render/sector_visibility.cpp
  game/render/world_scene_prepare.cpp
  game/render/world_source_animation.cpp
  game/render/world_scene_submitter.cpp
  game/render/world_source.cpp
  game/render/world_source_pair.cpp
  game/render/world_temporal.cpp
  titles/spyro1/core/spyro1_boot_sequence.cpp
  titles/spyro1/core/spyro1_field_scheduler.cpp
  titles/spyro1/core/spyro1_frame_driver.cpp
  titles/spyro1/core/handoff_store_observer.cpp
  titles/spyro1/core/stage_update_observer.cpp
  titles/spyro1/core/spyro1_transition_skip.cpp
  titles/spyro1/core/spyro1_runtime.cpp
  titles/spyro1/core/native_sound_position.cpp
  titles/spyro1/core/native_audio_key_state.cpp
  titles/spyro1/core/native_camera.cpp
  titles/spyro1/core/native_collision_shade.cpp
  titles/spyro1/core/native_cutscene.cpp
  titles/spyro1/core/native_draw_setup.cpp
  titles/spyro1/core/native_effect_state.cpp
  titles/spyro1/core/native_gamepad.cpp
  titles/spyro1/core/native_glow_sparkle_pools.cpp
  titles/spyro1/core/native_level_globals_table.cpp
  titles/spyro1/core/native_moby_allocator.cpp
  titles/spyro1/core/native_level_initialization.cpp
  titles/spyro1/core/native_moby_helpers.cpp
  titles/spyro1/core/native_moby_lists.cpp
  titles/spyro1/core/native_moby_transform.cpp
  titles/spyro1/core/native_particle_alloc.cpp
  titles/spyro1/core/native_pause_menu.cpp
  titles/spyro1/core/native_pixel_fade.cpp
  titles/spyro1/core/native_player_animation.cpp
  titles/spyro1/core/native_player_physics.cpp
  titles/spyro1/core/native_random_range.cpp
  titles/spyro1/core/native_shaded_moby_queue.cpp
  titles/spyro2/core/spyro2_frame_driver.cpp
  titles/spyro1/core/native_shared_models.cpp
  titles/spyro1/core/native_spu_registers.cpp
  titles/spyro1/core/native_spu_voice_pitch.cpp
  titles/spyro1/core/native_spu_state.cpp
  titles/spyro1/core/native_spu_callbacks.cpp
  titles/spyro1/core/native_spu_voice_attributes.cpp
  titles/spyro1/core/native_spu_key_status.cpp
  titles/spyro1/core/native_spu_transfer_mode.cpp
  titles/spyro1/core/native_spu_common_attr.cpp
  titles/spyro2/core/spyro2_runtime.cpp
  titles/spyro3/core/spyro3_frame_driver.cpp
  titles/spyro3/core/spyro3_runtime.cpp)

add_library(spyro_runtime STATIC ${SPYRO_RUNTIME_SOURCES})
add_dependencies(spyro_runtime spyro_title_catalog)
target_include_directories(spyro_runtime PUBLIC
  game/core
  game/render
  titles/spyro1/core
  titles/spyro2/core
  titles/spyro3/core
  ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(spyro_runtime PUBLIC psxport OpenSSL::Crypto)

add_executable(spyro_port
  game/core/main.cpp
  game/core/world_guest_execution.cpp)
add_dependencies(spyro_port gen_gpu_shaders)
target_link_libraries(spyro_port PRIVATE spyro_runtime)
set_target_properties(spyro_port PROPERTIES
  ENABLE_EXPORTS ON
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(CTest)
if(BUILD_TESTING)
  # This discriminator consumes an explicitly supplied authenticated EXE locally. No game assets
  # enter hosted CTest/CI, and no extracted instructions are committed as synthetic fixtures.
  add_executable(test_spyro_digital_input tests/test_spyro_digital_input.cpp)
  target_link_libraries(test_spyro_digital_input PRIVATE spyro_runtime)
  add_executable(test_archive_transfer tests/test_archive_transfer.cpp)
  target_link_libraries(test_archive_transfer PRIVATE spyro_runtime)
  add_test(NAME archive_transfer COMMAND test_archive_transfer)
  foreach(test_name IN ITEMS actor_billboard_face actor_prefix_builder actor_mesh_scratch actor_model_codec archive_transfer_contract cutscene_scene_recipe cyclorama_mask_recipe cyclorama_scene_recipe field_collectables_recipe field_2d_overlay_recipe pause_menu_recipe field_environment_recipe field_environment_scene field_scene_recipe field_tracers_recipe memcard_event_stack paired_actor_decode screen_border_recipe screen_fade_recipe secondary_actor_scene spu_hardware_init spu_pio_upload spyro_gate_debug stage13_scene_recipe text_sprites title_menu_recipe wide_clip_plan world_chunk_codec world_material_codec world_recipe world_scene_oracle actor_draw_recipe native_render_producers face_light_program secondary_actor_recipe secondary_actor_temporal temporal_scene world_animation world_source_pair world_temporal actor_temporal world_scene_prepare world_hq_refinement field_shadow_recipe moby_shadow_recipe actor_shadow_staging spyro_flame_recipe spyro_flame_matrix glow_recipe sparkle_recipe dragon_scene_recipe dragon_burst_recipe actor_scale_translation field_particles_recipe field_particle_endpoint margin_object_census wide_screen_space field_particle_oriented paired_actor_color_fade actor_global_order field_scheduler transition_skip cyclorama_portal_mesh_recipe hud_text_builder level_transition_tally_recipe field_shaded_queue_recipe field_shaded_queue_scene field_shaded_queue_temporal terrain_recipe terrain_temporal gpu_packet_decode ui_anchor hud_layout)
    add_executable(test_${test_name} tests/test_${test_name}.cpp)
    target_include_directories(test_${test_name} PRIVATE ${PSXPORT_DIR}/tests)
    target_link_libraries(test_${test_name} PRIVATE spyro_runtime)
    add_test(NAME ${test_name} COMMAND test_${test_name})
  endforeach()
  add_executable(test_runtime_run tests/test_runtime_run.cpp)
  target_link_libraries(test_runtime_run PRIVATE spyro_runtime)
  add_test(NAME runtime_run COMMAND test_runtime_run)
  add_executable(test_stage_update_observer tests/test_stage_update_observer.cpp)
  target_include_directories(test_stage_update_observer PRIVATE ${PSXPORT_DIR}/tests)
  target_link_libraries(test_stage_update_observer PRIVATE spyro_runtime)
  add_test(NAME stage_update_observer COMMAND test_stage_update_observer)
  add_executable(test_handoff_store_observer tests/test_handoff_store_observer.cpp)
  target_include_directories(test_handoff_store_observer PRIVATE ${PSXPORT_DIR}/tests)
  target_link_libraries(test_handoff_store_observer PRIVATE spyro_runtime)
  add_test(NAME handoff_store_observer COMMAND test_handoff_store_observer)
  add_executable(test_guest_execution tests/test_guest_execution.cpp)
  target_link_libraries(test_guest_execution PRIVATE spyro_runtime)
  add_test(NAME guest_execution COMMAND test_guest_execution)
  # Spyro 2's boot driver: the finite guest call, one delivered field per guest display wait, one
  # presented field per step, and the refusals that keep a wedged or misentered call from reading as
  # progress. Registered explicitly rather than through the list above because it is the only case
  # that drives the Spyro 2 driver end to end.
  add_executable(test_spyro2_boot_driver
    tests/test_spyro2_boot_driver.cpp)
  target_include_directories(test_spyro2_boot_driver PRIVATE
    ${PSXPORT_DIR}/tests game/core titles/spyro2/core)
  target_link_libraries(test_spyro2_boot_driver PRIVATE spyro_runtime)
  add_test(NAME spyro2_boot_driver COMMAND test_spyro2_boot_driver)
  # Spyro 3's boot driver: the same title-neutral resume and delivery contract through Spyro 3's own
  # measured constants, plus the plan self-consistency check that keeps a wrong guest address from
  # only showing up in a run with a disc attached.
  add_executable(test_spyro3_boot_driver
    tests/test_spyro3_boot_driver.cpp)
  target_include_directories(test_spyro3_boot_driver PRIVATE
    ${PSXPORT_DIR}/tests game/core titles/spyro3/core)
  target_link_libraries(test_spyro3_boot_driver PRIVATE spyro_runtime)
  add_test(NAME spyro3_boot_driver COMMAND test_spyro3_boot_driver)
  add_executable(test_title_selection
    tests/test_title_selection.cpp)
  target_include_directories(test_title_selection PRIVATE game/core)
  target_link_libraries(test_title_selection PRIVATE spyro_runtime OpenSSL::Crypto)
  add_test(NAME title_selection COMMAND test_title_selection)
  add_executable(test_spyro3_runtime
    tests/test_spyro3_runtime.cpp)
  target_include_directories(test_spyro3_runtime PRIVATE game/core)
  target_link_libraries(test_spyro3_runtime PRIVATE spyro_runtime)
  add_test(NAME spyro3_runtime COMMAND test_spyro3_runtime)
  add_test(NAME launcher_policy
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_launcher.py)
  add_test(NAME provision_titles
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_provision_titles.py)
  add_test(NAME source_policy
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/source_policy.py)
  add_test(NAME source_policy_selftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/source_policy.py --selftest)
  # The agent drivers read their guest addresses out of game/core/guest_globals.h. If that parser
  # ever silently stops matching the header's form, every driver would fall back to nothing at all.
  add_test(NAME guest_globals_selftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/guest_globals.py --selftest)
  # The route both the interactive driver and the oracle's level checkpoint walk. A steering table
  # that is quietly wrong still reaches destinations, by detouring; only a fixture catches it.
  add_test(NAME spyro1_steering_selftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/spyro1_steering.py --selftest)
  # The four scene routes the reach corpus judges on (docs/issues/0147): a flight level, a boss, a
  # death and respawn, and the in-game save. Gated for the failure they most need to not have --
  # reporting a scene they never reached. So every selftest case is a REFUSAL: a homeworld with no
  # flight or boss portal, a level with no fairy, a bounded island where nothing dies, and a guest
  # that is still at GS_Playing after every one of those. A module whose gate only proved the happy
  # path would pass for a route that walks off the edge of the map and calls it a boss fight.
  add_test(NAME route_scenes_selftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/route_scenes.py --selftest)
  # The pre-arrival pad script, which is the ONLY way a driven run can press anything before it
  # reaches GS_Playing -- boot, the logos, the attract fly-in and every transition screen. Gated
  # because the failure it has to not have is the quiet one: a condition that silently never matches
  # looks exactly like a screen the game never reached. So its selftest covers a screen that IS up
  # (one edge out of forty samples), a screen that is NOT (scanned and matched nothing, reported as
  # such with the sample count), a re-visit (still ONE edge -- gamestate 0 is both the boot logo and
  # the arrival state, and a live run that re-fired there pressed Start in gameplay and opened the
  # pause menu), and every malformed spec refused by name rather than left looking armed.
  add_test(NAME pre_arrival_press_selftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/pre_arrival_press.py --selftest)
  # The widened-capture margin census (issue 0152). Gated for the same reason as the probes above:
  # its whole job is to say whether a widening revealed anything, and a margin counter that can only
  # return 0 measures a widening perfectly while reporting that nothing appeared. The selftest
  # carries both classes -- a grown picture with content in the new columns must come out NON-zero,
  # and a widened canvas with a black margin, a re-centred picture and a flat card must not.
  add_test(NAME margin_coverage_selftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/margin_coverage.py --selftest)
  # The per-element HUD anchoring census. Its selftest is BOTH answers — a correctly anchored pair
  # and a deliberately stretched one — because a census that has only ever printed PASS is a census
  # that cannot fail.
  add_test(NAME hud_anchor_census_selftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/hud_anchor_census.py --selftest)
  # The four instruments the tick-41 divergence rests on. Each one's answer is only worth reading if
  # the instrument can be shown to produce the OTHER answer, which is what these selftests are for —
  # and three of the four exist because the instrument was caught being wrong first:
  #   probe_guest_disasm  the file-offset formula is verified against 62,183 decoded instructions, and
  #                       its selftest carries a byte-order discriminator that must separate the two
  #                       modes, since a big-endian slip would decode plausible nonsense.
  #   probe_emitter_predicate  re-derives the six call-site rows and all ten listing claims the moby
  #                       census rests on FROM THE LISTING TEXT, so the census cannot quietly disagree
  #                       with the disassembly it claims to measure.
  #   probe_store_sites   the call-site resolver's sign extension is load-bearing: the one REAL listing
  #                       line the console recorded has a NEGATIVE displacement, and the suite fails if
  #                       the resolver stops sign-extending. Three decoys sit one word away, carry a
  #                       positive displacement, or have no `lui` base at all.
  #   probe_tick_divergence  `dist2d`/`OctDistance` against 5 listing-derived values, including both
  #                       asymmetric arms that a swapped transcription gets wrong.
  #   probe_level_update_dispatch  the per-level update dispatch is DERIVED from the image (the switch's
  #                       jump table, the `lui`/`addiu` pair feeding each store, the `jalr` on each
  #                       reader) and cross-checked against external/spyro-1. Its selftest requires both
  #                       answers -- a level id with a case body and one that shares the tail -- plus the
  #                       derived values that must agree with the decompilation, so a derivation that
  #                       silently stopped matching cannot pass. This is the instrument issue 0133's
  #                       "the level update is never entered" conclusion needed and did not have.
  #   probe_blended_anim  the census of the environment animation BLEND FACTOR, the one byte the image
  #                       reads at 0x80025BF4 to choose INTPL/DPCS over a straight copy. Its zero is the
  #                       load-bearing claim -- "no reachable nonzero blend factor in the levels these
  #                       routes load" -- so the selftest covers the four ways that zero can be
  #                       manufactured: a positive fixture that must be READ, a negative fixture that
  #                       must read as zero, a set pointer outside the dump that must report NOT READ
  #                       rather than zero, and an unauthored count of 0 that must scan nothing at all
  #                       (the fixed 128-index scan that preceded it read 6 fabricated "entries" out of
  #                       a table Artisans never authored, three carrying a nonzero factor byte).
  #   census_level_blend  the OFFLINE census of that same blend-factor byte across every level in
  #                       WAD.WAD, bounded by each channel's own authored count and each record's own
  #                       base-offset word. Nine hermetic cases, and the load-bearing one is that it
  #                       reports BOTH metrics at once: the SELECTED keyframe (the one the record's
  #                       index byte names) and the AUTHORED table (every 8-byte slot). They disagree
  #                       on real data -- every level authors 739 selected keyframes at factor 0 and
  #                       47,932 of 51,042 authored keyframes at a nonzero factor -- so an
  #                       instrument reporting only one of them could agree with a wrong conclusion
  #                       forever. Its fixtures are built with `LoadLevelScene`'s own component
  #                       arithmetic, so a fixture cannot pass by agreeing with itself about a
  #                       convention the census invented. The refusal cases are the ones 0134 needed:
  #                       a count of 0 scans nothing, a record resolving outside its entry is NOT
  #                       READ and never factor 0, a payload claiming more than its component holds
  #                       is flagged, a run shorter than seven has no channel attribution and is
  #                       refused outright, and an authored count above the guest's own 0x80 index
  #                       ceiling is refused rather than scanned.
  #   probe_load_step  where `g_LoadStage`'s value at a checkpoint was TAKEN, decided by the image's
  #                     own bytes: `LoadLevel`'s case 1 stores `stage + 1` at 0x800155E0, the next
  #                     instruction is a `lui` and NOT a branch, and case 2's body writes nothing to
  #                     `g_LoadStage` before the shared tail overwrites it at 0x800163B8 -- so stage 2
  #                     exists only INSIDE one call and is not a resting value. The selftest re-reads
  #                     every one of those words out of SCUS_942.28 and fails if they change, and
  #                     requires the classifier to return the OTHER verdict on stage 1-vs-3 and on a
  #                     differing `m_State`, so a classifier that can only ever exonerate cannot pass.
  #                     Like `probe_guest_disasm`, it needs the provisioned image and REFUSES (exit 2)
  #                     without it rather than passing vacuously; that refusal is the correct answer
  #                     on a machine with no disc, not a green test.
  foreach(probe
      probe_guest_disasm
      probe_emitter_predicate
      probe_store_sites
      probe_tick_divergence
      probe_level_update_dispatch
      probe_blended_anim
      census_level_blend
      #   census_level_face_material  the OFFLINE census of the material word on every low-poly
      #                       face of every level in WAD.WAD, which is the question `c229e45`'s
      #                       translucent arm turns on: does ANY level author a face whose material
      #                       word has bit 2 set, the bit 0x8002651C `andi $a3,$t6,4` tests? It
      #                       answers YES -- 1,592 of 83,401 faces across 6 of 35 levels -- so the
      #                       arm is live in this game and unreachable only in the one resident
      #                       level issue 0143 measured. Two things make that answer trustworthy
      #                       rather than a plausible one. The face record's layout is EXTRACTED
      #                       from the shipping game/render/world_chunk_codec.cpp by
      #                       world_chunk_layout.py and refused if that text stops having the
      #                       shape it reads, and the material bit censused is the bit
      #                       world_lq_recipe.cpp itself tests, read from that file. And the
      #                       selftest covers the four ways this answer could be manufactured: a
      #                       positive fixture whose bit-2 face MUST be found, a negative over a
      #                       corpus whose low bits span four combinations (0143's all-multiples-
      #                       of-8 Artisans corpus could be reproduced by a broken mask), a sector
      #                       pointer resolving outside the entry reported NOT READ rather than
      #                       zero faces, and an authored sector count of 0 scanning nothing. The
      #                       mis-stride case is the load-bearing one: reading past the declared
      #                       face count FINDS bit-2 faces the narrow walk does not, so a reported
      #                       narrow zero cannot be a runaway read in disguise.
      census_level_face_material
      #   world_chunk_layout  the reader the census above depends on, which reads the low-poly
      #                       field layout out of the shipping codec instead of restating it. Its
      #                       selftest requires four perturbed COPIES of the codec to each change
      #                       or refuse the extracted layout, and requires the port's own
      #                       `& 4u` translucent selector to be read from world_lq_recipe.cpp
      #                       rather than taken on trust -- a second copy of a layout is a second
      #                       unverified claim about the same guest bytes, and it drifts exactly
      #                       where a re-port drifts.
      world_chunk_layout
      # pool_water_probe measures whether the pool's per-block colour noise -- the defect the
      # guest's discarded water colours caused -- is present in a captured frame. Its load-bearing
      # selftest cases are the two WRONG-REGION failures this tool actually made in the wild, made
      # permanent: a saturated blue DRAGON is larger in pixel count than the pool and is not a
      # sheet, and a sky/gem sliver is blue and tiny. Both were measured and both produced a
      # confident "this is the defect" verdict about something that was not in the picture. The
      # positive case -- a broad noisy sheet IS located -- is asserted alongside them so the shape
      # guard cannot be satisfied by refusing everything.
      pool_water_probe
      probe_load_stage
      probe_load_step
      # The HUD GLYPH reader. Its selftest is hermetic and covers the three ways the read can lie
      # about what it was asked for -- the 64-word transport cap, a short answer, and an answer that
      # echoes a DIFFERENT address, which is the one that bit this probe in a real run: taking the
      # first word-list on the wire reads one request late, still returns plausible words, and put
      # a record's fields in disagreement with a hex dump of the very same words.
      probe_hud_glyph
      # The TITLE CARD reader. Its selftest covers the two ways this class of report lies: a
      # precondition reported as satisfied when it is not (five negative cases against one positive,
      # including the exact 0xFFFF0800 m_Held word a real run produces, which a sibling probe once
      # misread as "no button held"), and a `lui` immediate read as a low displacement -- the bug
      # that made this file's first module map print an arena of 0x80082A40 and still agree with
      # itself, because both of its cross-checks went through the same wrong decoder.
      probe_title_card
      # The SCREEN-SPACE OVERLAY EXTENT reader: how many columns of a submitted 2D quad actually
      # reach the picture, over a strip of consecutive presents. Hermetic, and its selftest is the
      # negative set that matters for a detector: a clean frame (ABSENT), a 13-column world glow the
      # same statistic also fires on (not the overlay), scattered hits (not a rectangle), an overlay
      # clipped to 146 of its own 232 columns (NARROWER, with the short span), a strip whose overlay
      # is on 3 of 6 presents (the cadence a single screenshot cannot see), and three refusals -- no
      # frames, a non-P6 file, a truncated PPM -- none of which may read as a clean zero.
      overlay_extent
      # The PAUSE PANEL COLOUR RECOVERY. docs/issues/0144 shipped a panel whose colour byte was the
      # port's own 0xE0 where retail stores 0x40, and the recovery is a reaching-definition dataflow
      # over all 2,212 words of the handler: 29 writes to $s4 exist and exactly ONE of them reaches the
      # panel's three colour stores. That "exactly one" is a claim a reader cannot check by eye -- an
      # omitted opcode class, a mishandled delay-slot branch, or a wrongly modelled unconditional jump
      # all produce a confident answer about a different program -- so the analysis is gated on its
      # negative set: the fallthrough case that must report TWO definitions, the computed-register
      # `jr` that must refuse, the `sw` in place of the `sb`, and the perturbed immediate that must
      # move the answer. Hermetic: it builds bodies, never reads the image, so it cannot pass by
      # having a corpus.
      probe_pause_panel_colour
      # The swarm acceptance gate for one native override; its selftest pins the scope rule.
      native_override_gate
      # Override constants must be addresses the retail code computes (docs/issues/0148): the
      # selftest pins a sign-extended table accepted and its unsigned-immediate twin refused.
      override_constants
      # The route corpus's jal-target denominator, image split, registration parse and the scene
      # proof reader (absent / present / stale / unreadable).
      reach_corpus
      # The per-FIELD route trace and the first divergence between two builds' runs of one route.
      # Its selftest is the negative set a "these two runs agree" claim needs: a log with no field
      # lines, a field index that skips and a state line that moves backwards are all REFUSED
      # instead of reading as an empty trace, and a truncated second log is reported as truncated
      # rather than as agreement over the prefix.
      route_trace
      # The GUEST WORD reader. Its selftest is the negative set that matters for a reachability
      # claim: an `ori`-built base, a base invalidated by a call, a base that survives a COP2 (GTE)
      # instruction and an undefined SPECIAL encoding must all be REFUSED, because this class of
      # report reads as a clean measurement of access when it is really a measurement of nothing.
      # The third negative is the defect this tool had: it carried a constant across a GTE block
      # and reported `sll` and `bltz` sites as stores over a guest word. Two positives guard the
      # fix -- a fresh base after every barrier, and two stores through one base, which a probe
      # that treated a store as a register clobber would report once.
      probe_guest_word)
    add_test(NAME ${probe}_selftest
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/${probe}.py --selftest)
  endforeach()
  # THE DECOMPILATION PIPELINE'S OWN SELFTEST. Gated because a decompilation tool that reports a
  # clean result while it never ran, or while it emitted a TRUNCATED body that reads like a complete
  # function, is the workspace's signature failure mode -- and this tool's whole output is prose a
  # human is expected to trust. Four modules, each with its own negative cases: the image mapping
  # (62,183 recorded instructions, and a discriminator that shows a shifted formula separating),
  # the decompile ADMISSION rules (7 refusals plus the falsifier that proves the anchor check can
  # fail), the Ghidra session (6 refusals, driven through an injected runner so no Ghidra is needed),
  # and the work order (which must refuse an address it does not name, or the corpus is unbounded).
  # Hermetic: no Ghidra, no network, no product run. The decompile itself is NOT a test -- it needs
  # the provisioned image and 1 GB of heap, and its verdict belongs to a run, not to a gate.
  add_test(NAME decomp_selftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/decomp.py --selftest)
  set_tests_properties(decomp_selftest PROPERTIES TIMEOUT 300)

  # THE COMPARATOR'S OWN SELFTEST, which was never wired in. A comparator that reports 0 divergences
  # is only evidence if it CAN report one, and this is the check that says so: it seeds a byte at a
  # declared range and requires the comparator to detect it. Verified working before being gated --
  # `selftest: seeded byte at 0x80078A58; comparator DETECTED it`.
  #
  # It costs ~82 s because it drives both cores for real, so it is labelled SLOW: a full gate should
  # pay it, and a tight edit loop should not. `ctest -LE slow` skips it; nothing else is skipped.
  add_test(NAME oracle_compare_selftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/oracle_compare.py --selftest)
  set_tests_properties(oracle_compare_selftest PROPERTIES LABELS slow TIMEOUT 900)

  # The cadence probe's arithmetic, gated because it is what issue 0114's cadence claim rests on and
  # an ungated boundary rule is exactly the kind of thing that silently stops being the rule. It is
  # hermetic (synthetic series, no disc, no window) and instant, so it is NOT labelled slow: that
  # label means "drives both cores for ~80 s", and this does not.
  #
  # Its cases are the ones that WOULD fail: a window whose size and the value the guest stored
  # DISAGREE must be reported rather than accepted, and a 6-field window must read as the clamp's 4
  # and not as 6. A build that only ever saw 2-field windows would pass a weaker version of this.
  add_test(NAME oracle_cadence_probe_selftest
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/oracle_cadence_probe.py --selftest)
  set_tests_properties(oracle_cadence_probe_selftest PROPERTIES TIMEOUT 60)

  # The level-entry attribution's arithmetic, gated for the same reason and because the failure it
  # must not have is the quiet one: an address no transfer covers must return NOTHING, and two
  # transfers that both cover it must both be returned. A reader that returned the nearest load
  # would name a supplier for bytes nobody wrote, which is how a level entry gets "evidence" that
  # no image was ever resident.
  add_test(NAME oracle_level_entry_selftest
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/oracle_level_entry.py --selftest)
  set_tests_properties(oracle_level_entry_selftest PROPERTIES TIMEOUT 60)
endif()

# THE PIN TOOL AND ITS BEHAVIOUR TEST. Unguarded, because this repository calls enable_testing()
# directly: an if(BUILD_TESTING) add_test here is silently inert, which is the failure this gate exists to
# prevent. Verified live: perturbing psxport.pin turns the first one red.
add_test(
  NAME spyro_psxport_pin_live
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/psxport_sync.py"
          --check --build "${CMAKE_BINARY_DIR}")
add_test(
  NAME spyro_psxport_pin_selftest
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_psxport_sync.py")
