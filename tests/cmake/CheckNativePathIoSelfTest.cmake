# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)

foreach(FSIM_REQUIRED IN ITEMS FSIM_SOURCE_DIR FSIM_FIXTURE_DIR)
  if(NOT DEFINED ${FSIM_REQUIRED} OR "${${FSIM_REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${FSIM_REQUIRED} is required")
  endif()
endforeach()

set(FSIM_GATE "${FSIM_SOURCE_DIR}/cmake/CheckNativePathIo.cmake")

function(fsim_run_gate FSIM_ROOT FSIM_REVIEWED FSIM_OUTPUT_VARIABLE)
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      "-DFSIM_SOURCE_DIR=${FSIM_ROOT}"
      "-DFSIM_NATIVE_PATH_IO_TEST_REVIEWED=${FSIM_REVIEWED}"
      -P "${FSIM_GATE}"
    RESULT_VARIABLE FSIM_RESULT
    OUTPUT_VARIABLE FSIM_OUTPUT
    ERROR_VARIABLE FSIM_ERROR)
  set(${FSIM_OUTPUT_VARIABLE} "${FSIM_RESULT}\n${FSIM_OUTPUT}${FSIM_ERROR}"
    PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE "${FSIM_FIXTURE_DIR}")

set(FSIM_CLEAN "${FSIM_FIXTURE_DIR}/clean")
file(WRITE "${FSIM_CLEAN}/src/clean.cpp" [=[
#include "fsim/support/native_filesystem.hpp"
namespace fsim {
void clean(const std::filesystem::path& path) {
  std::error_code error;
  // std::filesystem::exists(path) in a comment is not a call.
  (void)support::native_fs::exists(path, error);
  auto input = support::native_fs::open_ifstream(path, std::ios::binary);
  auto stream = std::make_unique<std::fstream>(
      support::native_fs::open_fstream(path));
  std::ofstream later;
  support::native_fs::open(later, path, std::ios::out);
  const std::filesystem::directory_iterator end;
  auto iterator = support::native_fs::directory_iterator(path, error);
  const HANDLE file = CreateFileW(
      support::path_for_native_io(path).c_str(), 0, 0, nullptr, 0, 0, nullptr);
  auto* maps = std::fopen("/proc/self/maps", "r");
}
}  // namespace fsim
]=])
fsim_run_gate("${FSIM_CLEAN}" "src/clean.cpp|fopen|1" FSIM_CLEAN_RESULT)
if(NOT FSIM_CLEAN_RESULT MATCHES "^0\n")
  message(FATAL_ERROR "native path I/O gate rejected clean seams:\n${FSIM_CLEAN_RESULT}")
endif()

set(FSIM_BYPASS "${FSIM_FIXTURE_DIR}/bypass")
file(WRITE "${FSIM_BYPASS}/src/operation.cpp" [=[
void operation(const std::filesystem::path& path) {
  std::filesystem::create_directories(path);
}
]=])
file(WRITE "${FSIM_BYPASS}/src/alias.cpp" [=[
namespace stdfs = std::filesystem;
void alias(const stdfs::path& path) { stdfs::remove_all(path); }
]=])
file(WRITE "${FSIM_BYPASS}/src/iterator.cpp" [=[
void iterate(const std::filesystem::path& root) {
  for (std::filesystem::recursive_directory_iterator it(root), end; it != end; ++it) {}
}
]=])
file(WRITE "${FSIM_BYPASS}/src/stream.cpp" [=[
void stream(const std::filesystem::path& path) {
  std::ifstream input{path, std::ios::binary};
}
]=])
file(WRITE "${FSIM_BYPASS}/src/unique.cpp" [=[
auto unique(const std::filesystem::path& path) {
  return std::make_unique<std::ofstream>(path, std::ios::binary);
}
]=])
file(WRITE "${FSIM_BYPASS}/src/member.cpp" [=[
void member(std::ofstream& output, const std::filesystem::path& path) {
  output.open(path);
}
]=])
file(WRITE "${FSIM_BYPASS}/src/native.cpp" [=[
void native(const std::filesystem::path& path) {
  DeleteFileW(path.c_str());
}
]=])
file(WRITE "${FSIM_BYPASS}/src/using.cpp" [=[
using namespace std::filesystem;
]=])
fsim_run_gate("${FSIM_BYPASS}" "src/stale.cpp|fopen|1" FSIM_BYPASS_RESULT)
if(FSIM_BYPASS_RESULT MATCHES "^0\n")
  message(FATAL_ERROR "native path I/O gate accepted bypassing sources")
endif()
foreach(FSIM_EXPECTED IN ITEMS
    "src/operation.cpp: filesystem operation bypasses"
    "src/alias.cpp: filesystem operation bypasses"
    "src/iterator.cpp: directory iterator bypasses"
    "src/stream.cpp: file stream bypasses"
    "src/unique.cpp: file stream bypasses"
    "src/member.cpp: unreviewed native file call keeps an ordinary path: open x1"
    "src/native.cpp: unreviewed native file call keeps an ordinary path: DeleteFileW x1"
    "src/using.cpp: filesystem using-declaration bypasses"
    "reviewed native file call no longer matches its source: src/stale.cpp|fopen|1")
  string(FIND "${FSIM_BYPASS_RESULT}" "${FSIM_EXPECTED}" FSIM_OFFSET)
  if(FSIM_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "native path I/O gate missed '${FSIM_EXPECTED}':\n${FSIM_BYPASS_RESULT}")
  endif()
endforeach()

file(REMOVE_RECURSE "${FSIM_FIXTURE_DIR}")
message(STATUS "native path I/O gate fixtures passed")
