// SPDX-License-Identifier: Apache-2.0
#include "fsim/api.h"
#include "fsim/support/path.hpp"

#include <algorithm>
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
    int lifecycle { };
    int values { };
    int safe_points { };
    fsim_object_t last_safe_point_process { FSIM_INVALID_OBJECT };
    bool reentry_attempted { };
    fsim_status_t reentry_status { FSIM_STATUS_OK };
    bool callback_mutation_attempted { };
    fsim_status_t callback_mutation_status { FSIM_STATUS_OK };
    bool stop_on_safe_point { };
    bool stop_attempted { };
    fsim_status_t stop_status { FSIM_STATUS_OK };
    int assertions { };
    fsim_object_t assertion_process { FSIM_INVALID_OBJECT };
    fsim_severity_t assertion_severity { FSIM_SEVERITY_NOTE };
    std::string assertion_code;
    std::string assertion_message;
    std::string assertion_path;
    std::uint32_t assertion_line { };
    std::uint32_t assertion_column { };
    std::string first_assertion_code;
    fsim_severity_t first_assertion_severity { FSIM_SEVERITY_NOTE };
    int detailed_safe_points { };
    int scheduler_safe_points { };
    int statement_safe_points { };
    int call_safe_points { };
    int wait_safe_points { };
    int assertion_safe_points { };
    int entry_safe_points { };
    int suspend_safe_points { };
    std::uint32_t scheduler_phase_mask { };
    fsim_object_t detailed_process { FSIM_INVALID_OBJECT };
    std::string detailed_path;
    std::string provenance_unit;
    std::string provenance_standard;
    std::string provenance_profile;
    std::uint32_t detailed_line { };
    std::uint64_t detailed_instruction { UINT64_MAX };
    std::string manifest;
};

void lifecycle(
    fsim_session_t,
    fsim_lifecycle_event_t,
    void* user_data)
{
    ++static_cast<CallbackCounts*>(user_data)->lifecycle;
}

void value_change(
    fsim_session_t session,
    fsim_object_t object,
    fsim_time_t,
    std::uint64_t,
    void* user_data)
{
    auto& state = *static_cast<CallbackCounts*>(user_data);
    ++state.values;
    if (!state.callback_mutation_attempted) {
        state.callback_mutation_attempted = true;
        const fsim_string_view_t zero { "0", 1 };
        state.callback_mutation_status = fsim_session_deposit(session, object, zero);
    }
}

void safe_point(
    fsim_session_t session,
    fsim_object_t process,
    fsim_time_t,
    std::uint64_t,
    void* user_data)
{
    auto& state = *static_cast<CallbackCounts*>(user_data);
    ++state.safe_points;
    if (process != FSIM_INVALID_OBJECT) {
        state.last_safe_point_process = process;
    }
    if (!state.reentry_attempted) {
        state.reentry_attempted = true;
        state.reentry_status = fsim_session_load_project(session, state.manifest.c_str());
    }
    if (state.stop_on_safe_point && !state.stop_attempted) {
        state.stop_attempted = true;
        state.stop_status = fsim_session_request_stop(session);
    }
}

void safe_point_info(
    fsim_session_t,
    const fsim_safe_point_info_t* info,
    void* user_data)
{
    assert(info != nullptr);
    assert(info->struct_size == sizeof(*info));
    assert(info->api_version == FSIM_API_VERSION);
    auto& state = *static_cast<CallbackCounts*>(user_data);
    ++state.detailed_safe_points;
    switch (info->kind) {
    case FSIM_SAFE_POINT_SCHEDULER:
        ++state.scheduler_safe_points;
        assert(info->process == FSIM_INVALID_OBJECT);
        assert(info->phase != FSIM_SCHEDULER_PHASE_UNKNOWN);
        assert(info->instruction == UINT64_MAX);
        assert(info->source_path.size == 0);
        state.scheduler_phase_mask |= UINT32_C(1) << info->phase;
        break;
    case FSIM_SAFE_POINT_STATEMENT:
        ++state.statement_safe_points;
        break;
    case FSIM_SAFE_POINT_CALL:
        ++state.call_safe_points;
        break;
    case FSIM_SAFE_POINT_WAIT:
        ++state.wait_safe_points;
        break;
    case FSIM_SAFE_POINT_ASSERTION:
        ++state.assertion_safe_points;
        break;
    case FSIM_SAFE_POINT_PROCESS_ENTRY:
        ++state.entry_safe_points;
        break;
    case FSIM_SAFE_POINT_PROCESS_SUSPEND:
        ++state.suspend_safe_points;
        break;
    }
    if (info->kind != FSIM_SAFE_POINT_SCHEDULER) {
        assert(info->process != FSIM_INVALID_OBJECT);
        assert(info->phase == FSIM_SCHEDULER_PHASE_UNKNOWN);
        assert(info->instruction != UINT64_MAX);
        assert(info->source_path.data != nullptr);
        assert(info->source_path.size != 0);
        assert(info->source_line != 0);
        assert(info->source_column != 0);
        const auto extension = std::filesystem::path(std::string(
            info->source_path.data, info->source_path.size)).extension();
        if (extension == ".sv" || extension == ".v") {
            assert(info->provenance_unit.data != nullptr);
            assert(info->provenance_unit.size != 0);
            assert(info->provenance_source_path.size != 0);
            assert(info->provenance_language.size != 0);
            assert(info->provenance_standard.size != 0);
            assert(info->provenance_compatibility_profile.size != 0);
            assert(info->provenance_unit_id != UINT32_MAX);
            assert(info->provenance_source_id != UINT32_MAX);
            assert(info->provenance_source_line != 0);
            state.provenance_unit.assign(
                info->provenance_unit.data, info->provenance_unit.size);
            state.provenance_standard.assign(
                info->provenance_standard.data,
                info->provenance_standard.size);
            state.provenance_profile.assign(
                info->provenance_compatibility_profile.data,
                info->provenance_compatibility_profile.size);
        }
        state.detailed_process = info->process;
        state.detailed_path.assign(
            info->source_path.data, info->source_path.size);
        state.detailed_line = info->source_line;
        state.detailed_instruction = info->instruction;
    }
}

void assertion(
    fsim_session_t,
    fsim_object_t process,
    const fsim_diagnostic_t* diagnostic,
    void* user_data)
{
    auto& state = *static_cast<CallbackCounts*>(user_data);
    if (state.assertions == 0) {
        state.first_assertion_code.assign(
            diagnostic->code.data, diagnostic->code.size);
        state.first_assertion_severity = diagnostic->severity;
    }
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

int visit(fsim_session_t, fsim_object_t, void* user_data)
{
    ++*static_cast<int*>(user_data);
    return 1;
}

int visit_one(fsim_session_t, fsim_object_t, void* user_data)
{
    ++*static_cast<int*>(user_data);
    return 0;
}

int collect_object(
    fsim_session_t,
    const fsim_object_t object,
    void* user_data)
{
    static_cast<std::vector<fsim_object_t>*>(user_data)
        ->push_back(object);
    return 1;
}

fsim_string_view_t text(const char* value)
{
    return { value, std::strlen(value) };
}

} // namespace

