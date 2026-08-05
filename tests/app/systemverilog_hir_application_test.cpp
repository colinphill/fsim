// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/frontend/class_resolution.hpp"

#include "../../src/app/application_internal.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

namespace {

struct TemporaryDirectory {
  std::filesystem::path path;
  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

} // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-systemverilog-hir-" + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "semantic_hir.sv";
  {
    std::ofstream output{source, std::ios::binary};
    output << R"(`timescale 1ns/1ps
`default_nettype tri0
package values;
  typedef logic signed [3:0] key_t;
  typedef enum logic [1:0] {idle = 0, busy = 1} state_t;
  typedef struct packed { logic valid; logic [2:0] data; } packet_t;
  localparam int VALUE = 3;
endpackage

interface bus_if #(
    parameter type ITEM = logic [7:0],
    parameter int WIDTH = 4);
  ITEM data;
  logic valid;
  int dynamic_values[];
  byte bounded_values[$:3];
  logic [15:0] scores[values::key_t];
  logic [7:0] static_values[3:0];
  function automatic ITEM sample(input ITEM increment);
    return data + increment;
  endfunction
  task automatic drive(input ITEM value);
    data = value;
  endtask
  modport initiator(output data, valid, import function sample,
                    import task drive);
endinterface

module semantic_hir_top;
  import values::*;
  bus_if #(.ITEM(logic [3:0])) link();
  integer handle;
  int values[];
  logic [7:0] memory[3:0];
  generate
    if (1) begin : generated
      logic active;
    end
  endgenerate
  initial begin : executable
    int local_value = 1;
    handle = $fopen("trace.txt", "w");
    values.push_back(local_value);
    memory = '{default: 8'h11, 2: 8'h22};
    fork : workers
      #1 local_value += 1;
      #2 local_value++;
    join_any : workers
    wait fork;
    @(posedge link.valid) local_value = link.data[0];
    unique case (local_value)
      1: local_value = 2;
      default: local_value = 3;
    endcase
    assert (local_value != 0) else $error("zero");
    $fdisplay(handle, "value=%0d", local_value);
    $fflush(handle);
    $fclose(handle);
  end
endmodule
`resetall
)";
    assert(output.good());
  }

  fsim::project::Config config;
  config.base_directory = directory.path;
  config.project.name = "systemverilog-hir";
  config.project.top = "sv:work.semantic_hir_top";
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::system_verilog;
  sources.standard = "2017";
  sources.library = "work";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));

  fsim::diagnostic::Engine diagnostics;
  auto checked = fsim::app::check_project(config, diagnostics);
  assert(checked);
  const auto interface_unit = std::ranges::find_if(
      checked->systemverilog_hir.units(), [](const auto& unit) {
        return unit.kind == fsim::semantic::sv::UnitKind::interface
            && unit.name == "bus_if";
      });
  assert(interface_unit != checked->systemverilog_hir.units().end());
  assert(interface_unit->compilation.time_unit == "1ns");
  assert(interface_unit->compilation.time_precision == "1ps");
  assert(interface_unit->compilation.default_nettype == "tri0");
  assert(interface_unit->modports.size() == 1);
  assert(interface_unit->modports.front().members.size() == 4);

  const auto declaration_for = [&](const std::string_view declaration_name) {
    return std::ranges::find_if(
        checked->systemverilog_hir.declarations(),
        [&](const auto& declaration) {
          return declaration.scope == interface_unit->scope
              && declaration.name == declaration_name;
        });
  };
  const auto type_parameter = declaration_for("ITEM");
  const auto dynamic = declaration_for("dynamic_values");
  const auto bounded = declaration_for("bounded_values");
  const auto associative = declaration_for("scores");
  const auto static_values = declaration_for("static_values");
  assert(type_parameter
         != checked->systemverilog_hir.declarations().end());
  assert(dynamic != checked->systemverilog_hir.declarations().end());
  assert(bounded != checked->systemverilog_hir.declarations().end());
  assert(associative != checked->systemverilog_hir.declarations().end());
  assert(static_values
         != checked->systemverilog_hir.declarations().end());
  assert(type_parameter->form
         == fsim::semantic::sv::DeclarationForm::type_parameter);
  assert(type_parameter->declared_type && type_parameter->default_type);
  assert(dynamic->type->container_form
         == fsim::semantic::sv::TypeForm::dynamic_array);
  assert(bounded->type->container_form
         == fsim::semantic::sv::TypeForm::queue);
  assert(bounded->type->queue_maximum);
  assert(associative->type->container_form
         == fsim::semantic::sv::TypeForm::associative_array);
  assert(associative->type->associative_index);
  assert(associative->type->associative_index->spelling
         == "values::key_t");
  assert(static_values->type->container_form
         == fsim::semantic::sv::TypeForm::static_array);
  assert(static_values->type->unpacked_dimensions.size() == 1);

  const auto state_type = std::ranges::find_if(
      checked->systemverilog_hir.types(), [](const auto& type) {
        return type.name == "state_t";
      });
  const auto packet_type = std::ranges::find_if(
      checked->systemverilog_hir.types(), [](const auto& type) {
        return type.name == "packet_t";
      });
  assert(state_type != checked->systemverilog_hir.types().end());
  assert(packet_type != checked->systemverilog_hir.types().end());
  assert(state_type->form == fsim::semantic::sv::TypeForm::enumeration);
  assert(state_type->enumeration_literals.size() == 2);
  assert(packet_type->form
         == fsim::semantic::sv::TypeForm::packed_structure);
  assert(packet_type->members.size() == 2);

  const auto top = std::ranges::find_if(
      checked->systemverilog_hir.units(), [](const auto& unit) {
        return unit.name == "semantic_hir_top";
      });
  assert(top != checked->systemverilog_hir.units().end());
  assert(top->imports.size() == 1);
  assert(top->instances.size() == 1);
  assert(top->generates.size() == 1);
  assert(top->processes.size() == 1);
  assert(checked->systemverilog_hir.processes().size() == 1);
  const auto& process = checked->systemverilog_hir.processes().front();
  assert(process.kind == fsim::semantic::sv::ProcessKind::initial);
  assert(process.statements.size() == 1);
  const auto statement_for = [&](const fsim::semantic::StatementId id)
      -> const fsim::semantic::sv::Statement& {
    const auto found = std::ranges::find_if(
        checked->systemverilog_hir.statements(),
        [&](const auto& statement) { return statement.id == id; });
    assert(found != checked->systemverilog_hir.statements().end());
    return *found;
  };
  const auto& executable = statement_for(process.statements.front());
  assert(executable.kind == fsim::semantic::sv::StatementKind::block);
  assert(executable.nested_scope);
  assert(executable.declarations.size() == 1);
  const auto has_statement_kind = [&](const auto kind) {
    return std::ranges::any_of(
        executable.statements, [&](const auto id) {
          return statement_for(id).kind == kind;
        });
  };
  assert(has_statement_kind(
      fsim::semantic::sv::StatementKind::container_method));
  assert(has_statement_kind(fsim::semantic::sv::StatementKind::fork));
  assert(has_statement_kind(
      fsim::semantic::sv::StatementKind::wait_fork));
  assert(has_statement_kind(
      fsim::semantic::sv::StatementKind::event_control));
  assert(has_statement_kind(
      fsim::semantic::sv::StatementKind::selection));
  assert(has_statement_kind(
      fsim::semantic::sv::StatementKind::assertion));
  assert(has_statement_kind(
      fsim::semantic::sv::StatementKind::file_display));
  assert(has_statement_kind(
      fsim::semantic::sv::StatementKind::file_flush));
  assert(has_statement_kind(
      fsim::semantic::sv::StatementKind::file_close));
  const auto fork_id = *std::ranges::find_if(
      executable.statements, [&](const auto id) {
        return statement_for(id).kind
            == fsim::semantic::sv::StatementKind::fork;
      });
  const auto& fork = statement_for(fork_id);
  assert(fork.fork_join == fsim::semantic::sv::ForkJoinKind::any);
  assert(fork.nested_scope && fork.statements.size() == 2);
  const auto selection_id = *std::ranges::find_if(
      executable.statements, [&](const auto id) {
        return statement_for(id).kind
            == fsim::semantic::sv::StatementKind::selection;
      });
  const auto& selection = statement_for(selection_id);
  assert(selection.case_qualifier
         == fsim::semantic::sv::CaseQualifier::unique);
  assert(selection.case_alternatives.size() == 2);
  assert(std::ranges::any_of(
      checked->systemverilog_hir.expressions(), [](const auto& expression) {
        return expression.kind
                == fsim::semantic::sv::ExpressionKind::assignment_pattern
            && expression.associations.size() == 2;
      }));
  assert(checked->semantics.expression_identities().size()
         == checked->systemverilog_hir.expressions().size());
  assert(checked->semantics.statement_identities().size()
         == checked->systemverilog_hir.statements().size());
  const auto sample = std::ranges::find_if(
      checked->systemverilog_hir.declarations(), [](const auto& declaration) {
        return declaration.form
                == fsim::semantic::sv::DeclarationForm::function
            && declaration.name == "sample";
      });
  const auto drive = std::ranges::find_if(
      checked->systemverilog_hir.declarations(), [](const auto& declaration) {
        return declaration.form == fsim::semantic::sv::DeclarationForm::task
            && declaration.name == "drive";
      });
  assert(sample != checked->systemverilog_hir.declarations().end());
  assert(drive != checked->systemverilog_hir.declarations().end());
  assert(!sample->statements.empty() && !drive->statements.empty());
  const auto retained_unit = interface_unit->id;
  const auto retained_type = state_type->id;
  const auto retained_process = process.id;
  const auto retained_expression_count =
      checked->systemverilog_hir.expressions().size();
  checked->parsed.units.clear();
  assert(interface_unit->id == retained_unit);
  assert(state_type->id == retained_type);
  assert(process.id == retained_process);
  assert(checked->systemverilog_hir.expressions().size()
         == retained_expression_count);

  auto class_parsed = fsim::frontend::parse_text(
      "class_hir.sv",
      R"(
class HirObject;
  int payload;
  static int count;
  function new();
  endfunction
  function int read();
    return payload;
  endfunction
endclass
module class_hir_top;
  HirObject handle;
  HirObject other;
  int result;
  initial begin
    handle = new();
    other = null;
    other = handle;
    result = $cast(other, handle);
    handle.payload = 1;
    result = handle.read();
    result = HirObject::count;
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(class_parsed.ok());
  for (auto& unit : class_parsed.design.units) unit.library = "work";
  for (auto& declaration : class_parsed.design.systemverilog_classes) {
    declaration.library = "work";
  }
  std::vector<fsim::frontend::Diagnostic> class_resolution;
  assert(fsim::frontend::resolve_systemverilog_classes(
      class_parsed.design, class_resolution));
  auto class_model = fsim::app::application_detail::build_semantic_model(
      class_parsed.design,
      std::span<const fsim::app::CheckedSource>{},
      std::span<const fsim::app::CheckedSource>{},
      std::span<const fsim::app::CheckedSource>{});
  auto class_hir = fsim::app::application_detail::build_systemverilog_hir(
      class_parsed.design, class_model);
  const auto handle_declaration = std::ranges::find(
      class_hir.declarations(),
      std::string{"handle"},
      &fsim::semantic::sv::Declaration::name);
  assert(handle_declaration != class_hir.declarations().end());
  assert(handle_declaration->type);
  assert(handle_declaration->type->value_form
         == fsim::semantic::sv::TypeForm::class_handle);
  assert(handle_declaration->type->class_identity
         == "work::$unit::HirObject");
  const auto has_class_expression = [&](const auto kind) {
    return std::ranges::any_of(
        class_hir.expressions(), [&](const auto& expression) {
          return expression.kind == kind
              && !expression.class_identity.empty();
        });
  };
  assert(has_class_expression(
      fsim::semantic::sv::ExpressionKind::class_allocation));
  assert(has_class_expression(
      fsim::semantic::sv::ExpressionKind::class_null));
  assert(has_class_expression(
      fsim::semantic::sv::ExpressionKind::class_cast));
  assert(has_class_expression(
      fsim::semantic::sv::ExpressionKind::class_property));
  assert(has_class_expression(
      fsim::semantic::sv::ExpressionKind::class_method_call));
  assert(has_class_expression(
      fsim::semantic::sv::ExpressionKind::class_static_property));
  assert(std::ranges::count_if(
             class_hir.statements(), [](const auto& statement) {
               return statement.class_handle_transfer;
             }) >= 3);

  const auto design_source = directory.path / "design_ir.sv";
  {
    std::ofstream output{design_source, std::ios::binary};
    output << R"(
module design_ir_leaf(source, sink);
  input source;
  output sink;
  reg sink;
  always @(*) sink = source;
endmodule

module design_ir_top;
  reg source;
  wire sink;
  design_ir_leaf child(.source(source), .sink(sink));
  initial begin
    source = 1'b0;
    #1 source = 1'b1;
    #1 $finish;
  end
endmodule
)";
    assert(output.good());
  }
  fsim::project::Config design_config;
  design_config.base_directory = directory.path;
  design_config.project.name = "design-ir";
  design_config.project.top = "sv:work.design_ir_top";
  design_config.project.time_resolution = "1ns";
  fsim::project::SourceSet design_sources;
  design_sources.language = fsim::project::Language::verilog;
  design_sources.standard = "2005";
  design_sources.library = "work";
  design_sources.files.push_back(design_source);
  design_config.source_sets.push_back(std::move(design_sources));
  fsim::diagnostic::Engine build_diagnostics;
  auto built = fsim::app::build_project(
      design_config, build_diagnostics);
  if (!built) {
    fsim::diagnostic::print_text(std::cerr, build_diagnostics);
  }
  assert(built);
  assert(built->design_ir.valid());
  assert(built->design_ir.valid(built->semantics));
  assert(built->design_ir.top() == "design_ir_top");
  assert(built->design_ir.specializations().size() >= 2);
  assert(built->design_ir.instances().size()
         == built->design_ir.specializations().size());
  assert(std::ranges::all_of(
      built->design_ir.specializations(), [](const auto& specialization) {
        return specialization.unit.valid()
            && specialization.scope.valid()
            && specialization.instance.valid();
      }));
  const auto child = std::ranges::find_if(
      built->design_ir.instances(), [](const auto& instance) {
        return instance.path == "design_ir_top.child";
      });
  assert(child != built->design_ir.instances().end());
  assert(child->parent);
  assert(!built->design_ir.objects().empty());
  assert(!built->design_ir.ports().empty());
  assert(built->design_ir.processes().size()
         == built->design.processes().size());
  assert(!built->design_ir.processes().empty());
  assert(!built->design_ir.sensitivities().empty());
  assert(!built->design_ir.drivers().empty());
  assert(built->design_ir.transactions().size()
         == built->design_ir.drivers().size());
  assert(std::ranges::all_of(
      built->design_ir.objects(), [](const auto& object) {
        return object.specialization.valid() && !object.path.empty();
      }));
  fsim::app::Simulation simulation{
      std::move(*built), design_config.run.max_deltas,
      fsim::app::SimulationEngine::interpreter};
  const auto sink = simulation.find_signal("design_ir_top.sink");
  assert(sink);
  const auto run = simulation.run();
  assert(run.status == fsim::runtime::RunStatus::stopped);
  assert(run.time == 2);
  assert(simulation.read_signal(*sink).to_msb_string() == "1");
}
