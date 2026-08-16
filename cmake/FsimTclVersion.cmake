# SPDX-License-Identifier: Apache-2.0

function(fsim_detect_tcl_header_version include_path output_variable)
  set(tcl_header "${include_path}/tcl.h")
  if(NOT EXISTS "${tcl_header}")
    set(${output_variable} "" PARENT_SCOPE)
    return()
  endif()

  file(
    STRINGS
    "${tcl_header}"
    tcl_patch_level_definition
    LIMIT_COUNT 1
    REGEX
      "^[ \t]*#[ \t]*define[ \t]+TCL_PATCH_LEVEL[ \t]+\"[0-9]+\\.[0-9]+\\.[0-9]+\""
  )
  if(
    tcl_patch_level_definition
    MATCHES "\"([0-9]+\\.[0-9]+\\.[0-9]+)\""
  )
    set(${output_variable} "${CMAKE_MATCH_1}" PARENT_SCOPE)
  else()
    set(${output_variable} "" PARENT_SCOPE)
  endif()
endfunction()

function(
  fsim_tcl_version_is_supported
  include_path
  result_variable
  version_variable
)
  fsim_detect_tcl_header_version(
    "${include_path}"
    detected_tcl_version
  )
  set(supported OFF)
  if(
    detected_tcl_version MATCHES "^9\\.0\\."
    AND NOT detected_tcl_version VERSION_LESS FSIM_TCL_VERSION
  )
    set(supported ON)
  endif()
  set(${result_variable} "${supported}" PARENT_SCOPE)
  set(${version_variable} "${detected_tcl_version}" PARENT_SCOPE)
endfunction()

function(fsim_tcl_release_series version output_variable)
  if(NOT version MATCHES "^([0-9]+)\\.([0-9]+)\\.[0-9]+$")
    message(FATAL_ERROR "invalid Tcl release version: ${version}")
  endif()
  set(${output_variable} "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}" PARENT_SCOPE)
endfunction()

function(
  fsim_tcl_static_library_name
  version
  platform
  msvc_runtime_is_dll
  output_variable
)
  fsim_tcl_release_series("${version}" release_series)
  if("${platform}" STREQUAL "WINDOWS")
    string(REPLACE "." "" compact_series "${release_series}")
    if(msvc_runtime_is_dll)
      set(name "tcl${compact_series}s.lib")
    else()
      set(name "tcl${compact_series}sx.lib")
    endif()
  elseif("${platform}" STREQUAL "MINGW")
    string(REPLACE "." "" compact_series "${release_series}")
    set(name "libtcl${compact_series}.a")
  elseif("${platform}" STREQUAL "UNIX")
    set(name "libtcl${release_series}.a")
  else()
    message(FATAL_ERROR "unsupported Tcl library platform: ${platform}")
  endif()
  set(${output_variable} "${name}" PARENT_SCOPE)
endfunction()

function(
  fsim_tcl_windows_build_options
  msvc_runtime_is_dll
  output_variable
)
  # Tcl's Windows static build normally embeds its standard-library scripts
  # into tclsh and then omits them from install-libraries. fsim links Tcl into
  # a different executable, so retain the scripts as relocatable files.
  if(msvc_runtime_is_dll)
    set(options "OPTS=static,noembed,msvcrt")
  else()
    set(options "OPTS=static,noembed,nomsvcrt")
  endif()
  set(${output_variable} "${options}" PARENT_SCOPE)
endfunction()

function(fsim_tcl_windows_build_targets output_variable)
  # install-libraries always copies Tcl's script archive, including for a
  # noembed build, so the narrowed Windows build must create it explicitly.
  set(
    targets
    core
    shell
    dlls
    libtclzip
  )
  set(${output_variable} "${targets}" PARENT_SCOPE)
endfunction()
