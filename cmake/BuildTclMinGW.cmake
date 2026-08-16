# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)

foreach(required IN ITEMS
  MODE SOURCE_DIR BINARY_DIR INSTALL_DIR CC AR RANLIB RC
  GIT_BASH MAKE MAKE_SHELL TOOLCHAIN_BIN JOBS
)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

get_filename_component(git_root "${GIT_BASH}" DIRECTORY)
get_filename_component(git_root "${git_root}" DIRECTORY)
get_filename_component(make_dir "${MAKE}" DIRECTORY)
set(
  ENV{PATH}
  "${git_root}/usr/bin;${make_dir};${TOOLCHAIN_BIN};$ENV{PATH}"
)

file(MAKE_DIRECTORY "${BINARY_DIR}")

if(MODE STREQUAL "configure")
  execute_process(
    COMMAND
      "${CMAKE_COMMAND}" -E env
      "CC=${CC}" "AR=${AR}" "RANLIB=${RANLIB}" "RC=${RC}"
      "${GIT_BASH}" "${SOURCE_DIR}/win/configure"
      --build=x86_64-w64-mingw32
      --host=x86_64-w64-mingw32
      "--prefix=${INSTALL_DIR}"
      --disable-shared
      --enable-64bit=amd64
    WORKING_DIRECTORY "${BINARY_DIR}"
    COMMAND_ECHO STDOUT
    RESULT_VARIABLE result
  )
elseif(MODE STREQUAL "build")
  execute_process(
    COMMAND "${MAKE}" "-j${JOBS}" binaries "SHELL=${MAKE_SHELL}"
    WORKING_DIRECTORY "${BINARY_DIR}"
    COMMAND_ECHO STDOUT
    RESULT_VARIABLE result
  )
elseif(MODE STREQUAL "install")
  execute_process(
    COMMAND
      "${MAKE}" install-binaries install-libraries install-headers
      "SHELL=${MAKE_SHELL}"
      "TCL_EXE=${BINARY_DIR}/tclsh90s.exe"
    WORKING_DIRECTORY "${BINARY_DIR}"
    COMMAND_ECHO STDOUT
    RESULT_VARIABLE result
  )
else()
  message(FATAL_ERROR "unsupported Tcl MinGW build mode: ${MODE}")
endif()

if(NOT result EQUAL 0)
  message(FATAL_ERROR "Tcl MinGW ${MODE} failed with exit code ${result}")
endif()
