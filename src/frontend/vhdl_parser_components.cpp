// SPDX-License-Identifier: Apache-2.0
#include "vhdl_parser_internal.hpp"

namespace fsim::frontend {

namespace {

bool same_expression(
    const Expression& left,
    const Expression& right) {
  return left.kind == right.kind
      && left.text == right.text
      && left.operands.size() == right.operands.size()
      && std::ranges::equal(
          left.operands,
          right.operands,
          same_expression);
}

bool same_component_type(
    const Type& left,
    const Type& right) {
  const auto same_packed_range =
      [&]() {
        if (left.packed_range.has_value()
            != right.packed_range.has_value()) {
          return false;
        }
        if (!left.packed_range) {
          return true;
        }
        return left.packed_range->left
                   == right.packed_range->left
            && left.packed_range->right
                   == right.packed_range->right
            && left.packed_range->descending
                   == right.packed_range->descending;
      };
  const auto same_packed_expression =
      [&]() {
        if (left.packed_range_expression.has_value()
            != right.packed_range_expression.has_value()) {
          return false;
        }
        if (!left.packed_range_expression) {
          return true;
        }
        return same_expression(
                   left.packed_range_expression->left,
                   right.packed_range_expression->left)
            && same_expression(
                   left.packed_range_expression->right,
                   right.packed_range_expression->right)
            && left.packed_range_expression->descending
                   == right.packed_range_expression->descending;
      };
  return left.domain == right.domain
      && left.spelling == right.spelling
      && left.named_type == right.named_type
      && left.is_signed == right.is_signed
      && same_packed_range()
      && same_packed_expression();
}

bool same_component_generic(
    const ParameterDeclaration& left,
    const ParameterDeclaration& right) {
  if (left.kind != right.kind) {
    return false;
  }
  if (left.kind == ParameterKind::Value) {
    return left.object_class == right.object_class
        && left.direction == right.direction
        && same_component_type(left.type, right.type);
  }
  if (left.kind == ParameterKind::Type) {
    return true;
  }
  if (left.kind == ParameterKind::Function) {
    if (!left.function_profile
        || !right.function_profile) {
      return left.function_profile.has_value()
          == right.function_profile.has_value();
    }
    const auto& lhs = *left.function_profile;
    const auto& rhs = *right.function_profile;
    if (lhs.pure != rhs.pure
        || !same_component_type(
            lhs.return_type, rhs.return_type)
        || lhs.arguments.size() != rhs.arguments.size()) {
      return false;
    }
    for (std::size_t index = 0;
         index < lhs.arguments.size(); ++index) {
      if (lhs.arguments[index].direction
              != rhs.arguments[index].direction
          || !same_component_type(
              lhs.arguments[index].type,
              rhs.arguments[index].type)) {
        return false;
      }
    }
    return true;
  }
  if (left.kind == ParameterKind::Procedure) {
    if (!left.procedure_profile
        || !right.procedure_profile) {
      return left.procedure_profile.has_value()
          == right.procedure_profile.has_value();
    }
    const auto& lhs = *left.procedure_profile;
    const auto& rhs = *right.procedure_profile;
    if (lhs.arguments.size() != rhs.arguments.size()) {
      return false;
    }
    for (std::size_t index = 0;
         index < lhs.arguments.size(); ++index) {
      if (lhs.arguments[index].direction
              != rhs.arguments[index].direction
          || lhs.arguments[index].object_class
              != rhs.arguments[index].object_class
          || !same_component_type(
              lhs.arguments[index].type,
              rhs.arguments[index].type)) {
        return false;
      }
    }
    return true;
  }
  if (!left.package_profile
      || !right.package_profile) {
    return left.package_profile.has_value()
        == right.package_profile.has_value();
  }
  const auto& lhs = *left.package_profile;
  const auto& rhs = *right.package_profile;
  if (lhs.template_name != rhs.template_name
      || lhs.generic_map_box != rhs.generic_map_box
      || lhs.generic_map.size() != rhs.generic_map.size()) {
    return false;
  }
  for (std::size_t index = 0;
       index < lhs.generic_map.size(); ++index) {
    const auto& left_actual = lhs.generic_map[index];
    const auto& right_actual = rhs.generic_map[index];
    if (left_actual.name != right_actual.name
        || left_actual.default_box
            != right_actual.default_box
        || left_actual.type_value.has_value()
            != right_actual.type_value.has_value()
        || (left_actual.type_value
            && !same_component_type(
                *left_actual.type_value,
                *right_actual.type_value))
        || (!left_actual.type_value
            && !same_expression(
                left_actual.value,
                right_actual.value))) {
      return false;
    }
  }
  return true;
}

bool same_component_profile(
    const VhdlComponentDeclaration& left,
    const VhdlComponentDeclaration& right) {
  if (left.generics.size() != right.generics.size()
      || left.ports.size() != right.ports.size()) {
    return false;
  }
  for (std::size_t index = 0;
       index < left.generics.size();
       ++index) {
    if (!same_component_generic(
            left.generics[index],
            right.generics[index])) {
      return false;
    }
  }
  for (std::size_t index = 0;
       index < left.ports.size();
       ++index) {
    if (left.ports[index].direction
            != right.ports[index].direction
        || !same_component_type(
            left.ports[index].type,
            right.ports[index].type)) {
      return false;
    }
  }
  return true;
}

}  // namespace

void VhdlParser::add_vhdl_component_declaration(
    std::vector<VhdlComponentDeclaration>& declarations,
    VhdlComponentDeclaration declaration,
    const Token& start) {
  const auto duplicate = std::ranges::find_if(
      declarations,
      [&](const auto& existing) {
        return existing.name == declaration.name
            && existing.scope_path == declaration.scope_path
            && same_component_profile(
                existing, declaration);
      });
  if (duplicate != declarations.end()) {
    error(
        start,
        "FSIM-VHDL-SEM-068",
        "duplicate component declaration profile '"
            + declaration.name + "'");
    return;
  }
  declaration.declaration_order = declarations.size();
  declarations.push_back(std::move(declaration));
}

void VhdlParser::parse_vhdl_component_ports(
    VhdlComponentDeclaration& declaration,
    const Token& start) {
  expect(
      TokenKind::LeftParen,
      "'(' after component port",
      "FSIM-VHDL-PARSE-221");
  while (!at_end() && !at(TokenKind::RightParen)) {
    std::vector<Token> names;
    names.push_back(
        expect_identifier("component port name"));
    while (match(TokenKind::Comma)) {
      names.push_back(
          expect_identifier("component port name"));
    }
    expect(
        TokenKind::Colon,
        "':' after component port name",
        "FSIM-VHDL-PARSE-222");

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
      error(
          current(),
          "FSIM-VHDL-PARSE-223",
          "expected component port mode");
    }

    auto type = parse_vhdl_type(true, true);
    std::optional<Expression> default_value;
    if (match(TokenKind::ColonEqual)) {
      default_value = parse_expression();
      if (direction != PortDirection::Input) {
        error(
            names.front(),
            "FSIM-VHDL-SEM-072",
            "a component port default is only legal on an input port");
      }
    }
    for (const auto& name : names) {
      const auto canonical = vhdl_name(name.text);
      const auto duplicate =
          std::ranges::any_of(
              declaration.ports,
              [&](const auto& port) {
                return port.name == canonical;
              });
      const auto generic_conflict =
          std::ranges::any_of(
              declaration.generics,
              [&](const auto& generic) {
                return generic.name == canonical;
              });
      if (duplicate || generic_conflict) {
        error(
            name,
            "FSIM-VHDL-SEM-071",
            "component interface formal '" + canonical
                + "' is declared more than once");
        continue;
      }
      declaration.ports.push_back(
          VhdlComponentPort{
              canonical,
              type,
              direction,
              default_value,
              span_from(name, previous())});
    }
    if (!match(TokenKind::Semicolon)
        && !at(TokenKind::RightParen)) {
      error(
          current(),
          "FSIM-VHDL-PARSE-224",
          "expected ';' between component port declarations");
      skip_to_semicolon();
    }
  }
  expect(
      TokenKind::RightParen,
      "')' after component port declarations",
      "FSIM-VHDL-PARSE-225");
  expect(
      TokenKind::Semicolon,
      "';' after component port clause",
      "FSIM-VHDL-PARSE-226");
  (void)start;
}

