# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)

project(
  fsim
  VERSION 0.1.0
  DESCRIPTION "Mixed-language compiled HDL simulator"
  LANGUAGES C CXX
)

if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(
    FATAL_ERROR
    "fsim currently supports only Windows and Linux x86-64 "
    "(detected ${CMAKE_SYSTEM_NAME})"
  )
endif()
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" FSIM_SYSTEM_PROCESSOR)
if(
  NOT CMAKE_SIZEOF_VOID_P EQUAL 8
  OR NOT FSIM_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|x64)$"
)
  message(
    FATAL_ERROR
    "fsim currently supports only Windows and Linux x86-64 "
    "(detected ${CMAKE_SYSTEM_PROCESSOR}, ${CMAKE_SIZEOF_VOID_P}-byte pointers)"
  )
endif()

include(GNUInstallDirs)
include(CTest)
include(cmake/FsimWarnings.cmake)
include(cmake/FsimDependencies.cmake)
include(cmake/FsimBoostContext.cmake)

option(FSIM_BUILD_TESTS "Build fsim's unit and integration tests" ${BUILD_TESTING})
option(FSIM_BUILD_FUZZERS "Build Clang/libFuzzer targets" OFF)
option(FSIM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
set(
  FSIM_LLVM_MODE
  "AUTO"
  CACHE STRING
  "LLVM backend selection: AUTO discovers LLVM 22.1.8, ON requires it, OFF disables it"
)
set_property(CACHE FSIM_LLVM_MODE PROPERTY STRINGS AUTO ON OFF)
set(
  FSIM_SYSTEMC_FIBER_MODE
  "AUTO"
  CACHE STRING
  "SystemC fiber selection: AUTO discovers Boost.Context 1.91.0, ON requires it, OFF disables it"
)
set_property(CACHE FSIM_SYSTEMC_FIBER_MODE PROPERTY STRINGS AUTO ON OFF)

string(TOUPPER "${FSIM_LLVM_MODE}" FSIM_LLVM_MODE_NORMALIZED)
if(NOT FSIM_LLVM_MODE_NORMALIZED MATCHES "^(AUTO|ON|OFF)$")
  message(
    FATAL_ERROR
    "FSIM_LLVM_MODE must be AUTO, ON, or OFF (received '${FSIM_LLVM_MODE}')"
  )
endif()

set(FSIM_PRODUCTION_LLVM_VERSION "${FSIM_LLVM_VERSION}")
set(FSIM_WITH_LLVM OFF)
if(NOT FSIM_LLVM_MODE_NORMALIZED STREQUAL "OFF")
  if(FSIM_LLVM_MODE_NORMALIZED STREQUAL "ON")
    find_package(
      LLVM ${FSIM_PRODUCTION_LLVM_VERSION}
      EXACT
      CONFIG
      REQUIRED
    )
  else()
    find_package(
      LLVM ${FSIM_PRODUCTION_LLVM_VERSION}
      EXACT
      CONFIG
      QUIET
    )
  endif()

  if(LLVM_FOUND)
    if(NOT LLVM_PACKAGE_VERSION VERSION_EQUAL FSIM_PRODUCTION_LLVM_VERSION)
      message(
        FATAL_ERROR
        "fsim requires LLVM ${FSIM_PRODUCTION_LLVM_VERSION}; "
        "LLVMConfig.cmake reported ${LLVM_PACKAGE_VERSION}"
      )
    endif()
    set(FSIM_WITH_LLVM ON)
    message(STATUS "fsim LLVM backend: LLVM ${LLVM_PACKAGE_VERSION}")
  elseif(FSIM_LLVM_MODE_NORMALIZED STREQUAL "AUTO")
    message(
      STATUS
      "fsim LLVM backend disabled: LLVM ${FSIM_PRODUCTION_LLVM_VERSION} "
      "was not found (set LLVM_DIR or use -DFSIM_LLVM_MODE=ON to require it)"
    )
  endif()
else()
  message(STATUS "fsim LLVM backend disabled by FSIM_LLVM_MODE=OFF")
endif()

string(
  TOUPPER
  "${FSIM_SYSTEMC_FIBER_MODE}"
  FSIM_SYSTEMC_FIBER_MODE_NORMALIZED
)
if(NOT FSIM_SYSTEMC_FIBER_MODE_NORMALIZED MATCHES "^(AUTO|ON|OFF)$")
  message(
    FATAL_ERROR
    "FSIM_SYSTEMC_FIBER_MODE must be AUTO, ON, or OFF "
    "(received '${FSIM_SYSTEMC_FIBER_MODE}')"
  )
endif()

set(FSIM_WITH_SYSTEMC_FIBERS OFF)
if(NOT FSIM_SYSTEMC_FIBER_MODE_NORMALIZED STREQUAL "OFF")
  find_package(
    Boost ${FSIM_BOOST_VERSION}
    EXACT
    CONFIG
    QUIET
    COMPONENTS context
  )
  if(NOT Boost_FOUND OR NOT TARGET Boost::context)
    include(FetchContent)
    string(
      REPLACE "." "_"
      FSIM_BOOST_ARCHIVE_VERSION
      "${FSIM_BOOST_VERSION}"
    )
    FetchContent_Declare(
      fsim_boost
      URL
        "https://archives.boost.io/release/${FSIM_BOOST_VERSION}/source/boost_${FSIM_BOOST_ARCHIVE_VERSION}.tar.bz2"
      URL_HASH
        "SHA256=${FSIM_BOOST_SOURCE_SHA256}"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    message(
      STATUS
      "Boost.Context ${FSIM_BOOST_VERSION} was not found; "
      "fetching the pinned Boost source archive"
    )
    FetchContent_MakeAvailable(fsim_boost)
    if(WIN32)
      enable_language(ASM_MASM)
    else()
      enable_language(ASM)
    endif()
    fsim_add_fetched_boost_context("${fsim_boost_SOURCE_DIR}")
  endif()
  if(TARGET Boost::context)
    set(FSIM_WITH_SYSTEMC_FIBERS ON)
    message(
      STATUS
      "fsim SystemC fibers: Boost.Context ${FSIM_BOOST_VERSION}"
    )
  else()
    message(
      FATAL_ERROR
      "Boost.Context ${FSIM_BOOST_VERSION} did not provide Boost::context"
    )
  endif()
else()
  message(STATUS "fsim SystemC fibers disabled by FSIM_SYSTEMC_FIBER_MODE=OFF")
endif()

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN YES)

function(fsim_configure_library target)
  target_compile_features(${target} PUBLIC cxx_std_20)
  target_include_directories(
    ${target}
    PUBLIC
      "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
      "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
  )
  fsim_enable_warnings(${target})
endfunction()

function(fsim_configure_executable target)
  target_compile_features(${target} PRIVATE cxx_std_20)
  target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/include")
  fsim_enable_warnings(${target})
endfunction()

function(fsim_configure_test target)
  fsim_configure_executable(${target})
  # Several small, dependency-free tests intentionally use assert(). Keep
  # those assertions live in multi-config and Release test builds.
  if(MSVC)
    target_compile_options(${target} PRIVATE /UNDEBUG)
  else()
    target_compile_options(${target} PRIVATE -UNDEBUG)
  endif()
endfunction()

add_library(fsim_diagnostic STATIC src/diagnostic/diagnostic.cpp)
add_library(fsim::diagnostic ALIAS fsim_diagnostic)
fsim_configure_library(fsim_diagnostic)

add_library(fsim_project STATIC src/project/project.cpp)
add_library(fsim::project ALIAS fsim_project)
fsim_configure_library(fsim_project)
target_link_libraries(fsim_project PUBLIC fsim_diagnostic)

set(
  FSIM_FRONTEND_SOURCES
  "${PROJECT_SOURCE_DIR}/src/frontend/common.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/lexer.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/parser.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/verilog_parser.cpp"
  "${PROJECT_SOURCE_DIR}/src/frontend/vhdl_parser.cpp"
)
add_library(fsim_frontend STATIC ${FSIM_FRONTEND_SOURCES})
add_library(fsim::frontend ALIAS fsim_frontend)
fsim_configure_library(fsim_frontend)

add_library(
  fsim_runtime
  STATIC
    src/runtime/packed_value.cpp
    src/runtime/scheduler.cpp
    src/runtime/simir.cpp
    src/runtime/vcd_writer.cpp
)
add_library(fsim::runtime ALIAS fsim_runtime)
fsim_configure_library(fsim_runtime)

add_library(fsim_elaboration STATIC src/elaboration/elaborator.cpp)
add_library(fsim::elaboration ALIAS fsim_elaboration)
fsim_configure_library(fsim_elaboration)
target_link_libraries(fsim_elaboration PUBLIC fsim_frontend fsim_runtime)

add_library(fsim_support STATIC src/support/sha256.cpp)
add_library(fsim::support ALIAS fsim_support)
fsim_configure_library(fsim_support)

add_library(fsim_object_cache STATIC src/compiler/object_cache.cpp)
add_library(fsim::object_cache ALIAS fsim_object_cache)
fsim_configure_library(fsim_object_cache)
target_link_libraries(fsim_object_cache PUBLIC fsim_support)

add_library(fsim_platform STATIC src/platform/dynamic_library.cpp)
add_library(fsim::platform ALIAS fsim_platform)
fsim_configure_library(fsim_platform)
target_link_libraries(fsim_platform PRIVATE ${CMAKE_DL_LIBS})

add_library(fsim_systemc_headers INTERFACE)
add_library(fsim::systemc_headers ALIAS fsim_systemc_headers)
target_compile_features(fsim_systemc_headers INTERFACE cxx_std_20)
target_include_directories(
  fsim_systemc_headers
  INTERFACE
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

add_library(
  fsim_systemc
  STATIC
    src/systemc/hierarchy.cpp
    src/systemc/plugin_compiler.cpp
    src/systemc/plugin_loader.cpp
)
add_library(fsim::systemc ALIAS fsim_systemc)
fsim_configure_library(fsim_systemc)
target_link_libraries(
  fsim_systemc
  PUBLIC
    fsim_platform
    fsim_systemc_headers
    fsim_project
    fsim_diagnostic
    fsim_object_cache
    fsim_runtime
)
if(FSIM_WITH_SYSTEMC_FIBERS)
  target_link_libraries(fsim_systemc PRIVATE Boost::context)
  target_compile_definitions(
    fsim_systemc
    PUBLIC FSIM_HAS_BOOST_CONTEXT=1
  )
  target_compile_definitions(
    fsim_elaboration
    PUBLIC FSIM_HAS_BOOST_CONTEXT=1
  )
endif()

add_library(fsim_api SHARED src/api/api.cpp)
add_library(fsim::api ALIAS fsim_api)
fsim_configure_library(fsim_api)
target_link_libraries(fsim_api PUBLIC fsim_project fsim_diagnostic)
target_compile_definitions(
  fsim_api
  PRIVATE FSIM_SHARED FSIM_BUILDING_LIBRARY
  INTERFACE FSIM_SHARED
)
set_target_properties(
  fsim_api
  PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
if(UNIX AND NOT APPLE)
  target_link_options(
    fsim_api
    PRIVATE
      "LINKER:--version-script=${PROJECT_SOURCE_DIR}/cmake/fsim_api.map"
  )
  set_property(
    TARGET fsim_api
    APPEND
    PROPERTY LINK_DEPENDS "${PROJECT_SOURCE_DIR}/cmake/fsim_api.map"
  )
endif()

add_library(fsim_cli STATIC src/cli/driver.cpp)
add_library(fsim::cli ALIAS fsim_cli)
fsim_configure_library(fsim_cli)
target_link_libraries(fsim_cli PUBLIC fsim_project fsim_diagnostic)

if(FSIM_WITH_LLVM)
  add_library(fsim_llvm STATIC src/compiler/llvm_jit.cpp)
  add_library(fsim::llvm ALIAS fsim_llvm)
  fsim_configure_library(fsim_llvm)
  target_link_libraries(fsim_llvm PUBLIC fsim_runtime PRIVATE fsim_object_cache)
  target_include_directories(fsim_llvm SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
  target_compile_definitions(fsim_llvm PUBLIC FSIM_HAS_LLVM=1)

  # Prefer a distribution's monolithic imported target. Static LLVM builds
  # expose components instead; map only the narrow ORC adapter's requirements.
  if(TARGET LLVM)
    target_link_libraries(fsim_llvm PRIVATE LLVM)
  else()
    llvm_map_components_to_libnames(
      FSIM_LLVM_LIBRARIES
      core
      executionengine
      native
      nativecodegen
      object
      orcjit
      passes
      runtimedyld
      support
      targetparser
    )
    target_link_libraries(fsim_llvm PRIVATE ${FSIM_LLVM_LIBRARIES})
  endif()
endif()

if(EXISTS "${PROJECT_SOURCE_DIR}/src/app/application.cpp")
  add_library(fsim_application STATIC src/app/application.cpp)
  add_library(fsim::application ALIAS fsim_application)
  fsim_configure_library(fsim_application)
  target_link_libraries(
    fsim_application
    PUBLIC
      fsim_cli
      fsim_elaboration
      fsim_object_cache
      fsim_systemc
  )
  if(FSIM_WITH_LLVM)
    target_link_libraries(fsim_application PRIVATE fsim_llvm)
    target_compile_definitions(fsim_application PUBLIC FSIM_HAS_LLVM=1)
  endif()
  target_link_libraries(fsim_api PUBLIC fsim_application)
endif()

if(EXISTS "${PROJECT_SOURCE_DIR}/src/main.cpp")
  add_executable(fsim src/main.cpp)
  fsim_configure_executable(fsim)
  if(TARGET fsim_application)
    target_link_libraries(fsim PRIVATE fsim_application)
  else()
    target_link_libraries(fsim PRIVATE fsim_cli)
  endif()

  set(FSIM_COMMAND_TARGETS fsim)
  foreach(alias_name IN ITEMS fsim-vhdl fsim-sv fsim-elab fsim-run)
    add_executable(${alias_name} src/main.cpp)
    fsim_configure_executable(${alias_name})
    if(TARGET fsim_application)
      target_link_libraries(${alias_name} PRIVATE fsim_application)
    else()
      target_link_libraries(${alias_name} PRIVATE fsim_cli)
    endif()
    list(APPEND FSIM_COMMAND_TARGETS ${alias_name})
  endforeach()
endif()

if(FSIM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(FSIM_BUILD_FUZZERS)
  add_subdirectory(tests/fuzz)
endif()

install(
  FILES
    include/systemc
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(
  FILES
    include/fsim/api.h
    include/fsim/systemc.hpp
    include/fsim/systemc_abi.h
    include/fsim/version.hpp
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fsim"
)
if(FSIM_COMMAND_TARGETS)
  install(
    TARGETS ${FSIM_COMMAND_TARGETS}
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  )
endif()
install(
  TARGETS fsim_api
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(
  FILES LICENSE README.md
  DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)
install(
  DIRECTORY docs/
  DESTINATION "${CMAKE_INSTALL_DOCDIR}"
  FILES_MATCHING PATTERN "*.md"
)
