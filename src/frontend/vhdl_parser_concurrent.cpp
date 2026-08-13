// SPDX-License-Identifier: Apache-2.0
#include "vhdl_parser_internal.hpp"

#include <unordered_map>

namespace fsim::frontend {

GenerateRegion VhdlParser::parse_vhdl_conditional_generate(
  const Token& label,
  const Token& start) {
  GenerateRegion result;
  result.then_scope = vhdl_name(label.text);
  result.else_scope = result.then_scope;
  result.condition = parse_expression();
  expect_keyword("generate", true, "FSIM-VHDL-PARSE-056");
  const bool then_declarations = parse_vhdl_generate_declarations(
      result.then_body,
      VhdlComponentDeclarationRegion::Generate);
  if (then_declarations) {
    expect_keyword("begin", true, "FSIM-VHDL-PARSE-234");
  } else {
    (void)match_keyword("begin", true);
  }
  parse_vhdl_generate_branch(result.then_body);
  if (match_keyword("else", true)) {
      (void)require_vhdl_standard(
          previous(),
          VhdlStandard::Vhdl2008,
          "if-generate alternatives",
          "use separate VHDL-93 if-generate statements");
      expect_keyword("generate", true, "FSIM-VHDL-PARSE-057");
      const bool else_declarations = parse_vhdl_generate_declarations(
          result.else_body,
          VhdlComponentDeclarationRegion::Generate);
      if (else_declarations) {
          expect_keyword("begin", true, "FSIM-VHDL-PARSE-234");
      } else {
          (void)match_keyword("begin", true);
      }
    parse_vhdl_generate_branch(result.else_body);
  }
  expect_keyword("end", true, "FSIM-VHDL-PARSE-058");
  expect_keyword("generate", true, "FSIM-VHDL-PARSE-059");
  if (at(TokenKind::Identifier)) {
    const auto end_label = advance();
    if (vhdl_name(end_label.text) != result.then_scope) {
      error(
          end_label,
          "FSIM-VHDL-PARSE-060",
          "generate end label does not match '"
              + result.then_scope + "'");
    }
  }
  expect(
      TokenKind::Semicolon,
      "';' after conditional generate",
      "FSIM-VHDL-PARSE-061");
  result.span = span_from(start, previous());
  return result;
}

GenerateRegion VhdlParser::parse_vhdl_iterative_generate(
  const Token& label,
  const Token& start) {
  GenerateRegion result;
  result.kind = GenerateKind::Iterative;
  result.then_scope = vhdl_name(label.text);
  const auto variable = expect_identifier("generate loop variable");
  result.variable = vhdl_name(variable.text);
  expect_keyword("in", true, "FSIM-VHDL-PARSE-062");
  result.initial = parse_expression();
  bool descending = false;
  if (match_keyword("to", true)) {
    descending = false;
  } else if (match_keyword("downto", true)) {
    descending = true;
  } else {
    error(
        current(),
        "FSIM-VHDL-PARSE-063",
        "expected 'to' or 'downto' in generate iteration range");
  }
  auto limit = parse_expression();
  expect_keyword("generate", true, "FSIM-VHDL-PARSE-064");
  const auto expression_span = span_from(variable, previous());
  Expression loop_variable{
      ExpressionKind::Identifier,
      result.variable,
      {},
      variable.span};
  result.condition = {
      ExpressionKind::Binary,
      descending ? ">=" : "<=",
      {loop_variable, std::move(limit)},
      expression_span};
  result.iteration = {
      ExpressionKind::Binary,
      descending ? "-" : "+",
      {
          std::move(loop_variable),
          Expression{
              ExpressionKind::IntegerLiteral,
              "1",
              {},
              expression_span}},
      expression_span};
  const bool has_declarations = parse_vhdl_generate_declarations(
      result.then_body,
      VhdlComponentDeclarationRegion::Generate);
  if (has_declarations) {
    expect_keyword("begin", true, "FSIM-VHDL-PARSE-234");
  } else {
    (void)match_keyword("begin", true);
  }
  parse_vhdl_generate_branch(result.then_body);
  expect_keyword("end", true, "FSIM-VHDL-PARSE-065");
  expect_keyword("generate", true, "FSIM-VHDL-PARSE-066");
  if (at(TokenKind::Identifier)) {
    const auto end_label = advance();
    if (vhdl_name(end_label.text) != result.then_scope) {
      error(
          end_label,
          "FSIM-VHDL-PARSE-067",
          "generate end label does not match '"
              + result.then_scope + "'");
    }
  }
  expect(
      TokenKind::Semicolon,
      "';' after iterative generate",
      "FSIM-VHDL-PARSE-068");
  result.span = span_from(start, previous());
  return result;
}

GenerateRegion VhdlParser::parse_vhdl_selection_generate(
  const Token& label,
  const Token& start) {
  GenerateRegion result;
  result.kind = GenerateKind::Selection;
  result.condition = parse_expression();
  expect_keyword("generate", true, "FSIM-VHDL-PARSE-069");
  bool saw_default = false;
  while (!at_end() && !keyword("end", 0, true)) {
    GenerateAlternative alternative;
    const auto alternative_start = current();
    if (saw_default) {
      error(
          current(),
          "FSIM-VHDL-SEM-018",
          "an others case-generate alternative must be last");
    }
    if (!(at(TokenKind::Identifier)
          && at(TokenKind::Colon, 1)
          && keyword("when", 2, true))) {
      error(
          current(),
          "FSIM-VHDL-PARSE-070",
          "a case-generate alternative must have a stable label");
    }
    const auto alternative_label =
        expect_identifier("case-generate alternative label");
    alternative.scope = vhdl_name(alternative_label.text);
    expect(
        TokenKind::Colon,
        "':' after case-generate alternative label",
        "FSIM-VHDL-PARSE-071");
    expect_keyword("when", true, "FSIM-VHDL-PARSE-071");
    if (match_keyword("others", true)) {
      alternative.is_default = true;
      if (saw_default) {
        error(
            previous(),
            "FSIM-VHDL-SEM-017",
            "case generate contains more than one others "
            "alternative");
      }
      saw_default = true;
    } else {
      do {
        GenerateChoice choice;
        choice.left = parse_expression();
        choice.span = choice.left.span;
        if (match_keyword("to", true)
            || match_keyword("downto", true)) {
          choice.descending =
              vhdl_name(previous().text) == "downto";
          choice.right = parse_expression();
          choice.span =
              cover(choice.left.span, choice.right->span);
        }
        alternative.choices.push_back(
            std::move(choice));
      } while (match(TokenKind::Pipe));
      if (alternative.choices.empty()) {
        error(
            current(),
            "FSIM-VHDL-PARSE-072",
            "case-generate alternative requires a choice");
      }
    }
    expect(
        TokenKind::Arrow,
        "'=>' after case-generate choices",
        "FSIM-VHDL-PARSE-073");
    const bool has_declarations = parse_vhdl_generate_declarations(
        alternative.body,
        VhdlComponentDeclarationRegion::Generate);
    if (has_declarations) {
      expect_keyword("begin", true, "FSIM-VHDL-PARSE-234");
    } else {
      (void)match_keyword("begin", true);
    }
    parse_vhdl_generate_branch(
        alternative.body,
        true);
    alternative.span =
        span_from(alternative_start, previous());
    result.alternatives.push_back(std::move(alternative));
  }
  expect_keyword("end", true, "FSIM-VHDL-PARSE-074");
  expect_keyword("generate", true, "FSIM-VHDL-PARSE-075");
  if (at(TokenKind::Identifier)) {
    const auto end_label = advance();
    if (vhdl_name(end_label.text) != vhdl_name(label.text)) {
      error(
          end_label,
          "FSIM-VHDL-PARSE-076",
          "generate end label does not match '"
              + vhdl_name(label.text) + "'");
    }
  }
  expect(
      TokenKind::Semicolon,
      "';' after case generate",
      "FSIM-VHDL-PARSE-077");
  result.span = span_from(start, previous());
  return result;
}

GenerateRegion VhdlParser::parse_vhdl_static_block(
  const Token& label,
  const Token& start) {
  GenerateRegion result;
  result.kind = GenerateKind::StaticBlock;
  result.then_scope = vhdl_name(label.text);
  if (match(TokenKind::LeftParen)) {
    result.condition = parse_expression();
    expect(
        TokenKind::RightParen,
        "')' after a VHDL block guard expression",
        "FSIM-VHDL-PARSE-231");
  }
  (void)match_keyword("is", true);
  DesignUnit interface;
  Instance associations;
  if (match_keyword("generic", true)) {
    parse_vhdl_generics(interface, previous());
    if (match_keyword("generic", true)) {
      parse_vhdl_generic_map(associations, previous());
      expect(
          TokenKind::Semicolon,
          "';' after a block generic map aspect",
          "FSIM-VHDL-PARSE-232");
    }
  }
  if (match_keyword("port", true)) {
    parse_vhdl_ports(interface);
    if (match_keyword("port", true)) {
      parse_vhdl_port_map(associations.connections, previous());
      expect(
          TokenKind::Semicolon,
          "';' after a block port map aspect",
          "FSIM-VHDL-PARSE-233");
    }
  }
  result.block_generics = std::move(interface.parameters);
  result.block_generic_map =
      std::move(associations.parameter_overrides);
  result.block_ports = std::move(interface.ports);
  result.block_port_map = std::move(associations.connections);
  (void)parse_vhdl_generate_declarations(
      result.then_body,
      VhdlComponentDeclarationRegion::Block);
  expect_keyword("begin", true, "FSIM-VHDL-PARSE-078");
  parse_vhdl_generate_branch(result.then_body);
  expect_keyword("end", true, "FSIM-VHDL-PARSE-079");
  expect_keyword("block", true, "FSIM-VHDL-PARSE-080");
  if (at(TokenKind::Identifier)) {
    const auto end_label = advance();
    if (vhdl_name(end_label.text) != result.then_scope) {
      error(
          end_label,
          "FSIM-VHDL-PARSE-081",
          "block end label does not match '"
              + result.then_scope + "'");
    }
  }
  expect(
      TokenKind::Semicolon,
      "';' after block statement",
      "FSIM-VHDL-PARSE-082");
  result.span = span_from(start, previous());
  return result;
}

bool VhdlParser::parse_vhdl_generate_declarations(
    GenerateBody& body,
    const VhdlComponentDeclarationRegion region) {
  struct GeneratedDeclarationName {
    std::string name;
    std::string family;
    SourceSpan span;
    bool callable{};
  };
  const auto validate_cross_family_names = [&]() {
    std::vector<GeneratedDeclarationName> names;
    const auto append = [&](const auto& declarations,
                            const std::string_view family,
                            const bool callable = false) {
      for (const auto& declaration : declarations) {
        names.push_back({
            declaration.name,
            std::string{family},
            declaration.span,
            callable});
      }
    };
    append(body.constants, "constant");
    append(body.type_aliases, "type");
    append(body.signals, "signal");
    append(body.signal_aliases, "signal alias");
    append(body.variables, "file");
    append(body.functions, "function", true);
    append(body.tasks, "task", true);
    append(body.procedures, "procedure", true);
    for (const auto& declaration :
         body.generic_function_templates) {
      names.push_back({
          declaration.function.name,
          "generic function",
          declaration.span,
          true});
    }
    for (const auto& declaration :
         body.generic_procedure_templates) {
      names.push_back({
          declaration.procedure.name,
          "generic procedure",
          declaration.span,
          true});
    }
    append(body.generic_function_instances, "function", true);
    append(body.generic_procedure_instances, "procedure", true);
    append(body.package_instances, "package");
    append(body.vhdl_component_declarations, "component");
    std::ranges::stable_sort(
        names, {}, [](const auto& declaration) {
          return declaration.span.begin.offset;
        });
    std::unordered_map<std::string, GeneratedDeclarationName> prior;
    for (const auto& declaration : names) {
      const auto found = prior.find(declaration.name);
      if (found == prior.end()) {
        prior.emplace(declaration.name, declaration);
        continue;
      }
      if (found->second.family == declaration.family
          || (found->second.callable && declaration.callable)) {
        continue;
      }
      error(
          Token{
              TokenKind::Identifier,
              declaration.name,
              declaration.span,
              {}},
          "FSIM-VHDL-SEM-081",
          "generated " + declaration.family + " declaration '"
              + declaration.name + "' conflicts with prior "
              + found->second.family + " declaration");
    }
  };
  bool parsed = false;
  for (;;) {
    if (match_keyword("signal", true)) {
      parsed = true;
      parse_signal_declaration(
          body.signals, &body.constants);
      continue;
    }
    if (match_keyword("constant", true)) {
      parsed = true;
      parse_vhdl_generate_constant(
          body, previous());
      continue;
    }
    if (match_keyword("file", true)) {
      parsed = true;
      parse_vhdl_file_declaration(
          body.variables, previous());
      continue;
    }
    if (match_keyword("type", true)) {
      parsed = true;
      DesignUnit declarations;
      declarations.type_aliases =
          std::move(body.type_aliases);
      parse_type_declaration(
          declarations, previous(), true);
      body.type_aliases =
          std::move(declarations.type_aliases);
      continue;
    }
    if (match_keyword("subtype", true)) {
      parsed = true;
      DesignUnit declarations;
      declarations.type_aliases =
          std::move(body.type_aliases);
      parse_subtype_declaration(
          declarations, previous(), true);
      body.type_aliases =
          std::move(declarations.type_aliases);
      continue;
    }
    if (match_keyword("alias", true)) {
      parsed = true;
      parse_vhdl_object_alias(
          body.signal_aliases, previous());
      continue;
    }
    if (keyword("pure", 0, true)
        || keyword("impure", 0, true)) {
      parsed = true;
      const auto pure = keyword("pure", 0, true);
      (void)advance();
      expect_keyword("function", true, "FSIM-VHDL-PARSE-235");
      body.functions.push_back(
          parse_vhdl_function(previous(), pure, true));
      continue;
    }
    if (match_keyword("generic", true)) {
      parsed = true;
      DesignUnit declarations;
      declarations.generic_function_templates =
          std::move(body.generic_function_templates);
      declarations.generic_procedure_templates =
          std::move(body.generic_procedure_templates);
      parse_vhdl_generic_subprogram(
          declarations, previous(), true);
      body.generic_function_templates =
          std::move(declarations.generic_function_templates);
      body.generic_procedure_templates =
          std::move(declarations.generic_procedure_templates);
      continue;
    }
    if (match_keyword("function", true)) {
      parsed = true;
      DesignUnit declarations;
      declarations.functions = std::move(body.functions);
      declarations.procedures = std::move(body.procedures);
      declarations.generic_function_instances =
          std::move(body.generic_function_instances);
      declarations.generic_procedure_instances =
          std::move(body.generic_procedure_instances);
      parse_vhdl_function_item(
          declarations, previous(), true, true);
      body.functions = std::move(declarations.functions);
      body.procedures = std::move(declarations.procedures);
      body.generic_function_instances =
          std::move(declarations.generic_function_instances);
      body.generic_procedure_instances =
          std::move(declarations.generic_procedure_instances);
      continue;
    }
    if (match_keyword("procedure", true)) {
      parsed = true;
      DesignUnit declarations;
      declarations.functions = std::move(body.functions);
      declarations.procedures = std::move(body.procedures);
      declarations.generic_function_instances =
          std::move(body.generic_function_instances);
      declarations.generic_procedure_instances =
          std::move(body.generic_procedure_instances);
      parse_vhdl_procedure_item(
          declarations, previous(), true);
      body.functions = std::move(declarations.functions);
      body.procedures = std::move(declarations.procedures);
      body.generic_function_instances =
          std::move(declarations.generic_function_instances);
      body.generic_procedure_instances =
          std::move(declarations.generic_procedure_instances);
      continue;
    }
    if (keyword("package", 0, true)
        && at(TokenKind::Identifier, 1)
        && keyword("is", 2, true)
        && keyword("new", 3, true)) {
      parsed = true;
      const auto package_start = advance();
      auto instance =
          parse_vhdl_package_instantiation(package_start);
      if (std::ranges::any_of(
              body.package_instances,
              [&](const auto& existing) {
                return existing.name == instance.name;
              })) {
        error(
            package_start,
            "FSIM-VHDL-SEM-059",
            "duplicate local package instance '"
                + instance.name + "'");
      } else {
        body.package_instances.push_back(
            std::move(instance));
      }
      continue;
    }
    if (match_keyword("component", true)) {
      parsed = true;
      const auto component_start = previous();
      auto declaration =
          parse_vhdl_component_declaration(
              component_start,
              body.vhdl_component_declarations.size());
      declaration.region = region;
      add_vhdl_component_declaration(
          body.vhdl_component_declarations,
          std::move(declaration),
          component_start);
      continue;
    }
    if (match_keyword("attribute", true)) {
      parsed = true;
      DesignUnit declarations;
      declarations.vhdl_attributes = std::move(body.vhdl_attributes);
      declarations.vhdl_groups = std::move(body.vhdl_groups);
      parse_vhdl_attribute_declaration(declarations, previous());
      body.vhdl_attributes = std::move(declarations.vhdl_attributes);
      body.vhdl_groups = std::move(declarations.vhdl_groups);
      continue;
    }
    if (match_keyword("group", true)) {
      parsed = true;
      DesignUnit declarations;
      declarations.vhdl_attributes = std::move(body.vhdl_attributes);
      declarations.vhdl_groups = std::move(body.vhdl_groups);
      parse_vhdl_group_declaration(declarations, previous());
      body.vhdl_attributes = std::move(declarations.vhdl_attributes);
      body.vhdl_groups = std::move(declarations.vhdl_groups);
      continue;
    }
    if (match_keyword("disconnect", true)) {
      parsed = true;
      parse_vhdl_disconnection_specification(
          body.vhdl_disconnections, previous());
      continue;
    }
    if (keyword("variable", 0, true)
        || keyword("shared", 0, true)
        || keyword("use", 0, true)
        || keyword("package", 0, true)) {
      parsed = true;
      const auto unsupported = advance();
      error(
          unsupported,
          "FSIM-VHDL-UNSUPPORTED-053",
          "unsupported generated declarative item '"
              + vhdl_name(unsupported.text) + "'");
      skip_to_semicolon();
      continue;
    }
    validate_cross_family_names();
    return parsed;
  }
}

void VhdlParser::parse_vhdl_generate_constant(
  GenerateBody& body, const Token& start) {
  std::vector<Token> names;
  names.push_back(expect_identifier("constant name"));
  while (match(TokenKind::Comma)) {
    names.push_back(expect_identifier("constant name"));
  }
  expect(
      TokenKind::Colon,
      "':' after constant names",
      "FSIM-VHDL-PARSE-083");
  const auto type = parse_vhdl_type(true);
  Expression value;
  if (match(TokenKind::ColonEqual)) {
    value = parse_expression();
  } else {
    error(
        current(),
        "FSIM-VHDL-PARSE-084",
        "a generated constant requires a default expression");
  }
  expect(
      TokenKind::Semicolon,
      "';' after constant declaration",
      "FSIM-VHDL-PARSE-085");
  for (const auto& name : names) {
    const auto canonical = vhdl_name(name.text);
    const bool duplicate =
        std::any_of(
            body.constants.begin(),
            body.constants.end(),
            [&](const ParameterDeclaration& constant) {
              return constant.name == canonical;
            })
        || std::any_of(
            body.signals.begin(),
            body.signals.end(),
            [&](const SignalDeclaration& signal) {
              return signal.name == canonical;
            });
    if (duplicate) {
      error(
          name,
          "FSIM-VHDL-SEM-019",
          "duplicate or conflicting generated constant declaration '"
              + canonical + "'");
      continue;
    }
    body.constants.push_back(ParameterDeclaration{
        canonical,
        type,
        value,
        true,
        span_from(start, previous()),
        ParameterKind::Value,
        std::nullopt});
  }
}

void VhdlParser::parse_vhdl_generate_branch(
    GenerateBody& body,
    const bool stop_at_case_alternative) {
  while (!at_end() && !keyword("else", 0, true)
         && !keyword("end", 0, true)
         && !(stop_at_case_alternative
              && at(TokenKind::Identifier)
              && at(TokenKind::Colon, 1)
              && keyword("when", 2, true))) {
    std::optional<Token> label;
    if (at(TokenKind::Identifier)
        && at(TokenKind::Colon, 1)) {
      label = advance();
      advance();
    }
    const auto postponed_token = match_keyword("postponed", true)
        ? std::optional<Token>{previous()} : std::nullopt;
    if (keyword("process", 0, true)) {
      body.processes.push_back(
          parse_process(label, postponed_token.has_value()));
      continue;
    }
    if (match_keyword("assert", true)) {
      auto statement = parse_vhdl_assertion(previous());
      statement.vhdl_postponed = postponed_token.has_value();
      if (postponed_token) {
        statement.span = cover(postponed_token->span, statement.span);
      }
      if (label) {
        statement.label = vhdl_name(label->text);
      }
      body.concurrent_statements.push_back(std::move(statement));
      continue;
    }
    if (postponed_token) {
      if (auto procedure = parse_vhdl_procedure_call()) {
        procedure->vhdl_postponed = true;
        procedure->span = cover(postponed_token->span, procedure->span);
        if (label) {
          procedure->label = vhdl_name(label->text);
          procedure->span = cover(label->span, procedure->span);
        }
        body.concurrent_statements.push_back(std::move(*procedure));
        continue;
      }
      error(
          *postponed_token,
          "FSIM-VHDL-SEM-104",
          "postponed is permitted only on a process, concurrent assertion, "
          "or concurrent procedure call");
      skip_to_semicolon();
      continue;
    }
    if (match_keyword("with", true)) {
      auto statement =
          parse_vhdl_selected_assignment(previous(), true);
      if (label) {
        statement.label = vhdl_name(label->text);
        statement.span = cover(label->span, statement.span);
      }
      body.concurrent_statements.push_back(std::move(statement));
      continue;
    }
    if (label && match_keyword("if", true)) {
      body.generate_regions.push_back(
          parse_vhdl_conditional_generate(
              *label, previous()));
      continue;
    }
    if (label && match_keyword("for", true)) {
      body.generate_regions.push_back(
          parse_vhdl_iterative_generate(
              *label, previous()));
      continue;
    }
    if (label && match_keyword("case", true)) {
      body.generate_regions.push_back(
          parse_vhdl_selection_generate(
              *label, previous()));
      continue;
    }
    if (label && match_keyword("block", true)) {
      body.generate_regions.push_back(
          parse_vhdl_static_block(
              *label, previous()));
      continue;
    }
    if (label && (
        keyword("entity", 0, true)
        || keyword("configuration", 0, true)
        || (at(TokenKind::Identifier)
            && (keyword("port", 1, true)
                || keyword("generic", 1, true))))) {
      body.instances.push_back(parse_vhdl_instance(*label));
      continue;
    }
    const auto before = position();
    auto assignment = parse_assignment(true);
    if (assignment) {
      if (label) {
        assignment->label = vhdl_name(label->text);
        assignment->span = cover(label->span, assignment->span);
      }
      body.concurrent_statements.push_back(
          std::move(*assignment));
      continue;
    }
    rewind(before);
    if (auto procedure = parse_vhdl_procedure_call()) {
      if (label) {
        procedure->label = vhdl_name(label->text);
        procedure->span = cover(label->span, procedure->span);
      }
      body.concurrent_statements.push_back(std::move(*procedure));
      continue;
    }
    const auto unsupported = advance();
    error(
        unsupported,
        "FSIM-VHDL-UNSUPPORTED-020",
        "unsupported concurrent item in generate branch");
    skip_to_semicolon();
  }
  apply_vhdl_disconnection_specifications(
      body.signals,
      body.vhdl_disconnections,
      body.concurrent_statements);
}

Instance VhdlParser::parse_vhdl_instance(const Token& label) {
  Instance instance;
  instance.name = vhdl_name(label.text);

  if (match_keyword("entity", true)) {
      (void)require_vhdl_standard(
          previous(),
          VhdlStandard::Vhdl1993,
          "direct entity instantiation",
          "declare and instantiate a component in VHDL-87");
      const auto first = expect_identifier("entity name");
      instance.unit_name = vhdl_name(first.text);
      if (match(TokenKind::Dot)) {
          const auto unit = expect_identifier("entity name after library");
          instance.unit_name += '.';
          instance.unit_name += vhdl_name(unit.text);
      }
    if (match(TokenKind::LeftParen)) {
      const auto architecture =
          expect_identifier("architecture name in entity aspect");
      instance.unit_name += '(';
      instance.unit_name += vhdl_name(architecture.text);
      instance.unit_name += ')';
      expect(TokenKind::RightParen, "')' after architecture name",
             "FSIM-VHDL-PARSE-036");
    }
  } else if (match_keyword("configuration", true)) {
      (void)require_vhdl_standard(
          previous(),
          VhdlStandard::Vhdl1993,
          "direct configuration instantiation",
          "declare and instantiate a component in VHDL-87");
      const auto first = expect_identifier("configuration name");
      instance.unit_name = vhdl_name(first.text);
      if (match(TokenKind::Dot)) {
          const auto unit = expect_identifier("configuration name after library");
          instance.unit_name += '.';
          instance.unit_name += vhdl_name(unit.text);
      }
    instance.vhdl_configuration_instance = true;
  } else {
    const auto component = expect_identifier("component name");
    instance.unit_name = vhdl_name(component.text);
    instance.vhdl_component_instance = true;
  }

  if (match_keyword("generic", true)) {
    parse_vhdl_generic_map(instance, previous());
  }

  if (!match_keyword("port", true)) {
    expect(TokenKind::Semicolon, "';' after VHDL instance",
           "FSIM-VHDL-PARSE-043");
    instance.span = span_from(label, previous());
    return instance;
  }
  parse_vhdl_port_map(instance.connections, previous());
  expect(TokenKind::Semicolon, "';' after VHDL instance",
         "FSIM-VHDL-PARSE-043");
  instance.span = span_from(label, previous());
  return instance;
}

void VhdlParser::parse_vhdl_port_map(
    std::vector<PortConnection>& connections,
    const Token& start) {
  expect_keyword("map", true, "FSIM-VHDL-PARSE-040");
  expect(
      TokenKind::LeftParen,
      "'(' after port map",
      "FSIM-VHDL-PARSE-041");
  bool saw_named_port = false;
  while (!at_end() && !at(TokenKind::RightParen)) {
    const auto actual_start = current();
    auto connection = parse_vhdl_port_connection();
    if (connection.port) {
      saw_named_port = true;
      if (std::ranges::any_of(
              connections,
              [&](const PortConnection& existing) {
                return existing.port == connection.port;
              })) {
        error(
            actual_start,
            "FSIM-VHDL-SEM-078",
            "duplicate named port actual '"
                + *connection.port + "'");
      }
    } else if (saw_named_port) {
      error(
          actual_start,
          "FSIM-VHDL-SEM-079",
          "a positional port actual cannot follow a named actual");
    }
    connections.push_back(std::move(connection));
    if (!match(TokenKind::Comma)) {
      break;
    }
  }
  expect(TokenKind::RightParen, "')' after port map",
         "FSIM-VHDL-PARSE-042");
  (void)start;
}

void VhdlParser::parse_vhdl_generic_map(
  Instance& instance,
  const Token& start) {
  expect_keyword("map", true, "FSIM-VHDL-PARSE-037");
  expect(
      TokenKind::LeftParen,
      "'(' after generic map",
      "FSIM-VHDL-PARSE-038");
  bool saw_named = false;
  const auto begins_unambiguous_subtype_indication = [&]() {
    if (!at(TokenKind::Identifier)) {
      return false;
    }
    std::size_t lookahead = 1;
    while (at(TokenKind::Dot, lookahead)
           && at(TokenKind::Identifier, lookahead + 1)) {
      lookahead += 2;
    }
    return keyword("range", lookahead, true);
  };
  while (!at_end() && !at(TokenKind::RightParen)) {
    const auto association_start = current();
    ParameterOverride actual;
    if (at(TokenKind::Identifier)
        && at(TokenKind::Arrow, 1)) {
      saw_named = true;
      const auto name = advance();
      advance();
      actual.name = vhdl_name(name.text);
      if (std::any_of(
              instance.parameter_overrides.begin(),
              instance.parameter_overrides.end(),
              [&](const ParameterOverride& existing) {
                return existing.name == actual.name;
              })) {
        error(
            name,
            "FSIM-VHDL-SEM-015",
            "duplicate named generic actual '"
                + *actual.name + "'");
      }
    } else if (saw_named) {
      error(
          current(),
          "FSIM-VHDL-SEM-016",
          "a positional generic actual cannot follow a named actual");
    }
    if (match_keyword("open", true)) {
      actual.default_box = true;
    } else if (match(TokenKind::Less)) {
      expect(
          TokenKind::Greater,
          "'>' in default generic association '<>'",
          "FSIM-VHDL-PARSE-230");
      actual.default_box = true;
    } else {
      if (begins_unambiguous_subtype_indication()) {
        actual.type_value = parse_vhdl_type(true, true);
      } else {
        actual.value = parse_expression();
      }
    }
    actual.span =
        cover(association_start.span, previous().span);
    instance.parameter_overrides.push_back(std::move(actual));
    if (!match(TokenKind::Comma)) {
      break;
    }
  }
  expect(
      TokenKind::RightParen,
      "')' after generic map",
      "FSIM-VHDL-PARSE-055");
  (void)start;
}

PortConnection VhdlParser::parse_vhdl_port_connection() {
  const auto start = current();
  PortConnection connection;
  if (at(TokenKind::Identifier) && at(TokenKind::Arrow, 1)) {
    connection.port = vhdl_name(advance().text);
    advance();
  }

  if (match_keyword("open", true)) {
    connection.kind = PortActualKind::Open;
    connection.span = cover(start.span, previous().span);
    return connection;
  }

  connection.value = parse_expression();
  connection.span = cover(start.span, previous().span);
  return connection;
}

void VhdlParser::skip_vhdl_connection_actual() {
  std::size_t parenthesis_depth = 0;
  std::size_t bracket_depth = 0;
  std::size_t brace_depth = 0;
  while (!at_end()) {
    if (at(TokenKind::Comma) && parenthesis_depth == 0 &&
        bracket_depth == 0 && brace_depth == 0) {
      return;
    }
    if (at(TokenKind::RightParen) && parenthesis_depth == 0 &&
        bracket_depth == 0 && brace_depth == 0) {
      return;
    }
    if (match(TokenKind::LeftParen)) {
      ++parenthesis_depth;
    } else if (at(TokenKind::RightParen)) {
      advance();
      if (parenthesis_depth != 0) {
        --parenthesis_depth;
      }
    } else if (match(TokenKind::LeftBracket)) {
      ++bracket_depth;
    } else if (at(TokenKind::RightBracket)) {
      advance();
      if (bracket_depth != 0) {
        --bracket_depth;
      }
    } else if (match(TokenKind::LeftBrace)) {
      ++brace_depth;
    } else if (at(TokenKind::RightBrace)) {
      advance();
      if (brace_depth != 0) {
        --brace_depth;
      }
    } else {
      advance();
    }
  }
}

Process VhdlParser::parse_process(
    std::optional<Token> label,
    const bool postponed) {
  const auto start =
      expect_keyword("process", true, "FSIM-VHDL-PARSE-019");
  Process process;
  process.kind = ProcessKind::VhdlProcess;
  process.vhdl_postponed = postponed;
  process.name = label ? vhdl_name(label->text) : std::string{};

  if (match(TokenKind::LeftParen)) {
    while (!at_end() && !at(TokenKind::RightParen)) {
      auto expression = parse_expression();
      const bool identifier =
          expression.kind == ExpressionKind::Identifier;
      const bool wildcard = identifier
          && detail::iequals(expression.text, "all");
      if (wildcard) {
          (void)require_vhdl_standard(
              previous(),
              VhdlStandard::Vhdl2008,
              "process(all) sensitivity lists",
              "list every sensitivity signal explicitly");
      }
      process.sensitivities.push_back(Sensitivity{
          EdgeKind::Any,
          identifier
              ? (wildcard ? "*" : vhdl_name(expression.text))
              : std::string{},
          expression.span,
          identifier ? Expression{} : std::move(expression)});
      if (!match(TokenKind::Comma)) {
        break;
      }
    }
    if (process.sensitivities.size() > 1
        && std::ranges::any_of(
            process.sensitivities,
            [](const Sensitivity& sensitivity) {
              return sensitivity.signal == "*";
            })) {
      error(
          start,
          "FSIM-VHDL-SEM-085",
          "process(all) cannot combine all with another sensitivity "
          "name");
    }
    expect(TokenKind::RightParen, "')' after sensitivity list",
           "FSIM-VHDL-PARSE-020");
  }
  match_keyword("is", true);
  while (!at_end() && !keyword("begin", 0, true)) {
    if (match_keyword("file", true)) {
      parse_vhdl_file_declaration(
          process.variables, previous());
      continue;
    }
    if (parse_vhdl_local_nonobject_declaration(
            process.constants,
            process.type_aliases,
            process.signal_aliases,
            process.variables,
            process.package_instances,
            process.functions,
            process.procedures,
            process.vhdl_attributes,
            process.vhdl_groups)) {
      continue;
    }
    if (!match_keyword("variable", true)) {
      const auto declaration = current();
      error(
          declaration,
          "FSIM-VHDL-UNSUPPORTED-007",
          "unsupported process declarative item");
      while (!at_end() && !keyword("begin", 0, true)
             && !at(TokenKind::Semicolon)) {
        advance();
      }
      match(TokenKind::Semicolon);
      continue;
    }
    std::vector<Token> names;
    names.push_back(expect_identifier("variable name"));
    while (match(TokenKind::Comma)) {
      names.push_back(expect_identifier("variable name"));
    }
    expect(
        TokenKind::Colon,
        "':' after variable names",
        "FSIM-VHDL-PARSE-047");
    const auto type = parse_vhdl_type(true, true);
    std::optional<Expression> initializer;
    if (match(TokenKind::ColonEqual)) {
      initializer = parse_expression();
    }
    expect(
        TokenKind::Semicolon,
        "';' after variable declaration",
        "FSIM-VHDL-PARSE-048");
    for (const auto& name : names) {
      process.variables.push_back(VariableDeclaration{
          vhdl_name(name.text),
          type,
          initializer,
          span_from(name, previous())});
    }
  }
  validate_vhdl_local_declaration_names(
      process.constants,
      process.type_aliases,
      process.signal_aliases,
      process.variables,
      process.package_instances,
      process.functions,
      process.procedures);
  expect_keyword("begin", true, "FSIM-VHDL-PARSE-021");
  sequential_loop_labels_seen_.clear();
  process.statements = parse_statement_list({"end"});
  sequential_loop_labels_seen_.clear();
  const auto contains_explicit_wait =
      [&](const auto& self,
          const std::vector<Statement>& statements) -> bool {
        for (const auto& statement : statements) {
          if (statement.kind == StatementKind::Delay
              || statement.kind == StatementKind::WaitOn
              || statement.kind == StatementKind::WaitUntil
              || self(self, statement.statements)
              || self(self, statement.else_statements)) {
            return true;
          }
        }
        return false;
      };
  if (!process.sensitivities.empty()
      && contains_explicit_wait(
          contains_explicit_wait, process.statements)) {
    error(
        start,
        "FSIM-VHDL-SEM-012",
        "a process sensitivity list cannot be combined with an explicit "
        "wait statement");
  }
  expect_keyword("end", true, "FSIM-VHDL-PARSE-022");
  const bool closing_postponed = match_keyword("postponed", true);
  if (closing_postponed && !postponed) {
    error(
        previous(),
        "FSIM-VHDL-SEM-103",
        "an ordinary process cannot use postponed in its closing clause");
  }
  match_keyword("process", true);
  parse_statement_end_label(process.name, "process");
  expect(TokenKind::Semicolon, "';' after process",
         "FSIM-VHDL-PARSE-023");
  process.span = label ? span_from(*label, previous())
                       : span_from(start, previous());
  infer_process_edge(process);
  return process;
}

}  // namespace fsim::frontend
