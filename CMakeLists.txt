cmake_minimum_required(VERSION 3.21)
project(spyro_port CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PSXPORT_DIR "${CMAKE_SOURCE_DIR}/external/psxport" CACHE PATH
    "psxport framework source directory")
if(NOT EXISTS "${PSXPORT_DIR}/cmake/psxport.cmake")
  message(FATAL_ERROR
    "PSXPORT_DIR=${PSXPORT_DIR} has no cmake/psxport.cmake; resolve the psxport checkout")
endif()
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
  game/core/image_publication.cpp
  game/core/load_ledger.cpp
  game/core/stock_read_publication.cpp
  game/core/cd_queue.cpp
  game/core/boot_prefix_frame_driver.cpp
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
  game/host/title_availability.cpp
  game/host/picker_content.cpp
  game/host/picker_layout.cpp
  game/host/picker_composite.cpp
  game/host/picker_runtime.cpp
  game/host/picker_session.cpp
  game/host/panel_sessions.cpp
  game/host/panel_logo.cpp
  game/host/title_session.cpp
  game/host/product_host.cpp
  game/render/actor/actor_billboard_face.cpp
  game/render/actor/actor_draw_recipe.cpp
  game/render/actor/actor_emit.cpp
  game/render/temporal/actor_pairing.cpp
  game/render/actor/actor_stage.cpp
  game/render/actor/actor_submission.cpp
  game/render/actor/actor_face_submitter.cpp
  game/render/actor/actor_global_order.cpp
  game/render/actor/actor_ot_coalescer.cpp
  game/render/actor/actor_model_codec.cpp
  game/render/actor/actor_prefix_builder.cpp
  game/render/actor/actor_recipe_capture.cpp
  game/render/actor/actor_scene_builder.cpp
  game/render/temporal/actor_temporal.cpp
  game/render/actor/actor_transform_math.cpp
  game/render/scene/cutscene_scene_recipe.cpp
  game/render/field/face_light_environment.cpp
  game/render/field/face_light_program.cpp
  game/render/world/cyclorama_mask_recipe.cpp
  game/render/world/cyclorama_mask_submitter.cpp
  game/render/world/cyclorama_portal_mesh_recipe.cpp
  game/render/frame/guest_trig.cpp
  game/render/field/shaded_moby_light.cpp
  game/render/hud/hud_text_builder.cpp
  game/render/scene/demo_text_scene.cpp
  game/render/scene/level_transition_scene.cpp
  game/render/scene/pause_menu_recipe.cpp
  game/render/scene/pause_menu_scene.cpp
  game/render/world/menu_world_pass.cpp
  game/render/hud/menu_lighting.cpp
  game/render/hud/menu_panel_submit.cpp
  game/render/scene/fairy_menu_recipe.cpp
  game/render/scene/fairy_menu_scene.cpp
  game/render/scene/level_transition_tally_recipe.cpp
  game/render/world/cyclorama_portal_submitter.cpp
  game/render/world/cyclorama_scene_recipe.cpp
  game/render/field/field_collectables_recipe.cpp
  game/render/field/field_environment_recipe.cpp
  game/render/field/field_environment_scene.cpp
  game/render/field/field_moby_lists.cpp
  game/render/field/field_particle_quad_submitter.cpp
  game/render/field/field_particle_oriented_submitter.cpp
  game/render/field/field_particle_type2_submitter.cpp
  game/render/field/field_particle_type3_submitter.cpp
  game/render/field/field_particles_recipe.cpp
  game/render/hud/wide_screen_space.cpp
  game/render/actor/actor_scene_oracle.cpp
  game/render/actor/paired_actor_color_fade.cpp
  game/render/frame/gte_color_ops.cpp
  game/render/frame/gpu_packet_decode.cpp
  game/render/field/field_scene_recipe.cpp
  game/render/field/field_shaded_queue_emit.cpp
  game/render/field/field_shaded_queue_recipe.cpp
  game/render/field/field_shaded_queue_scene.cpp
  game/render/field/field_shaded_queue_submitter.cpp
  game/render/temporal/field_shaded_queue_temporal.cpp
  game/render/field/field_shadow_recipe.cpp
  game/render/field/field_shadow_submitter.cpp
  game/render/field/moby_shadow_recipe.cpp
  game/render/field/glow_recipe.cpp
  game/render/field/glow_submitter.cpp
  game/render/scene/dragon_burst_recipe.cpp
  game/render/scene/dragon_scene_recipe.cpp
  game/render/scene/dragon_burst_producer.cpp
  game/render/scene/dragon_scene_producer.cpp
  game/render/field/field_model_chain.cpp
  game/render/frame/projection_stream.cpp
  game/render/field/sparkle_recipe.cpp
  game/render/field/sparkle_submitter.cpp
  game/render/field/glow_sparkle.cpp
  game/render/field/spyro_flame_matrix.cpp
    game/render/field/spyro_flame_recipe.cpp
  game/render/field/spyro_flame_submitter.cpp
  game/render/field/spyro_flame.cpp
  game/render/field/moby_shadow_submitter.cpp
  game/render/field/field_tracers_recipe.cpp
  game/render/frame/frame_env.cpp
  game/render/actor/actor_producer.cpp
  game/render/field/field_actor_composition.cpp
  game/render/field/field_collectables.cpp
  game/render/field/field_cyclorama.cpp
  game/render/field/field_environment.cpp
  game/render/field/field_particles.cpp
  game/render/field/field_player_actor.cpp
  game/render/field/field_shadow.cpp
  game/render/field/moby_shadow.cpp
  game/render/field/field_tracers.cpp
  game/render/actor/paired_actor.cpp
  game/render/actor/paired_actor_producer.cpp
  game/render/actor/paired_actor_projection.cpp
  game/render/actor/paired_actor_selftest.cpp
  game/render/actor/paired_actor_temporal.cpp
  game/render/hud/screen_border.cpp
  game/render/hud/screen_fade.cpp
  game/render/field/sprite_queue.cpp
  game/render/scene/title_menu.cpp
  game/render/world/world_producer.cpp
  game/render/terrain/native_terrain.cpp
  game/render/frame/painter_submission_preflight.cpp
  game/render/actor/paired_actor_decode.cpp
  game/render/actor/paired_actor_pose.cpp
  game/render/actor/paired_actor_depth.cpp
  game/render/actor/paired_actor_temporal_evidence.cpp
  game/render/temporal/interp_census.cpp
  game/render/temporal/temporal_scene.cpp
  game/render/terrain/terrain_emit.cpp
  game/render/terrain/guest_camera_builder.cpp
  game/render/terrain/terrain_packet_sink.cpp
  game/render/temporal/terrain_world_pass.cpp
  game/render/terrain/terrain_recipe.cpp
  game/render/terrain/terrain_scene.cpp
  game/render/terrain/terrain_submitter.cpp
  game/render/temporal/terrain_temporal.cpp
  game/render/hud/ui_anchor.cpp
  game/render/frame/presentation_owner.cpp
  game/render/frame/frame_renderer.cpp
  game/render/frame/scene.cpp
  game/render/frame/scene_painter_order.cpp
  game/render/hud/screen_border_recipe.cpp
  game/render/hud/screen_fade_recipe.cpp
  game/render/actor/secondary_actor_emit.cpp
  game/render/actor/secondary_actor_recipe.cpp
  game/render/actor/secondary_actor_scene.cpp
  game/render/temporal/secondary_actor_temporal.cpp
  game/render/scene/stage13_scene_recipe.cpp
  game/render/scene/stage13_text_run.cpp
  game/render/scene/title_menu_recipe.cpp
  game/render/scene/title_menu_state.cpp
  game/render/world/world_animation.cpp
  game/render/world/world_chunk_codec.cpp
  game/render/world/world_hq_recipe.cpp
  game/render/world/world_hq_refinement.cpp
  game/render/world/world_lq_recipe.cpp
  game/render/world/world_material_codec.cpp
    game/render/world/world_projection_math.cpp
  game/render/world/world_recipe.cpp
  game/render/world/world_scene_builder.cpp
  game/render/world/world_scene_capture.cpp
  game/render/world/world_scene_oracle.cpp
  game/render/temporal/margin_object_census.cpp
  game/render/field/sector_visibility.cpp
  game/render/world/world_scene_prepare.cpp
  game/render/world/world_source_animation.cpp
  game/render/world/world_scene_submitter.cpp
  game/render/world/world_source.cpp
  game/render/world/world_source_pair.cpp
  game/render/temporal/world_temporal.cpp
  titles/spyro1/core/spyro1_boot_sequence.cpp
  titles/spyro1/core/spyro1_field_scheduler.cpp
  titles/spyro1/core/spyro1_frame_driver.cpp
  titles/spyro1/core/spyro1_input_phase.cpp
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
  titles/spyro1/core/native_environment_light.cpp
  titles/spyro1/core/native_gamepad.cpp
  titles/spyro1/core/native_glow_sparkle_pools.cpp
  titles/spyro1/core/native_hud_collectables.cpp
  titles/spyro1/core/native_level_globals_table.cpp
  titles/spyro1/core/native_moby_allocator.cpp
  titles/spyro1/core/native_moby_collision.cpp
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
  titles/spyro1/core/native_shared_models.cpp
  titles/spyro1/core/native_spu_registers.cpp
  titles/spyro1/core/native_spu_voice_pitch.cpp
  titles/spyro1/core/native_spu_state.cpp
  titles/spyro1/core/native_spu_callbacks.cpp
  titles/spyro1/core/native_spu_voice_attributes.cpp
  titles/spyro1/core/native_spu_key_status.cpp
  titles/spyro1/core/native_spu_transfer_mode.cpp
  titles/spyro1/core/native_spu_common_attr.cpp
  game/core/guest_widescreen_owner.cpp
  game/render/field/guest_moby_rotation.cpp
  game/render/field/guest_moby_visibility.cpp
  game/render/terrain/guest_terrain_classify.cpp
  game/render/terrain/guest_terrain_coarse.cpp
  game/render/terrain/guest_terrain_detail.cpp
  game/render/terrain/guest_terrain_drawer.cpp
  game/render/terrain/guest_terrain_far.cpp
  game/render/terrain/guest_terrain_fine.cpp
  game/render/terrain/guest_terrain_fog.cpp
  game/render/terrain/guest_terrain_mesh.cpp
  game/render/terrain/guest_terrain_memory.cpp
  game/render/terrain/guest_terrain_resplit.cpp
  game/render/terrain/guest_terrain_split.cpp
  game/render/terrain/guest_terrain_translucent.cpp
  titles/spyro2/core/spyro2_runtime.cpp
  titles/spyro2/render/spyro2_hud_anchor.cpp
  titles/spyro3/core/spyro3_runtime.cpp
  titles/spyro3/render/spyro3_hud_anchor.cpp)

