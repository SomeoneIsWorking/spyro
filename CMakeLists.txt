# SpyroEngine — the Spyro the Dragon PC-native port, built on the psxport framework
# (external/psxport submodule).
#
# The framework (PSX-generic static-recomp substrate + runtime + HW backends + SBS harness + SDL_GPU
# renderer) is the reusable `psxport` library; this repo adds the game (game/**) and the recompiled
# SCUS_942.28 substrate (generated/**, produced by run.sh / tools/ensure_recomp.py) and links it.
cmake_minimum_required(VERSION 3.21)
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
