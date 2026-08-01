// SPDX-License-Identifier: Apache-2.0
#include "vhdl_parser_internal.hpp"

namespace fsim::frontend {

bool VhdlParser::vhdl_generic_clause_precedes_subprogram() const {
  const std::size_t first =
      keyword("generic", 0, true) ? 1 : 0;
  if (!at(TokenKind::LeftParen, first)) {
    return false;
  }
  std::size_t depth = 0;
  for (std::size_t lookahead = first;
       current(lookahead).kind != TokenKind::EndOfFile;
       ++lookahead) {
    if (current(lookahead).kind == TokenKind::LeftParen) {
      ++depth;
      continue;
    }
    if (current(lookahead).kind != TokenKind::RightParen) {
      continue;
    }
    if (depth == 0) {
      return false;
    }
    --depth;
    if (depth != 0) {
      continue;
    }
    const auto next = lookahead + 1;
    return keyword("function", next, true)
        || keyword("procedure", next, true)
        || ((keyword("pure", next, true)
             || keyword("impure", next, true))
            && keyword("function", next + 1, true));
  }
  return false;
}

void VhdlParser::parse_vhdl_subprogram_generic_map(
    std::vector<ParameterOverride>& associations,
    bool& box,
    const Token& start) {
  expect_keyword("map", true, "FSIM-VHDL-PARSE-196");
  expect(
      TokenKind::LeftParen,
      "'(' after subprogram generic map",
      "FSIM-VHDL-PARSE-197");
  if (match(TokenKind::Less)) {
    expect(
        TokenKind::Greater,
        "'>' in subprogram generic box",
        "FSIM-VHDL-PARSE-198");
    box = true;
    expect(
        TokenKind::RightParen,
        "')' after subprogram generic box",
        "FSIM-VHDL-PARSE-199");
    return;
  }

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
      if (std::ranges::any_of(
              associations,
              [&](const ParameterOverride& existing) {
                return existing.name == actual.name;
              })) {
        error(
            name,
            "FSIM-VHDL-SEM-060",
            "duplicate generic subprogram association '"
                + *actual.name + "'");
      }
    } else if (saw_named) {
      error(
          current(),
          "FSIM-VHDL-SEM-061",
          "a positional generic subprogram association cannot follow a "
          "named association");
    }

    if (match(TokenKind::Less)) {
      expect(
          TokenKind::Greater,
          "'>' in default generic subprogram association",
          "FSIM-VHDL-PARSE-200");
      actual.default_box = true;
    } else if (begins_unambiguous_subtype_indication()) {
      actual.type_value = parse_vhdl_type(true, true);
    } else {
      actual.value = parse_expression();
    }
    actual.span =
        cover(association_start.span, previous().span);
    associations.push_back(std::move(actual));
    if (!match(TokenKind::Comma)) {
      break;
    }
  }
  expect(
      TokenKind::RightParen,
      "')' after generic subprogram associations",
      "FSIM-VHDL-PARSE-201");
  (void)start;
}

GenericSubprogramInstantiation
VhdlParser::parse_vhdl_subprogram_instantiation(
    const Token& start,
    const std::string_view kind) {
  GenericSubprogramInstantiation result;
  const auto name =
      expect_identifier(
          std::string{kind} + " instance name");
  result.name = vhdl_name(name.text);
  expect_keyword("is", true, "FSIM-VHDL-PARSE-194");
  expect_keyword("new", true, "FSIM-VHDL-PARSE-195");
  result.template_name =
      parse_vhdl_selected_name(
          "generic subprogram template name");
  if (match_keyword("generic", true)) {
    parse_vhdl_subprogram_generic_map(
        result.generic_map,
        result.generic_map_box,
        previous());
  }
  expect(
      TokenKind::Semicolon,
      "';' after generic subprogram instantiation",
      "FSIM-VHDL-PARSE-202");
  result.span = span_from(start, previous());
  return result;
}

