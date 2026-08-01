// SPDX-License-Identifier: Apache-2.0
#include "vhdl_parser_internal.hpp"

namespace fsim::frontend {

VhdlParser::VhdlParser(LexResult lexed)
    : ParserBase(std::move(lexed.tokens),
                 std::move(lexed.diagnostics)) {}

ParseResult VhdlParser::run() {
  ParsedDesign design;
  std::vector<VhdlContextItem> pending_context;
  while (!at_end()) {
    if (keyword("context", 0, true)
        && at(TokenKind::Identifier, 1)
        && keyword("is", 2, true)) {
      const auto start = advance();
      auto unit = parse_context_declaration(start);
      unit.vhdl_context.insert(
          unit.vhdl_context.begin(),
          std::make_move_iterator(pending_context.begin()),
          std::make_move_iterator(pending_context.end()));
      pending_context.clear();
      design.units.push_back(std::move(unit));
    } else if (any_keyword({"library", "use", "context"}, true)) {
      if (auto item = parse_context_item()) {
        pending_context.push_back(std::move(*item));
      }
    } else if (match_keyword("entity", true)) {
      auto unit = parse_entity(previous());
      unit.vhdl_context = std::exchange(pending_context, {});
      design.units.push_back(std::move(unit));
    } else if (match_keyword("architecture", true)) {
      auto unit = parse_architecture(previous());
      unit.vhdl_context = std::exchange(pending_context, {});
      design.units.push_back(std::move(unit));
    } else if (match_keyword("configuration", true)) {
      auto unit = parse_vhdl_configuration(previous());
      unit.vhdl_context = std::exchange(pending_context, {});
      design.units.push_back(std::move(unit));
    } else if (match_keyword("package", true)) {
      const auto start = previous();
      if (match_keyword("body", true)) {
        auto unit = parse_package(start, true);
        unit.vhdl_context = std::exchange(pending_context, {});
        design.units.push_back(std::move(unit));
      } else {
        auto unit = parse_package(start);
        unit.vhdl_context = std::exchange(pending_context, {});
        design.units.push_back(std::move(unit));
      }
    } else {
      const auto unexpected = advance();
      error(unexpected, "FSIM-VHDL-UNSUPPORTED-001",
            "unsupported VHDL design unit or context item '" +
                unexpected.text + "'");
      skip_to_semicolon();
    }
  }
  return ParseResult{std::move(design), std::move(diagnostics_)};
}

SourceSpan VhdlParser::span_from(const Token& first, const Token& last) {
  return cover(first.span, last.span);
}

std::string VhdlParser::string_literal_text(const Token& token) {
  if (token.text.size() >= 2 && token.text.front() == '"'
      && token.text.back() == '"') {
    const auto spelling =
        token.text.substr(1, token.text.size() - 2);
    std::string result;
    result.reserve(spelling.size());
    for (std::size_t index = 0;
         index < spelling.size();
         ++index) {
      result.push_back(spelling[index]);
      if (spelling[index] == '"' && index + 1 < spelling.size()
          && spelling[index + 1] == '"') {
        ++index;
      }
    }
    return result;
  }
  return token.text;
}

std::optional<VhdlContextItem> VhdlParser::parse_context_item() {
  const auto start = advance();
  const auto context_kind =
      detail::iequals(start.text, "library")
          ? VhdlContextItemKind::LibraryClause
          : detail::iequals(start.text, "use")
              ? VhdlContextItemKind::UseClause
              : VhdlContextItemKind::ContextReference;

  if (context_kind == VhdlContextItemKind::ContextReference
      && at(TokenKind::Identifier)
      && keyword("is", 1, true)) {
    error(
        start,
        "FSIM-VHDL-UNSUPPORTED-015",
        "a context declaration cannot be nested where a context "
        "reference is required");
    while (!at_end()) {
      if (match_keyword("end", true)) {
        match_keyword("context", true);
        if (at(TokenKind::Identifier)) {
          advance();
        }
        skip_to_semicolon();
        break;
      }
      advance();
    }
    return std::nullopt;
  }

  VhdlContextItem item;
  item.kind = context_kind;
  std::string selected_name;
  bool malformed = false;
  while (!at_end() && !at(TokenKind::Semicolon)) {
    if (match(TokenKind::Comma)) {
      if (selected_name.empty() || selected_name.back() == '.') {
        malformed = true;
      } else {
        item.selected_names.push_back(std::move(selected_name));
        selected_name.clear();
      }
      continue;
    }
    if (at(TokenKind::Dot)) {
      if (selected_name.empty() || selected_name.back() == '.') {
        malformed = true;
      } else {
        selected_name.push_back('.');
      }
      advance();
      continue;
    }
    if (at(TokenKind::Identifier)) {
      const auto identifier = advance();
      if (!selected_name.empty() && selected_name.back() != '.') {
        malformed = true;
      } else {
        selected_name += vhdl_name(identifier.text);
      }
      continue;
    }
    malformed = true;
    advance();
  }
  if (!selected_name.empty() && selected_name.back() != '.') {
    item.selected_names.push_back(std::move(selected_name));
  } else {
    malformed = true;
  }
  if (!match(TokenKind::Semicolon)) {
    malformed = true;
  }
  if (malformed) {
    error(
        start,
        "FSIM-VHDL-PARSE-044",
        "malformed or unterminated VHDL context clause");
  }
  item.span = span_from(start, previous());
  return item;
}

Token VhdlParser::expect_identifier(std::string_view description) {
  return expect(TokenKind::Identifier, description, "FSIM-VHDL-PARSE-001");
}

DesignUnit VhdlParser::parse_context_declaration(const Token& start) {
  DesignUnit unit;
  unit.kind = UnitKind::VhdlContext;
  unit.language = Language::Vhdl2008;
  const auto name = expect_identifier("context name");
  unit.name = vhdl_name(name.text);
  expect_keyword("is", true, "FSIM-VHDL-PARSE-090");
  while (!at_end() && !keyword("end", 0, true)) {
    if (any_keyword({"library", "use", "context"}, true)) {
      if (auto item = parse_context_item()) {
        unit.vhdl_context.push_back(std::move(*item));
      }
      continue;
    }
    const auto declaration = advance();
    error(
        declaration,
        "FSIM-VHDL-UNSUPPORTED-024",
        "unsupported context declaration item '"
            + declaration.text + "'");
    skip_to_semicolon();
  }
  expect_keyword("end", true, "FSIM-VHDL-PARSE-091");
  (void)match_keyword("context", true);
  if (at(TokenKind::Identifier)) {
    const auto end_name = advance();
    if (vhdl_name(end_name.text) != unit.name) {
      error(
          end_name,
          "FSIM-VHDL-PARSE-092",
          "context end name does not match '" + unit.name + "'");
    }
  }
  expect(
      TokenKind::Semicolon,
      "';' after context declaration",
      "FSIM-VHDL-PARSE-093");
  unit.span = span_from(start, previous());
  return unit;
}

DesignUnit VhdlParser::parse_package(
  const Token& start,
  const bool body) {
  vhdl_named_types_.clear();
  DesignUnit unit;
  unit.kind = UnitKind::VhdlPackage;
  unit.language = Language::Vhdl2008;
  const auto name = expect_identifier("package name");
  unit.name = vhdl_name(name.text);
  if (body) {
    // Package bodies share the package design-unit kind so existing package
    // lookup stays stable; primary_name distinguishes the secondary body
    // unit until elaboration merges its bounded subprogram bodies.
    unit.primary_name = unit.name;
  }
  expect_keyword("is", true, "FSIM-VHDL-PARSE-086");
  while (!at_end() && !keyword("end", 0, true)) {
    if (keyword("generic", 0, true)
        && vhdl_generic_clause_precedes_subprogram()) {
      const auto generic_start = advance();
      parse_vhdl_generic_subprogram(
          unit, generic_start, true);
    } else if (!body && match_keyword("generic", true)) {
      parse_vhdl_generics(unit, previous());
    } else if (match_keyword("constant", true)) {
      parse_package_constant(unit, previous());
    } else if (
        (keyword("pure", 0, true)
         || keyword("impure", 0, true))
        && keyword("function", 1, true)) {
      const bool pure = match_keyword("pure", true);
      if (!pure) {
        (void)match_keyword("impure", true);
      }
      const auto function_start =
          expect_keyword("function", true);
      parse_vhdl_function_item(
          unit, function_start, pure, true);
    } else if (match_keyword("function", true)) {
      parse_vhdl_function_item(
          unit, previous(), true, true);
    } else if (match_keyword("procedure", true)) {
      parse_vhdl_procedure_item(
          unit, previous(), true);
    } else if (match_keyword("type", true)) {
      parse_type_declaration(unit, previous());
    } else if (match_keyword("subtype", true)) {
      parse_subtype_declaration(unit, previous());
    } else if (!body && match_keyword("component", true)) {
      const auto component_start = previous();
      auto declaration =
          parse_vhdl_component_declaration(
              component_start,
              unit.vhdl_component_declarations.size());
      declaration.region =
          VhdlComponentDeclarationRegion::Package;
      declaration.owner_name = unit.name;
      add_vhdl_component_declaration(
          unit.vhdl_component_declarations,
          std::move(declaration),
          component_start);
    } else {
      const auto declaration = advance();
      error(
          declaration,
          "FSIM-VHDL-UNSUPPORTED-022",
          "unsupported package declaration '"
              + declaration.text + "'");
      skip_to_semicolon();
    }
  }
  if (body) {
    expect_keyword(
        "end", true, "FSIM-VHDL-PARSE-166");
    (void)match_keyword("package", true);
    (void)match_keyword("body", true);
    if (at(TokenKind::Identifier)) {
      const auto end_name = advance();
      if (vhdl_name(end_name.text) != unit.name) {
        error(
            end_name,
            "FSIM-VHDL-SEM-047",
            "package body end name does not match '"
                + unit.name + "'");
      }
    }
    expect(
        TokenKind::Semicolon,
        "';' after package body",
        "FSIM-VHDL-PARSE-167");
  } else {
    parse_vhdl_end("package");
  }
  unit.span = span_from(start, previous());
  return unit;
}

void VhdlParser::parse_package_constant(
  DesignUnit& unit, const Token& start) {
  std::vector<Token> names;
  names.push_back(expect_identifier("package constant name"));
  while (match(TokenKind::Comma)) {
    names.push_back(
        expect_identifier("package constant name"));
  }
  expect(
      TokenKind::Colon,
      "':' after package constant names",
      "FSIM-VHDL-PARSE-087");
  const auto type = parse_vhdl_type(true);
  if (type.named_type.empty()
      && (type.packed_range
          || (type.domain != ValueDomain::Integer
              && type.domain != ValueDomain::Boolean
              && type.domain != ValueDomain::Bit2))) {
    error(
        names.front(),
        "FSIM-VHDL-UNSUPPORTED-023",
        "package constants are bounded to scalar integer, Boolean, and "
        "bit types");
  }
  Expression value;
  if (match(TokenKind::ColonEqual)) {
    value = parse_expression();
  } else {
    error(
        current(),
        "FSIM-VHDL-PARSE-088",
        "a package constant requires a default expression");
  }
  expect(
      TokenKind::Semicolon,
      "';' after package constant declaration",
      "FSIM-VHDL-PARSE-089");
  for (const auto& constant_name : names) {
    const auto canonical =
        vhdl_name(constant_name.text);
    if (std::any_of(
            unit.parameters.begin(),
            unit.parameters.end(),
            [&](const ParameterDeclaration& existing) {
              return existing.name == canonical;
            })) {
      error(
          constant_name,
          "FSIM-VHDL-SEM-020",
          "duplicate package constant declaration '"
              + canonical + "'");
      continue;
    }
    unit.parameters.push_back(ParameterDeclaration{
        canonical,
        type,
        value,
        true,
        span_from(start, previous()),
        ParameterKind::Value,
        std::nullopt});
  }
}

DesignUnit VhdlParser::parse_entity(const Token& start) {
  vhdl_named_types_.clear();
  DesignUnit unit;
  unit.kind = UnitKind::VhdlEntity;
  unit.language = Language::Vhdl2008;
  const auto name = expect_identifier("entity name");
  unit.name = vhdl_name(name.text);
  expect_keyword("is", true, "FSIM-VHDL-PARSE-002");

  while (!at_end() && !keyword("end", 0, true)) {
    if (keyword("generic", 0, true)
        && vhdl_generic_clause_precedes_subprogram()) {
      const auto generic_start = advance();
      parse_vhdl_generic_subprogram(
          unit, generic_start, true);
    } else if (match_keyword("port", true)) {
      parse_vhdl_ports(unit);
    } else if (match_keyword("generic", true)) {
      parse_vhdl_generics(unit, previous());
    } else if (
        (keyword("pure", 0, true)
         || keyword("impure", 0, true))
        && keyword("function", 1, true)) {
      const bool pure = match_keyword("pure", true);
      if (!pure) {
        (void)match_keyword("impure", true);
      }
      const auto function_start =
          expect_keyword("function", true);
      parse_vhdl_function_item(
          unit, function_start, pure, true);
    } else if (match_keyword("function", true)) {
      parse_vhdl_function_item(
          unit, previous(), true, true);
    } else if (match_keyword("procedure", true)) {
      parse_vhdl_procedure_item(
          unit, previous(), true);
    } else if (match_keyword("package", true)) {
      const auto package_start = previous();
      auto instance =
          parse_vhdl_package_instantiation(package_start);
      if (std::ranges::any_of(
              unit.package_instances,
              [&](const auto& existing) {
                return existing.name == instance.name;
              })) {
        error(
            package_start,
            "FSIM-VHDL-SEM-059",
            "duplicate local package instance '"
                + instance.name + "'");
      } else {
        unit.package_instances.push_back(
            std::move(instance));
      }
    } else if (match_keyword("type", true)) {
      parse_type_declaration(unit, previous());
    } else if (match_keyword("subtype", true)) {
      parse_subtype_declaration(unit, previous());
    } else if (match_keyword("component", true)) {
      const auto component_start = previous();
      auto declaration =
          parse_vhdl_component_declaration(
              component_start,
              unit.vhdl_component_declarations.size());
      declaration.region =
          VhdlComponentDeclarationRegion::Entity;
      declaration.owner_name = unit.name;
      add_vhdl_component_declaration(
          unit.vhdl_component_declarations,
          std::move(declaration),
          component_start);
    } else {
      const auto declaration = advance();
      error(declaration, "FSIM-VHDL-UNSUPPORTED-003",
            "unsupported entity declaration '" + declaration.text + "'");
      skip_to_semicolon();
    }
  }

  parse_vhdl_end("entity");
  unit.span = span_from(start, previous());
  return unit;
}

void VhdlParser::add_vhdl_generic(
  DesignUnit& unit,
  ParameterDeclaration generic,
  const Token& name) {
  const auto canonical = generic.name;
  const auto type_conflict =
      generic.kind == ParameterKind::Type
      && (vhdl_named_types_.contains(canonical)
          || std::ranges::any_of(
              unit.type_aliases,
              [&](const auto& declaration) {
                return declaration.name == canonical;
              }));
  const auto object_conflict =
      std::any_of(
          unit.ports.begin(),
          unit.ports.end(),
          [&](const SignalDeclaration& declaration) {
            return declaration.name == canonical;
          })
      || std::any_of(
          unit.signals.begin(),
          unit.signals.end(),
          [&](const SignalDeclaration& declaration) {
            return declaration.name == canonical;
          });
  if (type_conflict) {
    error(
        name,
        "FSIM-VHDL-SEM-036",
        "interface type generic '" + canonical
            + "' conflicts with a bounded type declaration");
    return;
  }
  if (object_conflict) {
    error(
        name,
        "FSIM-VHDL-SEM-014",
        "generic '" + canonical
            + "' conflicts with an object declaration");
    return;
  }
  if (std::any_of(
          unit.parameters.begin(),
          unit.parameters.end(),
          [&](const ParameterDeclaration& existing) {
            return existing.name == canonical;
          })) {
    error(
        name,
        "FSIM-VHDL-SEM-013",
        "duplicate generic declaration '" + canonical + "'");
    return;
  }
  unit.parameters.push_back(std::move(generic));
}

void VhdlParser::parse_vhdl_generics(
  DesignUnit& unit,
  const Token& start,
  const bool expect_terminating_semicolon) {
  expect(
      TokenKind::LeftParen,
      "'(' after generic",
      "FSIM-VHDL-PARSE-050");
  while (!at_end() && !at(TokenKind::RightParen)) {
    if (match_keyword("package", true)) {
      const auto package_start = previous();
      auto generic =
          parse_vhdl_interface_package(package_start);
      Token name;
      name.kind = TokenKind::Identifier;
      name.text = generic.name;
      name.span = generic.span;
      add_vhdl_generic(
          unit, std::move(generic), name);
      if (!match(TokenKind::Semicolon)
          && !at(TokenKind::RightParen)) {
        error(
            current(),
            "FSIM-VHDL-PARSE-052",
            "expected ';' between generic declarations");
        skip_to_semicolon();
      }
      continue;
    }
    if ((keyword("pure", 0, true)
         || keyword("impure", 0, true))
        && keyword("function", 1, true)) {
      const bool pure = match_keyword("pure", true);
      if (!pure) {
        (void)match_keyword("impure", true);
      }
      const auto function_start =
          expect_keyword("function", true);
      auto generic =
          parse_vhdl_interface_function(function_start, pure);
      Token name;
      name.kind = TokenKind::Identifier;
      name.text = generic.name;
      name.span = generic.span;
      add_vhdl_generic(
          unit, std::move(generic), name);
      if (!match(TokenKind::Semicolon)
          && !at(TokenKind::RightParen)) {
        error(
            current(),
            "FSIM-VHDL-PARSE-052",
            "expected ';' between generic declarations");
        skip_to_semicolon();
      }
      continue;
    }
    if (match_keyword("function", true)) {
      const auto function_start = previous();
      auto generic =
          parse_vhdl_interface_function(function_start, true);
      Token name;
      name.kind = TokenKind::Identifier;
      name.text = generic.name;
      name.span = generic.span;
      add_vhdl_generic(
          unit, std::move(generic), name);
      if (!match(TokenKind::Semicolon)
          && !at(TokenKind::RightParen)) {
        error(
            current(),
            "FSIM-VHDL-PARSE-052",
            "expected ';' between generic declarations");
        skip_to_semicolon();
      }
      continue;
    }
    if (match_keyword("procedure", true)) {
      const auto procedure_start = previous();
      auto generic =
          parse_vhdl_interface_procedure(procedure_start);
      Token name;
      name.kind = TokenKind::Identifier;
      name.text = generic.name;
      name.span = generic.span;
      add_vhdl_generic(
          unit, std::move(generic), name);
      if (!match(TokenKind::Semicolon)
          && !at(TokenKind::RightParen)) {
        error(
            current(),
            "FSIM-VHDL-PARSE-052",
            "expected ';' between generic declarations");
        skip_to_semicolon();
      }
      continue;
    }
    if (match_keyword("type", true)) {
      const auto type_start = previous();
      const auto name =
          expect_identifier("interface type generic name");
      ParameterDeclaration generic;
      generic.name = vhdl_name(name.text);
      generic.span = span_from(type_start, name);
      generic.kind = ParameterKind::Type;
      add_vhdl_generic(unit, std::move(generic), name);
      if (match_keyword("is", true)
          || match(TokenKind::ColonEqual)) {
        error(
            previous(),
            "FSIM-VHDL-UNSUPPORTED-028",
            "VHDL-2019 classified or invalid default-like interface type "
            "syntax is not part of the VHDL-2008 unclassified 'type T' "
            "form");
        skip_to_semicolon();
      }
      if (!match(TokenKind::Semicolon)
          && !at(TokenKind::RightParen)) {
        error(
            current(),
            "FSIM-VHDL-PARSE-052",
            "expected ';' between generic declarations");
        skip_to_semicolon();
      }
      continue;
    }
    std::vector<Token> names;
    names.push_back(expect_identifier("generic name"));
    while (match(TokenKind::Comma)) {
      names.push_back(expect_identifier("generic name"));
    }
    expect(
        TokenKind::Colon,
        "':' after generic name",
        "FSIM-VHDL-PARSE-051");
    const auto type = parse_vhdl_type(true);
    if (type.named_type.empty()
        && (type.packed_range
            || (type.domain != ValueDomain::Integer
                && type.domain != ValueDomain::Boolean
                && type.domain != ValueDomain::Bit2))) {
      error(
          names.front(),
          "FSIM-VHDL-UNSUPPORTED-018",
          "this generic type is outside the bounded scalar integer, "
          "Boolean, and bit subset");
    }
    Expression default_value;
    if (match(TokenKind::ColonEqual)) {
      default_value = parse_expression();
    }
    for (const auto& name : names) {
      add_vhdl_generic(
          unit,
          ParameterDeclaration{
              vhdl_name(name.text),
              type,
              default_value,
              false,
              span_from(name, previous()),
              ParameterKind::Value,
              std::nullopt},
          name);
    }
    if (!match(TokenKind::Semicolon)
        && !at(TokenKind::RightParen)) {
      error(
          current(),
          "FSIM-VHDL-PARSE-052",
          "expected ';' between generic declarations");
      skip_to_semicolon();
    }
  }
  expect(
      TokenKind::RightParen,
      "')' after generic declarations",
      "FSIM-VHDL-PARSE-053");
  if (expect_terminating_semicolon) {
    expect(
        TokenKind::Semicolon,
        "';' after generic clause",
        "FSIM-VHDL-PARSE-054");
  }
  (void)start;
}

void VhdlParser::parse_vhdl_ports(DesignUnit& unit) {
  expect(TokenKind::LeftParen, "'(' after port",
         "FSIM-VHDL-PARSE-003");
  while (!at_end() && !at(TokenKind::RightParen)) {
    std::vector<Token> names;
    names.push_back(expect_identifier("port name"));
    while (match(TokenKind::Comma)) {
      names.push_back(expect_identifier("port name"));
    }
    expect(TokenKind::Colon, "':' after port name",
           "FSIM-VHDL-PARSE-004");

    PortDirection direction = PortDirection::Unknown;
    if (match_keyword("in", true)) {
      direction = PortDirection::Input;
    } else if (match_keyword("out", true)) {
      direction = PortDirection::Output;
    } else if (match_keyword("inout", true)) {
      direction = PortDirection::Inout;
    } else if (match_keyword("buffer", true)) {
      direction = PortDirection::Buffer;
    } else {
      error(current(), "FSIM-VHDL-PARSE-005",
            "expected VHDL port mode");
    }

    Type type = parse_vhdl_type(true, true);
    std::optional<Expression> default_value;
    if (match(TokenKind::ColonEqual)) {
      const auto initializer = previous();
      default_value = parse_expression();
      if (direction != PortDirection::Input) {
        error(
            initializer,
            "FSIM-VHDL-SEM-075",
            "a VHDL port default is permitted only on an input formal");
      }
    }
    for (const auto& name : names) {
      const auto canonical = vhdl_name(name.text);
      const auto duplicate = std::find_if(
          unit.ports.begin(),
          unit.ports.end(),
          [&](const SignalDeclaration& port) {
            return port.name == canonical;
          });
      if (duplicate != unit.ports.end()) {
        error(
            name,
            "FSIM-VHDL-SEM-002",
            "duplicate port declaration '" + canonical + "'");
      } else if (
          std::any_of(
              unit.parameters.begin(),
              unit.parameters.end(),
              [&](const ParameterDeclaration& generic) {
                return generic.name == canonical;
              })) {
        error(
            name,
            "FSIM-VHDL-SEM-014",
            "port '" + canonical
                + "' conflicts with a generic declaration");
      } else {
        unit.ports.push_back(
            SignalDeclaration{
                canonical,
                type,
                direction,
                true,
                span_from(name, previous()),
                std::nullopt,
                {},
                {},
                default_value});
      }
    }

    if (!match(TokenKind::Semicolon) && !at(TokenKind::RightParen)) {
      error(current(), "FSIM-VHDL-PARSE-006",
            "expected ';' between port declarations");
      skip_to_semicolon();
    }
  }
  expect(TokenKind::RightParen, "')' after port declarations",
         "FSIM-VHDL-PARSE-007");
  expect(TokenKind::Semicolon, "';' after port clause",
         "FSIM-VHDL-PARSE-008");
}

Type VhdlParser::parse_vhdl_type(
  const bool allow_integer,
  const bool /*runtime_base_integer_only*/) {
  const auto first = expect_identifier("subtype indication");
  std::string spelling = vhdl_name(first.text);
  while (match(TokenKind::Dot)) {
    const auto selected = expect_identifier("selected type name");
    spelling += '.';
    spelling += vhdl_name(selected.text);
  }

  Type type;
  type.spelling = spelling;
  const auto simple_name =
      spelling.substr(spelling.find_last_of('.') == std::string::npos
                          ? 0
                          : spelling.find_last_of('.') + 1);
  if (simple_name == "bit" || simple_name == "bit_vector") {
    type.domain = ValueDomain::Bit2;
  } else if (simple_name == "std_logic" ||
             simple_name == "std_logic_vector" ||
             simple_name == "std_ulogic" ||
             simple_name == "std_ulogic_vector" ||
             simple_name == "signed" || simple_name == "unsigned") {
    type.domain = ValueDomain::Logic9;
    type.is_signed = simple_name == "signed";
  } else if (simple_name == "boolean") {
    type.domain = ValueDomain::Boolean;
  } else if (simple_name == "integer" || simple_name == "natural" ||
             simple_name == "positive") {
    type.domain = ValueDomain::Integer;
    type.is_signed = true;
    constexpr auto integer_first =
        std::int64_t{std::numeric_limits<std::int32_t>::min()};
    constexpr auto integer_last =
        std::int64_t{std::numeric_limits<std::int32_t>::max()};
    type.integer_range = IntegerRange{
        simple_name == "integer"
            ? integer_first
            : simple_name == "natural" ? 0 : 1,
        integer_last,
        false};
  }
  if (type.domain == ValueDomain::Unknown) {
    type.named_type = spelling;
    type.named_type_span =
        cover(first.span, previous().span);
  } else if (
      type.domain == ValueDomain::Integer && !allow_integer) {
    error(
        first,
        "FSIM-VHDL-UNSUPPORTED-014",
        "VHDL integer-family objects are parsed but not executable "
        "in this frontend slice");
  }

  if (match_keyword("range", true)) {
    const auto range_start = previous();
    auto left_expression = parse_expression();
    bool descending = false;
    if (match_keyword("downto", true)) {
      descending = true;
    } else if (!match_keyword("to", true)) {
      error(
          current(),
          "FSIM-VHDL-PARSE-009",
          "expected 'to' or 'downto' in discrete subtype constraint");
    }
    auto right_expression = parse_expression();
    const auto left = simple_integer_constant(left_expression);
    const auto right = simple_integer_constant(right_expression);
    if (type.domain == ValueDomain::Integer) {
      type.integer_base_range = type.integer_range;
      type.integer_base_range_expression =
          type.integer_range_expression;
      if (left && right) {
        type.integer_range =
            IntegerRange{*left, *right, descending};
      } else {
        type.integer_range.reset();
      }
      type.integer_range_expression = IntegerRangeExpression{
          std::move(left_expression),
          std::move(right_expression),
          cover(range_start.span, previous().span),
          descending};
    } else {
      type.discrete_range_expression =
          DiscreteRangeExpression{
              std::move(left_expression),
              std::move(right_expression),
              cover(range_start.span, previous().span),
              descending};
    }
  } else if (match(TokenKind::LeftParen)) {
    const auto range_start = previous();
    if (type.domain == ValueDomain::Integer) {
      error(
          range_start,
          "FSIM-VHDL-PARSE-009",
          "an integer subtype constraint uses 'range', not a packed "
          "parenthesized range");
    }
    auto left_expression = parse_expression();
    bool descending = true;
    if (match_keyword("downto", true)) {
      descending = true;
    } else if (match_keyword("to", true)) {
      descending = false;
    } else {
      error(current(), "FSIM-VHDL-PARSE-009",
            "only locally static integer ranges are supported here");
    }
    auto right_expression = parse_expression();
    expect(TokenKind::RightParen, "')' after range",
           "FSIM-VHDL-PARSE-010");
    const auto left = simple_integer_constant(left_expression);
    const auto right = simple_integer_constant(right_expression);
    if (left && right) {
      type.packed_range = PackedRange{*left, *right, descending};
    }
    type.packed_range_expression = PackedRangeExpression{
        std::move(left_expression),
        std::move(right_expression),
        cover(range_start.span, previous().span),
        descending};
  }
  return type;
}

void VhdlParser::parse_vhdl_end(std::string_view expected_kind) {
  expect_keyword("end", true, "FSIM-VHDL-PARSE-012");
  match_keyword(expected_kind, true);
  if (at(TokenKind::Identifier)) {
    advance();
  }
  expect(TokenKind::Semicolon, "';' after end clause",
         "FSIM-VHDL-PARSE-013");
}

DesignUnit VhdlParser::parse_architecture(const Token& start) {
  vhdl_named_types_.clear();
  DesignUnit unit;
  unit.kind = UnitKind::VhdlArchitecture;
  unit.language = Language::Vhdl2008;
  const auto name = expect_identifier("architecture name");
  unit.name = vhdl_name(name.text);
  expect_keyword("of", true, "FSIM-VHDL-PARSE-014");
  const auto entity = expect_identifier("entity name");
  unit.primary_name = vhdl_name(entity.text);
  expect_keyword("is", true, "FSIM-VHDL-PARSE-015");

  while (!at_end() && !keyword("begin", 0, true)) {
    if (keyword("generic", 0, true)
        && vhdl_generic_clause_precedes_subprogram()) {
      const auto generic_start = advance();
      parse_vhdl_generic_subprogram(
          unit, generic_start, true);
    } else if (match_keyword("signal", true)) {
      parse_signal_declaration(unit.signals);
    } else if (
        (keyword("pure", 0, true)
         || keyword("impure", 0, true))
        && keyword("function", 1, true)) {
      const bool pure = match_keyword("pure", true);
      if (!pure) {
        (void)match_keyword("impure", true);
      }
      const auto function_start =
          expect_keyword("function", true);
      parse_vhdl_function_item(
          unit, function_start, pure, true);
    } else if (match_keyword("function", true)) {
      parse_vhdl_function_item(
          unit, previous(), true, true);
    } else if (match_keyword("procedure", true)) {
      parse_vhdl_procedure_item(
          unit, previous(), true);
    } else if (match_keyword("package", true)) {
      const auto package_start = previous();
      auto instance =
          parse_vhdl_package_instantiation(package_start);
      if (std::ranges::any_of(
              unit.package_instances,
              [&](const auto& existing) {
                return existing.name == instance.name;
              })) {
        error(
            package_start,
            "FSIM-VHDL-SEM-059",
            "duplicate local package instance '"
                + instance.name + "'");
      } else {
        unit.package_instances.push_back(
            std::move(instance));
      }
    } else if (match_keyword("type", true)) {
      parse_type_declaration(unit, previous());
    } else if (match_keyword("subtype", true)) {
      parse_subtype_declaration(unit, previous());
    } else if (match_keyword("component", true)) {
      const auto component_start = previous();
      auto declaration = parse_vhdl_component_declaration(
          component_start,
          unit.vhdl_component_declarations.size());
      declaration.region =
          VhdlComponentDeclarationRegion::Architecture;
      declaration.owner_name = unit.name;
      add_vhdl_component_declaration(
          unit.vhdl_component_declarations,
          std::move(declaration),
          component_start);
    } else if (match_keyword("for", true)) {
      const auto specification =
          parse_vhdl_component_configuration(previous(), false);
      unit.vhdl_configuration_specifications.push_back(
          std::move(specification));
    } else {
      const auto declaration = advance();
      error(declaration, "FSIM-VHDL-UNSUPPORTED-004",
            "unsupported architecture declaration '" + declaration.text +
                "'");
      skip_to_semicolon();
    }
  }
  expect_keyword("begin", true, "FSIM-VHDL-PARSE-016");

  while (!at_end() && !keyword("end", 0, true)) {
    parse_concurrent_statement(unit);
  }
  parse_vhdl_end("architecture");
  unit.span = span_from(start, previous());
  return unit;
}

}  // namespace fsim::frontend
