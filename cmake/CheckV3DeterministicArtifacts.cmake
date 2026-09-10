# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(layout "${FSIM_SOURCE_DIR}/packaging/v3-archive-layout.tsv")
file(STRINGS "${layout}" rows)
set(artifacts)
set(source_count 0)
set(binary_count 0)
foreach(row IN LISTS rows)
  if(row MATCHES "^#" OR row STREQUAL "")
    continue()
  endif()
  string(REPLACE "\t" ";" fields "${row}")
  list(LENGTH fields field_count)
  if(NOT field_count EQUAL 6)
    message(FATAL_ERROR "invalid v3 archive-layout row: ${row}")
  endif()
  list(GET fields 0 artifact)
  list(GET fields 1 kind)
  list(GET fields 2 platform)
  list(GET fields 3 compiler)
  list(GET fields 4 llvm)
  list(GET fields 5 owner)
  if(artifact IN_LIST artifacts OR NOT owner STREQUAL "B188-C16"
      OR NOT artifact MATCHES "^fsim-v3[.]0[.]0-[A-Za-z0-9_.+-]+[.]zip$")
    message(FATAL_ERROR "invalid v3 archive identity/owner: ${row}")
  endif()
  if(kind STREQUAL "source")
    if(NOT platform STREQUAL "portable" OR NOT compiler STREQUAL "none"
        OR NOT llvm STREQUAL "none")
      message(FATAL_ERROR "invalid v3 source archive row: ${row}")
    endif()
    math(EXPR source_count "${source_count} + 1")
  elseif(kind STREQUAL "binary")
    if(NOT platform MATCHES "^(linux|windows)-x86_64$"
        OR NOT compiler MATCHES "^(clang-22|llvm-mingw-20260616)$"
        OR NOT llvm MATCHES "^(off|22[.]1[.]8)$")
      message(FATAL_ERROR "invalid v3 binary archive row: ${row}")
    endif()
    math(EXPR binary_count "${binary_count} + 1")
  else()
    message(FATAL_ERROR "unknown v3 archive kind: ${kind}")
  endif()
  list(APPEND artifacts "${artifact}")
endforeach()
if(NOT source_count EQUAL 1 OR NOT binary_count EQUAL 4)
  message(FATAL_ERROR "expected one source and four binary v3 archives")
endif()

foreach(required IN ITEMS
    fsim-v3.0.0-source.zip
    fsim-v3.0.0-linux-x86_64-clang22-no-llvm.zip
    fsim-v3.0.0-linux-x86_64-clang22-llvm22.zip
    fsim-v3.0.0-windows-x86_64-llvm-mingw-no-llvm.zip
    fsim-v3.0.0-windows-x86_64-llvm-mingw-llvm22.zip)
  if(NOT required IN_LIST artifacts)
    message(FATAL_ERROR "missing v3 archive layout entry: ${required}")
  endif()
endforeach()

file(READ "${FSIM_SOURCE_DIR}/cmake/CreateDeterministicArchive.cmake" creator)
foreach(token IN ITEMS
    "archive manifest contains unsafe path"
    "archive manifest contains duplicate path"
    "archive manifest is not bytewise ordered"
    "create_deterministic_zip.py"
    "--mtime")
  if(NOT creator MATCHES "${token}")
    message(FATAL_ERROR "deterministic archive creator misses: ${token}")
  endif()
endforeach()
file(READ "${FSIM_SOURCE_DIR}/scripts/create_deterministic_zip.py" zipper)
foreach(token IN ITEMS
    "ZIP_DEFLATED"
    "compresslevel=9"
    "create_system = 3"
    "external_attr"
    "os.replace")
  if(NOT zipper MATCHES "${token}")
    message(FATAL_ERROR "deterministic zip helper misses: ${token}")
  endif()
endforeach()

file(SHA256 "${layout}" digest)
if(NOT digest STREQUAL "991e93c3c46359a3b4ed2388489fef4e1be5508b338778002c9212745f351426")
  message(FATAL_ERROR "v3 archive layout digest changed: ${digest}")
endif()