void VhdlParser::parse_vhdl_function_item(
    DesignUnit& unit,
    const Token& start,
    const bool pure,
    const bool allow_declaration) {
  const bool instance =
      (at(TokenKind::Identifier)
       || at(TokenKind::StringLiteral))
      && keyword("is", 1, true)
      && keyword("new", 2, true);
  if (!instance) {
    unit.functions.push_back(
        parse_vhdl_function(
            start, pure, allow_declaration));
    return;
  }
  if (!pure) {
    error(
        start,
        "FSIM-VHDL-UNSUPPORTED-046",
        "a generic function instantiation has no pure or impure prefix");
  }
  auto parsed =
      parse_vhdl_subprogram_instantiation(start, "function");
  const auto conflict =
      std::ranges::any_of(
          unit.functions,
          [&](const auto& function) {
            return function.name == parsed.name;
          })
      || std::ranges::any_of(
          unit.procedures,
          [&](const auto& procedure) {
            return procedure.name == parsed.name;
          })
      || std::ranges::any_of(
          unit.generic_function_instances,
          [&](const auto& existing) {
            return existing.name == parsed.name;
          })
      || std::ranges::any_of(
          unit.generic_procedure_instances,
          [&](const auto& existing) {
            return existing.name == parsed.name;
          });
  if (conflict) {
    error(
        start,
        "FSIM-VHDL-SEM-062",
        "duplicate subprogram declaration or instance '"
            + parsed.name + "'");
  } else {
    unit.generic_function_instances.push_back(
        std::move(parsed));
  }
}

void VhdlParser::parse_vhdl_procedure_item(
    DesignUnit& unit,
    const Token& start,
    const bool allow_declaration) {
  const bool instance =
      (at(TokenKind::Identifier)
       || at(TokenKind::StringLiteral))
      && keyword("is", 1, true)
      && keyword("new", 2, true);
  if (!instance) {
    unit.procedures.push_back(
        parse_vhdl_procedure(
            start, allow_declaration));
    return;
  }
  auto parsed =
      parse_vhdl_subprogram_instantiation(start, "procedure");
  const auto conflict =
      std::ranges::any_of(
          unit.functions,
          [&](const auto& function) {
            return function.name == parsed.name;
          })
      || std::ranges::any_of(
          unit.procedures,
          [&](const auto& procedure) {
            return procedure.name == parsed.name;
          })
      || std::ranges::any_of(
          unit.generic_function_instances,
          [&](const auto& existing) {
            return existing.name == parsed.name;
          })
      || std::ranges::any_of(
          unit.generic_procedure_instances,
          [&](const auto& existing) {
            return existing.name == parsed.name;
          });
  if (conflict) {
    error(
        start,
        "FSIM-VHDL-SEM-062",
        "duplicate subprogram declaration or instance '"
            + parsed.name + "'");
  } else {
    unit.generic_procedure_instances.push_back(
        std::move(parsed));
  }
}

void VhdlParser::parse_vhdl_generic_subprogram(
    DesignUnit& unit,
    const Token& start,
    const bool allow_declaration) {
  DesignUnit generic_interface;
  generic_interface.language = Language::Vhdl2008;
  parse_vhdl_generics(
      generic_interface, start, false);
  if (generic_interface.parameters.empty()) {
    error(
        start,
        "FSIM-VHDL-SEM-063",
        "a generic subprogram requires at least one generic formal");
  }
  for (const auto& formal :
       generic_interface.parameters) {
    if (formal.kind == ParameterKind::Package) {
      error(
          Token{
              TokenKind::Identifier,
              formal.name,
              formal.span,
              {}},
          "FSIM-VHDL-UNSUPPORTED-045",
          "nested interface package formals are outside the bounded "
          "generic subprogram subset");
    }
  }

  bool pure = true;
  if (match_keyword("pure", true)) {
    pure = true;
  } else if (match_keyword("impure", true)) {
    pure = false;
    error(
        previous(),
        "FSIM-VHDL-UNSUPPORTED-047",
        "bounded generic functions must be pure");
  }
  if (match_keyword("function", true)) {
    const auto function_start = previous();
    auto function = parse_vhdl_function(
        function_start, pure, allow_declaration);
    const auto duplicate = std::ranges::any_of(
        unit.generic_function_templates,
        [&](const auto& existing) {
          return existing.function.name == function.name
              && existing.function.defined
                  == function.defined;
        });
    if (duplicate) {
      error(
          function_start,
          "FSIM-VHDL-SEM-064",
          "duplicate generic function template '"
              + function.name + "'");
      return;
    }
    unit.generic_function_templates.push_back(
        GenericFunctionTemplate{
            std::move(generic_interface.parameters),
            std::move(function),
            span_from(start, previous())});
    return;
  }
  if (match_keyword("procedure", true)) {
    const auto procedure_start = previous();
    auto procedure = parse_vhdl_procedure(
        procedure_start, allow_declaration);
    const auto duplicate = std::ranges::any_of(
        unit.generic_procedure_templates,
        [&](const auto& existing) {
          return existing.procedure.name == procedure.name
              && existing.procedure.defined
                  == procedure.defined;
        });
    if (duplicate) {
      error(
          procedure_start,
          "FSIM-VHDL-SEM-064",
          "duplicate generic procedure template '"
              + procedure.name + "'");
      return;
    }
    unit.generic_procedure_templates.push_back(
        GenericProcedureTemplate{
            std::move(generic_interface.parameters),
            std::move(procedure),
            span_from(start, previous())});
    return;
  }
  error(
      current(),
      "FSIM-VHDL-PARSE-203",
      "expected function or procedure after generic subprogram clause");
  skip_to_semicolon();
}

