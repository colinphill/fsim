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
  typedef struct packed {
    logic [3:0] prefix = 4'ha;
    packet_t packet = '{valid: 1'b1, data: 3'h5};
  } initialized_t;
  typedef union packed {
    logic [15:0] wide;
    logic [7:0] narrow;
  } unequal_t;
  typedef union tagged packed {
    logic [15:0] wide;
    logic [7:0] narrow;
  } tagged_t;
  localparam int VALUE = 3;
endpackage

interface bus_if #(
    parameter type ITEM = logic [7:0],
    parameter int WIDTH = 4);
  ITEM data;
  logic valid;
  logic clock;
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
  clocking cb @(posedge clock);
    input #0 data;
  endclocking
  modport initiator(output data, valid, import function sample,
                    import task drive, clocking cb);
endinterface

program semantic_hir_program(input logic clock);
  integer observed;
  initial observed = clock;
  final observed = 0;
endprogram : semantic_hir_program

module semantic_hir_top;
  import values::*;
  bus_if #(.ITEM(logic [3:0])) link();
  integer handle;
  int values[];
  logic [7:0] memory[3:0];
  logic ready_signal;
  property ready;
    ready_signal;
  endproperty
  ready_check: assert property (ready)
    $display("ready"); else $error("not ready");
  assume property (ready) else $warning("assumption");
  cover property (ready) $display("covered");
  restrict property (ready);
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
  if (!checked) {
    fsim::diagnostic::print_text(std::cerr, diagnostics);
  }
  assert(checked);
  const auto semantic_program = std::ranges::find_if(
      checked->semantics.units(), [](const auto& unit) {
        return unit.kind
                == fsim::semantic::UnitKind::systemverilog_program
            && unit.name == "semantic_hir_program";
      });
  assert(semantic_program != checked->semantics.units().end());
  const auto program_unit = std::ranges::find_if(
      checked->systemverilog_hir.units(), [](const auto& unit) {
        return unit.kind == fsim::semantic::sv::UnitKind::program
            && unit.name == "semantic_hir_program";
      });
  assert(program_unit != checked->systemverilog_hir.units().end());
  assert(program_unit->processes.size() == 2);
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
  assert(interface_unit->modports.front().members.size() == 5);
  assert(interface_unit->modports.front().members.back().kind
         == fsim::semantic::sv::ModportMemberKind::clocking);

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
  const auto unequal_type = std::ranges::find_if(
      checked->systemverilog_hir.types(), [](const auto& type) {
        return type.name == "unequal_t";
      });
  const auto tagged_type = std::ranges::find_if(
      checked->systemverilog_hir.types(), [](const auto& type) {
        return type.name == "tagged_t";
      });
  const auto initialized_type = std::ranges::find_if(
      checked->systemverilog_hir.types(), [](const auto& type) {
        return type.name == "initialized_t";
      });
  assert(state_type != checked->systemverilog_hir.types().end());
  assert(packet_type != checked->systemverilog_hir.types().end());
  assert(unequal_type != checked->systemverilog_hir.types().end());
  assert(tagged_type != checked->systemverilog_hir.types().end());
  assert(initialized_type != checked->systemverilog_hir.types().end());
  assert(state_type->form == fsim::semantic::sv::TypeForm::enumeration);
  assert(state_type->enumeration_literals.size() == 2);
  assert(packet_type->form
         == fsim::semantic::sv::TypeForm::packed_structure);
  assert(packet_type->members.size() == 2);
  assert(unequal_type->form
         == fsim::semantic::sv::TypeForm::packed_union);
  assert(unequal_type->members.size() == 2);
  assert(tagged_type->form
         == fsim::semantic::sv::TypeForm::tagged_union);
  assert(tagged_type->members.size() == 2);
  assert(initialized_type->members.size() == 2);
  assert(initialized_type->members[0].initializer);
  assert(initialized_type->members[1].initializer);

  const auto top = std::ranges::find_if(
      checked->systemverilog_hir.units(), [](const auto& unit) {
        return unit.name == "semantic_hir_top";
      });
  assert(top != checked->systemverilog_hir.units().end());
  assert(top->imports.size() == 1);
  assert(top->instances.size() == 1);
  assert(top->generates.size() == 1);
  assert(top->concurrent_assertions.size() == 4);
  assert(top->processes.size()
         == 1U + top->concurrent_assertions.size());
  const auto& ready_assertion = top->concurrent_assertions[0];
  const auto& assumption = top->concurrent_assertions[1];
  const auto& cover = top->concurrent_assertions[2];
  const auto& restriction = top->concurrent_assertions[3];
  assert(ready_assertion.kind
         == fsim::semantic::sv::ConcurrentAssertionKind::assertion);
  assert(ready_assertion.name == "ready_check");
  assert(ready_assertion.explicit_label);
  assert(ready_assertion.coverage_slot == 0);
  assert(ready_assertion.has_pass_action);
  assert(ready_assertion.has_failure_action);
  assert(std::ranges::find(
             ready_assertion.pass_action_tokens, "$display")
         != ready_assertion.pass_action_tokens.end());
  assert(std::ranges::find(
             ready_assertion.failure_action_tokens, "$error")
         != ready_assertion.failure_action_tokens.end());
  assert(ready_assertion.sampling_region
         == fsim::semantic::sv::AssertionRegion::preponed);
  assert(ready_assertion.evaluation_region
         == fsim::semantic::sv::AssertionRegion::observed);
  assert(ready_assertion.action_region
         == fsim::semantic::sv::AssertionRegion::reactive);
  assert(ready_assertion.observers.callback_on_failure);
  assert(ready_assertion.observers.debugger_visible);
  assert(ready_assertion.observers.trace_visible);
  assert(ready_assertion.observers.coverage_enabled);
  assert(ready_assertion.label_source);
  assert(ready_assertion.pass_action_source);
  assert(ready_assertion.failure_action_source);
  assert(ready_assertion.source.valid());
  assert(ready_assertion.origin.valid());
  assert(assumption.kind
         == fsim::semantic::sv::ConcurrentAssertionKind::assumption);
  assert(assumption.name == "$assertion$2");
  assert(!assumption.explicit_label);
  assert(!assumption.has_pass_action);
  assert(assumption.has_failure_action);
  assert(assumption.coverage_slot == 1);
  assert(cover.kind
         == fsim::semantic::sv::ConcurrentAssertionKind::cover);
  assert(!cover.observers.callback_on_failure);
  assert(cover.coverage_slot == 2);
  assert(restriction.kind
         == fsim::semantic::sv::ConcurrentAssertionKind::restriction);
  assert(!restriction.observers.callback_on_failure);
  assert(restriction.coverage_slot == 3);
  assert(checked->systemverilog_hir.processes().size()
         == 3U + top->concurrent_assertions.size());
  const auto& process = checked->systemverilog_hir.processes()[
      top->processes.front().value()];
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
class HirBase;
  rand int base_value;
  constraint nonnegative {
    base_value >= 0;
  }
