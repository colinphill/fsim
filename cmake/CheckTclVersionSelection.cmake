# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)

foreach(required IN ITEMS FSIM_SOURCE_DIR FSIM_WORK_DIR)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

include("${FSIM_SOURCE_DIR}/cmake/FsimDependencies.cmake")
include("${FSIM_SOURCE_DIR}/cmake/FsimTclVersion.cmake")

file(READ "${FSIM_SOURCE_DIR}/cmake/FsimTcl.cmake" tcl_adapter)
file(READ "${FSIM_SOURCE_DIR}/cmake/BuildTclMinGW.cmake" tcl_mingw_adapter)
foreach(required_policy IN ITEMS
    "CMAKE_GENERATOR MATCHES \"Makefiles\""
    "BUILD_JOB_SERVER_AWARE TRUE"
    "--unset=MAKEFLAGS"
    "--unset=MAKELEVEL"
    "\"\${FSIM_MAKE_EXECUTABLE}\" -j1")
  string(FIND "${tcl_adapter}" "${required_policy}" policy_index)
  if(policy_index EQUAL -1)
    message(
      FATAL_ERROR
      "fetched Tcl build lost jobserver policy: ${required_policy}"
    )
  endif()
endforeach()
string(FIND
  "${tcl_mingw_adapter}" "CFLAGS=-Wno-c++-keyword"
  mingw_warning_policy_index
)
if(mingw_warning_policy_index EQUAL -1)
  message(FATAL_ERROR "fetched MinGW Tcl lost its warning-isolation policy")
endif()

file(REMOVE_RECURSE "${FSIM_WORK_DIR}")
foreach(version IN ITEMS 8.6.18 9.0.3 9.0.4 9.0.5 9.1.0)
  set(include_dir "${FSIM_WORK_DIR}/${version}")
  file(MAKE_DIRECTORY "${include_dir}")
  file(
    WRITE
    "${include_dir}/tcl.h"
    "#define TCL_PATCH_LEVEL \"${version}\"\n"
  )
  fsim_tcl_version_is_supported(
    "${include_dir}"
    supported
    detected
  )
  if(NOT detected STREQUAL version)
    message(
      FATAL_ERROR
      "detected Tcl ${detected}, expected ${version}"
    )
  endif()
  if(version MATCHES "^9\\.0\\.[45]$")
    if(NOT supported)
      message(FATAL_ERROR "Tcl ${version} should be supported")
    endif()
  elseif(supported)
    message(FATAL_ERROR "Tcl ${version} should be rejected")
  endif()
endforeach()

set(malformed_include "${FSIM_WORK_DIR}/malformed")
file(MAKE_DIRECTORY "${malformed_include}")
file(WRITE "${malformed_include}/tcl.h" "#define TCL_VERSION \"9.0\"\n")
fsim_tcl_version_is_supported(
  "${malformed_include}"
  supported
  detected
)
if(supported OR NOT detected STREQUAL "")
  message(FATAL_ERROR "a header without TCL_PATCH_LEVEL should be rejected")
endif()

fsim_tcl_release_series("${FSIM_TCL_VERSION}" release_series)
if(NOT release_series STREQUAL "9.0")
  message(FATAL_ERROR "unexpected Tcl release series: ${release_series}")
endif()

fsim_tcl_static_library_name(
  "${FSIM_TCL_VERSION}"
  UNIX
  OFF
  unix_library
)
fsim_tcl_static_library_name(
  "${FSIM_TCL_VERSION}"
  MINGW
  OFF
  mingw_library
)
fsim_tcl_static_library_name(
  "${FSIM_TCL_VERSION}"
  WINDOWS
  ON
  windows_dll_crt_library
)
fsim_tcl_static_library_name(
  "${FSIM_TCL_VERSION}"
  WINDOWS
  OFF
  windows_static_crt_library
)
if(NOT unix_library STREQUAL "libtcl9.0.a")
  message(FATAL_ERROR "unexpected Unix Tcl library: ${unix_library}")
endif()
if(NOT mingw_library STREQUAL "libtcl90.a")
  message(FATAL_ERROR "unexpected MinGW Tcl library: ${mingw_library}")
endif()
if(NOT windows_dll_crt_library STREQUAL "tcl90s.lib")
  message(
    FATAL_ERROR
    "unexpected dynamic-CRT Tcl library: ${windows_dll_crt_library}"
  )
endif()
if(NOT windows_static_crt_library STREQUAL "tcl90sx.lib")
  message(
    FATAL_ERROR
    "unexpected static-CRT Tcl library: ${windows_static_crt_library}"
  )
endif()

fsim_tcl_windows_build_options(ON windows_dll_crt_options)
fsim_tcl_windows_build_options(OFF windows_static_crt_options)
if(
  NOT windows_dll_crt_options
  STREQUAL "OPTS=static,noembed,msvcrt"
)
  message(
    FATAL_ERROR
    "unexpected dynamic-CRT Tcl options: ${windows_dll_crt_options}"
  )
endif()
if(
  NOT windows_static_crt_options
  STREQUAL "OPTS=static,noembed,nomsvcrt"
)
  message(
    FATAL_ERROR
    "unexpected static-CRT Tcl options: ${windows_static_crt_options}"
  )
endif()

fsim_tcl_windows_build_targets(windows_build_targets)
if(
  NOT windows_build_targets
  STREQUAL "core;shell;dlls;libtclzip"
)
  message(
    FATAL_ERROR
    "unexpected Windows Tcl build targets: ${windows_build_targets}"
  )
endif()
