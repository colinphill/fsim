# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.25)

foreach(required IN ITEMS FSIM_SOURCE_DIR FSIM_ARCHIVE_AUDIT_BINARY_DIR
    FSIM_ARCHIVE_AUDIT_ARCHIVE FSIM_ARCHIVE_AUDIT_ROOT
    FSIM_ARCHIVE_AUDIT_REGRESSION_LOG FSIM_ARCHIVE_AUDIT_WORK_DIR
    FSIM_ARCHIVE_AUDIT_REQUIRED_CTESTS FSIM_ARCHIVE_AUDIT_REQUIRED_ENTRIES
    FSIM_ARCHIVE_AUDIT_TOOLCHAIN)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()
set(FSIM_TCL_MODE_CACHE
  "${FSIM_ARCHIVE_AUDIT_BINARY_DIR}/CMakeCache.txt")
if(NOT EXISTS "${FSIM_TCL_MODE_CACHE}")
  message(FATAL_ERROR "configured Tcl mode is unavailable: ${FSIM_TCL_MODE_CACHE}")
endif()
file(STRINGS "${FSIM_TCL_MODE_CACHE}" FSIM_TCL_MODE_ROWS
  REGEX "^FSIM_TCL_MODE:STRING=")
list(LENGTH FSIM_TCL_MODE_ROWS FSIM_TCL_MODE_COUNT)
if(NOT FSIM_TCL_MODE_COUNT EQUAL 1)
  message(FATAL_ERROR "configured Tcl mode is missing or duplicated in CMakeCache")
endif()
list(GET FSIM_TCL_MODE_ROWS 0 FSIM_TCL_MODE_ROW)
string(REGEX REPLACE "^FSIM_TCL_MODE:STRING=" "" FSIM_TCL_MODE_VALUE
  "${FSIM_TCL_MODE_ROW}")
string(TOUPPER "${FSIM_TCL_MODE_VALUE}" FSIM_TCL_MODE_VALUE)
if(NOT FSIM_TCL_MODE_VALUE MATCHES "^(AUTO|ON|OFF)$")
  message(FATAL_ERROR "configured Tcl mode is invalid: ${FSIM_TCL_MODE_VALUE}")
endif()
if(FSIM_TCL_MODE_VALUE STREQUAL "OFF")
  set(FSIM_OPTIONAL_REQUIRED_CTESTS fsim.application.tcl)
endif()
if(NOT FSIM_ARCHIVE_AUDIT_ROOT MATCHES "^[A-Za-z0-9][A-Za-z0-9._-]*$")
  message(FATAL_ERROR "v3 installed-archive root is unsafe")
endif()
get_filename_component(FSIM_BINARY_ROOT
  "${FSIM_ARCHIVE_AUDIT_BINARY_DIR}" REALPATH)
get_filename_component(FSIM_WORK_ROOT
  "${FSIM_ARCHIVE_AUDIT_WORK_DIR}" ABSOLUTE)
string(FIND "${FSIM_WORK_ROOT}/" "${FSIM_BINARY_ROOT}/"
  FSIM_WORK_PREFIX)
if(NOT FSIM_WORK_PREFIX EQUAL 0 OR FSIM_WORK_ROOT STREQUAL FSIM_BINARY_ROOT)
  message(FATAL_ERROR "v3 installed-archive work directory must be inside the build")
endif()
if(EXISTS "${FSIM_WORK_ROOT}")
  get_filename_component(FSIM_EXISTING_WORK_ROOT "${FSIM_WORK_ROOT}" REALPATH)
  string(FIND "${FSIM_EXISTING_WORK_ROOT}/" "${FSIM_BINARY_ROOT}/"
    FSIM_EXISTING_WORK_PREFIX)
  if(NOT FSIM_EXISTING_WORK_PREFIX EQUAL 0)
    message(FATAL_ERROR
      "v3 installed-archive work directory resolves outside the build")
  endif()
