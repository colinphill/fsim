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
    values = new[3];
    pending.push_front(8'h11);
    bounded.delete();
    if (pending.size() == 1)
      values[0] = pending.pop_back();
  end
endmodule

module static_port_child #(
    parameter int LEFT = 3,
    parameter int RIGHT = 0) (
    input logic signed [7:0] source[LEFT:RIGHT],
    output bit [3:0] result[-1:1],
    inout logic [15:0] shared[0:2]);
endmodule

module non_ansi_static_port(source, result);
  parameter int LEFT = 3;
  input logic [7:0] source[LEFT:0];
  output bit [3:0] result[-1:1];
endmodule
)",
      Language::SystemVerilog2017);
  require(parsed.ok(), "bounded container syntax parses");
  const auto* unit =
      parsed.design.find(UnitKind::VerilogModule, "containers");
  require(
      unit != nullptr && unit->variables.size() == 7,
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
          && !unit->processes[0].statements[0].memory_start
          && unit->processes[0].statements[1].kind
              == StatementKind::MemoryLoad
          && !unit->processes[0].statements[1].memory_hex
          && unit->processes[0].statements[1].memory_start
          && unit->processes[0].statements[1].memory_finish,
      "$readmemh/$readmemb retain target, radix, and optional bounds");
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
  const auto* non_ansi_static_port =
      parsed.design.find(
          UnitKind::VerilogModule, "non_ansi_static_port");
  require(
      non_ansi_static_port != nullptr
          && non_ansi_static_port->ports.size() == 2
          && non_ansi_static_port->variables.empty()
          && non_ansi_static_port->ports[0]
                 .type.systemverilog_container
          && non_ansi_static_port->ports[1]
                 .type.systemverilog_container,
      "non-ANSI static-array declarations refine port placeholders "
      "without becoming module variables");

  const auto invalid = parse_text(
      "container-invalid.sv",
      R"(
module container_invalid;
  input int dynamic_port[];
  output int queue_port[$];
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
          && has_code(invalid, "FSIM-SV-SEM-081")
          && has_code(invalid, "FSIM-SV-SEM-084"),
      "unsupported dimensions, elements, and method arities diagnose");
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
      "module m; integer values[]; endmodule",
      Language::Verilog2005);
  require(
      !verilog.ok()
          && has_code(verilog, "FSIM-SV-SEM-077"),
      "containers require SystemVerilog-2017");
}

}  // namespace fsim::tests::frontend
