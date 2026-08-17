# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

foreach(FSIM_REQUIRED IN ITEMS
    FSIM_SOURCE_DIR
    FSIM_BINARY_DIR
    FSIM_WORK_DIR
    FSIM_BINDIR
    FSIM_LIBDIR
    FSIM_INCLUDEDIR
    FSIM_DOCDIR
    FSIM_DATADIR
    FSIM_EXECUTABLE_NAME
    FSIM_API_LIBRARY_NAME
    FSIM_API_LIBRARY_DIR)
  if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()

file(REMOVE_RECURSE "${FSIM_WORK_DIR}")
file(MAKE_DIRECTORY "${FSIM_WORK_DIR}")
set(FSIM_STAGE "${FSIM_WORK_DIR}/install ownership é")
set(FSIM_SENTINEL "${FSIM_WORK_DIR}/outside-install.sentinel")
file(WRITE "${FSIM_SENTINEL}" "outside install ownership\n")

set(FSIM_INSTALL_COMMAND
    "${CMAKE_COMMAND}" --install "${FSIM_BINARY_DIR}" --prefix "${FSIM_STAGE}")
if(DEFINED FSIM_CONFIG AND NOT FSIM_CONFIG STREQUAL "")
  list(APPEND FSIM_INSTALL_COMMAND --config "${FSIM_CONFIG}")
endif()
execute_process(
  COMMAND ${FSIM_INSTALL_COMMAND}
  RESULT_VARIABLE FSIM_INSTALL_RESULT
  OUTPUT_VARIABLE FSIM_INSTALL_OUTPUT
  ERROR_VARIABLE FSIM_INSTALL_ERROR)
if(NOT FSIM_INSTALL_RESULT EQUAL 0)
  message(FATAL_ERROR
    "binary ownership install failed with ${FSIM_INSTALL_RESULT}\n"
    "${FSIM_INSTALL_OUTPUT}${FSIM_INSTALL_ERROR}")
endif()

set(FSIM_BUILD_MANIFEST "${FSIM_BINARY_DIR}/install_manifest.txt")
if(NOT EXISTS "${FSIM_BUILD_MANIFEST}")
  message(FATAL_ERROR "CMake install manifest was not produced")
endif()
file(STRINGS "${FSIM_BUILD_MANIFEST}" FSIM_INSTALLED_PATHS ENCODING UTF-8)
list(LENGTH FSIM_INSTALLED_PATHS FSIM_INSTALL_COUNT)
if(FSIM_INSTALL_COUNT LESS 50)
  message(FATAL_ERROR
    "binary install manifest is unexpectedly small: ${FSIM_INSTALL_COUNT}")
endif()
set(FSIM_UNIQUE_PATHS "${FSIM_INSTALLED_PATHS}")
list(REMOVE_DUPLICATES FSIM_UNIQUE_PATHS)
list(LENGTH FSIM_UNIQUE_PATHS FSIM_UNIQUE_COUNT)
if(NOT FSIM_INSTALL_COUNT EQUAL FSIM_UNIQUE_COUNT)
  message(FATAL_ERROR "binary install manifest contains duplicate paths")
endif()

file(TO_CMAKE_PATH "${FSIM_STAGE}" FSIM_NORMAL_STAGE)
string(APPEND FSIM_NORMAL_STAGE "/")
set(FSIM_MANIFEST_RELATIVE)
foreach(FSIM_PATH IN LISTS FSIM_INSTALLED_PATHS)
  file(TO_CMAKE_PATH "${FSIM_PATH}" FSIM_NORMAL_PATH)
  string(FIND "${FSIM_NORMAL_PATH}" "${FSIM_NORMAL_STAGE}" FSIM_PREFIX_INDEX)
  if(NOT FSIM_PREFIX_INDEX EQUAL 0
     OR FSIM_NORMAL_PATH MATCHES "(^|/)\\.\\.?(/|$)")
    message(FATAL_ERROR
      "install manifest path escapes the staged prefix: ${FSIM_PATH}")
  endif()
  string(LENGTH "${FSIM_NORMAL_STAGE}" FSIM_PREFIX_LENGTH)
  string(SUBSTRING "${FSIM_NORMAL_PATH}" ${FSIM_PREFIX_LENGTH} -1 FSIM_RELATIVE)
  if(FSIM_RELATIVE STREQUAL "" OR FSIM_RELATIVE MATCHES "\\\\")
    message(FATAL_ERROR "invalid binary install path: ${FSIM_PATH}")
  endif()
  if(NOT EXISTS "${FSIM_NORMAL_PATH}" AND NOT IS_SYMLINK "${FSIM_NORMAL_PATH}")
    message(FATAL_ERROR "manifest-owned installed path is missing: ${FSIM_PATH}")
  endif()
  list(APPEND FSIM_MANIFEST_RELATIVE "${FSIM_RELATIVE}")
endforeach()
list(SORT FSIM_MANIFEST_RELATIVE)

file(GLOB_RECURSE FSIM_STAGED_PATHS LIST_DIRECTORIES FALSE
     RELATIVE "${FSIM_STAGE}" "${FSIM_STAGE}/*")
list(SORT FSIM_STAGED_PATHS)
foreach(FSIM_PATH IN LISTS FSIM_STAGED_PATHS)
  list(FIND FSIM_MANIFEST_RELATIVE "${FSIM_PATH}" FSIM_MANIFEST_INDEX)
  if(FSIM_MANIFEST_INDEX EQUAL -1)
    message(FATAL_ERROR "installed file lacks manifest ownership: ${FSIM_PATH}")
  endif()