endif()
if(NOT EXISTS "${FSIM_ARCHIVE_AUDIT_ARCHIVE}"
    OR NOT EXISTS "${FSIM_ARCHIVE_AUDIT_REGRESSION_LOG}"
    OR NOT EXISTS "${FSIM_ARCHIVE_AUDIT_REQUIRED_CTESTS}"
    OR NOT EXISTS "${FSIM_ARCHIVE_AUDIT_REQUIRED_ENTRIES}")
  message(FATAL_ERROR "v3 installed-archive evidence is missing")
endif()

execute_process(
  COMMAND "${CMAKE_CTEST_COMMAND}"
    --test-dir "${FSIM_ARCHIVE_AUDIT_BINARY_DIR}"
    --show-only=json-v1
  RESULT_VARIABLE FSIM_CTEST_RESULT
  OUTPUT_VARIABLE FSIM_CTEST_JSON
  ERROR_VARIABLE FSIM_CTEST_ERROR)
if(NOT FSIM_CTEST_RESULT EQUAL 0)
  message(FATAL_ERROR
    "cannot enumerate v3 installed-archive CTests: ${FSIM_CTEST_ERROR}")
endif()
string(JSON FSIM_REGISTERED_TEST_COUNT LENGTH "${FSIM_CTEST_JSON}" tests)
if(FSIM_REGISTERED_TEST_COUNT LESS 1)
  message(FATAL_ERROR "v3 installed-archive CTest set is empty")
endif()
file(MAKE_DIRECTORY "${FSIM_WORK_ROOT}")
set(FSIM_REGISTERED_TESTS_FILE "${FSIM_WORK_ROOT}/registered-ctests.txt")
file(WRITE "${FSIM_REGISTERED_TESTS_FILE}" "")
math(EXPR FSIM_LAST_TEST "${FSIM_REGISTERED_TEST_COUNT} - 1")
foreach(FSIM_INDEX RANGE 0 ${FSIM_LAST_TEST})
  string(JSON FSIM_TEST_NAME GET "${FSIM_CTEST_JSON}"
    tests ${FSIM_INDEX} name)
  file(APPEND "${FSIM_REGISTERED_TESTS_FILE}" "${FSIM_TEST_NAME}\n")
endforeach()
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DFSIM_REQUIRED_CTESTS_FILE=${FSIM_ARCHIVE_AUDIT_REQUIRED_CTESTS}"
    "-DFSIM_OPTIONAL_REQUIRED_CTESTS=${FSIM_OPTIONAL_REQUIRED_CTESTS}"
    "-DFSIM_REGISTERED_CTESTS_FILE=${FSIM_REGISTERED_TESTS_FILE}"
    -P "${FSIM_SOURCE_DIR}/cmake/CheckRequiredCTestSet.cmake"
  RESULT_VARIABLE FSIM_REQUIRED_TEST_RESULT
  OUTPUT_VARIABLE FSIM_REQUIRED_TEST_OUTPUT
  ERROR_VARIABLE FSIM_REQUIRED_TEST_ERROR)
if(NOT FSIM_REQUIRED_TEST_RESULT EQUAL 0)
  message(FATAL_ERROR
    "v3 installed archive lost required CTests: "
    "${FSIM_REQUIRED_TEST_OUTPUT}${FSIM_REQUIRED_TEST_ERROR}")
endif()

file(READ "${FSIM_ARCHIVE_AUDIT_REGRESSION_LOG}" regression)
string(REGEX MATCHALL "100% tests passed, 0 tests failed out of [0-9]+"
  FSIM_PASS_SUMMARIES "${regression}")
list(LENGTH FSIM_PASS_SUMMARIES FSIM_PASS_COUNT)
if(NOT FSIM_PASS_COUNT EQUAL 1)
  message(FATAL_ERROR
    "v3 installed-archive regression log must contain one green CTest run")
