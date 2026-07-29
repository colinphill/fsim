// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "fsim/frontend/parser.hpp"

#include "parser_support.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fsim::frontend {


using detail::decimal_i64;
using detail::decimal_u64;
using detail::vhdl_name;

[[nodiscard]] inline std::optional<std::int64_t> simple_integer_constant(
    const Expression& expression) {
  if (expression.kind == ExpressionKind::IntegerLiteral) {
    return decimal_i64(expression.text);
  }
  if (expression.kind == ExpressionKind::Unary
      && expression.operands.size() == 1
      && (expression.text == "+" || expression.text == "-")) {
    const auto magnitude =
        simple_integer_constant(expression.operands.front());
    if (!magnitude) {
      return std::nullopt;
    }
    if (expression.text == "+") {
      return magnitude;
    }
    if (*magnitude == std::numeric_limits<std::int64_t>::min()) {
      return std::nullopt;
    }
    return -*magnitude;
  }
  return std::nullopt;
}

class VhdlParser final : private detail::ParserBase {
 public:
  explicit VhdlParser(LexResult lexed);

  ParseResult run();

 private:
  static SourceSpan span_from(const Token& first, const Token& last);

  static std::string string_literal_text(const Token& token);

  std::optional<VhdlContextItem> parse_context_item();

  Token expect_identifier(std::string_view description);

  DesignUnit parse_context_declaration(const Token& start);

  void skip_vhdl_package_body();

  DesignUnit parse_package(const Token& start);

  void parse_package_constant(
      DesignUnit& unit, const Token& start);

  DesignUnit parse_entity(const Token& start);

  void add_vhdl_generic(
      DesignUnit& unit,
      ParameterDeclaration generic,
      const Token& name);

  void parse_vhdl_generics(
      DesignUnit& unit,
      const Token& start);

  void parse_vhdl_ports(DesignUnit& unit);

  Type parse_vhdl_type(
      const bool allow_integer = false,
      const bool /*runtime_base_integer_only*/ = false);

  void parse_vhdl_end(std::string_view expected_kind);

  DesignUnit parse_architecture(const Token& start);

  void parse_type_declaration(
      DesignUnit& unit, const Token& start);

  void parse_subtype_declaration(
      DesignUnit& unit, const Token& start);

  void parse_signal_declaration(
      std::vector<SignalDeclaration>& signals,
      const std::vector<ParameterDeclaration>* constants = nullptr);

  void parse_concurrent_statement(DesignUnit& unit);

  GenerateRegion parse_vhdl_conditional_generate(
      const Token& label,
      const Token& start);

  GenerateRegion parse_vhdl_iterative_generate(
      const Token& label,
      const Token& start);

  GenerateRegion parse_vhdl_selection_generate(
      const Token& label,
      const Token& start);

  GenerateRegion parse_vhdl_static_block(
      const Token& label,
      const Token& start);

  void parse_vhdl_generate_declarations(GenerateBody& body);

  void parse_vhdl_generate_constant(
      GenerateBody& body, const Token& start);

  void parse_vhdl_generate_branch(
      GenerateBody& body,
      const bool stop_at_case_alternative = false);

  Instance parse_vhdl_instance(const Token& label);

  void parse_vhdl_generic_map(
      Instance& instance,
      const Token& start);

  PortConnection parse_vhdl_port_connection();

  void skip_vhdl_connection_actual();

  Process parse_process(std::string label);

  std::vector<Statement> parse_statement_list(
      std::initializer_list<std::string_view> terminators);

  std::optional<Statement> parse_sequential_statement();

  Statement parse_vhdl_assertion(const Token& start);

  void validate_opening_loop_label(
      const std::string_view label,
      const Token& token);

  void parse_loop_end_label(
      const std::string_view opening_label);

  Statement parse_if_branch(const Token& start);

  std::optional<Statement> parse_assignment(bool concurrent);

  Statement parse_vhdl_selected_assignment(const Token& start);

  Statement parse_conditional_signal_assignment(Statement assignment);

  void parse_vhdl_waveform(Statement& statement);

  void parse_vhdl_delay_mechanism(Statement& statement);

  void diagnose_misplaced_vhdl_delay_mechanism();

  Expression parse_conditional_assignment_value();

  Delay parse_vhdl_delay(const Token& start);

  Expression parse_lvalue();

  Expression parse_expression(int minimum_precedence = 0);

  struct BinaryOperation {
    int precedence;
    std::string name;
  };

  std::optional<BinaryOperation> binary_operation() const;

  Expression parse_unary();

  Expression parse_primary();

  static void infer_process_edge(Process& process);

  std::size_t sequential_loop_depth_{};
  std::unordered_set<std::string> vhdl_named_types_;
  std::vector<std::string> sequential_loop_labels_;
  std::vector<std::string> sequential_loop_labels_seen_;
};

}  // namespace fsim::frontend
