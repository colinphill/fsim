// SPDX-License-Identifier: Apache-2.0
#include "fsim/api.h"

#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>

namespace {

struct CallbackCounts {
  int lifecycle{};
  int values{};
  int safe_points{};
  fsim_object_t last_safe_point_process{FSIM_INVALID_OBJECT};
  bool reentry_attempted{};
  fsim_status_t reentry_status{FSIM_STATUS_OK};
  bool callback_mutation_attempted{};
  fsim_status_t callback_mutation_status{FSIM_STATUS_OK};
  bool stop_on_safe_point{};
  bool stop_attempted{};
  fsim_status_t stop_status{FSIM_STATUS_OK};
  int assertions{};
  fsim_object_t assertion_process{FSIM_INVALID_OBJECT};
  fsim_severity_t assertion_severity{FSIM_SEVERITY_NOTE};
  std::string assertion_code;
  std::string assertion_message;
  std::string assertion_path;
  std::uint32_t assertion_line{};
  std::uint32_t assertion_column{};
  std::string manifest;
};

void lifecycle(
    fsim_session_t,
    fsim_lifecycle_event_t,
    void* user_data) {
  ++static_cast<CallbackCounts*>(user_data)->lifecycle;
}

void value_change(
    fsim_session_t session,
    fsim_object_t object,
    fsim_time_t,
    std::uint64_t,
    void* user_data) {
  auto& state = *static_cast<CallbackCounts*>(user_data);
  ++state.values;
  if (!state.callback_mutation_attempted) {
    state.callback_mutation_attempted = true;
    const fsim_string_view_t zero{"0", 1};
    state.callback_mutation_status =
        fsim_session_deposit(session, object, zero);
  }
}

void safe_point(
    fsim_session_t session,
    fsim_object_t process,
    fsim_time_t,
    std::uint64_t,
    void* user_data) {
  auto& state = *static_cast<CallbackCounts*>(user_data);
  ++state.safe_points;
  if (process != FSIM_INVALID_OBJECT) {
    state.last_safe_point_process = process;
  }
  if (!state.reentry_attempted) {
    state.reentry_attempted = true;
    state.reentry_status =
        fsim_session_load_project(session, state.manifest.c_str());
  }
  if (state.stop_on_safe_point && !state.stop_attempted) {
    state.stop_attempted = true;
    state.stop_status = fsim_session_request_stop(session);
  }
}

void assertion(
    fsim_session_t,
    fsim_object_t process,
    const fsim_diagnostic_t* diagnostic,
    void* user_data) {
  auto& state = *static_cast<CallbackCounts*>(user_data);
  ++state.assertions;
  state.assertion_process = process;
  state.assertion_severity = diagnostic->severity;
  state.assertion_code.assign(diagnostic->code.data, diagnostic->code.size);
  state.assertion_message.assign(
      diagnostic->message.data, diagnostic->message.size);
  state.assertion_path.assign(
      diagnostic->path.data, diagnostic->path.size);
  state.assertion_line = diagnostic->line;
  state.assertion_column = diagnostic->column;
}

int visit(fsim_session_t, fsim_object_t, void* user_data) {
  ++*static_cast<int*>(user_data);
  return 1;
}

fsim_string_view_t text(const char* value) {
  return {value, std::strlen(value)};
}

}  // namespace