void test_multiple_root_api(const std::filesystem::path& directory);
void test_mapped_library_api(const std::filesystem::path& directory);

int main()
{
    // FSIM-CONFORMANCE CF-COMMON-CAPI-001 source=SRC-COCOTB expectation=execute
    // FSIM-CONFORMANCE CF-COMMON-CALLBACK-001 source=SRC-COCOTB expectation=execute
    // FSIM-CONFORMANCE CF-COMMON-HANDLE-001 source=SRC-COCOTB expectation=execute
    static_assert(FSIM_STRUCT_HEADER_SIZE == 8);
    static_assert(
        FSIM_OBJECT_INFO_V1_SIZE
        == offsetof(fsim_object_info_t, source_path));

    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path()
        / fsim::support::path_from_utf8(
            "fsim-api-test-\xC3\xA9-" + std::to_string(suffix));
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
  generate
    if (1) begin : selected
      logic generated_value;
      initial generated_value = 1'b1;
      for (genvar i = 0; i < 1; i++) begin : lane
        child u();
      end
    end
  endgenerate
  logic [136:0] wide_value;
endmodule

module child;
  logic child_value;
  initial child_value = 1'b1;
  wire resolved;
  assign resolved = 1'b0;
  assign resolved = 1'b1;
endmodule
)";
    }
    const auto manifest_path = directory / "fsim.toml";
    {
        std::ofstream manifest(manifest_path);
        manifest << R"(
schema = 2
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
        std::ofstream source(directory / "named_systemc.cpp");
        source << R"CPP(
#include <systemc>

struct MetadataChannel final : sc_core::sc_prim_channel {
  explicit MetadataChannel(const char* name) : sc_prim_channel(name) {}
};

SC_MODULE(NamedChild) {
  sc_core::sc_signal<bool> state{"state"};
  sc_core::sc_event pulse{"pulse"};
  MetadataChannel metadata{"metadata"};
  sc_core::sc_export<sc_core::sc_signal_in_if<bool>> view{"view"};

  SC_CTOR(NamedChild) {
    view.bind(state);
    SC_METHOD(run);
  }

  void run() {
    state.write(true);
    pulse.notify();
  }
};

SC_MODULE(NamedTop) {
  sc_core::sc_in<bool> input{"input"};
  NamedChild child{"child"};

  SC_CTOR(NamedTop) {}
};

extern "C" fsim_sc_status_v1 fsim_plugin_init_v1(
    const fsim_sc_host_v1* host,
    fsim_sc_registrar_v1* registrar) {
  return fsim::systemc::register_module_factory<NamedTop>(
      host, registrar, "named");
}
)CPP";
    }
    const auto systemc_manifest_path = directory / "systemc.toml";
    {
        std::ofstream manifest(systemc_manifest_path);
        manifest
            << R"TOML(schema = 2
[project]
name = "api-systemc-test"
top = "systemc:api.named"
time_resolution = "1ns"

[[source_set]]
language = "systemc"
standard = "2023-subset"
library = "api"
files = ["named_systemc.cpp"]
include_directories = [")TOML"
            << (std::filesystem::path { FSIM_TEST_SOURCE_DIR } / "include")
                   .generic_string()
            << R"TOML("]

[build]
cache_path = "systemc-cache"

[run]
max_deltas = 1000
)TOML";
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
    report "api warning" severity warning;
    assert 0 = 1 report "api mismatch" severity failure;
  end process;
