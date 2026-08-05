// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

#include <functional>

#include <algorithm>
#include <iterator>
#include <utility>

namespace fsim::frontend {

namespace {

[[nodiscard]] std::string class_identity(
    const std::string_view enclosing_scope,
    const std::string_view name) {
  std::string result{enclosing_scope};
  if (!result.empty()) {
    result += "::";
  }
  result += name;
  return result;
}

}  // namespace

void VerilogParser::add_class_declaration(
    std::vector<SystemVerilogClassDeclaration>& declarations,
    SystemVerilogClassDeclaration declaration,
    const Token& location) {
  const auto existing = std::ranges::find(
      declarations, declaration.name,
      &SystemVerilogClassDeclaration::name);
  if (existing == declarations.end()) {
    declarations.push_back(std::move(declaration));
    return;
  }
  if (existing->is_forward_declaration
      && !declaration.is_forward_declaration) {
    declaration.span = cover(existing->span, declaration.span);
    *existing = std::move(declaration);
    return;
  }
  if (!existing->is_forward_declaration
      && declaration.is_forward_declaration) {
    return;
  }
  if (existing->is_forward_declaration) {
    return;
  }
  error(
      location,
      "FSIM-SV-SEM-167",
      "duplicate SystemVerilog class declaration '"
          + declaration.name + "'");
}

SystemVerilogClassDeclaration
VerilogParser::parse_class_forward_declaration(
    const Token& start,
    std::string enclosing_scope) {
  SystemVerilogClassDeclaration declaration;
  declaration.enclosing_scope = std::move(enclosing_scope);
  declaration.is_forward_declaration = true;
  expect_keyword("class", false, "FSIM-SV-PARSE-256");
  const auto name = expect_identifier("forward-declared class name");
  declaration.name = name.text;
  declaration.canonical_identity = class_identity(
      declaration.enclosing_scope, declaration.name);
  expect(
      TokenKind::Semicolon,
      "';' after forward class declaration",
      "FSIM-SV-PARSE-257");
  declaration.span = span_from(start, previous());
  return declaration;
}

bool VerilogParser::parse_class_property(
    SystemVerilogClassDeclaration& declaration,
    const Token& start) {
  auto visibility = SystemVerilogClassVisibility::Public;
  bool is_static = false;
  bool is_const = false;
  bool is_rand = false;
  bool is_randc = false;
  bool saw_qualifier = false;
  for (;;) {
    if (match_keyword("local")) {
      visibility = SystemVerilogClassVisibility::Local;
      saw_qualifier = true;
    } else if (match_keyword("protected")) {
      visibility = SystemVerilogClassVisibility::Protected;
      saw_qualifier = true;
    } else if (match_keyword("static")) {
      is_static = true;
      saw_qualifier = true;
    } else if (match_keyword("const")) {
      is_const = true;
      saw_qualifier = true;
    } else if (match_keyword("rand")) {
      is_rand = true;
      saw_qualifier = true;
    } else if (match_keyword("randc")) {
      is_randc = true;
      saw_qualifier = true;
    } else {
      break;
    }
  }
  (void)match_keyword("var");
  if (is_rand && is_randc) {
    error(
        start,
        "FSIM-SV-SEM-169",
        "a class property cannot be both rand and randc");
  }

  const bool built_in =
      keyword("string") || keyword("byte") || keyword("shortint")
      || keyword("longint") || keyword("time") || keyword("integer")
      || keyword("int") || keyword("logic") || keyword("reg")
      || keyword("bit") || keyword("signed") || keyword("unsigned")
      || at(TokenKind::LeftBracket);
  if (!built_in && !is_named_type_reference_start()) {
    if (saw_qualifier) {
      error(
          current(),
          "FSIM-SV-PARSE-260",
          "expected a data type after class property qualifiers");
    }
    return false;
  }

  Type common_type = built_in
      ? parse_parameter_type()
      : parse_named_type();
  for (;;) {
    const auto name = expect_identifier("class property name");
    auto type = common_type;
    (void)parse_optional_container_dimension(type);
    std::optional<Expression> initializer;
    if (match(TokenKind::Assign)) {
      initializer = parse_expression();
    }
    const bool duplicate = std::ranges::any_of(
        declaration.properties,
        [&](const SystemVerilogClassProperty& property) {
          return property.declaration.name == name.text;
        });
    if (duplicate) {
      error(
          name,
          "FSIM-SV-SEM-170",
          "duplicate class property declaration '" + name.text + "'");
    } else {
      const auto span = cover(start.span, previous().span);
      SystemVerilogClassProperty property;
      property.declaration = VariableDeclaration{
          name.text,
          std::move(type),
          std::move(initializer),
          span};
      property.visibility = visibility;
      property.is_static = is_static;
      property.is_const = is_const;
      property.is_rand = is_rand;
      property.is_randc = is_randc;
      property.span = span;
      declaration.properties.push_back(std::move(property));
    }
    if (!match(TokenKind::Comma)) {
      break;
    }
  }
  expect(
      TokenKind::Semicolon,
      "';' after class property declaration",
      "FSIM-SV-PARSE-261");
  return true;
}

SystemVerilogClassMethod VerilogParser::parse_class_method(
    const Token& start,
    const SystemVerilogClassMethodKind kind,
    const SystemVerilogClassVisibility visibility,
    const bool is_static,
    const bool is_virtual,
    const bool is_pure,
    const bool is_final,
    const bool is_extern,
    const std::string_view owner_identity) {
  SystemVerilogClassMethod method;
  method.kind = kind;
  method.visibility = visibility;
  method.is_static = is_static;
  method.is_virtual = is_virtual || is_pure;
  method.is_pure = is_pure;
  method.is_final = is_final;
  method.is_extern = is_extern;
  const bool prototype = is_extern || is_pure;
  if (kind == SystemVerilogClassMethodKind::Task) {
    auto task = parse_task(start, prototype, true);
    method.name = std::move(task.name);
    method.variables = std::move(task.variables);
    method.statements = std::move(task.statements);
    method.lifetime = task.lifetime_explicit
        ? task.automatic
            ? SystemVerilogClassLifetime::Automatic
            : SystemVerilogClassLifetime::Static
        : SystemVerilogClassLifetime::Inherited;
    for (auto& argument : task.arguments) {
      FunctionArgument retained{
          std::move(argument.name),
          std::move(argument.type),
          argument.direction,
          std::move(argument.span),
          argument.reference,
          std::move(argument.default_value)};
      method.arguments.push_back(std::move(retained));
    }
    method.span = std::move(task.span);
  } else {
    auto function = parse_function(start, prototype, true);
    method.name = std::move(function.name);
    method.kind = method.name == "new"
        ? SystemVerilogClassMethodKind::Constructor
        : SystemVerilogClassMethodKind::Function;
    method.return_type = std::move(function.return_type);
    method.arguments = std::move(function.arguments);
    method.variables = std::move(function.variables);
    method.statements = std::move(function.statements);
    method.lifetime = function.lifetime_explicit
        ? function.automatic
            ? SystemVerilogClassLifetime::Automatic
            : SystemVerilogClassLifetime::Static
        : SystemVerilogClassLifetime::Inherited;
    method.span = std::move(function.span);
  }
  method.defined = !prototype;
  method.canonical_identity = std::string{owner_identity};
  if (!method.canonical_identity.empty()) {
    method.canonical_identity += "::";
  }
  method.canonical_identity += method.name;
  if (is_pure && !is_virtual) {
    error(
        start,
        "FSIM-SV-SEM-171",
        "a pure class method must also be virtual");
  }
  return method;
}

SystemVerilogClassMethod VerilogParser::parse_class_out_of_block_method(
    const Token& start,
    const SystemVerilogClassMethodKind kind) {
  auto method = parse_class_method(
      start,
      kind,
      SystemVerilogClassVisibility::Public,
      false,
      false,
      false,
      false,
      false,
      {});
  method.out_of_block_definition = true;
  method.canonical_identity = method.name;
  if (method.name.find("::") == std::string::npos) {
    error(
        start,
        "FSIM-SV-SEM-172",
        "an out-of-block class method definition requires a "
        "class-qualified name");
  }
  return method;
}

SystemVerilogClassConstraint VerilogParser::parse_class_constraint(
    const Token& start,
    const std::string_view owner_identity,
    const SystemVerilogClassVisibility visibility,
    const bool is_static,
    const bool is_pure,
    const bool is_extern) {
  SystemVerilogClassConstraint constraint;
  constraint.visibility = visibility;
  constraint.is_static = is_static;
  constraint.is_pure = is_pure;
  constraint.is_extern = is_extern;
  const auto name = expect_identifier("class constraint name");
  constraint.name = name.text;
  constraint.canonical_identity = std::string{owner_identity};
  if (!constraint.canonical_identity.empty()) {
    constraint.canonical_identity += "::";
  }
  constraint.canonical_identity += constraint.name;
  if (is_extern || is_pure) {
    expect(
        TokenKind::Semicolon,
        "';' after class constraint prototype",
        "FSIM-SV-PARSE-263");
    constraint.defined = false;
    constraint.span = span_from(start, previous());
    return constraint;
  }
  expect(
      TokenKind::LeftBrace,
      "'{' before class constraint block",
      "FSIM-SV-PARSE-264");
  std::function<Expression()> parse_constraint_item;
  std::function<Expression()> parse_constraint_set;
  const auto parse_plain_constraint = [&]() {
    const auto soft = keyword("soft");
    std::optional<Token> soft_token;
    if (soft) soft_token = advance();
    auto expression = parse_expression();
    if (keyword("dist")) {
      const auto dist = advance();
      expect(
          TokenKind::LeftBrace,
          "'{' after dist",
          "FSIM-SV-PARSE-268");
      std::vector<Expression> operands;
      operands.push_back(std::move(expression));
      while (!at_end() && !at(TokenKind::RightBrace)) {
        Expression choice;
        if (match(TokenKind::LeftBracket)) {
          const auto range_start = previous();
          auto low = parse_expression();
          expect(
              TokenKind::Colon,
              "':' in dist range",
              "FSIM-SV-PARSE-269");
          auto high = parse_expression();
          expect(
              TokenKind::RightBracket,
              "']' after dist range",
              "FSIM-SV-PARSE-270");
          choice = Expression{
              ExpressionKind::Call,
              "@inside-range",
              {std::move(low), std::move(high)},
              cover(range_start.span, previous().span)};
        } else {
          choice = parse_expression();
        }
        std::string weight_kind;
        if (match(TokenKind::ColonEqual)) {
          weight_kind = "@dist-:=";
        } else if (match(TokenKind::Colon)) {
          expect(
              TokenKind::Slash,
              "'/' after ':' in dist weight",
              "FSIM-SV-PARSE-271");
          weight_kind = "@dist-:/";
        } else {
          error(
              current(),
              "FSIM-SV-PARSE-272",
              "a dist item requires ':=' or ':/' weight syntax");
          weight_kind = "@dist-:=";
        }
        auto weight = parse_expression();
        const auto item_span = cover(dist.span, weight.span);
        operands.push_back(Expression{
            ExpressionKind::Call,
            std::move(weight_kind),
            {std::move(choice), std::move(weight)},
            item_span});
        if (!match(TokenKind::Comma)) break;
      }
      expect(
          TokenKind::RightBrace,
          "'}' after dist list",
          "FSIM-SV-PARSE-273");
      expression = Expression{
          ExpressionKind::Call,
          "dist",
          std::move(operands),
          cover(dist.span, previous().span)};
    }
    if (soft_token) {
      const auto soft_span = cover(soft_token->span, expression.span);
      expression = Expression{
          ExpressionKind::Call,
          "soft",
          {std::move(expression)},
          soft_span};
    }
    if (match(TokenKind::ThinArrow)) {
      auto consequent = parse_constraint_set();
      const auto implication_span = cover(expression.span, consequent.span);
      return Expression{
          ExpressionKind::Call,
          "@constraint-implies",
          {std::move(expression), std::move(consequent)},
          implication_span};
    }
    expect(
        TokenKind::Semicolon,
        "';' after class constraint expression",
        "FSIM-SV-PARSE-265");
    return expression;
  };
  parse_constraint_set = [&]() {
    if (!match(TokenKind::LeftBrace)) return parse_constraint_item();
    const auto block_start = previous();
    std::vector<Expression> items;
    while (!at_end() && !at(TokenKind::RightBrace)) {
      items.push_back(parse_constraint_item());
    }
    expect(
        TokenKind::RightBrace,
        "'}' after structured constraint set",
        "FSIM-SV-PARSE-274");
    return Expression{
        ExpressionKind::Call,
        "@constraint-block",
        std::move(items),
        cover(block_start.span, previous().span)};
  };
  parse_constraint_item = [&]() {
    if (match_keyword("solve")) {
      const auto solve_token = previous();
      std::vector<Expression> earlier;
      do {
        earlier.push_back(parse_expression());
      } while (match(TokenKind::Comma));
      if (!match_keyword("before")) {
        error(
            current(),
            "FSIM-SV-PARSE-279",
            "a solve-order constraint requires 'before'");
      }
      std::vector<Expression> later;
      do {
        later.push_back(parse_expression());
      } while (match(TokenKind::Comma));
      expect(
          TokenKind::Semicolon,
          "';' after solve-before constraint",
          "FSIM-SV-PARSE-280");
      Expression earlier_list{
          ExpressionKind::Call,
          "@solve-list",
          std::move(earlier),
          solve_token.span};
      Expression later_list{
          ExpressionKind::Call,
          "@solve-list",
          std::move(later),
          previous().span};
      return Expression{
          ExpressionKind::Call,
          "@solve-before",
          {std::move(earlier_list), std::move(later_list)},
          cover(solve_token.span, previous().span)};
    }
    if (match_keyword("if")) {
      const auto if_token = previous();
      expect(
          TokenKind::LeftParen,
          "'(' after constraint if",
          "FSIM-SV-PARSE-275");
      auto condition = parse_expression();
      expect(
          TokenKind::RightParen,
          "')' after constraint if condition",
          "FSIM-SV-PARSE-276");
      auto when_true = parse_constraint_set();
      std::vector<Expression> operands;
      operands.push_back(std::move(condition));
      operands.push_back(std::move(when_true));
      if (match_keyword("else")) {
        operands.push_back(parse_constraint_set());
      }
      const auto item_span = cover(if_token.span, operands.back().span);
      return Expression{
          ExpressionKind::Call,
          "@constraint-if",
          std::move(operands),
          item_span};
    }
    if (match_keyword("foreach")) {
      const auto foreach_token = previous();
      expect(
          TokenKind::LeftParen,
          "'(' after constraint foreach",
          "FSIM-SV-PARSE-277");
      auto selection = parse_expression();
      expect(
          TokenKind::RightParen,
          "')' after constraint foreach selection",
          "FSIM-SV-PARSE-278");
      auto body = parse_constraint_set();
      const auto item_span = cover(foreach_token.span, body.span);
      return Expression{
          ExpressionKind::Call,
          "@constraint-foreach",
          {std::move(selection), std::move(body)},
          item_span};
    }
    return parse_plain_constraint();
  };
  while (!at_end() && !at(TokenKind::RightBrace)) {
    constraint.expressions.push_back(parse_constraint_item());
  }
  expect(
      TokenKind::RightBrace,
      "'}' after class constraint block",
      "FSIM-SV-PARSE-266");
  constraint.span = span_from(start, previous());
  return constraint;
}

SystemVerilogClassDeclaration VerilogParser::parse_class(
    const Token& start,
    std::string enclosing_scope,
    const bool virtual_class,
    const bool interface_class) {
  SystemVerilogClassDeclaration declaration;
  declaration.enclosing_scope = std::move(enclosing_scope);
  declaration.is_virtual = virtual_class || interface_class;
  declaration.is_interface = interface_class;
  if (match_keyword("automatic")) {
    declaration.lifetime = SystemVerilogClassLifetime::Automatic;
  } else if (match_keyword("static")) {
    declaration.lifetime = SystemVerilogClassLifetime::Static;
  }

  const auto name = expect_identifier("class name");
  declaration.name = name.text;
  declaration.canonical_identity = class_identity(
      declaration.enclosing_scope, declaration.name);

  if (match(TokenKind::Hash)) {
    DesignUnit parameter_owner;
    parse_parameter_port_list(parameter_owner, previous());
    declaration.parameters = std::move(parameter_owner.parameters);
  }

  const auto parse_selected_name = [&]() {
    const auto first = expect_identifier("class type name");
    std::string selected = first.text;
    auto span = first.span;
    while (match(TokenKind::Scope)) {
      const auto suffix = expect_identifier("selected class type name");
      selected += "::";
      selected += suffix.text;
      span = cover(span, suffix.span);
    }
    return std::pair{std::move(selected), std::move(span)};
  };
  const auto parse_base = [&]() {
    auto [selected, span] = parse_selected_name();
    SystemVerilogClassBase base;
    base.name = std::move(selected);
    base.span = std::move(span);
    if (match(TokenKind::Hash)) {
      const auto hash = previous();
      Instance actual_owner;
      parse_parameter_overrides(actual_owner, hash);
      for (auto& actual : actual_owner.parameter_overrides) {
        SystemVerilogClassParameterActual retained;
        retained.name = actual.name.value_or(std::string{});
        retained.value = std::move(actual.value);
        retained.type_actual = std::move(actual.type_value);
        retained.span = std::move(actual.span);
        base.parameter_actuals.push_back(std::move(retained));
      }
      base.span = cover(base.span, previous().span);
    }
    return base;
  };

  if (match_keyword("extends")) {
    declaration.base = parse_base();
  }
  if (match_keyword("implements")) {
    do {
      declaration.implemented_interfaces.push_back(parse_base());
    } while (match(TokenKind::Comma));
  }
  expect(
      TokenKind::Semicolon,
      "';' after class header",
      "FSIM-SV-PARSE-258");

  while (!at_end() && !keyword("endclass")) {
    if (match_keyword("typedef")) {
      const auto nested_start = previous();
      if (keyword("class")) {
        add_class_declaration(
            declaration.nested_classes,
            parse_class_forward_declaration(
                nested_start, declaration.canonical_identity),
            nested_start);
      } else {
        DesignUnit type_owner;
        parse_typedef(type_owner, nested_start);
        declaration.type_aliases.insert(
            declaration.type_aliases.end(),
            std::make_move_iterator(type_owner.type_aliases.begin()),
            std::make_move_iterator(type_owner.type_aliases.end()));
      }
      continue;
    }
    if (keyword("virtual") && keyword("class", 1)) {
      const auto qualifier = advance();
      if (match_keyword("class")) {
        add_class_declaration(
            declaration.nested_classes,
            parse_class(
                previous(), declaration.canonical_identity, true),
            qualifier);
      } else {
        error(
            qualifier,
            "FSIM-SV-PARSE-254",
            "class-member 'virtual' must introduce a nested class or "
            "qualify a method");
        skip_to_semicolon();
      }
      continue;
    }
    if (match_keyword("interface")) {
      const auto qualifier = previous();
      if (match_keyword("class")) {
        add_class_declaration(
            declaration.nested_classes,
            parse_class(
                previous(), declaration.canonical_identity, false, true),
            qualifier);
      } else {
        error(
            qualifier,
            "FSIM-SV-PARSE-255",
            "class-member 'interface' must introduce an interface class");
        skip_to_semicolon();
      }
      continue;
    }
    if (match_keyword("class")) {
      const auto nested_start = previous();
      add_class_declaration(
          declaration.nested_classes,
          parse_class(nested_start, declaration.canonical_identity),
          nested_start);
      continue;
    }

    const auto method_prefix = [&]() {
      std::size_t lookahead = 0;
      while (contains_word(
          {"local", "protected", "static", "virtual", "pure", "final",
           "extern"},
          current(lookahead).text)) {
        ++lookahead;
      }
      return keyword("function", lookahead)
          || keyword("task", lookahead);
    };
    if (method_prefix()) {
      const auto method_start = current();
      auto visibility = SystemVerilogClassVisibility::Public;
      bool is_static = false;
      bool is_virtual = false;
      bool is_pure = false;
      bool is_final = false;
      bool is_extern = false;
      for (;;) {
        if (match_keyword("local")) {
          visibility = SystemVerilogClassVisibility::Local;
        } else if (match_keyword("protected")) {
          visibility = SystemVerilogClassVisibility::Protected;
        } else if (match_keyword("static")) {
          is_static = true;
        } else if (match_keyword("virtual")) {
          is_virtual = true;
        } else if (match_keyword("pure")) {
          is_pure = true;
        } else if (match_keyword("final")) {
          is_final = true;
        } else if (match_keyword("extern")) {
          is_extern = true;
        } else {
          break;
        }
      }
      const auto kind = match_keyword("task")
          ? SystemVerilogClassMethodKind::Task
          : (expect_keyword(
                 "function", false, "FSIM-SV-PARSE-262"),
             SystemVerilogClassMethodKind::Function);
      declaration.methods.push_back(parse_class_method(
          method_start,
          kind,
          visibility,
          is_static,
          is_virtual,
          is_pure,
          is_final,
          is_extern,
          declaration.canonical_identity));
      continue;
    }

    const auto constraint_prefix = [&]() {
      std::size_t lookahead = 0;
      while (contains_word(
          {"local", "protected", "static", "pure", "extern"},
          current(lookahead).text)) {
        ++lookahead;
      }
      return keyword("constraint", lookahead);
    };
    if (constraint_prefix()) {
      const auto constraint_start = current();
      auto visibility = SystemVerilogClassVisibility::Public;
      bool is_static = false;
      bool is_pure = false;
      bool is_extern = false;
      for (;;) {
        if (match_keyword("local")) {
          visibility = SystemVerilogClassVisibility::Local;
        } else if (match_keyword("protected")) {
          visibility = SystemVerilogClassVisibility::Protected;
        } else if (match_keyword("static")) {
          is_static = true;
        } else if (match_keyword("pure")) {
          is_pure = true;
        } else if (match_keyword("extern")) {
          is_extern = true;
        } else {
          break;
        }
      }
      expect_keyword("constraint", false, "FSIM-SV-PARSE-267");
      auto constraint = parse_class_constraint(
          constraint_start,
          declaration.canonical_identity,
          visibility,
          is_static,
          is_pure,
          is_extern);
      const bool duplicate = std::ranges::any_of(
          declaration.constraints,
          [&](const SystemVerilogClassConstraint& existing) {
            return existing.name == constraint.name;
          });
      if (duplicate) {
        error(
            constraint_start,
            "FSIM-SV-SEM-173",
            "duplicate class constraint declaration '"
                + constraint.name + "'");
      } else {
        declaration.constraints.push_back(std::move(constraint));
      }
      continue;
    }

    const auto member_start = current();
    if (parse_class_property(declaration, member_start)) {
      continue;
    }

    const auto unsupported = advance();
    error(
        unsupported,
        "FSIM-SV-UNSUPPORTED-045",
        "class members are implemented after the class declaration "
        "foundation");
    skip_to_semicolon();
  }

  expect_keyword("endclass", false, "FSIM-SV-PARSE-259");
  if (match(TokenKind::Colon)) {
    const auto end_name = expect_identifier("class name after endclass");
    declaration.end_name = end_name.text;
    if (end_name.text != declaration.name) {
      error(
          end_name,
          "FSIM-SV-SEM-168",
          "class end name does not match '" + declaration.name + "'");
    }
  }
  declaration.span = span_from(start, previous());
  return declaration;
}

}  // namespace fsim::frontend