std::vector<FunctionArgument>
VhdlParser::parse_vhdl_function_parameters() {
  std::vector<FunctionArgument> arguments;
  (void)match_keyword("parameter", true);
  if (!match(TokenKind::LeftParen)) {
    return arguments;
  }

  while (!at_end() && !at(TokenKind::RightParen)) {
    const auto declaration_start = current();
    bool supported_class = true;
    if (match_keyword("constant", true)) {
      // Constant is the default class for function parameters.
    } else if (
        match_keyword("signal", true)
        || match_keyword("variable", true)
        || match_keyword("file", true)) {
      supported_class = false;
      error(
          previous(),
          "FSIM-VHDL-UNSUPPORTED-029",
          "bounded VHDL functions require constant-class parameters");
    }

    std::vector<Token> names;
    names.push_back(expect_identifier("function parameter name"));
    while (match(TokenKind::Comma)) {
      names.push_back(
          expect_identifier("function parameter name"));
    }
    expect(
        TokenKind::Colon,
        "':' after function parameter names",
        "FSIM-VHDL-PARSE-152");

    bool input_mode = true;
    if (match_keyword("in", true)) {
      // In is the default mode for function parameters.
    } else if (
        match_keyword("out", true)
        || match_keyword("inout", true)
        || match_keyword("buffer", true)
        || match_keyword("linkage", true)) {
      input_mode = false;
      error(
          previous(),
          "FSIM-VHDL-UNSUPPORTED-030",
          "bounded VHDL functions require input parameters");
    }

    const auto type_start = current();
    const auto type = parse_vhdl_type(true, true);
    if (type.named_type.empty()
        && (type.packed_range
            || (type.domain != ValueDomain::Integer
                && type.domain != ValueDomain::Boolean
                && type.domain != ValueDomain::Bit2
                && type.domain != ValueDomain::Logic9))) {
      error(
          type_start,
          "FSIM-VHDL-UNSUPPORTED-031",
          "bounded VHDL function parameters require scalar integer, "
          "Boolean, bit, std_logic, or visible scalar subtype profiles");
    }
    std::optional<Expression> default_value;
    if (match(TokenKind::ColonEqual)) {
      default_value = parse_expression();
    }

    for (const auto& name : names) {
      const auto canonical = vhdl_name(name.text);
      if (std::ranges::any_of(
              arguments,
              [&](const FunctionArgument& existing) {
                return existing.name == canonical;
              })) {
        error(
            name,
            "FSIM-VHDL-SEM-042",
            "duplicate VHDL function parameter '" + canonical + "'");
        continue;
      }
      if (supported_class && input_mode) {
        arguments.push_back(FunctionArgument{
            canonical,
            type,
            PortDirection::Input,
            span_from(name, previous()),
            false,
            default_value});
      }
    }

    if (!match(TokenKind::Semicolon)
        && !at(TokenKind::RightParen)) {
      error(
          current(),
          "FSIM-VHDL-PARSE-153",
          "expected ';' between function parameter declarations");
      skip_to_semicolon();
    }
    (void)declaration_start;
  }
  expect(
      TokenKind::RightParen,
      "')' after function parameter declarations",
      "FSIM-VHDL-PARSE-154");
  return arguments;
}