add_library(spyro_runtime STATIC ${SPYRO_RUNTIME_SOURCES})
add_dependencies(spyro_runtime spyro_title_catalog)
target_include_directories(spyro_runtime PUBLIC
  game/core
  game/host
  game/render/actor
  game/render/field
  game/render/frame
  game/render/hud
  game/render/scene
  game/render/temporal
  game/render/terrain
  game/render/world
  titles/spyro1/core
  titles/spyro2/core
  titles/spyro2/render
  titles/spyro3/core
  titles/spyro3/render
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
  foreach(test_name IN ITEMS actor_billboard_face actor_prefix_builder actor_mesh_scratch actor_model_codec archive_transfer_contract cutscene_scene_recipe cyclorama_mask_recipe cyclorama_scene_recipe field_collectables_recipe field_2d_overlay_recipe pause_menu_recipe fairy_menu_recipe field_environment_recipe field_environment_scene field_scene_recipe field_tracers_recipe memcard_event_stack paired_actor_decode screen_border_recipe screen_fade_recipe secondary_actor_scene spu_hardware_init spu_pio_upload spyro_gate_debug stage13_scene_recipe text_sprites title_menu_recipe wide_clip_plan world_chunk_codec world_material_codec world_recipe world_scene_oracle actor_draw_recipe native_render_producers face_light_program shaded_moby_light secondary_actor_recipe secondary_actor_temporal temporal_scene interp_census world_animation world_source_pair world_temporal actor_temporal world_scene_prepare world_hq_refinement field_shadow_recipe moby_shadow_recipe actor_shadow_staging spyro_flame_recipe spyro_flame_matrix glow_recipe sparkle_recipe dragon_scene_recipe dragon_burst_recipe actor_scale_translation field_particles_recipe field_particle_endpoint margin_object_census wide_screen_space field_particle_oriented paired_actor_color_fade actor_global_order field_scheduler transition_skip press_latch cyclorama_portal_mesh_recipe hud_text_builder demo_text_scene level_transition_tally_recipe field_shaded_queue_recipe field_shaded_queue_scene field_shaded_queue_temporal terrain_recipe terrain_temporal guest_moby_frustum spyro2_hud_anchor spyro3_hud_anchor guest_terrain_screen guest_camera_builder guest_terrain_memory terrain_packet_sink terrain_packet_arenas terrain_packet_window gpu_packet_decode ui_anchor hud_layout load_ledger)

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
  # The shared boot-prefix frame driver, over synthetic facts: the finite guest call, one delivered
  # field per guest display wait, one presented field per step, and the refusals that keep a wedged
  # or misentered call from reading as progress.
  add_executable(test_boot_prefix_frame_driver
    tests/test_boot_prefix_frame_driver.cpp)
  target_include_directories(test_boot_prefix_frame_driver PRIVATE
    ${PSXPORT_DIR}/tests game/core titles/spyro2/core)
  target_link_libraries(test_boot_prefix_frame_driver PRIVATE spyro_runtime)
  add_test(NAME boot_prefix_frame_driver COMMAND test_boot_prefix_frame_driver)
  # Each title's measured boot-prefix facts and the runtime plan that has to agree with them, so a
  # wrong guest address fails here rather than only in a run with a disc attached.
  add_executable(test_boot_prefix_facts
    tests/test_boot_prefix_facts.cpp)
  target_include_directories(test_boot_prefix_facts PRIVATE
    ${PSXPORT_DIR}/tests game/core titles/spyro2/core titles/spyro3/core)
  target_link_libraries(test_boot_prefix_facts PRIVATE spyro_runtime)
  add_test(NAME boot_prefix_facts COMMAND test_boot_prefix_facts)
  # Spyro 2 and 3 stream code modules through the framework's stock CdRead and call into them: the
  # landing is published as a resident image (game/core/stock_read_publication.*), driven end to end
  # through the shipping read, both title runtimes, the image catalog, and Lightrec.
  add_executable(test_stock_read_publication
    tests/test_stock_read_publication.cpp)
  target_include_directories(test_stock_read_publication PRIVATE
    ${PSXPORT_DIR}/tests game/core titles/spyro2/core titles/spyro3/core)
  target_link_libraries(test_stock_read_publication PRIVATE spyro_runtime)
  add_test(NAME stock_read_publication COMMAND test_stock_read_publication)
  add_executable(test_title_selection
    tests/test_title_selection.cpp)
  target_include_directories(test_title_selection PRIVATE game/core)
  target_link_libraries(test_title_selection PRIVATE spyro_runtime OpenSSL::Crypto)
  add_test(NAME title_selection COMMAND test_title_selection)
  add_executable(test_title_picker tests/test_title_picker.cpp)
  target_link_libraries(test_title_picker PRIVATE spyro_runtime)
  add_test(NAME title_picker COMMAND test_title_picker)
  # The picker's panel geometry: the shares tile the surface, the selected panel ends up widest, the
  # width animation converges without overshooting, a picture is fitted to its panel instead of
  # stretched, and one or two panels lay out by the same rule as three.
  add_executable(test_picker_layout tests/test_picker_layout.cpp)
  target_include_directories(test_picker_layout PRIVATE game/host)
  target_link_libraries(test_picker_layout PRIVATE spyro_runtime)
  add_test(NAME picker_layout COMMAND test_picker_layout)
  add_executable(test_spyro3_runtime
    tests/test_spyro3_runtime.cpp)
  target_include_directories(test_spyro3_runtime PRIVATE game/core)
  target_link_libraries(test_spyro3_runtime PRIVATE spyro_runtime)
  add_test(NAME spyro3_runtime COMMAND test_spyro3_runtime)
  # The shared horizontal widening arithmetic and the per-title sites each image binds: the widened
  # centre, the widened clipping rectangle, the fact that neither ever narrows, and both titles'
  # measured leaves against their own bytes.
  add_executable(test_guest_widescreen
    tests/test_guest_widescreen.cpp)
  target_include_directories(test_guest_widescreen PRIVATE
    game/core titles/spyro2/core titles/spyro3/core)
  target_link_libraries(test_guest_widescreen PRIVATE spyro_runtime)
  add_test(NAME guest_widescreen COMMAND test_guest_widescreen)
  # The phase a Spyro 1 pad recording is keyed on. Gated for the two properties a recording's
  # meaning rests on: the same guest words give the same key (so presses are interchangeable within
  # a phase), and the framework's reserved "no phase" is unreachable (so a keyed file can never
  # replay as absolute-from-boot while claiming to be keyed).
  add_executable(test_spyro1_input_phase
    tests/test_spyro1_input_phase.cpp)
  target_include_directories(test_spyro1_input_phase PRIVATE game/core)
  target_link_libraries(test_spyro1_input_phase PRIVATE spyro_runtime)
  add_test(NAME spyro1_input_phase COMMAND test_spyro1_input_phase)
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
  # The Spyro 2/3 boot report. Its cases are pairs: a completed run and an aborted one (the aborted run's
  # totals must read NOT MEASURED, never 0), a matching CD count and a mismatching one, and the refusals.
  add_test(NAME boot_run_selftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/boot_run.py --selftest)
  # The per-title play facts and the route that uses them. Cases are pairs: a title that loads and plays
  # against one that never leaves its title, never shows one, skips loading, or whose position and
  # height words do not respond to the pad. A route that cannot refuse would report a loading screen as play.
  add_test(NAME title_profile_selftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/title_profile.py --selftest)
  add_test(NAME title_route_selftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/title_route.py --selftest)
  # A .pad replay runs with NO input driver and on a private card in the state the capture was made
  # against (docs/issues/0116). The selftest covers the card-state and environment claims here; the
  # run itself needs the disc, so it is a maintainer command (tools/pad_replay.py).
  add_test(NAME pad_replay_selftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/pad_replay.py --selftest)
  # Two runs of one binary on one set of inputs must be one run (docs/issues/0169). The comparison logic is
  # gated here without the disc, with the cases that matter as pairs: identical runs against runs that drift,
  # and a perturbed leg that diverges against one the digest cannot see. The live check is maintainer-run:
  # `uv run --frozen python tools/determinism_check.py --title spyro2|spyro3`.
  add_test(NAME determinism_check_selftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/determinism_check.py --selftest)
  add_test(NAME drive_environment_card
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_drive_environment.py)
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
  # The walker and the recorded-route follower against a fake product: a cutscene is not a wall, a
  # distance is Spyro's and not the trailing camera's, a jump keeps its direction, and a route
  # refuses in a level it was not recorded in.
  add_test(NAME repl_walk_unit
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_repl_walk.py)
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
  # The pad script, which is the ONLY way a driven run can press anything on a screen it did not
  # reach with a frame count: --press-while for everything up to GS_Playing, --press-after for the
  # presentation screens that only exist after it (the level-transition tally, the entrance sweep,
  # the return-home glide). Gated because the failure it has to not have is the quiet one: a
  # condition that silently never matches looks exactly like a screen the game never reached. So its
  # selftest covers a screen that IS up (one edge out of forty samples), a screen that is NOT
  # (scanned and matched nothing, reported as such with the sample count), a re-visit (still ONE edge
  # -- gamestate 0 is both the boot logo and the arrival state, and a live run that re-fired there
  # pressed Start in gameplay and opened the pause menu), the post-arrival refusals that keep the
  # second phase off those three states for the same measured reason, and every malformed spec
  # refused by name rather than left looking armed.
  add_test(NAME press_conditions_selftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/press_conditions.py --selftest)
  # The post-skip RAM comparison. Gated because its whole value is being able to say "these two runs
  # reached DIFFERENT guest state", and a comparison instrument that cannot report a difference is
  # indistinguishable from one that never looked. Its selftest therefore plants a difference in a
  # clock AND in a hand-off field and requires both to be reported and classified, compares a capture
  # against itself and requires every field equal, and refuses a missing and a truncated capture --
  # a short file would make every address read as zero and every field look equal.
  add_test(NAME ram_compare_selftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/ram_compare.py --selftest)
  # 0155 §7 M1 (payload) and M2 (terminal state) of the loading census, and the R6 XA-gate question.
  # Gated for the failure this class of instrument has produced nine times in this workspace: a
  # comparison that cannot report a difference. Its selftest plants every way M1 can be unequal --
  # one differing digest, one missing operation, one extra operation, and §7's own negative, the
  # archive truncated by one sector, where a missing payload must become an UNEQUAL operation rather
  # than a shorter table -- refuses a ledger report and an observer record set it cannot read (an
  # unreadable report would compare as zero operations on both sides and report M1 as passing), and
  # runs M2's field list through the SHARED ram_compare comparer, whose negative is the other half:
  # a capture pair differing in a clock AND in a hand-off field must report exactly those two.
  # The live legs need the disc, the full-console reference and a second core, so they are
  # maintainer-run: `uv run --frozen python tools/load_compare.py --bios ../SCPH1001.BIN`.
  add_test(NAME load_compare_selftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/load_compare.py --selftest)
  # The menu policy the two drivers share. Gated because a driver that presses the wrong button does
  # not fail -- it walks into another menu, or opens the pause menu during gameplay, and the run still
  # reaches GS_Playing and looks fine. Its selftest pins each decision and, more importantly, the
  # negatives: the destructive card prompts are never answered, the cutscene is untouched unless
  # --skip-transitions is on, a tally that has already ended is not a screen to cancel, the title
  # menu is not the flyby card even though both are gamestate 13, and the options submenu is backed
  # out with triangle because that is where a fixed Down-count ends up.
  add_test(NAME title_prompts_selftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/title_prompts.py --selftest)
  # The two probes that remain, and why they are the two. Each answer is only worth reading if the
  # instrument can be shown to produce the OTHER answer, which is what these selftests are for:
  #   probe_guest_disasm  the file-offset formula is verified against 62,183 decoded instructions, and
  #                       its selftest carries a byte-order discriminator that must separate the two
  #                       modes, since a big-endian slip would decode plausible nonsense.
  # The census probes that answered ONE offline question -- which blend-factor byte any level
  # authors, which material word any low-poly face carries -- were deleted with their answers already
  # recorded in docs/project-state.md. What survives them is not a probe but a reusable reader:
  # world_chunk_layout below reads the face layout out of the shipping codec instead of restating it,
  # and that reader is gated. A question asked once is an answer; a rule that will be asked again is
  # a gate.

  foreach(probe
      probe_guest_disasm
      #   world_chunk_layout  reads the low-poly face record layout out of the SHIPPING codec,
      #                       game/render/world/world_chunk_codec.cpp, instead of restating it. Its
      #                       selftest requires four perturbed COPIES of the codec to each change or
      #                       refuse the extracted layout, and requires the port's own `& 4u`
      #                       translucent selector to be read from world_lq_recipe.cpp rather than
      #                       taken on trust -- a second copy of a layout is a second unverified
      #                       claim about the same guest bytes, and it drifts exactly where a
      #                       re-port drifts. This is why it stays a gate and the census that read
      #                       through it does not: the reader is the reusable rule, the census was
      #                       one question asked once.
      world_chunk_layout
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
      probe_pause_panel_colour)
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

