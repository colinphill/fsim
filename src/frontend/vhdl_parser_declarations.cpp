// SPDX-License-Identifier: Apache-2.0
#include "vhdl_parser_internal.hpp"

#include <unordered_map>

namespace fsim::frontend {

void VhdlParser::parse_type_declaration(DesignUnit &unit, const Token &start,
                                        const bool nested_scope) {
  const auto name = expect_identifier("type name");
  const auto canonical_name = vhdl_name(name.text);
  auto prior_declaration =
      std::find_if(unit.type_aliases.begin(), unit.type_aliases.end(),
                   [&](const TypeAliasDeclaration &declaration) {
                     return declaration.name == canonical_name;
                   });
  const bool incomplete_declaration = at(TokenKind::Semicolon);
  const bool completing_incomplete =
      !incomplete_declaration && prior_declaration != unit.type_aliases.end() &&
      prior_declaration->declaration_kind ==
          TypeDeclarationKind::VhdlIncomplete;
  const bool duplicate =
      (!nested_scope && vhdl_named_types_.contains(canonical_name) &&
       !completing_incomplete) ||
      (prior_declaration != unit.type_aliases.end() &&
       !completing_incomplete) ||
      std::any_of(unit.parameters.begin(), unit.parameters.end(),
                  [&](const ParameterDeclaration &parameter) {
                    return parameter.kind == ParameterKind::Type &&
                           parameter.name == canonical_name;
                  });
  if (duplicate) {
    error(name, "FSIM-VHDL-SEM-036",
          "duplicate bounded type declaration '" + canonical_name + "'");
  }
  if (incomplete_declaration) {
    advance();
    if (!duplicate) {
      Type type;
      type.spelling = canonical_name;
      type.nominal_type = start.span.source_name + ":" +
                          std::to_string(start.span.begin.offset) + ":" +
                          canonical_name;
      type.vhdl_type_declaration = type.nominal_type;
      if (!nested_scope) {
        vhdl_named_types_.insert(canonical_name);
      }
      unit.type_aliases.push_back(
          TypeAliasDeclaration { canonical_name,
              std::move(type),
              span_from(start, previous()),
              { },
              TypeDeclarationKind::VhdlIncomplete,
              { } });
    }
    return;
  }
  if (completing_incomplete) {
    unit.type_aliases.erase(prior_declaration);
  }
  expect_keyword("is", true, "FSIM-VHDL-PARSE-127");
  if (match_keyword("file", true)) {
    expect_keyword("of", true, "FSIM-VHDL-PARSE-254");
    const auto element_start = current();
    auto element_type = parse_vhdl_type(true, true);
    const auto element_span = cover(element_start.span, previous().span);
    expect(TokenKind::Semicolon, "';' after file type declaration",
           "FSIM-VHDL-PARSE-255");

    Type type;
    type.spelling = canonical_name;
    type.nominal_type = start.span.source_name + ":" +
                        std::to_string(start.span.begin.offset) + ":" +
                        canonical_name;
    type.vhdl_type_declaration = type.nominal_type;
    VhdlFileInfo file;
    file.element_span = element_span;
    file.element_types.push_back(std::move(element_type));
    type.vhdl_file = std::move(file);
    if (!duplicate) {
      if (!nested_scope) {
        vhdl_named_types_.insert(canonical_name);
      }
      unit.type_aliases.push_back(
          TypeAliasDeclaration { canonical_name,
              std::move(type),
              span_from(start, previous()),
              { },
              TypeDeclarationKind::VhdlFile,
              { } });
    }
    return;
  }
  if (match_keyword("access", true)) {
    const auto designated_start = current();
    auto designated_type = parse_vhdl_type(true, true);
    const auto designated_span = cover(designated_start.span, previous().span);
    expect(TokenKind::Semicolon, "';' after access type declaration",
           "FSIM-VHDL-PARSE-237");

    Type type;
    type.spelling = canonical_name;
    type.nominal_type = start.span.source_name + ":" +
                        std::to_string(start.span.begin.offset) + ":" +
                        canonical_name;
    type.vhdl_type_declaration = type.nominal_type;
    VhdlAccessInfo access;
    access.designated_span = designated_span;
    access.designated_types.push_back(std::move(designated_type));
    type.vhdl_access = std::move(access);
    if (!duplicate) {
      if (!nested_scope) {
        vhdl_named_types_.insert(canonical_name);
      }
      unit.type_aliases.push_back(
          TypeAliasDeclaration { canonical_name,
              std::move(type),
              span_from(start, previous()),
              { },
              TypeDeclarationKind::VhdlAccess,
              { } });
    }
    return;
  }
  if (match_keyword("range", true)) {
    const auto range_start = previous();
    auto left_expression = parse_expression();
    bool descending = false;
    if (match_keyword("downto", true)) {
      descending = true;
    } else if (!match_keyword("to", true)) {
      error(current(), "FSIM-VHDL-PARSE-238",
            "expected 'to' or 'downto' in physical type range");
    }
    auto right_expression = parse_expression();
    const auto range_span = cover(range_start.span, previous().span);
    expect_keyword("units", true, "FSIM-VHDL-PARSE-239");

    VhdlPhysicalInfo physical;
    physical.range = DiscreteRangeExpression{std::move(left_expression),
                                             std::move(right_expression),
                                             range_span, descending};
    std::unordered_set<std::string> unit_names;
    while (!at_end() &&
           !(keyword("end", 0, true) && keyword("units", 1, true))) {
      const auto unit_start = current();
      const auto unit_name = expect_identifier("physical unit name");
      const auto canonical_unit = vhdl_name(unit_name.text);
      if (!unit_names.insert(canonical_unit).second) {
        error(unit_name, "FSIM-VHDL-SEM-088",
              "duplicate physical unit '" + canonical_unit + "'");
      }
      std::optional<Expression> scale;
      if (match(TokenKind::Assign)) {
        scale = parse_expression();
      } else if (!physical.units.empty()) {
        error(current(), "FSIM-VHDL-PARSE-240",
              "a secondary physical unit requires '= physical_literal'");
      }
      expect(TokenKind::Semicolon, "';' after physical unit declaration",
             "FSIM-VHDL-PARSE-241");
      physical.units.push_back(
          VhdlPhysicalUnit{canonical_unit, std::move(scale), std::nullopt,
                           span_from(unit_start, previous())});
    }
    if (physical.units.empty()) {
      error(current(), "FSIM-VHDL-SEM-089",
            "a physical type requires a primary unit");
    }
    expect_keyword("end", true, "FSIM-VHDL-PARSE-242");
    expect_keyword("units", true, "FSIM-VHDL-PARSE-243");
    if (at(TokenKind::Identifier)) {
      const auto end_name = advance();
      if (vhdl_name(end_name.text) != canonical_name) {
        error(end_name, "FSIM-VHDL-SEM-090",
              "physical type end name '" + vhdl_name(end_name.text) +
                  "' does not match '" + canonical_name + "'");
      }
    }
    expect(TokenKind::Semicolon, "';' after physical type declaration",
           "FSIM-VHDL-PARSE-244");

    Type type;
    type.spelling = canonical_name;
    type.nominal_type = start.span.source_name + ":" +
                        std::to_string(start.span.begin.offset) + ":" +
                        canonical_name;
    type.vhdl_type_declaration = type.nominal_type;
    type.vhdl_physical = std::move(physical);
    if (!duplicate) {
      if (!nested_scope) {
        vhdl_named_types_.insert(canonical_name);
      }
      unit.type_aliases.push_back(
          TypeAliasDeclaration { canonical_name,
              std::move(type),
              span_from(start, previous()),
              { },
              TypeDeclarationKind::VhdlPhysical,
              { } });
    }
    return;
  }
  if (match_keyword("protected", true)) {
      require_vhdl_standard(
          previous(), VhdlStandard::Vhdl2000, "a protected type declaration",
          "select VHDL-2000 or replace the protected object with an ordinary "
          "package-managed declaration");
      const bool body = match_keyword("body", true);
      DesignUnit protected_region;
      protected_region.language = Language::Vhdl2008;
      std::vector<VariableDeclaration> variables;
      while (!at_end() && !(keyword("end", 0, true) && keyword("protected", 1, true))) {
          if ((keyword("pure", 0, true) || keyword("impure", 0, true)) && keyword("function", 1, true)) {
              const bool pure = match_keyword("pure", true);
              if (!pure) {
                  (void)match_keyword("impure", true);
              }
              const auto function_start = expect_keyword("function", true);
              parse_vhdl_function_item(protected_region, function_start, pure, !body);
              continue;
          }
          if (match_keyword("function", true)) {
              parse_vhdl_function_item(protected_region, previous(), true, !body);
              continue;
          }
          if (match_keyword("procedure", true)) {
              parse_vhdl_procedure_item(protected_region, previous(), !body);
              continue;
          }
          if (body && match_keyword("variable", true)) {
              const auto variable_start = previous();
              std::vector<Token> names { expect_identifier("protected variable name") };
              while (match(TokenKind::Comma)) {
                  names.push_back(expect_identifier("protected variable name"));
              }
              expect(TokenKind::Colon, "':' after protected variable names",
                  "FSIM-VHDL-PARSE-245");
              const auto variable_type = parse_vhdl_type(true, true);
              std::optional<Expression> initializer;
              if (match(TokenKind::ColonEqual)) {
                  initializer = parse_expression();
              }
              expect(TokenKind::Semicolon, "';' after protected variable declaration",
                  "FSIM-VHDL-PARSE-246");
              for (const auto& variable_name : names) {
                  const auto canonical_variable = vhdl_name(variable_name.text);
                  if (std::ranges::any_of(variables, [&](const auto& existing) {
                          return existing.name == canonical_variable;
                      })) {
                      error(variable_name, "FSIM-VHDL-SEM-091",
                          "duplicate protected variable '" + canonical_variable + "'");
                      continue;
                  }
                  variables.push_back(VariableDeclaration {
                      canonical_variable, variable_type, initializer,
                      span_from(variable_start, previous()) });
              }
              continue;
          }
          const auto item = advance();
          error(item, "FSIM-VHDL-UNSUPPORTED-055",
              "unsupported protected type declarative item '" + item.text + "'");
          skip_to_semicolon();
      }
    expect_keyword("end", true, "FSIM-VHDL-PARSE-247");
    expect_keyword("protected", true, "FSIM-VHDL-PARSE-248");
    if (body) {
      expect_keyword("body", true, "FSIM-VHDL-PARSE-249");
    }
    if (at(TokenKind::Identifier)) {
      const auto end_name = advance();
      if (vhdl_name(end_name.text) != canonical_name) {
        error(end_name, "FSIM-VHDL-SEM-092",
              "protected type end name '" + vhdl_name(end_name.text) +
                  "' does not match '" + canonical_name + "'");
      }
    }
    expect(TokenKind::Semicolon, "';' after protected type declaration",
           "FSIM-VHDL-PARSE-250");

    Type type;
    type.spelling = canonical_name;
    type.nominal_type = start.span.source_name + ":" +
                        std::to_string(start.span.begin.offset) + ":" +
                        canonical_name;
    type.vhdl_type_declaration = type.nominal_type;
    type.vhdl_protected = std::make_shared<VhdlProtectedInfo>(
        VhdlProtectedInfo{body,
                          false,
                          false,
                          std::move(variables),
                          std::move(protected_region.functions),
                          std::move(protected_region.procedures),
                          {},
                          0,
                          span_from(start, previous())});
    if (!duplicate) {
      if (!nested_scope && !body) {
        vhdl_named_types_.insert(canonical_name);
      }
      unit.type_aliases.push_back(
          TypeAliasDeclaration { canonical_name,
              std::move(type),
              span_from(start, previous()),
              { },
              body ? TypeDeclarationKind::VhdlProtectedBody
                   : TypeDeclarationKind::VhdlProtected,
              { } });
    }
    return;
  }
  if (match_keyword("array", true)) {
    Type type;
    type.spelling = canonical_name;
    type.nominal_type = start.span.source_name + ":" +
                        std::to_string(start.span.begin.offset) + ":" +
                        canonical_name;
    type.vhdl_type_declaration = type.nominal_type;
    VhdlArrayInfo array;

    expect(TokenKind::LeftParen, "'(' after array", "FSIM-VHDL-PARSE-143");
    constexpr auto integer_first =
        std::int64_t{std::numeric_limits<std::int32_t>::min()};
    constexpr auto integer_last =
        std::int64_t{std::numeric_limits<std::int32_t>::max()};
    do {
      VhdlArrayDimension dimension;
      const auto index_start = current();
      if (at(TokenKind::Identifier) && keyword("range", 1, true)) {
        const auto index_type = advance();
        dimension.index_subtype = vhdl_name(index_type.text);
        dimension.index_span = index_type.span;
        match_keyword("range", true);
        if (match(TokenKind::Less)) {
          dimension.unconstrained = true;
          expect(TokenKind::Greater, "'>' in unconstrained array index '<>'",
                 "FSIM-VHDL-PARSE-144");
        }
      } else {
        dimension.index_subtype = "integer";
        dimension.index_span = index_start.span;
      }

      if (dimension.index_subtype == "integer") {
        dimension.index_base_range =
            IntegerRange{integer_first, integer_last, false};
      } else if (dimension.index_subtype == "natural") {
        dimension.index_base_range = IntegerRange{0, integer_last, false};
      } else if (dimension.index_subtype == "positive") {
        dimension.index_base_range = IntegerRange{1, integer_last, false};
      } else {
        error(index_start, "FSIM-VHDL-UNSUPPORTED-027",
              "VHDL arrays currently require integer, natural, or "
              "positive index subtypes");
      }

      if (!dimension.unconstrained) {
        auto left_expression = parse_expression();
        bool descending = false;
        if (match_keyword("downto", true)) {
          descending = true;
        } else if (!match_keyword("to", true)) {
          error(current(), "FSIM-VHDL-PARSE-145",
                "expected 'to' or 'downto' in array index range");
        }
        auto right_expression = parse_expression();
        dimension.constraint = DiscreteRangeExpression{
            std::move(left_expression), std::move(right_expression),
            cover(index_start.span, previous().span), descending};
      }
      array.dimensions.push_back(std::move(dimension));
    } while (match(TokenKind::Comma));
    expect(TokenKind::RightParen, "')' after array index definition",
           "FSIM-VHDL-PARSE-146");
    expect_keyword("of", true, "FSIM-VHDL-PARSE-147");
    const auto element_start = current();
    const auto element_type = parse_vhdl_type(true, true);
    const auto element_simple_name = element_type.spelling.substr(
        element_type.spelling.find_last_of('.') == std::string::npos
            ? 0
            : element_type.spelling.find_last_of('.') + 1);
    const bool unconstrained_builtin_element = !element_type.packed_range && (element_simple_name == "bit_vector" || element_simple_name == "std_logic_vector" || element_simple_name == "std_ulogic_vector" || element_simple_name == "signed" || element_simple_name == "unsigned" || element_simple_name == "string");
    if (unconstrained_builtin_element) {
        require_vhdl_standard(
            element_start, VhdlStandard::Vhdl2008,
            "an unconstrained array element subtype",
            "select VHDL-2008 or constrain the element subtype in the array "
            "declaration");
    }
    const bool unresolved_element = !element_type.named_type.empty();
    const auto element_width = element_type.width();
    const bool scalar_element =
        unresolved_element || (element_width && *element_width == 1 &&
                               element_type.packed_members.empty() &&
                               element_type.enumeration_literals.empty() &&
                               element_type.domain != ValueDomain::Integer &&
                               element_type.domain != ValueDomain::Unknown);
    expect(TokenKind::Semicolon, "';' after array type declaration",
           "FSIM-VHDL-PARSE-148");

    const auto &first_dimension = array.dimensions.front();
    array.index_subtype = first_dimension.index_subtype;
    array.index_span = first_dimension.index_span;
    array.index_base_range = first_dimension.index_base_range;
    array.element_spelling = element_type.spelling;
    array.element_named_type = element_type.named_type;
    array.element_span = element_type.named_type_span.source_name.empty()
                             ? element_start.span
                             : element_type.named_type_span;
    array.element_domain = element_type.domain;
    array.unconstrained = std::ranges::any_of(
        array.dimensions, [](const VhdlArrayDimension &dimension) {
          return dimension.unconstrained;
        });
    array.element_types.push_back(element_type);
    const bool legacy_scalar_array =
        array.dimensions.size() == 1 && scalar_element;
    type.domain =
        legacy_scalar_array ? element_type.domain : ValueDomain::Unknown;
    if (legacy_scalar_array && first_dimension.constraint) {
      const auto &constraint = *first_dimension.constraint;
      const auto left = simple_vhdl_integer_constant(constraint.left);
      const auto right = simple_vhdl_integer_constant(constraint.right);
      if (left && right) {
        type.packed_range = PackedRange{*left, *right, constraint.descending};
      }
      type.packed_range_expression =
          PackedRangeExpression{constraint.left, constraint.right,
                                constraint.span, constraint.descending};
    }
    type.vhdl_array = std::move(array);
    if (!duplicate) {
      if (!nested_scope) {
        vhdl_named_types_.insert(canonical_name);
      }
      unit.type_aliases.push_back(
          TypeAliasDeclaration { canonical_name,
              std::move(type),
              span_from(start, previous()),
              { },
              TypeDeclarationKind::VhdlArray,
              { } });
    }
    return;
  }
  if (match(TokenKind::LeftParen)) {
    Type type;
    type.spelling = canonical_name;
    type.domain = ValueDomain::Bit2;
    type.nominal_type = start.span.source_name + ":" +
                        std::to_string(start.span.begin.offset) + ":" +
                        canonical_name;
    type.vhdl_type_declaration = type.nominal_type;
    std::vector<EnumLiteralDeclaration> literals;
    std::unordered_set<std::string> literal_names;
    while (!at_end() && !at(TokenKind::RightParen) &&
           !at(TokenKind::Semicolon)) {
      if (!at(TokenKind::Identifier) && !at(TokenKind::CharacterLiteral)) {
        error(current(), "FSIM-VHDL-PARSE-136",
              "an enumeration literal must be an identifier or character "
              "literal");
        advance();
      } else {
        const auto literal = advance();
        const auto literal_name = literal.kind == TokenKind::Identifier
                                      ? vhdl_name(literal.text)
                                      : literal.text;
        if (!literal_names.insert(literal_name).second) {
          error(literal, "FSIM-VHDL-SEM-040",
                "duplicate enumeration literal '" + literal_name + "'");
        } else {
          type.enumeration_literals.push_back(literal_name);
          literals.push_back(
              EnumLiteralDeclaration{literal_name,
                                     Expression{ExpressionKind::IntegerLiteral,
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
        error(current(), "FSIM-VHDL-PARSE-137",
              "expected ',' between enumeration literals");
        while (!at_end() && !at(TokenKind::Comma) &&
               !at(TokenKind::RightParen) && !at(TokenKind::Semicolon)) {
          advance();
        }
        (void)match(TokenKind::Comma);
      } else if (at(TokenKind::RightParen)) {
        error(previous(), "FSIM-VHDL-PARSE-138",
              "an enumeration declaration cannot end with ','");
      }
    }
    if (type.enumeration_literals.empty()) {
      error(name, "FSIM-VHDL-PARSE-139",
            "an enumeration type requires at least one literal");
    }
    expect(TokenKind::RightParen, "')' after enumeration literals",
           "FSIM-VHDL-PARSE-140");
    expect(TokenKind::Semicolon, "';' after enumeration type declaration",
           "FSIM-VHDL-PARSE-141");

    std::uint64_t maximum_ordinal = type.enumeration_literals.empty()
                                        ? 0
                                        : type.enumeration_literals.size() - 1U;
    std::uint64_t width = 1;
    while (maximum_ordinal > 1U) {
      ++width;
      maximum_ordinal >>= 1U;
    }
    type.packed_range =
        PackedRange{static_cast<std::int64_t>(width - 1U), 0, true};
    if (!type.enumeration_literals.empty()) {
      type.enumeration_range = EnumerationRange{
          0, static_cast<std::int64_t>(type.enumeration_literals.size() - 1U),
          false};
    }
    if (!duplicate && !type.enumeration_literals.empty()) {
      if (!nested_scope) {
        vhdl_named_types_.insert(canonical_name);
      }
      unit.type_aliases.push_back(TypeAliasDeclaration {
          canonical_name, std::move(type), span_from(start, previous()),
          std::move(literals), TypeDeclarationKind::VhdlEnumeration, { } });
    }
    return;
  }
  if (!match_keyword("record", true)) {
    error(current(), "FSIM-VHDL-UNSUPPORTED-026",
          "bounded VHDL type declarations require a record definition");
    skip_to_semicolon();
    return;
  }

  Type type;
  type.spelling = canonical_name;
  type.domain = ValueDomain::Bit2;
  type.packed_aggregate = PackedAggregateKind::Struct;
  type.nominal_type = start.span.source_name + ":" +
                      std::to_string(start.span.begin.offset) + ":" +
                      canonical_name;
  type.vhdl_type_declaration = type.nominal_type;
  std::unordered_set<std::string> member_names;
  while (!at_end() &&
         !(keyword("end", 0, true) && keyword("record", 1, true))) {
    const auto member_start = current();
    std::vector<Token> names;
    names.push_back(expect_identifier("record element name"));
    while (match(TokenKind::Comma)) {
      names.push_back(expect_identifier("record element name"));
    }
    expect(TokenKind::Colon, "':' after record element names",
           "FSIM-VHDL-PARSE-128");
    const auto member_type = parse_vhdl_type(true, true);
    const bool supported = !member_type.named_type.empty() ||
                           member_type.domain != ValueDomain::Unknown;
    if (!supported) {
      error(member_start, "FSIM-VHDL-UNSUPPORTED-026",
            "a VHDL record element requires a scalar, integer, statically "
            "ranged vector, or named complete or incomplete subtype");
    }
    expect(TokenKind::Semicolon, "';' after record element declaration",
           "FSIM-VHDL-PARSE-129");
    for (const auto &member_name : names) {
      const auto canonical_member = vhdl_name(member_name.text);
      if (!member_names.insert(canonical_member).second) {
        error(member_name, "FSIM-VHDL-SEM-035",
              "duplicate record element '" + canonical_member + "'");
        continue;
      }
      if (!supported) {
        continue;
      }
      type.packed_members.push_back(PackedMember{
          canonical_member, member_type.domain, member_type.spelling,
          member_type.packed_range, member_type.is_signed,
          member_type.packed_range_expression, 0,
          cover(member_start.span, previous().span),
          member_type.named_type.empty() ? std::vector<Type>{}
                                         : std::vector<Type>{member_type},
          std::nullopt});
      if (member_type.domain == ValueDomain::Logic9) {
        type.domain = ValueDomain::Logic9;
      } else if (member_type.domain == ValueDomain::Logic4 &&
                 type.domain != ValueDomain::Logic9) {
        type.domain = ValueDomain::Logic4;
      }
    }
  }
  if (type.packed_members.empty()) {
    error(name, "FSIM-VHDL-PARSE-130",
          "a bounded VHDL record type requires at least one supported "
          "element");
  }
  expect_keyword("end", true, "FSIM-VHDL-PARSE-131");
  expect_keyword("record", true, "FSIM-VHDL-PARSE-131");
  if (at(TokenKind::Identifier)) {
    const auto end_name = advance();
    if (vhdl_name(end_name.text) != canonical_name) {
      error(end_name, "FSIM-VHDL-SEM-037",
            "record end name '" + vhdl_name(end_name.text) +
                "' does not match type name '" + canonical_name + "'");
    }
  }
  expect(TokenKind::Semicolon, "';' after record type declaration",
         "FSIM-VHDL-PARSE-131");

  std::uint64_t total_width = 0;
  bool concrete = !type.packed_members.empty();
  for (const auto &member : type.packed_members) {
    const auto width = member.width();
    if (!width || *width == 0 ||
        *width > std::numeric_limits<std::uint64_t>::max() - total_width) {
      concrete = false;
      break;
    }
    total_width += *width;
  }
  if (concrete && total_width != 0 &&
      total_width - 1U <= static_cast<std::uint64_t>(
                              std::numeric_limits<std::int64_t>::max())) {
    auto offset = total_width;
    for (auto &member : type.packed_members) {
      offset -= *member.width();
      member.lsb_offset = offset;
    }
    type.packed_range =
        PackedRange{static_cast<std::int64_t>(total_width - 1U), 0, true};
  }

  if (!duplicate) {
    if (!nested_scope) {
      vhdl_named_types_.insert(canonical_name);
    }
    unit.type_aliases.push_back(
        TypeAliasDeclaration { canonical_name,
            std::move(type),
            span_from(start, previous()),
            { },
            TypeDeclarationKind::VhdlRecord,
            { } });
  }
}

void VhdlParser::parse_vhdl_shared_variable(DesignUnit &unit,
                                            const Token &start) {
    require_vhdl_standard(
        start, VhdlStandard::Vhdl1993, "a shared variable declaration",
        "select VHDL-1993 or use a process-local ordinary variable");
    expect_keyword("variable", true, "FSIM-VHDL-PARSE-251");
    std::vector<Token> names { expect_identifier("shared variable name") };
    while (match(TokenKind::Comma)) {
        names.push_back(expect_identifier("shared variable name"));
    }
  expect(TokenKind::Colon, "':' after shared variable names",
         "FSIM-VHDL-PARSE-252");
  const auto type = parse_vhdl_type(true, true);
  std::optional<Expression> initializer;
  if (match(TokenKind::ColonEqual)) {
    initializer = parse_expression();
  }
  expect(TokenKind::Semicolon, "';' after shared variable declaration",
         "FSIM-VHDL-PARSE-253");
  for (const auto &name : names) {
    const auto canonical = vhdl_name(name.text);
    const bool duplicate =
        std::ranges::any_of(
            unit.variables,
            [&](const auto &variable) { return variable.name == canonical; }) ||
        std::ranges::any_of(
            unit.signals,
            [&](const auto &signal) { return signal.name == canonical; }) ||
        std::ranges::any_of(unit.parameters, [&](const auto &parameter) {
          return parameter.name == canonical;
        });
    if (duplicate) {
      error(name, "FSIM-VHDL-SEM-093",
            "duplicate shared variable declaration '" + canonical + "'");
      continue;
    }
    unit.variables.push_back(VariableDeclaration{
        canonical, type, initializer, span_from(start, previous()), true});
  }
}

void VhdlParser::parse_vhdl_file_declaration(
    std::vector<VariableDeclaration> &files, const Token &start) {
  std::vector<Token> names{expect_identifier("file object name")};
  while (match(TokenKind::Comma)) {
    names.push_back(expect_identifier("file object name"));
  }
  expect(TokenKind::Colon, "':' after file object names",
         "FSIM-VHDL-PARSE-256");
  const auto type = parse_vhdl_type(true, true);

  std::optional<Expression> open_kind;
  std::optional<Expression> logical_name;
  const bool has_open = match_keyword("open", true);
  if (has_open) {
      require_vhdl_standard(
          previous(), VhdlStandard::Vhdl1993,
          "an explicit file open-kind expression",
          "select VHDL-1993 or use the VHDL-1987 'is in'/'is out' file mode "
          "form");
      if (keyword("is", 0, true) || at(TokenKind::Semicolon)) {
          error(current(), "FSIM-VHDL-PARSE-257",
              "expected a file open-kind expression after 'open'");
      } else {
          open_kind = parse_expression();
      }
  }
  if (match_keyword("is", true)) {
      if (keyword("in", 0, true) || keyword("out", 0, true)) {
          const auto legacy_mode = advance();
          if (vhdl_standard_ != VhdlStandard::Vhdl1987) {
              error(legacy_mode, "FSIM-FE-VHSTD-003",
                  "the VHDL-1987 file mode '" + vhdl_name(legacy_mode.text) + "' is unavailable in VHDL-" + std::string(to_string(vhdl_standard_)) + "; replace it with 'open " + (detail::iequals(legacy_mode.text, "in") ? "read_mode" : "write_mode") + " is'");
          }
          open_kind = Expression {
              ExpressionKind::Identifier,
              detail::iequals(legacy_mode.text, "in") ? "read_mode"
                                                      : "write_mode",
              { }, legacy_mode.span
          };
      }
    if (at(TokenKind::Semicolon)) {
      error(current(), "FSIM-VHDL-PARSE-258",
            "expected a file logical-name expression after 'is'");
    } else {
      logical_name = parse_expression();
    }
  } else if (has_open) {
    error(current(), "FSIM-VHDL-PARSE-259",
          "a file open-kind expression requires 'is' and a logical name");
  }
  expect(TokenKind::Semicolon, "';' after file object declaration",
         "FSIM-VHDL-PARSE-260");

  for (const auto &name : names) {
    const auto canonical = vhdl_name(name.text);
    if (std::ranges::any_of(files, [&](const auto &existing) {
          return existing.name == canonical;
        })) {
      error(name, "FSIM-VHDL-SEM-094",
            "duplicate file object declaration '" + canonical + "'");
      continue;
    }
    VariableDeclaration file{canonical, type, logical_name,
                             span_from(start, previous())};
    file.vhdl_file = true;
    file.vhdl_file_open_kind = open_kind;
    files.push_back(std::move(file));
  }
}

void VhdlParser::parse_vhdl_attribute_declaration(DesignUnit &unit,
                                                  const Token &start) {
  const auto name_token = expect_identifier("attribute name");
  const auto name = vhdl_name(name_token.text);
  if (match(TokenKind::Colon)) {
    auto type = parse_vhdl_type(true, true);
    expect(TokenKind::Semicolon, "';' after attribute declaration",
           "FSIM-VHDL-PARSE-265");
    if (std::ranges::any_of(unit.vhdl_attributes, [&](const auto &attribute) {
          return !attribute.specification && attribute.name == name;
        })) {
      error(name_token, "FSIM-VHDL-SEM-098",
            "duplicate VHDL attribute declaration '" + name + "'");
      return;
    }
    unit.vhdl_attributes.push_back(
        VhdlAttributeDeclaration{name,
                                 std::move(type),
                                 false,
                                 {},
                                 {},
                                 {},
                                 span_from(start, previous())});
    return;
  }
  if (!match_keyword("of", true)) {
    error(current(), "FSIM-VHDL-PARSE-265",
          "a VHDL attribute declaration requires ':' and a subtype, or 'of' "
          "and an entity specification");
    skip_to_semicolon();
    return;
  }

  std::vector<std::string> entity_names;
  std::string entity_name;
  while (!at_end() && !at(TokenKind::Colon) && !at(TokenKind::Semicolon)) {
    if (match(TokenKind::Comma)) {
      if (entity_name.empty()) {
        error(previous(), "FSIM-VHDL-PARSE-266",
              "an attribute entity-name list cannot contain an empty item");
      } else {
        entity_names.push_back(std::move(entity_name));
        entity_name.clear();
      }
      continue;
    }
    const auto token = advance();
    entity_name += token.kind == TokenKind::Identifier ? vhdl_name(token.text)
                                                       : token.text;
  }
  if (!entity_name.empty()) {
    entity_names.push_back(std::move(entity_name));
  }
  if (entity_names.empty()) {
    error(current(), "FSIM-VHDL-PARSE-266",
          "an attribute specification requires at least one entity name");
  }
  expect(TokenKind::Colon, "':' before attribute entity class",
         "FSIM-VHDL-PARSE-266");
  const auto entity_class = expect_identifier("attribute entity class");
  expect_keyword("is", true, "FSIM-VHDL-PARSE-267");
  auto value = parse_expression();
  expect(TokenKind::Semicolon, "';' after attribute specification",
         "FSIM-VHDL-PARSE-265");
  if (!std::ranges::any_of(unit.vhdl_attributes, [&](const auto &attribute) {
        return !attribute.specification && attribute.name == name;
      })) {
    error(name_token, "FSIM-VHDL-SEM-099",
          "VHDL attribute specification references undeclared attribute '" +
              name + "'");
  }
  unit.vhdl_attributes.push_back(
      VhdlAttributeDeclaration{name,
                               {},
                               true,
                               std::move(entity_names),
                               vhdl_name(entity_class.text),
                               std::move(value),
                               span_from(start, previous())});
}

void VhdlParser::parse_vhdl_group_declaration(DesignUnit &unit,
                                               const Token &start) {
    require_vhdl_standard(
        start, VhdlStandard::Vhdl1993, "a group declaration",
        "select VHDL-1993 or remove the group template or instance");
    const auto name_token = expect_identifier("group name");
    const auto name = vhdl_name(name_token.text);
    const bool template_declaration = match_keyword("is", true);
    std::string template_name;
    if (!template_declaration) {
        if (!match(TokenKind::Colon)) {
            error(current(), "FSIM-VHDL-PARSE-268",
                "a VHDL group requires 'is' for a template or ':' for an "
                "instance");
            skip_to_semicolon();
            return;
        }
        template_name = parse_vhdl_selected_name("group template name");
    }

  expect(TokenKind::LeftParen, "'(' before VHDL group entries",
         "FSIM-VHDL-PARSE-269");
  std::vector<std::string> entries;
  std::string entry;
  std::size_t nested = 0;
  while (!at_end() && !(at(TokenKind::RightParen) && nested == 0)) {
    if (at(TokenKind::Comma) && nested == 0) {
      advance();
      if (entry.empty()) {
        error(previous(), "FSIM-VHDL-PARSE-269",
              "a VHDL group entry list cannot contain an empty item");
      } else {
        entries.push_back(std::move(entry));
        entry.clear();
      }
      continue;
    }
    const auto token = advance();
    if (token.kind == TokenKind::LeftParen) {
      ++nested;
    } else if (token.kind == TokenKind::RightParen && nested != 0) {
      --nested;
    }
    entry += token.kind == TokenKind::Identifier ? vhdl_name(token.text)
                                                 : token.text;
  }
  if (!entry.empty()) {
    entries.push_back(std::move(entry));
  }
  if (entries.empty()) {
    error(current(), "FSIM-VHDL-PARSE-269",
          "a VHDL group requires at least one entry");
  }
  expect(TokenKind::RightParen, "')' after VHDL group entries",
         "FSIM-VHDL-PARSE-269");
  expect(TokenKind::Semicolon, "';' after VHDL group declaration",
         "FSIM-VHDL-PARSE-268");
  if (std::ranges::any_of(unit.vhdl_groups, [&](const auto &group) {
        return group.name == name;
      })) {
    error(name_token, "FSIM-VHDL-SEM-100",
          "duplicate VHDL group declaration '" + name + "'");
    return;
  }
  if (!template_declaration &&
      !std::ranges::any_of(unit.vhdl_groups, [&](const auto &group) {
        return group.template_declaration && group.name == template_name;
      })) {
    error(name_token, "FSIM-VHDL-SEM-101",
          "VHDL group instance references undeclared template '" +
              template_name + "'");
  }
  unit.vhdl_groups.push_back(
      VhdlGroupDeclaration{name, template_declaration, std::move(template_name),
                           std::move(entries), span_from(start, previous())});
}

void VhdlParser::parse_vhdl_disconnection_specification(
    std::vector<VhdlDisconnectionSpecification> &specifications,
    const Token &start) {
  VhdlDisconnectionSpecification specification;
  if (match_keyword("all", true)) {
    specification.all = true;
  } else if (match_keyword("others", true)) {
    specification.others = true;
  } else {
    const auto first = expect_identifier("guarded signal name");
    specification.signals.push_back(vhdl_name(first.text));
    while (match(TokenKind::Comma)) {
      const auto name = expect_identifier("guarded signal name");
      const auto canonical = vhdl_name(name.text);
      if (std::ranges::find(specification.signals, canonical)
          != specification.signals.end()) {
        error(
            name,
            "FSIM-VHDL-SEM-106",
            "duplicate guarded signal in a disconnection specification");
      } else {
        specification.signals.push_back(canonical);
      }
    }
  }
  expect(
      TokenKind::Colon,
      "':' after guarded signal list",
      "FSIM-VHDL-PARSE-283");
  const auto type = expect_identifier("disconnection type mark");
  specification.type_mark = vhdl_name(type.text);
  while (match(TokenKind::Dot)) {
    const auto part = expect_identifier("selected disconnection type mark");
    specification.type_mark += '.';
    specification.type_mark += vhdl_name(part.text);
  }
  const auto after = expect_keyword(
      "after", true, "FSIM-VHDL-PARSE-284");
  specification.delay = parse_vhdl_delay(after);
  expect(
      TokenKind::Semicolon,
      "';' after disconnection specification",
      "FSIM-VHDL-PARSE-285");
  specification.span = span_from(start, previous());
  specifications.push_back(std::move(specification));
}

void VhdlParser::apply_vhdl_disconnection_specifications(
    const std::vector<SignalDeclaration> &signals,
    const std::vector<VhdlDisconnectionSpecification> &specifications,
    std::vector<Statement> &statements) {
  const auto assignment_target = [](const auto &self, const Statement &node)
      -> const Expression * {
    if (node.kind == StatementKind::Assignment) {
      return &node.target;
    }
    for (const auto &child : node.statements) {
      if (const auto *target = self(self, child)) {
        return target;
      }
    }
    for (const auto &child : node.else_statements) {
      if (const auto *target = self(self, child)) {
        return target;
      }
    }
    for (const auto &alternative : node.case_alternatives) {
      for (const auto &child : alternative.statements) {
        if (const auto *target = self(self, child)) {
          return target;
        }
      }
    }
    return nullptr;
  };
  const auto base_name = [&](const Expression &expression) {
    const auto *base = &expression;
    while ((base->kind == ExpressionKind::Index
            || base->kind == ExpressionKind::Slice)
           && !base->operands.empty()) {
      base = &base->operands.front();
    }
    return base->kind == ExpressionKind::Identifier
        ? vhdl_name(base->text) : std::string{};
  };
  const auto simple_name = [](const std::string_view name) {
    const auto separator = name.rfind('.');
    return std::string{
        separator == std::string_view::npos
            ? name : name.substr(separator + 1)};
  };
  const auto type_matches = [&](const SignalDeclaration &signal,
                                const std::string_view mark) {
    const auto expected = simple_name(mark);
    return vhdl_name(signal.type.spelling) == expected
        || simple_name(signal.type.nominal_type) == expected
        || simple_name(signal.type.vhdl_type_declaration) == expected;
  };

  for (auto &statement : statements) {
    if (!statement.vhdl_guarded_assignment) {
      continue;
    }
    const auto *target = assignment_target(assignment_target, statement);
    const auto target_name = target == nullptr
        ? std::string{} : base_name(*target);
    if (target_name.empty()) {
      continue;
    }
    const auto declaration = std::ranges::find(
        signals, target_name, &SignalDeclaration::name);
    const bool explicitly_selected_elsewhere = std::ranges::any_of(
        specifications,
        [&](const VhdlDisconnectionSpecification &specification) {
          return std::ranges::find(
                     specification.signals, target_name)
              != specification.signals.end();
        });
    const VhdlDisconnectionSpecification *selected{};
    for (const auto &specification : specifications) {
      const bool explicitly_selected = std::ranges::find(
          specification.signals, target_name)
          != specification.signals.end();
      const bool type_selected = declaration != signals.end()
          && type_matches(*declaration, specification.type_mark);
      if (explicitly_selected && declaration != signals.end()
          && !type_selected) {
        error(
            Token{
                TokenKind::Identifier,
                target_name,
                specification.span,
                {}},
            "FSIM-VHDL-SEM-105",
            "disconnection specification type mark does not match signal '"
                + target_name + "'");
        continue;
      }
      if (!explicitly_selected
          && !(specification.all && type_selected)
          && !(specification.others && type_selected
               && !explicitly_selected_elsewhere)) {
        continue;
      }
      if (selected != nullptr) {
        error(
            Token{
                TokenKind::Identifier,
                target_name,
                specification.span,
                {}},
            "FSIM-VHDL-SEM-106",
            "overlapping disconnection specifications select signal '"
                + target_name + "'");
        continue;
      }
      selected = &specification;
    }
    if (selected != nullptr) {
      statement.vhdl_disconnection_delay = selected->delay;
    }
  }
}

void VhdlParser::parse_subtype_declaration(DesignUnit &unit, const Token &start,
                                           const bool nested_scope) {
  const auto name = expect_identifier("subtype name");
  const auto canonical_name = vhdl_name(name.text);
  const bool duplicate =
      (!nested_scope && vhdl_named_types_.contains(canonical_name)) ||
      std::any_of(unit.type_aliases.begin(), unit.type_aliases.end(),
                  [&](const TypeAliasDeclaration &declaration) {
                    return declaration.name == canonical_name;
                  }) ||
      std::any_of(unit.parameters.begin(), unit.parameters.end(),
                  [&](const ParameterDeclaration &parameter) {
                    return parameter.kind == ParameterKind::Type &&
                           parameter.name == canonical_name;
                  });
  if (duplicate) {
    error(name, "FSIM-VHDL-SEM-036",
          "duplicate bounded type declaration '" + canonical_name + "'");
  }
  expect_keyword("is", true, "FSIM-VHDL-PARSE-134");
  std::string resolution_function;
  if (at(TokenKind::Identifier)) {
    std::size_t lookahead = 1;
    while (at(TokenKind::Dot, lookahead) &&
           at(TokenKind::Identifier, lookahead + 1)) {
      lookahead += 2;
    }
    if (at(TokenKind::Identifier, lookahead) &&
        !keyword("range", lookahead, true)) {
      resolution_function =
          parse_vhdl_selected_name("resolution function name");
    }
  }
  auto type = parse_vhdl_type(true, true);
  type.vhdl_resolution_function = std::move(resolution_function);
  type.vhdl_type_declaration = start.span.source_name + ":" +
                               std::to_string(start.span.begin.offset) + ":" +
                               canonical_name;
  expect(TokenKind::Semicolon, "';' after subtype declaration",
         "FSIM-VHDL-PARSE-135");
  if (duplicate) {
    return;
  }
  if (!nested_scope) {
    vhdl_named_types_.insert(canonical_name);
  }
  unit.type_aliases.push_back(
      TypeAliasDeclaration { canonical_name,
          std::move(type),
          span_from(start, previous()),
          { },
          TypeDeclarationKind::VhdlSubtype,
          { } });
}

void VhdlParser::parse_signal_declaration(
    std::vector<SignalDeclaration> &signals,
    const std::vector<ParameterDeclaration> *constants) {
  const auto start = previous();
  std::vector<Token> names;
  names.push_back(expect_identifier("signal name"));
  while (match(TokenKind::Comma)) {
    names.push_back(expect_identifier("signal name"));
  }
  expect(TokenKind::Colon, "':' after signal name", "FSIM-VHDL-PARSE-017");
  const Type type = parse_vhdl_type(true, true);
  std::optional<Expression> default_value;
  if (match(TokenKind::ColonEqual)) {
    default_value = parse_expression();
  }
  expect(TokenKind::Semicolon, "';' after signal declaration",
         "FSIM-VHDL-PARSE-018");
  for (const auto &name : names) {
    const auto canonical = vhdl_name(name.text);
    const auto duplicate = std::find_if(signals.begin(), signals.end(),
                                        [&](const SignalDeclaration &signal) {
                                          return signal.name == canonical;
                                        });
    const bool constant_conflict =
        constants != nullptr &&
        std::any_of(constants->begin(), constants->end(),
                    [&](const ParameterDeclaration &constant) {
                      return constant.name == canonical;
                    });
    if (constant_conflict) {
      error(name, "FSIM-VHDL-SEM-019",
            "generated object '" + canonical +
                "' is declared as both a signal and a constant");
    } else if (duplicate != signals.end()) {
      error(name, "FSIM-VHDL-SEM-003",
            "duplicate signal declaration '" + canonical + "'");
    } else {
      auto declaration = SignalDeclaration{
          canonical, type, PortDirection::Unknown, false,
          span_from(start, previous())};
      declaration.default_value = default_value;
      signals.push_back(std::move(declaration));
    }
  }
}

void VhdlParser::parse_vhdl_object_alias(
    std::vector<SignalAliasDeclaration> &aliases, const Token &start) {
  const auto name = expect_identifier("alias name");
  if (!match(TokenKind::Colon)) {
    error(name, "FSIM-VHDL-UNSUPPORTED-054",
          "bounded object aliases require an explicit subtype indication");
    skip_to_semicolon();
    return;
  }
  auto type = parse_vhdl_type(true, true);
  expect_keyword("is", true, "FSIM-VHDL-PARSE-236");
  const auto actual = parse_vhdl_selected_name("alias target");
  if (match(TokenKind::LeftBracket)) {
      const auto signature_start = previous();
      require_vhdl_standard(
          signature_start, VhdlStandard::Vhdl1993, "an alias signature",
          "select VHDL-1993 or remove the overload-disambiguating signature");
      std::size_t signature_tokens = 0;
      while (!at_end() && !at(TokenKind::RightBracket) && !at(TokenKind::Semicolon)) {
          ++signature_tokens;
          advance();
      }
      if (signature_tokens == 0) {
          error(signature_start, "FSIM-VHDL-PARSE-236",
              "an alias signature cannot be empty");
      }
      expect(TokenKind::RightBracket, "']' after alias signature",
          "FSIM-VHDL-PARSE-236");
  }
  expect(TokenKind::Semicolon, "';' after alias declaration",
         "FSIM-VHDL-PARSE-236");
  const auto canonical = vhdl_name(name.text);
  if (std::ranges::any_of(aliases, [&](const auto &alias) {
        return alias.name == canonical;
      })) {
    error(name, "FSIM-VHDL-SEM-083",
          "duplicate bounded object alias '" + canonical + "'");
    return;
  }
  aliases.push_back(SignalAliasDeclaration {
      canonical, std::move(actual), std::move(type),
      PortDirection::Unknown, span_from(start, previous()) });
}

bool VhdlParser::parse_vhdl_local_nonobject_declaration(
    std::vector<ParameterDeclaration> &constants,
    std::vector<TypeAliasDeclaration> &type_aliases,
    std::vector<SignalAliasDeclaration> &signal_aliases,
    const std::vector<VariableDeclaration> &variables,
    std::vector<PackageInstantiation> &package_instances,
    std::vector<FunctionDeclaration> &functions,
    std::vector<ProcedureDeclaration> &procedures,
    std::vector<VhdlAttributeDeclaration> &attributes,
    std::vector<VhdlGroupDeclaration> &groups) {
  if (match_keyword("constant", true)) {
    GenerateBody declarations;
    declarations.constants = std::move(constants);
    declarations.type_aliases = std::move(type_aliases);
    declarations.signals.reserve(variables.size());
    for (const auto &variable : variables) {
      declarations.signals.push_back(
          SignalDeclaration{variable.name, variable.type,
                            PortDirection::Unknown, false, variable.span});
    }
    parse_vhdl_generate_constant(declarations, previous());
    constants = std::move(declarations.constants);
    type_aliases = std::move(declarations.type_aliases);
    return true;
  }
  if (match_keyword("alias", true)) {
    parse_vhdl_object_alias(signal_aliases, previous());
    return true;
  }
  if (keyword("package", 0, true) && at(TokenKind::Identifier, 1) &&
      keyword("is", 2, true) && keyword("new", 3, true)) {
    const auto package_start = advance();
    auto instance = parse_vhdl_package_instantiation(package_start);
    if (std::ranges::any_of(package_instances, [&](const auto &existing) {
          return existing.name == instance.name;
        })) {
      error(package_start, "FSIM-VHDL-SEM-059",
            "duplicate local package instance '" + instance.name + "'");
    } else {
      package_instances.push_back(std::move(instance));
    }
    return true;
  }
  if (keyword("pure", 0, true) || keyword("impure", 0, true)) {
    const auto pure = keyword("pure", 0, true);
    (void)advance();
    expect_keyword("function", true, "FSIM-VHDL-PARSE-235");
    functions.push_back(parse_vhdl_function(previous(), pure, true));
    return true;
  }
  if (match_keyword("function", true)) {
    functions.push_back(parse_vhdl_function(previous(), true, true));
    return true;
  }
  if (match_keyword("procedure", true)) {
    procedures.push_back(parse_vhdl_procedure(previous(), true));
    return true;
  }
  if (match_keyword("attribute", true)) {
    DesignUnit declarations;
    declarations.vhdl_attributes = std::move(attributes);
    declarations.vhdl_groups = std::move(groups);
    parse_vhdl_attribute_declaration(declarations, previous());
    attributes = std::move(declarations.vhdl_attributes);
    groups = std::move(declarations.vhdl_groups);
    return true;
  }
  if (match_keyword("group", true)) {
    DesignUnit declarations;
    declarations.vhdl_attributes = std::move(attributes);
    declarations.vhdl_groups = std::move(groups);
    parse_vhdl_group_declaration(declarations, previous());
    attributes = std::move(declarations.vhdl_attributes);
    groups = std::move(declarations.vhdl_groups);
    return true;
  }
  if (!match_keyword("type", true) && !match_keyword("subtype", true)) {
    return false;
  }
  const auto declaration = previous();
  const auto subtype = vhdl_name(declaration.text) == "subtype";
  DesignUnit declarations;
  declarations.type_aliases = std::move(type_aliases);
  if (subtype) {
    parse_subtype_declaration(declarations, declaration, true);
  } else {
    parse_type_declaration(declarations, declaration, true);
  }
  type_aliases = std::move(declarations.type_aliases);
  return true;
}

void VhdlParser::validate_vhdl_local_declaration_names(
    const std::vector<ParameterDeclaration> &constants,
    const std::vector<TypeAliasDeclaration> &type_aliases,
    const std::vector<SignalAliasDeclaration> &signal_aliases,
    const std::vector<VariableDeclaration> &variables,
    const std::vector<PackageInstantiation> &package_instances,
    const std::vector<FunctionDeclaration> &functions,
    const std::vector<ProcedureDeclaration> &procedures) {
  struct LocalName {
    std::string name;
    std::string family;
    SourceSpan span;
    bool callable{};
  };
  std::vector<LocalName> names;
  const auto append = [&](const auto &declarations,
                          const std::string_view family,
                          const bool callable = false) {
    for (const auto &declaration : declarations) {
      names.push_back(
          {declaration.name, std::string{family}, declaration.span, callable});
    }
  };
  append(constants, "constant");
  append(type_aliases, "type");
  append(signal_aliases, "alias");
  append(variables, "variable");
  append(package_instances, "package");
  append(functions, "function", true);
  append(procedures, "procedure", true);
  std::ranges::stable_sort(names, {}, [](const auto &declaration) {
    return declaration.span.begin.offset;
  });
  std::unordered_map<std::string, LocalName> prior;
  for (const auto &declaration : names) {
    const auto [found, inserted] =
        prior.try_emplace(declaration.name, declaration);
    if (inserted || found->second.family == declaration.family ||
        (found->second.callable && declaration.callable)) {
      continue;
    }
    error(Token{TokenKind::Identifier, declaration.name, declaration.span, {}},
          "FSIM-VHDL-SEM-082",
          "local " + declaration.family + " declaration '" + declaration.name +
              "' conflicts with prior " + found->second.family +
              " declaration");
  }
}

void VhdlParser::parse_concurrent_statement(DesignUnit &unit) {
  if (parse_vhdl_psl_directive(unit)) {
    return;
  }
  std::optional<Token> label_token;
  if (at(TokenKind::Identifier) && at(TokenKind::Colon, 1)) {
    label_token = advance();
    advance();
  }

  const auto postponed_token = match_keyword("postponed", true)
      ? std::optional<Token>{previous()} : std::nullopt;
  if (postponed_token) {
      (void)require_vhdl_standard(
          *postponed_token,
          VhdlStandard::Vhdl1993,
          "postponed concurrent statements",
          "use an ordinary process, assertion, or procedure call");
  }
  if (keyword("process", 0, true)) {
    unit.processes.push_back(
        parse_process(label_token, postponed_token.has_value()));
    return;
  }
  if (match_keyword("assert", true)) {
    auto statement = parse_vhdl_assertion(previous());
    statement.vhdl_postponed = postponed_token.has_value();
    if (postponed_token) {
      statement.span = cover(postponed_token->span, statement.span);
    }
    if (label_token) {
      statement.label = vhdl_name(label_token->text);
    }
    unit.concurrent_statements.push_back(std::move(statement));
    return;
  }
  if (postponed_token) {
    if (auto procedure = parse_vhdl_procedure_call()) {
      procedure->vhdl_postponed = true;
      procedure->span = cover(postponed_token->span, procedure->span);
      if (label_token) {
        procedure->label = vhdl_name(label_token->text);
        procedure->span = cover(label_token->span, procedure->span);
      }
      unit.concurrent_statements.push_back(std::move(*procedure));
      return;
    }
    error(
        *postponed_token,
        "FSIM-VHDL-SEM-104",
        "postponed is permitted only on a process, concurrent assertion, "
        "or concurrent procedure call");
    skip_to_semicolon();
    return;
  }
  if (match_keyword("with", true)) {
    auto statement = parse_vhdl_selected_assignment(previous(), true);
    if (label_token) {
      statement.label = vhdl_name(label_token->text);
    }
    unit.concurrent_statements.push_back(std::move(statement));
    return;
  }
  if (label_token && match_keyword("if", true)) {
    unit.generate_regions.push_back(
        parse_vhdl_conditional_generate(*label_token, previous()));
    return;
  }
  if (label_token && match_keyword("for", true)) {
    unit.generate_regions.push_back(
        parse_vhdl_iterative_generate(*label_token, previous()));
    return;
  }
  if (label_token && match_keyword("case", true)) {
      (void)require_vhdl_standard(
          previous(),
          VhdlStandard::Vhdl2008,
          "case-generate statements",
          "replace the case generate with VHDL-93 if-generate statements");
      unit.generate_regions.push_back(
          parse_vhdl_selection_generate(*label_token, previous()));
      return;
  }
  if (label_token && match_keyword("block", true)) {
    unit.generate_regions.push_back(
        parse_vhdl_static_block(*label_token, previous()));
    return;
  }
  if (label_token &&
      (keyword("entity", 0, true) || keyword("configuration", 0, true) ||
       (at(TokenKind::Identifier) &&
        (keyword("port", 1, true) || keyword("generic", 1, true))))) {
    unit.instances.push_back(parse_vhdl_instance(*label_token));
    return;
  }
  if (label_token) {
    const auto label = vhdl_name(label_token->text);
    if (auto procedure = parse_vhdl_procedure_call()) {
      procedure->label = label;
      procedure->span = cover(label_token->span, procedure->span);
      unit.concurrent_statements.push_back(std::move(*procedure));
      return;
    }
    if (auto statement = parse_assignment(true)) {
      statement->label = label;
      statement->span = cover(label_token->span, statement->span);
      unit.concurrent_statements.push_back(std::move(*statement));
      return;
    }
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
  if (auto procedure = parse_vhdl_procedure_call()) {
    unit.concurrent_statements.push_back(std::move(*procedure));
    return;
  }
  const auto unexpected = advance();
  error(unexpected, "FSIM-VHDL-UNSUPPORTED-006",
        "unsupported concurrent statement starting with '" + unexpected.text +
            "'");
  skip_to_semicolon();
}

} // namespace fsim::frontend
