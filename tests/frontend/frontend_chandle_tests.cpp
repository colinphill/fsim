// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/class_resolution.hpp"
#include "fsim/frontend/frontend.hpp"
#include "fsim/frontend/systemverilog_scalar_folding.hpp"

#include <algorithm>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::frontend {
namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition) throw std::runtime_error{std::string{message}};
}

}  // namespace

void test_systemverilog_chandle_declarations() {
  using namespace fsim::frontend;
  auto parsed = parse_text(
      "chandle-declarations.sv",
      R"(
module chandle_types #(
    parameter chandle EMPTY = null,
    parameter chandle ALIAS = EMPTY,
    parameter bit IS_NULL = (ALIAS == null)) (
    input chandle imported,
    output chandle exported);
  typedef chandle native_handle_t;
  chandle stored;
  chandle source[$];
  chandle target[$];
  integer descriptor;

  function automatic chandle choose(
      input chandle primary,
      input chandle fallback = null);
    return chandle'(primary);
  endfunction

  task automatic replace(
      ref chandle target,
      input chandle value = null);
    target = value;
  endtask

  function automatic void copy_queue();
    process current;
    target = source;
    void'(target.pop_front());
    current = process::self();
    if (current != process::self())
      current = null;
  endfunction

  initial begin
    stored = imported;
    $fwrite(descriptor, "%0h", stored);
    if (stored != null)
      exported = choose(stored);
  end
endmodule

class chandle_owner;
  chandle foreign = null;
  process current;

  function bit same_process(process observed);
    return observed == current;
  endfunction
endclass
)",
      Language::SystemVerilog2017);
  if (!parsed.ok()) {
    for (const auto& diagnostic : parsed.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  require(parsed.ok(), "SystemVerilog chandle declarations must parse");
  const auto& unit = parsed.design.units.front();
  const auto chandle_kind = SystemVerilogScalarKind::Chandle;
  require(
      unit.parameters.size() == 3
          && unit.parameters[0].type.systemverilog_scalar == chandle_kind
          && unit.parameters[1].type.systemverilog_scalar == chandle_kind
          && unit.ports.size() == 2
          && unit.ports[0].type.systemverilog_scalar == chandle_kind
          && unit.ports[1].type.systemverilog_scalar == chandle_kind
          && unit.ports[0].type.width() == 64,
      "parameters and ports retain exact opaque chandle identity");
  require(
      unit.type_aliases.size() == 1
          && unit.type_aliases[0].type.systemverilog_scalar == chandle_kind
          && unit.signals.size() >= 2
          && unit.signals[0].type.systemverilog_scalar == chandle_kind,
      "typedefs and variables retain exact opaque chandle identity");
  require(
      unit.functions.size() == 2
          && unit.functions[0].return_type.systemverilog_scalar
              == chandle_kind
          && unit.functions[0].arguments.size() == 2
          && unit.functions[0].arguments[0].type.systemverilog_scalar
              == chandle_kind
          && unit.functions[0].arguments[1].type.systemverilog_scalar
              == chandle_kind
          && unit.tasks.size() == 1
          && unit.tasks[0].arguments.size() == 2
          && unit.tasks[0].arguments[0].reference
          && unit.tasks[0].arguments[0].type.systemverilog_scalar
              == chandle_kind,
      "function and task profiles retain chandle returns, formals, and ref");
  require(
      parsed.design.systemverilog_classes.size() == 1
          && parsed.design.systemverilog_classes[0].properties.size() == 2
          && parsed.design.systemverilog_classes[0].properties[0]
                 .declaration.type.systemverilog_scalar == chandle_kind,
      "class properties retain chandle identity");

  std::string error;
  auto equal = Expression{
      ExpressionKind::Binary,
      "==",
      {
          Expression{ExpressionKind::Identifier, "handle", {}, {}},
          Expression{ExpressionKind::Call, "@sv-null", {}, {}},
      },
      {}};
  const SystemVerilogScalarTypeEnvironment types{{"handle", chandle_kind}};
  require(
      propagate_systemverilog_scalar_types(equal, types, error)
          && equal.systemverilog_scalar_kind
              == SystemVerilogScalarKind::None,
      "chandle equality with null resolves to an integral predicate");
  const SystemVerilogScalarConstantEnvironment constants{
      {"handle", {chandle_kind, 0}}};
  const auto equal_value = evaluate_systemverilog_scalar_constant(
      equal, constants, {}, error);
  require(
      equal_value && equal_value->integral()
          && *equal_value->integral() == 1,
      "null chandle equality folds without treating the handle as numeric");

  auto cast = Expression{
      ExpressionKind::Call,
      "@sv-cast:chandle",
      {Expression{ExpressionKind::Call, "@sv-null", {}, {}}},
      {}};
  require(
      propagate_systemverilog_scalar_types(cast, {}, error)
          && cast.systemverilog_scalar_kind == chandle_kind
          && evaluate_systemverilog_scalar_constant(cast, {}, {}, error),
      "chandle casts preserve a chandle or null operand");

  auto invalid = Expression{
      ExpressionKind::Binary,
      "+",
      {
          Expression{ExpressionKind::Identifier, "handle", {}, {}},
          Expression{ExpressionKind::IntegerLiteral, "1", {}, {}},
      },
      {}};
  require(
      !propagate_systemverilog_scalar_types(invalid, types, error)
          && error.find("chandle") != std::string::npos,
      "chandle arithmetic is rejected before numeric scalar folding");

  require(
      resolve_systemverilog_classes(parsed.design, parsed.diagnostics),
      "valid chandle assignments, casts, comparisons, and returns resolve");
  std::vector<Diagnostic> repeated_diagnostics;
  require(
      resolve_systemverilog_classes(parsed.design, repeated_diagnostics),
      "cached class-property chandle expressions resolve repeatedly");

  auto rejected = parse_text(
      "invalid-chandle-semantics.sv",
      R"(
module invalid_chandle_semantics;
  chandle handle;
  real number;
  initial begin
    handle = number;
    number = handle;
    handle = handle + 1;
    if (handle)
      number = 0;
    handle = int'(handle);
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(rejected.ok(), "invalid chandle expressions must first parse");
  require(
      !resolve_systemverilog_classes(
          rejected.design, rejected.diagnostics),
      "numeric assignment, arithmetic, truth, and numeric casts reject");
  const auto has_code = [&](const std::string_view code) {
    return std::ranges::any_of(
        rejected.diagnostics,
        [&](const Diagnostic& diagnostic) {
          return diagnostic.code == code;
        });
  };
  require(
      has_code("FSIM-SV-SEM-174") && has_code("FSIM-SV-SEM-175"),
      "chandle assignment/cast and operator failures use stable diagnostics");
}

}  // namespace fsim::tests::frontend
