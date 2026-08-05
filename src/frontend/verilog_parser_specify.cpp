// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

#include <algorithm>
#include <array>
#include <ranges>
#include <string_view>

namespace fsim::frontend {
namespace {

void identify_path_pulse(VerilogSpecparamDeclaration& declaration) {
  constexpr std::string_view prefix{"PATHPULSE$"};
  if (!declaration.name.starts_with(prefix)) return;
  declaration.path_pulse = true;
  auto terminals = std::string_view{declaration.name}.substr(prefix.size());
  if (terminals.empty()) return;
  const auto separator = terminals.find('$');
  if (separator == std::string_view::npos) {
    declaration.path_pulse_input = std::string{terminals};
    return;
  }
  declaration.path_pulse_input = std::string{terminals.substr(0, separator)};
  declaration.path_pulse_output = std::string{terminals.substr(separator + 1)};
}

}  // namespace

void VerilogParser::parse_specparam_declaration(
    VerilogSpecifyBlock& block,
    const Token& start) {
  for (;;) {
    const auto name = expect_identifier("specparam name");
    VerilogSpecparamDeclaration declaration;
    declaration.name = name.text;
    identify_path_pulse(declaration);
    expect(
        TokenKind::Assign,
        "'=' after specparam name",
        "FSIM-SV-PARSE-141");

    const bool path_pulse_pair =
        declaration.path_pulse && match(TokenKind::LeftParen);
    declaration.value = parse_expression();
    if (path_pulse_pair) {
      if (match(TokenKind::Comma)) {
        declaration.path_pulse_error_limit = parse_expression();
      }
      expect(
          TokenKind::RightParen,
          "')' after PATHPULSE limits",
          "FSIM-SV-PARSE-142");
    } else if (match(TokenKind::Colon)) {
      declaration.minimum = declaration.value;
      declaration.typical = parse_expression();
      expect(
          TokenKind::Colon,
          "second ':' in specparam mintypmax expression",
          "FSIM-SV-PARSE-143");
      declaration.maximum = parse_expression();
      declaration.value = *declaration.typical;
    }
    declaration.span = span_from(start, previous());
    if (declaration.path_pulse) {
      const auto pulse_delay = [&](const Expression& expression) {
        Delay delay;
        delay.magnitude = module_time_unit_.empty()
            ? 1U : module_time_unit_magnitude_;
        delay.unit = module_time_unit_;
        delay.expression = expression;
        delay.span = expression.span;
        return delay;
      };
      declaration.path_pulse_reject_delay =
          pulse_delay(declaration.value);
      if (declaration.path_pulse_error_limit) {
        declaration.path_pulse_error_delay =
            pulse_delay(*declaration.path_pulse_error_limit);
      }
    }

    const bool duplicate = std::ranges::any_of(
        block.specparams,
        [&](const VerilogSpecparamDeclaration& existing) {
          return existing.name == declaration.name;
        });
    if (duplicate) {
      error(
          name,
          "FSIM-SV-SEM-165",
          "duplicate specparam declaration '" + name.text + "'");
    } else {
      block.specparams.push_back(std::move(declaration));
    }
    if (!match(TokenKind::Comma)) break;
  }
  expect(
      TokenKind::Semicolon,
      "';' after specparam declaration",
      "FSIM-SV-PARSE-144");
}

VerilogModulePathDeclaration VerilogParser::parse_specify_module_path(
    const Token& start,
    Expression condition,
    const bool conditional,
    const bool ifnone) {
  VerilogModulePathDeclaration path;
  path.condition = std::move(condition);
  path.conditional = conditional;
  path.ifnone = ifnone;
  expect(
      TokenKind::LeftParen,
      "'(' before module path",
      "FSIM-SV-PARSE-147");

  if (match_keyword("posedge")) {
    path.source_edge = VerilogSpecifyEdge::Posedge;
  } else if (match_keyword("negedge")) {
    path.source_edge = VerilogSpecifyEdge::Negedge;
  }
  for (;;) {
    path.sources.push_back(parse_lvalue());
    if (!match(TokenKind::Comma)) break;
  }
  if (match(TokenKind::Arrow)) {
    path.kind = VerilogModulePathKind::Parallel;
  } else {
    expect(
        TokenKind::Star,
        "'=>' or '*>' in module path",
        "FSIM-SV-PARSE-148");
    expect(
        TokenKind::Greater,
        "'>' after '*' in full module path",
        "FSIM-SV-PARSE-149");
    path.kind = VerilogModulePathKind::Full;
  }

  if (match(TokenKind::Plus)) {
    path.polarity = VerilogPathPolarity::Positive;
  } else if (match(TokenKind::Minus)) {
    path.polarity = VerilogPathPolarity::Negative;
  }
  const bool grouped_destination = match(TokenKind::LeftParen);
  for (;;) {
    path.destinations.push_back(parse_lvalue());
    if (!match(TokenKind::Comma)) break;
  }
  if (match(TokenKind::PlusColon)
      || (match(TokenKind::Plus) && match(TokenKind::Colon))) {
    path.polarity = VerilogPathPolarity::Positive;
    path.destination_data_source = parse_expression();
  } else if (match(TokenKind::MinusColon)
             || (match(TokenKind::Minus) && match(TokenKind::Colon))) {
    path.polarity = VerilogPathPolarity::Negative;
    path.destination_data_source = parse_expression();
  }
  if (grouped_destination) {
    expect(
        TokenKind::RightParen,
        "')' after module-path destination",
        "FSIM-SV-PARSE-150");
  }
  expect(
      TokenKind::RightParen,
      "')' after module path",
      "FSIM-SV-PARSE-151");
  const auto assign = current();
  expect(
      TokenKind::Assign,
      "'=' after module path",
      "FSIM-SV-PARSE-152");
  auto delay = parse_verilog_delay(assign, 12);
  path.delays.reserve(delay.additional_values.size() + 1);
  auto additional = std::move(delay.additional_values);
  delay.additional_values.clear();
  path.delays.push_back(std::move(delay));
  std::ranges::move(additional, std::back_inserter(path.delays));
  if (path.delays.size() != 1 && path.delays.size() != 2
      && path.delays.size() != 3 && path.delays.size() != 6
      && path.delays.size() != 12) {
    error(
        assign,
        "FSIM-SV-SEM-164",
        "a module path requires exactly 1, 2, 3, 6, or 12 delays");
  }
  expect(
      TokenKind::Semicolon,
      "';' after module path",
      "FSIM-SV-PARSE-153");
  path.span = span_from(start, previous());
  return path;
}

VerilogSpecifyPulseDeclaration
VerilogParser::parse_specify_pulse_declaration(
    const Token& start,
    const bool controls_style,
    const VerilogPulseStyle style,
    const bool show_cancelled) {
  VerilogSpecifyPulseDeclaration declaration;
  declaration.controls_style = controls_style;
  declaration.style = style;
  declaration.show_cancelled = show_cancelled;
  for (;;) {
    declaration.terminals.push_back(parse_lvalue());
    if (!match(TokenKind::Comma)) break;
  }
  expect(
      TokenKind::Semicolon,
      "';' after specify pulse declaration",
      "FSIM-SV-PARSE-156");
  declaration.span = span_from(start, previous());
  return declaration;
}

VerilogTimingCheckEvent VerilogParser::parse_verilog_timing_check_event() {
  VerilogTimingCheckEvent event;
  const auto start = current();
  if (match_keyword("posedge")) {
    event.edge = VerilogSpecifyEdge::Posedge;
  } else if (match_keyword("negedge")) {
    event.edge = VerilogSpecifyEdge::Negedge;
  } else if (match_keyword("edge")) {
    event.edge = VerilogSpecifyEdge::Edge;
    expect(
        TokenKind::LeftBracket,
        "'[' after timing-check edge",
        "FSIM-SV-PARSE-157");
    while (!at_end() && !at(TokenKind::RightBracket)) {
      std::string descriptor;
      while (!at_end() && !at(TokenKind::Comma)
             && !at(TokenKind::RightBracket)) {
        descriptor += advance().text;
      }
      if (!descriptor.empty()) {
        event.edge_descriptors.push_back(std::move(descriptor));
      }
      if (!match(TokenKind::Comma)) break;
    }
    expect(
        TokenKind::RightBracket,
        "']' after timing-check edge descriptors",
        "FSIM-SV-PARSE-158");
  }
  event.expression = parse_expression();
  if (match(TokenKind::AndAndAnd)) {
    event.condition = parse_expression();
  }
  event.span = span_from(start, previous());
  return event;
}

VerilogTimingCheckDeclaration VerilogParser::parse_verilog_timing_check(
    const Token& start,
    const VerilogTimingCheckKind kind) {
  VerilogTimingCheckDeclaration declaration;
  declaration.kind = kind;
  expect(
      TokenKind::LeftParen,
      "'(' after timing-check name",
      "FSIM-SV-PARSE-159");
  auto first_event = parse_verilog_timing_check_event();
  const bool one_event =
      kind == VerilogTimingCheckKind::Period
      || kind == VerilogTimingCheckKind::Width;
  std::optional<VerilogTimingCheckEvent> second_event;
  if (!one_event) {
    expect(
        TokenKind::Comma,
        "',' between timing-check events",
        "FSIM-SV-PARSE-160");
    second_event = parse_verilog_timing_check_event();
  }
  if (kind == VerilogTimingCheckKind::Setup) {
    declaration.data_event = std::move(first_event);
    declaration.reference_event = std::move(*second_event);
  } else {
    declaration.reference_event = std::move(first_event);
    if (second_event) declaration.data_event = std::move(*second_event);
  }

  std::vector<Expression> arguments;
  std::vector<std::optional<std::array<Expression, 3>>>
      argument_mintypmax;
  while (match(TokenKind::Comma)) {
    if (at(TokenKind::Comma) || at(TokenKind::RightParen)) {
      arguments.emplace_back();
      argument_mintypmax.emplace_back();
    } else {
      auto argument = parse_expression();
      std::optional<std::array<Expression, 3>> alternatives;
      if (match(TokenKind::Colon)) {
        auto typical = parse_expression();
        expect(
            TokenKind::Colon,
            "second ':' in timing-check min:typ:max expression",
            "FSIM-SV-PARSE-163");
        auto maximum = parse_expression();
        alternatives = std::array{
            argument, typical, maximum};
        argument = std::move(typical);
      }
      arguments.push_back(std::move(argument));
      argument_mintypmax.push_back(std::move(alternatives));
    }
  }
  expect(
      TokenKind::RightParen,
      "')' after timing-check arguments",
      "FSIM-SV-PARSE-161");
  expect(
      TokenKind::Semicolon,
      "';' after timing check",
      "FSIM-SV-PARSE-162");

  std::size_t minimum_arguments = 1;
  std::size_t maximum_arguments = 2;
  std::size_t limit_count = 1;
  switch (kind) {
    case VerilogTimingCheckKind::SetupHold:
    case VerilogTimingCheckKind::RecRem:
      minimum_arguments = 2;
      maximum_arguments = 7;
      limit_count = 2;
      break;
    case VerilogTimingCheckKind::FullSkew:
      minimum_arguments = 2;
      maximum_arguments = 5;
      limit_count = 2;
      break;
    case VerilogTimingCheckKind::TimeSkew:
      maximum_arguments = 4;
      break;
    case VerilogTimingCheckKind::Width:
      maximum_arguments = 3;
      break;
    case VerilogTimingCheckKind::NoChange:
      minimum_arguments = 2;
      maximum_arguments = 3;
      limit_count = 2;
      break;
    default:
      break;
  }
  if (arguments.size() < minimum_arguments
      || arguments.size() > maximum_arguments
      || (kind == VerilogTimingCheckKind::Width
          && arguments.size() >= 2 && !arguments[1].valid())
      || std::ranges::any_of(
          arguments | std::views::take(
              std::min(limit_count, arguments.size())),
          [](const Expression& expression) { return !expression.valid(); })) {
    error(
        start,
        "FSIM-SV-SEM-166",
        "timing check '" + start.text
            + "' has a missing or excessive required argument");
  }
  for (std::size_t index = 0;
       index < std::min(limit_count, arguments.size()); ++index) {
    declaration.limits.push_back(std::move(arguments[index]));
  }
  const auto assign = [&](Expression& destination, const std::size_t index) {
    if (index < arguments.size()) destination = std::move(arguments[index]);
  };
  if (kind == VerilogTimingCheckKind::Width) {
    assign(declaration.threshold, 1);
    assign(declaration.notifier, 2);
  } else {
    assign(declaration.notifier, limit_count);
  }
  if (kind == VerilogTimingCheckKind::SetupHold
      || kind == VerilogTimingCheckKind::RecRem) {
    assign(declaration.timestamp_condition, 3);
    assign(declaration.timecheck_condition, 4);
    assign(declaration.delayed_reference, 5);
    assign(declaration.delayed_data, 6);
  } else if (kind == VerilogTimingCheckKind::TimeSkew) {
    assign(declaration.event_based_flag, 2);
    assign(declaration.remain_active_flag, 3);
  } else if (kind == VerilogTimingCheckKind::FullSkew) {
    assign(declaration.event_based_flag, 3);
    assign(declaration.remain_active_flag, 4);
  }
  const auto timing_delay = [&](const Expression& expression) {
    Delay delay;
    delay.magnitude = module_time_unit_.empty()
        ? 1U : module_time_unit_magnitude_;
    delay.unit = module_time_unit_;
    delay.expression = expression;
    delay.span = expression.span;
    return delay;
  };
  const auto normalized_argument = [&] (
      const std::size_t index,
      const Expression& selected) {
    auto delay = timing_delay(selected);
    if (index >= argument_mintypmax.size()
        || !argument_mintypmax[index]) {
      return delay;
    }
    const auto alternative = [&](const Expression& expression) {
      DelayAlternative result;
      result.magnitude = delay.magnitude;
      result.unit = delay.unit;
      result.expression = expression;
      result.span = expression.span;
      return result;
    };
    delay.minimum = alternative((*argument_mintypmax[index])[0]);
    delay.typical = alternative((*argument_mintypmax[index])[1]);
    delay.maximum = alternative((*argument_mintypmax[index])[2]);
    return delay;
  };
  for (std::size_t index = 0;
       index < declaration.limits.size(); ++index) {
    declaration.normalized_limits.push_back(
        normalized_argument(index, declaration.limits[index]));
  }
  if (declaration.threshold.valid()) {
    declaration.normalized_threshold = normalized_argument(
        1, declaration.threshold);
  }
  declaration.span = span_from(start, previous());
  return declaration;
}

VerilogSpecifyBlock VerilogParser::parse_specify_block(const Token& start) {
  VerilogSpecifyBlock block;
  while (!at_end() && !keyword("endspecify")) {
    if (match_keyword("specparam")) {
      parse_specparam_declaration(block, previous());
      continue;
    }
    if (at(TokenKind::LeftParen)) {
      block.module_paths.push_back(
          parse_specify_module_path(current()));
      continue;
    }
    if (match_keyword("if")) {
      const auto conditional = previous();
      expect(
          TokenKind::LeftParen,
          "'(' after specify if",
          "FSIM-SV-PARSE-154");
      auto condition = parse_expression();
      expect(
          TokenKind::RightParen,
          "')' after specify path condition",
          "FSIM-SV-PARSE-155");
      block.module_paths.push_back(parse_specify_module_path(
          conditional, std::move(condition), true, false));
      continue;
    }
    if (match_keyword("ifnone")) {
      block.module_paths.push_back(parse_specify_module_path(
          previous(), {}, false, true));
      continue;
    }
    if (match_keyword("pulsestyle_onevent")) {
      block.pulse_declarations.push_back(
          parse_specify_pulse_declaration(
              previous(), true, VerilogPulseStyle::Onevent, false));
      continue;
    }
    if (match_keyword("pulsestyle_ondetect")) {
      block.pulse_declarations.push_back(
          parse_specify_pulse_declaration(
              previous(), true, VerilogPulseStyle::Ondetect, false));
      continue;
    }
    if (match_keyword("showcancelled")) {
      block.pulse_declarations.push_back(
          parse_specify_pulse_declaration(
              previous(), false, VerilogPulseStyle::Onevent, true));
      continue;
    }
    if (match_keyword("noshowcancelled")) {
      block.pulse_declarations.push_back(
          parse_specify_pulse_declaration(
              previous(), false, VerilogPulseStyle::Onevent, false));
      continue;
    }
    if (at(TokenKind::Identifier) && current().text.starts_with('$')) {
      constexpr std::array timing_checks{
          std::pair{"$setup", VerilogTimingCheckKind::Setup},
          std::pair{"$hold", VerilogTimingCheckKind::Hold},
          std::pair{"$setuphold", VerilogTimingCheckKind::SetupHold},
          std::pair{"$recovery", VerilogTimingCheckKind::Recovery},
          std::pair{"$removal", VerilogTimingCheckKind::Removal},
          std::pair{"$recrem", VerilogTimingCheckKind::RecRem},
          std::pair{"$skew", VerilogTimingCheckKind::Skew},
          std::pair{"$timeskew", VerilogTimingCheckKind::TimeSkew},
          std::pair{"$fullskew", VerilogTimingCheckKind::FullSkew},
          std::pair{"$period", VerilogTimingCheckKind::Period},
          std::pair{"$width", VerilogTimingCheckKind::Width},
          std::pair{"$nochange", VerilogTimingCheckKind::NoChange},
      };
      const auto match = std::ranges::find_if(
          timing_checks,
          [&](const auto& entry) { return entry.first == current().text; });
      if (match != timing_checks.end()) {
        const auto timing_check = advance();
        block.timing_checks.push_back(
            parse_verilog_timing_check(timing_check, match->second));
        continue;
      }
    }
    const auto unexpected = advance();
    error(
        unexpected,
        "FSIM-SV-PARSE-145",
        "unsupported specify item starting with '" + unexpected.text + "'");
    skip_to_semicolon();
  }
  expect_keyword("endspecify", false, "FSIM-SV-PARSE-146");
  block.span = span_from(start, previous());
  return block;
}

}  // namespace fsim::frontend