end architecture;
)";
    }
    const auto assertion_manifest_path = directory / "assertion.toml";
    {
        std::ofstream manifest(assertion_manifest_path);
        manifest << R"TOML(
schema = 2
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
    {
        std::ofstream source(directory / "logic9.vhd");
        source << R"(
entity api_logic9 is
  port (value : out std_logic_vector(7 downto 0));
end entity;
architecture rtl of api_logic9 is
begin
end architecture;
)";
    }
    const auto logic9_manifest_path = directory / "logic9.toml";
    {
        std::ofstream manifest(logic9_manifest_path);
        manifest << R"TOML(
schema = 2
[project]
name = "api-logic9-test"
top = "vhdl:work.api_logic9(rtl)"
time_resolution = "1ns"

[[source_set]]
language = "vhdl"
standard = "2008"
library = "work"
files = ["logic9.vhd"]

[build]
cache_path = "logic9-cache"

[run]
max_deltas = 1000
)TOML";
    }
    {
        std::ofstream source(directory / "severity_actions.sv");
        source << R"(
module severity_actions;
  initial begin
    $info("api info");
    $warning("api warning");
    $error("api error");
    assert (1'b0);
    $fatal("api fatal");
  end
endmodule
)";
    }
    const auto severity_manifest_path = directory / "severity.toml";
    {
        std::ofstream manifest(severity_manifest_path);
        manifest << R"TOML(
schema = 2
[project]
name = "api-severity-test"
top = "sv:work.severity_actions"
time_resolution = "1ns"

[[source_set]]
language = "systemverilog"
standard = "2017"
library = "work"
files = ["severity_actions.sv"]

[build]
cache_path = "severity-cache"

[run]
max_deltas = 1000
)TOML";
    }
    {
        std::ofstream source(directory / "api_random.sv");
        source << R"(
module api_random;
  logic [31:0] value;
  initial value = $urandom;
endmodule
)";
    }
    const auto random_manifest_path = directory / "random.toml";
    {
        std::ofstream manifest(random_manifest_path);
        manifest << R"(
schema = 2
[project]
name = "api-random-test"
top = "sv:work.api_random"
time_resolution = "1ns"
seed = 999

[[source_set]]
language = "systemverilog"
standard = "2017"
library = "work"
files = ["api_random.sv"]

[build]
cache_path = "random-cache"

[run]
max_deltas = 1000
)";
    }

    fsim_session_options_t options { };
    options.struct_size = sizeof(options);
    options.api_version = FSIM_API_VERSION;
    options.max_deltas = 1000;
    options.seed = 1;

    // Input structures are append-only prefixes: a header-only caller receives
    // defaults and the implementation must not inspect unavailable tail fields.
    fsim_session_options_t prefix_options { };
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

    fsim_session_t legacy_callback_session = FSIM_INVALID_SESSION;
    assert(
        fsim_session_create(&options, &legacy_callback_session)
        == FSIM_STATUS_OK);
    CallbackCounts legacy_counts;
    legacy_counts.manifest = fsim::support::path_to_utf8(manifest_path);
    fsim_callbacks_t legacy_callbacks { };
    legacy_callbacks.struct_size = FSIM_CALLBACKS_V1_SIZE;
    legacy_callbacks.api_version = FSIM_API_VERSION;
    legacy_callbacks.user_data = &legacy_counts;
    legacy_callbacks.safe_point = safe_point;
    legacy_callbacks.safe_point_info = safe_point_info;
    assert(
        fsim_session_set_callbacks(
            legacy_callback_session, &legacy_callbacks)
        == FSIM_STATUS_OK);
    assert(
        fsim_session_load_project(
            legacy_callback_session,
            fsim::support::path_to_utf8(manifest_path).c_str())
        == FSIM_STATUS_OK);
    assert(fsim_session_check(legacy_callback_session) == FSIM_STATUS_OK);
    assert(fsim_session_build(legacy_callback_session) == FSIM_STATUS_OK);
    assert(
        fsim_session_run(legacy_callback_session, 10)
        == FSIM_STATUS_STOPPED);
    assert(legacy_counts.safe_points > 0);
    assert(legacy_counts.detailed_safe_points == 0);
    assert(
        fsim_session_destroy(legacy_callback_session)
        == FSIM_STATUS_OK);

    fsim_session_t session = FSIM_INVALID_SESSION;
    assert(fsim_session_create(&options, &session) == FSIM_STATUS_OK);
    assert(session != FSIM_INVALID_SESSION);

    CallbackCounts counts;
    counts.manifest = fsim::support::path_to_utf8(manifest_path);
    fsim_callbacks_t callbacks { };
    callbacks.struct_size = sizeof(callbacks);
    callbacks.api_version = FSIM_API_VERSION;
    callbacks.user_data = &counts;
    callbacks.safe_point = safe_point;
    callbacks.value_change = value_change;
    callbacks.assertion = assertion;
    callbacks.lifecycle = lifecycle;
    callbacks.safe_point_info = safe_point_info;

    // A callback outside the advertised prefix is ignored even when the backing
    // allocation happens to contain a non-null value.
    fsim_callbacks_t prefix_callbacks = callbacks;
    prefix_callbacks.struct_size = offsetof(fsim_callbacks_t, lifecycle);
    assert(
        fsim_session_set_callbacks(session, &prefix_callbacks)
        == FSIM_STATUS_OK);
    assert(
        fsim_session_load_project(
            session, fsim::support::path_to_utf8(manifest_path).c_str())
        == FSIM_STATUS_OK);
    assert(counts.lifecycle == 0);
    assert(fsim_session_set_callbacks(session, &callbacks) == FSIM_STATUS_OK);
    assert(
        fsim_session_load_project(
            session, fsim::support::path_to_utf8(manifest_path).c_str())
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
    assert(children == 6);

    fsim_object_t generated_scope = FSIM_INVALID_OBJECT;
    fsim_object_t generated_lane = FSIM_INVALID_OBJECT;
    fsim_object_t generated_signal = FSIM_INVALID_OBJECT;
    fsim_object_t child_instance = FSIM_INVALID_OBJECT;
    fsim_object_t child_signal = FSIM_INVALID_OBJECT;
    fsim_object_t child_process = FSIM_INVALID_OBJECT;
    assert(
        fsim_session_find_object(
            session, text("tb.selected"), &generated_scope)
        == FSIM_STATUS_OK);
    assert(
        fsim_session_find_object(
            session,
            text("tb.selected.lane[0]"),
            &generated_lane)
        == FSIM_STATUS_OK);
    assert(
        fsim_session_find_object(
            session,
            text("tb.selected.generated_value"),
            &generated_signal)
        == FSIM_STATUS_OK);
    assert(
        fsim_session_find_object(
            session,
            text("tb.selected.lane[0].u"),
            &child_instance)
        == FSIM_STATUS_OK);
    assert(
        fsim_session_find_object(
            session,
            text("tb.selected.lane[0].u.child_value"),
            &child_signal)
        == FSIM_STATUS_OK);
    fsim_object_info_t generated_scope_info { };
    generated_scope_info.struct_size = sizeof(generated_scope_info);
    generated_scope_info.api_version = FSIM_API_VERSION;
    assert(
        fsim_session_get_object_info(
            session, generated_scope, &generated_scope_info)
        == FSIM_STATUS_OK);
    assert(generated_scope_info.kind == FSIM_OBJECT_SCOPE);
    assert(generated_scope_info.parent == root);
    assert(
        std::string(
            generated_scope_info.name.data,
            generated_scope_info.name.size)
        == "selected");
    assert(
        std::string(
            generated_scope_info.type_name.data,
            generated_scope_info.type_name.size)
        == "generate");
    std::vector<fsim_object_t> generated_children;
    assert(
        fsim_session_visit_children(
            session,
            generated_scope,
            collect_object,
            &generated_children)
        == FSIM_STATUS_OK);
    assert(generated_children.size() == 3);
    assert(
        std::find(
            generated_children.begin(),
            generated_children.end(),
            generated_lane)
        != generated_children.end());
    assert(
        std::find(
            generated_children.begin(),
            generated_children.end(),
            generated_signal)
        != generated_children.end());
    fsim_object_t generated_process = FSIM_INVALID_OBJECT;
    for (const auto child : generated_children) {
        fsim_object_info_t child_info { };
        child_info.struct_size = sizeof(child_info);
        child_info.api_version = FSIM_API_VERSION;
        assert(
            fsim_session_get_object_info(session, child, &child_info)
            == FSIM_STATUS_OK);
        if (child_info.kind == FSIM_OBJECT_PROCESS) {
            generated_process = child;
        }
    }
    assert(generated_process != FSIM_INVALID_OBJECT);
    fsim_object_info_t generated_signal_info { };
    generated_signal_info.struct_size = sizeof(generated_signal_info);
    generated_signal_info.api_version = FSIM_API_VERSION;
    assert(
        fsim_session_get_object_info(
            session, generated_signal, &generated_signal_info)
        == FSIM_STATUS_OK);
    assert(generated_signal_info.parent == generated_scope);
    fsim_object_info_t generated_process_info { };
    generated_process_info.struct_size = sizeof(generated_process_info);
    generated_process_info.api_version = FSIM_API_VERSION;
    assert(
        fsim_session_get_object_info(
            session, generated_process, &generated_process_info)
        == FSIM_STATUS_OK);
    assert(generated_process_info.parent == generated_scope);
    assert(
        std::string(
            generated_process_info.name.data,
            generated_process_info.name.size)
        == "$process");
    fsim_object_t found_generated_process = FSIM_INVALID_OBJECT;
    assert(
        fsim_session_find_object(
            session,
            generated_process_info.full_name,
            &found_generated_process)
        == FSIM_STATUS_OK);
    assert(found_generated_process == generated_process);
    fsim_object_info_t generated_lane_info { };
    generated_lane_info.struct_size = sizeof(generated_lane_info);
    generated_lane_info.api_version = FSIM_API_VERSION;
    assert(
        fsim_session_get_object_info(
            session, generated_lane, &generated_lane_info)
        == FSIM_STATUS_OK);
    assert(generated_lane_info.parent == generated_scope);
    assert(
        std::string(
            generated_lane_info.name.data,
            generated_lane_info.name.size)
        == "lane[0]");
    std::vector<fsim_object_t> lane_children;
    assert(
        fsim_session_visit_children(
            session,
            generated_lane,
            collect_object,
            &lane_children)
        == FSIM_STATUS_OK);
    assert(lane_children.size() == 1);
    assert(lane_children.front() == child_instance);
    fsim_object_info_t child_instance_info { };
    child_instance_info.struct_size = sizeof(child_instance_info);
    child_instance_info.api_version = FSIM_API_VERSION;
    assert(
        fsim_session_get_object_info(
            session, child_instance, &child_instance_info)
        == FSIM_STATUS_OK);
    assert(child_instance_info.kind == FSIM_OBJECT_SCOPE);
    assert(child_instance_info.parent == generated_lane);
    assert(
        std::string(
            child_instance_info.name.data,
            child_instance_info.name.size)
        == "u");
    assert(
        std::string(
            child_instance_info.full_name.data,
            child_instance_info.full_name.size)
        == "tb.selected.lane[0].u");
    assert(
        std::string(
            child_instance_info.type_name.data,
            child_instance_info.type_name.size)
            .find("child")
        != std::string::npos);
    assert(
        (child_instance_info.flags & FSIM_OBJECT_FLAG_HAS_SOURCE)
        == FSIM_OBJECT_FLAG_HAS_SOURCE);
    assert(
        (child_instance_info.flags & FSIM_OBJECT_FLAG_HAS_PROVENANCE)
        == FSIM_OBJECT_FLAG_HAS_PROVENANCE);
    assert(std::string(
        child_instance_info.provenance_unit.data,
        child_instance_info.provenance_unit.size) == "work::child");
    assert(std::string(
        child_instance_info.provenance_language.data,
        child_instance_info.provenance_language.size) == "systemverilog");
    assert(std::string(
        child_instance_info.provenance_standard.data,
        child_instance_info.provenance_standard.size)
        == "systemverilog-2017");
    assert(std::string(
        child_instance_info.provenance_compatibility_profile.data,
        child_instance_info.provenance_compatibility_profile.size) == "none");
    assert(child_instance_info.provenance_unit_id != UINT32_MAX);
    assert(child_instance_info.provenance_source_id != UINT32_MAX);
    assert(child_instance_info.provenance_source_line != 0);
    std::vector<fsim_object_t> child_instance_children;
    assert(
        fsim_session_visit_children(
            session,
            child_instance,
            collect_object,
            &child_instance_children)
        == FSIM_STATUS_OK);
    assert(child_instance_children.size() == 5);
    for (const auto child : child_instance_children) {
        fsim_object_info_t child_info { };
        child_info.struct_size = sizeof(child_info);
        child_info.api_version = FSIM_API_VERSION;
        assert(
            fsim_session_get_object_info(session, child, &child_info)
            == FSIM_STATUS_OK);
        if (child_info.kind == FSIM_OBJECT_PROCESS) {
            child_process = child;
        }
    }
    assert(child_process != FSIM_INVALID_OBJECT);
    assert(
        std::find(
            child_instance_children.begin(),
            child_instance_children.end(),
            child_signal)
        != child_instance_children.end());
    assert(
        std::find(
            child_instance_children.begin(),
            child_instance_children.end(),
            child_process)
        != child_instance_children.end());
    fsim_object_info_t child_signal_info { };
    child_signal_info.struct_size = sizeof(child_signal_info);
    child_signal_info.api_version = FSIM_API_VERSION;
    assert(
        fsim_session_get_object_info(
            session, child_signal, &child_signal_info)
        == FSIM_STATUS_OK);
    assert(child_signal_info.parent == child_instance);
    fsim_object_info_t child_process_info { };
    child_process_info.struct_size = sizeof(child_process_info);
    child_process_info.api_version = FSIM_API_VERSION;
    assert(
        fsim_session_get_object_info(
            session, child_process, &child_process_info)
        == FSIM_STATUS_OK);
    assert(child_process_info.parent == child_instance);
    fsim_object_t found_child_process = FSIM_INVALID_OBJECT;
    assert(
        fsim_session_find_object(
            session,
            child_process_info.full_name,
            &found_child_process)
        == FSIM_STATUS_OK);
    assert(found_child_process == child_process);

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
    assert(process_children.size() == 1);

    fsim_object_t control_scope = FSIM_INVALID_OBJECT;
    fsim_object_t inner_scope = FSIM_INVALID_OBJECT;
    fsim_object_t selected_scope = FSIM_INVALID_OBJECT;
    fsim_object_t unselected_scope = FSIM_INVALID_OBJECT;
    fsim_object_t control_state = FSIM_INVALID_OBJECT;
    fsim_object_t inner_state = FSIM_INVALID_OBJECT;
    fsim_object_t timed_later = FSIM_INVALID_OBJECT;
    fsim_object_t selected_chosen = FSIM_INVALID_OBJECT;
    fsim_object_t unselected_local = FSIM_INVALID_OBJECT;
    assert(
        fsim_session_find_object(
            session,
            text("tb.process_0.control"),
            &control_scope)
        == FSIM_STATUS_OK);
    assert(process_children.front() == control_scope);
    assert(
        fsim_session_find_object(
            session,
            text("tb.process_0.control.inner"),
            &inner_scope)
        == FSIM_STATUS_OK);
    assert(
        fsim_session_find_object(
            session,
            text("tb.process_0.control.selected"),
            &selected_scope)
        == FSIM_STATUS_OK);
    assert(
        fsim_session_find_object(
            session,
            text("tb.process_0.control.unselected"),
            &unselected_scope)
        == FSIM_STATUS_OK);
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
    fsim_object_info_t local_info { };
    local_info.struct_size = sizeof(local_info);
    local_info.api_version = FSIM_API_VERSION;
    assert(
        fsim_session_get_object_info(
            session, inner_state, &local_info)
        == FSIM_STATUS_OK);
    assert(local_info.kind == FSIM_OBJECT_VARIABLE);
    assert(local_info.parent == inner_scope);
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
    assert(
        (local_info.flags & FSIM_OBJECT_FLAG_INITIALIZED) == 0);

    fsim_object_info_t control_scope_info { };
    control_scope_info.struct_size = sizeof(control_scope_info);
    control_scope_info.api_version = FSIM_API_VERSION;
    assert(
        fsim_session_get_object_info(
            session, control_scope, &control_scope_info)
        == FSIM_STATUS_OK);
    assert(control_scope_info.kind == FSIM_OBJECT_SCOPE);
    assert(control_scope_info.parent == process);
    assert(control_scope_info.width == 0);
    assert(
        std::string(
            control_scope_info.name.data,
            control_scope_info.name.size)
        == "control");
    assert(
        std::string(
            control_scope_info.full_name.data,
            control_scope_info.full_name.size)
        == "tb.process_0.control");
    assert(
        std::string(
            control_scope_info.type_name.data,
            control_scope_info.type_name.size)
        == "scope");
    assert(
        (control_scope_info.flags & FSIM_OBJECT_FLAG_HAS_SOURCE)
        == FSIM_OBJECT_FLAG_HAS_SOURCE);
    assert(control_scope_info.source_line == 6);
    assert(
        (control_scope_info.flags & FSIM_OBJECT_FLAG_ENTERED) == 0);

    fsim_object_info_t inner_scope_info { };
    inner_scope_info.struct_size = sizeof(inner_scope_info);
    inner_scope_info.api_version = FSIM_API_VERSION;
    assert(
        fsim_session_get_object_info(
            session, inner_scope, &inner_scope_info)
        == FSIM_STATUS_OK);
    assert(inner_scope_info.kind == FSIM_OBJECT_SCOPE);
    assert(inner_scope_info.parent == control_scope);
    assert(inner_scope_info.source_line == 8);

    std::vector<fsim_object_t> control_children;
    assert(
        fsim_session_visit_children(
            session,
            control_scope,
            collect_object,
            &control_children)
        == FSIM_STATUS_OK);
    assert(control_children.size() == 5);
    int stopped_children = 0;
    assert(
        fsim_session_visit_children(
            session,
            control_scope,
            visit_one,
            &stopped_children)
        == FSIM_STATUS_OK);
    assert(stopped_children == 1);
    std::vector<fsim_object_t> inner_children;
    assert(
        fsim_session_visit_children(
            session,
            inner_scope,
            collect_object,
            &inner_children)
        == FSIM_STATUS_OK);
    assert(inner_children.size() == 1);
    assert(inner_children.front() == inner_state);

    std::size_t scope_required = 123;
    assert(
        fsim_session_read_value(
            session,
            control_scope,
            nullptr,
            0,
            &scope_required)
        == FSIM_STATUS_INVALID_HANDLE);
    assert(scope_required == 0);

    // The original v1 object-info prefix remains accepted. Appended fields and
    // their capability flag are not touched when the caller advertises only the
    // old size.
    fsim_object_info_t legacy_local_info { };
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

    fsim_object_info_t partial_source_info { };
    partial_source_info.struct_size = offsetof(fsim_object_info_t, source_line);
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
    FutureObjectInfo future_local_info { };
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

    fsim_object_info_t process_info { };
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
    fsim_object_info_t info { };
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

    fsim_object_t resolved = FSIM_INVALID_OBJECT;
    assert(
        fsim_session_find_object(
            session,
            text("tb.selected.lane[0].u.resolved"),
            &resolved)
        == FSIM_STATUS_OK);
    fsim_object_info_t resolved_info { };
    resolved_info.struct_size = sizeof(resolved_info);
    resolved_info.api_version = FSIM_API_VERSION;
    assert(
        fsim_session_get_object_info(
            session, resolved, &resolved_info)
        == FSIM_STATUS_OK);
    assert(resolved_info.kind == FSIM_OBJECT_SIGNAL);
    assert(
        (resolved_info.flags & FSIM_OBJECT_FLAG_RESOLVED)
        == FSIM_OBJECT_FLAG_RESOLVED);
    std::vector<fsim_object_t> resolved_drivers;
    assert(
        fsim_session_visit_children(
            session,
            resolved,
            collect_object,
            &resolved_drivers)
        == FSIM_STATUS_OK);
    assert(resolved_drivers.size() == 2);

    fsim_object_t q_driver = FSIM_INVALID_OBJECT;
    assert(
        fsim_session_find_object(
            session, text("tb.q.$driver[0]"), &q_driver)
        == FSIM_STATUS_OK);
    std::vector<fsim_object_t> q_children;
    assert(
        fsim_session_visit_children(
            session, q, collect_object, &q_children)
        == FSIM_STATUS_OK);
    assert(q_children.size() == 1);
    assert(q_children.front() == q_driver);
    fsim_object_info_t q_driver_info { };
    q_driver_info.struct_size = sizeof(q_driver_info);
    q_driver_info.api_version = FSIM_API_VERSION;
    assert(
        fsim_session_get_object_info(
            session, q_driver, &q_driver_info)
        == FSIM_STATUS_OK);
    assert(q_driver_info.kind == FSIM_OBJECT_DRIVER);
    assert(q_driver_info.parent == q);
    assert(q_driver_info.width == 1);
    assert(
        std::string(
            q_driver_info.name.data,
            q_driver_info.name.size)
        == "$driver[0]");
    assert(
        std::string(
            q_driver_info.type_name.data,
            q_driver_info.type_name.size)
        == "driver");
    assert(
        (q_driver_info.flags & FSIM_OBJECT_FLAG_HAS_SOURCE)
        == FSIM_OBJECT_FLAG_HAS_SOURCE);
    assert(q_driver_info.source_line == 5);
    int driver_children = 0;
    assert(
        fsim_session_visit_children(
            session, q_driver, visit, &driver_children)
        == FSIM_STATUS_OK);
    assert(driver_children == 0);
    assert(
        fsim_session_deposit(session, q_driver, text("0"))
        == FSIM_STATUS_INVALID_HANDLE);
    assert(
        fsim_session_force(session, q_driver, text("1"))
        == FSIM_STATUS_INVALID_HANDLE);
    assert(
        fsim_session_release(session, q_driver)
        == FSIM_STATUS_INVALID_HANDLE);

    fsim_object_t generated_driver = FSIM_INVALID_OBJECT;
    assert(
        fsim_session_find_object(
            session,
            text("tb.selected.generated_value.$driver[0]"),
            &generated_driver)
        == FSIM_STATUS_OK);
    fsim_object_info_t generated_driver_info { };
    generated_driver_info.struct_size = sizeof(generated_driver_info);
    generated_driver_info.api_version = FSIM_API_VERSION;
    assert(
        fsim_session_get_object_info(
            session, generated_driver, &generated_driver_info)
        == FSIM_STATUS_OK);
    assert(generated_driver_info.parent == generated_signal);
    std::vector<fsim_object_t> generated_signal_children;
    assert(
        fsim_session_visit_children(
            session,
            generated_signal,
            collect_object,
            &generated_signal_children)
        == FSIM_STATUS_OK);
    assert(generated_signal_children.size() == 1);
    assert(generated_signal_children.front() == generated_driver);

    fsim_object_t observed = FSIM_INVALID_OBJECT;
    assert(
        fsim_session_find_object(
            session, text("observed"), &observed)
        == FSIM_STATUS_OK);
    fsim_object_info_t port_info { };
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
    char value[2] { };
    assert(
        fsim_session_read_value(session, q, value, sizeof(value), &required)
        == FSIM_STATUS_OK);
    assert(std::string(value) == "X");
    assert(
        fsim_session_read_value(
            session,
            q_driver,
            value,
            sizeof(value),
            &required)
        == FSIM_STATUS_OK);
    assert(std::string(value) == "X");

    assert(fsim_session_deposit(session, q, text("0")) == FSIM_STATUS_OK);
    assert(fsim_session_force(session, q, text("1")) == FSIM_STATUS_OK);
    assert(fsim_session_deposit(session, q, text("0")) == FSIM_STATUS_OK);
    assert(
        fsim_session_read_value(session, q, value, sizeof(value), &required)
        == FSIM_STATUS_OK);
    assert(std::string(value) == "1");
    assert(
        fsim_session_read_value(
            session,
            q_driver,
            value,
            sizeof(value),
            &required)
        == FSIM_STATUS_OK);
    assert(std::string(value) == "0");
    assert(fsim_session_release(session, q) == FSIM_STATUS_OK);
    assert(
        fsim_session_read_value(session, q, value, sizeof(value), &required)
        == FSIM_STATUS_OK);
    assert(std::string(value) == "0");

    fsim_object_t wide_value = FSIM_INVALID_OBJECT;
    assert(
        fsim_session_find_object(
            session, text("wide_value"), &wide_value)
        == FSIM_STATUS_OK);
    fsim_object_info_t wide_info { };
    wide_info.struct_size = sizeof(wide_info);
    wide_info.api_version = FSIM_API_VERSION;
    assert(
        fsim_session_get_object_info(session, wide_value, &wide_info)
        == FSIM_STATUS_OK);
    assert(wide_info.width == 137);
    std::string wide_seed(137, '0');
    wide_seed[0] = 'X';
    wide_seed[63] = 'Z';
    wide_seed[133] = '1';
    std::string wide_amount(137, '0');
    wide_amount[8] = 'Z';
    wide_amount[129] = 'X';
    wide_amount[136] = '1';
    assert(
        fsim_session_deposit(
            session, wide_value,
            { wide_seed.data(), wide_seed.size() })
        == FSIM_STATUS_OK);
    std::size_t wide_required { };
    assert(
        fsim_session_read_value(
            session, wide_value, nullptr, 0, &wide_required)
        == FSIM_STATUS_INVALID_ARGUMENT);
    assert(wide_required == 138);
    std::vector<char> wide_buffer(wide_required);
    assert(
        fsim_session_read_value(
            session, wide_value, wide_buffer.data(), wide_buffer.size(),
            &wide_required)
        == FSIM_STATUS_OK);
    assert(std::string { wide_buffer.data() } == wide_seed);
    assert(
        fsim_session_force(
            session, wide_value,
            { wide_amount.data(), wide_amount.size() })
        == FSIM_STATUS_OK);
    assert(
        fsim_session_deposit(
            session, wide_value,
            { wide_seed.data(), wide_seed.size() })
        == FSIM_STATUS_OK);
    assert(
        fsim_session_read_value(
            session, wide_value, wide_buffer.data(), wide_buffer.size(),
            &wide_required)
        == FSIM_STATUS_OK);
    assert(std::string { wide_buffer.data() } == wide_amount);
    assert(fsim_session_release(session, wide_value) == FSIM_STATUS_OK);
    assert(
        fsim_session_read_value(
            session, wide_value, wide_buffer.data(), wide_buffer.size(),
            &wide_required)
        == FSIM_STATUS_OK);
    assert(std::string { wide_buffer.data() } == wide_seed);

    assert(fsim_session_run(session, 10) == FSIM_STATUS_STOPPED);
    assert(
        fsim_session_read_value(session, q, value, sizeof(value), &required)
        == FSIM_STATUS_OK);
    assert(std::string(value) == "1");
    assert(
        fsim_session_read_value(
            session,
            resolved,
            value,
            sizeof(value),
            &required)
        == FSIM_STATUS_OK);
    assert(std::string(value) == "X");
    std::vector<std::string> resolved_driver_values;
    for (const auto driver : resolved_drivers) {
        assert(
            fsim_session_read_value(
                session,
                driver,
                value,
                sizeof(value),
                &required)
            == FSIM_STATUS_OK);
        resolved_driver_values.emplace_back(value);
    }
    std::ranges::sort(resolved_driver_values);
    assert((
        resolved_driver_values
        == std::vector<std::string> { "0", "1" }));
    for (const auto& [local, expected] :
        std::array {
            std::pair { control_state, std::string_view { "0" } },
            std::pair { inner_state, std::string_view { "1" } },
            std::pair { timed_later, std::string_view { "1" } },
            std::pair { selected_chosen, std::string_view { "1" } },
        }) {
        assert(
            fsim_session_read_value(
                session, local, value, sizeof(value), &required)
            == FSIM_STATUS_OK);
        assert(std::string_view { value } == expected);
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

    local_info.struct_size = sizeof(local_info);
    local_info.api_version = FSIM_API_VERSION;
    assert(
        fsim_session_get_object_info(
            session, inner_state, &local_info)
        == FSIM_STATUS_OK);
    assert(
        (local_info.flags & FSIM_OBJECT_FLAG_INITIALIZED)
        == FSIM_OBJECT_FLAG_INITIALIZED);
    control_scope_info.struct_size = sizeof(control_scope_info);
    control_scope_info.api_version = FSIM_API_VERSION;
    assert(
        fsim_session_get_object_info(
            session, control_scope, &control_scope_info)
        == FSIM_STATUS_OK);
    assert(
        (control_scope_info.flags & FSIM_OBJECT_FLAG_ENTERED)
        == FSIM_OBJECT_FLAG_ENTERED);
    fsim_object_info_t selected_scope_info { };
    selected_scope_info.struct_size = sizeof(selected_scope_info);
    selected_scope_info.api_version = FSIM_API_VERSION;
    assert(
        fsim_session_get_object_info(
            session, selected_scope, &selected_scope_info)
        == FSIM_STATUS_OK);
    assert(
        (selected_scope_info.flags & FSIM_OBJECT_FLAG_ENTERED)
        == FSIM_OBJECT_FLAG_ENTERED);
    fsim_object_info_t unselected_scope_info { };
    unselected_scope_info.struct_size = sizeof(unselected_scope_info);
    unselected_scope_info.api_version = FSIM_API_VERSION;
    assert(
        fsim_session_get_object_info(
            session, unselected_scope, &unselected_scope_info)
        == FSIM_STATUS_OK);
    assert(
        (unselected_scope_info.flags & FSIM_OBJECT_FLAG_ENTERED) == 0);
    assert(counts.values >= 3);
    assert(counts.safe_points > 0);
    assert(counts.detailed_safe_points == counts.safe_points);
    assert(counts.scheduler_safe_points > 0);
    assert(
        (counts.scheduler_phase_mask
            & (UINT32_C(1) << FSIM_SCHEDULER_PHASE_ACTIVE))
        != 0);
    assert(
        (counts.scheduler_phase_mask
            & (UINT32_C(1) << FSIM_SCHEDULER_PHASE_POSTPONED))
        != 0);
    assert(counts.statement_safe_points > 0);
    assert(counts.call_safe_points > 0);
    assert(counts.wait_safe_points > 0);
    assert(counts.entry_safe_points > 0);
    assert(counts.suspend_safe_points > 0);
    assert(counts.detailed_process != FSIM_INVALID_OBJECT);
    assert(
        std::filesystem::path(counts.detailed_path).filename()
        == "tb.sv");
    assert(counts.detailed_line > 0);
    assert(counts.detailed_instruction != UINT64_MAX);
    assert(counts.provenance_unit.starts_with("work::"));
    assert(counts.provenance_standard == "systemverilog-2017");
    assert(counts.provenance_profile == "none");
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
    const auto old_q_driver = q_driver;
    const auto old_process = process;
    const auto old_child_instance = child_instance;
    const auto old_generated_scope = generated_scope;
    const auto old_control_scope = control_scope;
    const auto old_control_state = control_state;
    assert(fsim_session_build(session) == FSIM_STATUS_OK);
    assert(
        fsim_session_read_value(
            session, old_q, value, sizeof(value), &required)
        == FSIM_STATUS_INVALID_HANDLE);
    assert(
        fsim_session_read_value(
            session,
            old_q_driver,
            value,
            sizeof(value),
            &required)
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
        fsim_session_visit_children(
            session, old_control_scope, visit, &stale_children)
        == FSIM_STATUS_INVALID_HANDLE);
    assert(
        fsim_session_visit_children(
            session, old_child_instance, visit, &stale_children)
        == FSIM_STATUS_INVALID_HANDLE);
    assert(
        fsim_session_visit_children(
            session, old_generated_scope, visit, &stale_children)
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
    fsim_object_info_t safe_process_info { };
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

    counts.manifest = fsim::support::path_to_utf8(assertion_manifest_path);
    assert(
        fsim_session_load_project(
            session,
            fsim::support::path_to_utf8(assertion_manifest_path).c_str())
        == FSIM_STATUS_OK);
    assert(fsim_session_check(session) == FSIM_STATUS_OK);
    assert(fsim_session_build(session) == FSIM_STATUS_OK);
    assert(fsim_session_run(session, 10) == FSIM_STATUS_RUNTIME_ERROR);
    assert(counts.assertions == 2);
    assert(counts.first_assertion_code == "FSIM-API-REPORT-0001");
    assert(counts.first_assertion_severity == FSIM_SEVERITY_WARNING);
    assert(counts.assertion_process != FSIM_INVALID_OBJECT);
    assert(counts.assertion_severity == FSIM_SEVERITY_FATAL);
    assert(counts.assertion_code == "FSIM-API-ASSERT-0001");
    assert(
        counts.assertion_message.find("api mismatch")
        != std::string::npos);
    assert(
        counts.assertion_path
        == fsim::support::path_to_utf8(directory / "assertion.vhd"));
    assert(counts.assertion_line == 10);
    assert(counts.assertion_column == 5);
    assert(counts.assertion_safe_points > 0);

    assert(
        fsim_session_diagnostic_count(session, &diagnostic_count)
        == FSIM_STATUS_OK);
    assert(diagnostic_count == 2);
    struct FutureDiagnostic {
        fsim_diagnostic_t value;
        std::uint64_t future_tail;
    };
    FutureDiagnostic future_diagnostic { };
    future_diagnostic.value.struct_size = sizeof(future_diagnostic);
    future_diagnostic.value.api_version = FSIM_API_VERSION;
    future_diagnostic.future_tail = UINT64_C(0x0123456789abcdef);
    assert(
        fsim_session_get_diagnostic(
            session, 1, &future_diagnostic.value)
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

    fsim_session_t logic9_session = FSIM_INVALID_SESSION;
    assert(
        fsim_session_create(&options, &logic9_session)
        == FSIM_STATUS_OK);
    assert(
        fsim_session_load_project(
            logic9_session,
            fsim::support::path_to_utf8(logic9_manifest_path).c_str())
        == FSIM_STATUS_OK);
    assert(fsim_session_check(logic9_session) == FSIM_STATUS_OK);
    assert(fsim_session_build(logic9_session) == FSIM_STATUS_OK);
    fsim_object_t logic9_value = FSIM_INVALID_OBJECT;
    assert(
        fsim_session_find_object(
            logic9_session, text("value"), &logic9_value)
        == FSIM_STATUS_OK);
    char logic9_text[9] { };
    std::size_t logic9_required = 0;
    assert(
        fsim_session_read_value(
            logic9_session,
            logic9_value,
            logic9_text,
            sizeof(logic9_text),
            &logic9_required)
        == FSIM_STATUS_OK);
    assert(logic9_required == sizeof(logic9_text));
    assert(std::string { logic9_text } == "UUUUUUUU");
    assert(
        fsim_session_deposit(
            logic9_session,
            logic9_value,
            text("ULH-WZ01"))
        == FSIM_STATUS_OK);
    assert(
        fsim_session_read_value(
            logic9_session,
            logic9_value,
            logic9_text,
            sizeof(logic9_text),
            &logic9_required)
        == FSIM_STATUS_OK);
    assert(std::string { logic9_text } == "ULH-WZ01");
    assert(
        fsim_session_force(
            logic9_session,
            logic9_value,
            text("HHHHLLLL"))
        == FSIM_STATUS_OK);
    assert(
        fsim_session_read_value(
            logic9_session,
            logic9_value,
            logic9_text,
            sizeof(logic9_text),
            &logic9_required)
        == FSIM_STATUS_OK);
    assert(std::string { logic9_text } == "HHHHLLLL");
    assert(
        fsim_session_release(logic9_session, logic9_value)
        == FSIM_STATUS_OK);
    assert(
        fsim_session_read_value(
            logic9_session,
            logic9_value,
            logic9_text,
            sizeof(logic9_text),
            &logic9_required)
        == FSIM_STATUS_OK);
    assert(std::string { logic9_text } == "ULH-WZ01");
    assert(
        fsim_session_destroy(logic9_session)
        == FSIM_STATUS_OK);

    CallbackCounts severity_counts;
    severity_counts.manifest = fsim::support::path_to_utf8(severity_manifest_path);
    fsim_session_options_t severity_options { };
    severity_options.struct_size = sizeof(severity_options);
    severity_options.api_version = FSIM_API_VERSION;
    severity_options.max_deltas = 1000;
    fsim_session_t severity_session = FSIM_INVALID_SESSION;
    assert(
        fsim_session_create(
            &severity_options, &severity_session)
        == FSIM_STATUS_OK);
    auto severity_callbacks = callbacks;
    severity_callbacks.user_data = &severity_counts;
    assert(
        fsim_session_set_callbacks(
            severity_session, &severity_callbacks)
        == FSIM_STATUS_OK);
    assert(
        fsim_session_load_project(
            severity_session,
            fsim::support::path_to_utf8(severity_manifest_path).c_str())
        == FSIM_STATUS_OK);
    assert(
        fsim_session_check(severity_session)
        == FSIM_STATUS_OK);
    assert(
        fsim_session_build(severity_session)
        == FSIM_STATUS_OK);
    assert(
        fsim_session_run(severity_session, 10)
        == FSIM_STATUS_RUNTIME_ERROR);
    assert(severity_counts.assertions == 5);
    assert(
        severity_counts.first_assertion_code
        == "FSIM-API-REPORT-0001");
    assert(
        severity_counts.first_assertion_severity
        == FSIM_SEVERITY_NOTE);
    assert(
        severity_counts.assertion_code
        == "FSIM-API-REPORT-0001");
    assert(
        severity_counts.assertion_severity
        == FSIM_SEVERITY_FATAL);
    assert(
        severity_counts.assertion_message
        == "api fatal");
    assert(
        std::filesystem::path {
            severity_counts.assertion_path }
            .filename()
        == "severity_actions.sv");
    size_t severity_diagnostic_count { };
    assert(
        fsim_session_diagnostic_count(
            severity_session,
            &severity_diagnostic_count)
        == FSIM_STATUS_OK);
    assert(severity_diagnostic_count == 5);
    assert(
        fsim_session_destroy(severity_session)
        == FSIM_STATUS_OK);

    const auto api_random_value =
        [&](const std::uint64_t seed) {
            fsim_session_options_t random_options { };
            random_options.struct_size = sizeof(random_options);
            random_options.api_version = FSIM_API_VERSION;
            random_options.max_deltas = 1000;
            random_options.seed = seed;
            fsim_session_t random_session = FSIM_INVALID_SESSION;
            assert(
                fsim_session_create(
                    &random_options, &random_session)
                == FSIM_STATUS_OK);
            assert(
                fsim_session_load_project(
                    random_session,
                    fsim::support::path_to_utf8(random_manifest_path).c_str())
                == FSIM_STATUS_OK);
            assert(
                fsim_session_check(random_session)
                == FSIM_STATUS_OK);
            assert(
                fsim_session_build(random_session)
                == FSIM_STATUS_OK);
            assert(
                fsim_session_run(random_session, 10)
                == FSIM_STATUS_OK);
            fsim_object_t object = FSIM_INVALID_OBJECT;
            assert(
                fsim_session_find_object(
                    random_session,
                    text("api_random.value"),
                    &object)
                == FSIM_STATUS_OK);
            char random_text[64] { };
            size_t random_required { };
            assert(
                fsim_session_read_value(
                    random_session,
                    object,
                    random_text,
                    sizeof(random_text),
                    &random_required)
                == FSIM_STATUS_OK);
            const std::string result { random_text };
            assert(
                fsim_session_destroy(random_session)
                == FSIM_STATUS_OK);
            return result;
        };
    const auto api_seed_111 = api_random_value(111);
    assert(api_seed_111 == api_random_value(111));
    assert(api_seed_111 != api_random_value(112));

    fsim_session_t systemc_session = FSIM_INVALID_SESSION;
    assert(
        fsim_session_create(nullptr, &systemc_session)
        == FSIM_STATUS_OK);
    assert(
        fsim_session_load_project(
            systemc_session,
            fsim::support::path_to_utf8(systemc_manifest_path).c_str())
        == FSIM_STATUS_OK);
    assert(fsim_session_check(systemc_session) == FSIM_STATUS_OK);
    assert(fsim_session_build(systemc_session) == FSIM_STATUS_OK);

    const auto find_systemc = [&](const char* path) {
        fsim_object_t object = FSIM_INVALID_OBJECT;
        assert(
            fsim_session_find_object(
                systemc_session, text(path), &object)
            == FSIM_STATUS_OK);
        assert(object != FSIM_INVALID_OBJECT);
        return object;
    };
    const auto systemc_info = [&](const fsim_object_t object) {
        fsim_object_info_t object_info { };
        object_info.struct_size = sizeof(object_info);
        object_info.api_version = FSIM_API_VERSION;
        assert(
            fsim_session_get_object_info(
                systemc_session, object, &object_info)
            == FSIM_STATUS_OK);
        return object_info;
    };

    fsim_object_t systemc_root = FSIM_INVALID_OBJECT;
    assert(
        fsim_session_root(systemc_session, &systemc_root)
        == FSIM_STATUS_OK);
    const auto input = find_systemc("named.input");
    const auto child = find_systemc("named.child");
    const auto state = find_systemc("named.child.state");
    const auto pulse = find_systemc("named.child.pulse");
    const auto metadata = find_systemc("named.child.metadata");
    const auto view_object = find_systemc("named.child.view");
    const auto run = find_systemc("named.child.run");
    assert(systemc_info(input).kind == FSIM_OBJECT_PORT);
    assert(systemc_info(input).parent == systemc_root);
    assert(systemc_info(child).kind == FSIM_OBJECT_SCOPE);
    assert(systemc_info(child).parent == systemc_root);
    assert(systemc_info(state).kind == FSIM_OBJECT_SIGNAL);
    assert(systemc_info(state).parent == child);
    assert(systemc_info(pulse).kind == FSIM_OBJECT_EVENT);
    assert(systemc_info(pulse).parent == child);
    assert(systemc_info(metadata).kind == FSIM_OBJECT_CHANNEL);
    assert(systemc_info(metadata).parent == child);
    assert(systemc_info(view_object).kind == FSIM_OBJECT_EXPORT);
    assert(systemc_info(view_object).parent == child);
    assert(systemc_info(run).kind == FSIM_OBJECT_PROCESS);
    assert(systemc_info(run).parent == child);
    assert(state != view_object);

    std::vector<fsim_object_t> systemc_root_children;
    assert(
        fsim_session_visit_children(
            systemc_session,
            systemc_root,
            collect_object,
            &systemc_root_children)
        == FSIM_STATUS_OK);
    assert(
        std::ranges::find(systemc_root_children, input)
        != systemc_root_children.end());
    assert(
        std::ranges::find(systemc_root_children, child)
        != systemc_root_children.end());
    std::vector<fsim_object_t> systemc_child_objects;
    assert(
        fsim_session_visit_children(
            systemc_session,
            child,
            collect_object,
            &systemc_child_objects)
        == FSIM_STATUS_OK);
    assert(systemc_child_objects.size() == 5);
    for (const auto object : { state, pulse, metadata, view_object, run }) {
        assert(
            std::ranges::find(systemc_child_objects, object)
            != systemc_child_objects.end());
    }

    const fsim_string_view_t one { "1", 1 };
    assert(
        fsim_session_deposit(systemc_session, state, one)
        == FSIM_STATUS_OK);
    char systemc_value[2] { };
    size_t systemc_required { };
    assert(
        fsim_session_read_value(
            systemc_session,
            view_object,
            systemc_value,
            sizeof(systemc_value),
            &systemc_required)
        == FSIM_STATUS_OK);
    assert(std::string_view { systemc_value } == "1");
    assert(
        fsim_session_read_value(
            systemc_session,
            metadata,
            systemc_value,
            sizeof(systemc_value),
            &systemc_required)
        == FSIM_STATUS_INVALID_HANDLE);
    assert(
        fsim_session_destroy(systemc_session)
        == FSIM_STATUS_OK);

    test_multiple_root_api(directory);
    test_mapped_library_api(directory);

    std::error_code cleanup_error;
    std::filesystem::remove_all(directory, cleanup_error);
    std::cout << "C API tests passed\n";
}
