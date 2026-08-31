# cmake/spyro_port.cmake — build the native PC port binary `spyro_port` (SDL3 / SDL_GPU).
#
# This target is just the GAME: game/** + generated/** linking libpsxport.a. Every PSX-generic piece
# (the runtime substrate, the Beetle GTE/MDEC/SPU backends, the SDL_GPU renderer, the SBS harness)
# lives in the psxport framework library — see ${PSXPORT_DIR}/cmake/psxport.cmake (set in the root
# CMakeLists; the submodule by default).
#
#   cmake -S . -B build && cmake --build build --target spyro_port
#   ./scratch/bin/spyro_port scratch/bin/spyro/SCUS_942.28     # after run.sh extracted it

option(PSXPORT_BUILD_PORT "Build the Spyro native port binary (spyro_port)" ON)

# The framework static library + its psxport_smoke agnosticism proof. Always included so `psxport` is
# buildable even when the game target is off.
include(${PSXPORT_DIR}/cmake/psxport.cmake)

include(CTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_custom_target(format-check
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/format.py --check
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Checking first-party C++ formatting")

set(SPYRO_CPP_POLICY_COMMAND
  ${Python3_EXECUTABLE} ${PSXPORT_DIR}/tools/check_cpp_style.py
  --root ${CMAKE_SOURCE_DIR}
  --compile-commands ${CMAKE_BINARY_DIR}
  --cap game/core/actor_chain_oracle.cpp=2634
  --cap game/core/native_terrain.cpp=1347
  --cap game/render/fx_paired_actor.cpp=2590)

add_custom_target(cpp-policy
  COMMAND ${SPYRO_CPP_POLICY_COMMAND}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Checking Clang format, structure caps, and clang-tidy")

# Keeping executable identity selection in a small target lets the shipping path and its falsifiers
# exercise one exact implementation.
add_library(spyro_title_selection STATIC ${CMAKE_SOURCE_DIR}/game/core/title_selection.cpp)
target_include_directories(spyro_title_selection PUBLIC ${CMAKE_SOURCE_DIR}/game/core)
target_compile_features(spyro_title_selection PUBLIC cxx_std_20)
target_link_libraries(spyro_title_selection PUBLIC psxport OpenSSL::Crypto)

set(SPYRO_TITLE_CATALOG_DIR ${CMAKE_BINARY_DIR}/generated/spyro)
set(SPYRO_TITLE_CATALOG ${SPYRO_TITLE_CATALOG_DIR}/spyro_title_catalog.generated.h)
add_custom_command(
  OUTPUT ${SPYRO_TITLE_CATALOG}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/generate_title_catalog.py
          --output ${SPYRO_TITLE_CATALOG}
  DEPENDS
    ${CMAKE_SOURCE_DIR}/tools/generate_title_catalog.py
    ${CMAKE_SOURCE_DIR}/tools/title_identity.py
    ${CMAKE_SOURCE_DIR}/titles/spyro1/executable.json
    ${CMAKE_SOURCE_DIR}/titles/spyro2/executable.json
    ${CMAKE_SOURCE_DIR}/titles/spyro3/executable.json
  COMMENT "Generating serial-identified Spyro executable catalog")
add_custom_target(spyro_title_catalog DEPENDS ${SPYRO_TITLE_CATALOG})

if(BUILD_TESTING)
  add_executable(test_archive_transfer_contract
    ${CMAKE_SOURCE_DIR}/tests/test_archive_transfer_contract.cpp)
  target_include_directories(test_archive_transfer_contract PRIVATE ${CMAKE_SOURCE_DIR}/game/core)
  target_compile_features(test_archive_transfer_contract PRIVATE cxx_std_20)
  add_test(NAME archive_transfer_contract COMMAND test_archive_transfer_contract)
  add_executable(test_wide_clip_plan ${CMAKE_SOURCE_DIR}/tests/test_wide_clip_plan.cpp)
  target_include_directories(test_wide_clip_plan PRIVATE ${CMAKE_SOURCE_DIR}/game/core)
  target_compile_features(test_wide_clip_plan PRIVATE cxx_std_20)
  add_test(NAME wide_clip_plan COMMAND test_wide_clip_plan)
  add_executable(test_title_menu_recipe
    ${CMAKE_SOURCE_DIR}/tests/test_title_menu_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/title_menu_recipe.cpp)
  target_include_directories(test_title_menu_recipe PRIVATE ${CMAKE_SOURCE_DIR}/game/render)
  target_compile_features(test_title_menu_recipe PRIVATE cxx_std_20)
  add_test(NAME title_menu_recipe COMMAND test_title_menu_recipe)
  add_executable(test_paired_actor_decode
    ${CMAKE_SOURCE_DIR}/tests/test_paired_actor_decode.cpp
    ${CMAKE_SOURCE_DIR}/game/render/paired_actor_decode.cpp)
  target_include_directories(test_paired_actor_decode PRIVATE ${CMAKE_SOURCE_DIR}/game/render)
  target_compile_features(test_paired_actor_decode PRIVATE cxx_std_20)
  add_test(NAME paired_actor_decode COMMAND test_paired_actor_decode)
  add_executable(test_field_player_actor
    ${CMAKE_SOURCE_DIR}/tests/test_field_player_actor.cpp)
  target_include_directories(test_field_player_actor PRIVATE ${CMAKE_SOURCE_DIR}/game/render)
  target_compile_features(test_field_player_actor PRIVATE cxx_std_20)
  add_test(NAME field_player_actor COMMAND test_field_player_actor)
  add_executable(test_paired_actor_temporal_evidence
    ${CMAKE_SOURCE_DIR}/tests/test_paired_actor_temporal_evidence.cpp
    ${CMAKE_SOURCE_DIR}/game/render/paired_actor_temporal_evidence.cpp
    ${CMAKE_SOURCE_DIR}/game/core/spyro_context.cpp)
  target_include_directories(test_paired_actor_temporal_evidence PRIVATE
    ${CMAKE_SOURCE_DIR}/game/render ${CMAKE_SOURCE_DIR}/game/core)
  target_compile_features(test_paired_actor_temporal_evidence PRIVATE cxx_std_20)
  target_link_libraries(test_paired_actor_temporal_evidence PRIVATE psxport)
  add_test(NAME paired_actor_temporal_evidence COMMAND test_paired_actor_temporal_evidence)
  add_executable(test_actor_model_codec
    ${CMAKE_SOURCE_DIR}/tests/test_actor_model_codec.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_model_codec.cpp)
  target_include_directories(test_actor_model_codec PRIVATE ${CMAKE_SOURCE_DIR}/game/render)
  target_compile_features(test_actor_model_codec PRIVATE cxx_std_20)
  target_link_libraries(test_actor_model_codec PRIVATE psxport)
  add_test(NAME actor_model_codec COMMAND test_actor_model_codec)
  add_executable(test_actor_prefix_builder
    ${CMAKE_SOURCE_DIR}/tests/test_actor_prefix_builder.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_prefix_builder.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_model_codec.cpp)
  target_include_directories(test_actor_prefix_builder PRIVATE
    ${CMAKE_SOURCE_DIR}/game/render ${CMAKE_SOURCE_DIR}/game/core)
  target_compile_features(test_actor_prefix_builder PRIVATE cxx_std_20)
  target_link_libraries(test_actor_prefix_builder PRIVATE psxport)
  add_test(NAME actor_prefix_builder COMMAND test_actor_prefix_builder)
  add_executable(test_actor_draw_recipe
    ${CMAKE_SOURCE_DIR}/tests/test_actor_draw_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_draw_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_prefix_builder.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_model_codec.cpp)
  target_include_directories(test_actor_draw_recipe PRIVATE
    ${CMAKE_SOURCE_DIR}/game/render ${CMAKE_SOURCE_DIR}/game/core)
  target_compile_features(test_actor_draw_recipe PRIVATE cxx_std_20)
  target_link_libraries(test_actor_draw_recipe PRIVATE psxport)
  add_test(NAME actor_draw_recipe COMMAND test_actor_draw_recipe)
  add_executable(test_actor_global_order
    ${CMAKE_SOURCE_DIR}/tests/test_actor_global_order.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_global_order.cpp)
  target_include_directories(test_actor_global_order PRIVATE
    ${CMAKE_SOURCE_DIR}/game/render ${CMAKE_SOURCE_DIR}/game/core ${PSXPORT_DIR}/tests)
  target_compile_features(test_actor_global_order PRIVATE cxx_std_20)
  target_link_libraries(test_actor_global_order PRIVATE psxport)
  add_test(NAME actor_global_order COMMAND test_actor_global_order)
  add_executable(test_scene_painter_order
    ${CMAKE_SOURCE_DIR}/tests/test_scene_painter_order.cpp
    ${CMAKE_SOURCE_DIR}/game/render/scene_painter_order.cpp)
  target_include_directories(test_scene_painter_order PRIVATE
    ${CMAKE_SOURCE_DIR}/game/render ${PSXPORT_DIR}/tests)
  target_compile_features(test_scene_painter_order PRIVATE cxx_std_20)
  target_link_libraries(test_scene_painter_order PRIVATE psxport)
  add_test(NAME scene_painter_order COMMAND test_scene_painter_order)
  add_executable(test_field_shadow_recipe
    ${CMAKE_SOURCE_DIR}/tests/test_field_shadow_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/field_shadow_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_transform_math.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_projection_math.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_chunk_codec.cpp)
  target_include_directories(test_field_shadow_recipe PRIVATE
    ${CMAKE_SOURCE_DIR}/game/render ${CMAKE_SOURCE_DIR}/game/core ${PSXPORT_DIR}/tests)
  target_compile_features(test_field_shadow_recipe PRIVATE cxx_std_20)
  target_link_libraries(test_field_shadow_recipe PRIVATE psxport)
  add_test(NAME field_shadow_recipe COMMAND test_field_shadow_recipe)
  add_executable(test_stage13_scene_recipe
    ${CMAKE_SOURCE_DIR}/tests/test_stage13_scene_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/stage13_scene_recipe.cpp)
  target_include_directories(test_stage13_scene_recipe PRIVATE
    ${CMAKE_SOURCE_DIR}/game/render ${CMAKE_SOURCE_DIR}/game/core ${PSXPORT_DIR}/tests)
  target_compile_features(test_stage13_scene_recipe PRIVATE cxx_std_20)
  target_link_libraries(test_stage13_scene_recipe PRIVATE psxport)
  add_test(NAME stage13_scene_recipe COMMAND test_stage13_scene_recipe)
  add_executable(test_cutscene_scene_recipe
    ${CMAKE_SOURCE_DIR}/tests/test_cutscene_scene_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/cutscene_scene_recipe.cpp)
  target_include_directories(test_cutscene_scene_recipe PRIVATE
    ${CMAKE_SOURCE_DIR}/game/render ${CMAKE_SOURCE_DIR}/game/core ${PSXPORT_DIR}/tests)
  target_compile_features(test_cutscene_scene_recipe PRIVATE cxx_std_20)
  target_link_libraries(test_cutscene_scene_recipe PRIVATE psxport)
  add_test(NAME cutscene_scene_recipe COMMAND test_cutscene_scene_recipe)
  add_executable(test_screen_fade_recipe
    ${CMAKE_SOURCE_DIR}/tests/test_screen_fade_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/screen_fade_recipe.cpp)
  target_include_directories(test_screen_fade_recipe PRIVATE
    ${CMAKE_SOURCE_DIR}/game/render ${PSXPORT_DIR}/tests)
  target_compile_features(test_screen_fade_recipe PRIVATE cxx_std_20)
  target_link_libraries(test_screen_fade_recipe PRIVATE psxport)
  add_test(NAME screen_fade_recipe COMMAND test_screen_fade_recipe)
  add_executable(test_screen_border_recipe
    ${CMAKE_SOURCE_DIR}/tests/test_screen_border_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/screen_border_recipe.cpp)
  target_include_directories(test_screen_border_recipe PRIVATE
    ${CMAKE_SOURCE_DIR}/game/render ${PSXPORT_DIR}/tests)
  target_compile_features(test_screen_border_recipe PRIVATE cxx_std_20)
  target_link_libraries(test_screen_border_recipe PRIVATE psxport)
  add_test(NAME screen_border_recipe COMMAND test_screen_border_recipe)
  add_executable(test_field_environment_recipe
    ${CMAKE_SOURCE_DIR}/tests/test_field_environment_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/field_environment_recipe.cpp)
  target_include_directories(test_field_environment_recipe PRIVATE ${CMAKE_SOURCE_DIR}/game/render)
  target_compile_features(test_field_environment_recipe PRIVATE cxx_std_20)
  add_test(NAME field_environment_recipe COMMAND test_field_environment_recipe)
  add_executable(test_cyclorama_scene_recipe
    ${CMAKE_SOURCE_DIR}/tests/test_cyclorama_scene_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/cyclorama_scene_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/cyclorama_portal_mesh_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_model_codec.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_chunk_codec.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_projection_math.cpp)
  target_include_directories(test_cyclorama_scene_recipe PRIVATE
    ${CMAKE_SOURCE_DIR}/game/render ${CMAKE_SOURCE_DIR}/game/core ${PSXPORT_DIR}/tests)
  target_compile_features(test_cyclorama_scene_recipe PRIVATE cxx_std_20)
  target_link_libraries(test_cyclorama_scene_recipe PRIVATE psxport)
  add_test(NAME cyclorama_scene_recipe COMMAND test_cyclorama_scene_recipe)
  add_executable(test_cyclorama_portal_mesh_recipe
    ${CMAKE_SOURCE_DIR}/tests/test_cyclorama_portal_mesh_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/cyclorama_portal_mesh_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_model_codec.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_chunk_codec.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_projection_math.cpp)
  target_include_directories(test_cyclorama_portal_mesh_recipe PRIVATE
    ${CMAKE_SOURCE_DIR}/game/render ${CMAKE_SOURCE_DIR}/game/core ${PSXPORT_DIR}/tests)
  target_compile_features(test_cyclorama_portal_mesh_recipe PRIVATE cxx_std_20)
  target_link_libraries(test_cyclorama_portal_mesh_recipe PRIVATE psxport)
  add_test(NAME cyclorama_portal_mesh_recipe COMMAND test_cyclorama_portal_mesh_recipe)
  add_executable(test_cyclorama_portal_submitter
    ${CMAKE_SOURCE_DIR}/tests/test_cyclorama_portal_submitter.cpp
    ${CMAKE_SOURCE_DIR}/game/render/cyclorama_portal_submitter.cpp
    ${CMAKE_SOURCE_DIR}/game/render/scene_painter_order.cpp
    ${CMAKE_SOURCE_DIR}/game/render/painter_submission_preflight.cpp)
  target_include_directories(test_cyclorama_portal_submitter PRIVATE
    ${CMAKE_SOURCE_DIR}/game/render ${CMAKE_SOURCE_DIR}/game/core ${PSXPORT_DIR}/tests)
  target_compile_features(test_cyclorama_portal_submitter PRIVATE cxx_std_20)
  target_link_libraries(test_cyclorama_portal_submitter PRIVATE psxport)
  add_test(NAME cyclorama_portal_submitter COMMAND test_cyclorama_portal_submitter)
  add_executable(test_cyclorama_mask_recipe
    ${CMAKE_SOURCE_DIR}/tests/test_cyclorama_mask_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/cyclorama_mask_recipe.cpp)
  target_include_directories(test_cyclorama_mask_recipe PRIVATE
    ${CMAKE_SOURCE_DIR}/game/render ${CMAKE_SOURCE_DIR}/game/core ${PSXPORT_DIR}/tests)
  target_compile_features(test_cyclorama_mask_recipe PRIVATE cxx_std_20)
  target_link_libraries(test_cyclorama_mask_recipe PRIVATE psxport)
  add_test(NAME cyclorama_mask_recipe COMMAND test_cyclorama_mask_recipe)
  add_executable(test_cyclorama_mask_submitter
    ${CMAKE_SOURCE_DIR}/tests/test_cyclorama_mask_submitter.cpp
    ${CMAKE_SOURCE_DIR}/game/render/cyclorama_mask_submitter.cpp
    ${CMAKE_SOURCE_DIR}/game/render/scene_painter_order.cpp
    ${CMAKE_SOURCE_DIR}/game/render/painter_submission_preflight.cpp)
  target_include_directories(test_cyclorama_mask_submitter PRIVATE
    ${CMAKE_SOURCE_DIR}/game/render ${CMAKE_SOURCE_DIR}/game/core ${PSXPORT_DIR}/tests)
  target_compile_features(test_cyclorama_mask_submitter PRIVATE cxx_std_20)
  target_link_libraries(test_cyclorama_mask_submitter PRIVATE psxport)
  add_test(NAME cyclorama_mask_submitter COMMAND test_cyclorama_mask_submitter)
  add_executable(test_field_scene_recipe
    ${CMAKE_SOURCE_DIR}/tests/test_field_scene_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/field_scene_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/field_environment_recipe.cpp)
  target_include_directories(test_field_scene_recipe PRIVATE
    ${CMAKE_SOURCE_DIR}/game/render ${CMAKE_SOURCE_DIR}/game/core ${PSXPORT_DIR}/tests)
  target_compile_features(test_field_scene_recipe PRIVATE cxx_std_20)
  target_link_libraries(test_field_scene_recipe PRIVATE psxport)
  add_test(NAME field_scene_recipe COMMAND test_field_scene_recipe)
  add_executable(test_field_collectables_recipe
    ${CMAKE_SOURCE_DIR}/tests/test_field_collectables_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/field_collectables_recipe.cpp)
  target_include_directories(test_field_collectables_recipe PRIVATE
    ${CMAKE_SOURCE_DIR}/game/render ${PSXPORT_DIR}/tests)
  target_compile_features(test_field_collectables_recipe PRIVATE cxx_std_20)
  target_link_libraries(test_field_collectables_recipe PRIVATE psxport)
  add_test(NAME field_collectables_recipe COMMAND test_field_collectables_recipe)
  add_executable(test_field_particles_recipe
    ${CMAKE_SOURCE_DIR}/tests/test_field_particles_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/field_particles_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_chunk_codec.cpp)
  target_include_directories(test_field_particles_recipe PRIVATE ${CMAKE_SOURCE_DIR}/game/render)
  target_compile_features(test_field_particles_recipe PRIVATE cxx_std_20)
  add_test(NAME field_particles_recipe COMMAND test_field_particles_recipe)
  add_executable(test_field_tracers_recipe
    ${CMAKE_SOURCE_DIR}/tests/test_field_tracers_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/field_tracers_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_chunk_codec.cpp)
  target_include_directories(test_field_tracers_recipe PRIVATE ${CMAKE_SOURCE_DIR}/game/render)
  target_compile_features(test_field_tracers_recipe PRIVATE cxx_std_20)
  add_test(NAME field_tracers_recipe COMMAND test_field_tracers_recipe)
  add_executable(test_field_shaded_queue_recipe
    ${CMAKE_SOURCE_DIR}/tests/test_field_shaded_queue_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/field_shaded_queue_recipe.cpp)
  target_include_directories(test_field_shaded_queue_recipe PRIVATE
    ${CMAKE_SOURCE_DIR}/game/render ${CMAKE_SOURCE_DIR}/game/core ${PSXPORT_DIR}/tests)
  target_compile_features(test_field_shaded_queue_recipe PRIVATE cxx_std_20)
  target_link_libraries(test_field_shaded_queue_recipe PRIVATE psxport)
  add_test(NAME field_shaded_queue_recipe COMMAND test_field_shaded_queue_recipe)
  add_executable(test_field_shaded_queue_scene
    ${CMAKE_SOURCE_DIR}/tests/test_field_shaded_queue_scene.cpp
    ${CMAKE_SOURCE_DIR}/game/render/field_shaded_queue_scene.cpp
    ${CMAKE_SOURCE_DIR}/game/render/field_shaded_queue_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_transform_math.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_recipe_capture.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_draw_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_prefix_builder.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_model_codec.cpp)
  target_include_directories(test_field_shaded_queue_scene PRIVATE
    ${CMAKE_SOURCE_DIR}/game/render ${CMAKE_SOURCE_DIR}/game/core ${PSXPORT_DIR}/tests)
  target_compile_features(test_field_shaded_queue_scene PRIVATE cxx_std_20)
  target_link_libraries(test_field_shaded_queue_scene PRIVATE psxport)
  add_test(NAME field_shaded_queue_scene COMMAND test_field_shaded_queue_scene)
  add_executable(test_field_shaded_queue_submitter
    ${CMAKE_SOURCE_DIR}/tests/test_field_shaded_queue_submitter.cpp
    ${CMAKE_SOURCE_DIR}/game/render/field_shaded_queue_submitter.cpp
    ${CMAKE_SOURCE_DIR}/game/render/painter_submission_preflight.cpp
    ${CMAKE_SOURCE_DIR}/game/render/scene_painter_order.cpp)
  target_include_directories(test_field_shaded_queue_submitter PRIVATE
    ${CMAKE_SOURCE_DIR}/game/render ${CMAKE_SOURCE_DIR}/game/core ${PSXPORT_DIR}/tests)
  target_compile_features(test_field_shaded_queue_submitter PRIVATE cxx_std_20)
  target_link_libraries(test_field_shaded_queue_submitter PRIVATE psxport)
  add_test(NAME field_shaded_queue_submitter COMMAND test_field_shaded_queue_submitter)
  add_executable(test_field_environment_scene
    ${CMAKE_SOURCE_DIR}/tests/test_field_environment_scene.cpp
    ${CMAKE_SOURCE_DIR}/game/render/field_environment_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/field_environment_scene.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_scene_builder.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_animation.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_scene_prepare.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_lq_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_hq_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_hq_refinement.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_projection_math.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_chunk_codec.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_material_codec.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_model_codec.cpp)
  target_include_directories(test_field_environment_scene PRIVATE
    ${CMAKE_SOURCE_DIR}/game/render ${CMAKE_SOURCE_DIR}/game/core ${PSXPORT_DIR}/tests)
  target_compile_features(test_field_environment_scene PRIVATE cxx_std_20)
  target_link_libraries(test_field_environment_scene PRIVATE psxport)
  add_test(NAME field_environment_scene COMMAND test_field_environment_scene)
  add_executable(test_world_scene_submitter
    ${CMAKE_SOURCE_DIR}/tests/test_world_scene_submitter.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_scene_submitter.cpp
    ${CMAKE_SOURCE_DIR}/game/render/painter_submission_preflight.cpp
    ${CMAKE_SOURCE_DIR}/game/render/scene_painter_order.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_recipe.cpp)
  target_include_directories(test_world_scene_submitter PRIVATE
    ${CMAKE_SOURCE_DIR}/game/render ${CMAKE_SOURCE_DIR}/game/core ${PSXPORT_DIR}/tests)
  target_compile_features(test_world_scene_submitter PRIVATE cxx_std_20)
  target_link_libraries(test_world_scene_submitter PRIVATE psxport)
  add_test(NAME world_scene_submitter COMMAND test_world_scene_submitter)
  add_executable(test_secondary_actor_recipe
    ${CMAKE_SOURCE_DIR}/tests/test_secondary_actor_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/secondary_actor_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_draw_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_prefix_builder.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_model_codec.cpp)
  target_include_directories(test_secondary_actor_recipe PRIVATE
    ${CMAKE_SOURCE_DIR}/game/render ${CMAKE_SOURCE_DIR}/game/core ${PSXPORT_DIR}/tests)
  target_compile_features(test_secondary_actor_recipe PRIVATE cxx_std_20)
  target_link_libraries(test_secondary_actor_recipe PRIVATE psxport)
  add_test(NAME secondary_actor_recipe COMMAND test_secondary_actor_recipe)
  add_executable(test_secondary_actor_scene
    ${CMAKE_SOURCE_DIR}/tests/test_secondary_actor_scene.cpp
    ${CMAKE_SOURCE_DIR}/game/render/secondary_actor_scene.cpp
    ${CMAKE_SOURCE_DIR}/game/render/secondary_actor_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_scene_builder.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_transform_math.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_recipe_capture.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_draw_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_prefix_builder.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_model_codec.cpp)
  target_include_directories(test_secondary_actor_scene PRIVATE
    ${CMAKE_SOURCE_DIR}/game/render ${CMAKE_SOURCE_DIR}/game/core ${PSXPORT_DIR}/tests)
  target_compile_features(test_secondary_actor_scene PRIVATE cxx_std_20)
  target_link_libraries(test_secondary_actor_scene PRIVATE psxport)
  add_test(NAME secondary_actor_scene COMMAND test_secondary_actor_scene)
  add_executable(test_actor_face_submitter
    ${CMAKE_SOURCE_DIR}/tests/test_actor_face_submitter.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_face_submitter.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_global_order.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_draw_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_prefix_builder.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_model_codec.cpp
    ${CMAKE_SOURCE_DIR}/game/render/painter_submission_preflight.cpp
    ${CMAKE_SOURCE_DIR}/game/render/scene_painter_order.cpp)
  target_include_directories(test_actor_face_submitter PRIVATE
    ${CMAKE_SOURCE_DIR}/game/render ${CMAKE_SOURCE_DIR}/game/core ${PSXPORT_DIR}/tests)
  target_compile_features(test_actor_face_submitter PRIVATE cxx_std_20)
  target_link_libraries(test_actor_face_submitter PRIVATE psxport)
  add_test(NAME actor_face_submitter COMMAND test_actor_face_submitter)
  add_executable(test_actor_mesh_scratch
    ${CMAKE_SOURCE_DIR}/tests/test_actor_mesh_scratch.cpp)
  target_include_directories(test_actor_mesh_scratch PRIVATE ${CMAKE_SOURCE_DIR}/game/core)
  target_compile_features(test_actor_mesh_scratch PRIVATE cxx_std_20)
  add_test(NAME actor_mesh_scratch COMMAND test_actor_mesh_scratch)
  add_executable(test_spu_pio_upload
    ${CMAKE_SOURCE_DIR}/tests/test_spu_pio_upload.cpp)
  target_include_directories(test_spu_pio_upload PRIVATE ${CMAKE_SOURCE_DIR}/game/core)
  target_compile_features(test_spu_pio_upload PRIVATE cxx_std_20)
  add_test(NAME spu_pio_upload COMMAND test_spu_pio_upload)
  add_executable(test_spu_hardware_init
    ${CMAKE_SOURCE_DIR}/tests/test_spu_hardware_init.cpp)
  target_include_directories(test_spu_hardware_init PRIVATE ${CMAKE_SOURCE_DIR}/game/core)
  target_compile_features(test_spu_hardware_init PRIVATE cxx_std_20)
  add_test(NAME spu_hardware_init COMMAND test_spu_hardware_init)
  add_executable(test_text_sprites
    ${CMAKE_SOURCE_DIR}/tests/test_text_sprites.cpp)
  target_include_directories(test_text_sprites PRIVATE ${CMAKE_SOURCE_DIR}/game/core)
  target_compile_features(test_text_sprites PRIVATE cxx_std_20)
  add_test(NAME text_sprites COMMAND test_text_sprites)
  add_executable(test_memcard_event_stack
    ${CMAKE_SOURCE_DIR}/tests/test_memcard_event_stack.cpp)
  target_include_directories(test_memcard_event_stack PRIVATE ${CMAKE_SOURCE_DIR}/game/core)
  target_compile_features(test_memcard_event_stack PRIVATE cxx_std_20)
  add_test(NAME memcard_event_stack COMMAND test_memcard_event_stack)
  add_executable(test_world_recipe
    ${CMAKE_SOURCE_DIR}/tests/test_world_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_recipe.cpp)
  target_include_directories(test_world_recipe PRIVATE ${CMAKE_SOURCE_DIR}/game/render)
  target_compile_features(test_world_recipe PRIVATE cxx_std_20)
  add_test(NAME world_recipe COMMAND test_world_recipe)
  add_executable(test_world_chunk_codec
    ${CMAKE_SOURCE_DIR}/tests/test_world_chunk_codec.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_chunk_codec.cpp)
  target_include_directories(test_world_chunk_codec PRIVATE ${CMAKE_SOURCE_DIR}/game/render)
  target_compile_features(test_world_chunk_codec PRIVATE cxx_std_20)
  add_test(NAME world_chunk_codec COMMAND test_world_chunk_codec)
  add_executable(test_world_material_codec
    ${CMAKE_SOURCE_DIR}/tests/test_world_material_codec.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_material_codec.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_model_codec.cpp)
  target_include_directories(test_world_material_codec PRIVATE
    ${CMAKE_SOURCE_DIR}/game/render ${CMAKE_SOURCE_DIR}/game/core)
  target_compile_features(test_world_material_codec PRIVATE cxx_std_20)
  target_link_libraries(test_world_material_codec PRIVATE psxport)
  add_test(NAME world_material_codec COMMAND test_world_material_codec)
  add_executable(test_world_animation
    ${CMAKE_SOURCE_DIR}/tests/test_world_animation.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_animation.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_chunk_codec.cpp)
  target_include_directories(test_world_animation PRIVATE ${CMAKE_SOURCE_DIR}/game/render)
  target_compile_features(test_world_animation PRIVATE cxx_std_20)
  target_link_libraries(test_world_animation PRIVATE psxport)
  add_test(NAME world_animation COMMAND test_world_animation)
  add_executable(test_world_scene_prepare
    ${CMAKE_SOURCE_DIR}/tests/test_world_scene_prepare.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_animation.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_scene_prepare.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_projection_math.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_chunk_codec.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_recipe.cpp)
  target_include_directories(test_world_scene_prepare PRIVATE ${CMAKE_SOURCE_DIR}/game/render)
  target_compile_features(test_world_scene_prepare PRIVATE cxx_std_20)
  target_link_libraries(test_world_scene_prepare PRIVATE psxport)
  add_test(NAME world_scene_prepare COMMAND test_world_scene_prepare)
  add_executable(test_world_hq_refinement
    ${CMAKE_SOURCE_DIR}/tests/test_world_hq_refinement.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_hq_refinement.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_projection_math.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_material_codec.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_chunk_codec.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_recipe.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_model_codec.cpp)
  target_include_directories(test_world_hq_refinement PRIVATE
    ${CMAKE_SOURCE_DIR}/game/render ${CMAKE_SOURCE_DIR}/game/core)
  target_compile_features(test_world_hq_refinement PRIVATE cxx_std_20)
  target_link_libraries(test_world_hq_refinement PRIVATE psxport)
  add_test(NAME world_hq_refinement COMMAND test_world_hq_refinement)
  add_executable(test_world_scene_oracle
    ${CMAKE_SOURCE_DIR}/tests/test_world_scene_oracle.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_scene_oracle.cpp
    ${CMAKE_SOURCE_DIR}/game/render/world_recipe.cpp)
  target_include_directories(test_world_scene_oracle PRIVATE ${CMAKE_SOURCE_DIR}/game/render)
  target_compile_features(test_world_scene_oracle PRIVATE cxx_std_20)
  add_test(NAME world_scene_oracle COMMAND test_world_scene_oracle)
  add_test(
    NAME format_check
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/format.py --check)
  add_test(
    NAME format_tool_selftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/format.py --selftest)
  add_test(
    NAME re_frontier_tool_selftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/re_frontier.py --selftest)
  add_test(
    NAME launcher
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_launcher.py)
  add_test(
    NAME runtime_structure
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_runtime_structure.py)
  add_test(
    NAME title_provision
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_provision_titles.py)
  add_executable(test_spyro2_runtime
    ${CMAKE_SOURCE_DIR}/tests/test_spyro2_runtime.cpp
    ${CMAKE_SOURCE_DIR}/game/core/spyro_runtime.cpp
    ${CMAKE_SOURCE_DIR}/titles/spyro2/core/spyro2_runtime.cpp
    ${CMAKE_SOURCE_DIR}/titles/spyro2/core/spyro2_frame_driver.cpp
    ${CMAKE_SOURCE_DIR}/titles/spyro2/core/spyro2_display_bootstrap.cpp
    ${CMAKE_SOURCE_DIR}/titles/spyro2/core/spyro2_gpu_sync.cpp)
  target_include_directories(test_spyro2_runtime PRIVATE
    ${CMAKE_SOURCE_DIR}/game/core ${CMAKE_SOURCE_DIR}/titles/spyro2/core)
  target_compile_features(test_spyro2_runtime PRIVATE cxx_std_20)
  target_link_libraries(test_spyro2_runtime PRIVATE psxport)
  add_test(NAME spyro2_runtime COMMAND test_spyro2_runtime)
  add_executable(test_spyro2_gpu_sync
    ${CMAKE_SOURCE_DIR}/tests/test_spyro2_gpu_sync.cpp
    ${CMAKE_SOURCE_DIR}/titles/spyro2/core/spyro2_gpu_sync.cpp)
  target_include_directories(test_spyro2_gpu_sync PRIVATE
    ${CMAKE_SOURCE_DIR}/titles/spyro2/core)
  target_compile_features(test_spyro2_gpu_sync PRIVATE cxx_std_20)
  target_link_libraries(test_spyro2_gpu_sync PRIVATE psxport)
  add_test(NAME spyro2_gpu_sync COMMAND test_spyro2_gpu_sync)
  add_executable(test_spyro2_loaded_bootstrap_timing
    ${CMAKE_SOURCE_DIR}/titles/spyro2/tests/test_spyro2_loaded_bootstrap_timing.cpp)
  target_include_directories(test_spyro2_loaded_bootstrap_timing PRIVATE
    ${CMAKE_SOURCE_DIR}/titles/spyro2/core)
  target_compile_features(test_spyro2_loaded_bootstrap_timing PRIVATE cxx_std_20)
  add_test(NAME spyro2_loaded_bootstrap_timing COMMAND test_spyro2_loaded_bootstrap_timing)
  add_executable(test_spyro3_runtime
    ${CMAKE_SOURCE_DIR}/tests/test_spyro3_runtime.cpp
    ${CMAKE_SOURCE_DIR}/game/core/spyro_runtime.cpp
    ${CMAKE_SOURCE_DIR}/titles/spyro3/core/spyro3_runtime.cpp)
  target_include_directories(test_spyro3_runtime PRIVATE
    ${CMAKE_SOURCE_DIR}/game/core ${CMAKE_SOURCE_DIR}/titles/spyro3/core)
  target_compile_features(test_spyro3_runtime PRIVATE cxx_std_20)
  target_link_libraries(test_spyro3_runtime PRIVATE psxport)
  add_test(NAME spyro3_runtime COMMAND test_spyro3_runtime)
  add_executable(test_title_selection ${CMAKE_SOURCE_DIR}/tests/test_title_selection.cpp)
  target_link_libraries(test_title_selection PRIVATE spyro_title_selection)
  add_test(NAME title_selection COMMAND test_title_selection)
  add_executable(test_presentation_owner ${CMAKE_SOURCE_DIR}/tests/test_presentation_owner.cpp)
  target_include_directories(test_presentation_owner PRIVATE ${CMAKE_SOURCE_DIR}/game/render)
  target_compile_features(test_presentation_owner PRIVATE cxx_std_20)
  add_test(NAME presentation_owner COMMAND test_presentation_owner)
  add_executable(test_spyro1_frame_policy
    ${CMAKE_SOURCE_DIR}/tests/test_spyro1_frame_policy.cpp)
  target_include_directories(test_spyro1_frame_policy PRIVATE
    ${CMAKE_SOURCE_DIR}/titles/spyro1/core)
  target_compile_features(test_spyro1_frame_policy PRIVATE cxx_std_20)
  add_test(NAME spyro1_frame_policy COMMAND test_spyro1_frame_policy)
  add_executable(test_spyro1_vblank_irq
    ${CMAKE_SOURCE_DIR}/tests/test_spyro1_vblank_irq.cpp)
  target_include_directories(test_spyro1_vblank_irq PRIVATE
    ${CMAKE_SOURCE_DIR}/titles/spyro1/core)
  target_compile_features(test_spyro1_vblank_irq PRIVATE cxx_std_20)
  add_test(NAME spyro1_vblank_irq COMMAND test_spyro1_vblank_irq)
  add_executable(test_spyro_gate_debug
    ${CMAKE_SOURCE_DIR}/tests/test_spyro_gate_debug.cpp
    ${CMAKE_SOURCE_DIR}/game/core/spyro_gate_debug.cpp)
  target_include_directories(test_spyro_gate_debug PRIVATE ${CMAKE_SOURCE_DIR}/game/core)
  target_compile_features(test_spyro_gate_debug PRIVATE cxx_std_20)
  target_link_libraries(test_spyro_gate_debug PRIVATE psxport)
  add_test(NAME spyro_gate_debug COMMAND test_spyro_gate_debug)
  add_executable(test_spyro1_transition_skip
    ${CMAKE_SOURCE_DIR}/tests/test_spyro1_transition_skip.cpp
    ${CMAKE_SOURCE_DIR}/titles/spyro1/core/spyro1_transition_skip.cpp)
  target_include_directories(test_spyro1_transition_skip PRIVATE
    ${CMAKE_SOURCE_DIR}/titles/spyro1/core)
  target_compile_features(test_spyro1_transition_skip PRIVATE cxx_std_20)
  target_link_libraries(test_spyro1_transition_skip PRIVATE psxport)
  add_test(NAME spyro1_transition_skip COMMAND test_spyro1_transition_skip)
  add_executable(test_native_gameplay
    ${CMAKE_SOURCE_DIR}/tests/test_native_gameplay.cpp)
  target_include_directories(test_native_gameplay PRIVATE
    ${CMAKE_SOURCE_DIR}/game/core)
  target_compile_features(test_native_gameplay PRIVATE cxx_std_20)
  add_test(NAME native_gameplay COMMAND test_native_gameplay)
  add_test(
    NAME computed_jumps_selftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/computed_jumps.py --selftest)
  add_test(NAME cpp_policy COMMAND ${SPYRO_CPP_POLICY_COMMAND})
endif()

# Spyro 2 has a title-local substrate and product binary. Keeping it in a separate link prevents
# SCUS_944.25's generated `func_*` namespace from colliding with Spyro 1's different bodies at the
# same guest addresses. The launcher emits this ignored manifest before configuring the selected
# title; a generic Spyro 1 build therefore has no hidden dependency on Spyro 2's restricted bytes.
set(SPYRO2_REC_DIR ${CMAKE_SOURCE_DIR}/scratch/generated/spyro2)
set(SPYRO2_REC_MANIFEST ${SPYRO2_REC_DIR}/rec_sources.cmake)
if(PSXPORT_BUILD_PORT AND EXISTS ${SPYRO2_REC_MANIFEST})
  include(${SPYRO2_REC_MANIFEST})
  set(SPYRO2_REC_SRCS ${GEN_REC_SRCS})
  list(TRANSFORM SPYRO2_REC_SRCS PREPEND ${SPYRO2_REC_DIR}/)
  set_source_files_properties(${SPYRO2_REC_SRCS}
    PROPERTIES LANGUAGE CXX
    COMPILE_OPTIONS "-w;-O1;-foptimize-sibling-calls;-fno-strict-aliasing;-fwrapv")

  add_executable(spyro2_port
    ${CMAKE_SOURCE_DIR}/titles/spyro2/core/main.cpp
    ${CMAKE_SOURCE_DIR}/titles/spyro2/core/spyro2_runtime.cpp
    ${CMAKE_SOURCE_DIR}/titles/spyro2/core/spyro2_frame_driver.cpp
    ${CMAKE_SOURCE_DIR}/titles/spyro2/core/spyro2_display_bootstrap.cpp
    ${CMAKE_SOURCE_DIR}/titles/spyro2/core/spyro2_gpu_sync.cpp
    ${CMAKE_SOURCE_DIR}/titles/spyro2/core/spyro2_recomp_register.cpp
    ${CMAKE_SOURCE_DIR}/game/core/spyro_runtime.cpp
    ${SPYRO2_REC_SRCS})
  add_dependencies(spyro2_port gen_gpu_shaders spyro_title_catalog)
  target_include_directories(spyro2_port PRIVATE
    ${CMAKE_SOURCE_DIR}/game/core
    ${CMAKE_SOURCE_DIR}/titles/spyro2/core
    ${SPYRO2_REC_DIR}
    ${SPYRO_TITLE_CATALOG_DIR})
  target_compile_features(spyro2_port PRIVATE cxx_std_20)
  target_compile_options(spyro2_port PRIVATE -O2 -g
    ${SDL3_CFLAGS_OTHER} ${FREETYPE_CFLAGS_OTHER})
  target_link_libraries(spyro2_port PRIVATE spyro_title_selection psxport)
  set_target_properties(spyro2_port PROPERTIES
    ENABLE_EXPORTS ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/scratch/bin)

  if(BUILD_TESTING)
    add_test(NAME spyro2_executable_help COMMAND $<TARGET_FILE:spyro2_port> --help)
    set_tests_properties(spyro2_executable_help PROPERTIES
      PASS_REGULAR_EXPRESSION "Usage: .*spyro2_port")
  endif()
elseif(PSXPORT_BUILD_PORT)
  message(STATUS "Spyro 2 substrate absent: run tools/ensure_spyro2_substrate.py before building spyro2_port")
endif()

if(NOT PSXPORT_BUILD_PORT)
  return()
endif()

# ---- game sources ------------------------------------------------------------------------------
# Phase 0 is deliberately thin: the seam (config/hooks/recomp registry) plus main(). Everything else
# still runs as recompiled substrate. Native reimplementations get added here as they are RE'd and
# byte-gated — see docs/re-frontier.md.
set(GAME_SRC
  game/core/boot_skip.cpp
  game/core/main.cpp
  game/core/game_config.cpp
  game/core/game_hooks.cpp
  game/core/spyro_gate_debug.cpp
  game/core/spyro_context.cpp
  game/core/spyro_runtime.cpp
  game/core/title_runtime_registry.cpp
  titles/spyro2/core/spyro2_runtime.cpp
  titles/spyro2/core/spyro2_frame_driver.cpp
  titles/spyro2/core/spyro2_display_bootstrap.cpp
  titles/spyro2/core/spyro2_gpu_sync.cpp
  titles/spyro3/core/spyro3_runtime.cpp
  titles/spyro1/core/spyro1_runtime.cpp
  titles/spyro1/core/spyro1_frame_driver.cpp
  titles/spyro1/core/spyro1_field_scheduler.cpp
  titles/spyro1/core/spyro1_transition_skip.cpp
  titles/spyro1/core/spyro1_boot_sequence.cpp
  game/core/recomp_register.cpp
  game/core/vsync.cpp
  game/core/producer_run.cpp
  game/render/render_frame.cpp
  game/render/scene.cpp
  game/render/frame_env.cpp
  game/render/fx_title_menu.cpp
  game/render/title_menu_recipe.cpp
  game/render/title_menu_state.cpp
  game/render/title_menu_oracle.cpp
  game/render/fx_sprite_queue.cpp
  game/render/paired_actor_decode.cpp
  game/render/presentation_owner.cpp
  game/render/actor_model_codec.cpp
  game/render/actor_prefix_builder.cpp
  game/render/actor_draw_recipe.cpp
  game/render/actor_face_submitter.cpp
  game/render/actor_global_order.cpp
  game/render/scene_painter_order.cpp
  game/render/stage13_scene_recipe.cpp
  game/render/cutscene_scene_recipe.cpp
  game/render/screen_fade_recipe.cpp
  game/render/fx_screen_fade.cpp
  game/render/screen_border_recipe.cpp
  game/render/fx_screen_border.cpp
  game/render/field_environment_recipe.cpp
  game/render/field_environment_oracle.cpp
  game/render/field_environment_scene.cpp
  game/render/fx_field_environment.cpp
  game/render/cyclorama_scene_recipe.cpp
  game/render/cyclorama_portal_mesh_recipe.cpp
  game/render/cyclorama_mask_recipe.cpp
  game/render/cyclorama_mask_submitter.cpp
  game/render/cyclorama_portal_submitter.cpp
  game/render/fx_field_cyclorama.cpp
  game/render/field_scene_recipe.cpp
  game/render/field_moby_lists.cpp
  game/render/field_collectables_recipe.cpp
  game/render/fx_field_collectables.cpp
  game/render/field_particles_recipe.cpp
  game/render/field_particle_type2_submitter.cpp
  game/render/particle_sine_table.h
  game/render/fx_field_particles.cpp
  game/render/fx_field_actor_composition.cpp
  game/render/fx_field_player_actor.cpp
  game/render/field_shadow_recipe.cpp
  game/render/field_shadow_submitter.cpp
  game/render/fx_field_shadow.cpp
  game/render/field_tracers_recipe.cpp
  game/render/fx_field_tracers.cpp
  game/render/shadow_oracle.cpp
  game/render/field_shaded_queue_recipe.cpp
  game/render/field_shaded_queue_scene.cpp
  game/render/field_shaded_queue_submitter.cpp
  game/render/fx_field_shaded_queue.cpp
  game/render/secondary_actor_scene.cpp
  game/render/secondary_actor_recipe.cpp
  game/render/fx_secondary_actor.cpp
  game/render/actor_recipe_capture.cpp
  game/render/actor_scene_builder.cpp
  game/render/actor_transform_math.cpp
  game/render/actor_scene_oracle.cpp
  game/render/painter_submission_preflight.cpp
  game/render/fx_actor_draw.cpp
  game/render/world_recipe.cpp
  game/render/world_chunk_codec.cpp
  game/render/world_material_codec.cpp
  game/render/world_projection_math.cpp
  game/core/world_animation_oracle.cpp
  game/render/world_animation.cpp
  game/render/world_scene_prepare.cpp
  game/render/world_lq_recipe.cpp
  game/render/world_hq_recipe.cpp
  game/render/world_hq_refinement.cpp
  game/render/world_scene_builder.cpp
  game/render/world_scene_submitter.cpp
  game/render/world_scene_oracle.cpp
  game/render/world_scene_capture.cpp
  game/render/fx_world_draw.cpp
  game/render/paired_actor_temporal_evidence.cpp
  game/render/fx_paired_actor.cpp
  game/core/cd_queue.cpp
  game/core/native_rand.cpp
  game/core/native_leaf.cpp
  game/core/native_vec.cpp
  game/core/native_gte.cpp
  game/core/native_angle.cpp
  game/core/native_util.cpp
  game/core/native_printf.cpp
  game/core/native_actor_mesh_scratch.cpp
  game/core/native_spu_pio_upload.cpp
  game/core/native_spu_hardware_init.cpp
  game/core/native_text_sprites.cpp
  game/core/native_memcard_event_stack.cpp
  game/core/native_memcard_operations.cpp
  game/core/native_gameplay.cpp
  game/core/native_render.cpp
  game/core/wide_clip.cpp
  game/core/native_terrain.cpp
  game/core/native_world.cpp
  game/core/actor_chain_oracle.cpp
)

# ---- the recompiled substrate --------------------------------------------------------------------
# emit.py writes the exact TU list to generated/rec_sources.cmake (GEN_REC_SRCS, basenames), so the
# set is deterministic — no globbing, which would wrongly pull unlinked stub TUs.
#
# -foptimize-sibling-calls IS REQUIRED, NOT an optimization nicety: a guest TAIL JUMP (a computed `jr`
# routed to rec_dispatch, or a `j`/branch to a framed sibling) is emitted as `dispatch(c,x); return;`
# in tail position, and the guest uses such tail jumps for LOOPS that iterate indefinitely. Without
# sibling-call optimization each iteration becomes a real C call, the stack grows per loop, and the
# process SIGSEGVs. -O1 plus this flag keeps the whole tail chain collapsing to a jump (O(1) stack).
include(${CMAKE_SOURCE_DIR}/generated/rec_sources.cmake)
list(TRANSFORM GEN_REC_SRCS PREPEND generated/)
set_source_files_properties(${GEN_REC_SRCS}
  PROPERTIES LANGUAGE CXX
  COMPILE_OPTIONS "-O1;-foptimize-sibling-calls;-fno-strict-aliasing;-fwrapv")

add_executable(spyro_port ${GAME_SRC} ${GEN_REC_SRCS})

# The framework's SDL_GPU shader header is produced by a psxport custom target; gpu_vk.cpp (inside
# libpsxport) needs it present before this target's link ordering.
add_dependencies(spyro_port gen_gpu_shaders)

set_target_properties(spyro_port PROPERTIES
  CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
  ENABLE_EXPORTS ON                                    # -rdynamic: watchdog backtrace symbol names
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/scratch/bin)

# Only game/* include dirs here — the framework's (runtime, generated, vendored backends, SDL,
# freetype) are inherited PUBLICly from the psxport link below.
target_include_directories(spyro_port PRIVATE
  game game/core game/render titles/spyro1/core titles/spyro2/core titles/spyro3/core
  ${SPYRO_TITLE_CATALOG_DIR})
add_dependencies(spyro_port spyro_title_catalog)

target_compile_options(spyro_port PRIVATE -w -O2 -g
  ${SDL3_CFLAGS_OTHER} ${FREETYPE_CFLAGS_OTHER})

target_link_libraries(spyro_port PRIVATE spyro_title_selection psxport)

if(BUILD_TESTING)
  add_test(NAME executable_help_short COMMAND $<TARGET_FILE:spyro_port> -h)
  add_test(NAME executable_help_long COMMAND $<TARGET_FILE:spyro_port> --help)
  set_tests_properties(executable_help_short executable_help_long PROPERTIES
    PASS_REGULAR_EXPRESSION "Usage: .*spyro_port \\[executable\\]")
endif()
