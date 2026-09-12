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
  game/core/guest_execution.cpp
  game/core/native_angle.cpp
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
  game/render/actor_draw_recipe.cpp
  game/render/actor_face_submitter.cpp
  game/render/actor_global_order.cpp
  game/render/actor_ot_coalescer.cpp
  game/render/actor_model_codec.cpp
  game/render/actor_prefix_builder.cpp
  game/render/actor_recipe_capture.cpp
  game/render/actor_scene_builder.cpp
  game/render/actor_transform_math.cpp
  game/render/cutscene_scene_recipe.cpp
  game/render/cyclorama_mask_recipe.cpp
  game/render/cyclorama_mask_submitter.cpp
  game/render/cyclorama_portal_mesh_recipe.cpp
  game/render/guest_trig.cpp
  game/render/hud_text_builder.cpp
  game/render/level_transition_scene.cpp
  game/render/level_transition_tally_recipe.cpp
  game/render/cyclorama_portal_submitter.cpp
  game/render/cyclorama_scene_recipe.cpp
  game/render/field_collectables_recipe.cpp
  game/render/field_environment_recipe.cpp
  game/render/field_environment_scene.cpp
  game/render/field_moby_lists.cpp
  game/render/field_particle_type2_submitter.cpp
  game/render/field_particles_recipe.cpp
  game/render/actor_scene_oracle.cpp
  game/render/gpu_packet_decode.cpp
  game/render/field_scene_recipe.cpp
  game/render/field_shaded_queue_recipe.cpp
  game/render/field_shaded_queue_scene.cpp
  game/render/field_shaded_queue_submitter.cpp
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
  game/render/fx_field_shaded_queue.cpp
  game/render/fx_field_shadow.cpp
  game/render/fx_moby_shadow.cpp
  game/render/fx_field_tracers.cpp
  game/render/fx_paired_actor.cpp
  game/render/fx_screen_border.cpp
  game/render/fx_screen_fade.cpp
  game/render/fx_secondary_actor.cpp
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
  game/render/presentation_owner.cpp
  game/render/render_frame.cpp
  game/render/scene.cpp
  game/render/scene_painter_order.cpp
  game/render/screen_border_recipe.cpp
  game/render/screen_fade_recipe.cpp
  game/render/secondary_actor_recipe.cpp
  game/render/secondary_actor_scene.cpp
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
  game/render/world_scene_prepare.cpp
  game/render/world_scene_submitter.cpp
  game/render/world_source.cpp
  game/render/world_source_pair.cpp
  game/render/world_temporal.cpp
  titles/spyro1/core/spyro1_boot_sequence.cpp
  titles/spyro1/core/spyro1_field_scheduler.cpp
  titles/spyro1/core/spyro1_frame_driver.cpp
  titles/spyro1/core/spyro1_transition_skip.cpp
  titles/spyro1/core/spyro1_runtime.cpp
  titles/spyro2/core/spyro2_gpu_sync.cpp
  titles/spyro2/core/spyro2_runtime.cpp
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
  foreach(test_name IN ITEMS native_render_producers secondary_actor_recipe temporal_scene world_animation world_source_pair world_temporal world_scene_prepare world_hq_refinement field_shadow_recipe moby_shadow_recipe actor_shadow_staging spyro_flame_recipe spyro_flame_matrix glow_recipe sparkle_recipe dragon_scene_recipe dragon_burst_recipe actor_scale_translation field_particles_recipe actor_global_order field_scheduler transition_skip cyclorama_portal_mesh_recipe hud_text_builder level_transition_tally_recipe)
    add_executable(test_${test_name} tests/test_${test_name}.cpp)
    target_include_directories(test_${test_name} PRIVATE ${PSXPORT_DIR}/tests)
    target_link_libraries(test_${test_name} PRIVATE spyro_runtime)
    add_test(NAME ${test_name} COMMAND test_${test_name})
  endforeach()
  add_executable(test_runtime_run tests/test_runtime_run.cpp)
  target_link_libraries(test_runtime_run PRIVATE spyro_runtime)
  add_test(NAME runtime_run COMMAND test_runtime_run)
  add_executable(test_guest_execution tests/test_guest_execution.cpp)
  target_link_libraries(test_guest_execution PRIVATE spyro_runtime)
  add_test(NAME guest_execution COMMAND test_guest_execution)
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
endif()