endif()
list(GET FSIM_PASS_SUMMARIES 0 FSIM_PASS_SUMMARY)
if(NOT FSIM_PASS_SUMMARY STREQUAL
    "100% tests passed, 0 tests failed out of ${FSIM_REGISTERED_TEST_COUNT}")
  message(FATAL_ERROR
    "v3 installed-archive regression count differs from registered CTests")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E tar tf "${FSIM_ARCHIVE_AUDIT_ARCHIVE}"
  RESULT_VARIABLE list_result OUTPUT_VARIABLE listing ERROR_VARIABLE list_error)
if(NOT list_result EQUAL 0)
  message(FATAL_ERROR "cannot list v3 installed archive: ${list_error}")
endif()
string(REPLACE "\r\n" "\n" listing "${listing}")
string(REGEX REPLACE "\n$" "" listing "${listing}")
string(REPLACE "\n" ";" entries "${listing}")
list(LENGTH entries entry_count)
set(seen)
foreach(entry IN LISTS entries)
  string(FIND "${entry}" "${FSIM_ARCHIVE_AUDIT_ROOT}/" root_offset)
  if(NOT root_offset EQUAL 0 OR entry MATCHES "(^|/)\\.\\.?(/|$)"
      OR entry MATCHES "^[A-Za-z]:" OR entry MATCHES "\\\\"
      OR entry MATCHES "//" OR entry IN_LIST seen)
    message(FATAL_ERROR "unsafe or duplicate v3 installed archive entry")
  endif()
  list(APPEND seen "${entry}")
endforeach()

if(DEFINED FSIM_ARCHIVE_AUDIT_EXECUTABLE_SUFFIX)
  set(suffix "${FSIM_ARCHIVE_AUDIT_EXECUTABLE_SUFFIX}")
else()
  set(suffix "")
endif()
file(STRINGS "${FSIM_ARCHIVE_AUDIT_REQUIRED_ENTRIES}"
  FSIM_REQUIRED_ENTRY_LINES)
set(FSIM_REQUIRED_PATHS)
foreach(path IN LISTS FSIM_REQUIRED_ENTRY_LINES)
  if(path STREQUAL "" OR path STREQUAL
      "# SPDX-License-Identifier: Apache-2.0")
    continue()
  endif()
  string(REPLACE "@EXE@" "${suffix}" path "${path}")
  if(IS_ABSOLUTE "${path}" OR path MATCHES "(^|/)\\.\\.?(/|$)"
      OR path MATCHES "[/\\\\]$" OR path MATCHES "\\\\"
      OR path MATCHES "@" OR path IN_LIST FSIM_REQUIRED_PATHS)
    message(FATAL_ERROR "unsafe or duplicate required archive path: ${path}")
  endif()
  if(NOT "${FSIM_ARCHIVE_AUDIT_ROOT}/${path}" IN_LIST seen)
    message(FATAL_ERROR "v3 installed archive omits required entry: ${path}")
  endif()
  list(APPEND FSIM_REQUIRED_PATHS "${path}")
endforeach()
if(NOT FSIM_REQUIRED_PATHS)
  message(FATAL_ERROR "v3 installed archive has no required entry set")
endif()
foreach(path IN ITEMS
    share/doc/fsim/third-party/sqlite-3.53.4/LICENSE
    share/doc/fsim/third-party/sqlite-3.53.4/NOTICE
    share/doc/fsim/third-party/sqlite-3.53.4/SOURCE_MANIFEST.txt
    share/doc/fsim/third-party/sqlite-3.53.4/sqlite-3.53.4.spdx.json)
  if(NOT "${FSIM_ARCHIVE_AUDIT_ROOT}/${path}" IN_LIST seen)
    message(FATAL_ERROR "v3 installed archive omits SQLite provenance: ${path}")
  endif()
  list(APPEND FSIM_REQUIRED_PATHS "${path}")