std::vector<ProcedureArgument>
VhdlParser::parse_vhdl_procedure_parameters() {
  std::vector<ProcedureArgument> arguments;
  (void)match_keyword("parameter", true);
  if (!match(TokenKind::LeftParen)) {
    return arguments;
  }

  while (!at_end() && !at(TokenKind::RightParen)) {
    std::optional<InterfaceObjectClass> explicit_class;
    bool supported_class = true;
    if (match_keyword("constant", true)) {
      explicit_class = InterfaceObjectClass::Constant;
    } else if (match_keyword("variable", true)) {
      explicit_class = InterfaceObjectClass::Variable;
    } else if (
        match_keyword("signal", true)
        || match_keyword("file", true)) {
      supported_class = false;
      error(
          previous(),
          "FSIM-VHDL-UNSUPPORTED-038",
          "bounded VHDL procedures require constant- or variable-class "
          "parameters");
    }

    std::vector<Token> names;
    names.push_back(
        expect_identifier("procedure parameter name"));
    while (match(TokenKind::Comma)) {
      names.push_back(
          expect_identifier("procedure parameter name"));
    }
    expect(
        TokenKind::Colon,
        "':' after procedure parameter names",
        "FSIM-VHDL-PARSE-168");

    auto direction = PortDirection::Input;
    if (match_keyword("in", true)) {
      direction = PortDirection::Input;
    } else if (match_keyword("out", true)) {
      direction = PortDirection::Output;
    } else if (match_keyword("inout", true)) {
      direction = PortDirection::Inout;
    } else if (
        match_keyword("buffer", true)
        || match_keyword("linkage", true)) {
      supported_class = false;
      error(
          previous(),
          "FSIM-VHDL-UNSUPPORTED-039",
          "bounded VHDL procedures support only in, out, and inout "
          "parameter modes");
    }
    const auto object_class = explicit_class.value_or(
        direction == PortDirection::Input
            ? InterfaceObjectClass::Constant
            : InterfaceObjectClass::Variable);
    if (object_class == InterfaceObjectClass::Constant
        && direction != PortDirection::Input) {
      supported_class = false;
      error(
          names.front(),
          "FSIM-VHDL-SEM-049",
          "a constant-class VHDL procedure parameter must have mode in");
    }

    const auto type_start = current();
    const auto type = parse_vhdl_type(true, true);
    if (type.named_type.empty()
        && (type.packed_range
            || (type.domain != ValueDomain::Integer
                && type.domain != ValueDomain::Boolean
                && type.domain != ValueDomain::Bit2
                && type.domain != ValueDomain::Logic9))) {
      error(
          type_start,
          "FSIM-VHDL-UNSUPPORTED-040",
          "bounded VHDL procedure parameters require scalar integer, "
          "Boolean, bit, std_logic, or visible scalar subtype profiles");
    }
    std::optional<Expression> default_value;
    if (match(TokenKind::ColonEqual)) {
      const auto default_start = previous();
      default_value = parse_expression();
      if (direction != PortDirection::Input) {
        error(
            default_start,
            "FSIM-VHDL-SEM-074",
            "a VHDL procedure parameter default requires mode in");
      }
    }

    for (const auto& name : names) {
      const auto canonical = vhdl_name(name.text);
      if (std::ranges::any_of(
              arguments,
              [&](const ProcedureArgument& existing) {
                return existing.name == canonical;
              })) {
        error(
            name,
            "FSIM-VHDL-SEM-050",
            "duplicate VHDL procedure parameter '" + canonical + "'");
        continue;
      }
      if (supported_class) {
        arguments.push_back(ProcedureArgument{
            canonical,
            type,
            direction,
            object_class,
            span_from(name, previous()),
            default_value});
      }
    }

    if (!match(TokenKind::Semicolon)
        && !at(TokenKind::RightParen)) {
      error(
          current(),
          "FSIM-VHDL-PARSE-169",
          "expected ';' between procedure parameter declarations");
      skip_to_semicolon();
    }
  }
  expect(
      TokenKind::RightParen,
      "')' after procedure parameter declarations",
      "FSIM-VHDL-PARSE-170");
  return arguments;
}

