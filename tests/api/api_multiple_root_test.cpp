// SPDX-License-Identifier: Apache-2.0
#include "fsim/api.h"
#include "fsim/support/path.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

fsim_string_view_t text(const char* value) {
  return {value, std::char_traits<char>::length(value)};
}

int collect_object(
    fsim_session_t,
    const fsim_object_t object,
    void* user_data) {
  static_cast<std::vector<fsim_object_t>*>(user_data)->push_back(object);
  return 1;
}

} // namespace

void test_multiple_root_api(const std::filesystem::path& directory) {
  const auto manifest_path = directory / "multiple-roots.toml";
  {
    std::ofstream manifest(manifest_path);
    manifest << R"(
schema = 2
[project]
name = "api-multiple-roots"
time_resolution = "1ns"

[[project.top]]
target = "sv:work.tb"
alias = "left"

[[project.top]]
target = "sv:work.tb"
alias = "right"

[[source_set]]
language = "systemverilog"
standard = "2017"
library = "work"
files = ["tb.sv"]

[build]
cache_path = "multiple-root-cache"

[run]
max_deltas = 1000
)";
  }
  fsim_session_t session = FSIM_INVALID_SESSION;
  assert(fsim_session_create(nullptr, &session) == FSIM_STATUS_OK);
  assert(
      fsim_session_load_project(
          session, fsim::support::path_to_utf8(manifest_path).c_str())
      == FSIM_STATUS_OK);
  assert(fsim_session_check(session) == FSIM_STATUS_OK);
  assert(fsim_session_build(session) == FSIM_STATUS_OK);

  fsim_object_t root = FSIM_INVALID_OBJECT;
  assert(fsim_session_root(session, &root) == FSIM_STATUS_OK);
  fsim_object_info_t root_info{};
  root_info.struct_size = sizeof(root_info);
  root_info.api_version = FSIM_API_VERSION;
  assert(
      fsim_session_get_object_info(session, root, &root_info)
      == FSIM_STATUS_OK);
  const std::string root_name{
      root_info.name.data, root_info.name.size};
  assert(root_name == "$root");

  std::vector<fsim_object_t> root_children;
  assert(
      fsim_session_visit_children(
          session, root, collect_object, &root_children)
      == FSIM_STATUS_OK);
  assert(root_children.size() == 2);
  const auto find = [&](const char* path) {
    fsim_object_t object = FSIM_INVALID_OBJECT;
    assert(
        fsim_session_find_object(session, text(path), &object)
        == FSIM_STATUS_OK);
    return object;
  };
  const auto left = find("left");
  const auto right = find("right");
  assert(left != root && right != root && left != right);
  for (const auto object : {left, right}) {
    fsim_object_info_t info{};
    info.struct_size = sizeof(info);
    info.api_version = FSIM_API_VERSION;
    assert(
        fsim_session_get_object_info(session, object, &info)
        == FSIM_STATUS_OK);
    assert(info.kind == FSIM_OBJECT_SCOPE);
    assert(info.parent == root);
  }
  assert(find("left.q") != find("right.q"));
  assert(fsim_session_destroy(session) == FSIM_STATUS_OK);
}
