// SPDX-License-Identifier: Apache-2.0
#include "fsim/api.h"

#include <array>
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

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

int collect_object(
    fsim_session_t,
    const fsim_object_t object,
    void* user_data) {
  static_cast<std::vector<fsim_object_t>*>(user_data)
      ->push_back(object);
  return 1;
}

fsim_string_view_t text(const char* value) {
  return {value, std::strlen(value)};
}

}  // namespace

int main() {
  static_assert(FSIM_STRUCT_HEADER_SIZE == 8);
  static_assert(
      FSIM_OBJECT_INFO_V1_SIZE
      == offsetof(fsim_object_info_t, source_path));

  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto directory =
      std::filesystem::temp_directory_path()
      / ("fsim-api-test-" + std::to_string(suffix));
  std::filesystem::create_directories(directory);
  {
    std::ofstream source(directory / "tb.sv");
    source << R"(
module tb(output logic observed);
  logic q;
  logic call_result;
  initial begin : control
    logic state = 1'b0;
    begin : inner
      logic state = 1'b1;
      q = state;
    end : inner
    q = state;
    call_result = $isunknown(q);
    #2 begin : timed
      logic later = 1'b1;
      q = later;
    end : timed
    if (q) begin : selected
      logic chosen = 1'b1;
      q = chosen;
    end : selected
    else begin : unselected
      logic never_entered = 1'b0;
      q = never_entered;
    end : unselected
    #1 $finish;
  end : control
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

  // Input structures are append-only prefixes: a header-only caller receives
  // defaults and the implementation must not inspect unavailable tail fields.
  fsim_session_options_t prefix_options{};
  prefix_options.struct_size = FSIM_STRUCT_HEADER_SIZE;
  prefix_options.api_version = FSIM_API_VERSION;
  prefix_options.max_deltas = 1;
  prefix_options.seed = 99;
  fsim_session_t prefix_session = FSIM_INVALID_SESSION;
  assert(
      fsim_session_create(&prefix_options, &prefix_session)
      == FSIM_STATUS_OK);
  assert(fsim_session_destroy(prefix_session) == FSIM_STATUS_OK);
  prefix_options.struct_size = FSIM_STRUCT_HEADER_SIZE - 1;
  assert(
      fsim_session_create(&prefix_options, &prefix_session)
      == FSIM_STATUS_INCOMPATIBLE_ABI);
  prefix_options.struct_size = FSIM_STRUCT_HEADER_SIZE;
  prefix_options.api_version = FSIM_API_VERSION + 1;
  assert(
      fsim_session_create(&prefix_options, &prefix_session)
      == FSIM_STATUS_INCOMPATIBLE_ABI);

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

  // A callback outside the advertised prefix is ignored even when the backing
  // allocation happens to contain a non-null value.
  fsim_callbacks_t prefix_callbacks = callbacks;
  prefix_callbacks.struct_size = offsetof(fsim_callbacks_t, lifecycle);
  assert(
      fsim_session_set_callbacks(session, &prefix_callbacks)
      == FSIM_STATUS_OK);
  assert(
      fsim_session_load_project(session, manifest_path.string().c_str())
      == FSIM_STATUS_OK);
  assert(counts.lifecycle == 0);
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
  assert(children == 4);

  fsim_object_t process = FSIM_INVALID_OBJECT;
  assert(
      fsim_session_find_object(
          session, text("tb.process_0"), &process)
      == FSIM_STATUS_OK);
  std::vector<fsim_object_t> process_children;
  assert(
      fsim_session_visit_children(
          session, process, collect_object, &process_children)
      == FSIM_STATUS_OK);
  assert(process_children.size() == 5);

  fsim_object_t control_state = FSIM_INVALID_OBJECT;
  fsim_object_t inner_state = FSIM_INVALID_OBJECT;
  fsim_object_t timed_later = FSIM_INVALID_OBJECT;
  fsim_object_t selected_chosen = FSIM_INVALID_OBJECT;
  fsim_object_t unselected_local = FSIM_INVALID_OBJECT;
  assert(
      fsim_session_find_object(
          session,
          text("tb.process_0.control.state"),
          &control_state)
      == FSIM_STATUS_OK);
  assert(
      fsim_session_find_object(
          session,
          text("tb.process_0.control.inner.state"),
          &inner_state)
      == FSIM_STATUS_OK);
  assert(
      fsim_session_find_object(
          session,
          text("tb.process_0.control.timed.later"),
          &timed_later)
      == FSIM_STATUS_OK);
  assert(
      fsim_session_find_object(
          session,
          text("tb.process_0.control.selected.chosen"),
          &selected_chosen)
      == FSIM_STATUS_OK);
  assert(
      fsim_session_find_object(
          session,
          text("tb.process_0.control.unselected.never_entered"),
          &unselected_local)
      == FSIM_STATUS_OK);
  fsim_object_info_t local_info{};
  local_info.struct_size = sizeof(local_info);
  local_info.api_version = FSIM_API_VERSION;
  assert(
      fsim_session_get_object_info(
          session, inner_state, &local_info)
      == FSIM_STATUS_OK);
  assert(local_info.kind == FSIM_OBJECT_VARIABLE);
  assert(local_info.parent == process);
  assert(local_info.width == 1);
  assert(
      std::string(local_info.name.data, local_info.name.size)
      == "state");
  assert(
      std::string(
          local_info.full_name.data,
          local_info.full_name.size)
      == "tb.process_0.control.inner.state");
  assert(
      std::string(
          local_info.type_name.data,
          local_info.type_name.size)
      == "logic");
  assert(
      (local_info.flags & FSIM_OBJECT_FLAG_HAS_SOURCE)
      == FSIM_OBJECT_FLAG_HAS_SOURCE);
  assert(
      std::filesystem::path(
          std::string(
              local_info.source_path.data,
              local_info.source_path.size))
              .filename()
      == "tb.sv");
  assert(local_info.source_line == 8);
  assert(local_info.source_column > 0);

  // The original v1 object-info prefix remains accepted. Appended fields and
  // their capability flag are not touched when the caller advertises only the
  // old size.
  fsim_object_info_t legacy_local_info{};
  legacy_local_info.struct_size = FSIM_OBJECT_INFO_V1_SIZE;
  legacy_local_info.api_version = FSIM_API_VERSION;
  legacy_local_info.source_path = text("untouched");
  legacy_local_info.source_line = 0x11223344U;
  legacy_local_info.source_column = 0x55667788U;
  assert(
      fsim_session_get_object_info(
          session, inner_state, &legacy_local_info)
      == FSIM_STATUS_OK);
  assert(legacy_local_info.kind == FSIM_OBJECT_VARIABLE);
  assert(
      (legacy_local_info.flags & FSIM_OBJECT_FLAG_HAS_SOURCE) == 0);
  assert(
      std::string(
          legacy_local_info.source_path.data,
          legacy_local_info.source_path.size)
      == "untouched");
  assert(legacy_local_info.source_line == 0x11223344U);
  assert(legacy_local_info.source_column == 0x55667788U);

  fsim_object_info_t partial_source_info{};
  partial_source_info.struct_size =
      offsetof(fsim_object_info_t, source_line);
  partial_source_info.api_version = FSIM_API_VERSION;
  partial_source_info.source_line = 0xaabbccddU;
  partial_source_info.source_column = 0xeeff0011U;
  assert(
      fsim_session_get_object_info(
          session, inner_state, &partial_source_info)
      == FSIM_STATUS_OK);
  assert(
      (partial_source_info.flags & FSIM_OBJECT_FLAG_HAS_SOURCE)
      == FSIM_OBJECT_FLAG_HAS_SOURCE);
  assert(
      std::filesystem::path(
          std::string(
              partial_source_info.source_path.data,
              partial_source_info.source_path.size))
              .filename()
      == "tb.sv");
  assert(partial_source_info.source_line == 0xaabbccddU);
  assert(partial_source_info.source_column == 0xeeff0011U);

  struct FutureObjectInfo {
    fsim_object_info_t value;
    std::uint64_t future_tail;
  };
  FutureObjectInfo future_local_info{};
  future_local_info.value.struct_size = sizeof(future_local_info);
  future_local_info.value.api_version = FSIM_API_VERSION;
  future_local_info.future_tail = UINT64_C(0xdecafbadcafebeef);
  assert(
      fsim_session_get_object_info(
          session, inner_state, &future_local_info.value)
      == FSIM_STATUS_OK);
  assert(
      future_local_info.future_tail
      == UINT64_C(0xdecafbadcafebeef));

  fsim_object_info_t process_info{};
  process_info.struct_size = sizeof(process_info);
  process_info.api_version = FSIM_API_VERSION;
  assert(
      fsim_session_get_object_info(
          session, process, &process_info)
      == FSIM_STATUS_OK);
  assert(
      (process_info.flags & FSIM_OBJECT_FLAG_HAS_SOURCE)
      == FSIM_OBJECT_FLAG_HAS_SOURCE);
  assert(
      std::filesystem::path(
          std::string(
              process_info.source_path.data,
              process_info.source_path.size))
              .filename()
      == "tb.sv");
  assert(process_info.source_line == 5);
  assert(process_info.source_column > 0);
  std::size_t local_required = 123;
  assert(
      fsim_session_read_value(
          session,
          control_state,
          nullptr,
          0,
          &local_required)
      == FSIM_STATUS_UNAVAILABLE);
  assert(local_required == 0);

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
  assert(
      (info.flags & FSIM_OBJECT_FLAG_HAS_SOURCE)
      == FSIM_OBJECT_FLAG_HAS_SOURCE);
  assert(
      std::filesystem::path(
          std::string(info.source_path.data, info.source_path.size))
              .filename()
      == "tb.sv");
  assert(info.source_line == 3);
  assert(info.source_column > 0);

  fsim_object_t observed = FSIM_INVALID_OBJECT;
  assert(
      fsim_session_find_object(
          session, text("observed"), &observed)
      == FSIM_STATUS_OK);
  fsim_object_info_t port_info{};
  port_info.struct_size = sizeof(port_info);
  port_info.api_version = FSIM_API_VERSION;
  assert(
      fsim_session_get_object_info(
          session, observed, &port_info)
      == FSIM_STATUS_OK);
  assert(port_info.kind == FSIM_OBJECT_PORT);
  assert(
      (port_info.flags & FSIM_OBJECT_FLAG_HAS_SOURCE)
      == FSIM_OBJECT_FLAG_HAS_SOURCE);
  assert(
      std::filesystem::path(
          std::string(
              port_info.source_path.data,
              port_info.source_path.size))
              .filename()
      == "tb.sv");
  assert(port_info.source_line == 2);
  assert(port_info.source_column > 0);

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
  for (const auto& [local, expected] :
       std::array{
           std::pair{control_state, std::string_view{"0"}},
           std::pair{inner_state, std::string_view{"1"}},
           std::pair{timed_later, std::string_view{"1"}},
           std::pair{selected_chosen, std::string_view{"1"}},
       }) {
    assert(
        fsim_session_read_value(
            session, local, value, sizeof(value), &required)
        == FSIM_STATUS_OK);
    assert(std::string_view{value} == expected);
  }
  required = 123;
  assert(
      fsim_session_read_value(
          session,
          unselected_local,
          value,
          sizeof(value),
          &required)
      == FSIM_STATUS_UNAVAILABLE);
  assert(required == 0);
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
  const auto old_process = process;
  const auto old_control_state = control_state;
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
  assert(
      fsim_session_visit_children(
          session, old_process, visit, &stale_children)
      == FSIM_STATUS_INVALID_HANDLE);
  assert(
      fsim_session_read_value(
          session,
          old_control_state,
          value,
          sizeof(value),
          &required)
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

  // A statement step must stop at the call point after first stopping at the
  // containing assignment. The result remains unwritten until the following
  // statement step executes the call and reaches the delay statement.
  assert(fsim_session_build(session) == FSIM_STATUS_OK);
  assert(
      fsim_session_find_object(session, text("q"), &rebuilt_q)
      == FSIM_STATUS_OK);
  fsim_object_t call_result = FSIM_INVALID_OBJECT;
  assert(
      fsim_session_find_object(
          session, text("call_result"), &call_result)
      == FSIM_STATUS_OK);
  assert(
      fsim_session_step(session, FSIM_STEP_STATEMENT)
      == FSIM_STATUS_OK);
  assert(
      fsim_session_step(session, FSIM_STEP_STATEMENT)
      == FSIM_STATUS_OK);
  assert(
      fsim_session_step(session, FSIM_STEP_STATEMENT)
      == FSIM_STATUS_OK);
  assert(
      fsim_session_read_value(
          session, call_result, value, sizeof(value), &required)
      == FSIM_STATUS_OK);
  assert(std::string(value) == "X");
  assert(
      fsim_session_step(session, FSIM_STEP_STATEMENT)
      == FSIM_STATUS_OK);
  assert(
      fsim_session_read_value(
          session, call_result, value, sizeof(value), &required)
      == FSIM_STATUS_OK);
  assert(std::string(value) == "X");
  assert(
      fsim_session_step(session, FSIM_STEP_STATEMENT)
      == FSIM_STATUS_OK);
  assert(
      fsim_session_read_value(
          session, call_result, value, sizeof(value), &required)
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

  assert(
      fsim_session_diagnostic_count(session, &diagnostic_count)
      == FSIM_STATUS_OK);
  assert(diagnostic_count == 1);
  struct FutureDiagnostic {
    fsim_diagnostic_t value;
    std::uint64_t future_tail;
  };
  FutureDiagnostic future_diagnostic{};
  future_diagnostic.value.struct_size = sizeof(future_diagnostic);
  future_diagnostic.value.api_version = FSIM_API_VERSION;
  future_diagnostic.future_tail = UINT64_C(0x0123456789abcdef);
  assert(
      fsim_session_get_diagnostic(
          session, 0, &future_diagnostic.value)
      == FSIM_STATUS_OK);
  assert(
      std::string(
          future_diagnostic.value.code.data,
          future_diagnostic.value.code.size)
      == "FSIM-API-ASSERT-0001");
  assert(
      future_diagnostic.future_tail
      == UINT64_C(0x0123456789abcdef));

  assert(fsim_session_destroy(session) == FSIM_STATUS_OK);

  std::error_code cleanup_error;
  std::filesystem::remove_all(directory, cleanup_error);
  std::cout << "C API tests passed\n";
}