ParameterDeclaration VhdlParser::parse_vhdl_interface_function(
  const Token& start,
  const bool pure) {
  ParameterDeclaration generic;
  generic.kind = ParameterKind::Function;

  Token name;
  if (at(TokenKind::StringLiteral)) {
    name = advance();
    error(
        name,
        "FSIM-VHDL-UNSUPPORTED-033",
        "operator-symbol interface function designators are not "
        "implemented");
    generic.name = string_literal_text(name);
  } else {
    name = expect_identifier("interface function name");
    generic.name = vhdl_name(name.text);
  }

  InterfaceFunctionProfile profile;
  profile.pure = pure;
  profile.arguments = parse_vhdl_function_parameters();
  expect_keyword(
      "return", true, "FSIM-VHDL-PARSE-155");
  const auto result_start = current();
  profile.return_type = parse_vhdl_type(true, true);
  if (profile.return_type.named_type.empty()
      && (profile.return_type.packed_range
          || (profile.return_type.domain != ValueDomain::Integer
              && profile.return_type.domain != ValueDomain::Boolean
              && profile.return_type.domain != ValueDomain::Bit2
              && profile.return_type.domain != ValueDomain::Logic9))) {
    error(
        result_start,
        "FSIM-VHDL-UNSUPPORTED-034",
        "bounded VHDL interface functions require scalar integer, "
        "Boolean, bit, std_logic, or visible scalar subtype results");
  }

  if (match_keyword("is", true)) {
    if (match(TokenKind::Less)) {
      const auto box_start = previous();
      expect(
          TokenKind::Greater,
          "'>' in interface function default '<>'",
          "FSIM-VHDL-PARSE-156");
      profile.default_box = true;
      profile.span = span_from(start, previous());
      (void)box_start;
    } else if (at(TokenKind::Identifier)) {
      const auto default_start = advance();
      std::string default_name =
          vhdl_name(default_start.text);
      while (match(TokenKind::Dot)) {
        const auto selected =
            expect_identifier("selected default function name");
        default_name += '.';
        default_name += vhdl_name(selected.text);
      }
      profile.default_name = std::move(default_name);
      profile.span = span_from(start, previous());
    } else {
      error(
          current(),
          "FSIM-VHDL-PARSE-157",
          "expected a function name or '<>' after interface function "
          "'is'");
    }
  }
  if (profile.span.source_name.empty()) {
    profile.span = span_from(start, previous());
  }
  generic.span = profile.span;
  generic.function_profile = std::move(profile);
  return generic;
}

ParameterDeclaration VhdlParser::parse_vhdl_interface_procedure(
  const Token& start) {
  ParameterDeclaration generic;
  generic.kind = ParameterKind::Procedure;

  Token name;
  if (at(TokenKind::StringLiteral)) {
    name = advance();
    error(
        name,
        "FSIM-VHDL-UNSUPPORTED-042",
        "operator-symbol interface procedure designators are not "
        "implemented");
    generic.name = string_literal_text(name);
  } else {
    name = expect_identifier("interface procedure name");
    generic.name = vhdl_name(name.text);
  }

  InterfaceProcedureProfile profile;
  profile.arguments = parse_vhdl_procedure_parameters();
  if (match_keyword("is", true)) {
    if (match(TokenKind::Less)) {
      expect(
          TokenKind::Greater,
          "'>' in interface procedure default '<>'",
          "FSIM-VHDL-PARSE-171");
      profile.default_box = true;
    } else if (at(TokenKind::Identifier)) {
      const auto default_start = advance();
      std::string default_name =
          vhdl_name(default_start.text);
      while (match(TokenKind::Dot)) {
        default_name += '.';
        default_name += vhdl_name(
            expect_identifier(
                "selected default procedure name").text);
      }
      profile.default_name = std::move(default_name);
    } else {
      error(
          current(),
          "FSIM-VHDL-PARSE-172",
          "expected a procedure name or '<>' after interface procedure "
          "'is'");
    }
  }
  profile.span = span_from(start, previous());
  generic.span = profile.span;
  generic.procedure_profile = std::move(profile);
  return generic;
}

