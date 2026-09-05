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
  game/core/guest_execution.cpp
  game/core/native_angle.cpp
  game/core/native_gameplay.cpp
  game/core/native_gte.cpp
  game/core/native_leaf.cpp
  game/core/native_rand.cpp
  game/core/native_util.cpp
  game/core/native_vec.cpp
  game/core/spyro_runtime.cpp
  game/core/title_runtime_registry.cpp
  game/core/title_selection.cpp
  titles/spyro1/core/spyro1_runtime.cpp
  titles/spyro2/core/spyro2_runtime.cpp
  titles/spyro3/core/spyro3_runtime.cpp)

add_library(spyro_runtime STATIC ${SPYRO_RUNTIME_SOURCES})
add_dependencies(spyro_runtime spyro_title_catalog)
target_include_directories(spyro_runtime PUBLIC
  game/core
  titles/spyro1/core
  titles/spyro2/core
  titles/spyro3/core
  ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(spyro_runtime PUBLIC psxport OpenSSL::Crypto)

add_executable(spyro_port
  game/core/main.cpp
  game/core/world_guest_execution.cpp)
target_link_libraries(spyro_port PRIVATE spyro_runtime)
set_target_properties(spyro_port PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(CTest)
if(BUILD_TESTING)
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
