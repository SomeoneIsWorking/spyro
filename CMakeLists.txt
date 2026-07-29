# SpyroEngine — the Spyro the Dragon PC-native port, built on the psxport framework
# (external/psxport submodule).
#
# The framework (PSX-generic static-recomp substrate + runtime + HW backends + SBS harness + SDL_GPU
# renderer) is the reusable `psxport` library; this repo adds the game (game/**) and the recompiled
# SCUS_942.28 substrate (generated/**, produced by run.sh / tools/ensure_recomp.py) and links it.
cmake_minimum_required(VERSION 3.21)

# ccache, when it is available. The recompiled substrate is ~200 MB of generated C across ~47
# translation units, and compiling it takes 10-15 minutes while a no-op build links in ~3 seconds —
# so compilation is essentially the whole cost of an iteration here. The pattern that makes this pay
# is the normal one: change the emitter, measure, change it again, and land back on bytes that were
# already compiled once. Must be set BEFORE project(), which is when CMake probes the compiler.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_C_COMPILER_LAUNCHER   "${CCACHE_PROGRAM}")
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  message(STATUS "ccache: ${CCACHE_PROGRAM}")
endif()

project(spyroengine CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(ZLIB REQUIRED)
find_package(zstd CONFIG QUIET)
if(NOT TARGET zstd::libzstd_shared)
  find_library(ZSTD_LIBRARY zstd REQUIRED)
  add_library(zstd::libzstd_shared SHARED IMPORTED)
  set_target_properties(zstd::libzstd_shared PROPERTIES IMPORTED_LOCATION "${ZSTD_LIBRARY}")
endif()
find_package(Threads REQUIRED)

# libchdr (CHD access) comes from the Beetle PSX submodule NESTED under the psxport submodule.
add_subdirectory(external/psxport/vendor/beetle-psx/deps/libchdr
                 ${CMAKE_BINARY_DIR}/libchdr_build EXCLUDE_FROM_ALL)

# The game binary (spyro_port): game/** + generated/** linking libpsxport. Output: scratch/bin/spyro_port.
# (This also includes cmake/psxport.cmake, which defines the framework library itself.)
include(cmake/spyro_port.cmake)