FunctionDeclaration VhdlParser::parse_vhdl_function(
  const Token& start,
  const bool pure,
  const bool allow_declaration) {
  FunctionDeclaration function;
  function.language = Language::Vhdl2008;
  function.pure = pure;
  function.automatic = true;

  Token name;
  if (at(TokenKind::StringLiteral)) {
    name = advance();
    function.name = string_literal_text(name);
  } else {
    name = expect_identifier("function name");
    function.name = vhdl_name(name.text);
  }
  function.arguments = parse_vhdl_function_parameters();
  expect_keyword(
      "return", true, "FSIM-VHDL-PARSE-155");
  const auto result_start = current();
  function.return_type = parse_vhdl_type(true, true);
  if (function.return_type.named_type.empty()
      && (function.return_type.packed_range
          || (function.return_type.domain != ValueDomain::Integer
              && function.return_type.domain != ValueDomain::Boolean
              && function.return_type.domain != ValueDomain::Bit2
              && function.return_type.domain != ValueDomain::Logic9))) {
    error(
        result_start,
        "FSIM-VHDL-UNSUPPORTED-034",
        "bounded VHDL functions require scalar integer, Boolean, bit, "
        "std_logic, or visible scalar subtype results");
  }

  if (match(TokenKind::Semicolon)) {
    function.defined = false;
    function.span = span_from(start, previous());
    if (!allow_declaration) {
      error(
          name,
          "FSIM-VHDL-SEM-043",
          "a VHDL function declaration in this region requires a body");
    }
    return function;
  }

  expect_keyword(
      "is", true, "FSIM-VHDL-PARSE-158");
  while (!at_end() && !keyword("begin", 0, true)) {
    if (!match_keyword("variable", true)) {
      const auto declaration = advance();
      error(
          declaration,
          "FSIM-VHDL-UNSUPPORTED-035",
          "bounded VHDL function bodies currently support only local "
          "variable declarations");
      skip_to_semicolon();
      continue;
    }

    std::vector<Token> names;
    names.push_back(
        expect_identifier("function local variable name"));
    while (match(TokenKind::Comma)) {
      names.push_back(
          expect_identifier("function local variable name"));
    }
    expect(
        TokenKind::Colon,
        "':' after function local variable names",
        "FSIM-VHDL-PARSE-159");
    const auto type = parse_vhdl_type(true, true);
    std::optional<Expression> initializer;
    if (match(TokenKind::ColonEqual)) {
      initializer = parse_expression();
    }
    expect(
        TokenKind::Semicolon,
        "';' after function local variable declaration",
        "FSIM-VHDL-PARSE-160");
    for (const auto& local_name : names) {
      const auto canonical =
          vhdl_name(local_name.text);
      const bool conflict =
          canonical == function.name
          || std::ranges::any_of(
              function.arguments,
              [&](const FunctionArgument& argument) {
                return argument.name == canonical;
              })
          || std::ranges::any_of(
              function.variables,
              [&](const VariableDeclaration& variable) {
                return variable.name == canonical;
              });
      if (conflict) {
        error(
            local_name,
            "FSIM-VHDL-SEM-044",
            "duplicate VHDL function-local declaration '"
                + canonical + "'");
        continue;
      }
      function.variables.push_back(VariableDeclaration{
          canonical,
          type,
          initializer,
          span_from(local_name, previous())});
    }
  }

  expect_keyword(
      "begin", true, "FSIM-VHDL-PARSE-161");
  const auto previous_function_state = in_vhdl_function_;
  in_vhdl_function_ = true;
  sequential_loop_labels_seen_.clear();
  function.statements = parse_statement_list({"end"});
  sequential_loop_labels_seen_.clear();
  in_vhdl_function_ = previous_function_state;

  expect_keyword(
      "end", true, "FSIM-VHDL-PARSE-162");
  (void)match_keyword("function", true);
  if (at(TokenKind::Identifier)
      || at(TokenKind::StringLiteral)) {
    const auto end_name = advance();
    const auto canonical =
        end_name.kind == TokenKind::Identifier
            ? vhdl_name(end_name.text)
            : string_literal_text(end_name);
    if (canonical != function.name) {
      error(
          end_name,
          "FSIM-VHDL-SEM-045",
          "function end name '" + canonical
              + "' does not match '" + function.name + "'");
    }
  }
  expect(
      TokenKind::Semicolon,
      "';' after function body",
      "FSIM-VHDL-PARSE-163");
  function.defined = true;
  function.span = span_from(start, previous());

  bool saw_return = false;
  const auto validate_statements =
      [&](const auto& self,
          const std::vector<Statement>& statements) -> void {
        for (const auto& statement : statements) {
          bool supported = true;
          if (statement.kind == StatementKind::Return) {
            saw_return = true;
          } else if (
              statement.kind == StatementKind::Assignment) {
            supported =
                statement.assignment_kind
                == AssignmentKind::Blocking;
          } else {
            supported =
                statement.kind == StatementKind::If
                || statement.kind == StatementKind::Case
                || statement.kind == StatementKind::Loop
                || statement.kind == StatementKind::Break
                || statement.kind == StatementKind::Continue
                || statement.kind == StatementKind::Null
                || statement.kind == StatementKind::Block
                || statement.kind == StatementKind::ProcedureCall;
          }
          if (!supported) {
            error(
                Token{
                    TokenKind::Identifier,
                    {},
                    statement.span,
                    {}},
                "FSIM-VHDL-UNSUPPORTED-037",
                "bounded VHDL function bodies must be time-free and may "
                "only update local variables");
          }
          self(self, statement.statements);
          self(self, statement.else_statements);
          for (const auto& alternative :
               statement.case_alternatives) {
            self(self, alternative.statements);
          }
        }
      };
  validate_statements(
      validate_statements, function.statements);
  if (!saw_return) {
    error(
        name,
        "FSIM-VHDL-SEM-048",
        "bounded VHDL function '" + function.name
            + "' requires an explicit return statement");
  }
  return function;
}