endforeach()
# Windows archives carry the fsim toolchain beside fsim: the runtime fsim's
# binaries import, and the compiler, linker and debugger for plug-ins. Their
# installed commands run with PATH unset, so any DLL they load comes from the
# archive or the Windows system directories.
set(launcher)
if(suffix STREQUAL ".exe")
  include("${FSIM_SOURCE_DIR}/cmake/FsimWindowsRuntime.cmake")
  set(toolchain_docs "share/doc/fsim/third-party/${FSIM_WINDOWS_TOOLCHAIN}")
  foreach(path IN ITEMS
      bin/libc++.dll
      bin/libunwind.dll
      bin/libLLVM-22.dll
      bin/clang++.exe
      bin/clang-22.exe
      bin/ld.lld.exe
      bin/lldb.exe
      bin/lldb-dap.exe
      x86_64-w64-mingw32/include/c++/v1/format
      x86_64-w64-mingw32/include/stddef.h
      x86_64-w64-mingw32/lib/libucrt.a
      x86_64-w64-mingw32/share/mingw32/COPYING.MinGW-w64-runtime.txt
      ${toolchain_docs}/FSIM-TOOLCHAIN.txt
      ${toolchain_docs}/LICENSE
      ${toolchain_docs}/NOTICE
      ${toolchain_docs}/SOURCE_MANIFEST.txt
      ${toolchain_docs}/${FSIM_WINDOWS_TOOLCHAIN}.spdx.json)
    if(NOT "${FSIM_ARCHIVE_AUDIT_ROOT}/${path}" IN_LIST seen)
      message(FATAL_ERROR "v3 installed archive omits the Windows toolchain: ${path}")
    endif()
    list(APPEND FSIM_REQUIRED_PATHS "${path}")
  endforeach()
  set(gpl_tools "busybox/" "bin/mingw32-make.exe" "bin/widl.exe" "bin/gendef.exe")
  foreach(entry IN LISTS seen)
    foreach(path IN LISTS gpl_tools)
      string(FIND "${entry}" "${FSIM_ARCHIVE_AUDIT_ROOT}/${path}" offset)
      if(offset EQUAL 0)
        message(FATAL_ERROR "v3 installed archive carries a GPL build tool: ${entry}")
      endif()
    endforeach()
  endforeach()
  # Consumers add the prefix include directory to their search path, where
  # the toolchain's C headers would shadow libc++'s wrappers for them.
  foreach(entry IN LISTS seen)
    foreach(path IN ITEMS include/stddef.h include/c++/)
      string(FIND "${entry}" "${FSIM_ARCHIVE_AUDIT_ROOT}/${path}" offset)
      if(offset EQUAL 0)
        message(FATAL_ERROR
          "v3 installed archive puts toolchain headers in its include directory: ${entry}")
      endif()
    endforeach()
  endforeach()
  set(launcher "${CMAKE_COMMAND}" -E env --unset=PATH)
endif()

# The installed commands run from the workspace, so their executable path
# must not depend on the caller's working directory.
file(REMOVE_RECURSE "${FSIM_WORK_ROOT}")
file(MAKE_DIRECTORY "${FSIM_WORK_ROOT}/extract")
file(ARCHIVE_EXTRACT INPUT "${FSIM_ARCHIVE_AUDIT_ARCHIVE}"
  DESTINATION "${FSIM_WORK_ROOT}/extract")
set(prefix "${FSIM_WORK_ROOT}/extract/${FSIM_ARCHIVE_AUDIT_ROOT}")
foreach(path IN LISTS FSIM_REQUIRED_PATHS)
  if(NOT EXISTS "${prefix}/${path}")
    message(FATAL_ERROR "v3 installed archive omits ${path}")
  endif()
endforeach()

# Read SIZE bytes at OFFSET of FILE as an unsigned little-endian integer.
function(fsim_binary_field file offset size output)
  file(READ "${file}" hex OFFSET ${offset} LIMIT ${size} HEX)
  string(LENGTH "${hex}" length)
  math(EXPR expected "${size} * 2")
  if(NOT length EQUAL expected)
    message(FATAL_ERROR "v3 installed archive binary is truncated: ${file}")
  endif()
  set(value "")
  math(EXPR last "${length} - 2")
  foreach(index RANGE 0 ${last} 2)
    string(SUBSTRING "${hex}" ${index} 2 byte)
    string(PREPEND value "${byte}")
  endforeach()
  math(EXPR value "0x${value}")
  set(${output} ${value} PARENT_SCOPE)
