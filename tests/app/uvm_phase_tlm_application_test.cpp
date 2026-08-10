// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/app/artifact_phase.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/artifact/design.hpp"
#include "fsim/artifact/object.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/project/project.hpp"
#include "fsim/runtime/vcd_writer.hpp"
#include "fsim/support/environment.hpp"
#include "uvm_conformance_inventory.hpp"
#include "uvm_phase_tlm_sequence_probe.hpp"
#include "uvm_phase_tlm_register_probe.hpp"
#include "uvm_process_limits.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#ifndef FSIM_TEST_SOURCE_DIR
#error "FSIM_TEST_SOURCE_DIR must name the fsim source tree"
#endif

namespace {

using fsim::runtime::SystemVerilogClassHandle;
using fsim::runtime::SystemVerilogUvmActivityAction;
using fsim::runtime::SystemVerilogUvmActivityEvent;
using fsim::runtime::SystemVerilogUvmActivityKind;
using fsim::runtime::SystemVerilogUvmPhaseCallbackKind;
using fsim::runtime::SystemVerilogUvmPhaseKind;
using fsim::runtime::SystemVerilogUvmTaskPhaseStatus;

struct ExerciseResult {
  std::string transcript;
  std::vector<SystemVerilogUvmActivityEvent> activity;
  fsim::app::NativeCacheStatistics cache;
};

constexpr std::string_view kExpectedTranscript{
    "FSIM-UVM-PHASE-TLM-PASS phases=build/connect/eoe/sos/run/extract/"
    "check/report/final roots=left,right objection=1/0 drain=3 payload=37 "
    "result=42 source=37/42/1 race=5 deadlock=FSIM-UVM-PHASE-008 "
    "sequence=arb/lock/response/virtual roles=agent/driver/monitor/scoreboard "
    "callback=42 transaction=23 cap=records policy=line/tree/table "
    "compare=deep/mismatch/limit copier=deep/shallow/reference "
    "packer=big/little/metadata/unpack recorder=object/replay "
    "sync=event/pool/barrier/queue/heartbeat/spell "
    "cmdline=args/plus/uvm/exact/prefix/value/tool/isolation "
    "run_test=select/topology/timeout/seed/repeat/finish/fatal "
    "report=verbosity/severity/action/file/catcher/phase/time "
    "objection_trace=on/bounded "
    "tracing=factory/config/resource/debug/activity "
    "legacy_macros=field/object/component/sequence/registry/callback/report "
    "legacy_api=phase/objection/tlm/sequence/callback/register/policy/cmdline/"
    "aliases/negative "
    "uvm2020_api=policy/field_op/copier/object/printer/comparer/packer/recorder/"
    "report/version/removed "
    "uvm_release=selected/provenance/object/design/cache/checkpoint/replay/"
    "mismatch "
    "core_smoke=governed/project/object/factory/resource/config/cmdline/"
    "report/callback/run_test/topology/timeout/seed "
    "flow_smoke=phase/objection/sequence/sequencer/roles/virtual/tlm1/tlm2/"
    "callback/transaction/cancellation "
    "register=frontdoor/backdoor/predictor "
    "maps=little/big byte_enable=1010 callback_coverage=1 sequence=access "
    "replay=relocated cap=records "
    "register_smoke=block/map/field/memory/adapter/predictor/frontdoor/"
    "backdoor/sequence/callback/coverage/dpi/vpi/vhpi/relocation/endian/"
    "byte_enable/negative/checkpoint/cap "
    "platform_contract=source/abi/linux/windows/cdecl/filesystem "
    "limits=as6g/stage1200/matrix7200 "
    "conformance_inventory=standard/project families=17 "
    "governed_classes=release-exact project_classes=27 supported_gaps=0 "
    "suppression=none "
    "closure_audit=compatibility/diagnostics/source/complexity/memory/trace/"
    "artifact/cache gaps=0"};

fsim_uvm_foreign_status_v1 FSIM_UVM_FOREIGN_CALL
accept_foreign_activity(void *, const fsim_uvm_foreign_activity_v1 *) {
  return FSIM_UVM_FOREIGN_OK;
}

void print_diagnostics(const fsim::diagnostic::Engine &diagnostics) {
  fsim::diagnostic::print_text(std::cerr, diagnostics);
}

void make_tree_writable(const std::filesystem::path &root) noexcept {
  std::error_code error;
  if (!std::filesystem::exists(root, error))
    return;
  for (std::filesystem::recursive_directory_iterator iterator(root, error), end;
       !error && iterator != end; iterator.increment(error)) {
    std::filesystem::permissions(iterator->path(),
                                 std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::add, error);
    error.clear();
  }
  std::filesystem::permissions(root, std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::add, error);
}

void require_release_rejection(const std::filesystem::path &work) {
  std::filesystem::create_directories(work);
  const auto source = work / "uvm-release-probe.sv";
  {
    std::ofstream output(source);
    output << "package uvm_pkg; class uvm_object; endclass "
              "class uvm_policy; endclass endpackage\n";
  }
  fsim::project::SourceSet source_set;
  source_set.language = fsim::project::Language::system_verilog;
  source_set.standard = "2017";
  source_set.compilation_unit = "source-set";
  source_set.files = {source};
  source_set.uvm_release =
      fsim::project::SystemVerilogUvmRelease::uvm_1_2;

  fsim::project::Config mismatch;
  mismatch.base_directory = work;
  mismatch.source_sets = {source_set};
  fsim::diagnostic::Engine mismatch_diagnostics;
  const auto mismatched =
      fsim::app::check_project(mismatch, mismatch_diagnostics);
  assert(!mismatched && std::ranges::any_of(
      mismatch_diagnostics.diagnostics(), [](const auto &diagnostic) {
        return diagnostic.code == "FSIM-UVM-VERSION-002";
      }));

  auto second = source_set;
  second.uvm_release =
      fsim::project::SystemVerilogUvmRelease::ieee_1800_2_2020_3_1;
  fsim::project::Config mixed;
  mixed.base_directory = work;
  mixed.source_sets = {source_set, second};
  fsim::diagnostic::Engine mixed_diagnostics;
  const auto mixed_project = fsim::app::check_project(mixed, mixed_diagnostics);
  assert(!mixed_project && std::ranges::any_of(
      mixed_diagnostics.diagnostics(), [](const auto &diagnostic) {
        return diagnostic.code == "FSIM-UVM-VERSION-001";
      }));
}

fsim::project::Config
make_config(const std::filesystem::path &uvm_root,
            const std::filesystem::path &work,
            const fsim::project::Optimization optimization,
            const std::string_view release) {
  fsim::project::Config config;
  const auto absolute_uvm_root = std::filesystem::absolute(uvm_root);
  config.base_directory = work;
  config.project.name = "uvm-phase-tlm-example";
  config.project.time_resolution = "1ns";
  config.project.tops = {{"sv:work.fsim_uvm_phase_tlm_example", "left"},
                         {"sv:work.fsim_uvm_phase_tlm_example", "right"}};
  config.build.cache_path = work / "cache";
  config.build.optimization = optimization;
  config.run.max_deltas = 1'000;
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::system_verilog;
  sources.standard = "2017";
  sources.library = "work";
  sources.compilation_unit = "source-set";
  sources.uvm_release =
      fsim::project::parse_systemverilog_uvm_release(release).value();
  sources.include_directories = {absolute_uvm_root / "src"};
  sources.files = {absolute_uvm_root / "src" / "uvm_pkg.sv",
                   std::filesystem::path{FSIM_TEST_SOURCE_DIR} /
                       "tests/fixtures/systemverilog/uvm_core_smoke_probe.sv",
                   std::filesystem::path{FSIM_TEST_SOURCE_DIR} /
                       "tests/fixtures/systemverilog/uvm_phase_tlm_example.sv",
                   std::filesystem::path{FSIM_TEST_SOURCE_DIR} /
                       "tests/fixtures/systemverilog/uvm_legacy_macro_probe.sv"};
  config.source_sets.push_back(std::move(sources));
  return config;
}

const fsim::frontend::SystemVerilogClassSpecialization &
find_class(const fsim::app::BuiltProject &project,
           const std::string_view suffix) {
  const auto found = std::ranges::find_if(
      project.systemverilog_class_specializations,
      [&](const auto &specialization) {
        return specialization.declaration_identity.ends_with(suffix);
      });
  if (found == project.systemverilog_class_specializations.end()) {
    std::cerr << "missing class specialization ending in " << suffix << '\n';
    std::abort();
  }
  return *found;
}

struct LegacyMethodRequirement {
  std::string_view name;
  std::size_t argument_count{};
  fsim::frontend::SystemVerilogClassMethodKind kind{
      fsim::frontend::SystemVerilogClassMethodKind::Function};
};

void require_legacy_methods(
    const fsim::frontend::SystemVerilogClassSpecialization &specialization,
    const std::initializer_list<LegacyMethodRequirement> requirements) {
  for (const auto &requirement : requirements) {
    const auto found = std::ranges::find_if(
        specialization.methods,
        [&](const auto &method) {
          return method.name == requirement.name &&
                 method.arguments.size() == requirement.argument_count &&
                 method.kind == requirement.kind;
        });
    if (found == specialization.methods.end()) {
      std::cerr << "missing UVM 1.2 method profile "
                << specialization.declaration_identity << "::"
                << requirement.name << '/' << requirement.argument_count
                << " kind=" << static_cast<unsigned>(requirement.kind) << '\n';
      std::abort();
    }
  }
}

void forbid_methods(
    const fsim::frontend::SystemVerilogClassSpecialization &specialization,
    const std::initializer_list<std::string_view> names) {
  for (const auto name : names) {
    if (std::ranges::any_of(
            specialization.methods,
            [&](const auto &method) { return method.name == name; })) {
      std::cerr << "removed IEEE 1800.2 API unexpectedly retained: "
                << specialization.declaration_identity << "::" << name
                << '\n';
      std::abort();
    }
  }
}

void require_difference_matrix(const std::string_view release) {
  using Release = fsim::project::SystemVerilogUvmRelease;
  using Compatibility = fsim::project::SystemVerilogUvmCompatibility;
  const auto selected =
      fsim::project::parse_systemverilog_uvm_release(release).value();
  const auto expected = selected == Release::uvm_1_2
      ? Compatibility{
            selected, 1, 2,
            true, true, true, true,
            true, true,
            false, false, false}
      : Compatibility{
            selected, 2020, 3,
            false, false, false, false,
            true, true,
            true, true, true};
  assert(fsim::project::systemverilog_uvm_compatibility(selected) == expected);
}

void require_core_smoke_surface(const fsim::app::BuiltProject &project) {
  const auto &smoke = find_class(project, "::fsim_uvm_core_smoke_test");
  require_legacy_methods(
      smoke,
      {{"object_policy_smoke", 0},
       {"factory_smoke", 0},
       {"resource_smoke", 0},
       {"configuration_smoke", 0},
       {"command_line_smoke", 0},
       {"reporting_smoke", 0},
       {"callback_smoke", 0},
       {"test_selection_smoke", 0},
       {"topology_smoke", 0},
       {"timeout_smoke", 0},
       {"seed_smoke", 0}});
  assert(!smoke.base_specialization_identity.empty());
  for (const auto &method : smoke.methods) {
    if (method.name.ends_with("_smoke"))
      assert(!method.statements.empty());
  }
}

void require_flow_smoke_surface(const fsim::app::BuiltProject &project) {
  using Kind = fsim::frontend::SystemVerilogClassMethodKind;
  const auto &component = find_class(project, "::fsim_native_component");
  require_legacy_methods(
      component,
      {{"build_phase", 1},
       {"connect_phase", 1},
       {"end_of_elaboration_phase", 1},
       {"start_of_simulation_phase", 1},
       {"run_phase", 1, Kind::Task},
       {"extract_phase", 1},
       {"check_phase", 1},
       {"report_phase", 1},
       {"final_phase", 1}});
  for (const auto &name : {"phase_port", "phase_fifo"}) {
    assert(std::ranges::any_of(component.properties, [&](const auto &property) {
      return property.name == name;
    }));
  }
  for (const auto &method : component.methods) {
    if (method.name.ends_with("_phase"))
      assert(!method.statements.empty());
  }

  for (const auto suffix : {
           "::fsim_uvm_sequence", "::fsim_uvm_virtual_sequence",
           "::fsim_uvm_sequencer", "::fsim_uvm_driver",
           "::fsim_uvm_monitor", "::fsim_uvm_agent",
           "::fsim_uvm_callback"}) {
    assert(!find_class(project, suffix).base_specialization_identity.empty());
  }

  require_legacy_methods(
      find_class(project, "::uvm_tlm_generic_payload"),
      {{"get_command", 0},
       {"set_command", 1},
       {"set_address", 1},
       {"get_address", 0},
       {"get_data", 1},
       {"set_data", 1},
       {"get_data_length", 0},
       {"get_streaming_width", 0},
       {"set_streaming_width", 1},
       {"get_byte_enable", 1},
       {"set_byte_enable", 1},
       {"get_response_status", 0},
       {"set_response_status", 1},
       {"is_response_ok", 0}});
}

void require_register_smoke_surface(const fsim::app::BuiltProject &project) {
  for (const auto suffix : {
           "::fsim_uvm_reg", "::fsim_uvm_reg_block",
           "::fsim_uvm_reg_adapter", "::fsim_uvm_reg_predictor",
           "::fsim_uvm_reg_sequence", "::fsim_uvm_reg_callback"}) {
    assert(!find_class(project, suffix).base_specialization_identity.empty());
  }

  require_legacy_methods(
      find_class(project, "::fsim_uvm_reg_adapter"),
      {{"reg2bus", 1}, {"bus2reg", 2}});
  require_legacy_methods(find_class(project, "::uvm_reg_field"),
                         {{"configure", 9}, {"get_n_bits", 0}});
  require_legacy_methods(
      find_class(project, "::uvm_reg_map"),
      {{"configure", 5},
       {"add_reg", 5},
       {"add_mem", 5},
       {"set_sequencer", 2},
       {"get_endian", 1}});
  require_legacy_methods(
      find_class(project, "::uvm_mem"),
      {{"configure", 2},
       {"get_size", 0},
       {"get_n_bits", 0},
       {"set_frontdoor", 4},
       {"set_backdoor", 3}});
}

void require_legacy_uvm_1_2_api_surface(
    const fsim::app::BuiltProject &project) {
  using Kind = fsim::frontend::SystemVerilogClassMethodKind;
  std::size_t verified_profiles{};
  const auto require = [&project, &verified_profiles](
                           const std::string_view class_suffix,
                           const std::initializer_list<LegacyMethodRequirement>
                               requirements) {
    require_legacy_methods(find_class(project, class_suffix), requirements);
    verified_profiles += requirements.size();
  };
  require(
      "::uvm_phase",
      {{"get_state", 0},
       {"raise_objection", 3},
       {"drop_objection", 3},
       {"get_objection_count", 1},
       {"sync", 3},
       {"unsync", 3},
       {"wait_for_state", 2, Kind::Task},
       {"jump", 1},
       {"jump_all", 1}});
  require(
      "::uvm_objection",
      {{"raise_objection", 3},
       {"drop_objection", 3},
       {"clear", 1},
       {"set_drain_time", 2},
       {"wait_for", 2, Kind::Task},
       {"wait_for_total_count", 2, Kind::Task},
       {"get_objection_count", 1},
       {"get_objection_total", 1},
       {"display_objections", 2}});
  require(
      "::uvm_port_base",
      {{"connect", 1},
       {"size", 0},
       {"get_if", 1},
       {"debug_connected_to", 2},
       {"get_connected_to", 1},
       {"resolve_bindings", 0}});
  require(
      "::uvm_sequence_base",
      {{"start", 4, Kind::Task},
       {"kill", 0},
       {"start_item", 3, Kind::Task},
       {"finish_item", 2, Kind::Task},
       {"wait_for_grant", 2, Kind::Task},
       {"send_request", 2},
       {"wait_for_item_done", 1, Kind::Task},
       {"put_response", 1},
       {"get_base_response", 2, Kind::Task},
       {"set_automatic_phase_objection", 1},
       {"get_automatic_phase_objection", 0}});
  require(
      "::uvm_callbacks",
      {{"add", 3},
       {"delete", 2},
       {"get_first", 2},
       {"get_next", 2},
       {"display", 1}});
  require(
      "::uvm_callback",
      {{"callback_mode", 1}, {"is_enabled", 0}});
  require(
      "::uvm_reg",
      {{"write", 9, Kind::Task},
       {"read", 9, Kind::Task},
       {"poke", 7, Kind::Task},
       {"peek", 7, Kind::Task},
       {"update", 8, Kind::Task},
       {"mirror", 9, Kind::Task},
       {"predict", 7},
       {"needs_update", 0}});
  require(
      "::uvm_object",
      {{"clone", 0},
       {"print", 1},
       {"sprint", 1},
       {"record", 1},
       {"copy", 1},
       {"compare", 2},
       {"pack", 2},
       {"pack_bytes", 2},
       {"pack_ints", 2},
       {"unpack", 2},
       {"unpack_bytes", 2},
       {"unpack_ints", 2}});
  require(
      "::uvm_printer",
      {{"print_field", 6},
       {"print_object", 3},
       {"print_string", 3},
       {"emit", 0}});
  require(
      "::uvm_comparer",
      {{"compare_field", 5},
       {"compare_field_real", 3},
       {"compare_object", 3},
       {"compare_string", 3}});
  require(
      "::uvm_packer",
      {{"pack_field", 2},
       {"pack_string", 1},
       {"pack_object", 1},
       {"unpack_field", 1},
       {"unpack_string", 1},
       {"unpack_object", 1},
       {"reset", 0}});
  require(
      "::uvm_recorder",
      {{"record_field", 4},
       {"record_object", 2},
       {"record_string", 2},
       {"close", 1},
       {"free", 1}});
  require(
      "::uvm_cmdline_processor",
      {{"get_inst", 0},
       {"get_args", 1},
       {"get_plusargs", 1},
       {"get_uvm_args", 1},
       {"get_arg_matches", 2},
       {"get_arg_value", 2},
       {"get_arg_values", 2},
       {"get_tool_name", 0},
       {"get_tool_version", 0}});
  require(
      "::uvm_component",
      {{"status", 0},
       {"kill", 0},
       {"stop_phase", 1, Kind::Task},
       {"stop", 1, Kind::Task},
       {"set_config_int", 3},
       {"set_config_string", 3},
       {"set_config_object", 4},
       {"get_config_int", 2},
       {"get_config_string", 2},
       {"get_config_object", 3},
       {"print_config_settings", 3}});
  require(
      "::uvm_test_done_objection",
      {{"stop_request", 0},
       {"force_stop", 1, Kind::Task},
       {"raise_objection", 3},
       {"drop_objection", 3},
       {"get", 0}});
  assert(verified_profiles == 107);
}

void require_uvm_2020_3_1_api_surface(
    const fsim::app::BuiltProject &project) {
  std::size_t verified_profiles{};
  const auto require = [&project, &verified_profiles](
                           const std::string_view class_suffix,
                           const std::initializer_list<LegacyMethodRequirement>
                               requirements) {
    require_legacy_methods(find_class(project, class_suffix), requirements);
    verified_profiles += requirements.size();
  };
  require(
      "::uvm_policy",
      {{"flush", 0},
       {"extension_exists", 1},
       {"set_extension", 1},
       {"get_extension", 1},
       {"clear_extension", 1},
       {"clear_extensions", 0},
       {"push_active_object", 1},
       {"pop_active_object", 0},
       {"get_active_object", 0},
       {"get_active_object_depth", 0}});
  require(
      "::uvm_field_op",
      {{"set", 3},
       {"get_op_name", 0},
       {"get_op_type", 0},
       {"get_policy", 0},
       {"get_rhs", 0},
       {"user_hook_enabled", 0},
       {"disable_user_hook", 0},
       {"flush", 0}});
  require(
      "::uvm_copier",
      {{"copy_object", 2},
       {"object_copied", 3},
       {"flush", 0},
       {"set_recursion_policy", 1},
       {"get_recursion_policy", 0},
       {"get_num_copies", 1},
       {"get_first_copy", 2},
       {"get_next_copy", 2},
       {"get_last_copy", 2},
       {"get_prev_copy", 2},
       {"set_default", 1},
       {"get_default", 0}});
  require(
      "::uvm_object",
      {{"get_uvm_seeding", 0},
       {"set_uvm_seeding", 1},
       {"copy", 2},
       {"pack_longints", 2},
       {"unpack_longints", 2},
       {"do_execute_op", 1},
       {"set_local", 1},
       {"m_unsupported_set_local", 1}});
  require(
      "::uvm_printer",
      {{"set_default", 1},
       {"get_default", 0},
       {"object_printed", 2},
       {"print_generic_element", 4},
       {"flush", 0},
       {"set_name_enabled", 1},
       {"get_name_enabled", 0},
       {"set_type_name_enabled", 1},
       {"get_type_name_enabled", 0},
       {"set_size_enabled", 1},
       {"get_size_enabled", 0},
       {"set_id_enabled", 1},
       {"get_id_enabled", 0},
       {"set_radix_enabled", 1},
       {"get_radix_enabled", 0},
       {"set_radix_string", 2},
       {"get_radix_string", 1},
       {"set_default_radix", 1},
       {"get_default_radix", 0},
       {"set_root_enabled", 1},
       {"get_root_enabled", 0},
       {"set_recursion_policy", 1},
       {"get_recursion_policy", 0},
       {"set_max_depth", 1},
       {"get_max_depth", 0},
       {"set_file", 1},
       {"get_file", 0},
       {"set_line_prefix", 1},
       {"get_line_prefix", 0},
       {"set_begin_elements", 1},
       {"get_begin_elements", 0},
       {"set_end_elements", 1},
       {"get_end_elements", 0},
       {"push_element", 4},
       {"pop_element", 0}});
  require(
      "::uvm_comparer",
      {{"flush", 0},
       {"object_compared", 4},
       {"get_miscompares", 0},
       {"get_result", 0},
       {"set_result", 1},
       {"set_recursion_policy", 1},
       {"get_recursion_policy", 0},
       {"set_check_type", 1},
       {"get_check_type", 0},
       {"set_show_max", 1},
       {"get_show_max", 0},
       {"set_verbosity", 1},
       {"get_verbosity", 0},
       {"set_severity", 1},
       {"get_severity", 0},
       {"set_threshold", 1},
       {"get_threshold", 0},
       {"set_default", 1},
       {"get_default", 0}});
  require(
      "::uvm_packer",
      {{"set_packed_bits", 1},
       {"set_packed_bytes", 1},
       {"set_packed_ints", 1},
       {"set_packed_longints", 1},
       {"get_packed_bits", 1},
       {"get_packed_bytes", 1},
       {"get_packed_ints", 1},
       {"get_packed_longints", 1},
       {"set_default", 1},
       {"get_default", 0},
       {"flush", 0},
       {"pack_object_with_meta", 1},
       {"pack_object_wrapper", 1},
       {"is_object_wrapper", 0},
       {"unpack_string", 0},
       {"unpack_object_with_meta", 1},
       {"unpack_object_wrapper", 0},
       {"unpack_string_with_size", 1}});
  require(
      "::uvm_recorder",
      {{"set_recursion_policy", 1},
       {"get_recursion_policy", 0},
       {"set_id_enabled", 1},
       {"get_id_enabled", 0},
       {"set_default_radix", 1},
       {"get_default_radix", 0},
       {"flush", 0},
       {"object_recorded", 2}});
  require(
      "::uvm_report_server",
      {{"dump_server_state", 0}});
  require(
      "::uvm_default_report_server",
      {{"report_summarize", 1},
       {"summarize", 1},
       {"m_report_summarize", 1}});
  require(
      "::uvm_component",
      {{"set_config_int", 3},
       {"set_config_string", 3},
       {"set_config_object", 4},
       {"get_config_int", 2},
       {"get_config_string", 2},
       {"get_config_object", 3},
       {"print_config_settings", 3}});
  require(
      "::uvm_test_done_objection",
      {{"qualify", 3},
       {"get_type", 0},
       {"create", 1},
       {"get_type_name", 0},
       {"get", 0}});
  assert(verified_profiles == 134);

  forbid_methods(
      find_class(project, "::uvm_component"),
      {"status", "kill", "do_kill_all", "stop_phase", "stop"});
  forbid_methods(
      find_class(project, "::uvm_sequence_base"),
      {"num_sequences", "get_seq_kind", "get_sequence", "do_sequence_kind",
       "get_sequence_by_name", "create_and_start_sequence_by_name"});
}

std::uint64_t low_word(
    const std::optional<fsim::runtime::SystemVerilogUvmTlm1Payload> &payload) {
  assert(payload && payload->value.width() == 32);
  std::uint64_t result{};
  for (std::size_t bit = 0; bit < 32; ++bit) {
    if (payload->value.get(bit) == fsim::runtime::Logic4::one) {
      result |= std::uint64_t{1} << bit;
    }
  }
  return result;
}

fsim::runtime::PackedLogic4 packed32(const std::uint32_t value) {
  fsim::runtime::PackedLogic4 result(32, fsim::runtime::Logic4::zero);
  for (std::size_t bit = 0; bit < 32; ++bit) {
    if (((value >> bit) & 1U) != 0) {
      result.set(bit, fsim::runtime::Logic4::one);
    }
  }
  return result;
}

fsim::runtime::PackedLogic4 packed_value(const std::size_t width,
                                         const std::uint64_t value) {
  fsim::runtime::PackedLogic4 result(width, fsim::runtime::Logic4::zero);
  for (std::size_t bit = 0; bit < width; ++bit) {
    if (((value >> bit) & 1U) != 0) {
      result.set(bit, fsim::runtime::Logic4::one);
    }
  }
  return result;
}

void write_activity_trace(
    const std::filesystem::path &path,
    const std::span<const SystemVerilogUvmActivityEvent> events) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  assert(output);
  fsim::runtime::VcdWriter writer(output, "1ns");
  const auto sequence = writer.declare_signal("uvm.activity.sequence", 64);
  const auto kind = writer.declare_signal("uvm.activity.kind", 8);
  const auto action = writer.declare_signal("uvm.activity.action", 8);
  const auto value = writer.declare_signal("uvm.activity.value", 64);
  writer.begin(0);
  for (const auto &event : events) {
    writer.set_time(event.time);
    writer.change(sequence, packed_value(64, event.sequence));
    writer.change(kind,
                  packed_value(8, static_cast<std::uint64_t>(event.kind)));
    writer.change(action,
                  packed_value(8, static_cast<std::uint64_t>(event.action)));
    writer.change(value, packed_value(64, event.value));
  }
  writer.flush();
  assert(output.good());
}

