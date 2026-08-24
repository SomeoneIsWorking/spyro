# SpyroEngine — the Spyro the Dragon PC-native port, built on the psxport framework
# (external/psxport).
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

# PSXPORT_DIR — WHERE THE FRAMEWORK SOURCE COMES FROM. Defaults to external/psxport, which is no
# longer a git submodule (2026-08-16): tools/psxport_sync.py --auto (called by run.sh) makes it a
# SYMLINK to the workspace's shared framework clone when one exists, or a private clone at this
# repo's psxport.pin on a fresh machine. So a fresh clone of this repo alone still builds
# standalone: that property is what "each game is its own project using psxport as the framework"
# means, and it must stay testable.
#
# Point it at a specific framework checkout to build against in-progress framework work:
#
#   cmake -S . -B build -DPSXPORT_DIR=$HOME/repo/psx/psxport
#
# It is a CACHE path, so it survives reconfigures, and `tools/run.py` passes it through from the
# environment. Everything else in this file and in cmake/ derives from it — never re-spell
# `external/psxport` at a call site, or the two halves of a build can come from different checkouts.
set(PSXPORT_DIR "${CMAKE_SOURCE_DIR}/external/psxport" CACHE PATH
    "psxport framework source directory (default: external/psxport)")
if(NOT EXISTS "${PSXPORT_DIR}/cmake/psxport.cmake")
  message(FATAL_ERROR "PSXPORT_DIR=${PSXPORT_DIR} has no cmake/psxport.cmake — "
                      "run `python3 tools/psxport_sync.py --auto`, or point PSXPORT_DIR at a psxport checkout.")
endif()
message(STATUS "psxport framework: ${PSXPORT_DIR}")

# RECORD WHAT THIS CONFIGURE RESOLVED, so `tools/psxport_sync.py --check` can compare the framework you
# actually BUILT against with the one psxport.pin RECORDS. The framework is no longer a git submodule
# (external/psxport is a symlink to the workspace's shared clone, or a private clone at the pin — see
# tools/psxport_sync.py); the pin is provenance, and this file is what keeps it honest. Measured
# 2026-08-16: this tree was once built against one framework commit while recording another, so a bare
# clone did not compile, and nothing noticed.
execute_process(COMMAND git -C "${PSXPORT_DIR}" rev-parse HEAD
                OUTPUT_VARIABLE PSXPORT_RESOLVED_SHA OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET RESULT_VARIABLE _psxport_rev_rc)
if(NOT _psxport_rev_rc EQUAL 0)
  set(PSXPORT_RESOLVED_SHA "unknown")   # honest: never silently record a plausible-looking sha
endif()
file(WRITE "${CMAKE_BINARY_DIR}/psxport_resolved.txt"
     "dir = ${PSXPORT_DIR}\ncommit = ${PSXPORT_RESOLVED_SHA}\n")
message(STATUS "psxport commit: ${PSXPORT_RESOLVED_SHA}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(zstd CONFIG QUIET)
if(NOT TARGET zstd::libzstd_shared)
  find_library(ZSTD_LIBRARY zstd REQUIRED)
  add_library(zstd::libzstd_shared SHARED IMPORTED)
  set_target_properties(zstd::libzstd_shared PROPERTIES IMPORTED_LOCATION "${ZSTD_LIBRARY}")
endif()
find_package(Threads REQUIRED)

# libchdr (CHD access) comes from the Beetle PSX submodule NESTED under psxport (PSXPORT_DIR, above).
add_subdirectory(${PSXPORT_DIR}/vendor/beetle-psx/deps/libchdr
                 ${CMAKE_BINARY_DIR}/libchdr_build EXCLUDE_FROM_ALL)

# The game binary (spyro_port): game/** + generated/** linking libpsxport. Output: scratch/bin/spyro_port.
# (This also includes cmake/psxport.cmake, which defines the framework library itself.)
include(cmake/spyro_port.cmake)