endfunction()

# A stripped PE image has no COFF symbol table and no debug sections, whose
# long names would need that table.
function(fsim_pe_stripped file output)
  set(${output} TRUE PARENT_SCOPE)
  fsim_binary_field("${file}" 60 4 header)
  file(READ "${file}" signature OFFSET ${header} LIMIT 4 HEX)
  if(NOT signature STREQUAL "50450000")
    message(FATAL_ERROR "v3 installed archive has a malformed PE image: ${file}")
  endif()
  math(EXPR sections_field "${header} + 6")
  math(EXPR table_field "${header} + 12")
  math(EXPR symbols_field "${header} + 16")
  math(EXPR optional_field "${header} + 20")
  fsim_binary_field("${file}" ${sections_field} 2 sections)
  fsim_binary_field("${file}" ${table_field} 4 table)
  fsim_binary_field("${file}" ${symbols_field} 4 symbols)
  fsim_binary_field("${file}" ${optional_field} 2 optional)
  if(NOT table EQUAL 0 OR NOT symbols EQUAL 0)
    set(${output} FALSE PARENT_SCOPE)
  endif()
  if(sections EQUAL 0)
    return()
  endif()
  math(EXPR headers "${header} + 24 + ${optional}")
  math(EXPR length "${sections} * 40")
  file(READ "${file}" section_table OFFSET ${headers} LIMIT ${length} HEX)
  math(EXPR last "${sections} - 1")
  foreach(index RANGE 0 ${last})
    math(EXPR name_offset "${index} * 80")
    string(SUBSTRING "${section_table}" ${name_offset} 16 name)
    # "/" introduces a long name; ".debug" is the DWARF section prefix.
    if(name MATCHES "^(2f|2e6465627567)")
      set(${output} FALSE PARENT_SCOPE)
    endif()
  endforeach()
endfunction()

# A stripped ELF file has no SHT_SYMTAB section and no .debug sections.
function(fsim_elf_stripped file output)
  set(${output} TRUE PARENT_SCOPE)
  file(READ "${file}" class OFFSET 4 LIMIT 1 HEX)
  if(NOT class STREQUAL "02")
    message(FATAL_ERROR "v3 installed archive has a non-64-bit ELF file: ${file}")
  endif()
  fsim_binary_field("${file}" 40 8 table)
  fsim_binary_field("${file}" 58 2 entry_size)
  fsim_binary_field("${file}" 60 2 count)
  fsim_binary_field("${file}" 62 2 names_index)
  if(count EQUAL 0)
    return()
  endif()
  math(EXPR last "${count} - 1")
  foreach(index RANGE 0 ${last})
    math(EXPR type_offset "${table} + ${index} * ${entry_size} + 4")
    fsim_binary_field("${file}" ${type_offset} 4 type)
    if(type EQUAL 2)
      set(${output} FALSE PARENT_SCOPE)
    endif()
  endforeach()
  math(EXPR names_header "${table} + ${names_index} * ${entry_size}")
  math(EXPR names_offset_field "${names_header} + 24")
  math(EXPR names_size_field "${names_header} + 32")
  fsim_binary_field("${file}" ${names_offset_field} 8 names_offset)
  fsim_binary_field("${file}" ${names_size_field} 8 names_size)
  file(READ "${file}" names OFFSET ${names_offset} LIMIT ${names_size} HEX)
  string(REGEX REPLACE "(..)" " \\1" names "${names}")
  string(FIND "${names}" " 2e 64 65 62 75 67" debug)
  if(NOT debug EQUAL -1)
    set(${output} FALSE PARENT_SCOPE)
  endif()
