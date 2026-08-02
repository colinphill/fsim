// SPDX-License-Identifier: Apache-2.0
#include "vhdl_parser_internal.hpp"

#include <unordered_map>

namespace fsim::frontend {

void VhdlParser::parse_type_declaration(
  DesignUnit& unit,
  const Token& start,
  const bool nested_scope) {
  const auto name = expect_identifier("type name");
  const auto canonical_name = vhdl_name(name.text);
  const bool duplicate =
      (!nested_scope
       && vhdl_named_types_.contains(canonical_name))
      || std::any_of(
          unit.type_aliases.begin(),
          unit.type_aliases.end(),
          [&](const TypeAliasDeclaration& declaration) {
            return declaration.name == canonical_name;
          })
      || std::any_of(
          unit.parameters.begin(),
          unit.parameters.end(),
          [&](const ParameterDeclaration& parameter) {
            return parameter.kind == ParameterKind::Type
                && parameter.name == canonical_name;
          });
  if (duplicate) {
    error(
        name,
        "FSIM-VHDL-SEM-036",
        "duplicate bounded type declaration '"
            + canonical_name + "'");
  }
  expect_keyword(
      "is", true, "FSIM-VHDL-PARSE-127");
  if (match_keyword("array", true)) {
    Type type;
    type.spelling = canonical_name;
    type.nominal_type =
        start.span.source_name + ":"
        + std::to_string(start.span.begin.offset) + ":"
        + canonical_name;
    type.vhdl_type_declaration = type.nominal_type;
    VhdlArrayInfo array;

    expect(
        TokenKind::LeftParen,
        "'(' after array",
        "FSIM-VHDL-PARSE-143");
    const auto index_start = current();
    bool has_range = false;
    bool unconstrained = false;
    std::optional<Expression> left_expression;
    std::optional<Expression> right_expression;
    SourceSpan index_range_span;
    bool descending = false;

    if (at(TokenKind::Identifier)
        && keyword("range", 1, true)) {
      const auto index_type = advance();
      array.index_subtype =
          vhdl_name(index_type.text);
      array.index_span = index_type.span;
      match_keyword("range", true);
      has_range = true;
      if (match(TokenKind::Less)) {
        unconstrained = true;
        expect(
            TokenKind::Greater,
            "'>' in unconstrained array index '<>'",
            "FSIM-VHDL-PARSE-144");
      }
    } else {
      array.index_subtype = "integer";
      array.index_span = index_start.span;
      has_range = true;
    }

    constexpr auto integer_first =
        std::int64_t{std::numeric_limits<std::int32_t>::min()};
    constexpr auto integer_last =
        std::int64_t{std::numeric_limits<std::int32_t>::max()};
    if (array.index_subtype == "integer") {
      array.index_base_range =
          IntegerRange{integer_first, integer_last, false};
    } else if (array.index_subtype == "natural") {
      array.index_base_range =
          IntegerRange{0, integer_last, false};
    } else if (array.index_subtype == "positive") {
      array.index_base_range =
          IntegerRange{1, integer_last, false};
    } else {
      error(
          index_start,
          "FSIM-VHDL-UNSUPPORTED-027",
          "one-dimensional VHDL arrays currently require an integer, "
          "natural, or positive index subtype");
    }

    if (has_range && !unconstrained) {
      left_expression = parse_expression();
      if (match_keyword("downto", true)) {
        descending = true;
      } else if (match_keyword("to", true)) {
        descending = false;
      } else {
        error(
            current(),
            "FSIM-VHDL-PARSE-145",
            "expected 'to' or 'downto' in array index range");
      }
      right_expression = parse_expression();
      index_range_span =
          cover(index_start.span, previous().span);
    }
    if (match(TokenKind::Comma)) {
      error(
          previous(),
          "FSIM-VHDL-UNSUPPORTED-027",
          "multidimensional VHDL array declarations are not implemented");
      while (!at_end() && !at(TokenKind::RightParen)
             && !at(TokenKind::Semicolon)) {
        advance();
      }
    }
    expect(
        TokenKind::RightParen,
        "')' after array index definition",
        "FSIM-VHDL-PARSE-146");
    expect_keyword(
        "of", true, "FSIM-VHDL-PARSE-147");
    const auto element_start = current();
    const auto element_type =
        parse_vhdl_type(true, true);
    const bool unresolved_element =
        !element_type.named_type.empty();
    const auto element_width = element_type.width();
    const bool supported_element =
        unresolved_element
        || (element_width && *element_width == 1
            && element_type.packed_members.empty()
            && element_type.enumeration_literals.empty()
            && element_type.domain != ValueDomain::Integer
            && element_type.domain != ValueDomain::Unknown);
    if (!supported_element) {
      error(
          element_start,
          "FSIM-VHDL-UNSUPPORTED-027",
          "one-dimensional VHDL arrays currently require a scalar bit, "
          "Boolean, std_logic, std_ulogic, or visible scalar subtype "
          "element");
    }
    expect(
        TokenKind::Semicolon,
        "';' after array type declaration",
        "FSIM-VHDL-PARSE-148");

    array.element_spelling = element_type.spelling;
    array.element_named_type = element_type.named_type;
    array.element_span =
        element_type.named_type_span.source_name.empty()
            ? element_start.span
            : element_type.named_type_span;
    array.element_domain = element_type.domain;
    array.unconstrained = unconstrained;
    type.domain = element_type.domain;
    type.vhdl_array = std::move(array);
    if (left_expression && right_expression) {
      const auto left =
          simple_integer_constant(*left_expression);
      const auto right =
          simple_integer_constant(*right_expression);
      if (left && right) {
        type.packed_range =
            PackedRange{*left, *right, descending};
      }
      type.packed_range_expression =
          PackedRangeExpression{
              std::move(*left_expression),
              std::move(*right_expression),
              index_range_span,
              descending};
    }
    if (!duplicate && supported_element) {
      if (!nested_scope) {
        vhdl_named_types_.insert(canonical_name);
      }
      unit.type_aliases.push_back(TypeAliasDeclaration{
          canonical_name,
          std::move(type),
          span_from(start, previous()),
          {},
          TypeDeclarationKind::VhdlArray});
    }
    return;
  }
  if (match(TokenKind::LeftParen)) {
    Type type;
    type.spelling = canonical_name;
    type.domain = ValueDomain::Bit2;
    type.nominal_type =
        start.span.source_name + ":"
        + std::to_string(start.span.begin.offset) + ":"
        + canonical_name;
    type.vhdl_type_declaration = type.nominal_type;
    std::vector<EnumLiteralDeclaration> literals;
    std::unordered_set<std::string> literal_names;
    while (!at_end()
           && !at(TokenKind::RightParen)
           && !at(TokenKind::Semicolon)) {
      if (!at(TokenKind::Identifier)
          && !at(TokenKind::CharacterLiteral)) {
        error(
            current(),
            "FSIM-VHDL-PARSE-136",
            "an enumeration literal must be an identifier or character "
            "literal");
        advance();
      } else {
        const auto literal = advance();
        const auto literal_name =
            literal.kind == TokenKind::Identifier
                ? vhdl_name(literal.text)
                : literal.text;
        if (!literal_names.insert(literal_name).second) {
          error(
              literal,
              "FSIM-VHDL-SEM-040",
              "duplicate enumeration literal '" + literal_name + "'");
        } else {
          type.enumeration_literals.push_back(literal_name);
          literals.push_back(EnumLiteralDeclaration{
              literal_name,
              Expression{
                  ExpressionKind::IntegerLiteral,
                  std::to_string(literals.size()),
                  {},
                  literal.span},
              literal.span});
        }
      }
      if (at(TokenKind::RightParen)) {
        break;
      }
      if (!match(TokenKind::Comma)) {
        error(
            current(),
            "FSIM-VHDL-PARSE-137",
            "expected ',' between enumeration literals");
        while (!at_end()
               && !at(TokenKind::Comma)
               && !at(TokenKind::RightParen)
               && !at(TokenKind::Semicolon)) {
          advance();
        }
        (void)match(TokenKind::Comma);
      } else if (at(TokenKind::RightParen)) {
        error(
            previous(),
            "FSIM-VHDL-PARSE-138",
            "an enumeration declaration cannot end with ','");
      }
    }
    if (type.enumeration_literals.empty()) {
      error(
          name,
          "FSIM-VHDL-PARSE-139",
          "an enumeration type requires at least one literal");
    }
    expect(
        TokenKind::RightParen,
        "')' after enumeration literals",
        "FSIM-VHDL-PARSE-140");
    expect(
        TokenKind::Semicolon,
        "';' after enumeration type declaration",
        "FSIM-VHDL-PARSE-141");

    std::uint64_t maximum_ordinal =
        type.enumeration_literals.empty()
            ? 0
            : type.enumeration_literals.size() - 1U;
    std::uint64_t width = 1;
    while (maximum_ordinal > 1U) {
      ++width;
      maximum_ordinal >>= 1U;
    }
    type.packed_range = PackedRange{
        static_cast<std::int64_t>(width - 1U), 0, true};
    if (!type.enumeration_literals.empty()) {
      type.enumeration_range = EnumerationRange{
          0,
          static_cast<std::int64_t>(
              type.enumeration_literals.size() - 1U),
          false};
    }
    if (!duplicate && !type.enumeration_literals.empty()) {
      if (!nested_scope) {
        vhdl_named_types_.insert(canonical_name);
      }
      unit.type_aliases.push_back(TypeAliasDeclaration{
          canonical_name,
          std::move(type),
          span_from(start, previous()),
          std::move(literals),
          TypeDeclarationKind::VhdlEnumeration});
    }
    return;
  }
  if (!match_keyword("record", true)) {
    error(
        current(),
        "FSIM-VHDL-UNSUPPORTED-026",
        "bounded VHDL type declarations require a record definition");
    skip_to_semicolon();
    return;
  }

  Type type;
  type.spelling = canonical_name;
  type.domain = ValueDomain::Bit2;
  type.packed_aggregate = PackedAggregateKind::Struct;
  type.nominal_type =
      start.span.source_name + ":"
      + std::to_string(start.span.begin.offset) + ":"
      + canonical_name;
  type.vhdl_type_declaration = type.nominal_type;
  std::unordered_set<std::string> member_names;
  while (!at_end()
         && !(keyword("end", 0, true)
              && keyword("record", 1, true))) {
    const auto member_start = current();
    std::vector<Token> names;
    names.push_back(
        expect_identifier("record element name"));
    while (match(TokenKind::Comma)) {
      names.push_back(
          expect_identifier("record element name"));
    }
    expect(
        TokenKind::Colon,
        "':' after record element names",
        "FSIM-VHDL-PARSE-128");
    const auto member_type = parse_vhdl_type(true, true);
    const bool supported =
        member_type.named_type.empty()
        && member_type.domain != ValueDomain::Unknown
        && member_type.domain != ValueDomain::Integer;
    if (!supported) {
      error(
          member_start,
          "FSIM-VHDL-UNSUPPORTED-026",
          "bounded VHDL record elements require scalar or statically "
          "ranged bit, bit_vector, Boolean, std_logic, "
          "std_ulogic, std_logic_vector, std_ulogic_vector, signed, "
          "or unsigned types; nested records are not implemented");
    }
    expect(
        TokenKind::Semicolon,
        "';' after record element declaration",
        "FSIM-VHDL-PARSE-129");
    for (const auto& member_name : names) {
      const auto canonical_member =
          vhdl_name(member_name.text);
      if (!member_names.insert(canonical_member).second) {
        error(
            member_name,
            "FSIM-VHDL-SEM-035",
            "duplicate record element '" + canonical_member + "'");
        continue;
      }
      if (!supported) {
        continue;
      }
      type.packed_members.push_back(PackedMember{
          canonical_member,
          member_type.domain,
          member_type.spelling,
          member_type.packed_range,
          member_type.is_signed,
          member_type.packed_range_expression,
          0,
          cover(member_start.span, previous().span),
          {}});
      if (member_type.domain == ValueDomain::Logic9) {
        type.domain = ValueDomain::Logic9;
      } else if (
          member_type.domain == ValueDomain::Logic4
          && type.domain != ValueDomain::Logic9) {
        type.domain = ValueDomain::Logic4;
      }
    }
  }
  if (type.packed_members.empty()) {
    error(
        name,
        "FSIM-VHDL-PARSE-130",
        "a bounded VHDL record type requires at least one supported "
        "element");
  }
  expect_keyword(
      "end", true, "FSIM-VHDL-PARSE-131");
  expect_keyword(
      "record", true, "FSIM-VHDL-PARSE-131");
  if (at(TokenKind::Identifier)) {
    const auto end_name = advance();
    if (vhdl_name(end_name.text) != canonical_name) {
      error(
          end_name,
          "FSIM-VHDL-SEM-037",
          "record end name '" + vhdl_name(end_name.text)
              + "' does not match type name '" + canonical_name + "'");
    }
  }
  expect(
      TokenKind::Semicolon,
      "';' after record type declaration",
      "FSIM-VHDL-PARSE-131");

  std::uint64_t total_width = 0;
  bool concrete = !type.packed_members.empty();
  for (const auto& member : type.packed_members) {
    const auto width = member.width();
    if (!width || *width == 0
        || *width
            > std::numeric_limits<std::uint64_t>::max()
                  - total_width) {
      concrete = false;
      break;
    }
    total_width += *width;
  }
  if (concrete
      && total_width != 0
      && total_width - 1U
          <= static_cast<std::uint64_t>(
              std::numeric_limits<std::int64_t>::max())) {
    auto offset = total_width;
    for (auto& member : type.packed_members) {
      offset -= *member.width();
      member.lsb_offset = offset;
    }
    type.packed_range = PackedRange{
        static_cast<std::int64_t>(total_width - 1U),
        0,
        true};
  }

  if (!duplicate) {
    if (!nested_scope) {
      vhdl_named_types_.insert(canonical_name);
    }
    unit.type_aliases.push_back(TypeAliasDeclaration{
        canonical_name,
        std::move(type),
        span_from(start, previous()),
        {},
        TypeDeclarationKind::VhdlRecord});
  }
}

void VhdlParser::parse_subtype_declaration(
  DesignUnit& unit,
  const Token& start,
  const bool nested_scope) {
  const auto name = expect_identifier("subtype name");
  const auto canonical_name = vhdl_name(name.text);
  const bool duplicate =
      (!nested_scope
       && vhdl_named_types_.contains(canonical_name))
      || std::any_of(
          unit.type_aliases.begin(),
          unit.type_aliases.end(),
          [&](const TypeAliasDeclaration& declaration) {
            return declaration.name == canonical_name;
          })
      || std::any_of(
          unit.parameters.begin(),
          unit.parameters.end(),
          [&](const ParameterDeclaration& parameter) {
            return parameter.kind == ParameterKind::Type
                && parameter.name == canonical_name;
          });
  if (duplicate) {
    error(
        name,
        "FSIM-VHDL-SEM-036",
        "duplicate bounded type declaration '"
            + canonical_name + "'");
  }
  expect_keyword(
      "is", true, "FSIM-VHDL-PARSE-134");
  std::string resolution_function;
  if (at(TokenKind::Identifier)) {
    std::size_t lookahead = 1;
    while (at(TokenKind::Dot, lookahead)
           && at(TokenKind::Identifier, lookahead + 1)) {
      lookahead += 2;
    }
    if (at(TokenKind::Identifier, lookahead)
        && !keyword("range", lookahead, true)) {
      resolution_function = parse_vhdl_selected_name(
          "resolution function name");
    }
  }
  auto type = parse_vhdl_type(true, true);
  type.vhdl_resolution_function =
      std::move(resolution_function);
  type.vhdl_type_declaration =
      start.span.source_name + ":"
      + std::to_string(start.span.begin.offset) + ":"
      + canonical_name;
  expect(
      TokenKind::Semicolon,
      "';' after subtype declaration",
      "FSIM-VHDL-PARSE-135");
  if (duplicate) {
    return;
  }
  if (!nested_scope) {
    vhdl_named_types_.insert(canonical_name);
  }
  unit.type_aliases.push_back(TypeAliasDeclaration{
      canonical_name,
      std::move(type),
      span_from(start, previous()),
      {},
      TypeDeclarationKind::VhdlSubtype});
}

void VhdlParser::parse_signal_declaration(
    std::vector<SignalDeclaration>& signals,
    const std::vector<ParameterDeclaration>* constants) {
  const auto start = previous();
  std::vector<Token> names;
  names.push_back(expect_identifier("signal name"));
  while (match(TokenKind::Comma)) {
    names.push_back(expect_identifier("signal name"));
  }
  expect(TokenKind::Colon, "':' after signal name",
         "FSIM-VHDL-PARSE-017");
  const Type type = parse_vhdl_type(true, true);
  if (match(TokenKind::ColonEqual)) {
    const auto initializer = previous();
    (void)parse_expression();
    error(
        initializer,
        "FSIM-VHDL-UNSUPPORTED-012",
        "VHDL signal initializers are not executable in this frontend "
        "slice");
  }
  expect(TokenKind::Semicolon, "';' after signal declaration",
         "FSIM-VHDL-PARSE-018");
  for (const auto& name : names) {
    const auto canonical = vhdl_name(name.text);
    const auto duplicate = std::find_if(
        signals.begin(),
        signals.end(),
        [&](const SignalDeclaration& signal) {
          return signal.name == canonical;
        });
    const bool constant_conflict =
        constants != nullptr
        && std::any_of(
            constants->begin(),
            constants->end(),
            [&](const ParameterDeclaration& constant) {
              return constant.name == canonical;
            });
    if (constant_conflict) {
      error(
          name,
          "FSIM-VHDL-SEM-019",
          "generated object '" + canonical
              + "' is declared as both a signal and a constant");
    } else if (duplicate != signals.end()) {
      error(
          name,
          "FSIM-VHDL-SEM-003",
          "duplicate signal declaration '" + canonical + "'");
    } else {
      signals.push_back(SignalDeclaration{
          canonical,
          type,
          PortDirection::Unknown,
          false,
          span_from(start, previous())});
    }
  }
}

void VhdlParser::parse_vhdl_object_alias(
    std::vector<SignalAliasDeclaration>& aliases,
    const Token& start) {
  const auto name = expect_identifier("alias name");
  if (!match(TokenKind::Colon)) {
    error(
        name,
        "FSIM-VHDL-UNSUPPORTED-054",
        "bounded object aliases require an explicit subtype indication");
    skip_to_semicolon();
    return;
  }
  auto type = parse_vhdl_type(true, true);
  expect_keyword("is", true, "FSIM-VHDL-PARSE-236");
  const auto actual = expect_identifier("alias target");
  expect(
      TokenKind::Semicolon,
      "';' after alias declaration",
      "FSIM-VHDL-PARSE-236");
  const auto canonical = vhdl_name(name.text);
  if (std::ranges::any_of(
          aliases,
          [&](const auto& alias) {
            return alias.name == canonical;
          })) {
    error(
        name,
        "FSIM-VHDL-SEM-083",
        "duplicate bounded object alias '" + canonical + "'");
    return;
  }
  aliases.push_back(SignalAliasDeclaration{
      canonical,
      vhdl_name(actual.text),
      std::move(type),
      PortDirection::Unknown,
      span_from(start, previous())});
}

bool VhdlParser::parse_vhdl_local_nonobject_declaration(
    std::vector<ParameterDeclaration>& constants,
    std::vector<TypeAliasDeclaration>& type_aliases,
    std::vector<SignalAliasDeclaration>& signal_aliases,
    const std::vector<VariableDeclaration>& variables,
    std::vector<PackageInstantiation>& package_instances,
    std::vector<FunctionDeclaration>& functions,
    std::vector<ProcedureDeclaration>& procedures) {
  if (match_keyword("constant", true)) {
    GenerateBody declarations;
    declarations.constants = std::move(constants);
    declarations.type_aliases = std::move(type_aliases);
    declarations.signals.reserve(variables.size());
    for (const auto& variable : variables) {
      declarations.signals.push_back(SignalDeclaration{
          variable.name,
          variable.type,
          PortDirection::Unknown,
          false,
          variable.span});
    }
    parse_vhdl_generate_constant(
        declarations, previous());
    constants = std::move(declarations.constants);
    type_aliases = std::move(declarations.type_aliases);
    return true;
  }
  if (match_keyword("alias", true)) {
    parse_vhdl_object_alias(signal_aliases, previous());
    return true;
  }
  if (keyword("package", 0, true)
      && at(TokenKind::Identifier, 1)
      && keyword("is", 2, true)
      && keyword("new", 3, true)) {
    const auto package_start = advance();
    auto instance =
        parse_vhdl_package_instantiation(package_start);
    if (std::ranges::any_of(
            package_instances,
            [&](const auto& existing) {
              return existing.name == instance.name;
            })) {
      error(
          package_start,
          "FSIM-VHDL-SEM-059",
          "duplicate local package instance '"
              + instance.name + "'");
    } else {
      package_instances.push_back(std::move(instance));
    }
    return true;
  }
  if (keyword("pure", 0, true)
      || keyword("impure", 0, true)) {
    const auto pure = keyword("pure", 0, true);
    (void)advance();
    expect_keyword("function", true, "FSIM-VHDL-PARSE-235");
    functions.push_back(
        parse_vhdl_function(previous(), pure, true));
    return true;
  }
  if (match_keyword("function", true)) {
    functions.push_back(
        parse_vhdl_function(previous(), true, true));
    return true;
  }
  if (match_keyword("procedure", true)) {
    procedures.push_back(
        parse_vhdl_procedure(previous(), true));
    return true;
  }
  if (!match_keyword("type", true)
      && !match_keyword("subtype", true)) {
    return false;
  }
  const auto declaration = previous();
  const auto subtype =
      vhdl_name(declaration.text) == "subtype";
  DesignUnit declarations;
  declarations.type_aliases = std::move(type_aliases);
  if (subtype) {
    parse_subtype_declaration(
        declarations, declaration, true);
  } else {
    parse_type_declaration(
        declarations, declaration, true);
  }
  type_aliases = std::move(declarations.type_aliases);
  return true;
}

void VhdlParser::validate_vhdl_local_declaration_names(
    const std::vector<ParameterDeclaration>& constants,
    const std::vector<TypeAliasDeclaration>& type_aliases,
    const std::vector<SignalAliasDeclaration>& signal_aliases,
    const std::vector<VariableDeclaration>& variables,
    const std::vector<PackageInstantiation>& package_instances,
    const std::vector<FunctionDeclaration>& functions,
    const std::vector<ProcedureDeclaration>& procedures) {
  struct LocalName {
    std::string name;
    std::string family;
    SourceSpan span;
    bool callable{};
  };
  std::vector<LocalName> names;
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
  append(constants, "constant");
  append(type_aliases, "type");
  append(signal_aliases, "alias");
  append(variables, "variable");
  append(package_instances, "package");
  append(functions, "function", true);
  append(procedures, "procedure", true);
  std::ranges::stable_sort(
      names, {}, [](const auto& declaration) {
        return declaration.span.begin.offset;
      });
  std::unordered_map<std::string, LocalName> prior;
  for (const auto& declaration : names) {
    const auto [found, inserted] =
        prior.try_emplace(declaration.name, declaration);
    if (inserted || found->second.family == declaration.family
        || (found->second.callable && declaration.callable)) {
      continue;
    }
    error(
        Token{
            TokenKind::Identifier,
            declaration.name,
            declaration.span,
            {}},
        "FSIM-VHDL-SEM-082",
        "local " + declaration.family + " declaration '"
            + declaration.name + "' conflicts with prior "
            + found->second.family + " declaration");
  }
}

void VhdlParser::parse_concurrent_statement(DesignUnit& unit) {
  std::optional<Token> label_token;
  if (at(TokenKind::Identifier) && at(TokenKind::Colon, 1)) {
    label_token = advance();
    advance();
  }

  if (keyword("process", 0, true)) {
    unit.processes.push_back(parse_process(
        label_token ? vhdl_name(label_token->text) : std::string{}));
    return;
  }
  if (match_keyword("assert", true)) {
    auto statement = parse_vhdl_assertion(previous());
    if (label_token) {
      statement.label = vhdl_name(label_token->text);
    }
    unit.concurrent_statements.push_back(std::move(statement));
    return;
  }
  if (match_keyword("with", true)) {
    auto statement = parse_vhdl_selected_assignment(previous());
    if (label_token) {
      statement.label = vhdl_name(label_token->text);
    }
    unit.concurrent_statements.push_back(std::move(statement));
    return;
  }
  if (label_token && match_keyword("if", true)) {
    unit.generate_regions.push_back(
        parse_vhdl_conditional_generate(
            *label_token, previous()));
    return;
  }
  if (label_token && match_keyword("for", true)) {
    unit.generate_regions.push_back(
        parse_vhdl_iterative_generate(
            *label_token, previous()));
    return;
  }
  if (label_token && match_keyword("case", true)) {
    unit.generate_regions.push_back(
        parse_vhdl_selection_generate(
            *label_token, previous()));
    return;
  }
  if (label_token && match_keyword("block", true)) {
    unit.generate_regions.push_back(
        parse_vhdl_static_block(
            *label_token, previous()));
    return;
  }
  if (label_token &&
      (keyword("entity", 0, true) ||
       keyword("configuration", 0, true) ||
       (at(TokenKind::Identifier) &&
        (keyword("port", 1, true) ||
         keyword("generic", 1, true))))) {
    unit.instances.push_back(parse_vhdl_instance(*label_token));
    return;
  }
  if (label_token) {
    error(previous(), "FSIM-VHDL-UNSUPPORTED-005",
          "this labeled concurrent statement is not supported");
  }

  const auto before = position();
  auto statement = parse_assignment(true);
  if (statement) {
    unit.concurrent_statements.push_back(std::move(*statement));
    return;
  }
  rewind(before);
  const auto unexpected = advance();
  error(unexpected, "FSIM-VHDL-UNSUPPORTED-006",
        "unsupported concurrent statement starting with '" +
            unexpected.text + "'");
  skip_to_semicolon();
}

}  // namespace fsim::frontend
