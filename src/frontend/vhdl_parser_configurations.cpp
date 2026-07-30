// SPDX-License-Identifier: Apache-2.0
#include "vhdl_parser_internal.hpp"

namespace fsim::frontend {

void VhdlParser::parse_vhdl_binding_port_map(
    std::vector<PortConnection>& associations,
    const Token& start) {
  expect_keyword(
      "map", true, "FSIM-VHDL-PARSE-211");
  expect(
      TokenKind::LeftParen,
      "'(' after configuration port map",
      "FSIM-VHDL-PARSE-212");
  while (!at_end() && !at(TokenKind::RightParen)) {
    associations.push_back(
        parse_vhdl_port_connection());
    if (!match(TokenKind::Comma)) {
      break;
    }
  }
  expect(
      TokenKind::RightParen,
      "')' after configuration port map",
      "FSIM-VHDL-PARSE-213");
  (void)start;
}

VhdlBindingIndication
VhdlParser::parse_vhdl_binding_indication(
    const Token& start) {
  VhdlBindingIndication result;
  if (match_keyword("entity", true)) {
    result.kind = VhdlBindingAspectKind::Entity;
    result.entity_name =
        parse_vhdl_selected_name(
            "entity aspect in configuration binding");
    expect(
        TokenKind::LeftParen,
        "'(' before configured architecture name",
        "FSIM-VHDL-PARSE-210");
    const auto architecture =
        expect_identifier(
            "architecture name in configuration binding");
    result.architecture_name =
        vhdl_name(architecture.text);
    expect(
        TokenKind::RightParen,
        "')' after configured architecture name",
        "FSIM-VHDL-PARSE-210");
  } else if (match_keyword("configuration", true)) {
    result.kind = VhdlBindingAspectKind::Configuration;
    result.configuration_name =
        parse_vhdl_selected_name(
            "configuration aspect in configuration binding");
  } else if (match_keyword("open", true)) {
    result.kind = VhdlBindingAspectKind::Open;
  } else {
    error(
        current(),
        "FSIM-VHDL-PARSE-209",
        "expected entity, configuration, or open binding aspect");
  }

  if (result.kind != VhdlBindingAspectKind::Open
      && match_keyword("generic", true)) {
    Instance map_holder;
    parse_vhdl_generic_map(
        map_holder, previous());
    result.generic_map =
        std::move(map_holder.parameter_overrides);
  }
  if (result.kind != VhdlBindingAspectKind::Open
      && match_keyword("port", true)) {
    parse_vhdl_binding_port_map(
        result.port_map, previous());
  }
  expect(
      TokenKind::Semicolon,
      "';' after configuration binding indication",
      "FSIM-VHDL-PARSE-214");
  result.span = span_from(start, previous());
  return result;
}

VhdlComponentConfiguration
VhdlParser::parse_vhdl_component_configuration(
    const Token& start,
    const bool require_end_for) {
  VhdlComponentConfiguration result;
  if (match_keyword("all", true)) {
    result.selection =
        VhdlInstantiationSelectionKind::All;
  } else if (match_keyword("others", true)) {
    result.selection =
        VhdlInstantiationSelectionKind::Others;
  } else {
    do {
      const auto label =
          expect_identifier(
              "component instance label in configuration");
      const auto canonical = vhdl_name(label.text);
      if (std::ranges::find(
              result.labels, canonical)
          != result.labels.end()) {
        error(
            label,
            "FSIM-VHDL-SEM-065",
            "duplicate component instance label '"
                + canonical
                + "' in configuration");
      } else {
        result.labels.push_back(canonical);
      }
    } while (match(TokenKind::Comma));
  }
  if ((result.selection
          != VhdlInstantiationSelectionKind::Labels)
      && match(TokenKind::Comma)) {
    error(
        previous(),
        "FSIM-VHDL-SEM-066",
        "all or others must be the complete configuration "
        "instantiation list");
    while (!at_end() && !at(TokenKind::Colon)) {
      advance();
    }
  }
  expect(
      TokenKind::Colon,
      "':' after configuration instantiation list",
      "FSIM-VHDL-PARSE-207");
  const auto component =
      expect_identifier(
          "component name in configuration");
  result.component_name =
      vhdl_name(component.text);
  const auto use =
      expect_keyword(
          "use", true, "FSIM-VHDL-PARSE-208");
  result.binding =
      parse_vhdl_binding_indication(use);

  if (require_end_for) {
    expect_keyword(
        "end", true, "FSIM-VHDL-PARSE-215");
    expect_keyword(
        "for", true, "FSIM-VHDL-PARSE-215");
    expect(
        TokenKind::Semicolon,
        "';' after component configuration",
        "FSIM-VHDL-PARSE-216");
  }
  result.span = span_from(start, previous());
  return result;
}

void VhdlParser::skip_vhdl_configuration_for_block() {
  std::size_t depth = 0;
  while (!at_end()) {
    if (match_keyword("for", true)) {
      ++depth;
      continue;
    }
    if (match_keyword("end", true)
        && match_keyword("for", true)) {
      (void)match(TokenKind::Semicolon);
      if (depth == 0) {
        return;
      }
      --depth;
      if (depth == 0) {
        return;
      }
      continue;
    }
    advance();
  }
}

VhdlBlockConfiguration
VhdlParser::parse_vhdl_block_configuration(
    const Token& start) {
  VhdlBlockConfiguration result;
  const auto block =
      expect_identifier(
          "block or generate name in configuration");
  result.block_name = vhdl_name(block.text);
  if (match(TokenKind::LeftParen)) {
    result.generate_index = parse_expression();
    expect(
        TokenKind::RightParen,
        "')' after generate configuration index",
        "FSIM-VHDL-PARSE-229");
  }
  while (!at_end() && !keyword("end", 0, true)) {
    if (match_keyword("for", true)) {
      const auto nested_start = previous();
      bool component_configuration =
          keyword("all", 0, true)
          || keyword("others", 0, true);
      std::size_t lookahead = 0;
      if (!component_configuration) {
        while (at(TokenKind::Identifier, lookahead)) {
          ++lookahead;
          if (!at(TokenKind::Comma, lookahead)) {
            break;
          }
          ++lookahead;
        }
        component_configuration =
            at(TokenKind::Colon, lookahead);
      }
      if (component_configuration) {
        result.component_configurations.push_back(
            parse_vhdl_component_configuration(
                nested_start, true));
      } else {
        result.block_configurations.push_back(
            parse_vhdl_block_configuration(
                nested_start));
      }
      continue;
    }
    const auto unsupported = advance();
    error(
        unsupported,
        "FSIM-VHDL-UNSUPPORTED-050",
        "unsupported configuration declarative item '"
            + unsupported.text + "'");
    skip_to_semicolon();
  }
  expect_keyword(
      "end", true, "FSIM-VHDL-PARSE-217");
  expect_keyword(
      "for", true, "FSIM-VHDL-PARSE-217");
  expect(
      TokenKind::Semicolon,
      "';' after architecture block configuration",
      "FSIM-VHDL-PARSE-218");
  result.span = span_from(start, previous());
  return result;
}

DesignUnit VhdlParser::parse_vhdl_configuration(
    const Token& start) {
  DesignUnit unit;
  unit.kind = UnitKind::VhdlConfiguration;
  unit.language = Language::Vhdl2008;
  const auto name =
      expect_identifier("configuration name");
  unit.name = vhdl_name(name.text);
  expect_keyword(
      "of", true, "FSIM-VHDL-PARSE-204");
  const auto entity_name =
      parse_vhdl_selected_name(
          "entity name in configuration declaration");
  const auto separator = entity_name.find_last_of('.');
  unit.primary_name =
      entity_name.substr(
          separator == std::string::npos
              ? 0
              : separator + 1);
  expect_keyword(
      "is", true, "FSIM-VHDL-PARSE-205");
  while (!at_end() && !keyword("for", 0, true)
         && !keyword("end", 0, true)) {
    const auto unsupported = advance();
    error(
        unsupported,
        "FSIM-VHDL-UNSUPPORTED-050",
        "unsupported configuration declaration item '"
            + unsupported.text + "'");
    skip_to_semicolon();
  }
  const auto block_start =
      expect_keyword(
          "for", true, "FSIM-VHDL-PARSE-206");
  VhdlConfigurationDeclaration declaration;
  declaration.block =
      parse_vhdl_block_configuration(block_start);
  expect_keyword(
      "end", true, "FSIM-VHDL-PARSE-219");
  (void)match_keyword("configuration", true);
  if (at(TokenKind::Identifier)) {
    const auto end_name = advance();
    if (vhdl_name(end_name.text) != unit.name) {
      error(
          end_name,
          "FSIM-VHDL-SEM-067",
          "configuration end name does not match '"
              + unit.name + "'");
    }
  }
  expect(
      TokenKind::Semicolon,
      "';' after configuration declaration",
      "FSIM-VHDL-PARSE-220");
  declaration.span = span_from(start, previous());
  unit.vhdl_configuration =
      std::move(declaration);
  unit.span = span_from(start, previous());
  return unit;
}

}  // namespace fsim::frontend