int main() {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto directory =
      std::filesystem::temp_directory_path()
      / ("fsim-api-test-" + std::to_string(suffix));
  std::filesystem::create_directories(directory);
  {
    std::ofstream source(directory / "tb.sv");
    source << R"(
module tb;
  logic q;
  initial begin
    q = 1'b0;
    #2 q = 1'b1;
    #1 $finish;
  end
endmodule
)";
  }
  const auto manifest_path = directory / "fsim.toml";
  {
    std::ofstream manifest(manifest_path);
    manifest << R"(
schema = 1
[project]
name = "api-test"
top = "sv:work.tb"
time_resolution = "1ns"

[[source_set]]
language = "systemverilog"
standard = "2017"
library = "work"
files = ["tb.sv"]

[build]
cache_path = "cache"

[run]
max_deltas = 1000
)";
  }
  {
    std::ofstream source(directory / "assertion.vhd");
    source << R"(
entity assertion_test is
end entity;
architecture rtl of assertion_test is
  signal trigger : std_logic;
begin
  check: process(trigger)
  begin
    assert 0 = 1 report "api mismatch" severity failure;
  end process;
end architecture;
)";
  }
  const auto assertion_manifest_path = directory / "assertion.toml";
  {
    std::ofstream manifest(assertion_manifest_path);
    manifest << R"TOML(
schema = 1
[project]
name = "api-assertion-test"
top = "vhdl:work.assertion_test(rtl)"
time_resolution = "1ns"

[[source_set]]
language = "vhdl"
standard = "2008"
library = "work"
files = ["assertion.vhd"]

[build]
cache_path = "assertion-cache"

[run]
max_deltas = 1000
)TOML";
  }

  fsim_session_options_t options{};
  options.struct_size = sizeof(options);
  options.api_version = FSIM_API_VERSION;
  options.max_deltas = 1000;
  options.seed = 1;
  fsim_session_t session = FSIM_INVALID_SESSION;
  assert(fsim_session_create(&options, &session) == FSIM_STATUS_OK);
  assert(session != FSIM_INVALID_SESSION);

  CallbackCounts counts;
  counts.manifest = manifest_path.string();
  fsim_callbacks_t callbacks{};
  callbacks.struct_size = sizeof(callbacks);
  callbacks.api_version = FSIM_API_VERSION;
  callbacks.user_data = &counts;
  callbacks.safe_point = safe_point;
  callbacks.value_change = value_change;
  callbacks.assertion = assertion;
  callbacks.lifecycle = lifecycle;
  assert(fsim_session_set_callbacks(session, &callbacks) == FSIM_STATUS_OK);

  assert(
      fsim_session_load_project(session, manifest_path.string().c_str())
      == FSIM_STATUS_OK);
  assert(fsim_session_check(session) == FSIM_STATUS_OK);
  assert(fsim_session_build(session) == FSIM_STATUS_OK);
  assert(counts.lifecycle >= 1);

  fsim_object_t root = FSIM_INVALID_OBJECT;
  assert(fsim_session_root(session, &root) == FSIM_STATUS_OK);
  assert(root != FSIM_INVALID_OBJECT);
  int children = 0;
  assert(
      fsim_session_visit_children(session, root, visit, &children)
      == FSIM_STATUS_OK);
  assert(children == 2);

  fsim_object_t q = FSIM_INVALID_OBJECT;
  assert(
      fsim_session_find_object(session, text("q"), &q)
      == FSIM_STATUS_OK);
  fsim_object_info_t info{};
  info.struct_size = sizeof(info);
  info.api_version = FSIM_API_VERSION;
  assert(fsim_session_get_object_info(session, q, &info) == FSIM_STATUS_OK);
  assert(info.kind == FSIM_OBJECT_SIGNAL);
  assert(info.width == 1);

  std::size_t required = 0;
  assert(
      fsim_session_read_value(session, q, nullptr, 0, &required)
      == FSIM_STATUS_INVALID_ARGUMENT);
  assert(required == 2);
  char value[2]{};
  assert(
      fsim_session_read_value(session, q, value, sizeof(value), &required)
      == FSIM_STATUS_OK);
  assert(std::string(value) == "X");

  assert(fsim_session_deposit(session, q, text("0")) == FSIM_STATUS_OK);
  assert(fsim_session_force(session, q, text("1")) == FSIM_STATUS_OK);
  assert(fsim_session_deposit(session, q, text("0")) == FSIM_STATUS_OK);
  assert(
      fsim_session_read_value(session, q, value, sizeof(value), &required)
      == FSIM_STATUS_OK);
  assert(std::string(value) == "1");
  assert(fsim_session_release(session, q) == FSIM_STATUS_OK);
  assert(
      fsim_session_read_value(session, q, value, sizeof(value), &required)
      == FSIM_STATUS_OK);
  assert(std::string(value) == "0");

  assert(fsim_session_run(session, 10) == FSIM_STATUS_STOPPED);
  assert(
      fsim_session_read_value(session, q, value, sizeof(value), &required)
      == FSIM_STATUS_OK);
  assert(std::string(value) == "1");
  assert(counts.values >= 3);
  assert(counts.safe_points > 0);
  assert(counts.reentry_attempted);
  assert(counts.reentry_status == FSIM_STATUS_UNAVAILABLE);
  assert(counts.callback_mutation_attempted);
  assert(counts.callback_mutation_status == FSIM_STATUS_UNAVAILABLE);

  std::size_t diagnostic_count = 0;
  assert(
      fsim_session_diagnostic_count(session, &diagnostic_count)
      == FSIM_STATUS_OK);
  assert(diagnostic_count == 0);

  const auto old_root = root;
  const auto old_q = q;
  assert(fsim_session_build(session) == FSIM_STATUS_OK);
  assert(
      fsim_session_read_value(
          session, old_q, value, sizeof(value), &required)
      == FSIM_STATUS_INVALID_HANDLE);
  int stale_children = 0;
  assert(
      fsim_session_visit_children(
          session, old_root, visit, &stale_children)
      == FSIM_STATUS_INVALID_HANDLE);
  fsim_object_t rebuilt_q = FSIM_INVALID_OBJECT;
  assert(
      fsim_session_find_object(session, text("q"), &rebuilt_q)
      == FSIM_STATUS_OK);
  assert(rebuilt_q != old_q);

  assert(
      fsim_session_step(session, FSIM_STEP_STATEMENT)
      == FSIM_STATUS_OK);
  assert(
      fsim_session_read_value(
          session, rebuilt_q, value, sizeof(value), &required)
      == FSIM_STATUS_OK);
  assert(std::string(value) == "X");
  assert(counts.last_safe_point_process != FSIM_INVALID_OBJECT);
  fsim_object_info_t safe_process_info{};
  safe_process_info.struct_size = sizeof(safe_process_info);
  safe_process_info.api_version = FSIM_API_VERSION;
  assert(
      fsim_session_get_object_info(
          session,
          counts.last_safe_point_process,
          &safe_process_info)
      == FSIM_STATUS_OK);
  assert(safe_process_info.kind == FSIM_OBJECT_PROCESS);
  assert(
      fsim_session_step(session, FSIM_STEP_PROCESS)
      == FSIM_STATUS_OK);
  assert(
      fsim_session_read_value(
          session, rebuilt_q, value, sizeof(value), &required)
      == FSIM_STATUS_OK);
  assert(std::string(value) == "0");

  assert(fsim_session_build(session) == FSIM_STATUS_OK);
  assert(
      fsim_session_find_object(session, text("q"), &rebuilt_q)
      == FSIM_STATUS_OK);
  assert(
      fsim_session_step(session, FSIM_STEP_DELTA)
      == FSIM_STATUS_OK);
  assert(
      fsim_session_read_value(
          session, rebuilt_q, value, sizeof(value), &required)
      == FSIM_STATUS_OK);
  assert(std::string(value) == "0");
  assert(
      fsim_session_step(session, FSIM_STEP_TIME)
      == FSIM_STATUS_OK);
  assert(
      fsim_session_read_value(
          session, rebuilt_q, value, sizeof(value), &required)
      == FSIM_STATUS_OK);
  assert(std::string(value) == "1");
  assert(
      fsim_session_step(session, FSIM_STEP_TIME)
      == FSIM_STATUS_OK);
  assert(fsim_session_run(session, 10) == FSIM_STATUS_STOPPED);

  // Rebuild to obtain a fresh, resumable simulation and request a stop from
  // the synchronous safe-point callback. request_stop is the only control
  // operation intentionally allowed to cross this callback boundary.
  assert(fsim_session_build(session) == FSIM_STATUS_OK);
  fsim_object_t stopped_q = FSIM_INVALID_OBJECT;
  assert(
      fsim_session_find_object(session, text("q"), &stopped_q)
      == FSIM_STATUS_OK);
  counts.stop_on_safe_point = true;
  counts.stop_attempted = false;
  assert(fsim_session_run(session, 10) == FSIM_STATUS_STOPPED);
  assert(counts.stop_attempted);
  assert(counts.stop_status == FSIM_STATUS_OK);
  counts.stop_on_safe_point = false;
  assert(
      fsim_session_read_value(
          session, stopped_q, value, sizeof(value), &required)
      == FSIM_STATUS_OK);
  assert(std::string(value) == "X");
  assert(fsim_session_run(session, 10) == FSIM_STATUS_STOPPED);
  assert(
      fsim_session_read_value(
          session, stopped_q, value, sizeof(value), &required)
      == FSIM_STATUS_OK);
  assert(std::string(value) == "1");

  counts.manifest = assertion_manifest_path.string();
  assert(
      fsim_session_load_project(
          session, assertion_manifest_path.string().c_str())
      == FSIM_STATUS_OK);
  assert(fsim_session_check(session) == FSIM_STATUS_OK);
  assert(fsim_session_build(session) == FSIM_STATUS_OK);
  assert(fsim_session_run(session, 10) == FSIM_STATUS_RUNTIME_ERROR);
  assert(counts.assertions == 1);
  assert(counts.assertion_process != FSIM_INVALID_OBJECT);
  assert(counts.assertion_severity == FSIM_SEVERITY_FATAL);
  assert(counts.assertion_code == "FSIM-API-ASSERT-0001");
  assert(
      counts.assertion_message.find("api mismatch")
      != std::string::npos);
  assert(counts.assertion_path == (directory / "assertion.vhd").string());
  assert(counts.assertion_line == 9);
  assert(counts.assertion_column == 5);

  assert(fsim_session_destroy(session) == FSIM_STATUS_OK);

  std::error_code cleanup_error;
  std::filesystem::remove_all(directory, cleanup_error);
  std::cout << "C API tests passed\n";
}
