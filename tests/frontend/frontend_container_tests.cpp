// SPDX-License-Identifier: Apache-2.0
#include "frontend_test_support.hpp"

#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::frontend {

namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error{std::string{message}};
  }
}

[[nodiscard]] bool has_code(
    const fsim::frontend::ParseResult& result,
    const std::string_view code) {
  return std::ranges::any_of(
      result.diagnostics,
      [code](const auto& diagnostic) {
        return diagnostic.code == code;
      });
}

}  // namespace

void test_systemverilog_containers() {
  using namespace fsim::frontend;
  const auto parsed = parse_text(
      "containers.sv",
      R"(
module containers;
  int values[];
  logic [7:0] pending[$];
  bit bounded[$:3];
  logic [15:0] scores[int];
  bit flags[logic signed [3:0]];
  logic [7:0] image[7:4];
  bit [3:0] ascending[-2:1];
  int located[$];
  int located_indices[$];

  task automatic mutate(
      input int source[],
      inout logic [7:0] target[$],
      inout logic [15:0] lookup[int]);
    int copy[];
    int key;
    copy = new[4];
    target.push_back(8'h2a);
    target.pop_front();
    lookup.delete(2);
    lookup.first(key);
  endtask

  initial begin
    $readmemh("image.hex", image);
    $readmemb("image.bin", image, 6, 4);
    image = '{8'h11, 8'h22, 8'h33, 8'h44};
    values = '{1, 2, 3};
    pending = '{8'haa, 8'hbb};
    lookup = '{-1: 16'h1234, 3: 16'h5678};
    image = '{default: 8'h55, 6: 8'h66};
    ascending = '{1: 4'hd, default: 4'ha, -2: 4'hc};
    image[6:5] = image[5:4];
    values = new[3];
    pending.push_front(8'h11);
    bounded.delete();
    if (pending.size() == 1)
      values[0] = pending.pop_back();
    assert ($left(image) == 7);
    assert ($right(image) == 4);
    assert ($low(ascending) == -2);
    assert ($high(ascending) == 1);
    assert ($increment(image) == 1);
    assert ($size(values, 1) == 3);
    assert ($bits(pending) == 8);
    assert ($dimensions(lookup) == 2);
    assert ($unpacked_dimensions(image) == 1);
    assert (values.sum() == 6);
    assert (values.product() == 6);
    assert (pending.and() == 0);
    assert (pending.or() == 8'hbb);
    assert (pending.xor() == 8'h11);
    assert (
        values.sum() with (
            item.index >= 0 ? item : 0) == 6);
    assert (
        values.xor(entry) with (
            entry.index >= 0 ? entry : 0) == 0);
    values.reverse();
    values.sort();
    values.rsort();
    values.sort() with (
        item.index >= 0 ? item : 0);
    pending.rsort(entry) with (entry);
    pending.reverse();
    pending.sort();
    bounded.rsort();
    located = values.min();
    located = values.max();
    located = values.unique();
    located_indices = values.unique_index();
    located =
        values.min() with (
            item.index < 0 ? 0 : item);
    located = values.max(entry) with (entry);
    located = values.unique() with (item);
    located_indices =
        values.unique_index(entry) with (
            entry.index >= 0 ? entry : 0);
    located =
        values.find(cell) with (cell > 0 && cell.index >= 0);
    located_indices = values.find_index() with (item != 2);
    located = values.find_first() with (item >= 1 && item < 3);
    located_indices =
        values.find_first_index() with (!(item == 0));
    located = values.find_last() with (item <= 3 || item == 7);
    located_indices =
        values.find_last_index() with (item > 1);
  end
endmodule

module static_port_child #(
    parameter int LEFT = 3,
    parameter int RIGHT = 0) (
    input logic signed [7:0] source[LEFT:RIGHT],
    output bit [3:0] result[-1:1],
    inout logic [15:0] shared[0:2]);
endmodule

module dynamic_port_child #(
    parameter int LIMIT = 3,
    parameter type KEY = logic signed [3:0]) (
    input int source[],
    output logic [7:0] pending[$],
    inout bit bounded[$:LIMIT],
    inout logic [15:0] scores[KEY]);
endmodule

module non_ansi_container_port(
    source, result, values, pending, scores);
  parameter int LEFT = 3;
  input logic [7:0] source[LEFT:0];
  output bit [3:0] result[-1:1];
  input int values[];
  output logic [7:0] pending[$];
  inout logic [15:0] scores[int];
endmodule
)",
      Language::SystemVerilog2017);
  require(parsed.ok(), "bounded container syntax parses");
  const auto* unit =
      parsed.design.find(UnitKind::VerilogModule, "containers");
  require(
      unit != nullptr && unit->variables.size() == 9,
      "module containers remain unpacked variable objects");
  require(
      unit->variables[0].type.systemverilog_container
          && unit->variables[0].type.systemverilog_container->kind
              == SystemVerilogContainerKind::DynamicArray,
      "empty unpacked dimension retains dynamic-array identity");
  require(
      unit->variables[1].type.systemverilog_container
          && unit->variables[1].type.systemverilog_container->kind
              == SystemVerilogContainerKind::Queue
          && !unit->variables[1]
                   .type.systemverilog_container->queue_maximum,
      "unbounded queue identity is distinct");
  require(
      unit->variables[2].type.systemverilog_container
          && unit->variables[2]
                 .type.systemverilog_container->queue_maximum
          && unit->variables[2]
                 .type.systemverilog_container->queue_maximum->text
              == "3",
      "bounded queue retains its maximum-index expression");
  const auto& scores =
      *unit->variables[3].type.systemverilog_container;
  require(
      scores.kind
              == SystemVerilogContainerKind::AssociativeArray
          && scores.associative_index_type
          && scores.associative_index_type->domain
              == ValueDomain::Integer
          && scores.associative_index_type->is_signed
          && scores.associative_index_type->width() == 32,
      "integral associative-array index metadata is retained");
  const auto& flags =
      *unit->variables[4].type.systemverilog_container;
  require(
      flags.kind
              == SystemVerilogContainerKind::AssociativeArray
          && flags.associative_index_type
          && flags.associative_index_type->domain
              == ValueDomain::Logic4
          && flags.associative_index_type->is_signed
          && flags.associative_index_type->width() == 4,
      "packed index width, state domain, and signedness are retained");
  const auto& image =
      *unit->variables[5].type.systemverilog_container;
  const auto& ascending =
      *unit->variables[6].type.systemverilog_container;
  require(
      image.kind == SystemVerilogContainerKind::StaticArray
          && image.static_range
          && image.static_range->left == 7
          && image.static_range->right == 4
          && image.static_range->descending
          && image.static_range_expressions.size() == 1,
      "descending static-array bounds retain concrete and expression "
      "metadata");
  require(
      ascending.kind
              == SystemVerilogContainerKind::StaticArray
          && ascending.static_range
          && ascending.static_range->left == -2
          && ascending.static_range->right == 1
          && !ascending.static_range->descending,
      "ascending signed static-array bounds retain exact direction");
  require(
      unit->processes.size() == 1
          && unit->processes[0].statements.size() >= 2
          && unit->processes[0].statements[0].kind
              == StatementKind::MemoryLoad
          && unit->processes[0].statements[0].memory_hex
          && unit->processes[0].statements[0].value.kind
              == ExpressionKind::StringLiteral
          && unit->processes[0].statements[0].target.text == "image"
          && unit->processes[0].statements[0].task_arguments.empty()
          && unit->processes[0].statements[1].kind
              == StatementKind::MemoryLoad
          && !unit->processes[0].statements[1].memory_hex
          && unit->processes[0].statements[1].value.kind
              == ExpressionKind::StringLiteral
          && unit->processes[0].statements[1].target.text == "image"
          && unit->processes[0].statements[1].task_arguments.size() == 2
          && unit->processes[0].statements[1].task_arguments[0].text == "6"
          && unit->processes[0].statements[1].task_arguments[1].text == "4",
      "$readmemh/$readmemb retain target, radix, and optional bounds");
  const auto& query_statements =
      unit->processes[0].statements;
  const auto slice_assignment =
      std::ranges::find_if(
          query_statements,
          [](const auto& statement) {
            return statement.target.kind
                    == ExpressionKind::Slice
                && statement.value.kind
                    == ExpressionKind::Slice;
          });
  require(
      slice_assignment != query_statements.end()
          && slice_assignment->target.text == ":"
          && slice_assignment->value.text == ":"
          && slice_assignment->target.operands.size() == 3
          && slice_assignment->value.operands.size() == 3
          && slice_assignment->target.operands[0].kind
              == ExpressionKind::Identifier
          && slice_assignment->target.operands[0].text == "image"
          && slice_assignment->target.operands[1].text == "6"
          && slice_assignment->target.operands[2].text == "5"
          && slice_assignment->value.operands[0].text == "image"
          && slice_assignment->value.operands[1].text == "5"
          && slice_assignment->value.operands[2].text == "4"
          && slice_assignment->target.span.source_name
              == "containers.sv"
          && slice_assignment->value.span.source_name
              == "containers.sv"
          && !slice_assignment->target.span.empty()
          && !slice_assignment->value.span.empty(),
      "static-array target and value slices remain distinct "
      "source-spanned colon HIR with explicit bounds");
  const auto has_query =
      [&](const std::string_view name) {
        return std::ranges::any_of(
            query_statements,
            [&](const auto& statement) {
              return statement.condition.kind
                          == ExpressionKind::Binary
                  && std::ranges::any_of(
                      statement.condition.operands,
                      [&](const auto& operand) {
                        return operand.kind
                                == ExpressionKind::Call
                            && operand.text == name;
                      });
            });
      };
  require(
      has_query("$left")
          && has_query("$right")
          && has_query("$low")
          && has_query("$high")
          && has_query("$increment")
          && has_query("$size")
          && has_query("$bits")
          && has_query("$dimensions")
          && has_query("$unpacked_dimensions"),
      "container query system functions remain explicit typed calls");
  require(
      has_query(".sum")
          && has_query(".product")
          && has_query(".and")
          && has_query(".or")
          && has_query(".xor"),
      "container reduction methods remain explicit no-argument calls");
  const auto find_call =
      [](const auto& self, const Expression& expression,
         const std::string_view name,
         const std::size_t operands) -> const Expression* {
        if (expression.kind == ExpressionKind::Call
            && expression.text == name
            && expression.operands.size() == operands) {
          return &expression;
        }
        for (const auto& operand : expression.operands) {
          if (const auto* found =
                  self(self, operand, name, operands)) {
            return found;
          }
        }
        return nullptr;
      };
  const Expression* transformed_reduction{};
  const Expression* named_transformed_reduction{};
  for (const auto& statement : query_statements) {
    transformed_reduction =
        find_call(
            find_call, statement.condition, ".sum", 2);
    if (transformed_reduction != nullptr) {
      break;
    }
  }
  for (const auto& statement : query_statements) {
    named_transformed_reduction =
        find_call(
            find_call, statement.condition, ".xor", 3);
    if (named_transformed_reduction != nullptr) {
      break;
    }
  }
  require(
      transformed_reduction != nullptr
          && transformed_reduction->operands[1].kind
              == ExpressionKind::Call
          && transformed_reduction->operands[1].text == "?:"
          && transformed_reduction->operands[1].span.source_name
              == "containers.sv"
          && !transformed_reduction->operands[1].span.empty(),
      "reduction with transformation remains explicit source-spanned "
      "HIR");
  require(
      named_transformed_reduction != nullptr
          && named_transformed_reduction->operands[1].kind
              == ExpressionKind::Identifier
          && named_transformed_reduction->operands[1].text
              == "entry"
          && named_transformed_reduction->operands[1].span.source_name
              == "containers.sv"
          && !named_transformed_reduction->operands[1].span.empty()
          && named_transformed_reduction->operands[2].kind
              == ExpressionKind::Call
          && named_transformed_reduction->operands[2].text
              == "?:",
      "named reduction iterator and transformation remain explicit "
      "source-spanned HIR");
  require(
      std::ranges::count_if(
          query_statements,
          [](const auto& statement) {
            return statement.value.kind
                == ExpressionKind::Aggregate;
          })
          == 6,
      "positional, associative, and static default/key assignment "
      "patterns remain aggregate HIR");
  require(
      std::ranges::count_if(
          query_statements,
          [](const auto& statement) {
            return statement.kind
                    == StatementKind::ContainerMethod
                && (statement.value.text == ".reverse"
                    || statement.value.text == ".sort"
                    || statement.value.text == ".rsort");
          })
          == 8,
      "container ordering methods remain explicit method-statement HIR");
  const auto implicit_ordering_key =
      std::ranges::find_if(
          query_statements,
          [](const auto& statement) {
            return statement.kind
                    == StatementKind::ContainerMethod
                && statement.value.text == ".sort"
                && statement.value.operands.size() == 2;
          });
  require(
      implicit_ordering_key != query_statements.end()
          && implicit_ordering_key->value.operands[1].kind
              == ExpressionKind::Call
          && implicit_ordering_key->value.operands[1].text == "?:"
          && implicit_ordering_key->value.operands[1].span.source_name
              == "containers.sv"
          && !implicit_ordering_key->value.operands[1].span.empty(),
      "implicit ordering with-key remains explicit source-spanned HIR");
  const auto named_ordering_key =
      std::ranges::find_if(
          query_statements,
          [](const auto& statement) {
            return statement.kind
                    == StatementKind::ContainerMethod
                && statement.value.text == ".rsort"
                && statement.value.operands.size() == 3;
          });
  require(
      named_ordering_key != query_statements.end()
          && named_ordering_key->value.operands[1].kind
              == ExpressionKind::Identifier
          && named_ordering_key->value.operands[1].text == "entry"
          && named_ordering_key->value.operands[2].kind
              == ExpressionKind::Identifier
          && named_ordering_key->value.operands[2].text == "entry"
          && !named_ordering_key->value.operands[1].span.empty()
          && !named_ordering_key->value.operands[2].span.empty(),
      "named ordering iterator and key remain explicit source-spanned HIR");
  require(
      std::ranges::count_if(
          query_statements,
          [](const auto& statement) {
            return statement.value.kind == ExpressionKind::Call
                && (statement.value.text == ".min"
                    || statement.value.text == ".max"
                    || statement.value.text == ".unique"
                    || statement.value.text == ".unique_index");
          })
          == 8,
      "container locator methods remain explicit call HIR");
  const auto implicit_locator_transformation =
      std::ranges::find_if(
          query_statements,
          [](const auto& statement) {
            return statement.value.kind == ExpressionKind::Call
                && statement.value.text == ".min"
                && statement.value.operands.size() == 2;
          });
  require(
      implicit_locator_transformation != query_statements.end()
          && implicit_locator_transformation
                 ->value.operands[1].kind
              == ExpressionKind::Call
          && implicit_locator_transformation
                 ->value.operands[1].text == "?:"
          && !implicit_locator_transformation
                  ->value.operands[1].span.empty(),
      "implicit locator transformation remains explicit source-spanned "
      "HIR");
  const auto named_locator_transformation =
      std::ranges::find_if(
          query_statements,
          [](const auto& statement) {
            return statement.value.kind == ExpressionKind::Call
                && statement.value.text == ".max"
                && statement.value.operands.size() == 3;
          });
  require(
      named_locator_transformation != query_statements.end()
          && named_locator_transformation->value.operands[1].kind
              == ExpressionKind::Identifier
          && named_locator_transformation->value.operands[1].text
              == "entry"
          && named_locator_transformation->value.operands[2].kind
              == ExpressionKind::Identifier
          && named_locator_transformation->value.operands[2].text
              == "entry",
      "named locator transformation iterator remains explicit HIR");
  require(
      std::ranges::count_if(
          query_statements,
          [](const auto& statement) {
            return statement.value.kind == ExpressionKind::Call
                && statement.value.text.starts_with(".find")
                && statement.value.operands.size() == 2;
          })
          == 5,
      "implicit predicate locator calls retain the receiver and one "
      "scoped with-clause expression");
  const auto named_locator =
      std::ranges::find_if(
          query_statements,
          [](const auto& statement) {
            return statement.value.kind == ExpressionKind::Call
                && statement.value.text == ".find"
                && statement.value.operands.size() == 3;
          });
  require(
      named_locator != query_statements.end()
          && named_locator->value.operands[1].kind
              == ExpressionKind::Identifier
          && named_locator->value.operands[1].text == "cell"
          && named_locator->value.operands[1].span.source_name
              == "containers.sv"
          && named_locator->value.operands[1].span.begin.line != 0
          && !named_locator->value.operands[1].span.empty(),
      "named predicate locator iterator remains explicit source-spanned "
      "identifier HIR");
  const auto keyed_pattern =
      std::ranges::find_if(
          query_statements,
          [](const auto& statement) {
            return statement.value.kind
                    == ExpressionKind::Aggregate
                && std::ranges::all_of(
                    statement.value.aggregate_choices,
                    [](const auto& choice) {
                      return choice == "@key";
                    });
          });
  require(
      keyed_pattern != query_statements.end()
          && keyed_pattern->value.operands.size() == 2
          && keyed_pattern->value.aggregate_choice_expressions.size()
              == 2,
      "associative assignment-pattern keys remain explicit HIR");
  const auto defaulted_pattern =
      std::ranges::find_if(
          query_statements,
          [](const auto& statement) {
            return statement.value.kind
                    == ExpressionKind::Aggregate
                && statement.value.aggregate_choices
                    == std::vector<std::string>{
                        "default", "@key"};
          });
  require(
      defaulted_pattern != query_statements.end()
          && defaulted_pattern->value.operands.size() == 2
          && defaulted_pattern->value
                 .aggregate_choice_expressions.size() == 2
          && defaulted_pattern->value
                 .aggregate_choice_expressions[0].size() == 1
          && defaulted_pattern->value
                 .aggregate_choice_expressions[0][0].kind
              == ExpressionKind::DefaultChoice
          && defaulted_pattern->value
                 .aggregate_choice_expressions[0][0].text
              == "default"
          && defaulted_pattern->value
                 .aggregate_choice_expressions[0][0].span.source_name
              == "containers.sv"
          && !defaulted_pattern->value
                  .aggregate_choice_expressions[0][0].span.empty()
          && defaulted_pattern->value
                 .aggregate_choice_expressions[1].size() == 1
          && defaulted_pattern->value
                 .aggregate_choice_expressions[1][0].kind
              == ExpressionKind::IntegerLiteral,
      "static default and index choices remain ordered, explicit, and "
      "source-spanned without treating default as an identifier");
  require(
      unit->tasks.size() == 1
          && unit->tasks[0].arguments.size() == 3
          && unit->tasks[0].arguments[0]
                 .type.systemverilog_container
          && unit->tasks[0].arguments[1]
                 .type.systemverilog_container
          && unit->tasks[0].arguments[2]
                 .type.systemverilog_container
          && unit->tasks[0].variables.size() == 2,
      "automatic task formals and locals retain container types");
  require(
      unit->tasks[0].statements.size() == 5
          && unit->tasks[0].statements[1].kind
              == StatementKind::ContainerMethod
          && unit->tasks[0].statements[2].kind
              == StatementKind::ContainerMethod
          && unit->tasks[0].statements[3].kind
              == StatementKind::ContainerMethod
          && unit->tasks[0].statements[4].kind
              == StatementKind::ContainerMethod,
      "mutating queue methods remain explicit statements");
  const auto* static_port_child =
      parsed.design.find(
          UnitKind::VerilogModule, "static_port_child");
  require(
      static_port_child != nullptr
          && static_port_child->ports.size() == 3
          && static_port_child->ports[0].is_port
          && static_port_child->ports[0].direction
              == PortDirection::Input
          && static_port_child->ports[0]
                 .type.systemverilog_container
          && static_port_child->ports[0]
                 .type.systemverilog_container->kind
              == SystemVerilogContainerKind::StaticArray
          && static_port_child->ports[0]
                 .type.systemverilog_container
                 ->static_range_expressions.size() == 1
          && static_port_child->ports[1].direction
              == PortDirection::Output
          && static_port_child->ports[2].direction
              == PortDirection::Inout,
      "ANSI static-array ports retain direction, element type, and "
      "specialization-aware bounds");
  const auto* dynamic_port_child =
      parsed.design.find(
          UnitKind::VerilogModule, "dynamic_port_child");
  require(
      dynamic_port_child != nullptr
          && dynamic_port_child->ports.size() == 4
          && dynamic_port_child->ports[0]
                 .type.systemverilog_container
          && dynamic_port_child->ports[0]
                 .type.systemverilog_container->kind
              == SystemVerilogContainerKind::DynamicArray
          && dynamic_port_child->ports[1]
                 .type.systemverilog_container,
      "ANSI dynamic arrays and queues remain typed container ports");
  require(
      dynamic_port_child->ports[1]
                 .type.systemverilog_container->kind
              == SystemVerilogContainerKind::Queue
          && dynamic_port_child->ports[2]
                 .type.systemverilog_container->queue_maximum
          && dynamic_port_child->ports[3]
                 .type.systemverilog_container->kind
              == SystemVerilogContainerKind::AssociativeArray
          && dynamic_port_child->ports[3]
                 .type.systemverilog_container
                 ->associative_index_type
          && dynamic_port_child->ports[3]
                 .type.systemverilog_container
                 ->associative_index_type->named_type
              == "KEY",
      "bounded queues and named associative index types retain "
      "specialization-aware metadata");
  const auto* non_ansi_container_port =
      parsed.design.find(
          UnitKind::VerilogModule, "non_ansi_container_port");
  require(
      non_ansi_container_port != nullptr
          && non_ansi_container_port->ports.size() == 5
          && non_ansi_container_port->variables.empty()
          && std::ranges::all_of(
              non_ansi_container_port->ports,
              [](const auto& port) {
                return port.type.systemverilog_container.has_value();
              }),
      "non-ANSI static and dynamic container declarations refine port "
      "placeholders without becoming module variables");

  const auto slice_consumers = parse_text(
      "container-slice-consumers.sv",
      R"(
module container_slice_consumers;
  logic [7:0] image[7:4];
  int result;
  int indices[$];
  initial begin
    result = $left(image[6:5]);
    result =
        image[6:5].sum(entry) with (
            entry.index == 5 ? entry : 8'h00);
    indices =
        image[6:5].find_index(entry) with (
            entry.index == 5);
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      slice_consumers.ok(),
      "direct static-array slice consumers parse");
  const auto* slice_unit =
      slice_consumers.design.find(
          UnitKind::VerilogModule,
          "container_slice_consumers");
  require(
      slice_unit != nullptr
          && slice_unit->processes.size() == 1
          && slice_unit->processes[0].statements.size() == 3,
      "static-array slice consumer statements remain explicit HIR");
  const auto& slice_statements =
      slice_unit->processes[0].statements;
  const auto has_direct_slice_receiver =
      [](const Expression& call) {
        return call.kind == ExpressionKind::Call
            && !call.operands.empty()
            && call.operands[0].kind
                == ExpressionKind::Slice
            && call.operands[0].text == ":"
            && call.operands[0].operands.size() == 3
            && call.operands[0].operands[0].kind
                == ExpressionKind::Identifier
            && call.operands[0].operands[0].text == "image"
            && call.operands[0].operands[1].text == "6"
            && call.operands[0].operands[2].text == "5"
            && call.operands[0].span.source_name
                == "container-slice-consumers.sv"
            && !call.operands[0].span.empty();
      };
  require(
      slice_statements[0].value.text == "$left"
          && slice_statements[0].value.operands.size() == 1
          && has_direct_slice_receiver(
              slice_statements[0].value),
      "a system query retains its direct source-spanned slice receiver");
  require(
      slice_statements[1].value.text == ".sum"
          && slice_statements[1].value.operands.size() == 3
          && has_direct_slice_receiver(
              slice_statements[1].value)
          && slice_statements[1].value.operands[1].kind
              == ExpressionKind::Identifier
          && slice_statements[1].value.operands[1].text
              == "entry"
          && slice_statements[1].value.operands[2].kind
              == ExpressionKind::Call
          && slice_statements[1].value.operands[2].text
              == "?:",
      "a transformed reduction retains its slice, iterator, and graph HIR");
  require(
      slice_statements[2].value.text == ".find_index"
          && slice_statements[2].value.operands.size() == 3
          && has_direct_slice_receiver(
              slice_statements[2].value)
          && slice_statements[2].value.operands[1].kind
              == ExpressionKind::Identifier
          && slice_statements[2].value.operands[1].text
              == "entry"
          && slice_statements[2].value.operands[2].kind
              == ExpressionKind::Binary,
      "a predicate locator retains its slice, iterator, and predicate HIR");

  const auto unsupported_slice_consumers = parse_text(
      "unsupported-slice-consumers.sv",
      "module unsupported_slice_consumers; "
      "int result; initial begin "
      "result = \"abcd\"[2:1].sum(); "
      "result = '{1, 2}[1:0].sum(); "
      "end endmodule",
      Language::SystemVerilog2017);
  require(
      !unsupported_slice_consumers.ok()
          && has_code(
              unsupported_slice_consumers,
              "FSIM-SV-PARSE-022")
          && has_code(
              unsupported_slice_consumers,
              "FSIM-SV-UNSUPPORTED-009"),
      "string and aggregate slice consumers diagnose before "
      "elaboration with stable recovery");

  const auto slice_calls = parse_text(
      "container-slice-calls.sv",
      R"(
module container_slice_calls;
  logic [7:0] values[3:0];
  logic [7:0] result[3:0];
  logic [7:0] scalar;
  function automatic logic [7:0] inspect(
      input logic [7:0] value[1:0]);
    return value.sum();
  endfunction
  task automatic transfer(
      input logic [7:0] source[1:0],
      output logic [7:0] destination[1:0],
      inout logic [7:0] working[1:0]);
    destination = source;
    working = source;
  endtask
  initial begin
    scalar = inspect(values[2:1]);
    transfer(
        values[3:2], result[1:0], values[1:0]);
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      slice_calls.ok(),
      "direct static-array slice callable actuals parse");
  const auto* slice_call_unit =
      slice_calls.design.find(
          UnitKind::VerilogModule,
          "container_slice_calls");
  require(
      slice_call_unit != nullptr
          && slice_call_unit->processes.size() == 1
          && slice_call_unit->processes[0].statements.size() == 2,
      "slice callable actual statements remain explicit HIR");
  const auto& function_call =
      slice_call_unit->processes[0].statements[0].value;
  require(
      function_call.kind == ExpressionKind::Call
          && function_call.text == "inspect"
          && function_call.operands.size() == 1
          && function_call.operands[0].kind
              == ExpressionKind::Slice
          && function_call.operands[0].text == ":"
          && function_call.operands[0].operands.size() == 3
          && function_call.operands[0].operands[0].text
              == "values"
          && function_call.operands[0].operands[1].text == "2"
          && function_call.operands[0].operands[2].text == "1"
          && function_call.operands[0].span.source_name
              == "container-slice-calls.sv"
          && !function_call.operands[0].span.empty(),
      "function slice actual retains direct source-spanned colon HIR");
  const auto& task_call =
      slice_call_unit->processes[0].statements[1];
  require(
      task_call.kind == StatementKind::TaskCall
          && task_call.task_name == "transfer"
          && task_call.task_arguments.size() == 3
          && std::ranges::all_of(
              task_call.task_arguments,
              [](const auto& actual) {
                return actual.kind == ExpressionKind::Slice
                    && actual.text == ":"
                    && actual.operands.size() == 3
                    && actual.operands[0].kind
                        == ExpressionKind::Identifier
                    && actual.span.source_name
                        == "container-slice-calls.sv"
                    && !actual.span.empty();
              }),
      "task input, output, and inout slice actuals remain distinct "
      "source-spanned colon HIR");

  const auto invalid = parse_text(
      "container-invalid.sv",
      R"(
module container_invalid;
  int fixed[3];
  int nested[][];
  string strings[];
  int wildcard[*];
  int string_key[string];
  int queue[$];
  task static bad_lifetime(ref byte values[int]);
  endtask
  initial begin
    queue.push_back();
    queue.delete(1, 2);
    queue.sum(1);
    queue.sort(1);
    queue.min(1);
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !invalid.ok()
          && has_code(invalid, "FSIM-SV-SEM-078")
          && has_code(invalid, "FSIM-SV-SEM-079")
          && has_code(invalid, "FSIM-SV-SEM-080")
          && has_code(invalid, "FSIM-SV-SEM-082")
          && has_code(invalid, "FSIM-SV-SEM-081"),
      "unsupported dimensions, elements, and method arities diagnose");

  const auto reduction_with = parse_text(
      "container-reduction-with.sv",
      "module m; int values[]; int result; "
      "initial result = values.sum() with (item); endmodule",
      Language::SystemVerilog2017);
  require(
      reduction_with.ok(),
      "container reduction with-clauses parse");
  const auto empty_reduction_with = parse_text(
      "container-reduction-empty-with.sv",
      "module m; int values[]; int result; "
      "initial result = values.sum() with (); endmodule",
      Language::SystemVerilog2017);
  require(
      !empty_reduction_with.ok()
          && has_code(
              empty_reduction_with,
              "FSIM-SV-SEM-091"),
      "empty container reduction with-clauses diagnose");
  const auto named_reduction_iterator = parse_text(
      "container-reduction-named-iterator.sv",
      "module m; int values[]; int result; "
      "initial begin "
      "result = values.sum(entry) with (entry); "
      "result = values.product(cell) with "
      "(cell.index > 0 ? cell : 1); "
      "end endmodule",
      Language::SystemVerilog2017);
  require(
      named_reduction_iterator.ok(),
      "named reduction transformation iterators parse");
  const auto invalid_reduction_iterator = parse_text(
      "container-reduction-invalid-iterator.sv",
      "module m; int values[]; int result; "
      "initial begin "
      "result = values.sum(entry); "
      "result = values.product(first, second) with (first); "
      "result = values.xor(1) with (item); "
      "result = entry; "
      "end endmodule",
      Language::SystemVerilog2017);
  require(
      !invalid_reduction_iterator.ok()
          && has_code(
              invalid_reduction_iterator,
              "FSIM-SV-SEM-094")
          && has_code(
              invalid_reduction_iterator,
              "FSIM-SV-SEM-090"),
      "named reduction transformation iterators diagnose malformed, "
      "multiple, missing-clause, and leaked bindings");
  const auto leaked_reduction_iterator = parse_text(
      "container-reduction-item-leak.sv",
      "module m; int values[]; int result; "
      "initial begin result = values.sum() with (item); "
      "result = item; end endmodule",
      Language::SystemVerilog2017);
  require(
      !leaked_reduction_iterator.ok()
          && has_code(
              leaked_reduction_iterator,
              "FSIM-SV-SEM-090"),
      "implicit reduction iterator cannot leak outside its with-clause");

  const auto ordering_with = parse_text(
      "container-ordering-with.sv",
      "module m; int values[]; initial begin "
      "values.sort() with (item); "
      "values.rsort(entry) with (entry.index < 2 ? entry : 0); "
      "end endmodule",
      Language::SystemVerilog2017);
  require(
      ordering_with.ok(),
      "container ordering with-clauses parse with implicit and named "
      "iterators");
  const auto invalid_ordering_with = parse_text(
      "container-ordering-invalid-with.sv",
      "module m; int values[]; int result; initial begin "
      "values.sort() with (); "
      "values.rsort(entry); "
      "values.sort(first, second) with (first); "
      "values.sort() with item; "
      "values.rsort() with (item; "
      "values.reverse() with (item); "
      "result = item; "
      "end endmodule",
      Language::SystemVerilog2017);
  require(
      !invalid_ordering_with.ok()
          && has_code(
              invalid_ordering_with,
              "FSIM-SV-SEM-092")
          && has_code(
              invalid_ordering_with,
              "FSIM-SV-PARSE-169")
          && has_code(
              invalid_ordering_with,
              "FSIM-SV-PARSE-170")
          && has_code(
              invalid_ordering_with,
              "FSIM-SV-UNSUPPORTED-041")
          && has_code(
              invalid_ordering_with,
              "FSIM-SV-SEM-090"),
      "container ordering with-clauses diagnose empty, malformed, invalid, "
      "leaked, and excluded forms");
  const auto locator_with = parse_text(
      "container-locator-with.sv",
      "module m; int values[]; int result[$]; initial begin "
      "result = values.unique() with (item); "
      "result = values.min(entry) with (entry.index < 0 ? 0 : entry); "
      "end endmodule",
      Language::SystemVerilog2017);
  require(
      locator_with.ok(),
      "extrema and uniqueness locator transformations parse");
  const auto invalid_locator_with = parse_text(
      "container-locator-invalid-with.sv",
      "module m; int values[]; int result[$]; int scalar; "
      "initial begin "
      "result = values.min() with (); "
      "result = values.max(entry); "
      "result = values.unique(first, second) with (first); "
      "result = values.unique_index() with item; "
      "result = values.min() with (item; "
      "scalar = item; "
      "end endmodule",
      Language::SystemVerilog2017);
  require(
      !invalid_locator_with.ok()
          && has_code(
              invalid_locator_with,
              "FSIM-SV-SEM-093")
          && has_code(
              invalid_locator_with,
              "FSIM-SV-PARSE-171")
          && has_code(
              invalid_locator_with,
              "FSIM-SV-PARSE-172")
          && has_code(
              invalid_locator_with,
              "FSIM-SV-SEM-090"),
      "locator transformations diagnose empty, malformed, invalid, and "
      "leaked binders");
  const auto missing_predicate = parse_text(
      "container-find-missing.sv",
      "module m; int values[]; int result[$]; "
      "initial begin result = values.find(); "
      "result = values.find_index() with (); end endmodule",
      Language::SystemVerilog2017);
  require(
      !missing_predicate.ok()
          && has_code(
              missing_predicate, "FSIM-SV-SEM-089"),
      "predicate locators require a nonempty with-clause");
  const auto malformed_predicate = parse_text(
      "container-find-malformed.sv",
      "module m; int values[]; int result[$]; "
      "initial result = values.find() with item; endmodule",
      Language::SystemVerilog2017);
  require(
      !malformed_predicate.ok()
          && has_code(
              malformed_predicate, "FSIM-SV-PARSE-165"),
      "predicate locator opening-parenthesis recovery is stable");
  const auto malformed_iterator = parse_text(
      "container-find-iterator-invalid.sv",
      "module m; int values[]; int result[$]; "
      "initial begin "
      "result = values.find(1) with (item); "
      "result = values.find(first, second) with (first); "
      "end endmodule",
      Language::SystemVerilog2017);
  require(
      !malformed_iterator.ok()
          && has_code(
              malformed_iterator, "FSIM-SV-SEM-090"),
      "predicate locators reject malformed or multiple iterator arguments");
  require(
      has_code(invalid, "FSIM-SV-UNSUPPORTED-037")
          && has_code(invalid, "FSIM-SV-UNSUPPORTED-038"),
      "static and ref container call boundaries diagnose");

  const auto malformed_static = parse_text(
      "static-range-invalid.sv",
      "module bad; logic [7:0] memory[3:0; endmodule",
      Language::SystemVerilog2017);
  require(
      !malformed_static.ok()
          && has_code(
              malformed_static, "FSIM-SV-PARSE-158"),
      "static-array closing-bracket recovery has a stable diagnostic");

  const auto verilog = parse_text(
      "container-verilog.v",
      "module m; integer values[]; integer value; "
      "initial begin value = '{1}; value = values.sum(); "
      "values.sort(); value = values.min(); "
      "value = values.find() with (item); end "
      "endmodule",
      Language::Verilog2005);
  require(
      !verilog.ok()
          && has_code(verilog, "FSIM-SV-SEM-077")
          && has_code(verilog, "FSIM-SV-SEM-084")
          && has_code(verilog, "FSIM-SV-SEM-085")
          && has_code(verilog, "FSIM-SV-SEM-086")
          && has_code(verilog, "FSIM-SV-SEM-087")
          && has_code(verilog, "FSIM-SV-SEM-088"),
      "containers, patterns, reductions, ordering, and locators require "
      "SystemVerilog-2017");

  const auto malformed_pattern = parse_text(
      "container-pattern-invalid.sv",
      "module m; int values[]; initial values = ' (1); endmodule",
      Language::SystemVerilog2017);
  require(
      !malformed_pattern.ok()
          && has_code(
              malformed_pattern, "FSIM-SV-PARSE-163"),
      "assignment-pattern opening-brace recovery is stable");
  const auto malformed_pattern_members = parse_text(
      "container-pattern-member-invalid.sv",
      "module m; int values[1:0]; initial begin "
      "values = '{default 1}; "
      "values = '{default: }; "
      "end endmodule",
      Language::SystemVerilog2017);
  require(
      !malformed_pattern_members.ok()
          && has_code(
              malformed_pattern_members,
              "FSIM-SV-PARSE-173")
          && has_code(
              malformed_pattern_members,
              "FSIM-SV-PARSE-174"),
      "assignment-pattern default punctuation and missing values "
      "diagnose with stable recovery");
}

}  // namespace fsim::tests::frontend
