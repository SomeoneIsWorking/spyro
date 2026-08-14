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

if(BUILD_TESTING)
  add_executable(test_paired_actor_decode
    ${CMAKE_SOURCE_DIR}/tests/test_paired_actor_decode.cpp
    ${CMAKE_SOURCE_DIR}/game/render/paired_actor_decode.cpp)
  target_include_directories(test_paired_actor_decode PRIVATE ${CMAKE_SOURCE_DIR}/game/render)
  target_compile_features(test_paired_actor_decode PRIVATE cxx_std_20)
  add_test(NAME paired_actor_decode COMMAND test_paired_actor_decode)
  add_executable(test_actor_model_codec
    ${CMAKE_SOURCE_DIR}/tests/test_actor_model_codec.cpp
    ${CMAKE_SOURCE_DIR}/game/render/actor_model_codec.cpp)
  target_include_directories(test_actor_model_codec PRIVATE ${CMAKE_SOURCE_DIR}/game/render)
  target_compile_features(test_actor_model_codec PRIVATE cxx_std_20)
  target_link_libraries(test_actor_model_codec PRIVATE psxport)
  add_test(NAME actor_model_codec COMMAND test_actor_model_codec)
  add_test(
    NAME format_check
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/format.py --check)
  add_test(
    NAME format_tool_selftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/format.py --selftest)
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
  game/core/recomp_register.cpp
  game/core/vsync.cpp
  game/core/frame_loop.cpp
  game/core/producer_run.cpp
  game/render/render_frame.cpp
  game/render/scene.cpp
  game/render/frame_env.cpp
  game/render/fx_title_menu.cpp
  game/render/fx_sprite_queue.cpp
  game/render/paired_actor_decode.cpp
  game/render/actor_model_codec.cpp
  game/render/fx_paired_actor.cpp
  game/core/cd_queue.cpp
  game/core/native_rand.cpp
  game/core/native_leaf.cpp
  game/core/native_vec.cpp
  game/core/native_gte.cpp
  game/core/native_angle.cpp
  game/core/native_util.cpp
  game/core/native_render.cpp
  game/core/wide_clip.cpp
  game/core/native_terrain.cpp
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
target_include_directories(spyro_port PRIVATE game game/core game/render)

target_compile_options(spyro_port PRIVATE -w -O2 -g
  ${SDL3_CFLAGS_OTHER} ${FREETYPE_CFLAGS_OTHER})

target_link_libraries(spyro_port PRIVATE psxport)