endfunction()

# Release archives ship stripped executables and shared libraries; other
# configurations keep their symbols and debug information. The toolchain's
# runtime DLLs keep the debug information they are published with, so they
# match their recorded digests.
file(STRINGS "${FSIM_TCL_MODE_CACHE}" build_type
  REGEX "^CMAKE_BUILD_TYPE:[A-Z]+=")
set(toolchain_runtimes)
if(suffix STREQUAL ".exe")
  foreach(directory IN ITEMS bin x86_64-w64-mingw32/bin)
    foreach(name IN ITEMS libc++.dll libunwind.dll libomp.dll
        libwinpthread-1.dll libclang_rt.asan_dynamic-x86_64.dll)
      list(APPEND toolchain_runtimes
        "${FSIM_ARCHIVE_AUDIT_ROOT}/${directory}/${name}")
    endforeach()
  endforeach()
endif()
set(binaries)
if(build_type MATCHES "=Release$")
  set(binaries "${seen}")
endif()
set(unstripped)
foreach(entry IN LISTS binaries)
  set(path "${FSIM_WORK_ROOT}/extract/${entry}")
  if(IS_DIRECTORY "${path}" OR IS_SYMLINK "${path}"
      OR entry IN_LIST toolchain_runtimes)
    continue()
  endif()
  file(READ "${path}" magic LIMIT 4 HEX)
  if(magic STREQUAL "7f454c46")
    fsim_elf_stripped("${path}" stripped)
  elseif(magic MATCHES "^4d5a")
    fsim_pe_stripped("${path}" stripped)
  else()
    continue()
  endif()
  if(NOT stripped)
    list(APPEND unstripped "${entry}")
  endif()
endforeach()
if(unstripped)
  string(REPLACE ";" "\n  " unstripped "${unstripped}")
  message(FATAL_ERROR "v3 installed archive has unstripped binaries:\n  ${unstripped}")
endif()

execute_process(COMMAND ${launcher} "${prefix}/bin/fsim${suffix}" --version
  RESULT_VARIABLE version_result OUTPUT_VARIABLE version ERROR_VARIABLE version_error)
string(STRIP "${version}" version)
if(NOT version_result EQUAL 0 OR NOT version STREQUAL "fsim 3.0.0 (C API 1)")
  message(FATAL_ERROR
    "v3 installed archive version failed with ${version_result}: "
    "${version}${version_error}")
endif()
if(suffix STREQUAL ".exe")
  # The bundled compiler builds and links C++20 against the bundled runtime
  # with the prefix include directory searched, as for a plug-in, and nothing
  # else on PATH.
  set(compile_root "${FSIM_WORK_ROOT}/bundled-compiler")
  file(MAKE_DIRECTORY "${compile_root}")
  file(WRITE "${compile_root}/probe.cpp"
    "#include <format>\n#include <iostream>\n"
    "int main() { std::cout << std::format(\"BUNDLED_{}\", 20) << '\\n'; }\n")
  execute_process(
    COMMAND ${launcher} "${prefix}/bin/clang++.exe" -std=c++20
      "-I${prefix}/include"
      "${compile_root}/probe.cpp" -o "${compile_root}/probe.exe"
    RESULT_VARIABLE compile_result
    OUTPUT_VARIABLE compile_output ERROR_VARIABLE compile_error)
  if(NOT compile_result EQUAL 0)
    message(FATAL_ERROR
      "v3 installed archive compiler failed with ${compile_result}: "
      "${compile_output}${compile_error}")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "PATH=${prefix}/bin"
      "${compile_root}/probe.exe"
    RESULT_VARIABLE probe_result
    OUTPUT_VARIABLE probe_output ERROR_VARIABLE probe_error)
  if(NOT probe_result EQUAL 0 OR NOT probe_output MATCHES "BUNDLED_20")
    message(FATAL_ERROR
      "v3 installed archive compiler output failed with ${probe_result}: "
      "${probe_output}${probe_error}")
  endif()
  file(REMOVE_RECURSE "${compile_root}")
