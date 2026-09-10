// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include <algorithm>
#include <iostream>
#include <string>

namespace fsim::tests::elaboration {

void test_systemverilog_interfaces() {
  const auto virtual_interfaces = fsim::frontend::parse_text(
      "virtual-interfaces.sv",
      R"(
interface virtual_if #(parameter int WIDTH = 8);
  logic clock;
  logic value;
  function automatic logic sample(); return value; endfunction
  clocking cb @(posedge clock);
    input #0 value;
  endclocking
  modport observer(input value, import function sample, clocking cb);
endinterface
module virtual_sink(virtual_if.observer bus);
endmodule
module virtual_forward(interface bus);
  virtual virtual_if.observer forwarded = bus;
endmodule
module virtual_interface_top;
  virtual_if link();
  virtual_if links[2:0]();
  virtual_sink high(.bus(links[2]));
  virtual_sink low(.bus(links[0]));
  virtual_forward forward(.bus(links[1]));
  virtual virtual_if #(.WIDTH(8)).observer selected = link;
  virtual interface virtual_if nullable = null;
  virtual virtual_if #(8).observer from_array = links[1],
      same_array = links[1], different_array = links[0];
  virtual virtual_if unrestricted = link;
  virtual virtual_if.observer narrowed = unrestricted;
  virtual virtual_if.observer copied = narrowed;
  virtual virtual_if #(8) positional = link;
  virtual virtual_if #(.WIDTH(8)).observer named_narrowed = positional;
  virtual virtual_if runtime_source = null;
  virtual virtual_if.observer runtime_narrowed = null;
  initial begin
    runtime_source = link;
    runtime_narrowed = link;
    runtime_narrowed = copied;
    runtime_narrowed = null;
    runtime_narrowed = runtime_source;
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(virtual_interfaces.ok());
  const auto virtual_result = fsim::elaboration::elaborate(
      virtual_interfaces.design, "sv:work.virtual_interface_top");
  if (!virtual_result.ok()) {
    for (const auto& diagnostic : virtual_result.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(virtual_result.ok());
  const auto selected = virtual_result.design->find_signal(
      "virtual_interface_top.selected");
  const auto nullable = virtual_result.design->find_signal(
      "virtual_interface_top.nullable");
  const auto from_array = virtual_result.design->find_signal(
      "virtual_interface_top.from_array");
  const auto same_array = virtual_result.design->find_signal(
      "virtual_interface_top.same_array");
  const auto different_array = virtual_result.design->find_signal(
      "virtual_interface_top.different_array");
  const auto unrestricted = virtual_result.design->find_signal(
      "virtual_interface_top.unrestricted");
  const auto narrowed = virtual_result.design->find_signal(
      "virtual_interface_top.narrowed");
  const auto copied = virtual_result.design->find_signal(
      "virtual_interface_top.copied");
  const auto positional = virtual_result.design->find_signal(
      "virtual_interface_top.positional");
  const auto named_narrowed = virtual_result.design->find_signal(
      "virtual_interface_top.named_narrowed");
  const auto runtime_source = virtual_result.design->find_signal(
      "virtual_interface_top.runtime_source");
  const auto runtime_narrowed = virtual_result.design->find_signal(
      "virtual_interface_top.runtime_narrowed");
  const auto high_member = virtual_result.design->find_signal(
      "virtual_interface_top.links[2].value");
  const auto middle_member = virtual_result.design->find_signal(
      "virtual_interface_top.links[1].value");
  const auto low_member = virtual_result.design->find_signal(
      "virtual_interface_top.links[0].value");
  const auto forwarded = virtual_result.design->find_signal(
      "virtual_interface_top.forward.forwarded");
  const auto forwarded_clock = virtual_result.design->find_signal(
      "virtual_interface_top.high.bus.cb");
  const auto forwarded_sample = virtual_result.design->find_signal(
      "virtual_interface_top.high.bus.cb.value");
  assert(
      selected && nullable && from_array && same_array && different_array
      && unrestricted && narrowed && copied && positional
      && named_narrowed && runtime_source && runtime_narrowed
      && high_member && middle_member && low_member && forwarded
      && forwarded_clock && forwarded_sample);
  auto virtual_interpreter =
      virtual_result.design->create_interpreter();
  const auto selected_array_value =
      virtual_interpreter->signal_value(*from_array).low_word();
  assert(
      virtual_result.design->signals().at(*selected).width == 64
      && virtual_interpreter->signal_value(*selected).low_word().aval != 0
      && virtual_interpreter->signal_value(*selected).low_word().bval == 0
      && virtual_interpreter->signal_value(*nullable).low_word().aval == 0
      && virtual_interpreter->signal_value(*nullable).low_word().bval == 0
      && selected_array_value.aval != 0
      && selected_array_value.bval == 0
      && virtual_interpreter->signal_value(*same_array).low_word()
          == selected_array_value
      && virtual_interpreter->signal_value(*different_array).low_word()
          != selected_array_value
      && virtual_interpreter->signal_value(*forwarded).low_word()
          == selected_array_value
      && virtual_interpreter->signal_value(*unrestricted).low_word().aval != 0
      && virtual_interpreter->signal_value(*narrowed).low_word()
          == virtual_interpreter->signal_value(*unrestricted).low_word()
      && virtual_interpreter->signal_value(*copied).low_word()
          == virtual_interpreter->signal_value(*unrestricted).low_word()
      && virtual_interpreter->signal_value(*positional).low_word()
          == virtual_interpreter->signal_value(*unrestricted).low_word()
      && virtual_interpreter->signal_value(*named_narrowed).low_word()
          == virtual_interpreter->signal_value(*unrestricted).low_word());
  (void)virtual_interpreter->run();
  assert(
      virtual_interpreter->signal_value(*runtime_source).low_word()
          == virtual_interpreter->signal_value(*unrestricted).low_word()
      && virtual_interpreter->signal_value(*runtime_narrowed).low_word()
          == virtual_interpreter->signal_value(*unrestricted).low_word());

  const auto invalid_virtual = fsim::frontend::parse_text(
      "invalid-virtual-interfaces.sv",
      R"(
interface first_if;
  modport view();
endinterface
interface second_if;
endinterface
interface parameterized_if #(parameter int WIDTH = 8);
  modport view();
endinterface
module generic_widen(interface bus); endmodule
module restricted_forward(first_if.view bus);
  generic_widen child(bus);
endmodule
module invalid_virtual_top;
  first_if first();
  second_if second();
  first_if first_array[1:0]();
  parameterized_if #(.WIDTH(16)) wide();
  restricted_forward widened(.bus(first));
  integer selected_index;
  virtual missing_if missing_type;
  virtual first_if.missing missing_view;
  virtual first_if.view unknown_target = absent;
  virtual first_if.view wrong_type = second;
  virtual first_if.view expression_target = first == first;
  virtual first_if.view out_of_range = first_array[2];
  virtual first_if.view dynamic_index = first_array[selected_index];
  virtual parameterized_if #(.WIDTH(8)).view
      wrong_parameters = wide;
  virtual first_if.view restricted = first;
  virtual first_if widened_view = restricted;
  virtual first_if procedural_widened = null;
  initial procedural_widened = restricted;
  virtual parameterized_if #(.WIDTH(16)).view wide_virtual = wide;
  virtual parameterized_if #(.WIDTH(8)).view narrow_virtual = null;
  initial begin
    narrow_virtual = wide_virtual;
    narrow_virtual = wide;
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(invalid_virtual.ok());
  const auto invalid_virtual_result = fsim::elaboration::elaborate(
      invalid_virtual.design, "sv:work.invalid_virtual_top");
  assert(!invalid_virtual_result.ok());
  for (const auto code : {
           "FSIM-ELAB-SVIFACE-002",
           "FSIM-ELAB-SVIFACE-003",
           "FSIM-ELAB-SVIFACE-004",
           "FSIM-ELAB-SVIFACE-010",
           "FSIM-ELAB-SVIFACE-011",
           "FSIM-ELAB-SVIFACE-012"}) {
    assert(has_diagnostic(invalid_virtual_result, code));
  }

  auto invalid = fsim::frontend::parse_text(
      "invalid-interfaces.sv",
      R"(
interface a_if;
  logic value;
  function automatic logic sample(); return value; endfunction
  modport reader(input value);
  modport provider(export function sample);
endinterface
interface b_if;
  logic value;
  modport reader(input value);
endinterface
module wrong_type(a_if.reader bus); endmodule
module wrong_view(a_if.missing bus); endmodule
module input_writer(a_if.reader bus);
  initial bus.value = 1'b1;
endmodule
module missing_export(a_if.provider bus); endmodule
module invalid_interfaces_top;
  a_if a();
  b_if b();
  wrong_type type_error(.bus(b));
  wrong_view view_error(.bus(a));
  input_writer write_error(.bus(a));
  missing_export export_error(.bus(a));
  wrong_type expression_error(.bus(a.value + 1'b1));
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(invalid.ok());
  const auto result = fsim::elaboration::elaborate(
      invalid.design, "sv:work.invalid_interfaces_top");
  assert(!result.ok());
  for (const auto code : {
           "FSIM-ELAB-SVIFACE-001",
           "FSIM-ELAB-SVIFACE-003",
           "FSIM-ELAB-SVIFACE-004",
           "FSIM-ELAB-SVIFACE-006",
           "FSIM-ELAB-SVIFACE-009"}) {
    assert(has_diagnostic(result, code));
  }

  auto malformed = fsim::frontend::parse_text(
      "malformed-interface-hir.sv",
      R"(
interface malformed_if;
  logic value;
  function automatic logic sample(); return value; endfunction
  modport view(input value, import function sample);
endinterface
module malformed_user(malformed_if.view bus); endmodule
module malformed_interface_top;
  malformed_if link();
  malformed_user child(.bus(link));
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(malformed.ok());
  const auto interface = std::ranges::find_if(
      malformed.design.units,
      [](const auto& unit) {
        return unit.kind
            == fsim::frontend::UnitKind::SystemVerilogInterface;
      });
  assert(interface != malformed.design.units.end());
  auto& members = interface->systemverilog_modports.front().members;
  auto missing_signal = members.front();
  missing_signal.name = "missing_signal";
  members.push_back(std::move(missing_signal));
  auto missing_callable = members[1];
  missing_callable.name = "missing_callable";
  members.push_back(std::move(missing_callable));
  members.push_back(members[1]);
  const auto malformed_result = fsim::elaboration::elaborate(
      malformed.design, "sv:work.malformed_interface_top");
  assert(!malformed_result.ok());
  for (const auto code : {
           "FSIM-ELAB-SVIFACE-005",
           "FSIM-ELAB-SVIFACE-007",
           "FSIM-ELAB-SVIFACE-008"}) {
    assert(has_diagnostic(malformed_result, code));
  }

  const auto scalar_callable = fsim::frontend::parse_text(
      "scalar-interface-callable.sv",
      R"(
interface scalar_service_if;
  function automatic real convert(input real value);
    return value;
  endfunction
  function automatic chandle retain(
      input chandle value,
      input chandle fallback = null);
    return chandle'(value);
  endfunction
  modport provider(export function convert, export function retain);
endinterface
module scalar_service_user(scalar_service_if.provider service);
  function automatic real convert(input real value);
    return value;
  endfunction
  function automatic chandle retain(
      input chandle value,
      input chandle fallback = null);
    return value;
  endfunction
endmodule
module scalar_service_top;
  scalar_service_if service();
  scalar_service_user user(service);
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(scalar_callable.ok());
  const auto scalar_callable_result = fsim::elaboration::elaborate(
      scalar_callable.design, "sv:work.scalar_service_top");
  assert(scalar_callable_result.ok());

  const auto wide_interface = fsim::frontend::parse_text(
      "wide-interface-values.sv",
      R"(
interface wide_value_if #(
    parameter logic [136:0] RESET =
        137'h10000000000000000000000000000000001
);
  struct packed {
    logic [72:0] upper;
    logic [63:0] lower;
  } value;
  initial value = '{
    upper: RESET[136:64],
    lower: RESET[63:0]
  };
  modport consumer(input value);
endinterface
module wide_value_sink(
    wide_value_if.consumer bus,
    output logic [136:0] observed
);
  assign observed = bus.value;
endmodule
module wide_interface_top;
  localparam logic [136:0] ROOT_VALUE =
      137'h10000000000000000000000000000000001;
  wide_value_if #(.RESET(ROOT_VALUE)) link();
  logic [136:0] observed;
  wide_value_sink sink(link, observed);
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(wide_interface.ok());
  const auto wide_interface_result = fsim::elaboration::elaborate(
      wide_interface.design, "sv:work.wide_interface_top");
  if (!wide_interface_result.ok()) {
    for (const auto& diagnostic : wide_interface_result.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(wide_interface_result.ok());
  const auto wide_observed =
      wide_interface_result.design->find_signal(
          "wide_interface_top.observed");
  assert(wide_observed);
  assert(
      wide_interface_result.design->signals().at(*wide_observed).width
      == 137);
  auto wide_interface_interpreter =
      wide_interface_result.design->create_interpreter();
  assert(
      wide_interface_interpreter->run().status
      == fsim::runtime::RunStatus::completed);
  assert(
      wide_interface_interpreter
          ->signal_value(*wide_observed)
          .to_msb_string()
      == "1" + std::string(135, '0') + "1");

  const auto mismatched_scalar_callable = fsim::frontend::parse_text(
      "scalar-interface-callable-mismatch.sv",
      R"(
interface scalar_mismatch_if;
  function automatic real convert(input real value);
    return value;
  endfunction
  function automatic chandle retain(input chandle value);
    return value;
  endfunction
  modport provider(export function convert, export function retain);
endinterface
module scalar_mismatch_user(scalar_mismatch_if.provider service);
  function automatic shortreal convert(input real value);
    return value;
  endfunction
  function automatic real retain(input chandle value);
    return 0.0;
  endfunction
endmodule
module scalar_mismatch_top;
  scalar_mismatch_if service();
  scalar_mismatch_user user(service);
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(mismatched_scalar_callable.ok());
  const auto mismatched_scalar_result = fsim::elaboration::elaborate(
      mismatched_scalar_callable.design, "sv:work.scalar_mismatch_top");
  assert(!mismatched_scalar_result.ok());
  assert(has_diagnostic(
      mismatched_scalar_result, "FSIM-ELAB-SVIFACE-009"));
}

void test_systemverilog_program_instances() {
  const auto parsed = fsim::frontend::parse_text(
      "program-instances.sv",
      R"(
package program_types;
  typedef logic [3:0] nibble_t;
endpackage

program driver #(parameter int WIDTH = 8) (
  input logic [WIDTH-1:0] stimulus,
  output logic [WIDTH-1:0] observed
);
  import program_types::*;
  nibble_t retained;
  initial begin
    $display("program-initial");
    observed = stimulus;
  end
  final $display("program-final");
endprogram

program standalone_program;
  import program_types::*;
  nibble_t retained;
endprogram

module program_host;
  logic [3:0] stimulus;
  logic [3:0] observed;
  initial begin
    $display("module-initial");
    stimulus <= 4'ha;
  end
  final $display("module-final");
  driver #(.WIDTH(4)) active_driver(stimulus, observed);
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(parsed.ok());

  const auto host = fsim::elaboration::elaborate(
      parsed.design, "sv:work.program_host");
  if (!host.ok()) {
    for (const auto& diagnostic : host.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(host.ok());
  assert(host.design->specializations().size() == 2);
  const auto child = std::ranges::find(
      host.design->specializations(),
      "program_host.active_driver",
      &fsim::elaboration::SpecializationInfo::instance);
  assert(child != host.design->specializations().end());
  assert(child->unit == "sv:work.program(driver)");
  assert((
      child->parameter_values
      == std::vector<std::pair<std::string, std::string>>{
          {"WIDTH", "4"}}));
  const auto retained = host.design->find_signal(
      "program_host.active_driver.retained");
  assert(retained);
  assert(host.design->signals().at(*retained).width == 4);
  assert(
      std::ranges::count_if(
          host.design->processes(),
          [](const auto& process) { return process.reactive; })
      == 2);
  assert(
      std::ranges::count_if(
          host.design->processes(),
          [](const auto& process) {
            return process.reactive && process.final;
          })
      == 1);
  const auto observed = host.design->find_signal(
      "program_host.observed");
  assert(observed);
  auto runtime = host.design->create_interpreter();
  struct LifecycleEvent {
    std::string text;
    std::string process;
    fsim::runtime::SchedulerPhase phase;
  };
  std::vector<LifecycleEvent> lifecycle;
  std::vector<fsim::runtime::simir::ExecutionPoint> debug_points;
  runtime->set_execution_point_hook(
      [&](fsim::runtime::Scheduler&,
          const fsim::runtime::simir::ExecutionPoint& point) {
        if (host.design->processes()
                .at(point.process)
                .name
                .starts_with("program_host.active_driver.")) {
          debug_points.push_back(point);
        }
      });
  runtime->set_output_hook(
      [&](const fsim::runtime::simir::ProcessId process,
          const std::string_view text,
          const bool,
          const fsim::runtime::SimulationTick,
          const std::uint64_t) {
          const auto phase = runtime->scheduler().current_phase();
          assert(phase);
          lifecycle.push_back({ std::string { text },
              host.design->processes().at(process).name,
              *phase });
      });
  const auto run = runtime->run();
  const auto observed_value = runtime->signal_value(*observed).to_msb_string();
  if (run.status != fsim::runtime::RunStatus::stopped
      || observed_value != "1010") {
      std::cerr << "program instance run status="
                << static_cast<int>(run.status)
                << " observed=" << observed_value << '\n';
  }
  assert(
      run.status == fsim::runtime::RunStatus::stopped
      && observed_value == "1010");
  assert(lifecycle.size() == 4);
  const std::array expected_text {
      std::string_view { "module-initial" },
      std::string_view { "program-initial" },
      std::string_view { "module-final" },
      std::string_view { "program-final" }
  };
  for (std::size_t index = 0; index < lifecycle.size(); ++index) {
    const auto program = index == 1 || index == 3;
    assert(lifecycle[index].text == expected_text[index]);
    assert(
        lifecycle[index].process.starts_with(
            program ? "program_host.active_driver."
                    : "program_host."));
    assert(
        lifecycle[index].phase
        == (program ? fsim::runtime::SchedulerPhase::reactive
                    : fsim::runtime::SchedulerPhase::active));
  }
  assert(std::ranges::any_of(
      debug_points,
      [](const auto& point) {
        return point.kind
                   == fsim::runtime::simir::ExecutionPointKind::statement
            && point.source.path == "program-instances.sv"
            && point.source.line != 0
            && point.source.column != 0
            && point.scope.starts_with(
                "program_host.active_driver");
      }));

  const auto clocked = fsim::frontend::parse_text(
      "program-clocking.sv",
      R"(
program clocked_driver(
  input logic clk,
  input logic sampled_source,
  output logic driven
);
  timeunit 1ns / 1ns;
  clocking cb @(posedge clk);
    default input #1step output negedge #1ns;
    input sampled = sampled_source;
    output driven;
  endclocking
  default clocking cb;
  initial begin
    ##2;
    if (cb.sampled) cb.driven = 1'b1;
  end
endprogram

module clocked_host;
  logic clk;
  logic sampled_source;
  logic driven;
  initial begin
    sampled_source = 1'b1;
    #1 clk = 1'b1;
    #1 clk = 1'b0;
    #1 sampled_source = 1'b0;
    clk = 1'b1;
    #1 clk = 1'b0;
  end
  clocked_driver active_driver(
      clk, sampled_source, driven);
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(clocked.ok());
  const auto clocked_host = fsim::elaboration::elaborate(
      clocked.design, "sv:work.clocked_host");
  if (!clocked_host.ok()) {
    for (const auto& diagnostic : clocked_host.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(clocked_host.ok());
  const auto sampled = clocked_host.design->find_signal(
      "clocked_host.active_driver.cb.sampled");
  const auto driven = clocked_host.design->find_signal(
      "clocked_host.driven");
  const auto driven_member = clocked_host.design->find_signal(
      "clocked_host.active_driver.cb.driven");
  assert(sampled && driven && driven_member);
  assert(*driven != *driven_member);
  assert(std::ranges::any_of(
      clocked_host.design->processes(),
      [](const auto& process) {
        return !process.reactive
            && process.name.find("$clocking$cb$sample")
                != std::string::npos;
      }));
  auto clocked_runtime =
      clocked_host.design->create_interpreter();
  const auto clocked_run = clocked_runtime->run();
  assert(clocked_run.status
         == fsim::runtime::RunStatus::completed);
  assert(
      clocked_runtime->signal_value(*sampled).to_msb_string()
      == "1");
  assert(
      clocked_runtime->signal_value(*driven).to_msb_string()
      == "1");
  assert(
      clocked_runtime->signal_value(*driven_member).to_msb_string()
      == "1");
  assert(clocked_runtime->scheduler().now() == 5);

  const auto invalid_clocking = fsim::frontend::parse_text(
      "program-clocking-invalid.sv",
      R"(
program invalid_driver(
  input logic clk,
  input logic left,
  input logic right
);
  clocking cb @(posedge clk);
    input mixed = left & right;
  endclocking
  initial ##1;
endprogram

module invalid_host;
  logic clk;
  logic left;
  logic right;
  invalid_driver active_driver(clk, left, right);
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(invalid_clocking.ok());
  const auto invalid_result = fsim::elaboration::elaborate(
      invalid_clocking.design, "sv:work.invalid_host");
  assert(!invalid_result.ok());
  assert(std::ranges::any_of(
      invalid_result.diagnostics,
      [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-ELAB-CLOCK-003";
      }));
  assert(std::ranges::any_of(
      invalid_result.diagnostics,
      [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-ELAB-CLOCK-005";
      }));

  const auto qualified_top = fsim::elaboration::elaborate(
      parsed.design, "sv:work.standalone_program");
  assert(qualified_top.ok());
  assert(
      qualified_top.design->specializations().size() == 1
      && qualified_top.design->specializations().front().unit
          == "sv:work.program(standalone_program)");
  assert(qualified_top.design->find_signal(
      "standalone_program.retained"));

  const auto simple_top = fsim::elaboration::elaborate(
      parsed.design, "standalone_program");
  assert(simple_top.ok());
  assert(
      simple_top.design->specializations().size() == 1
      && simple_top.design->specializations().front().unit
          == "sv:work.program(standalone_program)");

  const auto wrong_language_top = fsim::elaboration::elaborate(
      parsed.design, "verilog:work.standalone_program");
  assert(!wrong_language_top.ok());
}

}  // namespace fsim::tests::elaboration
