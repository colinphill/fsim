# SPDX-License-Identifier: Apache-2.0

include(ExternalProject)
include("${CMAKE_CURRENT_LIST_DIR}/FsimTclVersion.cmake")

# Build the pinned Tcl release outside fsim's CMake graph when a system
# development package is unavailable. Tcl's native Unix and MSVC builds are
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

  if(WIN32)
    find_program(FSIM_NMAKE_EXECUTABLE NAMES nmake REQUIRED)
    if(
      CMAKE_MSVC_RUNTIME_LIBRARY
      AND NOT CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL"
    )
      set(tcl_options "OPTS=static,nomsvcrt")
      fsim_tcl_static_library_name(
        "${FSIM_TCL_VERSION}"
        WINDOWS
        OFF
        tcl_library_name
      )
    else()
      set(tcl_options "OPTS=static,msvcrt")
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
        "${FSIM_NMAKE_EXECUTABLE}" /f makefile.vc core shell dlls
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
      BUILD_COMMAND "${FSIM_MAKE_EXECUTABLE}" -j4 binaries
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
