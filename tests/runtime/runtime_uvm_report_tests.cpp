// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_component.hpp"
#include "fsim/runtime/uvm_report.hpp"

#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {
namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition) throw std::runtime_error{std::string{message}};
}

template <typename Exception, typename Function>
void require_throws(Function&& function, const std::string_view message) {
  try {
    function();
  } catch (const Exception&) {
    return;
  }
  throw std::runtime_error{std::string{message}};
}

}  // namespace

void test_systemverilog_uvm_report() {
  using namespace fsim::runtime;
  const auto class_descriptor = [] {
    SystemVerilogClassDescriptor result;
    result.declared_type = "uvm_pkg::uvm_object";
    result.dynamic_type = "work::report_object";
    result.specialization_identity = "work::report_object";
    result.assignable_declared_types = {
        "work::report_object", "uvm_pkg::uvm_object"};
    return result;
  };

  SystemVerilogClassHeap heap{{16, 1'024}};
  SystemVerilogUvmObjectService objects{
      heap,
      [&](const std::string_view, const std::string_view,
          const std::string_view) {
        return heap.allocate(class_descriptor());
      }};
  SystemVerilogUvmObjectDescriptor type;
  type.specialization_identity = "work::report_object";
  type.type_name = "report_object";
  objects.register_type(std::move(type));

  const auto reporter = heap.allocate(class_descriptor());
  const auto referenced = heap.allocate(class_descriptor());
  objects.initialize(reporter, "reporter");
  objects.initialize(referenced, "referenced");
  objects.set_full_name(reporter, "uvm_test_top.env.agent");
  objects.set_full_name(referenced, "uvm_test_top.env.item");

  SystemVerilogUvmReportService reports{objects};
  std::vector<SystemVerilogUvmReportMessage> routed;
  reports.set_route_hook(
      [&](const auto& message) { routed.push_back(message); });
  require(
      reports.default_verbosity() == 200
          && reports.enabled(reporter, 200)
          && !reports.enabled(reporter, 201),
      "UVM report enablement must use the simulation verbosity threshold");

  SystemVerilogUvmReportRequest request;
  request.report_object = reporter;
  request.severity = SystemVerilogUvmReportSeverity::Info;
  request.id = "BUILD";
  request.message = "constructed";
  request.verbosity = 200;
  request.filename = "report_test.sv";
  request.line = 73;
  request.context = "uvm_test_top.env";
  request.elements.add_integer(
      "state", PackedLogic4::from_aval_bval(8, 0xa5, 0), 8,
      SystemVerilogUvmReportRadix::Hexadecimal);
  request.elements.add_string("note", "line\n\"quoted\"");
  request.elements.add_object("item", referenced);
  request.elements.add_string(
      "record_only", "hidden", SystemVerilogUvmReportAction::Record);
  require(reports.report(request), "enabled UVM info report must route");
  require(
      routed.size() == 1 && routed[0].sequence == 1
          && routed[0].report_object == reporter
          && routed[0].report_object_name == "uvm_test_top.env.agent"
          && routed[0].severity == SystemVerilogUvmReportSeverity::Info
          && routed[0].id == "BUILD" && routed[0].message == "constructed"
          && routed[0].verbosity == 200
          && routed[0].filename == "report_test.sv"
          && routed[0].line == 73
          && routed[0].context == "uvm_test_top.env",
      "UVM report construction must preserve object, metadata, and sequence");
  const auto expected_payload =
      "constructed\n +state = 8'hA5"
      "\n +note = \"line\\n\\\"quoted\\\"\""
      "\n +item = @" + std::to_string(referenced)
      + "{uvm_test_top.env.item}";
  require(
      reports.compose_payload(routed[0]) == expected_payload,
      "UVM report elements must compose deterministically in insertion order");

  auto copied = request.elements;
  const auto copied_bytes = copied.total_bytes();
  copied.erase(1);
  require(
      copied.size() == 3 && copied.total_bytes() < copied_bytes
          && request.elements.size() == 4,
      "UVM report element copies and erasure must retain independent accounting");
  copied.clear();
  require(copied.size() == 0 && copied.total_bytes() == 0,
          "UVM report element clear must reset its storage accounting");

  request.verbosity = 201;
  require(
      !reports.report(request) && reports.filtered_count() == 1
          && reports.routed_count() == 1 && routed.size() == 1,
      "over-threshold UVM info reports must filter before message construction");
  request.severity = SystemVerilogUvmReportSeverity::Warning;
  request.verbosity = 10'000;
  require(
      reports.report(request) && routed.back().sequence == 2
          && routed.back().verbosity == 10'000,
      "UVM warning reports must route independently of info verbosity gating");
  request.severity = SystemVerilogUvmReportSeverity::Info;
  request.report_enabled_checked = true;
  require(
      reports.report(request) && routed.back().sequence == 3,
      "prechecked UVM info reports must not repeat verbosity filtering");

  SystemVerilogUvmReportRequest defaults;
  defaults.report_object = reporter;
  defaults.id = "DEFAULT";
  defaults.message = "defaults";
  require(
      reports.construct(defaults).verbosity == 200,
      "UVM info reports must construct with UVM_MEDIUM verbosity by default");
  defaults.severity = SystemVerilogUvmReportSeverity::Error;
  require(
      reports.construct(defaults).verbosity == 0,
      "non-info UVM reports must construct with UVM_NONE verbosity by default");

  reports.set_route_hook([](const auto&) {
    throw std::runtime_error{"consumer failed"};
  });
  require(
      reports.report(defaults) && reports.route_failures() == 1
          && reports.routed_count() == 4,
      "UVM route-hook failures must be contained and counted");

  SystemVerilogUvmReportElementLimits element_limits;
  element_limits.max_elements = 1;
  element_limits.max_name_bytes = 4;
  element_limits.max_string_bytes = 4;
  element_limits.max_packed_width = 8;
  element_limits.max_total_bytes = 8;
  SystemVerilogUvmReportElementContainer bounded_elements{element_limits};
  bounded_elements.add_string("name", "data");
  require_throws<std::length_error>(
      [&] { bounded_elements.add_string("x", "y"); },
      "UVM report element count must be bounded");
  require(
      bounded_elements.size() == 1 && bounded_elements.total_bytes() == 8,
      "failed UVM report element insertion must be transactional");
  require_throws<std::length_error>(
      [&] {
        SystemVerilogUvmReportElementContainer value_limits{element_limits};
        value_limits.add_string("n", "12345");
      },
      "UVM report string values must be byte bounded");
  require_throws<std::length_error>(
      [&] {
        SystemVerilogUvmReportElementContainer width_limits{element_limits};
        width_limits.add_integer(
            "n", PackedLogic4(9, Logic4::zero), 9,
            SystemVerilogUvmReportRadix::Binary);
      },
      "UVM report integer widths must be bounded");
  require_throws<std::invalid_argument>(
      [&] {
        SystemVerilogUvmReportElementContainer invalid_radix;
        invalid_radix.add_integer(
            "n", PackedLogic4(1, Logic4::zero), 1,
            static_cast<SystemVerilogUvmReportRadix>(255));
      },
      "invalid UVM report element radices must reject");
  require_throws<std::invalid_argument>(
      [&] {
        SystemVerilogUvmReportElementContainer invalid_action;
        invalid_action.add_string(
            "n", "v", static_cast<SystemVerilogUvmReportAction>(1U << 31U));
      },
      "invalid UVM report element actions must reject");

  SystemVerilogUvmReportLimits limits;
  limits.max_id_bytes = 4;
  limits.max_message_bytes = 8;
  limits.max_filename_bytes = 4;
  limits.max_context_bytes = 4;
  limits.max_report_object_name_bytes = 64;
  limits.max_composed_bytes = 8;
  SystemVerilogUvmReportService bounded_reports{objects, limits};
  SystemVerilogUvmReportRequest bounded_request;
  bounded_request.report_object = reporter;
  bounded_request.id = "12345";
  require_throws<std::length_error>(
      [&] { (void)bounded_reports.construct(bounded_request); },
      "UVM report IDs must be byte bounded");
  bounded_request.id = "id";
  bounded_request.message = "123456789";
  require_throws<std::length_error>(
      [&] { (void)bounded_reports.construct(bounded_request); },
      "UVM report messages must be byte bounded");
  bounded_request.message = "12345678";
  const auto bounded_message = bounded_reports.construct(bounded_request);
  require(bounded_reports.compose_payload(bounded_message) == "12345678",
          "payloads exactly at the UVM report budget must compose");
  auto overflowing_message = bounded_message;
  overflowing_message.elements.add_string("x", "y");
  require_throws<std::length_error>(
      [&] { (void)bounded_reports.compose_payload(overflowing_message); },
      "composed UVM report payloads must be byte bounded");
  bounded_request.message = "ok";
  bounded_request.filename = "12345";
  require_throws<std::length_error>(
      [&] { (void)bounded_reports.construct(bounded_request); },
      "UVM report filenames must be byte bounded");
  bounded_request.filename.clear();
  bounded_request.context = "12345";
  require_throws<std::length_error>(
      [&] { (void)bounded_reports.construct(bounded_request); },
      "UVM report contexts must be byte bounded");

  require_throws<std::invalid_argument>(
      [&] {
        SystemVerilogUvmReportRequest invalid = defaults;
        invalid.severity = static_cast<SystemVerilogUvmReportSeverity>(255);
        (void)reports.construct(invalid);
      },
      "invalid UVM report severities must reject");
  require(
      reports.enabled(0, 200),
      "the global UVM report object must use handle zero explicitly");

  const auto defaults_policy = reports.policy(
      reporter, SystemVerilogUvmReportSeverity::Error, "DEFAULTS");
  require(
      defaults_policy.severity == SystemVerilogUvmReportSeverity::Error
          && has_action(
              defaults_policy.action,
              SystemVerilogUvmReportAction::Display)
          && has_action(
              defaults_policy.action, SystemVerilogUvmReportAction::Count)
          && defaults_policy.file == 0
          && defaults_policy.verbosity == 200,
      "UVM handlers must initialize standard severity actions and verbosity");

  reports.set_verbosity(reporter, 500);
  reports.set_id_verbosity(reporter, "POLICY", 400);
  reports.set_severity_id_verbosity(
      reporter, SystemVerilogUvmReportSeverity::Warning, "POLICY", 300);
  reports.set_severity_action(
      reporter, SystemVerilogUvmReportSeverity::Fatal,
      SystemVerilogUvmReportAction::Exit);
  reports.set_id_action(
      reporter, "POLICY", SystemVerilogUvmReportAction::Record);
  reports.set_severity_id_action(
      reporter, SystemVerilogUvmReportSeverity::Fatal, "POLICY",
      SystemVerilogUvmReportAction::Log
          | SystemVerilogUvmReportAction::Exit);
  reports.set_default_file(reporter, 11);
  reports.set_severity_file(
      reporter, SystemVerilogUvmReportSeverity::Fatal, 22);
  reports.set_id_file(reporter, "POLICY", 33);
  reports.set_severity_id_file(
      reporter, SystemVerilogUvmReportSeverity::Fatal, "POLICY", 44);
  reports.set_severity_override(
      reporter, SystemVerilogUvmReportSeverity::Warning,
      SystemVerilogUvmReportSeverity::Error);
  reports.set_severity_id_override(
      reporter, SystemVerilogUvmReportSeverity::Warning, "POLICY",
      SystemVerilogUvmReportSeverity::Fatal);
  const auto pair_policy = reports.policy(
      reporter, SystemVerilogUvmReportSeverity::Warning, "POLICY");
  require(
      pair_policy.severity == SystemVerilogUvmReportSeverity::Fatal
          && pair_policy.verbosity == 300
          && pair_policy.action
              == (SystemVerilogUvmReportAction::Log
                  | SystemVerilogUvmReportAction::Exit)
          && pair_policy.file == 44,
      "severity-ID policy must override ID, severity, and default policy");
  reports.set_severity_id_file(
      reporter, SystemVerilogUvmReportSeverity::Fatal, "POLICY", 0);
  require(
      reports.policy(
          reporter, SystemVerilogUvmReportSeverity::Warning, "POLICY").file
          == 33,
      "zero severity-ID files must fall back to the ID file");
  reports.set_id_file(reporter, "POLICY", 0);
  require(
      reports.policy(
          reporter, SystemVerilogUvmReportSeverity::Warning, "POLICY").file
          == 22,
      "zero ID files must fall back to the resolved-severity file");
  reports.set_severity_override(
      reporter, SystemVerilogUvmReportSeverity::Error,
      SystemVerilogUvmReportSeverity::Fatal);
  require(
      reports.policy(
          reporter, SystemVerilogUvmReportSeverity::Error, "POLICY").severity
          == SystemVerilogUvmReportSeverity::Error,
      "an ID-specific override table must suppress generic fallback exactly as UVM does");
  require(
      reports.policy(
          reporter, SystemVerilogUvmReportSeverity::Warning, "OTHER").severity
          == SystemVerilogUvmReportSeverity::Error
          && reports.policy(
                 reporter, SystemVerilogUvmReportSeverity::Warning, "OTHER")
                 .verbosity == 500,
      "generic severity overrides and max verbosity must remain fallbacks");

  std::vector<std::string> hook_order;
  reports.set_severity_id_action(
      reporter, SystemVerilogUvmReportSeverity::Info, "HOOK",
      SystemVerilogUvmReportAction::Display
          | SystemVerilogUvmReportAction::CallHook);
  reports.set_report_hook(reporter, [&](const auto& message) {
    hook_order.push_back("generic:" + message.id);
    return false;
  });
  reports.set_severity_hook(
      reporter, SystemVerilogUvmReportSeverity::Info,
      [&](const auto& message) {
        hook_order.push_back("info:" + message.id);
        return true;
      });
  SystemVerilogUvmReportRequest hooked;
  hooked.report_object = reporter;
  hooked.id = "HOOK";
  hooked.message = "hooked";
  hooked.verbosity = 0;
  const auto routed_before_hooks = reports.routed_count();
  require(
      !reports.report(hooked)
          && hook_order
              == std::vector<std::string>{"generic:HOOK", "info:HOOK"}
          && reports.hook_filtered_count() == 1
          && reports.routed_count() == routed_before_hooks,
      "generic and severity hooks must run in order even when one rejects");
  hook_order.clear();
  reports.set_report_hook(reporter, [&](const auto&) {
    hook_order.push_back("generic-throws");
    throw std::runtime_error{"hook failed"};
    return true;
  });
  reports.set_severity_hook(
      reporter, SystemVerilogUvmReportSeverity::Info,
      [&](const auto&) {
        hook_order.push_back("info-after-throw");
        return true;
      });
  require(
      !reports.report(hooked)
          && hook_order == std::vector<std::string>{
                 "generic-throws", "info-after-throw"}
          && reports.hook_failures() == 1
          && reports.hook_filtered_count() == 2,
      "hook exceptions must be contained without skipping the severity hook");
  reports.set_report_hook(reporter, {});
  reports.set_severity_hook(
      reporter, SystemVerilogUvmReportSeverity::Info, {});
  require(
      reports.report(hooked)
          && reports.routed_count() == routed_before_hooks + 1,
      "empty UVM report hooks must restore normal routing");

  SystemVerilogUvmReportLimits one_handler_limits;
  one_handler_limits.max_handlers = 1;
  SystemVerilogUvmReportService one_handler{objects, one_handler_limits};
  one_handler.set_verbosity(reporter, 1);
  require_throws<std::length_error>(
      [&] { one_handler.set_verbosity(referenced, 2); },
      "UVM report handler count must be bounded");
  SystemVerilogUvmReportLimits one_setting_limits;
  one_setting_limits.max_settings = 1;
  SystemVerilogUvmReportService one_setting{objects, one_setting_limits};
  one_setting.set_id_action(
      reporter, "ONLY", SystemVerilogUvmReportAction::Display);
  one_setting.set_id_action(
      reporter, "ONLY", SystemVerilogUvmReportAction::Log);
  require_throws<std::length_error>(
      [&] { one_setting.set_id_file(reporter, "SECOND", 7); },
      "aggregate UVM report handler settings must be bounded");
  require(
      one_setting.setting_count() == 1
          && one_setting.policy(
                 reporter, SystemVerilogUvmReportSeverity::Info, "ONLY")
                 .action == SystemVerilogUvmReportAction::Log,
      "handler setting replacement must not consume a second budget entry");
  require_throws<std::invalid_argument>(
      [&] {
        reports.set_id_action(
            reporter, "BAD",
            static_cast<SystemVerilogUvmReportAction>(1U << 31U));
      },
      "invalid UVM handler actions must reject");

  {
    const auto component_descriptor = [] {
      SystemVerilogClassDescriptor result;
      result.declared_type = "uvm_pkg::uvm_component";
      result.dynamic_type = "work::report_component";
      result.specialization_identity = "work::report_component";
      result.assignable_declared_types = {
          "work::report_component", "uvm_pkg::uvm_component",
          "uvm_pkg::uvm_object"};
      return result;
    };
    SystemVerilogUvmObjectDescriptor component_type;
    component_type.specialization_identity = "work::report_component";
    component_type.type_name = "report_component";
    objects.register_type(std::move(component_type));
    SystemVerilogUvmComponentService components{heap, objects};
    const auto root = components.create_root("report-root");
    const auto top = heap.allocate(component_descriptor());
    const auto child = heap.allocate(component_descriptor());
    const auto grandchild = heap.allocate(component_descriptor());
    const auto sibling = heap.allocate(component_descriptor());
    for (const auto& [object, name] : std::vector{
             std::pair{top, std::string{"top"}},
             std::pair{child, std::string{"child"}},
             std::pair{grandchild, std::string{"grandchild"}},
             std::pair{sibling, std::string{"sibling"}}}) {
      objects.initialize(object, name);
    }
    components.initialize(top, "top", 0, root);
    components.initialize(child, "child", top, root);
    components.initialize(grandchild, "grandchild", child, root);
    components.initialize(sibling, "sibling", 0, root);
    SystemVerilogUvmReportService hierarchical{
        objects, components};
    hierarchical.set_verbosity_hier(top, 100);
    hierarchical.set_id_verbosity_hier(top, "HIER", 90);
    hierarchical.set_severity_id_verbosity_hier(
        top, SystemVerilogUvmReportSeverity::Warning, "HIER", 80);
    hierarchical.set_severity_action_hier(
        top, SystemVerilogUvmReportSeverity::Warning,
        SystemVerilogUvmReportAction::Log);
    hierarchical.set_id_action_hier(
        top, "HIER", SystemVerilogUvmReportAction::Record);
    hierarchical.set_severity_id_action_hier(
        top, SystemVerilogUvmReportSeverity::Warning, "HIER",
        SystemVerilogUvmReportAction::Display);
    hierarchical.set_default_file_hier(top, 10);
    hierarchical.set_severity_file_hier(
        top, SystemVerilogUvmReportSeverity::Warning, 20);
    hierarchical.set_id_file_hier(top, "HIER", 30);
    hierarchical.set_severity_id_file_hier(
        top, SystemVerilogUvmReportSeverity::Warning, "HIER", 40);
    for (const auto component : {top, child, grandchild}) {
      const auto resolved = hierarchical.policy(
          component, SystemVerilogUvmReportSeverity::Warning, "HIER");
      require(
          resolved.verbosity == 80
              && resolved.action == SystemVerilogUvmReportAction::Display
              && resolved.file == 40,
          "hierarchical report policy must cover the complete component subtree");
    }
    require(
        hierarchical.policy(
            sibling, SystemVerilogUvmReportSeverity::Warning, "HIER")
                .verbosity == 200
            && hierarchical.handler_count() == 3
            && hierarchical.setting_count() == 18,
        "hierarchical report policy must not cross into sibling subtrees");
    SystemVerilogUvmReportLimits hierarchical_limits;
    hierarchical_limits.max_handlers = 2;
    hierarchical_limits.max_settings = 2;
    SystemVerilogUvmReportService bounded_hierarchy{
        objects, components, hierarchical_limits};
    require_throws<std::length_error>(
        [&] {
          bounded_hierarchy.set_id_action_hier(
              top, "ROLLBACK", SystemVerilogUvmReportAction::Display);
        },
        "bounded hierarchical report policy must reject oversized subtrees");
    require(
        bounded_hierarchy.handler_count() == 0
            && bounded_hierarchy.setting_count() == 0
            && bounded_hierarchy.policy(
                   child, SystemVerilogUvmReportSeverity::Info, "ROLLBACK")
                   .action == SystemVerilogUvmReportAction::Display,
        "failed hierarchical report policy must roll back every descendant");
    require_throws<std::invalid_argument>(
        [&] { hierarchical.set_verbosity_hier(reporter, 1); },
        "hierarchical report policy must reject non-component objects");
  }

  require(
      reports.server().severity_count(
          SystemVerilogUvmReportSeverity::Info) == 3
          && reports.server().severity_count(
                 SystemVerilogUvmReportSeverity::Warning) == 1
          && reports.server().severity_count(
                 SystemVerilogUvmReportSeverity::Error) == 1
          && reports.server().id_count("BUILD") == 3
          && reports.server().id_count("DEFAULT") == 1
          && reports.server().id_count("HOOK") == 1,
      "integrated report-server counts must include only routed messages");
  reports.set_id_action(
      reporter, "SUPPRESS", SystemVerilogUvmReportAction::None);
  auto suppressed = hooked;
  suppressed.id = "SUPPRESS";
  const auto info_before_suppression = reports.server().severity_count(
      SystemVerilogUvmReportSeverity::Info);
  require(
      !reports.report(suppressed)
          && reports.action_filtered_count() == 1
          && reports.server().severity_count(
                 SystemVerilogUvmReportSeverity::Info)
              == info_before_suppression
          && reports.server().id_count("SUPPRESS") == 0,
      "UVM NO_ACTION must suppress server accounting and routed identity");

  SystemVerilogUvmReportRequest numeric_request;
  numeric_request.report_object = reporter;
  numeric_request.id = "NUMERIC";
  numeric_request.message = "radices";
  const auto numeric_value = PackedLogic4::from_aval_bval(8, 0xa5, 0);
  numeric_request.elements.add_integer(
      "binary", numeric_value, 8, SystemVerilogUvmReportRadix::Binary);
  numeric_request.elements.add_integer(
      "octal", numeric_value, 8, SystemVerilogUvmReportRadix::Octal);
  numeric_request.elements.add_integer(
      "signed", numeric_value, 8, SystemVerilogUvmReportRadix::Decimal);
  numeric_request.elements.add_integer(
      "unsigned", numeric_value, 8, SystemVerilogUvmReportRadix::Unsigned);
  numeric_request.elements.add_integer(
      "hex", numeric_value, 8, SystemVerilogUvmReportRadix::Hexadecimal);
  numeric_request.elements.add_integer(
      "unknown", PackedLogic4(4, Logic4::x), 4,
      SystemVerilogUvmReportRadix::Hexadecimal);
  require(
      reports.compose_payload(reports.construct(numeric_request))
          == "radices\n +binary = 8'b10100101"
             "\n +octal = 8'o245"
             "\n +signed = 8'sd-91"
             "\n +unsigned = 8'd165"
             "\n +hex = 8'hA5"
             "\n +unknown = 4'hX",
      "UVM report numeric elements must format every radix deterministically");

  SystemVerilogUvmReportServer server;
  server.set_show_verbosity(true);
  server.set_show_terminator(true);
  std::vector<std::string> displayed;
  std::vector<std::pair<std::uint64_t, std::string>> logged;
  std::size_t recorded{};
  std::vector<SystemVerilogUvmReportExecution> controlled;
  server.set_display_sink(
      [&](const auto record) { displayed.emplace_back(record); });
  server.set_file_sink([&](const auto file, const auto record) {
    logged.emplace_back(file, record);
  });
  server.set_record_sink([&](const auto&) { ++recorded; });
  server.set_control_sink(
      [&](const auto& execution) { controlled.push_back(execution); });
  require(server.set_max_quit_count(2, false),
          "first UVM max-quit setting must apply");
  require(!server.set_max_quit_count(9),
          "non-overridable UVM max-quit setting must remain fixed");

  SystemVerilogUvmReportMessage server_message;
  server_message.report_object = reporter;
  server_message.report_object_name = "uvm_test_top.env.agent";
  server_message.severity = SystemVerilogUvmReportSeverity::Info;
  server_message.id = "FORMAT";
  server_message.message = "body";
  server_message.verbosity = 100;
  server_message.filename = "server_test.sv";
  server_message.line = 88;
  server_message.timestamp = 42;
  server_message.context = "run";
  server_message.action =
      SystemVerilogUvmReportAction::Display
      | SystemVerilogUvmReportAction::Log
      | SystemVerilogUvmReportAction::Record
      | SystemVerilogUvmReportAction::Count;
  server_message.file = 3;
  SystemVerilogUvmReportExecution first_execution;
  require(
      server.process(server_message, "body", &first_execution)
          && first_execution.composed
              == "UVM_INFO(UVM_LOW) server_test.sv(88) @ 42: "
                 "uvm_test_top.env.agent@@run [FORMAT] body -UVM_INFO"
          && first_execution.displayed && first_execution.logged
          && first_execution.recorded && !first_execution.exit_requested
          && first_execution.file == 2,
      "UVM report server must compose and execute display/log/record/count in order");
  SystemVerilogUvmReportExecution second_execution;
  require(
      server.process(server_message, "body", &second_execution)
          && second_execution.exit_requested
          && has_action(
              server_message.action, SystemVerilogUvmReportAction::Exit)
          && server.quit_count() == 2,
      "UVM max-quit count must add EXIT exactly when its threshold is reached");
  SystemVerilogUvmReportMessage stop_message = server_message;
  stop_message.severity = SystemVerilogUvmReportSeverity::Warning;
  stop_message.id = "STOP";
  stop_message.action = SystemVerilogUvmReportAction::Stop;
  stop_message.file = 0;
  SystemVerilogUvmReportExecution stop_execution;
  require(
      server.process(stop_message, {}, &stop_execution)
          && stop_execution.stop_requested
          && !stop_execution.displayed && !stop_execution.logged,
      "UVM STOP must request control without composing unused output");
  require(
      displayed.size() == 2 && logged.size() == 2 && recorded == 2
          && displayed[0] == first_execution.composed + "\n"
          && logged[0]
              == std::pair<std::uint64_t, std::string>{
                     2, first_execution.composed + "\n"}
          && controlled.size() == 2
          && controlled[0].exit_requested
          && controlled[1].stop_requested,
      "UVM server sinks must receive exact newline records and ordered control outcomes");
  require(
      server.summarize()
          == "\n--- UVM Report Summary ---\n\n"
             "Quit count reached!\n"
             "Quit count :     2 of     2\n"
             "** Report counts by severity\n"
             "UVM_INFO :    2\n"
             "UVM_WARNING :    1\n"
             "UVM_ERROR :    0\n"
             "UVM_FATAL :    0\n"
             "** Report counts by id\n"
             "[FORMAT]     2\n"
             "[STOP]     1\n",
      "UVM report summary must have deterministic ordering, widths, and newlines");

  server.reset_counts();
  server.set_record_all_messages(true);
  server.set_show_verbosity(false);
  server.set_show_terminator(false);
  server_message.action = SystemVerilogUvmReportAction::Display
      | SystemVerilogUvmReportAction::Log;
  server_message.file = 0x8000'0001ULL;
  server_message.id = "STDOUT";
  require(
      server.process(server_message, "plain")
          && recorded == 3 && displayed.size() == 3
          && logged.size() == 2
          && server.severity_count(SystemVerilogUvmReportSeverity::Info) == 1,
      "record-all must add RECORD while stdout LOG suppression avoids duplicates");
  server.set_display_sink([](const auto) {
    throw std::runtime_error{"display failed"};
  });
  server_message.id = "SINK_FAIL";
  require(
      server.process(server_message, "contained")
          && server.sink_failures() == 1
          && server.id_count("SINK_FAIL") == 1,
      "server sink exceptions must be contained after stable accounting");

  SystemVerilogUvmReportServerLimits one_id_server_limits;
  one_id_server_limits.max_ids = 1;
  SystemVerilogUvmReportServer one_id_server{one_id_server_limits};
  auto bounded_server_message = server_message;
  bounded_server_message.action = SystemVerilogUvmReportAction::Display;
  bounded_server_message.id = "ONE";
  require(one_id_server.process(bounded_server_message, "one"),
          "first bounded UVM report ID must process");
  bounded_server_message.id = "TWO";
  require_throws<std::length_error>(
      [&] { (void)one_id_server.process(bounded_server_message, "two"); },
      "UVM report-server ID count must be bounded");
  require(
      one_id_server.severity_count(
          SystemVerilogUvmReportSeverity::Info) == 1
          && one_id_server.id_count("TWO") == 0,
      "failed server count publication must be transactional");
  SystemVerilogUvmReportServerLimits output_limits;
  output_limits.max_output_bytes = 8;
  SystemVerilogUvmReportServer output_bounded{output_limits};
  require_throws<std::length_error>(
      [&] { (void)output_bounded.compose(server_message, "body"); },
      "UVM report-server output must be byte bounded");
  SystemVerilogUvmReportServer overflow_server;
  overflow_server.set_severity_count(
      SystemVerilogUvmReportSeverity::Info,
      std::numeric_limits<std::uint64_t>::max());
  require_throws<std::overflow_error>(
      [&] { (void)overflow_server.process(server_message, "overflow"); },
      "UVM report severity counts must reject overflow");

  SystemVerilogUvmReportService catcher_reports{objects};
  std::vector<std::string> catcher_order;
  std::vector<SystemVerilogUvmReportMessage> catcher_routed;
  catcher_reports.set_route_hook(
      [&](const auto& message) { catcher_routed.push_back(message); });
  const auto demoter = catcher_reports.add_catcher(
      0, "demoter", [&](auto& context) {
        catcher_order.push_back("demoter");
        require(
            context.message().severity
                == SystemVerilogUvmReportSeverity::Fatal,
            "prepend catcher must execute before an appended demoter");
        context.set_severity(SystemVerilogUvmReportSeverity::Error);
        context.set_message("demoted");
        return SystemVerilogUvmReportCatcherResult::Throw;
      });
  const auto instance_modifier = catcher_reports.add_catcher(
      reporter, "instance", [&](auto& context) {
        catcher_order.push_back("instance");
        require(
            context.message().severity
                    == SystemVerilogUvmReportSeverity::Error
                && context.message().message == "demoted",
            "later catchers must observe retained earlier modifications");
        context.set_id("MODIFIED");
        context.set_verbosity(321);
        context.set_context("catcher-context");
        context.elements().add_string("catcher", "added");
        return SystemVerilogUvmReportCatcherResult::Throw;
      });
  const auto failing = catcher_reports.add_catcher(
      reporter, "failing", [&](auto& context) {
        catcher_order.push_back("failing");
        context.set_message("must-roll-back");
        throw std::runtime_error{"catcher failed"};
        return SystemVerilogUvmReportCatcherResult::Throw;
      });
  SystemVerilogUvmReportCatcherHandle removed_during_dispatch{};
  const auto remover = catcher_reports.add_catcher(
      reporter, "remover", [&](auto&) {
        catcher_order.push_back("remover");
        require(
            catcher_reports.remove_catcher(removed_during_dispatch),
            "a catcher must be removable during dispatch");
        return SystemVerilogUvmReportCatcherResult::Throw;
      });
  removed_during_dispatch = catcher_reports.add_catcher(
      reporter, "removed", [&](auto&) {
        catcher_order.push_back("removed");
        return SystemVerilogUvmReportCatcherResult::Throw;
      });
  const auto head = catcher_reports.add_catcher(
      0, "head", [&](auto&) {
        catcher_order.push_back("head");
        return SystemVerilogUvmReportCatcherResult::Throw;
      },
      SystemVerilogUvmReportCatcherOrdering::Prepend);
  (void)demoter;
  (void)instance_modifier;
  (void)failing;
  (void)remover;
  (void)head;

  SystemVerilogUvmReportRequest catcher_request;
  catcher_request.report_object = reporter;
  catcher_request.severity = SystemVerilogUvmReportSeverity::Fatal;
  catcher_request.id = "ORIGINAL";
  catcher_request.message = "original";
  require(
      catcher_reports.report(catcher_request)
          && catcher_order == std::vector<std::string>{
                 "head", "demoter", "instance", "failing", "remover"}
          && catcher_routed.size() == 1
          && catcher_routed[0].severity
              == SystemVerilogUvmReportSeverity::Error
          && catcher_routed[0].id == "MODIFIED"
          && catcher_routed[0].message == "demoted"
          && catcher_routed[0].verbosity == 321
          && catcher_routed[0].context == "catcher-context"
          && catcher_routed[0].elements.size() == 1
          && catcher_routed[0].action
              == (SystemVerilogUvmReportAction::Display
                  | SystemVerilogUvmReportAction::Count),
      "catchers must preserve prepend/append order, modifications, rollback, removal, and default-action remapping");
  require(
      catcher_reports.catcher_invocations() == 5
          && catcher_reports.catcher_failures() == 1
          && catcher_reports.demoted_count(
                 SystemVerilogUvmReportSeverity::Fatal) == 1
          && catcher_reports.server().severity_count(
                 SystemVerilogUvmReportSeverity::Fatal) == 0
          && catcher_reports.server().severity_count(
                 SystemVerilogUvmReportSeverity::Error) == 1
          && catcher_reports.server().id_count("ORIGINAL") == 0
          && catcher_reports.server().id_count("MODIFIED") == 1,
      "report-server accounting must use the post-catcher severity and ID");
  require(
      !catcher_reports.remove_catcher(removed_during_dispatch)
          && catcher_reports.catcher_count() == 5,
      "removal during dispatch must publish once and stale removal must reject");
  catcher_reports.set_catcher_enabled(instance_modifier, false);
  require(!catcher_reports.catcher_enabled(instance_modifier),
          "catcher callback mode must disable an existing catcher");
  require_throws<std::invalid_argument>(
      [&] { catcher_reports.set_catcher_enabled(999'999, true); },
      "unknown catcher handles must reject callback-mode changes");

  SystemVerilogUvmReportService caught_reports{objects};
  std::size_t after_caught{};
  const auto catching = caught_reports.add_catcher(
      0, "catch", [&](auto& context) {
        context.set_severity(SystemVerilogUvmReportSeverity::Info);
        return SystemVerilogUvmReportCatcherResult::Caught;
      });
  const auto skipped = caught_reports.add_catcher(
      0, "skipped", [&](auto&) {
        ++after_caught;
        return SystemVerilogUvmReportCatcherResult::Throw;
      });
  SystemVerilogUvmReportRequest caught_request;
  caught_request.report_object = reporter;
  caught_request.severity = SystemVerilogUvmReportSeverity::Warning;
  caught_request.id = "CAUGHT";
  caught_request.message = "caught";
  require(
      !caught_reports.report(caught_request) && after_caught == 0
          && caught_reports.caught_count(
                 SystemVerilogUvmReportSeverity::Warning) == 1
          && caught_reports.demoted_count(
                 SystemVerilogUvmReportSeverity::Warning) == 1
          && caught_reports.server().id_count("CAUGHT") == 0,
      "CAUGHT must stop later callbacks and suppress server accounting");
  require(caught_reports.remove_catcher(catching)
              && caught_reports.remove_catcher(skipped),
          "registered catchers must be explicitly removable");
  std::vector<SystemVerilogUvmReportMessage> caught_routed;
  caught_reports.set_route_hook(
      [&](const auto& message) { caught_routed.push_back(message); });
  require(
      caught_reports.report(caught_request)
          && caught_routed.size() == 1 && caught_routed[0].sequence == 1,
      "caught messages must not consume routed sequence identity");
  require(
      caught_reports.catcher_summary()
          == "** Report catcher summary\n"
             "Caught UVM_INFO :     0\n"
             "Caught UVM_WARNING :     1\n"
             "Caught UVM_ERROR :     0\n"
             "Caught UVM_FATAL :     0\n"
             "Demoted UVM_INFO :     0\n"
             "Demoted UVM_WARNING :     1\n"
             "Demoted UVM_ERROR :     0\n"
             "Demoted UVM_FATAL :     0\n",
      "catcher statistics must summarize deterministically");

  SystemVerilogUvmReportService reentrant_reports{objects};
  std::vector<SystemVerilogUvmReportMessage> reentrant_routed;
  reentrant_reports.set_route_hook(
      [&](const auto& message) { reentrant_routed.push_back(message); });
  const auto reentrant = reentrant_reports.add_catcher(
      reporter, "reentrant", [&](auto& context) {
        if (context.message().id == "OUTER") {
          auto inner = caught_request;
          inner.severity = SystemVerilogUvmReportSeverity::Info;
          inner.id = "INNER";
          inner.message = "inner";
          require(reentrant_reports.report(inner),
                  "reports issued from a catcher must bypass catcher recursion");
        }
        return SystemVerilogUvmReportCatcherResult::Throw;
      });
  (void)reentrant;
  auto outer = caught_request;
  outer.id = "OUTER";
  require(
      reentrant_reports.report(outer)
          && reentrant_routed.size() == 2
          && reentrant_routed[0].id == "INNER"
          && reentrant_routed[0].sequence == 1
          && reentrant_routed[1].id == "OUTER"
          && reentrant_routed[1].sequence == 2
          && reentrant_reports.catcher_invocations() == 1
          && reentrant_reports.catcher_reentry_bypasses() == 1,
      "catcher re-entry must bypass recursive dispatch while retaining unique route order");

  SystemVerilogUvmReportService explicit_action_reports{objects};
  std::vector<SystemVerilogUvmReportMessage> explicit_routed;
  explicit_action_reports.set_route_hook(
      [&](const auto& message) { explicit_routed.push_back(message); });
  const auto explicit_action = explicit_action_reports.add_catcher(
      reporter, "explicit", [&](auto& context) {
        context.set_severity(SystemVerilogUvmReportSeverity::Warning);
        context.set_action(SystemVerilogUvmReportAction::Record);
        return SystemVerilogUvmReportCatcherResult::Throw;
      });
  (void)explicit_action;
  require(
      explicit_action_reports.report(catcher_request)
          && explicit_routed[0].action
              == SystemVerilogUvmReportAction::Record,
      "an explicitly modified catcher action must suppress severity-default remapping");

  SystemVerilogUvmReportLimits catcher_limits;
  catcher_limits.max_catchers = 1;
  catcher_limits.max_catcher_name_bytes = 4;
  SystemVerilogUvmReportService bounded_catchers{objects, catcher_limits};
  const auto only_catcher = bounded_catchers.add_catcher(
      reporter, "only", [](auto&) {
        return SystemVerilogUvmReportCatcherResult::Throw;
      });
  (void)only_catcher;
  require_throws<std::length_error>(
      [&] {
        (void)bounded_catchers.add_catcher(reporter, "next", [](auto&) {
          return SystemVerilogUvmReportCatcherResult::Throw;
        });
      },
      "UVM report catcher registration count must be bounded");
  require_throws<std::length_error>(
      [&] {
        SystemVerilogUvmReportService value{objects, catcher_limits};
        (void)value.add_catcher(reporter, "large", [](auto&) {
          return SystemVerilogUvmReportCatcherResult::Throw;
        });
      },
      "UVM report catcher names must be bounded");
  require_throws<std::invalid_argument>(
      [&] {
        SystemVerilogUvmReportService value{objects};
        (void)value.add_catcher(reporter, "empty", {});
      },
      "empty UVM report catcher callbacks must reject");
  require_throws<std::invalid_argument>(
      [&] {
        SystemVerilogUvmReportService value{objects};
        (void)value.add_catcher(
            reporter, "order", [](auto&) {
              return SystemVerilogUvmReportCatcherResult::Throw;
            },
            static_cast<SystemVerilogUvmReportCatcherOrdering>(255));
      },
      "invalid UVM report catcher ordering must reject");
  catcher_limits.max_catchers = 2;
  catcher_limits.max_catcher_dispatches = 1;
  SystemVerilogUvmReportService bounded_dispatch{objects, catcher_limits};
  for (const auto name : {"one", "two"}) {
    (void)bounded_dispatch.add_catcher(reporter, name, [](auto&) {
      return SystemVerilogUvmReportCatcherResult::Throw;
    });
  }
  require_throws<std::length_error>(
      [&] { (void)bounded_dispatch.report(caught_request); },
      "per-message UVM report catcher dispatch work must be bounded");
  require(
      bounded_dispatch.catcher_invocations() == 0
          && bounded_dispatch.server().id_count("CAUGHT") == 0,
      "oversized catcher dispatch must reject before callbacks or accounting");

  objects.erase(referenced);
  require_throws<std::invalid_argument>(
      [&] { (void)reports.compose_payload(routed.front()); },
      "stale UVM report element object handles must reject");
  objects.erase(reporter);
  require_throws<std::invalid_argument>(
      [&] { (void)reports.construct(defaults); },
      "stale UVM report-object handles must reject");
}

}  // namespace fsim::tests::runtime
