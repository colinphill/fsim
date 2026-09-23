# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED FSIM_SOURCE_DIR OR NOT IS_DIRECTORY "${FSIM_SOURCE_DIR}")
  message(FATAL_ERROR "FSIM_SOURCE_DIR must name the source tree")
endif()

set(FSIM_MANIFEST
  "${FSIM_SOURCE_DIR}/packaging/source-package-manifest.txt")
if(NOT EXISTS "${FSIM_MANIFEST}")
  message(FATAL_ERROR "source-package manifest is missing")
endif()
file(STRINGS "${FSIM_MANIFEST}" FSIM_PACKAGED_PATHS)

set(FSIM_AUTHORED_COUNT 0)
foreach(FSIM_RELATIVE IN LISTS FSIM_PACKAGED_PATHS)
  if(FSIM_RELATIVE STREQUAL "" OR FSIM_RELATIVE MATCHES "^#")
    continue()
  endif()
  if(NOT FSIM_RELATIVE MATCHES
      "^(\\.github/|cmake/|docs/|examples/|include/|scripts/|src/|tests/|\\.clang-format$|\\.gitignore$|CMakeLists\\.txt$|CMakePresets\\.json$|README\\.md$)")
    continue()
  endif()
  if(FSIM_RELATIVE MATCHES "^tests/fuzz/corpus/"
      OR FSIM_RELATIVE STREQUAL
         "examples/three_language_hierarchy/three_language.vcd")
    continue()
  endif()
  set(FSIM_FILE "${FSIM_SOURCE_DIR}/${FSIM_RELATIVE}")
  if(NOT EXISTS "${FSIM_FILE}" OR IS_DIRECTORY "${FSIM_FILE}")
    message(FATAL_ERROR "packaged authored file is missing: ${FSIM_RELATIVE}")
  endif()
  file(READ "${FSIM_FILE}" FSIM_PREFIX LIMIT 4096)
  string(FIND "${FSIM_PREFIX}" "SPDX-License-Identifier: Apache-2.0"
    FSIM_APACHE_OFFSET)
  if(FSIM_APACHE_OFFSET EQUAL -1)
    if(FSIM_RELATIVE STREQUAL "scripts/simplification_benchmarks.json")
      file(READ "${FSIM_FILE}.license" FSIM_COMPANION_LICENSE)
      if(FSIM_COMPANION_LICENSE STREQUAL
          "SPDX-License-Identifier: Apache-2.0\n")
        math(EXPR FSIM_AUTHORED_COUNT "${FSIM_AUTHORED_COUNT} + 1")
        continue()
      endif()
    endif()
    string(FIND "${FSIM_PREFIX}"
      "SPDX-License-Identifier: LicenseRef-IEEE-1800" FSIM_IEEE_OFFSET)
    if((NOT FSIM_RELATIVE STREQUAL "include/vpi_user.h"
        AND NOT FSIM_RELATIVE STREQUAL "include/sv_vpi_user.h")
       OR FSIM_IEEE_OFFSET EQUAL -1)
      message(FATAL_ERROR
        "packaged authored file lacks an approved SPDX notice: ${FSIM_RELATIVE}")
    endif()
  endif()
  math(EXPR FSIM_AUTHORED_COUNT "${FSIM_AUTHORED_COUNT} + 1")
endforeach()

if(FSIM_AUTHORED_COUNT LESS 1)
  message(FATAL_ERROR "source-package manifest has no authored files")
endif()
file(READ "${FSIM_SOURCE_DIR}/LICENSE" FSIM_LICENSE_PREFIX LIMIT 256)
if(NOT FSIM_LICENSE_PREFIX MATCHES "Apache License")
  message(FATAL_ERROR "repository LICENSE is not the reviewed Apache license")
endif()

message(STATUS
  "authored license inventory: ${FSIM_AUTHORED_COUNT} packaged authored files have approved SPDX notices")