endif()
file(READ "${prefix}/lib/pkgconfig/fsim.pc" pc)
foreach(token IN ITEMS "Version: 3.0.0" "Libs: -L\${libdir} -lfsim_api")
  string(FIND "${pc}" "${token}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "v3 installed archive pkg-config metadata misses ${token}")
  endif()
endforeach()

set(workspace "${FSIM_WORK_ROOT}/workspace")
file(MAKE_DIRECTORY "${workspace}")
file(WRITE "${workspace}/top.sv"
  "module archive_top; initial begin $display(\"WORKSPACE_ARCHIVE_PASS\"); $finish; end endmodule\n")
function(fsim_installed_workspace_command expected_output)
  execute_process(COMMAND ${launcher} "${prefix}/bin/fsim${suffix}" ${ARGN}
    WORKING_DIRECTORY "${workspace}"
    TIMEOUT 7200
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR
      "installed workspace command failed (${ARGN}) with ${result}: "
      "${output}${error}")
  endif()
  if(NOT expected_output STREQUAL "")
    string(FIND "${output}" "${expected_output}" offset)
    if(offset EQUAL -1)
      message(FATAL_ERROR
        "installed workspace command lost ${expected_output}: ${output}${error}")
    endif()
  endif()
endfunction()
fsim_installed_workspace_command("" compile --library work top.sv)
if(NOT EXISTS "${workspace}/.fsim/libraries/work/library.sqlite3")
  message(FATAL_ERROR "installed compilation did not publish a workspace catalog")
endif()
fsim_installed_workspace_command("archive_top" library objects work)
file(REMOVE "${workspace}/top.sv")
fsim_installed_workspace_command("" elaborate work.archive_top)
fsim_installed_workspace_command("" elaborate work.archive_top --snapshot retained)
if(NOT EXISTS "${workspace}/.fsim/snapshots/default"
    OR NOT EXISTS "${workspace}/.fsim/snapshots/retained")
  message(FATAL_ERROR "installed elaboration did not publish both snapshots")
endif()
fsim_installed_workspace_command("" library delete work)
fsim_installed_workspace_command("WORKSPACE_ARCHIVE_PASS" simulate --engine interpreter)
fsim_installed_workspace_command("WORKSPACE_ARCHIVE_PASS"
  simulate --snapshot retained --engine interpreter)
if(suffix STREQUAL ".exe")
  # SystemC plug-ins build and link with the bundled compiler by default, not
  # the compiler that built fsim.
  file(COPY_FILE
    "${FSIM_SOURCE_DIR}/examples/three_language_hierarchy/mixed_bridge.cpp"
    "${workspace}/bridge.cpp")
  execute_process(
    COMMAND ${launcher} "${prefix}/bin/fsim${suffix}"
      systemc compile -v --library models bridge.cpp
    WORKING_DIRECTORY "${workspace}"
    TIMEOUT 7200
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  string(REPLACE "\\" "/" reported "${output}")
  string(TOLOWER "${reported}" reported)
  string(TOLOWER "  compiler: ${prefix}/bin/clang++.exe (default)\n" expected)
  string(FIND "${reported}" "${expected}" offset)
  if(NOT result EQUAL 0 OR offset EQUAL -1)
    message(FATAL_ERROR
      "installed SystemC compile did not use the bundled compiler "
      "(${result}): ${output}${error}")
  endif()
  fsim_installed_workspace_command("" systemc link --library models)
endif()
file(REMOVE_RECURSE "${workspace}")
file(REMOVE_RECURSE "${prefix}")
if(EXISTS "${prefix}")
  message(FATAL_ERROR "v3 installed archive removal check failed")
endif()
message(STATUS
  "v3 installed archive passed: ${FSIM_ARCHIVE_AUDIT_TOOLCHAIN}, "
  "${entry_count} entries, ${FSIM_REGISTERED_TEST_COUNT} tests")
