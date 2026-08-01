# SPDX-License-Identifier: Apache-2.0

option(
  FSIM_COMPACT_DEBUG_BUILD
  "Use the compiler's debugger-friendly optimization level in Debug builds"
  ON
)
set(
  FSIM_LINK_POOL_SIZE
  "8"
  CACHE STRING
  "Maximum concurrent Ninja link/archive jobs"
)

if(NOT FSIM_LINK_POOL_SIZE MATCHES "^[1-9][0-9]*$")
  message(
    FATAL_ERROR
    "FSIM_LINK_POOL_SIZE must be a positive integer "
    "(received '${FSIM_LINK_POOL_SIZE}')"
  )
endif()

set(FSIM_HAS_LINK_POOL OFF)
if(CMAKE_GENERATOR MATCHES "Ninja")
  set_property(
    GLOBAL APPEND PROPERTY JOB_POOLS
    "fsim_link_pool=${FSIM_LINK_POOL_SIZE}"
  )
  set(CMAKE_JOB_POOL_LINK fsim_link_pool)
  set(FSIM_HAS_LINK_POOL ON)
  message(
    STATUS
    "fsim link/archive concurrency: ${FSIM_LINK_POOL_SIZE}"
  )
endif()

function(fsim_configure_debug_footprint target)
  if(
    FSIM_COMPACT_DEBUG_BUILD
    AND NOT MSVC
    AND CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$"
  )
    target_compile_options(
      ${target}
      PRIVATE "$<$<CONFIG:Debug>:-Og>"
    )
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      target_compile_options(
        ${target}
        PRIVATE "$<$<CONFIG:Debug>:-gz=zstd>"
      )
      target_link_options(
        ${target}
        PRIVATE "$<$<CONFIG:Debug>:-gz=zstd>"
      )
    endif()
  endif()

  if(FSIM_HAS_LINK_POOL)
    set_property(TARGET ${target} PROPERTY JOB_POOL_LINK fsim_link_pool)
  endif()
endfunction()
