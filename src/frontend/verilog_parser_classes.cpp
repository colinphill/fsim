// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

#include <functional>
#include <set>

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
  declaration.standard_revision = standard_revision_;
  declaration.verilog_compatibility_profile = compatibility_profile_;
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

namespace {

// Class enum constants are static properties whose initializers are
// evaluated without other class names in scope, so an implicit value
// (`previous + 1`) is rewritten over the previous literal's value.
void inline_implicit_enum_values(std::vector<Expression>& values,
    const std::vector<std::string>& names)
{
  for (std::size_t index = 1; index < values.size(); ++index) {
    auto& value = values[index];
    if (value.kind == ExpressionKind::Binary && value.text == "+"
        && value.operands.size() == 2U
        && value.operands.front().kind == ExpressionKind::Identifier
        && value.operands.front().text == names[index - 1U]) {
      value.operands.front() = values[index - 1U];
    }
  }
}

}  // namespace

bool VerilogParser::parse_class_property(
    SystemVerilogClassDeclaration& declaration,
    const Token& start) {
  auto visibility = SystemVerilogClassVisibility::Public;
  bool is_static = false;
  bool is_const = false;
  bool is_rand = false;
  bool is_randc = false;
  bool saw_qualifier = false;
  bool virtual_interface = false;
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
  if (match_keyword("virtual")) {
    virtual_interface = true;
    saw_qualifier = true;
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
      || keyword("shortreal") || keyword("real")
      || keyword("realtime") || keyword("chandle")
      || keyword("process") || keyword("event")
      || keyword("enum")
      || keyword("longint") || keyword("time") || keyword("integer")
      || keyword("int") || keyword("logic") || keyword("reg")
      || keyword("bit") || keyword("signed") || keyword("unsigned")
      || at(TokenKind::LeftBracket);
  if (!virtual_interface
      && !built_in && !is_named_type_reference_start()) {
    if (saw_qualifier) {
      error(
          current(),
          "FSIM-SV-PARSE-260",
          "expected a data type after class property qualifiers");
    }
    return false;
  }

  Type common_type = virtual_interface
      ? parse_virtual_interface_type(start)
      : built_in
          ? parse_parameter_type()
          : parse_named_type();
  // An anonymous enumeration's literals are class constants (8.23).
  if (common_type.named_type.empty()
      && common_type.enumeration_literals.size()
          == common_type.systemverilog_enumeration_values.size()) {
    auto values = common_type.systemverilog_enumeration_values;
    inline_implicit_enum_values(values, common_type.enumeration_literals);
    for (std::size_t index = 0;
         index < common_type.enumeration_literals.size(); ++index) {
      SystemVerilogClassProperty literal;
      literal.declaration = VariableDeclaration{
          common_type.enumeration_literals[index],
          common_type,
          std::optional<Expression>{values[index]},
          common_type.systemverilog_enumeration_values[index].span};
      literal.is_static = true;
      literal.is_const = true;
      literal.is_parameter = true;
      literal.span = literal.declaration.span;
      declaration.properties.push_back(std::move(literal));
    }
  }
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
  method.standard_revision = standard_revision_;
  method.verilog_compatibility_profile = compatibility_profile_;
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
    method.type_aliases = std::move(task.type_aliases);
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
          std::move(argument.default_value),
          false,
          argument.const_reference,
          argument.static_reference};
      method.arguments.push_back(std::move(retained));
    }
    method.span = std::move(task.span);
  } else {
    // A constructor declares its formals only in a parenthesized list
    // (IEEE 1800-2017 A.1.9 class_constructor_declaration).
    bool header_without_ports = false;
    for (std::size_t offset = 0; offset < 8U; ++offset) {
      if (keyword("new", offset)) {
        header_without_ports = at(TokenKind::Semicolon, offset + 1U);
        break;
      }
      if (at(TokenKind::Semicolon, offset)) {
        break;
      }
    }
    auto function = parse_function(start, prototype, true);
    if (header_without_ports && !function.arguments.empty()) {
      error(
          start,
          "FSIM-SV-PARSE-001",
          "a class constructor declares its arguments in a parenthesized "
          "port list");
    }
    method.name = std::move(function.name);
    method.kind = method.name == "new"
            || method.name.ends_with("::new")
        ? SystemVerilogClassMethodKind::Constructor
        : SystemVerilogClassMethodKind::Function;
    method.return_type = std::move(function.return_type);
    method.arguments = std::move(function.arguments);
    method.type_aliases = std::move(function.type_aliases);
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

std::vector<Expression> VerilogParser::parse_constraint_block_expressions(
    const std::string_view close_message,
    std::string close_code)
{
    ++constraint_parse_depth_;
    struct ConstraintDepth {
        std::size_t& depth;
        ~ConstraintDepth() { --depth; }
    } constraint_depth { constraint_parse_depth_ };
    std::function<Expression()> parse_constraint_item;
    std::function<Expression()> parse_constraint_set;
    const auto parse_plain_constraint = [&]() {
        const auto soft = keyword("soft")
            || (at(TokenKind::Identifier)
                && current().text == "soft"
                && at(TokenKind::Identifier, 1));
        std::optional<Token> soft_token;
        if (soft) {
            soft_token = advance();
            (void)require_standard(
                "a soft constraint",
                StandardRevision::SystemVerilog2012,
                *soft_token,
                "FSIM-SV-PARSE-349");
        }
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
                    choice = Expression {
                        ExpressionKind::Call,
                        "@inside-range",
                        { std::move(low), std::move(high) },
                        cover(range_start.span, previous().span)
                    };
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
                operands.push_back(Expression {
                    ExpressionKind::Call,
                    std::move(weight_kind),
                    { std::move(choice), std::move(weight) },
                    item_span });
                if (!match(TokenKind::Comma))
                    break;
            }
            expect(
                TokenKind::RightBrace,
                "'}' after dist list",
                "FSIM-SV-PARSE-273");
            expression = Expression {
                ExpressionKind::Call,
                "dist",
                std::move(operands),
                cover(dist.span, previous().span)
            };
        }
        if (soft_token) {
            const auto soft_span = cover(soft_token->span, expression.span);
            expression = Expression {
                ExpressionKind::Call,
                "soft",
                { std::move(expression) },
                soft_span
            };
        }
        if (match(TokenKind::ThinArrow)) {
            auto consequent = parse_constraint_set();
            const auto implication_span = cover(expression.span, consequent.span);
            return Expression {
                ExpressionKind::Call,
                "@constraint-implies",
                { std::move(expression), std::move(consequent) },
                implication_span
            };
        }
        expect(
            TokenKind::Semicolon,
            "';' after class constraint expression",
            "FSIM-SV-PARSE-265");
        return expression;
    };
    parse_constraint_set = [&]() {
        if (!match(TokenKind::LeftBrace))
            return parse_constraint_item();
        const auto block_start = previous();
        std::vector<Expression> items;
        while (!at_end() && !at(TokenKind::RightBrace)) {
            items.push_back(parse_constraint_item());
        }
        expect(
            TokenKind::RightBrace,
            "'}' after structured constraint set",
            "FSIM-SV-PARSE-274");
        return Expression {
            ExpressionKind::Call,
            "@constraint-block",
            std::move(items),
            cover(block_start.span, previous().span)
        };
    };
    parse_constraint_item = [&]() {
        if (match_keyword("unique")) {
            const auto unique_token = previous();
            (void)require_standard(
                "a unique constraint",
                StandardRevision::SystemVerilog2012,
                unique_token,
                "FSIM-SV-PARSE-349");
            expect(
                TokenKind::LeftBrace,
                "'{' after unique",
                "FSIM-SV-PARSE-377");
            std::vector<Expression> operands;
            if (at(TokenKind::RightBrace)) {
                error(
                    current(),
                    "FSIM-SV-PARSE-378",
                    "a unique constraint requires at least one item");
            } else {
                do {
                    operands.push_back(parse_expression());
                } while (match(TokenKind::Comma));
            }
            expect(
                TokenKind::RightBrace,
                "'}' after unique constraint items",
                "FSIM-SV-PARSE-379");
            expect(
                TokenKind::Semicolon,
                "';' after unique constraint",
                "FSIM-SV-PARSE-380");
            return Expression {
                ExpressionKind::Call,
                "@constraint-unique",
                std::move(operands),
                cover(unique_token.span, previous().span)
            };
        }
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
            Expression earlier_list {
                ExpressionKind::Call,
                "@solve-list",
                std::move(earlier),
                solve_token.span
            };
            Expression later_list {
                ExpressionKind::Call,
                "@solve-list",
                std::move(later),
                previous().span
            };
            return Expression {
                ExpressionKind::Call,
                "@solve-before",
                { std::move(earlier_list), std::move(later_list) },
                cover(solve_token.span, previous().span)
            };
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
            return Expression {
                ExpressionKind::Call,
                "@constraint-if",
                std::move(operands),
                item_span
            };
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
            return Expression {
                ExpressionKind::Call,
                "@constraint-foreach",
                { std::move(selection), std::move(body) },
                item_span
            };
        }
        return parse_plain_constraint();
    };
    std::vector<Expression> expressions;
    while (!at_end() && !at(TokenKind::RightBrace)) {
        expressions.push_back(parse_constraint_item());
    }
    expect(
        TokenKind::RightBrace,
        close_message,
        std::move(close_code));
    return expressions;
}

