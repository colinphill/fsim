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

  task automatic mutate(
      input int source[],
      inout logic [7:0] target[$]);
    int copy[];
    copy = new[4];
    target.push_back(8'h2a);
    target.pop_front();
  endtask

  initial begin
    values = new[3];
    pending.push_front(8'h11);
    bounded.delete();
    if (pending.size() == 1)
      values[0] = pending.pop_back();
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(parsed.ok(), "bounded container syntax parses");
  const auto* unit =
      parsed.design.find(UnitKind::VerilogModule, "containers");
  require(
      unit != nullptr && unit->variables.size() == 3,
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
  require(
      unit->tasks.size() == 1
          && unit->tasks[0].arguments.size() == 2
          && unit->tasks[0].arguments[0]
                 .type.systemverilog_container
          && unit->tasks[0].arguments[1]
                 .type.systemverilog_container
          && unit->tasks[0].variables.size() == 1,
      "automatic task formals and locals retain container types");
  require(
      unit->tasks[0].statements.size() == 3
          && unit->tasks[0].statements[1].kind
              == StatementKind::ContainerMethod
          && unit->tasks[0].statements[2].kind
              == StatementKind::ContainerMethod,
      "mutating queue methods remain explicit statements");

  const auto invalid = parse_text(
      "container-invalid.sv",
      R"(
module container_invalid;
  int fixed[3];
  int nested[][];
  string strings[];
  int queue[$];
  initial begin
    queue.push_back();
    queue.delete(1);
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !invalid.ok()
          && has_code(invalid, "FSIM-SV-SEM-078")
          && has_code(invalid, "FSIM-SV-SEM-079")
          && has_code(invalid, "FSIM-SV-SEM-080")
          && has_code(invalid, "FSIM-SV-SEM-081"),
      "unsupported dimensions, elements, and method arities diagnose");

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