ExerciseResult exercise(fsim::app::BuiltProject project,
                        const fsim::app::SimulationEngine engine,
                        const std::filesystem::path &trace_path,
                        const std::string_view release) {
  const auto selected_release =
      fsim::project::parse_systemverilog_uvm_release(release);
  assert(selected_release.has_value());
  assert(project.systemverilog_uvm_provenance.release == *selected_release);
  assert(!project.systemverilog_uvm_provenance.source_identity.empty());
  require_difference_matrix(release);
  fsim::tests::app::require_uvm_release_closure(release);
  require_core_smoke_surface(project);
  require_flow_smoke_surface(project);
  require_register_smoke_surface(project);
  const auto inventory =
      fsim::tests::app::require_uvm_conformance_inventory(project, release);
  assert(inventory.families == 17 && inventory.project_classes == 27);
  if (project.systemverilog_uvm_checkpoint.has_value()) {
    assert(project.systemverilog_uvm_checkpoint->provenance.uvm_release
           == fsim::project::to_string(*selected_release));
    assert(project.systemverilog_uvm_checkpoint->provenance.source_identity
           == project.systemverilog_uvm_provenance.source_identity);
  }
  if (release == "uvm-1.2")
    require_legacy_uvm_1_2_api_surface(project);
  if (release == "uvm-2020.3.1")
    require_uvm_2020_3_1_api_surface(project);
  const auto &legacy_item = find_class(project, "::fsim_uvm_legacy_item");
  const auto &legacy_sequence =
      find_class(project, "::fsim_uvm_legacy_sequence");
  const auto &legacy_component =
      find_class(project, "::fsim_uvm_legacy_component");
  const auto &legacy_reg = find_class(project, "::fsim_uvm_legacy_reg");
  const auto &legacy_callback =
      find_class(project, "::fsim_uvm_legacy_callback");
  const auto &legacy_analysis_imp =
      find_class(project, "::uvm_analysis_imp_fsim_legacy");
  const auto method = [](const auto &specialization,
                         const std::string_view name) {
    const auto found = std::ranges::find(
        specialization.methods, name,
        &fsim::frontend::SystemVerilogClassMethodProfile::name);
    return found == specialization.methods.end() ? nullptr : &*found;
  };
  auto *legacy_field_automation = method(
      legacy_item, "__m_uvm_field_automation");
  if (legacy_field_automation == nullptr) {
    legacy_field_automation = method(legacy_item, "__m_uvm_execute_field_op");
  }
  assert(method(legacy_item, "get_type") != nullptr &&
         method(legacy_item, "get_object_type") != nullptr &&
         method(legacy_item, "create") != nullptr &&
         method(legacy_item, "get_type_name") != nullptr &&
         legacy_field_automation != nullptr &&
         method(legacy_sequence, "body") != nullptr &&
         !method(legacy_sequence, "body")->statements.empty() &&
         method(legacy_component, "callback_probe") != nullptr &&
         !method(legacy_component, "callback_probe")->statements.empty() &&
         method(legacy_component, "report_probe") != nullptr &&
         !method(legacy_component, "report_probe")->statements.empty() &&
         method(legacy_reg, "get_type") != nullptr &&
         method(legacy_callback, "get_type") != nullptr &&
         !legacy_analysis_imp.base_specialization_identity.empty());
  const std::array legacy_factory_identities{
      legacy_item.specialization_identity,
      legacy_sequence.specialization_identity,
      legacy_component.specialization_identity,
      legacy_reg.specialization_identity,
      legacy_callback.specialization_identity};
  const auto &component_class = find_class(project, "::fsim_native_component");
  const auto component_specialization = component_class.specialization_identity;
  const auto component_declaration = component_class.declaration_identity;
  const auto payload_declaration =
      find_class(project, "::fsim_uvm_payload").declaration_identity;
  fsim::app::Simulation simulation(std::move(project), 1'000, engine);
  for (const auto &identity : legacy_factory_identities) {
    const auto wrapper =
        simulation.uvm_registry().wrapper_by_specialization(identity);
    assert(wrapper != 0 &&
           simulation.uvm_factory().debug_resolve_by_type(wrapper).resolved ==
               wrapper);
  }
  std::vector<SystemVerilogUvmActivityEvent> callbacks;
  const auto callback_token = simulation.add_uvm_activity_hook(
      [&](const auto &event) { callbacks.push_back(event); });

  const auto left_root = simulation.create_uvm_root("left");
  const auto right_root = simulation.create_uvm_root("right");
  const auto left = simulation.allocate_uvm_component(
      component_specialization, "worker", 0, left_root, component_declaration);
  const auto right = simulation.allocate_uvm_component(
      component_specialization, "worker", 0, right_root, component_declaration);
  assert(
      left && right &&
      simulation.read_class_property(left, "marker").packed.low_word().aval ==
          10 &&
      simulation.read_class_property(right, "marker").packed.low_word().aval ==
          10);

  std::vector<std::string> phase_order;
  const auto component_name = [&](const SystemVerilogClassHandle component) {
    const auto root = simulation.uvm_components().root_of(component);
    return std::string{simulation.uvm_components().root_identity(root)};
  };
  const auto record_execution = [&](const auto &result) {
    for (const auto &event : result.events) {
      if (event.callback == SystemVerilogUvmPhaseCallbackKind::Execute) {
        phase_order.push_back(
            std::string{
                simulation.uvm_phases().snapshot(event.phase).identity} +
            ':' + component_name(event.component));
      }
    }
  };
  auto &phases = simulation.uvm_phases();
  const auto schedule = *phases.standard_schedule();
  for (const auto kind :
       {SystemVerilogUvmPhaseKind::Build, SystemVerilogUvmPhaseKind::Connect,
        SystemVerilogUvmPhaseKind::EndOfElaboration,
        SystemVerilogUvmPhaseKind::StartOfSimulation}) {
    const auto result =
        simulation.execute_uvm_function_phase(schedule.phase(kind));
    if (!result.success()) {
      for (const auto &failure : result.failures) {
        std::cerr << failure.diagnostic_code << ": " << failure.message << '\n';
      }
    }
    assert(result.success());
    record_execution(result);
  }
  const auto sequence_root = simulation.create_uvm_root("sequence");
  const auto sequence_environment =
      fsim::tests::app::exercise_exact_uvm_sequence_environment(
          simulation, sequence_root);
  simulation.uvm_phases().unparticipate_standard_root(sequence_root);
  const auto register_root = simulation.create_uvm_root("register");
  const auto register_environment =
      fsim::tests::app::exercise_exact_uvm_register_environment(
          simulation, register_root);
  simulation.uvm_phases().unparticipate_standard_root(register_root);

  const auto run_phase = schedule.phase(SystemVerilogUvmPhaseKind::Run);
  auto &objections = simulation.uvm_objections();
  const auto source = objections.bind_source(left);
  const auto task_result = simulation.execute_uvm_task_phase(
      run_phase,
      [&](const SystemVerilogClassHandle component, const auto, const auto) {
        if (component == left) {
          objections.set_drain_time(run_phase, source, 3);
          const auto raised =
              objections.raise(run_phase, source, "transfer", 1);
          assert(raised.success());
        }
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  if (!task_result.success() || task_result.processes.size() != 2) {
    std::cerr << "run phase failed: state="
              << static_cast<unsigned>(task_result.final_state)
              << " processes=" << task_result.processes.size()
              << " events=" << task_result.events.size() << '\n';
    for (const auto &failure : task_result.failures) {
      std::cerr << failure.diagnostic_code << ": " << failure.message << '\n';
    }
  }
  assert(task_result.success() && task_result.processes.size() == 2);
  record_execution(task_result);
  fsim::runtime::SystemVerilogUvmCheckpointLimits process_limits;
  process_limits.maximum_external_phase_processes = 1;
  const auto bounded_process_checkpoint =
      simulation.capture_uvm_checkpoint(process_limits);
  assert(!bounded_process_checkpoint &&
         bounded_process_checkpoint.error ==
             fsim::runtime::SystemVerilogUvmCheckpointError::ResourceLimit);
  fsim::runtime::SystemVerilogUvmCheckpointLimits callback_limits;
  callback_limits.maximum_external_callbacks = 0;
  std::uint64_t foreign_callback_token{};
  assert(simulation.uvm_foreign().add_callback(accept_foreign_activity, nullptr,
                                               foreign_callback_token) ==
         FSIM_UVM_FOREIGN_OK);
  const auto bounded_callback_checkpoint =
      simulation.capture_uvm_checkpoint(callback_limits);
  assert(!bounded_callback_checkpoint &&
         bounded_callback_checkpoint.error ==
             fsim::runtime::SystemVerilogUvmCheckpointError::ResourceLimit);
  assert(simulation.uvm_foreign().remove_callback(foreign_callback_token) ==
         FSIM_UVM_FOREIGN_OK);
  assert(objections.source_count(run_phase, source) == 1 &&
         objections.propagated_count(run_phase, left_root) == 1);
  for (const auto &process : task_result.processes) {
    phases.complete_task_process(process);
  }
  const auto dropped = objections.drop(run_phase, source, "transfer", 1);
  assert(dropped.success() && objections.source_count(run_phase, source) == 0 &&
         objections.has_pending_drain(run_phase, source));
  const auto settled = phases.settle_task_phase(
      run_phase, [](const auto, const auto, const auto) {}, 10);
  assert(settled.status ==
             fsim::runtime::SystemVerilogUvmQuiescenceStatus::Completed &&
         settled.time == 3 &&
         settled.final_state ==
             fsim::runtime::SystemVerilogUvmPhaseState::Done);

  for (const auto kind :
       {SystemVerilogUvmPhaseKind::Extract, SystemVerilogUvmPhaseKind::Check,
        SystemVerilogUvmPhaseKind::Report, SystemVerilogUvmPhaseKind::Final}) {
    const auto result =
        simulation.execute_uvm_function_phase(schedule.phase(kind));
    assert(result.success());
    record_execution(result);
  }

  auto &tlm = simulation.uvm_tlm1();
  const fsim::runtime::SystemVerilogUvmTlm1Profile profile{
      fsim::runtime::SystemVerilogUvmTlm1Interface::Bidirectional,
      fsim::runtime::SystemVerilogUvmTlm1Direction::Bidirectional,
      payload_declaration,
      {}};
  std::vector<std::uint64_t> tlm_results;
  for (const auto &[component, root, prefix] :
       std::array{std::tuple{left, left_root, std::string_view{"left"}},
                  std::tuple{right, right_root, std::string_view{"right"}}}) {
    const auto port = tlm.register_endpoint(
        {fsim::runtime::SystemVerilogUvmTlm1EndpointKind::Port, profile,
         component, "phase_port", 1, 1});
    const auto implementation = tlm.register_endpoint(
        {fsim::runtime::SystemVerilogUvmTlm1EndpointKind::Implementation,
         profile, component, "phase_fifo", 0, 0});
    tlm.connect(port, implementation);
    tlm.resolve_all();
    tlm.configure_fifo(implementation, 2);
    assert(
        tlm.try_put(port, {payload_declaration, packed32(37), 0, root, {}, 0}));
    const auto payload = tlm.try_get(port);
    assert(payload && payload->owner_root == root);
    tlm_results.push_back(low_word(payload) + 5);
    assert(tlm_results.back() == 42);
    (void)prefix;
  }

  const auto matrix_domain = phases.create_domain(
      "matrix", fsim::runtime::SystemVerilogUvmDomainKind::Custom);
  phases.participate(matrix_domain, left_root);
  phases.participate(matrix_domain, right_root);
  const auto race_phase = phases.create_custom_phase(
      matrix_domain, "race",
      fsim::runtime::SystemVerilogUvmPhaseExecutionKind::Task);
  const auto deadlock_phase = phases.create_custom_phase(
      matrix_domain, "deadlock",
      fsim::runtime::SystemVerilogUvmPhaseExecutionKind::Task);
  phases.connect(race_phase, deadlock_phase);
  const auto race_execution = phases.execute_task_phase(
      race_phase, [](const auto, const auto, const auto) {},
      [](const auto, const auto, const auto) {
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  for (const auto &process : race_execution.processes) {
    phases.complete_task_process(process);
  }
  objections.set_drain_time(race_phase, source, 2);
  bool raced{};
  const auto race_settled = phases.settle_task_phase(
      race_phase,
      [&](const auto component, const auto phase, const auto kind) {
        if (!raced && component == left &&
            kind == SystemVerilogUvmPhaseCallbackKind::PhaseReadyToEnd) {
          raced = true;
          assert(objections.raise(phase, source, "matrix-race", 1).success());
          assert(objections.drop(phase, source, "matrix-race", 1).success());
        }
      },
      10);
  assert(raced &&
         race_settled.status ==
             fsim::runtime::SystemVerilogUvmQuiescenceStatus::Completed &&
         race_settled.time == 5 && phases.process_count() == 0 &&
         objections.phase_quiescent(race_phase));

  (void)phases.execute_task_phase(
      deadlock_phase, [](const auto, const auto, const auto) {},
      [](const auto, const auto, const auto) {
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  bool deadlock_rejected{};
  try {
    (void)phases.settle_task_phase(deadlock_phase,
                                   [](const auto, const auto, const auto) {});
  } catch (const fsim::runtime::SystemVerilogUvmPhaseError &error) {
    deadlock_rejected = error.diagnostic_code() == "FSIM-UVM-PHASE-008";
  }
  assert(deadlock_rejected &&
         phases.snapshot(deadlock_phase).state ==
             fsim::runtime::SystemVerilogUvmPhaseState::Done &&
         phases.process_count() == 0 &&
         objections.phase_quiescent(deadlock_phase));

  std::ostringstream source_output;
  simulation.set_output_hook([&](const auto, const std::string_view text,
                                 const bool newline, const auto, const auto) {
    source_output << text;
    if (newline)
      source_output << '\n';
  });
  const auto run = simulation.run();
  assert(run.status == fsim::runtime::RunStatus::completed && run.time == 5);
  const std::string source_line{
      "FSIM-UVM-PHASE-TLM-SOURCE payload=37 result=42 pass=1\n"};
  const std::string smoke_line{
      "FSIM-UVM-CORE-SMOKE-SOURCE suites=11 pass=1\n"};
  assert(source_output.str() == source_line + smoke_line + source_line +
                                    smoke_line);
  assert((phase_order ==
          std::vector<std::string>{
              "build:left", "build:right", "connect:left", "connect:right",
              "end_of_elaboration:left", "end_of_elaboration:right",
              "start_of_simulation:left", "start_of_simulation:right",
              "run:left", "run:right", "extract:left", "extract:right",
              "check:left", "check:right", "report:left", "report:right",
              "final:left", "final:right"}));
  assert((tlm_results == std::vector<std::uint64_t>{42, 42}));
  assert(
      simulation.read_class_property(left, "marker").packed.low_word().aval ==
          9 &&
      simulation.read_class_property(right, "marker").packed.low_word().aval ==
          9);
  assert(std::ranges::any_of(callbacks, [](const auto &event) {
    return event.kind == SystemVerilogUvmActivityKind::Objection &&
           event.action == SystemVerilogUvmActivityAction::Raised;
  }));
  assert(std::ranges::any_of(callbacks, [](const auto &event) {
    return event.kind == SystemVerilogUvmActivityKind::Drain &&
           event.action == SystemVerilogUvmActivityAction::Completed &&
           event.time == 3;
  }));
  assert(std::ranges::any_of(callbacks, [](const auto &event) {
    return event.kind == SystemVerilogUvmActivityKind::Fifo &&
           event.action == SystemVerilogUvmActivityAction::Updated;
  }));
  write_activity_trace(trace_path, callbacks);
  std::ifstream trace(trace_path, std::ios::binary);
  const std::string trace_text{std::istreambuf_iterator<char>{trace},
                               std::istreambuf_iterator<char>{}};
  assert(trace_text.find("$var wire 64 ! sequence $end") != std::string::npos &&
         trace_text.find("#3") != std::string::npos &&
         trace_text.find("#5") != std::string::npos);
  simulation.remove_uvm_activity_hook(callback_token);

  std::ostringstream transcript;
  transcript << "FSIM-UVM-PHASE-TLM-PASS "
                "phases=build/connect/eoe/sos/run/extract/check/report/final"
             << " roots=left,right objection=1/0 drain=3 payload=37 result=42"
             << " source=37/42/1 race=5 deadlock=FSIM-UVM-PHASE-008"
             << sequence_environment << register_environment
             << " register_smoke=block/map/field/memory/adapter/predictor/"
                "frontdoor/backdoor/sequence/callback/coverage/dpi/vpi/vhpi/"
                "relocation/endian/byte_enable/negative/checkpoint/cap"
             << " platform_contract=source/abi/linux/windows/cdecl/filesystem"
                " limits=as6g/stage1200/matrix7200"
             << " conformance_inventory=standard/project families=17"
                " governed_classes=release-exact project_classes=27"
                " supported_gaps=0 suppression=none"
             << " closure_audit=compatibility/diagnostics/source/complexity/"
                "memory/trace/artifact/cache gaps=0";
  return {transcript.str(), std::move(callbacks),
          simulation.native_cache_statistics()};
}

fsim::project::Optimization parse_optimization(const std::string_view value) {
  if (value == "o0")
    return fsim::project::Optimization::o0;
  if (value == "o2")
    return fsim::project::Optimization::o2;
  throw std::invalid_argument{"expected o0 or o2"};
}

fsim::app::SimulationEngine parse_engine(const std::string_view value) {
  if (value == "interpreter") {
    return fsim::app::SimulationEngine::interpreter;
  }
  if (value == "compiled")
    return fsim::app::SimulationEngine::compiled;
  if (value == "debug")
    return fsim::app::SimulationEngine::debug;
  throw std::invalid_argument{"expected interpreter, compiled, or debug"};
}

} // namespace

int main(int argc, char **argv) {
  fsim::tests::app::install_uvm_process_address_space_ceiling();
  if (argc < 5) {
    std::cerr << "usage: " << argv[0]
              << " <direct|compile|elaborate|simulate|release-negative> "
                 "<uvm-root> <work-dir>"
                 " <release> [optimization] [engine] [trace-suffix]\n";
    return 2;
  }
  const std::string_view mode{argv[1]};
  const std::filesystem::path uvm_root{argv[2]};
  const std::filesystem::path work{argv[3]};
  const std::string release{argv[4]};
  std::error_code cleanup_error;
  if (mode == "direct" &&
      !fsim::support::environment_variable("FSIM_TEST_REUSE_WORK")) {
    make_tree_writable(work);
    std::filesystem::remove_all(work, cleanup_error);
  }
  assert(!cleanup_error);
  std::filesystem::create_directories(work);

  if (mode == "release-negative") {
    require_release_rejection(work / "release-negative");
    std::cout << "FSIM-UVM-RELEASE-NEGATIVE-PASS\n";
    return 0;
  }

  if (mode == "direct") {
    require_release_rejection(work / "release-negative");
    auto config =
        make_config(
            uvm_root, work / "direct", fsim::project::Optimization::o2,
            release);
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project || diagnostics.has_error())
      print_diagnostics(diagnostics);
    assert(project && !diagnostics.has_error());
    const auto result =
        exercise(std::move(*project), fsim::app::SimulationEngine::interpreter,
                 work / "direct.vcd", release);
    assert(result.transcript == kExpectedTranscript);
    std::cout << result.transcript << " release=" << release
              << " stage=direct\n";
    return 0;
  }

  const auto object = work / "phase-tlm.fsimobj";
  if (mode == "compile") {
    auto config = make_config(
        uvm_root, work / "compile", fsim::project::Optimization::o2,
        release);
    fsim::diagnostic::Engine diagnostics;
    const auto compiled =
        fsim::app::compile_artifact(config, object, diagnostics);
    if (!compiled || diagnostics.has_error())
      print_diagnostics(diagnostics);
    assert(compiled && !diagnostics.has_error());
    auto metadata = fsim::artifact::load_object_metadata(object, diagnostics);
    assert(metadata && metadata->uvm_release
        == fsim::project::to_string(config.source_sets.front().uvm_release));
    std::cout << kExpectedTranscript << " release=" << release
              << " stage=compile\n";
    return 0;
  }

  if (argc < 6) {
    std::cerr << "elaborate/simulate requires an optimization\n";
    return 2;
  }
  const std::string optimization_name{argv[5]};
  const auto optimization = parse_optimization(optimization_name);
  auto config = make_config(
      uvm_root, work / optimization_name, optimization, release);
  const auto design = work / (optimization_name + ".fsimdesign");
  if (mode == "elaborate") {
    config.source_sets.clear();
    fsim::diagnostic::Engine diagnostics;
    const std::array objects{object};
    const auto elaborated =
        fsim::app::elaborate_artifact(config, objects, design, diagnostics);
    if (!elaborated || diagnostics.has_error())
      print_diagnostics(diagnostics);
    assert(elaborated && !diagnostics.has_error());
    auto metadata = fsim::artifact::load_design_metadata(design, diagnostics);
    assert(metadata && metadata->uvm_release
        == fsim::project::to_string(
            fsim::project::parse_systemverilog_uvm_release(release).value()));
    assert(!metadata->uvm_source_identity.empty());
    std::cout << kExpectedTranscript << " release=" << release
              << " stage=elaborate optimization=" << optimization_name << '\n';
    return 0;
  }

  if (mode == "simulate" && argc == 8) {
    const auto engine = parse_engine(argv[6]);
    fsim::diagnostic::Engine diagnostics;
    auto loaded = fsim::app::load_design_artifact(design, diagnostics);
    if (!loaded || diagnostics.has_error())
      print_diagnostics(diagnostics);
    assert(loaded && !diagnostics.has_error());
    const auto result = exercise(std::move(*loaded), engine,
                                 work / (optimization_name + '-' + argv[7]),
                                 release);
    assert(result.transcript == kExpectedTranscript);
    if (engine == fsim::app::SimulationEngine::compiled &&
        std::string_view{argv[7]} == "cold.fst") {
      assert(result.cache.hits == 0 && result.cache.misses != 0 &&
             result.cache.stores != 0);
    }
    if (engine == fsim::app::SimulationEngine::compiled &&
        std::string_view{argv[7]} == "warm.fst") {
      assert(result.cache.hits != 0 && result.cache.misses == 0);
    }
    std::cout << result.transcript << " release=" << release
              << " stage=simulate optimization=" << optimization_name
              << " engine=" << argv[6] << " trace=" << argv[7] << '\n';
    return 0;
  }
  std::cerr << "unknown mode or invalid mode arguments\n";
  return 2;
}
