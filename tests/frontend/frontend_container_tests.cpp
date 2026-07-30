// SPDX-License-Identifier: Apache-2.0
#include "frontend_test_support.hpp"

#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>

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
    values.reverse();
    values.sort();
    values.rsort();
    pending.reverse();
    pending.sort();
    bounded.rsort();
    located = values.min();
    located = values.max();
    located = values.unique();
    located_indices = values.unique_index();
    located = values.find() with (item > 0);
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
          && image.static_range_expression,
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
  require(
      std::ranges::count_if(
          query_statements,
          [](const auto& statement) {
            return statement.value.kind
                == ExpressionKind::Aggregate;
          })
          == 4,
      "static, dynamic, queue, and associative assignment patterns "
      "remain aggregate HIR");
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
          == 6,
      "container ordering methods remain explicit no-argument "
      "method-statement HIR");
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
          == 4,
      "container locator methods remain explicit no-argument call HIR");
  require(
      std::ranges::count_if(
          query_statements,
          [](const auto& statement) {
            return statement.value.kind == ExpressionKind::Call
                && statement.value.text.starts_with(".find")
                && statement.value.operands.size() == 2;
          })
          == 6,
      "predicate locator calls retain the receiver and one scoped "
      "with-clause expression");
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
                 ->static_range_expression
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

  const auto unsupported_reduction = parse_text(
      "container-reduction-with.sv",
      "module m; int values[]; int result; "
      "initial result = values.sum() with (item); endmodule",
      Language::SystemVerilog2017);
  require(
      !unsupported_reduction.ok()
          && has_code(
              unsupported_reduction,
              "FSIM-SV-UNSUPPORTED-039"),
      "container reduction with-clauses diagnose explicitly");

  const auto unsupported_ordering = parse_text(
      "container-ordering-with.sv",
      "module m; int values[]; "
      "initial values.sort() with (item); endmodule",
      Language::SystemVerilog2017);
  require(
      !unsupported_ordering.ok()
          && has_code(
              unsupported_ordering,
              "FSIM-SV-UNSUPPORTED-041"),
      "container ordering with-clauses diagnose explicitly");
  const auto unsupported_locator = parse_text(
      "container-locator-with.sv",
      "module m; int values[]; int result[$]; "
      "initial result = values.unique() with (item); endmodule",
      Language::SystemVerilog2017);
  require(
      !unsupported_locator.ok()
          && has_code(
              unsupported_locator,
              "FSIM-SV-UNSUPPORTED-042"),
      "container locator with-clauses diagnose explicitly");
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
}

}  // namespace fsim::tests::frontend