endforeach()
foreach(FSIM_PATH IN LISTS FSIM_MANIFEST_RELATIVE)
  list(FIND FSIM_STAGED_PATHS "${FSIM_PATH}" FSIM_STAGE_INDEX)
  if(FSIM_STAGE_INDEX EQUAL -1)
    message(FATAL_ERROR "install manifest names an absent staged file: ${FSIM_PATH}")
  endif()
endforeach()

set(FSIM_REQUIRED_PATHS
    "${FSIM_BINDIR}/${FSIM_EXECUTABLE_NAME}"
    "${FSIM_API_LIBRARY_DIR}/${FSIM_API_LIBRARY_NAME}"
    "${FSIM_INCLUDEDIR}/fsim/api.h"
    "${FSIM_INCLUDEDIR}/fsim/systemc.hpp"
    "${FSIM_LIBDIR}/cmake/fsim/fsimConfig.cmake"
    "${FSIM_LIBDIR}/cmake/fsim/fsimConfigVersion.cmake"
    "${FSIM_LIBDIR}/pkgconfig/fsim.pc"
    "${FSIM_DOCDIR}/LICENSE"
    "${FSIM_DOCDIR}/README.md"
    "${FSIM_DOCDIR}/third-party/systemc-3.0.2/LICENSE"
    "${FSIM_DOCDIR}/third-party/systemc-3.0.2/NOTICE"
    "${FSIM_DOCDIR}/third-party/systemc-3.0.2/SOURCE_MANIFEST.txt"
    "${FSIM_DOCDIR}/third-party/systemc-3.0.2/systemc-3.0.2.spdx.json"
    "${FSIM_DOCDIR}/third-party/scv-2.0.1/LICENSE"
    "${FSIM_DOCDIR}/third-party/scv-2.0.1/NOTICE"
    "${FSIM_DOCDIR}/third-party/scv-2.0.1/PATCHES.txt"
    "${FSIM_DOCDIR}/third-party/scv-2.0.1/SOURCE_MANIFEST.txt"
    "${FSIM_DOCDIR}/third-party/scv-2.0.1/scv-2.0.1.spdx.json"
    "${FSIM_DATADIR}/fsim/examples/vertical_slice/fsim.toml"
    "${FSIM_DATADIR}/fsim/examples/three_language_hierarchy/README.md"
    "${FSIM_DATADIR}/fsim/vhdl/ieee-1076-2019/LICENSE")
foreach(FSIM_PATH IN LISTS FSIM_REQUIRED_PATHS)
  list(FIND FSIM_MANIFEST_RELATIVE "${FSIM_PATH}" FSIM_REQUIRED_INDEX)
  if(FSIM_REQUIRED_INDEX EQUAL -1)
    message(FATAL_ERROR "binary install manifest omits required path: ${FSIM_PATH}")
  endif()
endforeach()

file(READ "${FSIM_STAGE}/${FSIM_LIBDIR}/pkgconfig/fsim.pc" FSIM_PC)
foreach(FSIM_TOKEN IN ITEMS
    "prefix=\${pcfiledir}/../.."
    "Name: fsim"
    "Version: 0.1.0"
    "Libs: -L\${libdir} -lfsim_api")
  string(FIND "${FSIM_PC}" "${FSIM_TOKEN}" FSIM_TOKEN_INDEX)
  if(FSIM_TOKEN_INDEX EQUAL -1)
    message(FATAL_ERROR "installed fsim.pc omits ${FSIM_TOKEN}")
  endif()
endforeach()
foreach(FSIM_LEAK IN ITEMS "${FSIM_SOURCE_DIR}" "${FSIM_BINARY_DIR}" "/usr/local")
  string(FIND "${FSIM_PC}" "${FSIM_LEAK}" FSIM_LEAK_INDEX)
  if(NOT FSIM_LEAK_INDEX EQUAL -1)
    message(FATAL_ERROR "installed fsim.pc leaks ${FSIM_LEAK}")
  endif()
endforeach()

file(COPY_FILE "${FSIM_BUILD_MANIFEST}"
     "${FSIM_WORK_DIR}/retained-install-manifest.txt" ONLY_IF_DIFFERENT)
foreach(FSIM_PATH IN LISTS FSIM_INSTALLED_PATHS)
  file(REMOVE "${FSIM_PATH}")
endforeach()
file(GLOB_RECURSE FSIM_REMAINDER LIST_DIRECTORIES FALSE "${FSIM_STAGE}/*")
if(FSIM_REMAINDER)
  list(GET FSIM_REMAINDER 0 FSIM_FIRST_REMAINDER)
  message(FATAL_ERROR
    "manifest-driven uninstall left installed content: ${FSIM_FIRST_REMAINDER}")
endif()
if(NOT EXISTS "${FSIM_SENTINEL}")
  message(FATAL_ERROR "manifest-driven uninstall removed an outside sentinel")
endif()
file(REMOVE_RECURSE "${FSIM_STAGE}")
if(EXISTS "${FSIM_STAGE}")
  message(FATAL_ERROR "empty staged install directories were not removed")
endif()

message(STATUS
  "binary install ownership: ${FSIM_INSTALL_COUNT} files, exact staged manifest, "
  "relocatable pkg-config, complete isolated uninstall")
