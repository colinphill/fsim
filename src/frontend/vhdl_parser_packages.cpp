// SPDX-License-Identifier: Apache-2.0
#include "vhdl_parser_internal.hpp"

namespace fsim::frontend {

std::string VhdlParser::parse_vhdl_selected_name(
    const std::string_view description) {
  const auto first = expect_identifier(description);
  std::string result = vhdl_name(first.text);
  while (match(TokenKind::Dot)) {
    const auto part = expect_identifier(description);
    result += '.';
    result += vhdl_name(part.text);
  }
  return result;
}

void VhdlParser::parse_vhdl_package_generic_map(
    std::vector<ParameterOverride>& associations,
    bool& box,
    const Token& start) {
  expect_keyword("map", true, "FSIM-VHDL-PARSE-181");
  expect(
      TokenKind::LeftParen,
      "'(' after package generic map",
      "FSIM-VHDL-PARSE-182");
  if (match(TokenKind::Less)) {
    expect(
        TokenKind::Greater,
        "'>' in package generic box",
        "FSIM-VHDL-PARSE-183");
    box = true;
    expect(
        TokenKind::RightParen,
        "')' after package generic box",
        "FSIM-VHDL-PARSE-184");
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
            "FSIM-VHDL-SEM-057",
            "duplicate package generic association '"
                + *actual.name + "'");
      }
    } else if (saw_named) {
      error(
          current(),
          "FSIM-VHDL-SEM-058",
          "a positional package generic association cannot follow a "
          "named association");
    }

    if (match(TokenKind::Less)) {
      expect(
          TokenKind::Greater,
          "'>' in default package generic association",
          "FSIM-VHDL-PARSE-185");
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
      "')' after package generic associations",
      "FSIM-VHDL-PARSE-186");
  (void)start;
}

ParameterDeclaration VhdlParser::parse_vhdl_interface_package(
    const Token& start) {
  const auto name =
      expect_identifier("interface package generic name");
  expect_keyword("is", true, "FSIM-VHDL-PARSE-187");
  expect_keyword("new", true, "FSIM-VHDL-PARSE-188");

  InterfacePackageProfile profile;
  profile.template_name =
      parse_vhdl_selected_name("generic package template name");
  const auto generic =
      expect_keyword("generic", true, "FSIM-VHDL-PARSE-189");
  parse_vhdl_package_generic_map(
      profile.generic_map, profile.generic_map_box, generic);
  profile.span = span_from(start, previous());

  ParameterDeclaration result;
  result.name = vhdl_name(name.text);
  result.span = profile.span;
  result.kind = ParameterKind::Package;
  result.package_profile = std::move(profile);
  return result;
}

PackageInstantiation VhdlParser::parse_vhdl_package_instantiation(
    const Token& start) {
    require_vhdl_standard(
        start, VhdlStandard::Vhdl2008, "a local package instantiation",
        "select VHDL-2008 or use a non-generic package declaration");
    PackageInstantiation result;
    const auto name = expect_identifier("local package instance name");
    result.name = vhdl_name(name.text);
    expect_keyword("is", true, "FSIM-VHDL-PARSE-190");
    expect_keyword("new", true, "FSIM-VHDL-PARSE-191");
    result.template_name = parse_vhdl_selected_name("generic package template name");
    const auto generic = expect_keyword("generic", true, "FSIM-VHDL-PARSE-192");
    parse_vhdl_package_generic_map(
        result.generic_map, result.generic_map_box, generic);
    expect(
        TokenKind::Semicolon,
        "';' after local package instantiation",
        "FSIM-VHDL-PARSE-193");
    result.span = span_from(start, previous());
    return result;
}

} // namespace fsim::frontend
