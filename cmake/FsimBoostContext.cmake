# SPDX-License-Identifier: Apache-2.0

# Official Boost release archives use the traditional unified-header layout
# and intentionally omit the superproject CMakeLists.txt. Build only the
# x86-64 Context sources fsim consumes, matching Boost.Context's own CMake
# source selection for ELF/GAS and Windows PE/MASM.
function(fsim_add_fetched_boost_context source_directory)
  if(TARGET Boost::context)
    return()
  endif()

  set(context_source "${source_directory}/libs/context/src")
  set(
    FSIM_BOOST_CONTEXT_IMPLEMENTATION
    "fcontext"
    CACHE STRING
    "Fetched Boost.Context implementation used by fsim"
  )
  set_property(
    CACHE FSIM_BOOST_CONTEXT_IMPLEMENTATION
    PROPERTY STRINGS fcontext ucontext
  )
  if(
    NOT WIN32
    AND (
      CMAKE_CXX_FLAGS MATCHES "(^| )-fsanitize=[^ ]*address"
      OR CMAKE_CXX_FLAGS_DEBUG MATCHES "(^| )-fsanitize=[^ ]*address"
    )
    AND FSIM_BOOST_CONTEXT_IMPLEMENTATION STREQUAL "fcontext"
  )
    set(
      FSIM_BOOST_CONTEXT_IMPLEMENTATION
      "ucontext"
      CACHE STRING
      "Fetched Boost.Context implementation used by fsim"
      FORCE
    )
  endif()
  if(
    NOT FSIM_BOOST_CONTEXT_IMPLEMENTATION
      MATCHES "^(fcontext|ucontext)$"
  )
    message(
      FATAL_ERROR
      "FSIM_BOOST_CONTEXT_IMPLEMENTATION must be fcontext or ucontext"
    )
  endif()

  if(FSIM_BOOST_CONTEXT_IMPLEMENTATION STREQUAL "ucontext")
    if(WIN32)
      message(FATAL_ERROR "Boost.Context ucontext is not supported on Windows")
    endif()
    set(
      context_sources
      "${context_source}/continuation.cpp"
      "${context_source}/fiber.cpp"
      "${context_source}/posix/stack_traits.cpp"
    )
  else()
    set(context_sources "${context_source}/fcontext.cpp")
    if(WIN32)
      list(
        APPEND context_sources
        "${context_source}/asm/make_x86_64_ms_pe_masm.asm"
        "${context_source}/asm/jump_x86_64_ms_pe_masm.asm"
        "${context_source}/asm/ontop_x86_64_ms_pe_masm.asm"
        "${context_source}/windows/stack_traits.cpp"
      )
    else()
      list(
        APPEND context_sources
        "${context_source}/asm/make_x86_64_sysv_elf_gas.S"
        "${context_source}/asm/jump_x86_64_sysv_elf_gas.S"
        "${context_source}/asm/ontop_x86_64_sysv_elf_gas.S"
        "${context_source}/posix/stack_traits.cpp"
      )
    endif()
  endif()

  add_library(fsim_boost_context STATIC ${context_sources})
  add_library(Boost::context ALIAS fsim_boost_context)
  target_include_directories(
    fsim_boost_context
    SYSTEM PUBLIC "${source_directory}"
  )
  target_compile_features(fsim_boost_context PUBLIC cxx_std_11)
  target_compile_definitions(
    fsim_boost_context
    PUBLIC
      BOOST_CONTEXT_EXPORT=
      BOOST_CONTEXT_NO_LIB
      BOOST_CONTEXT_STATIC_LINK
    PRIVATE BOOST_CONTEXT_SOURCE
  )
  if(FSIM_BOOST_CONTEXT_IMPLEMENTATION STREQUAL "ucontext")
    target_compile_definitions(
      fsim_boost_context
      PUBLIC BOOST_USE_UCONTEXT
    )
    if(
      CMAKE_CXX_FLAGS MATCHES "(^| )-fsanitize=[^ ]*address"
      OR CMAKE_CXX_FLAGS_DEBUG MATCHES "(^| )-fsanitize=[^ ]*address"
    )
      target_compile_definitions(
        fsim_boost_context
        PUBLIC BOOST_USE_ASAN
      )
    endif()
  endif()
  set_target_properties(
    fsim_boost_context
    PROPERTIES
      POSITION_INDEPENDENT_CODE ON
  )
endfunction()
