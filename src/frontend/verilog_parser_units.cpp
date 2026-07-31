// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

namespace fsim::frontend {

void VerilogParser::parse_import_clause(
  std::vector<SystemVerilogImport>& imports,
  const Token& start) {
  for (;;) {
    const auto package =
        expect_identifier("package name in import");
    expect(
        TokenKind::Scope,
        "'::' after imported package name",
        "FSIM-SV-PARSE-078");
    std::string name;
    if (match(TokenKind::Star)) {
      name.clear();
    } else {
      name =
          expect_identifier("imported package item").text;
    }
    imports.push_back({
        package.text,
        std::move(name),
        cover(start.span, previous().span)});
    if (!match(TokenKind::Comma)) {
      break;
    }
  }
  expect(
      TokenKind::Semicolon,
      "';' after package import",
      "FSIM-SV-PARSE-079");
}

DesignUnit VerilogParser::parse_package(const Token& start) {
  non_ansi_ports_.clear();
  body_port_declarations_.clear();
  port_type_refinements_.clear();
  implicit_net_references_.clear();
  container_iterator_names_.clear();
  current_procedural_names_.clear();
  current_generate_names_.clear();
  declared_genvars_.clear();
  external_genvar_uses_.clear();
  module_time_unit_magnitude_ =
      compilation_time_unit_.empty()
          ? current_time_unit_magnitude_
          : compilation_time_unit_magnitude_;
  module_time_unit_ =
      compilation_time_unit_.empty()
          ? current_time_unit_
          : compilation_time_unit_;
  module_time_precision_ =
      compilation_time_precision_.empty()
          ? current_time_precision_
          : compilation_time_precision_;
  module_time_unit_declared_ = false;
  module_time_precision_declared_ = false;
  module_has_non_time_item_ = false;
  DesignUnit unit;
  unit.kind = UnitKind::SystemVerilogPackage;
  unit.language = language_;
  unit.systemverilog_imports =
      compilation_unit_imports_;
  active_package_imports_ =
      unit.systemverilog_imports;
  update_unit_time(unit);
  const auto name = expect_identifier("package name");
  unit.name = name.text;
  expect(
      TokenKind::Semicolon,
      "';' after package header",
      "FSIM-SV-PARSE-080");
  while (!at_end() && !keyword("endpackage")) {
    if (time_declaration_start()) {
      const auto declaration = advance();
      parse_time_declaration(&unit, declaration);
    } else if (match_keyword("parameter")
        || match_keyword("localparam")) {
      module_has_non_time_item_ = true;
      parse_parameter_group(
          unit, true, false, previous());
    } else if (match_keyword("import")) {
      module_has_non_time_item_ = true;
      parse_import_clause(
          unit.systemverilog_imports, previous());
      active_package_imports_ =
          unit.systemverilog_imports;
    } else if (match_keyword("typedef")) {
      module_has_non_time_item_ = true;
      parse_typedef(unit, previous());
    } else if (match_keyword("function")) {
      module_has_non_time_item_ = true;
      auto function = parse_function(previous());
      const bool duplicate = std::ranges::any_of(
          unit.functions,
          [&](const FunctionDeclaration& existing) {
            return existing.name == function.name;
          })
          || std::ranges::any_of(
              unit.tasks,
              [&](const TaskDeclaration& existing) {
            return existing.name == function.name;
          });
      if (duplicate) {
        error(
            start,
            "FSIM-SV-SEM-066",
            "duplicate package function '" + function.name + "'");
      } else {
        unit.functions.push_back(std::move(function));
      }
    } else if (match_keyword("task")) {
      module_has_non_time_item_ = true;
      auto task = parse_task(previous());
      const bool duplicate = std::ranges::any_of(
          unit.tasks,
          [&](const TaskDeclaration& existing) {
            return existing.name == task.name;
          })
          || std::ranges::any_of(
              unit.functions,
              [&](const FunctionDeclaration& existing) {
            return existing.name == task.name;
          });
      if (duplicate) {
        error(
            start,
            "FSIM-SV-SEM-073",
            "duplicate package task '" + task.name + "'");
      } else {
        unit.tasks.push_back(std::move(task));
      }
    } else {
      module_has_non_time_item_ = true;
      const auto unsupported = advance();
      error(
          unsupported,
          "FSIM-SV-UNSUPPORTED-023",
          "unsupported package item starting with '"
              + unsupported.text + "'");
      skip_to_semicolon();
    }
  }
  expect_keyword(
      "endpackage", false, "FSIM-SV-PARSE-081");
  if (match(TokenKind::Colon)) {
    const auto end_name =
        expect_identifier("package name after endpackage");
    if (end_name.text != unit.name) {
      error(
          end_name,
          "FSIM-SV-SEM-023",
          "package end name does not match '"
              + unit.name + "'");
    }
  }
  unit.span = span_from(start, previous());
  return unit;
}

void VerilogParser::parse_genvar_declaration(DesignUnit& unit) {
  for (;;) {
    const auto name = expect_identifier("genvar name");
    const bool object_conflict =
        std::any_of(
            unit.parameters.begin(),
            unit.parameters.end(),
            [&](const ParameterDeclaration& parameter) {
              return parameter.name == name.text;
            })
        || std::any_of(
            unit.ports.begin(),
            unit.ports.end(),
            [&](const SignalDeclaration& port) {
              return port.name == name.text;
            })
        || std::any_of(
            unit.signals.begin(),
            unit.signals.end(),
            [&](const SignalDeclaration& signal) {
              return signal.name == name.text;
            });
    if (object_conflict
        || !declared_genvars_.insert(name.text).second) {
      error(
          name,
          "FSIM-SV-SEM-022",
          "duplicate or conflicting genvar declaration '"
              + name.text + "'");
    } else {
      ++current_generate_names_[name.text];
    }
    if (!match(TokenKind::Comma)) {
      break;
    }
  }
  expect(
      TokenKind::Semicolon,
      "';' after genvar declaration",
      "FSIM-SV-PARSE-077");
}

void VerilogParser::parse_generate_region(
  DesignUnit& unit, const Token& generate_token) {
  GenerateRegion direct_region;
  direct_region.kind = GenerateKind::StaticBlock;
  std::vector<std::string> direct_local_names;
  while (!at_end() && !keyword("endgenerate")) {
    if (match_keyword("if")) {
      direct_region.then_body.generate_regions.push_back(
          parse_conditional_generate(previous()));
      continue;
    }
    if (match_keyword("for")) {
      direct_region.then_body.generate_regions.push_back(
          parse_iterative_generate(previous()));
      continue;
    }
    if (match_keyword("case")) {
      direct_region.then_body.generate_regions.push_back(
          parse_selection_generate(previous()));
      continue;
    }
    if (keyword("begin")) {
      direct_region.then_body.generate_regions.push_back(
          parse_static_generate_block());
      continue;
    }
    if (match_keyword("parameter")) {
      parse_generated_parameter_group(
          direct_region.then_body,
          direct_local_names,
          false,
          previous());
      continue;
    }
    if (match_keyword("localparam")) {
      parse_generated_parameter_group(
          direct_region.then_body,
          direct_local_names,
          true,
          previous());
      continue;
    }
    if (
        keyword("final")
        || (language_ == Language::Verilog2005
            && at(TokenKind::Identifier)
            && current().text == "final")) {
      direct_region.then_body.processes.push_back(
          parse_final());
      continue;
    }
    if (is_declaration_start()) {
      parse_generate_declaration(
          direct_region.then_body,
          direct_local_names);
      continue;
    }
    if (match_keyword("assign")) {
      if (auto assignment =
              parse_continuous_assignment(previous())) {
        direct_region.then_body.concurrent_statements.push_back(
            std::move(*assignment));
      }
      continue;
    }
    if (
        keyword("always") || keyword("always_ff")
        || keyword("always_comb") || keyword("always_latch")) {
      direct_region.then_body.processes.push_back(
          parse_always());
      continue;
    }
    if (keyword("initial")) {
      direct_region.then_body.processes.push_back(
          parse_initial());
      continue;
    }
    if (
        at(TokenKind::Identifier)
        && ((at(TokenKind::Identifier, 1)
             && at(TokenKind::LeftParen, 2))
            || (at(TokenKind::Hash, 1)
                && at(TokenKind::LeftParen, 2)))) {
      direct_region.then_body.instances.push_back(
          parse_instance());
      continue;
    }
    const auto unsupported = advance();
    error(
        unsupported,
        "FSIM-SV-UNSUPPORTED-021",
        "only conditional, canonical genvar-for, and case instance "
        "generate regions are executable");
    while (!at_end() && !keyword("endgenerate")
           && !keyword("if") && !keyword("for")
           && !keyword("case")) {
      advance();
    }
  }
  expect_keyword(
      "endgenerate", false, "FSIM-SV-PARSE-064");
  direct_region.span =
      span_from(generate_token, previous());
  const bool has_direct_items =
      !direct_region.then_body.constants.empty()
      || !direct_region.then_body.signals.empty()
      || !direct_region.then_body.concurrent_statements.empty()
      || !direct_region.then_body.processes.empty()
      || !direct_region.then_body.instances.empty();
  if (has_direct_items) {
    unit.generate_regions.push_back(
        std::move(direct_region));
  } else {
    auto& nested =
        direct_region.then_body.generate_regions;
    unit.generate_regions.insert(
        unit.generate_regions.end(),
        std::make_move_iterator(nested.begin()),
        std::make_move_iterator(nested.end()));
  }
  for (const auto& local_name : direct_local_names) {
    const auto found = current_generate_names_.find(local_name);
    if (found != current_generate_names_.end()
        && --found->second == 0) {
      current_generate_names_.erase(found);
    }
  }
}

GenerateRegion VerilogParser::parse_static_generate_block() {
  GenerateRegion result;
  result.kind = GenerateKind::StaticBlock;
  const auto start = current();
  parse_generate_branch(
      result.then_scope, result.then_body);
  result.span = span_from(start, previous());
  return result;
}

GenerateRegion VerilogParser::parse_conditional_generate(
  const Token& start) {
  GenerateRegion result;
  expect(
      TokenKind::LeftParen,
      "'(' after generate if",
      "FSIM-SV-PARSE-059");
  result.condition = parse_expression();
  expect(
      TokenKind::RightParen,
      "')' after generate condition",
      "FSIM-SV-PARSE-060");
  parse_generate_branch(
      result.then_scope,
      result.then_body);
  if (match_keyword("else")) {
    parse_generate_branch(
        result.else_scope,
        result.else_body);
  }
  result.span = span_from(start, previous());
  return result;
}

GenerateRegion VerilogParser::parse_iterative_generate(const Token& start) {
  GenerateRegion result;
  result.kind = GenerateKind::Iterative;
  expect(
      TokenKind::LeftParen,
      "'(' after generate for",
      "FSIM-SV-PARSE-065");
  const bool inline_genvar = match_keyword("genvar");
  const auto variable = expect_identifier("generate loop variable");
  result.variable = variable.text;
  if (!inline_genvar) {
    external_genvar_uses_.push_back(variable);
  }
  ++current_generate_names_[result.variable];
  expect(
      TokenKind::Assign,
      "'=' after generate loop variable",
      "FSIM-SV-PARSE-067");
  result.initial = parse_expression();
  expect(
      TokenKind::Semicolon,
      "';' after generate loop initializer",
      "FSIM-SV-PARSE-068");
  result.condition = parse_expression();
  expect(
      TokenKind::Semicolon,
      "';' after generate loop condition",
      "FSIM-SV-PARSE-069");
  std::optional<Token> prefix_update;
  if (match(TokenKind::PlusPlus)
      || match(TokenKind::MinusMinus)) {
    prefix_update = previous();
  }
  const auto iteration_variable =
      expect_identifier("generate iteration variable");
  if (iteration_variable.text != result.variable) {
    error(
        iteration_variable,
        "FSIM-SV-PARSE-070",
        "generate iteration must assign loop variable '"
            + result.variable + "'");
  }
  const auto stepped_iteration =
      [&](const std::string_view operation,
          Expression amount,
          const SourceSpan& span) {
        return Expression{
            ExpressionKind::Binary,
            std::string{operation},
            {
                Expression{
                    ExpressionKind::Identifier,
                    result.variable,
                    {},
                    iteration_variable.span},
                std::move(amount)},
            span};
      };
  const auto one = [&]() {
    return Expression{
        ExpressionKind::IntegerLiteral,
        "1",
        {},
        iteration_variable.span};
  };
  if (prefix_update) {
    result.iteration = stepped_iteration(
        prefix_update->kind == TokenKind::PlusPlus
            ? "+" : "-",
        one(),
        cover(
            prefix_update->span,
            iteration_variable.span));
  } else if (match(TokenKind::Assign)) {
    result.iteration = parse_expression();
  } else if (
      match(TokenKind::PlusPlus)
      || match(TokenKind::MinusMinus)) {
    const auto update = previous();
    result.iteration = stepped_iteration(
        update.kind == TokenKind::PlusPlus ? "+" : "-",
        one(),
        cover(iteration_variable.span, update.span));
  } else if (
      match(TokenKind::PlusAssign)
      || match(TokenKind::MinusAssign)) {
    const auto update = previous();
    auto amount = parse_expression();
    result.iteration = stepped_iteration(
        update.kind == TokenKind::PlusAssign ? "+" : "-",
        std::move(amount),
        cover(iteration_variable.span, previous().span));
  } else {
    error(
        current(),
        "FSIM-SV-PARSE-071",
        "generate loop iteration must use assignment, increment, "
        "decrement, +=, or -=");
    result.iteration = Expression{
        ExpressionKind::Invalid,
        current().text,
        {},
        current().span};
    while (!at_end() && !at(TokenKind::RightParen)) {
      advance();
    }
  }
  expect(
      TokenKind::RightParen,
      "')' after generate loop header",
      "FSIM-SV-PARSE-072");
  parse_generate_branch(
      result.then_scope,
      result.then_body);
  const auto generate_name =
      current_generate_names_.find(result.variable);
  if (generate_name != current_generate_names_.end()
      && --generate_name->second == 0) {
    current_generate_names_.erase(generate_name);
  }
  result.span = span_from(start, previous());
  return result;
}

GenerateRegion VerilogParser::parse_selection_generate(const Token& start) {
  GenerateRegion result;
  result.kind = GenerateKind::Selection;
  expect(
      TokenKind::LeftParen,
      "'(' after generate case",
      "FSIM-SV-PARSE-073");
  result.condition = parse_expression();
  expect(
      TokenKind::RightParen,
      "')' after generate case selector",
      "FSIM-SV-PARSE-074");
  bool saw_default = false;
  while (!at_end() && !keyword("endcase")) {
    GenerateAlternative alternative;
    const auto alternative_start = current();
    if (match_keyword("default")) {
      alternative.is_default = true;
      if (saw_default) {
        error(
            previous(),
            "FSIM-SV-SEM-021",
            "generate case contains more than one default item");
      }
      saw_default = true;
    } else {
      do {
        auto expression = parse_expression();
        const auto span = expression.span;
        alternative.choices.push_back(GenerateChoice{
            std::move(expression),
            std::nullopt,
            false,
            span});
      } while (match(TokenKind::Comma));
    }
    expect(
        TokenKind::Colon,
        "':' after generate case choices",
        "FSIM-SV-PARSE-075");
    parse_generate_branch(
        alternative.scope,
        alternative.body);
    alternative.span =
        span_from(alternative_start, previous());
    result.alternatives.push_back(std::move(alternative));
  }
  expect_keyword("endcase", false, "FSIM-SV-PARSE-076");
  result.span = span_from(start, previous());
  return result;
}

void VerilogParser::parse_generate_branch(
  std::string& scope,
  GenerateBody& body) {
  if (!match_keyword("begin")) {
    error(
        current(),
        "FSIM-SV-PARSE-061",
        "a conditional generate branch must use a labeled begin/end "
        "block");
    return;
  }
  expect(
      TokenKind::Colon,
      "':' before generate block label",
      "FSIM-SV-PARSE-062");
  const auto label = expect_identifier("generate block label");
  scope = label.text;
  std::vector<std::string> local_names;
  while (!at_end() && !keyword("end")) {
    if (
        keyword("final")
        || (language_ == Language::Verilog2005
            && at(TokenKind::Identifier)
            && current().text == "final")) {
      body.processes.push_back(parse_final());
    } else if (is_declaration_start()) {
      parse_generate_declaration(body, local_names);
    } else if (match_keyword("parameter")) {
      parse_generated_parameter_group(
          body, local_names, false, previous());
    } else if (match_keyword("localparam")) {
      parse_generated_parameter_group(
          body, local_names, true, previous());
    } else if (match_keyword("assign")) {
      if (auto assignment =
              parse_continuous_assignment(previous())) {
        body.concurrent_statements.push_back(
            std::move(*assignment));
      }
    } else if (
        keyword("always") || keyword("always_ff")
        || keyword("always_comb") || keyword("always_latch")) {
      body.processes.push_back(parse_always());
    } else if (keyword("initial")) {
      body.processes.push_back(parse_initial());
    } else if (match_keyword("if")) {
      body.generate_regions.push_back(
          parse_conditional_generate(previous()));
    } else if (match_keyword("for")) {
      body.generate_regions.push_back(
          parse_iterative_generate(previous()));
    } else if (match_keyword("case")) {
      body.generate_regions.push_back(
          parse_selection_generate(previous()));
    } else if (keyword("begin")) {
      body.generate_regions.push_back(
          parse_static_generate_block());
    } else if (
        at(TokenKind::Identifier)
        && ((at(TokenKind::Identifier, 1)
             && at(TokenKind::LeftParen, 2))
            || (at(TokenKind::Hash, 1)
                && at(TokenKind::LeftParen, 2)))) {
      body.instances.push_back(parse_instance());
    } else {
      const auto unsupported = advance();
      error(
          unsupported,
          "FSIM-SV-UNSUPPORTED-021",
          "unsupported item in generated module body");
      skip_to_semicolon();
    }
  }
  expect_keyword("end", false, "FSIM-SV-PARSE-063");
  if (match(TokenKind::Colon)) {
    const auto end_label = expect_identifier(
        "generate block label after end");
    if (end_label.text != scope) {
      error(
          end_label,
          "FSIM-SV-PARSE-063",
          "generate end label does not match '" + scope + "'");
    }
  }
  for (const auto& local_name : local_names) {
    const auto found = current_generate_names_.find(local_name);
    if (found != current_generate_names_.end()
        && --found->second == 0) {
      current_generate_names_.erase(found);
    }
  }
}

void VerilogParser::parse_generate_declaration(
  GenerateBody& body,
  std::vector<std::string>& local_names) {
  const auto start = current();
  Type type = default_verilog_type();
  if (is_direction_keyword()) {
    error(
        current(),
        "FSIM-SV-UNSUPPORTED-022",
        "port directions are not legal generated local "
        "declarations");
    (void)parse_direction();
  }
  if (is_named_type_reference_start()) {
    type = parse_named_type();
  } else {
    parse_optional_net_type(type);
    parse_optional_signedness(type);
    parse_optional_range(type);
  }
  for (;;) {
    const auto name = expect_identifier(
        "generated local signal name");
    if (at(TokenKind::LeftBracket)) {
      const auto dimension = current();
      error(
          dimension,
          "FSIM-SV-UNSUPPORTED-007",
          "unpacked generated arrays are not implemented");
      skip_balanced(
          TokenKind::LeftBracket, TokenKind::RightBracket);
    }
    if (match(TokenKind::Assign)) {
      (void)parse_expression();
      error(
          name,
          "FSIM-SV-UNSUPPORTED-011",
          "generated declaration initializers are not executable");
    }
    const auto duplicate = std::find_if(
        body.signals.begin(),
        body.signals.end(),
        [&](const SignalDeclaration& signal) {
          return signal.name == name.text;
        });
    const bool parameter_conflict =
        std::any_of(
            body.constants.begin(),
            body.constants.end(),
            [&](const ParameterDeclaration& parameter) {
              return parameter.name == name.text;
            });
    if (parameter_conflict) {
      error(
          name,
          "FSIM-SV-SEM-020",
          "generated signal '" + name.text
              + "' conflicts with a parameter declaration");
    } else if (duplicate != body.signals.end()) {
      error(
          name,
          "FSIM-SV-SEM-006",
          "duplicate generated signal declaration '"
              + name.text + "'");
    } else {
      body.signals.push_back({
          name.text,
          type,
          PortDirection::Unknown,
          false,
          span_from(start, previous())});
      ++current_generate_names_[name.text];
      local_names.push_back(name.text);
    }
    if (!match(TokenKind::Comma)) {
      break;
    }
  }
  expect(
      TokenKind::Semicolon,
      "';' after generated declaration",
      "FSIM-SV-PARSE-008");
}

void VerilogParser::parse_generated_parameter_group(
  GenerateBody& body,
  std::vector<std::string>& local_names,
  const bool local,
  const Token& start) {
  const auto type = parse_parameter_type();
  for (;;) {
    const auto name = expect_identifier(
        local
            ? "generated localparam name"
            : "generated parameter name");
    Expression value;
    if (match(TokenKind::Assign)) {
      value = parse_expression();
    } else {
      error(
          current(),
          "FSIM-SV-PARSE-050",
          "value parameters require a default constant expression");
    }
    const bool signal_conflict =
        std::any_of(
            body.signals.begin(),
            body.signals.end(),
            [&](const SignalDeclaration& signal) {
              return signal.name == name.text;
            });
    const bool duplicate =
        std::any_of(
            body.constants.begin(),
            body.constants.end(),
            [&](const ParameterDeclaration& parameter) {
              return parameter.name == name.text;
            });
    if (signal_conflict) {
      error(
          name,
          "FSIM-SV-SEM-020",
          "generated parameter '" + name.text
              + "' conflicts with a signal declaration");
    } else if (duplicate) {
      error(
          name,
          "FSIM-SV-SEM-017",
          "duplicate generated parameter declaration '"
              + name.text + "'");
    } else {
      body.constants.push_back(ParameterDeclaration{
          name.text,
          type,
          std::move(value),
          true,
          cover(start.span, previous().span),
          ParameterKind::Value,
          std::nullopt});
      ++current_generate_names_[name.text];
      local_names.push_back(name.text);
    }
    if (!match(TokenKind::Comma)) {
      break;
    }
  }
  expect(
      TokenKind::Semicolon,
      "';' after generated parameter declaration",
      "FSIM-SV-PARSE-051");
}

Type VerilogParser::parse_parameter_type() {
  Type type{
      ValueDomain::Integer,
      "implicit",
      std::nullopt,
      true};
  if (keyword("string")) {
    (void)advance();
    type.spelling = "string";
    type.domain = ValueDomain::String;
    type.is_signed = false;
    return type;
  }
  if (keyword("byte") || keyword("shortint")
      || keyword("longint") || keyword("time")) {
    const auto token = advance();
    type.spelling = token.text;
    type.domain =
        token.text == "time"
            ? ValueDomain::Logic4
            : ValueDomain::Bit2;
    type.is_signed = token.text != "time";
    const auto width =
        token.text == "byte"
            ? std::int64_t{8}
        : token.text == "shortint"
            ? std::int64_t{16}
            : std::int64_t{64};
    type.packed_range = PackedRange{
        width - 1, 0, true};
  } else if (keyword("integer") || keyword("int")) {
    const auto token = advance();
    type.spelling = token.text;
    type.domain = ValueDomain::Integer;
    type.is_signed = true;
  } else if (
      keyword("logic") || keyword("reg") || keyword("bit")) {
    const auto token = advance();
    type.spelling = token.text;
    type.domain =
        token.text == "bit"
            ? ValueDomain::Bit2
            : ValueDomain::Logic4;
    type.is_signed = false;
  } else if (
      keyword("signed") || keyword("unsigned")
      || at(TokenKind::LeftBracket)) {
    type.spelling = "logic";
    type.domain = ValueDomain::Logic4;
    type.is_signed = false;
  } else if (is_named_type_reference_start()) {
    return parse_named_type();
  }
  parse_optional_signedness(type);
  if (type.spelling == "implicit"
      || type.spelling == "logic"
      || type.spelling == "reg"
      || type.spelling == "bit") {
    parse_optional_range(type);
  }
  return type;
}

Type VerilogParser::parse_type_parameter_actual() {
  if (at(TokenKind::Identifier)
      && !keyword_reserved(keyword_set_, current().text)) {
    return parse_named_type();
  }
  if (keyword("string")) {
    const auto unsupported = advance();
    error(
        unsupported,
        "FSIM-SV-UNSUPPORTED-020",
        "the string data type is not implemented as a bounded type "
        "parameter actual");
    return {};
  }
  if (keyword("byte") || keyword("shortint")
      || keyword("longint") || keyword("time")
      || keyword("integer") || keyword("int")
      || keyword("logic") || keyword("reg") || keyword("bit")
      || keyword("signed") || keyword("unsigned")
      || at(TokenKind::LeftBracket)) {
    return parse_parameter_type();
  }
  const auto unsupported = advance();
  error(
      unsupported,
      "FSIM-SV-UNSUPPORTED-019",
      "bounded type parameters require an integral built-in or visible "
      "named packed type");
  return {};
}

void VerilogParser::add_parameter(
  DesignUnit& unit,
  ParameterDeclaration parameter,
  const Token& name) {
  if (parameter.kind == ParameterKind::Type
      && std::ranges::any_of(
          unit.type_aliases,
          [&](const TypeAliasDeclaration& alias) {
              return alias.name == parameter.name;
          })) {
    error(
        name,
        "FSIM-SV-SEM-055",
        "type parameter '" + parameter.name
            + "' conflicts with a typedef declaration");
    return;
  }
  if (declared_genvars_.contains(parameter.name)) {
    error(
        name,
        "FSIM-SV-SEM-022",
        "parameter '" + parameter.name
            + "' conflicts with a genvar declaration");
    return;
  }
  const auto object_conflict =
      std::any_of(
          unit.ports.begin(),
          unit.ports.end(),
          [&](const SignalDeclaration& declaration) {
            return declaration.name == parameter.name;
          })
      || std::any_of(
          unit.signals.begin(),
          unit.signals.end(),
          [&](const SignalDeclaration& declaration) {
            return declaration.name == parameter.name;
          });
  if (object_conflict) {
    error(
        name,
        "FSIM-SV-SEM-020",
        "parameter '" + parameter.name
            + "' conflicts with a port or signal declaration");
    return;
  }
  if (std::any_of(
          unit.parameters.begin(),
          unit.parameters.end(),
          [&](const ParameterDeclaration& existing) {
            return existing.name == parameter.name;
          })) {
    error(
        name,
        "FSIM-SV-SEM-017",
        "duplicate parameter declaration '" + parameter.name + "'");
    return;
  }
  unit.parameters.push_back(std::move(parameter));
}

void VerilogParser::parse_parameter_group(
  DesignUnit& unit,
  const bool local,
  const bool port_list,
  const Token& start) {
  const bool type_parameter = match_keyword("type");
  const auto type =
      type_parameter ? Type{} : parse_parameter_type();
  for (;;) {
    const auto name = expect_identifier(
        local ? "localparam name" : "parameter name");
    Expression value;
    std::optional<Type> default_type;
    if (match(TokenKind::Assign)) {
      if (type_parameter) {
        default_type = parse_type_parameter_actual();
      } else {
        value = parse_expression();
      }
    } else if (!type_parameter) {
      error(
          current(),
          "FSIM-SV-PARSE-050",
          "value parameters require a default constant expression");
    }
    add_parameter(
        unit,
        ParameterDeclaration{
            name.text,
            type,
            std::move(value),
            local,
            cover(start.span, previous().span),
            type_parameter
                ? ParameterKind::Type
                : ParameterKind::Value,
            std::move(default_type)},
        name);
    if (!match(TokenKind::Comma)) {
      break;
    }
    if (port_list
        && (keyword("parameter") || keyword("localparam"))) {
      break;
    }
  }
  if (!port_list) {
    expect(
        TokenKind::Semicolon,
        "';' after parameter declaration",
        "FSIM-SV-PARSE-051");
  }
}

void VerilogParser::parse_parameter_port_list(
  DesignUnit& unit,
  const Token& hash) {
  expect(
      TokenKind::LeftParen,
      "'(' after module parameter '#'",
      "FSIM-SV-PARSE-052");
  while (!at_end() && !at(TokenKind::RightParen)) {
    if (match_keyword("parameter")) {
      parse_parameter_group(unit, false, true, previous());
    } else if (match_keyword("localparam")) {
      parse_parameter_group(unit, true, true, previous());
    } else {
      error(
          current(),
          "FSIM-SV-PARSE-053",
          "a module parameter port list item must begin with parameter "
          "or localparam");
      while (!at_end() && !at(TokenKind::Comma)
             && !at(TokenKind::RightParen)) {
        advance();
      }
      (void)match(TokenKind::Comma);
    }
  }
  expect(
      TokenKind::RightParen,
      "')' after module parameter port list",
      "FSIM-SV-PARSE-054");
  (void)hash;
}

Instance VerilogParser::parse_instance() {
  const auto start = expect_identifier("instantiated module name");
  Instance instance;
  instance.unit_name = start.text;
  if (match(TokenKind::Hash)) {
    parse_parameter_overrides(instance, previous());
  }
  const auto name = expect_identifier("instance name");
  instance.name = name.text;
  instance.unconnected_drive = current_unconnected_drive_;
  expect(
      TokenKind::LeftParen, "'(' after instance name",
      "FSIM-SV-PARSE-034");
  while (!at_end() && !at(TokenKind::RightParen)) {
    PortConnection connection;
    const auto connection_start = current();
    if (match(TokenKind::Dot)) {
      const auto port = expect_identifier("port name");
      connection.port = port.text;
      expect(
          TokenKind::LeftParen, "'(' after named port",
          "FSIM-SV-PARSE-035");
      connection.value = parse_expression();
      expect(
          TokenKind::RightParen, "')' after named port connection",
          "FSIM-SV-PARSE-036");
    } else {
      connection.value = parse_expression();
    }
    connection.span = cover(connection_start.span, previous().span);
    instance.connections.push_back(std::move(connection));
    if (!match(TokenKind::Comma)) {
      break;
    }
  }
  expect(
      TokenKind::RightParen, "')' after instance connections",
      "FSIM-SV-PARSE-037");
  expect(
      TokenKind::Semicolon, "';' after module instance",
      "FSIM-SV-PARSE-038");
  instance.span = span_from(start, previous());
  return instance;
}

}  // namespace fsim::frontend
