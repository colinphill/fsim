# SPDX-License-Identifier: Apache-2.0

include(ExternalProject)
include("${CMAKE_CURRENT_LIST_DIR}/FsimTclVersion.cmake")

# Build the pinned Tcl release outside fsim's CMake graph when a system
# development package is unavailable. Tcl's native Unix and Windows builds are
# the supported upstream build paths, so this adapter deliberately keeps
# those details behind one imported target.
function(fsim_add_fetched_tcl)
  if(TARGET fsim_tcl_dependency)
    return()
  endif()

  fsim_tcl_release_series(
    "${FSIM_TCL_VERSION}"
    tcl_library_version
  )
  set(tcl_install "${CMAKE_BINARY_DIR}/_deps/fsim_tcl-install")
  set(
    tcl_source
    "${CMAKE_BINARY_DIR}/fsim_tcl_external-prefix/src/fsim_tcl_external"
  )
  file(MAKE_DIRECTORY "${tcl_install}/include" "${tcl_install}/lib")

  set(
    tcl_url
    "https://prdownloads.sourceforge.net/tcl/tcl${FSIM_TCL_VERSION}-src.tar.gz"
  )

  if(
    WIN32
    AND NOT MINGW
    AND (
      MSVC
      OR CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
    )
  )
    find_program(FSIM_NMAKE_EXECUTABLE NAMES nmake REQUIRED)
    fsim_tcl_windows_build_targets(tcl_build_targets)
    if(
      CMAKE_MSVC_RUNTIME_LIBRARY
      AND NOT CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL"
    )
      fsim_tcl_windows_build_options(OFF tcl_options)
      fsim_tcl_static_library_name(
        "${FSIM_TCL_VERSION}"
        WINDOWS
        OFF
        tcl_library_name
      )
    else()
      fsim_tcl_windows_build_options(ON tcl_options)
      fsim_tcl_static_library_name(
        "${FSIM_TCL_VERSION}"
        WINDOWS
        ON
        tcl_library_name
      )
    endif()
    set(tcl_library "${tcl_install}/lib/${tcl_library_name}")
    set(tcl_optimization "OPTIMIZATIONS=/O2 /GS /GL-")
    ExternalProject_Add(
      fsim_tcl_external
      URL "${tcl_url}"
      URL_HASH "SHA256=${FSIM_TCL_SOURCE_SHA256}"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      SOURCE_DIR "${tcl_source}"
      CONFIGURE_COMMAND ""
      BUILD_COMMAND
        "${FSIM_NMAKE_EXECUTABLE}" /f makefile.vc
        ${tcl_build_targets}
        "${tcl_options}" "${tcl_optimization}"
        "INSTALLDIR=<INSTALL_DIR>"
      INSTALL_COMMAND
        "${FSIM_NMAKE_EXECUTABLE}" /f makefile.vc
        install-binaries install-libraries
        "${tcl_options}" "${tcl_optimization}"
        "INSTALLDIR=<INSTALL_DIR>"
      SOURCE_SUBDIR win
      BINARY_DIR "${tcl_source}/win"
      INSTALL_DIR "${tcl_install}"
      INSTALL_BYPRODUCTS "${tcl_library}"
    )
  elseif(WIN32 AND MINGW)
    get_filename_component(tcl_compiler_bin "${CMAKE_C_COMPILER}" DIRECTORY)
    get_filename_component(tcl_toolchain_root "${tcl_compiler_bin}" DIRECTORY)
    find_program(
      FSIM_MINGW_MAKE_EXECUTABLE
      NAMES make mingw32-make
      HINTS "${tcl_toolchain_root}/busybox/bin" "${tcl_compiler_bin}"
      REQUIRED
    )
    find_program(
      FSIM_GIT_BASH_EXECUTABLE
      NAMES bash
      HINTS "$ENV{ProgramFiles}/Git/bin"
      REQUIRED
    )
    find_program(
      FSIM_MINGW_BASH_EXECUTABLE
      NAMES bash
      HINTS "${tcl_toolchain_root}/busybox/bin"
      NO_DEFAULT_PATH
      REQUIRED
    )
    find_program(
      FSIM_TCL_AR_EXECUTABLE
      NAMES llvm-ar
      HINTS "${tcl_compiler_bin}"
      NO_DEFAULT_PATH
      REQUIRED
    )
    find_program(
      FSIM_TCL_RANLIB_EXECUTABLE
      NAMES llvm-ranlib
      HINTS "${tcl_compiler_bin}"
      NO_DEFAULT_PATH
      REQUIRED
    )
    find_program(
      FSIM_TCL_RC_EXECUTABLE
      NAMES llvm-windres
      HINTS "${tcl_compiler_bin}"
      NO_DEFAULT_PATH
      REQUIRED
    )
    fsim_tcl_static_library_name(
      "${FSIM_TCL_VERSION}"
      MINGW
      OFF
      tcl_library_name
    )
    set(tcl_library "${tcl_install}/lib/${tcl_library_name}")
    set(tcl_binary "${CMAKE_BINARY_DIR}/fsim_tcl_mingw-build")
    file(MAKE_DIRECTORY "${tcl_binary}")
    set(tcl_mingw_script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/BuildTclMinGW.cmake")
    set(tcl_mingw_common_arguments
      "-DSOURCE_DIR=<SOURCE_DIR>"
      "-DBINARY_DIR=<BINARY_DIR>"
      "-DINSTALL_DIR=<INSTALL_DIR>"
      "-DCC=${CMAKE_C_COMPILER}"
      "-DAR=${FSIM_TCL_AR_EXECUTABLE}"
      "-DRANLIB=${FSIM_TCL_RANLIB_EXECUTABLE}"
      "-DRC=${FSIM_TCL_RC_EXECUTABLE}"
      "-DGIT_BASH=${FSIM_GIT_BASH_EXECUTABLE}"
      "-DMAKE=${FSIM_MINGW_MAKE_EXECUTABLE}"
      "-DMAKE_SHELL=${FSIM_MINGW_BASH_EXECUTABLE}"
      "-DTOOLCHAIN_BIN=${tcl_compiler_bin}"
      "-DJOBS=4"
    )
    ExternalProject_Add(
      fsim_tcl_external
      URL "${tcl_url}"
      URL_HASH "SHA256=${FSIM_TCL_SOURCE_SHA256}"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      SOURCE_DIR "${tcl_source}"
      CONFIGURE_COMMAND
        "${CMAKE_COMMAND}" ${tcl_mingw_common_arguments}
        -DMODE=configure -P "${tcl_mingw_script}"
      BUILD_COMMAND
        "${CMAKE_COMMAND}" ${tcl_mingw_common_arguments}
        -DMODE=build -P "${tcl_mingw_script}"
      INSTALL_COMMAND
        "${CMAKE_COMMAND}" ${tcl_mingw_common_arguments}
        -DMODE=install -P "${tcl_mingw_script}"
      BINARY_DIR "${tcl_binary}"
      INSTALL_DIR "${tcl_install}"
      INSTALL_BYPRODUCTS "${tcl_library}"
    )
  elseif(WIN32)
    message(
      FATAL_ERROR
      "Fetched Tcl on Windows requires either MSVC-compatible or MinGW compilers"
    )
  else()
    find_program(FSIM_MAKE_EXECUTABLE NAMES gmake make REQUIRED)
    find_package(Threads REQUIRED)
    find_package(ZLIB REQUIRED)
    fsim_tcl_static_library_name(
      "${FSIM_TCL_VERSION}"
      UNIX
      OFF
      tcl_library_name
    )
    set(tcl_library "${tcl_install}/lib/${tcl_library_name}")
    ExternalProject_Add(
      fsim_tcl_external
      URL "${tcl_url}"
      URL_HASH "SHA256=${FSIM_TCL_SOURCE_SHA256}"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      SOURCE_DIR "${tcl_source}"
      CONFIGURE_COMMAND
        "<SOURCE_DIR>/unix/configure"
        "--prefix=<INSTALL_DIR>"
        --disable-shared
        --enable-64bit
      BUILD_COMMAND "${FSIM_MAKE_EXECUTABLE}" -j8 binaries
      INSTALL_COMMAND
        "${FSIM_MAKE_EXECUTABLE}"
        install-binaries install-libraries install-headers
      INSTALL_DIR "${tcl_install}"
      INSTALL_BYPRODUCTS "${tcl_library}"
    )
  endif()

  add_library(fsim_tcl_dependency STATIC IMPORTED GLOBAL)
  set_target_properties(
    fsim_tcl_dependency
    PROPERTIES
      IMPORTED_LOCATION "${tcl_library}"
      INTERFACE_INCLUDE_DIRECTORIES "${tcl_install}/include"
  )
  add_dependencies(fsim_tcl_dependency fsim_tcl_external)

  if(WIN32)
    target_compile_definitions(fsim_tcl_dependency INTERFACE STATIC_BUILD)
    target_link_libraries(
      fsim_tcl_dependency
      INTERFACE
        advapi32
        kernel32
        netapi32
        user32
        userenv
        ws2_32
    )
  else()
    target_link_libraries(
      fsim_tcl_dependency
      INTERFACE
        Threads::Threads
        ZLIB::ZLIB
        "${CMAKE_DL_LIBS}"
        m
    )
  endif()

  # Tcl_Init sources init.tcl and the rest of the standard library at runtime.
  # Export its staged location so fsim can install those scripts beside a
  # relocatable executable instead of retaining Tcl's build-tree prefix.
  set(
    FSIM_FETCHED_TCL_LIBRARY_DIR
    "${tcl_install}/lib/tcl${tcl_library_version}"
    PARENT_SCOPE
  )
  set(
    FSIM_TCL_LIBRARY_VERSION
    "${tcl_library_version}"
    PARENT_SCOPE
  )
  set(
    FSIM_FETCHED_TCL_LICENSE_FILE
    "${tcl_source}/license.terms"
    PARENT_SCOPE
  )
endfunction()
