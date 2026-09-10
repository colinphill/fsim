// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

namespace fsim::frontend {
namespace {

[[nodiscard]] std::optional<VerilogUdpLevelSymbol> udp_level(
    const std::string_view spelling) {
  const auto symbol = detail::ascii_lower(spelling);
  if (symbol == "0") return VerilogUdpLevelSymbol::Zero;
  if (symbol == "1") return VerilogUdpLevelSymbol::One;
  if (symbol == "x") return VerilogUdpLevelSymbol::Unknown;
  if (symbol == "?") return VerilogUdpLevelSymbol::DontCare;
  if (symbol == "b") return VerilogUdpLevelSymbol::Binary;
  return std::nullopt;
}

[[nodiscard]] std::optional<VerilogUdpOutputSymbol> udp_initial(
    const std::string_view spelling) {
  auto symbol = detail::ascii_lower(spelling);
  const auto quote = symbol.find('\'');
  if (quote != std::string::npos) {
    if (symbol.substr(0, quote) != "1") return std::nullopt;
    symbol = symbol.substr(quote + 1);
    if (!symbol.empty() && symbol.front() == 's') symbol.erase(0, 1);
    if (symbol.size() != 2 || symbol.front() != 'b') return std::nullopt;
    symbol.erase(0, 1);
  }
  if (symbol == "0") return VerilogUdpOutputSymbol::Zero;
  if (symbol == "1") return VerilogUdpOutputSymbol::One;
  if (symbol == "x") return VerilogUdpOutputSymbol::Unknown;
  return std::nullopt;
}

[[nodiscard]] bool contains_name(
    const std::vector<std::string>& names,
    const std::string_view name) {
  return std::ranges::find(names, name) != names.end();
}

}  // namespace

std::optional<VerilogUdpLevelSymbol>
VerilogParser::parse_udp_level_symbol() {
  if (match(TokenKind::Question)) {
    return VerilogUdpLevelSymbol::DontCare;
  }
  if (!at(TokenKind::Identifier) && !at(TokenKind::Number)) {
    return std::nullopt;
  }
  const auto parsed = udp_level(current().text);
  if (parsed) advance();
  return parsed;
}

std::optional<VerilogUdpOutputSymbol>
VerilogParser::parse_udp_output_symbol(const bool allow_no_change) {
  if (allow_no_change && match(TokenKind::Minus)) {
    return VerilogUdpOutputSymbol::NoChange;
  }
  if (!at(TokenKind::Identifier) && !at(TokenKind::Number)) {
    return std::nullopt;
  }
  const auto level = udp_level(current().text);
  if (!level || *level == VerilogUdpLevelSymbol::DontCare
      || *level == VerilogUdpLevelSymbol::Binary) {
    return std::nullopt;
  }
  advance();
  if (*level == VerilogUdpLevelSymbol::Zero) {
    return VerilogUdpOutputSymbol::Zero;
  }
  if (*level == VerilogUdpLevelSymbol::One) {
    return VerilogUdpOutputSymbol::One;
  }
  return VerilogUdpOutputSymbol::Unknown;
}

VerilogUdpTableRow VerilogParser::parse_udp_table_row(
    const VerilogUdpDeclaration& declaration) {
  const auto start = current();
  VerilogUdpTableRow row;
  while (!at_end() && !at(TokenKind::Colon)
         && !keyword("endtable")) {
    VerilogUdpInputPattern pattern;
    const auto symbol_start = current();
    if (match(TokenKind::LeftParen)) {
      std::string spelling;
      while (!at_end() && !at(TokenKind::RightParen)
             && !at(TokenKind::Colon)) {
        spelling += advance().text;
      }
      expect(
          TokenKind::RightParen,
          "')' after UDP transition pair",
          "FSIM-SV-PARSE-228");
      const auto previous = spelling.size() == 2U
          ? udp_level(spelling.substr(0, 1)) : std::nullopt;
      const auto current = spelling.size() == 2U
          ? udp_level(spelling.substr(1, 1)) : std::nullopt;
      if (previous && current) {
        pattern.previous = *previous;
        pattern.current = *current;
        pattern.edge = VerilogUdpEdgeSymbol::Explicit;
      } else {
        error(
            symbol_start,
            "FSIM-SV-PARSE-229",
            "a UDP transition pair requires exactly two level symbols");
      }
    } else if (match(TokenKind::Star)) {
      pattern.edge = VerilogUdpEdgeSymbol::Any;
    } else if (at(TokenKind::Identifier)) {
      const auto symbol = detail::ascii_lower(current().text);
      if (symbol == "r" || symbol == "f" || symbol == "p"
          || symbol == "n") {
        advance();
        pattern.edge = symbol == "r"
            ? VerilogUdpEdgeSymbol::Rising
            : symbol == "f"
            ? VerilogUdpEdgeSymbol::Falling
            : symbol == "p"
            ? VerilogUdpEdgeSymbol::Positive
            : VerilogUdpEdgeSymbol::Negative;
      } else if (const auto level = parse_udp_level_symbol()) {
        pattern.level = *level;
      } else {
        error(
            current(),
            "FSIM-SV-PARSE-230",
            "invalid UDP input table symbol '" + current().text + "'");
        advance();
      }
    } else if (const auto level = parse_udp_level_symbol()) {
      pattern.level = *level;
    } else {
      error(
          current(),
          "FSIM-SV-PARSE-230",
          "invalid UDP input table symbol '" + current().text + "'");
      advance();
    }
    pattern.span = span_from(symbol_start, previous());
    row.inputs.push_back(std::move(pattern));
  }
  expect(
      TokenKind::Colon,
      "':' after UDP input symbols",
      "FSIM-SV-PARSE-231");
  if (declaration.sequential) {
    row.current_state = parse_udp_level_symbol();
    if (!row.current_state) {
      error(
          current(),
          "FSIM-SV-PARSE-232",
          "a sequential UDP row requires a current-state symbol");
      if (!at_end() && !at(TokenKind::Colon)) advance();
    }
    expect(
        TokenKind::Colon,
        "':' after sequential UDP current state",
        "FSIM-SV-PARSE-233");
  }
  const auto output = parse_udp_output_symbol(declaration.sequential);
  if (output) {
    row.output = *output;
  } else {
    error(
        current(),
        "FSIM-SV-PARSE-234",
        "invalid UDP output table symbol '" + current().text + "'");
    if (!at_end() && !at(TokenKind::Semicolon)) advance();
  }
  expect(
      TokenKind::Semicolon,
      "';' after UDP table row",
      "FSIM-SV-PARSE-235");
  row.span = span_from(start, previous());
  return row;
}

VerilogUdpDeclaration VerilogParser::parse_udp_declaration(
    const Token& start) {
  VerilogUdpDeclaration declaration;
  declaration.language = language_;
  declaration.standard_revision = standard_revision_;
  declaration.verilog_compatibility_profile = compatibility_profile_;
  declaration.time_unit = current_time_unit_;
  declaration.time_precision = current_time_precision_;
  const auto name = expect_identifier("UDP name");
  declaration.name = name.text;
  expect(
      TokenKind::LeftParen,
      "'(' after UDP name",
      "FSIM-SV-PARSE-224");

  PortDirection direction{PortDirection::Unknown};
  const bool ansi = keyword("output") || keyword("input");
  std::vector<std::string> header_names;
  while (!at_end() && !at(TokenKind::RightParen)) {
    if (ansi && match_keyword("output")) {
      direction = PortDirection::Output;
      if (match_keyword("reg")) {
        declaration.sequential = true;
        declaration.output_reg = true;
      }
    } else if (ansi && match_keyword("input")) {
      direction = PortDirection::Input;
    }
    const auto terminal = expect_identifier("UDP terminal name");
    if (terminal.text.empty()) {
      if (!at_end()) advance();
      break;
    }
    header_names.push_back(terminal.text);
    if (direction == PortDirection::Output) {
      if (!declaration.output.empty()) {
        error(
            terminal,
            "FSIM-SV-SEM-130",
            "a UDP declaration requires exactly one output terminal");
      } else {
        declaration.output = terminal.text;
      }
    } else if (direction == PortDirection::Input) {
      declaration.inputs.push_back(terminal.text);
    }
    if (!match(TokenKind::Comma)) break;
  }
  expect(
      TokenKind::RightParen,
      "')' after UDP terminals",
      "FSIM-SV-PARSE-225");
  expect(
      TokenKind::Semicolon,
      "';' after UDP header",
      "FSIM-SV-PARSE-226");

  auto parse_terminal_group = [&](const PortDirection group_direction) {
    if (group_direction == PortDirection::Output && match_keyword("reg")) {
      declaration.sequential = true;
      declaration.output_reg = true;
    }
    do {
      const auto terminal = expect_identifier("UDP terminal declaration");
      if (group_direction == PortDirection::Output) {
        if (!declaration.output.empty()
            && declaration.output != terminal.text) {
          error(
              terminal,
              "FSIM-SV-SEM-130",
              "a UDP declaration requires exactly one output terminal");
        }
        declaration.output = terminal.text;
      } else if (!contains_name(declaration.inputs, terminal.text)) {
        declaration.inputs.push_back(terminal.text);
      }
    } while (match(TokenKind::Comma));
    expect(
        TokenKind::Semicolon,
        "';' after UDP terminal declaration",
        "FSIM-SV-PARSE-227");
  };

  while (!at_end() && !keyword("table") && !keyword("endprimitive")) {
    if (match_keyword("output")) {
      parse_terminal_group(PortDirection::Output);
    } else if (match_keyword("input")) {
      parse_terminal_group(PortDirection::Input);
    } else if (match_keyword("reg")) {
      declaration.sequential = true;
      declaration.output_reg = true;
      const auto output = expect_identifier("sequential UDP output name");
      if (!declaration.output.empty() && declaration.output != output.text) {
        error(
            output,
            "FSIM-SV-SEM-131",
            "a UDP reg refinement must name its output terminal");
      }
      expect(
          TokenKind::Semicolon,
          "';' after UDP reg refinement",
          "FSIM-SV-PARSE-236");
    } else if (match_keyword("initial")) {
      declaration.sequential = true;
      const auto output = expect_identifier("UDP initial output name");
      if (!declaration.output.empty() && declaration.output != output.text) {
        error(
            output,
            "FSIM-SV-SEM-132",
            "a UDP initial statement must assign its output terminal");
      }
      expect(
          TokenKind::Assign,
          "'=' in UDP initial statement",
          "FSIM-SV-PARSE-237");
      if (at(TokenKind::Number) || at(TokenKind::Identifier)) {
        declaration.initial_output = udp_initial(current().text);
        if (!declaration.initial_output) {
          error(
              current(),
              "FSIM-SV-SEM-133",
              "a UDP initial value must be scalar 0, 1, or x");
        }
        advance();
      } else {
        error(
            current(),
            "FSIM-SV-PARSE-238",
            "a UDP initial statement requires a scalar value");
      }
      expect(
          TokenKind::Semicolon,
          "';' after UDP initial statement",
          "FSIM-SV-PARSE-239");
    } else {
      error(
          current(),
          "FSIM-SV-PARSE-240",
          "unsupported UDP declaration item '" + current().text + "'");
      skip_to_semicolon();
    }
  }

  expect_keyword("table", false, "FSIM-SV-PARSE-241");
  while (!at_end() && !keyword("endtable")
         && !keyword("endprimitive")) {
    declaration.rows.push_back(parse_udp_table_row(declaration));
  }
  expect_keyword("endtable", false, "FSIM-SV-PARSE-242");
  expect_keyword("endprimitive", false, "FSIM-SV-PARSE-243");
  if (match(TokenKind::Colon)) {
    const auto end_name = expect_identifier("UDP closing name");
    if (end_name.text != declaration.name) {
      error(
          end_name,
          "FSIM-SV-SEM-134",
          "UDP closing name '" + end_name.text
              + "' does not match '" + declaration.name + "'");
    }
  }

  std::unordered_set<std::string> unique_terminals;
  for (const auto& terminal : header_names) {
    if (!unique_terminals.insert(terminal).second) {
      error(
          name,
          "FSIM-SV-SEM-137",
          "duplicate UDP header terminal '" + terminal + "'");
    }
  }
  std::vector<std::string> declared_terminals;
  if (!declaration.output.empty()) {
    declared_terminals.push_back(declaration.output);
  }
  declared_terminals.insert(
      declared_terminals.end(), declaration.inputs.begin(),
      declaration.inputs.end());
  if (declaration.output.empty() || header_names != declared_terminals) {
    error(
        name,
        "FSIM-SV-SEM-136",
        "UDP terminal declarations must name one output first followed by "
        "every header input in order");
  }
  if (declaration.inputs.empty()) {
    error(
        name,
        "FSIM-SV-SEM-138",
        "a UDP declaration requires at least one input terminal");
  }
  if (declaration.sequential && !declaration.output_reg) {
    error(
        name,
        "FSIM-SV-SEM-139",
        "a sequential UDP output must be declared reg");
  }
  if (declaration.rows.empty()) {
    error(
        name,
        "FSIM-SV-SEM-140",
        "a UDP table requires at least one row");
  }
  if (!verilog_udp_table_within_resource_budget(
          declaration.inputs.size(), declaration.rows.size())) {
    error(
        name,
        "FSIM-SV-SEM-147",
        "materializing the UDP table would exceed the 256 MiB frontend "
        "owning-storage budget");
  }
  const auto same_pattern = [](const VerilogUdpInputPattern& left,
                               const VerilogUdpInputPattern& right) {
    return left.level == right.level && left.edge == right.edge
        && left.previous == right.previous && left.current == right.current;
  };
  for (std::size_t row_index = 0; row_index < declaration.rows.size();
       ++row_index) {
    const auto& row = declaration.rows[row_index];
    Token location;
    location.text = "UDP table row";
    location.span = row.span;
    if (row.inputs.size() != declaration.inputs.size()) {
      error(
          location,
          "FSIM-SV-SEM-141",
          "UDP table row has " + std::to_string(row.inputs.size())
              + " inputs but declaration requires "
              + std::to_string(declaration.inputs.size()));
    }
    const auto edge_count = std::ranges::count_if(
        row.inputs, [](const VerilogUdpInputPattern& input) {
          return input.edge != VerilogUdpEdgeSymbol::None;
        });
    if (!declaration.sequential && edge_count != 0) {
      error(
          location,
          "FSIM-SV-SEM-142",
          "a combinational UDP table row cannot contain an edge symbol");
    } else if (declaration.sequential && edge_count > 1) {
      error(
          location,
          "FSIM-SV-SEM-143",
          "a sequential UDP table row can contain at most one edge symbol");
    }
    for (std::size_t prior = 0; prior < row_index; ++prior) {
      const auto& previous_row = declaration.rows[prior];
      if (row.current_state == previous_row.current_state
          && row.inputs.size() == previous_row.inputs.size()
          && std::ranges::equal(
              row.inputs, previous_row.inputs, same_pattern)) {
        error(
            location,
            "FSIM-SV-SEM-144",
            "a UDP table row duplicates an earlier input/state pattern");
        break;
      }
    }
  }
  declaration.span = span_from(start, previous());
  return declaration;
}

void VerilogParser::normalize_udp_instances(ParsedDesign& design) {
  const auto declaration_named = [&](const std::string_view name) {
    return std::ranges::any_of(
        design.udp_declarations,
        [&](const VerilogUdpDeclaration& declaration) {
          return declaration.name == name;
        });
  };
  const auto time_scale = [](const std::string_view spelling) {
    const auto split = std::ranges::find_if(
        spelling,
        [](const char character) {
          return character < '0' || character > '9';
        });
    const auto offset = static_cast<std::size_t>(
        std::distance(spelling.begin(), split));
    const auto magnitude = offset == 0
        ? std::optional<std::uint64_t>{}
        : detail::decimal_u64(spelling.substr(0, offset));
    return std::pair{
        magnitude.value_or(1), std::string{spelling.substr(offset)}};
  };
  const auto delay_value = [&](Expression expression,
                               const std::string_view unit_spelling) {
    Delay delay;
    const auto [scale, unit] = time_scale(unit_spelling);
    if (expression.kind == ExpressionKind::IntegerLiteral) {
      const auto ratio = decimal_ratio(expression.text);
      if (ratio
          && ratio->numerator
              <= std::numeric_limits<std::uint64_t>::max() / scale) {
        delay.magnitude = ratio->numerator * scale;
        delay.divisor = ratio->denominator;
        delay.unit = unit;
      } else {
        Token location;
        location.text = expression.text;
        location.span = expression.span;
        error(
            location,
            "FSIM-SV-SEM-145",
            "a UDP propagation delay must be a representable "
            "nonnegative decimal expression");
      }
    } else {
      delay.magnitude = scale;
      delay.unit = unit;
      delay.expression = std::move(expression);
    }
    delay.span = delay.expression
        ? delay.expression->span
        : expression.span;
    return delay;
  };
  const auto normalize = [&](Instance& instance,
                             const std::string_view unit_spelling,
                             const bool program_unit) {
    if (!declaration_named(instance.unit_name)) {
      return;
    }
    instance.udp_instance = true;
    if (program_unit) {
      Token location;
      location.text = instance.unit_name;
      location.span = instance.span;
      error(
          location,
          "FSIM-SV-SEM-390",
          "a program block cannot contain a user-defined primitive "
          "instance");
    }
    if (instance.udp_delay || instance.parameter_overrides.empty()) {
      return;
    }
    const bool positional_values = std::ranges::all_of(
        instance.parameter_overrides,
        [](const ParameterOverride& actual) {
          return !actual.name.has_value() && !actual.type_value.has_value();
        });
    if (!positional_values) {
      return;
    }
    if (instance.parameter_overrides.size() > 3) {
      Token location;
      location.text = instance.unit_name;
      location.span = instance.span;
      error(
          location,
          "FSIM-SV-SEM-146",
          "a UDP propagation delay accepts at most three values");
      return;
    }
    auto first = delay_value(
        std::move(instance.parameter_overrides.front().value),
        unit_spelling);
    for (std::size_t index = 1;
         index < instance.parameter_overrides.size(); ++index) {
      first.additional_values.push_back(delay_value(
          std::move(instance.parameter_overrides[index].value),
          unit_spelling));
    }
    instance.parameter_overrides.clear();
    instance.udp_delay = std::move(first);
  };
  const auto visit_regions = [&](const auto& self,
                                 std::vector<GenerateRegion>& regions,
                                 const std::string_view time_unit,
                                 const bool program_unit) -> void {
    const auto visit_body = [&](GenerateBody& body) {
      for (auto& instance : body.instances) {
        normalize(instance, time_unit, program_unit);
      }
      self(self, body.generate_regions, time_unit, program_unit);
    };
    for (auto& region : regions) {
      visit_body(region.then_body);
      visit_body(region.else_body);
      for (auto& alternative : region.alternatives) {
        visit_body(alternative.body);
      }
    }
  };
  for (auto& unit : design.units) {
    const bool program_unit =
        unit.kind == UnitKind::SystemVerilogProgram;
    for (auto& instance : unit.instances) {
      normalize(instance, unit.time_unit, program_unit);
    }
    visit_regions(
        visit_regions, unit.generate_regions, unit.time_unit, program_unit);
  }
}

}  // namespace fsim::frontend
