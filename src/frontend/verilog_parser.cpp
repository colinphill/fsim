// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

#include <algorithm>
#include <string>

namespace fsim::frontend {

ParseResult parse_verilog(SourceText source, bool system_verilog) {
  return parse_verilog(
      std::move(source),
      system_verilog ? StandardRevision::SystemVerilog2017
                     : StandardRevision::Verilog2005);
}

ParseResult parse_verilog(
    SourceText source,
    const StandardRevision standard_revision) {
  return parse_verilog(
      std::move(source), standard_revision, "none");
}

ParseResult parse_verilog(
    SourceText source,
    const StandardRevision standard_revision,
    const std::string_view compatibility_profile) {
  return VerilogParser(
      lex(std::move(source), language_for_standard_revision(standard_revision)),
      standard_revision, std::string { compatibility_profile }).run();
}

ParseResult parse_verilog(LexResult lexed, bool system_verilog) {
  return parse_verilog(
      std::move(lexed),
      system_verilog ? StandardRevision::SystemVerilog2017
                     : StandardRevision::Verilog2005);
}

ParseResult parse_verilog(
    LexResult lexed,
    const StandardRevision standard_revision) {
  return parse_verilog(
      std::move(lexed), standard_revision, "none");
}

ParseResult parse_verilog(
    LexResult lexed,
    const StandardRevision standard_revision,
    const std::string_view compatibility_profile) {
  return parse_verilog(std::move(lexed), standard_revision,
      compatibility_profile, {});
}

ParseResult parse_verilog(
    LexResult lexed,
    const StandardRevision standard_revision,
    const std::string_view compatibility_profile,
    SystemVerilogPackageMemberLookup package_member_lookup) {
  return VerilogParser(std::move(lexed), standard_revision,
      std::string { compatibility_profile },
      std::move(package_member_lookup)).run();
}

std::optional<Statement> uvm_run_test_statement(const SourceSpan& call,
    std::optional<Expression> test_name,
    const StandardRevision standard_revision, std::string& diagnostic) {
  std::string text(call.begin.line > 1U ? call.begin.line - 1U : 0U, '\n');
  const bool named = test_name.has_value();
  // Each function phase calls its method on the components in the order
  // next() hands them out; run_phase runs as one process per component
  // (IEEE 1800.2-2020 9.8).
  const auto function_phase = [](const int kind,
                                  const std::string_view method) {
    return "__fsim_uvm_phase = __fsim_uvm_run_test_function_phase("
        + std::to_string(kind)
        + "); __fsim_uvm_component = __fsim_uvm_run_test_next(); "
          "while (__fsim_uvm_component != null) begin __fsim_uvm_component."
        + std::string { method }
        + "(__fsim_uvm_phase); __fsim_uvm_component = "
          "__fsim_uvm_run_test_next(); end ";
  };
  text += "module __fsim_uvm_run_test; initial begin begin : __fsim_uvm_run_test "
          "int __fsim_uvm_count; uvm_pkg::uvm_phase __fsim_uvm_phase; "
          "uvm_pkg::uvm_component __fsim_uvm_component; "
          "__fsim_uvm_count = __fsim_uvm_run_test_begin(";
  text += named ? "\"__fsim_uvm_test_name\"" : "";
  text += "); ";
  text += function_phase(0, "build_phase");
  text += function_phase(1, "connect_phase");
  text += function_phase(2, "end_of_elaboration_phase");
  text += function_phase(3, "start_of_simulation_phase");
  text += "__fsim_uvm_count = __fsim_uvm_run_test_components(); "
          "__fsim_uvm_phase = __fsim_uvm_run_test_phase(); "
          "for (int __fsim_uvm_index = 0; __fsim_uvm_index < __fsim_uvm_count; "
          "__fsim_uvm_index++) fork automatic uvm_pkg::uvm_component "
          "__fsim_uvm_task_component = "
          "__fsim_uvm_run_test_component(__fsim_uvm_index); "
          "__fsim_uvm_task_component.run_phase(__fsim_uvm_phase); join_none "
          "__fsim_uvm_run_test_wait(); ";
  text += function_phase(4, "extract_phase");
  text += function_phase(5, "check_phase");
  text += function_phase(6, "report_phase");
  text += function_phase(7, "final_phase");
  text += "__fsim_uvm_run_test_end(); $finish; end end endmodule\n";
  auto parsed = parse_verilog(
      SourceText { std::string { call.source_name.str() }, std::move(text) },
      standard_revision);
  Statement* block {};
  const auto find = [&](auto& self, auto& items) -> void {
    for (auto& item : items) {
      if (block != nullptr) {
        return;
      }
      if (item.label == "__fsim_uvm_run_test") {
        block = &item;
      } else {
        self(self, item.statements);
      }
    }
  };
  for (auto& unit : parsed.design.units) {
    for (auto& process : unit.processes) {
      find(find, process.statements);
    }
  }
  if (!parsed.ok() || block == nullptr) {
    diagnostic = parsed.diagnostics.empty()
        ? std::string {} : parsed.diagnostics.front().message;
    return std::nullopt;
  }
  // The placeholder names are the native calls of the run.
  const auto rewrite = [&](auto& self, Expression& expression) -> void {
    for (auto& operand : expression.operands) {
      self(self, operand);
    }
    if (expression.kind == ExpressionKind::StringLiteral
        && expression.text.find("__fsim_uvm_test_name") != std::string::npos) {
      expression = *test_name;
    } else if (expression.kind == ExpressionKind::Call
        && expression.text.starts_with("__fsim_uvm_run_test_")) {
      auto member = expression.text.substr(
          std::string_view { "__fsim_uvm_run_test_" }.size());
      std::ranges::replace(member, '_', '-');
      expression.text = "@sv-static-method:@uvm-run-test-" + member;
      const bool count = member == "begin" || member == "components";
      expression.call_result_width = count ? 32U : 64U;
      expression.call_result_signed = count;
      expression.call_result_domain = ValueDomain::Bit2;
    }
  };
  const auto walk = [&](auto& self, Statement& statement) -> void {
    for (auto* expression : { &statement.target, &statement.value,
             &statement.condition, &statement.loop_initial,
             &statement.loop_limit, &statement.loop_update_target }) {
      rewrite(rewrite, *expression);
    }
    for (auto& argument : statement.task_arguments) {
      rewrite(rewrite, argument);
    }
    for (auto& declaration : statement.declarations) {
      if (declaration.initializer) {
        rewrite(rewrite, *declaration.initializer);
      }
    }
    if (statement.kind == StatementKind::TaskCall
        && statement.task_name.starts_with("__fsim_uvm_run_test_")) {
      statement.task_name = "@sv-static-task:@uvm-run-test-"
          + statement.task_name.substr(
              std::string_view { "__fsim_uvm_run_test_" }.size());
    }
    for (auto* nested : { &statement.statements, &statement.loop_updates,
             &statement.else_statements }) {
      for (auto& item : *nested) {
        self(self, item);
      }
    }
  };
  walk(walk, *block);
  block->label.clear();
  return std::move(*block);
}

}  // namespace fsim::frontend
