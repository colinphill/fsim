# SPDX-License-Identifier: Apache-2.0

# DPI-C imports call foreign functions through libffi; this module provides
# the `libffi` target when a usable libffi exists. Elsewhere the system
# libffi is used. Windows has none, so x86-64 Windows builds always compile
# the pinned release into a static library: the release archive then carries
# no extra DLL. libffi's autotools build needs a POSIX shell, so its sources
# are compiled here with the headers its configure would generate for
# x86_64-w64-mingw32.
#
# Included at directory scope: enable_language() cannot run in a function.

include_guard(GLOBAL)

set(FSIM_WITH_BUNDLED_LIBFFI OFF)

if(NOT WIN32)
  find_path(FSIM_FFI_INCLUDE_DIR ffi.h PATH_SUFFIXES ffi)
  find_library(FSIM_FFI_LIBRARY NAMES ffi_pic ffi)
  if(FSIM_FFI_INCLUDE_DIR AND FSIM_FFI_LIBRARY)
    add_library(libffi UNKNOWN IMPORTED)
    set_target_properties(
      libffi
      PROPERTIES
        IMPORTED_LOCATION "${FSIM_FFI_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${FSIM_FFI_INCLUDE_DIR}"
    )
  else()
    message(STATUS "libffi was not found; DPI-C imports cannot be called")
  endif()
  return()
endif()

if(MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8
   OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64)$")
  message(STATUS
    "No pinned libffi build for this Windows target; DPI-C imports cannot be called")
  return()
endif()

include(FetchContent)
FetchContent_Declare(
  fsim_libffi
  URL
    "https://github.com/libffi/libffi/releases/download/v${FSIM_LIBFFI_VERSION}/libffi-${FSIM_LIBFFI_VERSION}.tar.gz"
  URL_HASH
    "SHA256=${FSIM_LIBFFI_SOURCE_SHA256}"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# libffi has no CMakeLists.txt, so this only downloads and extracts it.
FetchContent_MakeAvailable(fsim_libffi)
# A disconnected configure that never fetched it has no source.
if(NOT EXISTS "${fsim_libffi_SOURCE_DIR}/include/ffi.h.in")
  message(STATUS
    "Pinned libffi ${FSIM_LIBFFI_VERSION} is unavailable; DPI-C imports cannot be called")
  return()
endif()
message(STATUS "Building pinned libffi ${FSIM_LIBFFI_VERSION} for DPI-C imports")
enable_language(ASM)

function(fsim_add_bundled_libffi source)
  set(generated "${CMAKE_BINARY_DIR}/generated/libffi")

  # The substitutions configure makes into ffi.h.in for X86_WIN64.
  string(REPLACE "." ";" parts "${FSIM_LIBFFI_VERSION}")
  list(GET parts 0 major)
  list(GET parts 1 minor)
  list(GET parts 2 micro)
  math(EXPR FFI_VERSION_NUMBER "${major} * 10000 + ${minor} * 100 + ${micro}")
  set(VERSION "${FSIM_LIBFFI_VERSION}")
  set(FFI_VERSION_STRING "${FSIM_LIBFFI_VERSION}")
  set(TARGET X86_WIN64)
  # MinGW's long double is the 80-bit x87 type, distinct from double.
  set(HAVE_LONG_DOUBLE 1)
  set(FFI_EXEC_TRAMPOLINE_TABLE 0)
  configure_file("${source}/include/ffi.h.in" "${generated}/ffi.h" @ONLY)
  configure_file(
    "${source}/src/x86/ffitarget.h" "${generated}/ffitarget.h" COPYONLY)

  # COFF has no hidden visibility or underscore prefix on x86-64, and win64.S
  # describes its frames with SEH directives, so no CFI is assumed.
  file(
    CONFIGURE
    OUTPUT "${generated}/fficonfig.h"
    CONTENT [[
/* fficonfig.h for x86_64-w64-mingw32, written by fsim's CMake build. */
#define EH_FRAME_FLAGS "a"
#define HAVE_INTTYPES_H 1
#define HAVE_LONG_DOUBLE 1
#define HAVE_MEMCPY 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1
#define HAVE_AS_X86_PCREL 1
#define PACKAGE "libffi"
#define PACKAGE_NAME "libffi"
#define PACKAGE_TARNAME "libffi"
#define PACKAGE_VERSION "@FSIM_LIBFFI_VERSION@"
#define PACKAGE_STRING "libffi @FSIM_LIBFFI_VERSION@"
#define VERSION "@FSIM_LIBFFI_VERSION@"
#define SIZEOF_DOUBLE 8
#define SIZEOF_LONG_DOUBLE 16
#define SIZEOF_SIZE_T 8
#define STDC_HEADERS 1
#ifdef LIBFFI_ASM
#define FFI_HIDDEN(name)
#else
#define FFI_HIDDEN
#endif
]]
    @ONLY
  )

  add_library(
    libffi
    STATIC
      "${source}/src/closures.c"
      "${source}/src/java_raw_api.c"
      "${source}/src/prep_cif.c"
      "${source}/src/raw_api.c"
      "${source}/src/tramp.c"
      "${source}/src/types.c"
      "${source}/src/x86/ffiw64.c"
      "${source}/src/x86/win64.S"
  )
  target_compile_definitions(libffi PUBLIC FFI_STATIC_BUILD)
  target_include_directories(libffi SYSTEM PUBLIC "${generated}")
  target_include_directories(
    libffi PRIVATE "${source}/include" "${source}/src/x86")
  set_target_properties(
    libffi PROPERTIES OUTPUT_NAME ffi POSITION_INDEPENDENT_CODE ON)
  # Upstream code: compiled with its own expectations, not fsim's warnings.
  target_compile_options(
    libffi PRIVATE $<$<COMPILE_LANGUAGE:C>:-w>)
endfunction()

fsim_add_bundled_libffi("${fsim_libffi_SOURCE_DIR}")
set(FSIM_WITH_BUNDLED_LIBFFI ON)
set(FSIM_BUNDLED_LIBFFI_LICENSE_FILE "${fsim_libffi_SOURCE_DIR}/LICENSE")