VhdlComponentDeclaration
VhdlParser::parse_vhdl_component_declaration(
    const Token& start,
    const std::size_t declaration_order) {
  VhdlComponentDeclaration result;
  const auto name =
      expect_identifier("component declaration name");
  result.name = vhdl_name(name.text);
  result.declaration_order = declaration_order;
  (void)match_keyword("is", true);

  while (!at_end() && !keyword("end", 0, true)) {
    if (match_keyword("generic", true)) {
      DesignUnit profile;
      profile.kind = UnitKind::VhdlEntity;
      profile.language = Language::Vhdl2008;
      parse_vhdl_generics(profile, previous(), true);
      for (const auto& generic : profile.parameters) {
        if (std::ranges::any_of(
                result.generics,
                [&](const auto& existing) {
                  return existing.name == generic.name;
                })) {
          error(
              start,
              "FSIM-VHDL-SEM-071",
              "component generic '" + generic.name
                  + "' is declared more than once");
          continue;
        }
        result.generics.push_back(generic);
      }
    } else if (match_keyword("port", true)) {
      parse_vhdl_component_ports(result, previous());
    } else {
      const auto unsupported = advance();
      error(
          unsupported,
          "FSIM-VHDL-UNSUPPORTED-052",
          "unsupported component declaration item '"
              + unsupported.text + "'");
      skip_to_semicolon();
    }
  }

  expect_keyword("end", true, "FSIM-VHDL-PARSE-227");
  (void)match_keyword("component", true);
  if (at(TokenKind::Identifier)) {
    const auto end_name = advance();
    result.end_name = vhdl_name(end_name.text);
    if (*result.end_name != result.name) {
      error(
          end_name,
          "FSIM-VHDL-SEM-069",
          "component end name '" + *result.end_name
              + "' does not match '" + result.name + "'");
    }
  }
  expect(
      TokenKind::Semicolon,
      "';' after component declaration",
      "FSIM-VHDL-PARSE-228");
  result.span = span_from(start, previous());
  return result;
}

}  // namespace fsim::frontend