endclass
class HirObject #(parameter int MAX = 3) extends HirBase;
  int payload;
  static int count;
  local static const int limit = 3;
  randc logic [1:0] choice;
  rand int samples[0:2];
  constraint nonnegative {
    base_value >= 1;
  }
  constraint valid_choice {
    this.choice inside {[0:2], 3};
    payload + super.base_value <= MAX;
    payload <= limit;
    payload <= limit_value();
  }
  constraint weighted_choice {
    soft choice inside {0, 1};
    choice dist {0 := 1, [1:3] :/ 6};
  }
  constraint structured_choice {
    (choice == 0) -> { payload == 0; }
    if (choice == 1) { payload == 1; }
    else { payload >= 0; }
    foreach (samples[i]) { samples[i] >= 0; }
    solve payload before choice;
  }
  pure constraint inherited_contract;
  function new();
  endfunction
  function int read();
    return payload;
  endfunction
  function int limit_value();
    return limit;
  endfunction
endclass
class IllegalOverride extends HirBase;
  static constraint nonnegative {
    base_value >= 2;
  }
endclass
class HirHolder;
  HirObject #(.MAX(7)) wide;
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
  auto class_specializations =
      fsim::frontend::specialize_systemverilog_classes(
          class_parsed.design);
  assert(class_specializations.ok());
  auto class_hir = fsim::app::application_detail::build_systemverilog_hir(
      class_parsed.design,
      class_model,
      class_specializations.specializations);
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
  const auto hir_base = std::ranges::find(
      class_hir.classes(),
      std::string{"work::$unit::HirBase"},
      &fsim::semantic::sv::ClassDeclaration::canonical_identity);
  const auto hir_object = std::ranges::find(
      class_hir.classes(),
      std::string{"work::$unit::HirObject"},
      &fsim::semantic::sv::ClassDeclaration::canonical_identity);
  const auto illegal_override = std::ranges::find(
      class_hir.classes(),
      std::string{"work::$unit::IllegalOverride"},
      &fsim::semantic::sv::ClassDeclaration::canonical_identity);
  assert(hir_base != class_hir.classes().end());
  assert(hir_object != class_hir.classes().end());
  assert(illegal_override != class_hir.classes().end());
  assert(hir_object->base_declaration_identity
         == "work::$unit::HirBase");
  assert(hir_base->properties.size() == 1);
  assert(hir_base->properties.front().owner_identity
         == "work::$unit::HirBase");
  assert(hir_base->properties.front().random_kind
         == fsim::semantic::sv::ClassRandomKind::rand);
  assert(hir_object->properties.size() == 5);
  assert(hir_object->properties[2].visibility
         == fsim::semantic::sv::ClassVisibility::local_access);
  assert(hir_object->properties[2].static_storage
         && hir_object->properties[2].constant);
  assert(hir_object->properties[3].random_kind
         == fsim::semantic::sv::ClassRandomKind::randc);
  assert(hir_object->constraints.size() == 5);
  const auto valid_choice_position = std::ranges::find(
      hir_object->constraints,
      std::string{"valid_choice"},
      &fsim::semantic::sv::ClassConstraint::name);
  assert(valid_choice_position != hir_object->constraints.end());
  const auto& valid_choice = *valid_choice_position;
  const auto weighted_choice_position = std::ranges::find(
      hir_object->constraints,
      std::string{"weighted_choice"},
      &fsim::semantic::sv::ClassConstraint::name);
  assert(weighted_choice_position != hir_object->constraints.end());
  const auto& weighted_choice = *weighted_choice_position;
  assert(weighted_choice.expressions.size() == 2);
  assert(weighted_choice.expressions[0].kind
         == fsim::semantic::sv::ConstraintExpressionKind::soft);
  assert(weighted_choice.expressions[0].operands.front().kind
         == fsim::semantic::sv::ConstraintExpressionKind::inside_set);
  assert(weighted_choice.expressions[1].kind
         == fsim::semantic::sv::ConstraintExpressionKind::distribution);
  assert(weighted_choice.expressions[1].operands.size() == 3);
  assert(weighted_choice.expressions[1].operands[2].kind
         == fsim::semantic::sv::ConstraintExpressionKind::distribution_item);
  assert(weighted_choice.expressions[1].operands[2].text == "@dist-:/");
  const auto structured_choice_position = std::ranges::find(
      hir_object->constraints,
      std::string{"structured_choice"},
      &fsim::semantic::sv::ClassConstraint::name);
  assert(structured_choice_position != hir_object->constraints.end());
  const auto& structured_choice = *structured_choice_position;
  assert(structured_choice.expressions.size() == 4);
  assert(structured_choice.expressions[0].kind
         == fsim::semantic::sv::ConstraintExpressionKind::implication);
  assert(structured_choice.expressions[1].kind
         == fsim::semantic::sv::ConstraintExpressionKind::conditional_constraint);
  assert(structured_choice.expressions[1].operands.size() == 3);
  assert(structured_choice.expressions[2].kind
         == fsim::semantic::sv::ConstraintExpressionKind::foreach_constraint);
  assert(structured_choice.expressions[2].operands[0].operands[1]
             .bindings.front().kind
         == fsim::semantic::sv::ConstraintReferenceKind::local_variable);
  assert(structured_choice.expressions[3].kind
         == fsim::semantic::sv::ConstraintExpressionKind::solve_before);
  assert(valid_choice.canonical_identity
         == "work::$unit::HirObject::valid_choice");
  assert(valid_choice.owner_identity == "work::$unit::HirObject");
  assert(valid_choice.expressions.size() == 4);
  assert(valid_choice.expressions.front().kind
         == fsim::semantic::sv::ConstraintExpressionKind::inside_set);
  assert(valid_choice.expressions.front().operands.size() == 3);
  assert(valid_choice.expressions.front().operands.front().bindings.size()
         == 2);
  assert(std::ranges::all_of(
      valid_choice.expressions.front().operands.front().bindings,
      [](const auto& binding) {
        return binding.canonical_identity
            == "work::$unit::HirObject::choice";
      }));
  assert(valid_choice.expressions.front().operands[1].kind
         == fsim::semantic::sv::ConstraintExpressionKind::inside_range);
  assert(valid_choice.expressions[1].kind
         == fsim::semantic::sv::ConstraintExpressionKind::binary);
  assert(valid_choice.expressions[1].operands.front().kind
         == fsim::semantic::sv::ConstraintExpressionKind::binary);
  assert(valid_choice.expressions[1].operands.front().operands[1]
             .bindings.front().canonical_identity
         == "work::$unit::HirBase::base_value");
  assert(valid_choice.expressions[1].operands[1].bindings.front().kind
         == fsim::semantic::sv::ConstraintReferenceKind::parameter);
  assert(std::ranges::any_of(
      valid_choice.expressions[1].operands[1].bindings,
      [](const auto& binding) { return binding.constant_value == "3"; }));
  assert(std::ranges::any_of(
      valid_choice.expressions[1].operands[1].bindings,
      [](const auto& binding) { return binding.constant_value == "7"; }));
  const auto& arithmetic_constraint = valid_choice.expressions[1];
  const auto& payload_reference =
      arithmetic_constraint.operands[0].operands[0];
  const auto& base_reference =
      arithmetic_constraint.operands[0].operands[1];
  const auto parameter_binding = std::ranges::find(
      arithmetic_constraint.operands[1].bindings,
      std::string{"3"},
      &fsim::semantic::sv::ConstraintBinding::constant_value);
  assert(parameter_binding
         != arithmetic_constraint.operands[1].bindings.end());
  const auto payload_binding = std::ranges::find(
      payload_reference.bindings,
      parameter_binding->specialization_identity,
      &fsim::semantic::sv::ConstraintBinding::specialization_identity);
  const auto base_binding = std::ranges::find(
      base_reference.bindings,
      parameter_binding->specialization_identity,
      &fsim::semantic::sv::ConstraintBinding::specialization_identity);
  assert(payload_binding != payload_reference.bindings.end());
  assert(base_binding != base_reference.bindings.end());
  assert(payload_binding->type.executable_width == 32
         && payload_binding->type.four_state
         && base_binding->type.executable_width == 32
         && base_binding->type.signed_value);
  fsim::runtime::SystemVerilogConstraintSolver source_solver;
  const auto add_source_variable = [&](
      const auto& binding,
      const std::initializer_list<std::uint64_t> domain) {
    fsim::runtime::SystemVerilogConstraintVariable variable;
    variable.canonical_identity = binding.canonical_identity;
    variable.profile = fsim::app::application_detail::
        systemverilog_constraint_profile(binding);
    for (const auto value : domain) {
      variable.domain.push_back(
          fsim::runtime::PackedLogic4::from_aval_bval(
              variable.profile.width, value, 0));
    }
    return source_solver.add_variable(std::move(variable));
  };
  const auto payload_variable = add_source_variable(
      *payload_binding, {4, 1});
  const auto base_variable = add_source_variable(*base_binding, {0, 1});
  const std::map<std::string,
                 fsim::runtime::SystemVerilogConstraintVariableId>
      source_variables{
          {payload_binding->canonical_identity, payload_variable},
          {base_binding->canonical_identity, base_variable}};
  std::vector<fsim::runtime::SystemVerilogConstraintVariableId>
      source_dependencies;
  std::string lowering_error;
  auto lowered_constraint = fsim::app::application_detail::
      lower_systemverilog_constraint_expression(
          arithmetic_constraint,
          parameter_binding->specialization_identity,
          source_variables,
          source_dependencies,
          lowering_error);
  assert(lowered_constraint && lowering_error.empty());
  source_solver.add_clause(
      fsim::runtime::systemverilog_constraint_expression_clause(
          valid_choice.canonical_identity + "::arithmetic",
          source_dependencies,
          std::move(*lowered_constraint)));
  const auto source_solution = source_solver.solve();
  assert(source_solution.status
         == fsim::runtime::SystemVerilogConstraintSolveStatus::Satisfied);
  assert(source_solution.values[payload_variable].low_word().aval == 1
         && source_solution.values[base_variable].low_word().aval == 0);
  const auto& weighted_subject = weighted_choice.expressions[1].operands[0];
  const auto weighted_binding = std::ranges::find(
      weighted_subject.bindings,
      parameter_binding->specialization_identity,
      &fsim::semantic::sv::ConstraintBinding::specialization_identity);
  assert(weighted_binding != weighted_subject.bindings.end());
  fsim::runtime::SystemVerilogConstraintSolver weighted_solver;
  fsim::runtime::SystemVerilogConstraintVariable weighted_variable;
  weighted_variable.canonical_identity = weighted_binding->canonical_identity;
  weighted_variable.profile = fsim::app::application_detail::
      systemverilog_constraint_profile(*weighted_binding);
  for (std::uint64_t value = 0; value != 4; ++value) {
    weighted_variable.domain.push_back(
        fsim::runtime::PackedLogic4::from_aval_bval(
            weighted_variable.profile.width, value, 0));
  }
  const auto weighted_variable_id = weighted_solver.add_variable(
      std::move(weighted_variable));
  const std::map<std::string,
                 fsim::runtime::SystemVerilogConstraintVariableId>
      weighted_variables{
          {weighted_binding->canonical_identity, weighted_variable_id}};
  std::vector<fsim::runtime::SystemVerilogConstraintVariableId>
      weighted_dependencies;
  auto lowered_soft = fsim::app::application_detail::
      lower_systemverilog_constraint_expression(
          weighted_choice.expressions[0],
          parameter_binding->specialization_identity,
          weighted_variables,
          weighted_dependencies,
          lowering_error);
  assert(lowered_soft && lowering_error.empty());
  auto soft_clause =
      fsim::runtime::systemverilog_constraint_expression_clause(
          weighted_choice.canonical_identity + "::soft-inside",
          weighted_dependencies,
          std::move(*lowered_soft));
  soft_clause.soft = true;
  weighted_solver.add_clause(std::move(soft_clause));
  auto lowered_distribution = fsim::app::application_detail::
      lower_systemverilog_constraint_distribution(
          weighted_choice.expressions[1],
          parameter_binding->specialization_identity,
          weighted_variables,
          weighted_solver.variables(),
          weighted_choice.canonical_identity + "::dist",
          lowering_error);
  assert(lowered_distribution && lowering_error.empty());
  weighted_solver.add_distribution(std::move(*lowered_distribution));
  const auto weighted_solution = weighted_solver.solve(77);
  const auto weighted_solution_replay = weighted_solver.solve(77);
  assert(weighted_solution.status
         == fsim::runtime::SystemVerilogConstraintSolveStatus::Satisfied);
  assert(weighted_solution.values == weighted_solution_replay.values);
  assert(weighted_solution.values[weighted_variable_id].low_word().aval <= 1);
  const auto& foreach_selection =
      structured_choice.expressions[2].operands[0];
  const auto samples_binding = std::ranges::find(
      foreach_selection.operands[0].bindings,
      parameter_binding->specialization_identity,
      &fsim::semantic::sv::ConstraintBinding::specialization_identity);
  assert(samples_binding
         != foreach_selection.operands[0].bindings.end());
  fsim::runtime::SystemVerilogConstraintSolver structured_solver;
  const auto add_structured_variable = [&](
      const auto& binding, const std::uint64_t initial,
      std::string identity) {
    fsim::runtime::SystemVerilogConstraintVariable variable;
    variable.canonical_identity = std::move(identity);
    variable.profile = fsim::app::application_detail::
        systemverilog_constraint_profile(binding);
    variable.domain.push_back(
        fsim::runtime::PackedLogic4::from_aval_bval(
            variable.profile.width, initial, 0));
    return structured_solver.add_variable(std::move(variable));
  };
  const auto structured_choice_variable = add_structured_variable(
      *weighted_binding, 1, weighted_binding->canonical_identity);
  const auto structured_payload_variable = add_structured_variable(
      *payload_binding, 1, payload_binding->canonical_identity);
  std::vector<fsim::runtime::SystemVerilogConstraintVariableId>
      sample_variables;
  for (std::size_t index = 0; index < 3; ++index) {
    sample_variables.push_back(add_structured_variable(
        *samples_binding, 0,
        samples_binding->canonical_identity + "["
            + std::to_string(index) + "]"));
  }
  const std::map<std::string,
                 fsim::runtime::SystemVerilogConstraintVariableId>
      structured_variables{
          {weighted_binding->canonical_identity,
           structured_choice_variable},
          {payload_binding->canonical_identity,
           structured_payload_variable}};
  const std::map<std::string, std::vector<
      fsim::runtime::SystemVerilogConstraintVariableId>>
      structured_containers{
          {samples_binding->canonical_identity, sample_variables}};
  for (std::size_t index = 0; index < 3; ++index) {
    std::vector<fsim::runtime::SystemVerilogConstraintVariableId>
        dependencies;
    auto lowered = fsim::app::application_detail::
        lower_systemverilog_constraint_expression(
            structured_choice.expressions[index],
            parameter_binding->specialization_identity,
            structured_variables,
            dependencies,
            lowering_error,
            structured_containers);
    assert(lowered && lowering_error.empty());
    structured_solver.add_clause(
        fsim::runtime::systemverilog_constraint_expression_clause(
            structured_choice.canonical_identity + "::"
                + std::to_string(index),
            dependencies,
            std::move(*lowered)));
  }
  auto solve_order = fsim::app::application_detail::
      lower_systemverilog_solve_before(
          structured_choice.expressions[3],
          parameter_binding->specialization_identity,
          structured_variables,
          lowering_error);
  assert(solve_order && lowering_error.empty() && solve_order->size() == 1);
  for (const auto& [earlier, later] : *solve_order) {
    structured_solver.add_solve_before(earlier, later);
  }
  const auto structured_order = structured_solver.search_order();
  assert(std::ranges::find(structured_order, structured_payload_variable)
         < std::ranges::find(structured_order, structured_choice_variable));
  const auto structured_solution = structured_solver.solve();
  assert(structured_solution.status
         == fsim::runtime::SystemVerilogConstraintSolveStatus::Satisfied);
  assert(structured_solution.values[structured_payload_variable]
             .low_word().aval == 1);
  assert(valid_choice.expressions[2].operands[1]
             .bindings.front().canonical_identity
         == "work::$unit::HirObject::limit");
  assert(valid_choice.expressions.back().operands[1].kind
         == fsim::semantic::sv::ConstraintExpressionKind::call);
  assert(valid_choice.expressions.back().operands[1]
             .bindings.front().canonical_identity
         == "work::$unit::HirObject::limit_value");
  assert(valid_choice.expressions.front().source
         != valid_choice.source);
  assert(hir_object->constraints.back().pure
         && !hir_object->constraints.back().defined);
  assert(hir_object->composed_constraints.size() == 5);
  assert(hir_object->composed_constraints.front().name == "nonnegative");
  assert(hir_object->composed_constraints.front().selected_identity
         == "work::$unit::HirObject::nonnegative");
  assert(hir_object->composed_constraints.front().overrides);
  assert(hir_object->composed_constraints.front().override_legal);
  assert(hir_object->composed_constraints.front().overridden_identity
         == "work::$unit::HirBase::nonnegative");
  assert(hir_object->composed_constraints.front().mode_enabled);
  assert(!hir_object->composed_constraints.back().mode_enabled);
  assert(illegal_override->composed_constraints.size() == 1);
  assert(illegal_override->composed_constraints.front().overrides);
  assert(!illegal_override->composed_constraints.front().override_legal);

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