SystemVerilogClassConstraint VerilogParser::parse_class_constraint(
    const Token& start,
    const std::string_view owner_identity,
    const SystemVerilogClassVisibility visibility,
    const bool is_static,
    const bool is_pure,
    const bool is_extern)
{
    SystemVerilogClassConstraint constraint;
    constraint.visibility = visibility;
    constraint.is_static = is_static;
    constraint.is_pure = is_pure;
    constraint.is_extern = is_extern;
    const auto name = expect_identifier("class constraint name");
    constraint.name = name.text;
    constraint.canonical_identity = std::string { owner_identity };
    if (!constraint.canonical_identity.empty()) {
        constraint.canonical_identity += "::";
    }
    constraint.canonical_identity += constraint.name;
    // `constraint c;` is an implicit external prototype (18.5.1).
    if (is_extern || is_pure || at(TokenKind::Semicolon)) {
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
    constraint.expressions = parse_constraint_block_expressions(
        "'}' after constraint block", "FSIM-SV-PARSE-266");
    constraint.span = span_from(start, previous());
    return constraint;
}

SystemVerilogClassDeclaration VerilogParser::parse_class(
    const Token& start,
    std::string enclosing_scope,
    const bool virtual_class,
    const bool interface_class) {
  SystemVerilogClassDeclaration declaration;
  // Names in a class body resolve to class members, never to implicit nets
  // of the enclosing module (IEEE 1800-2017 6.10).
  struct ImplicitNetScope {
    std::vector<ImplicitNetReference>& references;
    std::size_t count;
    ~ImplicitNetScope() { references.resize(count); }
  } implicit_net_scope { implicit_net_references_,
      implicit_net_references_.size() };
  declaration.standard_revision = standard_revision_;
  declaration.verilog_compatibility_profile = compatibility_profile_;
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
    parse_parameter_port_list(parameter_owner, previous(), true);
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

  // Arguments after the base class name are passed to the base
  // constructor, as by `super.new(...)` (IEEE 1800-2017 8.17).
  std::vector<Token> base_constructor_arguments;
  if (match_keyword("extends")) {
    declaration.base = parse_base();
    if (!interface_class && at(TokenKind::LeftParen)) {
      std::size_t depth {};
      do {
        if (at(TokenKind::LeftParen)) {
          ++depth;
        } else if (at(TokenKind::RightParen)) {
          --depth;
        }
        base_constructor_arguments.push_back(advance());
      } while (depth != 0U && !at_end());
    }
    if (interface_class) {
      while (match(TokenKind::Comma)) {
        (void)require_standard(
            "multiple interface-class inheritance",
            StandardRevision::SystemVerilog2023,
            previous(),
            "FSIM-SV-PARSE-369");
        declaration.extended_interfaces.push_back(parse_base());
      }
    }
  }
  if (match_keyword("implements")
      || (at(TokenKind::Identifier)
          && current().text == "implements" && (advance(), true))) {
    (void)require_standard(
        "an implements clause",
        StandardRevision::SystemVerilog2012,
        previous(),
        "FSIM-SV-PARSE-348");
    do {
      declaration.implemented_interfaces.push_back(parse_base());
    } while (match(TokenKind::Comma));
  }
  expect(
      TokenKind::Semicolon,
      "';' after class header",
      "FSIM-SV-PARSE-258");

  while (!at_end() && !keyword("endclass")) {
    // An empty class item (IEEE 1800-2017 8.3).
    if (match(TokenKind::Semicolon)) {
      continue;
    }
    if (keyword("localparam") || keyword("parameter")) {
      const auto parameter_start = advance();
      DesignUnit parameter_owner;
      parse_parameter_group(
          parameter_owner, true, false, parameter_start, false);
      for (auto& parameter : parameter_owner.parameters) {
        const bool duplicate = std::ranges::any_of(
            declaration.properties,
            [&](const SystemVerilogClassProperty& property) {
              return property.declaration.name == parameter.name;
            });
        if (duplicate) {
          error(
              parameter_start,
              "FSIM-SV-SEM-170",
              "duplicate class property declaration '"
                  + parameter.name + "'");
          continue;
        }
        SystemVerilogClassProperty property;
        property.declaration = VariableDeclaration{
            std::move(parameter.name),
            std::move(parameter.type),
            std::optional<Expression>{
                std::move(parameter.default_value)},
            parameter.span};
        property.is_static = true;
        property.is_const = true;
        property.is_parameter = true;
        property.span = parameter.span;
        declaration.properties.push_back(std::move(property));
      }
      continue;
    }
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
        // The literals of a class-scope enum are class constants, visible
        // in methods and through `obj.A` or `C::A` (IEEE 1800-2017 8.23).
        std::vector<Expression> literal_values;
        std::vector<std::string> literal_names;
        for (const auto& literal : type_owner.parameters) {
          literal_values.push_back(literal.default_value);
          literal_names.push_back(literal.name);
        }
        inline_implicit_enum_values(literal_values, literal_names);
        for (std::size_t index = 0; index < type_owner.parameters.size();
             ++index) {
          auto& literal = type_owner.parameters[index];
          literal.default_value = std::move(literal_values[index]);
          SystemVerilogClassProperty property;
          property.declaration = VariableDeclaration{
              literal.name,
              std::move(literal.type),
              std::optional<Expression>{std::move(literal.default_value)},
              literal.span};
          property.is_static = true;
          property.is_const = true;
          property.is_parameter = true;
          property.span = literal.span;
          declaration.properties.push_back(std::move(property));
        }
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
        error(
            qualifier,
            "FSIM-SV-SEM-245",
            "an interface class cannot be nested in another class");
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
    if (at(TokenKind::Identifier)
        && current().text == "covergroup") {
      const auto covergroup_start = advance();
      add_covergroup_declaration(
          declaration.covergroups,
          parse_covergroup_declaration(
              covergroup_start,
              SystemVerilogCovergroupOwnerKind::Class),
          covergroup_start);
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

  if (!base_constructor_arguments.empty()) {
    const auto& anchor = base_constructor_arguments.front();
    const auto synthetic = [&](const TokenKind kind, std::string text) {
      Token token;
      token.kind = kind;
      token.text = std::move(text);
      token.span = anchor.span;
      return token;
    };
    auto constructor = std::ranges::find_if(
        declaration.methods,
        [](const SystemVerilogClassMethod& method) {
          return method.kind == SystemVerilogClassMethodKind::Constructor;
        });
    std::vector<Token> tokens;
    if (constructor == declaration.methods.end()) {
      tokens = {synthetic(TokenKind::Identifier, "function"),
          synthetic(TokenKind::Identifier, "new"),
          synthetic(TokenKind::LeftParen, "("),
          synthetic(TokenKind::RightParen, ")"),
          synthetic(TokenKind::Semicolon, ";")};
    }
    tokens.push_back(synthetic(TokenKind::Identifier, "super"));
    tokens.push_back(synthetic(TokenKind::Dot, "."));
    tokens.push_back(synthetic(TokenKind::Identifier, "new"));
    tokens.insert(tokens.end(), base_constructor_arguments.begin(),
        base_constructor_arguments.end());
    tokens.push_back(synthetic(TokenKind::Semicolon, ";"));
    if (constructor == declaration.methods.end()) {
      tokens.push_back(synthetic(TokenKind::Identifier, "endfunction"));
    }
    tokens.push_back(synthetic(TokenKind::EndOfFile, ""));
    auto saved_tokens = std::move(tokens_);
    const auto saved_index = index_;
    tokens_ = std::move(tokens);
    index_ = 0;
    if (constructor == declaration.methods.end()) {
      const auto method_start = advance();
      declaration.methods.push_back(parse_class_method(
          method_start,
          SystemVerilogClassMethodKind::Function,
          SystemVerilogClassVisibility::Public,
          false, false, false, false, false,
          declaration.canonical_identity));
    } else if (auto call = parse_statement()) {
      constructor->statements.insert(
          constructor->statements.begin(), std::move(*call));
    }
    tokens_ = std::move(saved_tokens);
    index_ = saved_index;
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
  check_class_randomization_rules(declaration);
  return declaration;
}

// IEEE 1800-2017 18.6.3, 18.8, 18.9 and 18.13: the randomization methods
// are built in and cannot be overridden; 18.5.4, 18.5.10 and 18.5.14.1: a
// randc variable takes no distribution, solve-order, or soft constraint.
void VerilogParser::check_class_randomization_rules(
    const SystemVerilogClassDeclaration& declaration) {
  const auto report = [&](const SourceSpan& span, std::string code,
                          std::string message) {
    Token anchor;
    anchor.span = span;
    error(anchor, std::move(code), std::move(message));
  };
  for (const auto& method : declaration.methods) {
    const auto separator = method.name.rfind("::");
    const auto name = separator == std::string::npos
        ? std::string_view{method.name}
        : std::string_view{method.name}.substr(separator + 2U);
    if (name == "randomize" || name == "rand_mode"
        || name == "constraint_mode" || name == "srandom"
        || name == "get_randstate" || name == "set_randstate") {
      report(method.span, "FSIM-SV-CLASS-029",
          "the built-in method '" + std::string{name}
              + "' cannot be overridden");
    }
  }
  std::set<std::string, std::less<>> cyclic;
  for (const auto& property : declaration.properties) {
    if (property.is_randc) {
      cyclic.insert(property.declaration.name);
    }
  }
  if (cyclic.empty()) {
    return;
  }
  const std::function<bool(const Expression&)> names_cyclic =
      [&](const Expression& expression) {
        if (expression.kind == ExpressionKind::Identifier
            && cyclic.contains(expression.text)) {
          return true;
        }
        return std::ranges::any_of(expression.operands, names_cyclic);
      };
  const std::function<void(const Expression&)> check =
      [&](const Expression& expression) {
        if (expression.kind == ExpressionKind::Call) {
          const auto form = expression.text == "dist"
                  && !expression.operands.empty()
                  && names_cyclic(expression.operands.front())
              ? "a distribution constraint"
              : expression.text == "soft" && names_cyclic(expression)
              ? "a soft constraint"
              : expression.text == "@solve-before" && names_cyclic(expression)
              ? "a solve-before constraint"
              : nullptr;
          if (form != nullptr) {
            report(expression.span, "FSIM-SV-CLASS-030",
                std::string{"a randc variable cannot appear in "} + form);
            return;
          }
        }
        for (const auto& operand : expression.operands) {
          check(operand);
        }
      };
  for (const auto& constraint : declaration.constraints) {
    for (const auto& expression : constraint.expressions) {
      check(expression);
    }
  }
}


// Out-of-block constraint definitions (IEEE 1800-2017 18.5.1) complete the
// prototype of the named class; an explicit `extern constraint` requires one.
void VerilogParser::link_out_of_block_constraints(ParsedDesign& design) {
  const auto report = [&](const SourceSpan& span, std::string code,
                          std::string message) {
    Token anchor;
    anchor.span = span;
    error(anchor, std::move(code), std::move(message));
  };
  std::vector<SystemVerilogClassDeclaration*> classes;
  const std::function<void(std::vector<SystemVerilogClassDeclaration>&)>
      collect = [&](std::vector<SystemVerilogClassDeclaration>& list) {
        for (auto& declaration : list) {
          classes.push_back(&declaration);
          collect(declaration.nested_classes);
        }
      };
  collect(design.systemverilog_classes);
  for (auto& unit : design.units) {
    collect(unit.systemverilog_classes);
  }
  if (compilation_unit_package_) {
    collect(compilation_unit_package_->systemverilog_classes);
  }
  for (auto& [owner, definition] : out_of_block_constraints_) {
    // A class may be recorded in more than one container (its unit and the
    // compilation-unit scope); each copy receives the definition.
    std::vector<SystemVerilogClassConstraint*> prototypes;
    for (auto* declaration : classes) {
      if (declaration->name != owner) {
        continue;
      }
      for (auto& constraint : declaration->constraints) {
        if (constraint.name == definition.name && !constraint.defined) {
          prototypes.push_back(&constraint);
        }
      }
    }
    if (prototypes.empty()) {
      report(definition.span, "FSIM-SV-CLASS-031",
          "out-of-block constraint '" + owner + "::" + definition.name
              + "' has no matching constraint prototype");
      continue;
    }
    for (auto* prototype : prototypes) {
      prototype->expressions = definition.expressions;
      prototype->defined = true;
      prototype->is_extern = false;
    }
  }
  out_of_block_constraints_.clear();
  for (const auto* declaration : classes) {
    for (const auto& constraint : declaration->constraints) {
      if (constraint.is_extern && !constraint.defined) {
        report(constraint.span, "FSIM-SV-CLASS-032",
            "extern constraint '" + declaration->name + "::"
                + constraint.name + "' has no definition");
      }
    }
  }
}

}  // namespace fsim::frontend