ProcedureDeclaration VhdlParser::parse_vhdl_procedure(
  const Token& start,
  const bool allow_declaration) {
  ProcedureDeclaration procedure;
  procedure.language = Language::Vhdl2008;

  Token name;
  if (at(TokenKind::StringLiteral)) {
    name = advance();
    error(
        name,
        "FSIM-VHDL-UNSUPPORTED-042",
        "operator-symbol VHDL procedure designators are not implemented");
    procedure.name = string_literal_text(name);
  } else {
    name = expect_identifier("procedure name");
    procedure.name = vhdl_name(name.text);
  }
  procedure.arguments = parse_vhdl_procedure_parameters();

  if (match(TokenKind::Semicolon)) {
    procedure.defined = false;
    procedure.span = span_from(start, previous());
    if (!allow_declaration) {
      error(
          name,
          "FSIM-VHDL-SEM-051",
          "a VHDL procedure declaration in this region requires a body");
    }
    return procedure;
  }

  expect_keyword(
      "is", true, "FSIM-VHDL-PARSE-173");
  while (!at_end() && !keyword("begin", 0, true)) {
    if (!match_keyword("variable", true)) {
      const auto declaration = advance();
      error(
          declaration,
          "FSIM-VHDL-UNSUPPORTED-043",
          "bounded VHDL procedure bodies currently support only local "
          "variable declarations");
      skip_to_semicolon();
      continue;
    }

    std::vector<Token> names;
    names.push_back(
        expect_identifier("procedure local variable name"));
    while (match(TokenKind::Comma)) {
      names.push_back(
          expect_identifier("procedure local variable name"));
    }
    expect(
        TokenKind::Colon,
        "':' after procedure local variable names",
        "FSIM-VHDL-PARSE-174");
    const auto type = parse_vhdl_type(true, true);
    std::optional<Expression> initializer;
    if (match(TokenKind::ColonEqual)) {
      initializer = parse_expression();
    }
    expect(
        TokenKind::Semicolon,
        "';' after procedure local variable declaration",
        "FSIM-VHDL-PARSE-175");
    for (const auto& local_name : names) {
      const auto canonical = vhdl_name(local_name.text);
      const bool conflict =
          std::ranges::any_of(
              procedure.arguments,
              [&](const ProcedureArgument& argument) {
                return argument.name == canonical;
              })
          || std::ranges::any_of(
              procedure.variables,
              [&](const VariableDeclaration& variable) {
                return variable.name == canonical;
              });
      if (conflict) {
        error(
            local_name,
            "FSIM-VHDL-SEM-052",
            "duplicate VHDL procedure-local declaration '"
                + canonical + "'");
        continue;
      }
      procedure.variables.push_back(VariableDeclaration{
          canonical,
          type,
          initializer,
          span_from(local_name, previous())});
    }
  }

  expect_keyword(
      "begin", true, "FSIM-VHDL-PARSE-176");
  const auto previous_procedure_state = in_vhdl_procedure_;
  in_vhdl_procedure_ = true;
  sequential_loop_labels_seen_.clear();
  procedure.statements = parse_statement_list({"end"});
  sequential_loop_labels_seen_.clear();
  in_vhdl_procedure_ = previous_procedure_state;

  expect_keyword(
      "end", true, "FSIM-VHDL-PARSE-177");
  (void)match_keyword("procedure", true);
  if (at(TokenKind::Identifier)
      || at(TokenKind::StringLiteral)) {
    const auto end_name = advance();
    const auto canonical =
        end_name.kind == TokenKind::Identifier
            ? vhdl_name(end_name.text)
            : string_literal_text(end_name);
    if (canonical != procedure.name) {
      error(
          end_name,
          "FSIM-VHDL-SEM-053",
          "procedure end name '" + canonical
              + "' does not match '" + procedure.name + "'");
    }
  }
  expect(
      TokenKind::Semicolon,
      "';' after procedure body",
      "FSIM-VHDL-PARSE-178");
  procedure.defined = true;
  procedure.span = span_from(start, previous());

  const auto validate_statements =
      [&](const auto& self,
          const std::vector<Statement>& statements) -> void {
        for (const auto& statement : statements) {
          bool supported = true;
          if (statement.kind == StatementKind::Assignment) {
            supported =
                statement.assignment_kind
                == AssignmentKind::Blocking;
            const Expression* target = &statement.target;
            while ((target->kind == ExpressionKind::Index
                    || target->kind == ExpressionKind::Slice)
                   && !target->operands.empty()) {
              target = &target->operands.front();
            }
            if (target->kind == ExpressionKind::Identifier
                && std::ranges::any_of(
                    procedure.arguments,
                    [&](const ProcedureArgument& argument) {
                      return argument.name == target->text
                          && argument.object_class
                              == InterfaceObjectClass::Constant;
                    })) {
              error(
                  Token{
                      TokenKind::Identifier,
                      target->text,
                      statement.target.span,
                      {}},
                  "FSIM-VHDL-SEM-056",
                  "constant-class VHDL procedure parameter '"
                      + target->text + "' is not writable");
            }
          } else {
            supported =
                statement.kind == StatementKind::ProcedureCall
                || statement.kind == StatementKind::If
                || statement.kind == StatementKind::Case
                || statement.kind == StatementKind::Loop
                || statement.kind == StatementKind::Break
                || statement.kind == StatementKind::Continue
                || statement.kind == StatementKind::Return
                || statement.kind == StatementKind::Null
                || statement.kind == StatementKind::Block;
          }
          if (!supported) {
            error(
                Token{
                    TokenKind::Identifier,
                    {},
                    statement.span,
                    {}},
                "FSIM-VHDL-UNSUPPORTED-044",
                "bounded VHDL procedure bodies must be time-free and may "
                "only update variables and procedure formals");
          }
          self(self, statement.statements);
          self(self, statement.else_statements);
          for (const auto& alternative :
               statement.case_alternatives) {
            self(self, alternative.statements);
          }
        }
      };
  validate_statements(
      validate_statements, procedure.statements);
  return procedure;
}

}  // namespace fsim::frontend
