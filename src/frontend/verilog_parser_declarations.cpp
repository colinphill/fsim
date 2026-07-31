// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

namespace fsim::frontend {

void VerilogParser::parse_parameter_overrides(
  Instance& instance,
  const Token& hash) {
  expect(
      TokenKind::LeftParen,
      "'(' after instance parameter '#'",
      "FSIM-SV-PARSE-055");
  bool saw_named = false;
  bool saw_positional = false;
  const auto begins_unambiguous_type_actual = [&]() {
    return keyword("byte") || keyword("shortint")
        || keyword("longint") || keyword("time")
        || keyword("integer") || keyword("int")
        || keyword("logic") || keyword("reg") || keyword("bit")
        || keyword("signed") || keyword("unsigned")
        || keyword("string") || at(TokenKind::LeftBracket);
  };
  const auto parse_actual = [&](ParameterOverride& override) {
    if (begins_unambiguous_type_actual()) {
      override.type_value = parse_type_parameter_actual();
    } else {
      override.value = parse_expression();
    }
  };
  while (!at_end() && !at(TokenKind::RightParen)) {
    const auto start = current();
    ParameterOverride override;
    if (match(TokenKind::Dot)) {
      saw_named = true;
      const auto name = expect_identifier("overridden parameter name");
      override.name = name.text;
      expect(
          TokenKind::LeftParen,
          "'(' after named parameter override",
          "FSIM-SV-PARSE-056");
      parse_actual(override);
      expect(
          TokenKind::RightParen,
          "')' after named parameter override",
          "FSIM-SV-PARSE-057");
      if (std::any_of(
              instance.parameter_overrides.begin(),
              instance.parameter_overrides.end(),
              [&](const ParameterOverride& existing) {
                return existing.name == override.name;
              })) {
        error(
            name,
            "FSIM-SV-SEM-018",
            "duplicate named parameter override '" + name.text + "'");
      }
    } else {
      saw_positional = true;
      parse_actual(override);
    }
    override.span = cover(start.span, previous().span);
    instance.parameter_overrides.push_back(std::move(override));
    if (!match(TokenKind::Comma)) {
      break;
    }
  }
  expect(
      TokenKind::RightParen,
      "')' after instance parameter overrides",
      "FSIM-SV-PARSE-058");
  if (saw_named && saw_positional) {
    error(
        hash,
        "FSIM-SV-SEM-019",
        "named and positional parameter overrides cannot be mixed");
  }
}

void VerilogParser::parse_module_ports(DesignUnit& unit) {
  VerilogTypeSpec inherited;
  bool have_inherited_type = false;
  while (!at_end() && !at(TokenKind::RightParen)) {
    if (at(TokenKind::Dot)) {
      const auto dot = advance();
      error(dot, "FSIM-SV-UNSUPPORTED-005",
            "named port connections are not valid in a module declaration");
      skip_to_port_delimiter();
    } else {
      VerilogTypeSpec spec = inherited;
      bool declared_here = false;
      if (is_direction_keyword()) {
        spec.direction = parse_direction();
        spec.type = default_port_net_type();
        declared_here = true;
        const bool explicit_type =
            is_net_type_keyword()
            || is_named_type_reference_start();
        if (is_named_type_reference_start()) {
          spec.type = parse_named_type();
        } else {
          parse_optional_net_type(spec.type);
        }
        require_default_port_net_type(current(), explicit_type);
        if (spec.type.named_type.empty()) {
          parse_optional_signedness(spec.type);
          parse_optional_range(spec.type);
        }
        inherited = spec;
        have_inherited_type = true;
      } else if (!have_inherited_type) {
        spec.type = Type{ValueDomain::Unknown, {}, std::nullopt, false};
        spec.direction = PortDirection::Unknown;
      }

      const auto port_name = expect_identifier("port name");
      SignalDeclaration declaration{
          port_name.text, spec.type, spec.direction, true, port_name.span};
      if (spec.direction == PortDirection::Unknown) {
        non_ansi_ports_.insert(port_name.text);
      }
      if (match(TokenKind::Assign)) {
        const auto initializer = previous();
        (void)parse_expression();
        error(
            initializer,
            "FSIM-SV-UNSUPPORTED-010",
            "ANSI port default expressions are not executable in this "
            "frontend slice");
      }
      if (at(TokenKind::LeftBracket)) {
        (void)parse_optional_container_dimension(declaration.type);
      }
      const auto duplicate = std::find_if(
          unit.ports.begin(),
          unit.ports.end(),
          [&](const SignalDeclaration& port) {
            return port.name == port_name.text;
          });
      if (duplicate != unit.ports.end()) {
        error(
            port_name,
            "FSIM-SV-SEM-003",
            "duplicate module port declaration '" + port_name.text + "'");
      } else if (
          std::any_of(
              unit.parameters.begin(),
              unit.parameters.end(),
              [&](const ParameterDeclaration& parameter) {
                return parameter.name == port_name.text;
              })) {
        error(
            port_name,
            "FSIM-SV-SEM-020",
            "port '" + port_name.text
                + "' conflicts with a parameter declaration");
      } else {
        unit.ports.push_back(std::move(declaration));
      }
      (void)declared_here;
    }

    if (!match(TokenKind::Comma)) {
      break;
    }
  }
}

void VerilogParser::skip_to_port_delimiter() {
  std::size_t depth = 0;
  while (!at_end()) {
    if (at(TokenKind::LeftParen) || at(TokenKind::LeftBracket) ||
        at(TokenKind::LeftBrace)) {
      ++depth;
    } else if (at(TokenKind::RightParen) ||
               at(TokenKind::RightBracket) ||
               at(TokenKind::RightBrace)) {
      if (depth == 0) {
        return;
      }
      --depth;
    } else if (at(TokenKind::Comma) && depth == 0) {
      return;
    }
    advance();
  }
}

[[nodiscard]] bool VerilogParser::is_direction_keyword() const  {
  return keyword("input") || keyword("output") || keyword("inout");
}

PortDirection VerilogParser::parse_direction() {
  if (match_keyword("input")) {
    return PortDirection::Input;
  }
  if (match_keyword("output")) {
    return PortDirection::Output;
  }
  if (match_keyword("inout")) {
    return PortDirection::Inout;
  }
  return PortDirection::Unknown;
}

Type VerilogParser::default_verilog_type() {
  return Type{ValueDomain::Logic4, "wire", std::nullopt, false};
}

Type VerilogParser::default_port_net_type() const  {
  if (current_default_nettype_ == "none") {
    return default_verilog_type();
  }
  return Type{
      ValueDomain::Logic4,
      current_default_nettype_,
      std::nullopt,
      false};
}

void VerilogParser::require_default_port_net_type(
  const Token& location,
  const bool explicit_type) {
  if (current_default_nettype_ == "none" && !explicit_type) {
    error(
        location,
        "FSIM-SV-SEM-016",
        "a port without an explicit net or variable type is forbidden by "
        "`default_nettype none");
  }
}

[[nodiscard]] bool VerilogParser::is_net_type_keyword() const  {
  return any_keyword({
      "wire", "reg", "logic", "bit", "byte", "shortint",
      "int", "longint", "integer", "time"});
}

[[nodiscard]] bool VerilogParser::is_named_type_reference_start(
  const std::size_t offset) const  {
  if (!at(TokenKind::Identifier, offset)
      || keyword_reserved(keyword_set_, current(offset).text)) {
    return false;
  }
  if (contains_word(
          {"always_ff", "always_comb", "always_latch"},
          current(offset).text)) {
    return false;
  }
  if (at(TokenKind::Scope, offset + 1)) {
    return at(TokenKind::Identifier, offset + 2)
        && at(TokenKind::Identifier, offset + 3);
  }
  return at(TokenKind::Identifier, offset + 1)
      && !at(TokenKind::LeftParen, offset + 2)
      && !at(TokenKind::Hash, offset + 2);
}

Type VerilogParser::parse_named_type() {
  const auto first =
      expect_identifier("SystemVerilog type name");
  std::string name = first.text;
  auto span = first.span;
  if (match(TokenKind::Scope)) {
    const auto selected =
        expect_identifier("package type name");
    name += "::";
    name += selected.text;
    span = cover(span, selected.span);
  }
  Type type{
      ValueDomain::Unknown,
      name,
      std::nullopt,
      false};
  type.named_type = name;
  type.named_type_span = span;
  return type;
}

void VerilogParser::parse_typedef(
  DesignUnit& unit,
  const Token& start) {
  Type type;
  std::vector<std::pair<
      ParameterDeclaration,
      Token>> enum_parameters;
  std::vector<EnumLiteralDeclaration> enum_literals;
  if (keyword("struct") || keyword("union")) {
    const bool is_union = match_keyword("union");
    if (!is_union) {
      (void)match_keyword("struct");
    }
    if (!match_keyword("packed")) {
      error(
          current(),
          "FSIM-SV-UNSUPPORTED-027",
          "bounded struct/union typedefs require the packed qualifier");
      skip_to_semicolon();
      return;
    }
    type.spelling =
        is_union ? "union packed" : "struct packed";
    type.packed_aggregate =
        is_union
            ? PackedAggregateKind::Union
            : PackedAggregateKind::Struct;
    type.domain = ValueDomain::Bit2;
    parse_optional_signedness(type);
    expect(
        TokenKind::LeftBrace,
        "'{' before packed aggregate members",
        "FSIM-SV-PARSE-086");
    if (at(TokenKind::RightBrace)) {
      error(
          current(),
          "FSIM-SV-PARSE-089",
          "bounded packed aggregates require at least one member");
    }
    std::unordered_set<std::string> member_names;
    while (!at_end() && !at(TokenKind::RightBrace)) {
      const auto member_start = current();
      Type member_type;
      if (keyword("logic") || keyword("reg")
          || keyword("bit")) {
        const auto type_token = advance();
        member_type.spelling = type_token.text;
        member_type.domain =
            type_token.text == "bit"
                ? ValueDomain::Bit2
                : ValueDomain::Logic4;
        parse_optional_signedness(member_type);
        parse_optional_range(member_type);
      } else {
        error(
            current(),
            "FSIM-SV-UNSUPPORTED-028",
            "packed aggregate members require a non-aggregate bit, logic, "
            "or reg type");
        skip_to_semicolon();
        continue;
      }
      for (;;) {
        const auto member =
            expect_identifier("packed aggregate member name");
        if (at(TokenKind::LeftBracket)) {
          error(
              current(),
              "FSIM-SV-UNSUPPORTED-029",
              "unpacked aggregate member dimensions are not implemented");
          skip_balanced(
              TokenKind::LeftBracket,
              TokenKind::RightBracket);
        }
        if (match(TokenKind::Assign)) {
          (void)parse_expression();
          error(
              member,
              "FSIM-SV-UNSUPPORTED-029",
              "packed aggregate member initializers are not implemented");
        }
        if (!member_names.insert(member.text).second) {
          error(
              member,
              "FSIM-SV-SEM-025",
              "duplicate packed aggregate member '"
                  + member.text + "'");
        } else {
          type.packed_members.push_back(PackedMember{
              member.text,
              member_type.domain,
              member_type.spelling,
              member_type.packed_range,
              member_type.is_signed,
              member_type.packed_range_expression,
              0,
              cover(member_start.span, previous().span)});
          if (member_type.domain == ValueDomain::Logic4) {
            type.domain = ValueDomain::Logic4;
          }
        }
        if (!match(TokenKind::Comma)) {
          break;
        }
      }
      expect(
          TokenKind::Semicolon,
          "';' after packed aggregate member declaration",
          "FSIM-SV-PARSE-088");
    }
    expect(
        TokenKind::RightBrace,
        "'}' after packed aggregate members",
        "FSIM-SV-PARSE-087");
    std::uint64_t total_width = 0;
    std::optional<std::uint64_t> union_width;
    bool concrete = !type.packed_members.empty();
    for (const auto& member : type.packed_members) {
      const auto width = member.width();
      if (!width || *width == 0) {
        concrete = false;
        break;
      }
      if (is_union) {
        if (union_width && *union_width != *width) {
          concrete = false;
          break;
        }
        union_width = *width;
        total_width = *width;
      } else {
        if (*width
            > std::numeric_limits<std::uint64_t>::max()
                  - total_width) {
          concrete = false;
          break;
        }
        total_width += *width;
      }
    }
    if (concrete
        && total_width - 1U
            <= static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) {
      if (!is_union) {
        auto offset = total_width;
        for (auto& member : type.packed_members) {
          offset -= *member.width();
          member.lsb_offset = offset;
        }
      }
      type.packed_range = PackedRange{
          static_cast<std::int64_t>(total_width - 1U),
          0,
          true};
    }
  } else if (match_keyword("enum")) {
    if (keyword("logic") || keyword("reg")
        || keyword("bit")) {
      const auto type_token = advance();
      type.spelling = type_token.text;
      type.domain =
          type_token.text == "bit"
              ? ValueDomain::Bit2
              : ValueDomain::Logic4;
      parse_optional_signedness(type);
      parse_optional_range(type);
    } else {
      error(
          current(),
          "FSIM-SV-UNSUPPORTED-026",
          "bounded enum typedefs require an explicit bit, logic, or "
          "reg base type");
      skip_to_semicolon();
      return;
    }
    expect(
        TokenKind::LeftBrace,
        "'{' before enum literals",
        "FSIM-SV-PARSE-083");
    if (at(TokenKind::RightBrace)) {
      error(
          current(),
          "FSIM-SV-PARSE-085",
          "bounded enum typedefs require at least one literal");
    }
    std::optional<std::string> previous_literal;
    while (!at_end() && !at(TokenKind::RightBrace)) {
      const auto literal =
          expect_identifier("enum literal name");
      Expression value;
      if (match(TokenKind::Assign)) {
        value = parse_expression();
      } else if (!previous_literal) {
        value = Expression{
            ExpressionKind::IntegerLiteral,
            "0",
            {},
            literal.span};
      } else {
        value = Expression{
            ExpressionKind::Binary,
            "+",
            {
                Expression{
                    ExpressionKind::Identifier,
                    *previous_literal,
                    {},
                    literal.span},
                Expression{
                    ExpressionKind::IntegerLiteral,
                    "1",
                    {},
                    literal.span},
            },
            literal.span};
      }
      enum_literals.push_back({
          literal.text, value,
          cover(literal.span, previous().span)});
      enum_parameters.push_back({
          ParameterDeclaration{
              literal.text,
              type,
              std::move(value),
              true,
              cover(literal.span, previous().span),
              ParameterKind::Value,
              std::nullopt},
          literal});
      previous_literal = literal.text;
      if (!match(TokenKind::Comma)) {
        break;
      }
    }
    expect(
        TokenKind::RightBrace,
        "'}' after enum literals",
        "FSIM-SV-PARSE-084");
  } else if (keyword("logic") || keyword("reg")
      || keyword("bit") || keyword("integer")
      || keyword("int")) {
    const auto type_token = advance();
    type.spelling = type_token.text;
    if (type_token.text == "bit") {
      type.domain = ValueDomain::Bit2;
    } else if (
        type_token.text == "integer"
        || type_token.text == "int") {
      type.domain = ValueDomain::Integer;
      type.is_signed = true;
    } else {
      type.domain = ValueDomain::Logic4;
    }
    parse_optional_signedness(type);
    parse_optional_range(type);
  } else if (is_named_type_reference_start()) {
    type = parse_named_type();
  } else {
    error(
        current(),
        "FSIM-SV-UNSUPPORTED-024",
        "bounded typedef declarations require an integral built-in "
        "or previously declared user type");
    skip_to_semicolon();
    return;
  }
  const auto name = expect_identifier("typedef name");
  if (at(TokenKind::LeftBracket)) {
    error(
        current(),
        "FSIM-SV-UNSUPPORTED-025",
        "unpacked typedef dimensions are not implemented");
    skip_balanced(
        TokenKind::LeftBracket, TokenKind::RightBracket);
  }
  expect(
      TokenKind::Semicolon,
      "';' after typedef declaration",
      "FSIM-SV-PARSE-082");
  const bool duplicate = std::any_of(
      unit.type_aliases.begin(),
      unit.type_aliases.end(),
      [&](const TypeAliasDeclaration& alias) {
        return alias.name == name.text;
      });
  const bool type_parameter_conflict = std::ranges::any_of(
      unit.parameters,
      [&](const ParameterDeclaration& parameter) {
          return parameter.kind == ParameterKind::Type
              && parameter.name == name.text;
      });
  if (type_parameter_conflict) {
    error(
        name,
        "FSIM-SV-SEM-055",
        "typedef declaration '" + name.text
            + "' conflicts with a type parameter");
    return;
  }
  if (duplicate) {
    error(
        name,
        "FSIM-SV-SEM-024",
        "duplicate typedef declaration '" + name.text + "'");
    return;
  }
  unit.type_aliases.push_back({
      name.text,
      std::move(type),
      cover(start.span, previous().span),
      std::move(enum_literals),
      TypeDeclarationKind::SystemVerilogTypedef});
  for (auto& [parameter, parameter_name] :
       enum_parameters) {
    add_parameter(
        unit, std::move(parameter), parameter_name);
  }
}

void VerilogParser::parse_optional_net_type(Type& type) {
  if (!is_net_type_keyword()) {
    return;
  }
  const auto keyword_token = advance();
  type.spelling = keyword_token.text;
  if (keyword_token.text == "bit") {
    type.domain = ValueDomain::Bit2;
  } else if (
      keyword_token.text == "byte"
      || keyword_token.text == "shortint"
      || keyword_token.text == "int"
      || keyword_token.text == "longint"
      || keyword_token.text == "integer") {
    type.domain = ValueDomain::Integer;
    type.is_signed = true;
    const auto width =
        keyword_token.text == "byte"
            ? std::int64_t{8}
        : keyword_token.text == "shortint"
            ? std::int64_t{16}
        : keyword_token.text == "longint"
            ? std::int64_t{64}
            : std::int64_t{32};
    type.packed_range = PackedRange{
        width - 1, 0, true};
  } else if (keyword_token.text == "time") {
    type.domain = ValueDomain::Bit2;
    type.is_signed = false;
    type.packed_range = PackedRange{63, 0, true};
  } else {
    type.domain = ValueDomain::Logic4;
  }
}

void VerilogParser::parse_optional_signedness(Type& type) {
  if (match_keyword("signed")) {
    type.is_signed = true;
  } else if (match_keyword("unsigned")) {
    type.is_signed = false;
  }
}

void VerilogParser::parse_optional_range(Type& type) {
  if (!match(TokenKind::LeftBracket)) {
    return;
  }
  const auto start = previous();
  auto left_expression = parse_expression();
  expect(TokenKind::Colon, "':' in packed range", "FSIM-SV-PARSE-005");
  auto right_expression = parse_expression();
  expect(TokenKind::RightBracket, "']' after packed range",
         "FSIM-SV-PARSE-006");
  const auto left = simple_integer_constant(left_expression);
  const auto right = simple_integer_constant(right_expression);
  if (left && right) {
    type.packed_range = PackedRange{*left, *right, *left >= *right};
  }
  type.packed_range_expression = PackedRangeExpression{
      std::move(left_expression),
      std::move(right_expression),
      cover(start.span, previous().span),
      std::nullopt};
}

bool VerilogParser::parse_optional_container_dimension(Type& type) {
  if (!match(TokenKind::LeftBracket)) {
    return false;
  }
  const auto start = previous();
  SystemVerilogContainerInfo container;
  if (match(TokenKind::RightBracket)) {
    container.kind =
        SystemVerilogContainerKind::DynamicArray;
  } else if (
      at(TokenKind::Identifier)
      && current().text == "$") {
    (void)advance();
    container.kind = SystemVerilogContainerKind::Queue;
    if (match(TokenKind::Colon)) {
      container.queue_maximum = parse_expression();
    }
    expect(
        TokenKind::RightBracket,
        "']' after queue dimension",
        "FSIM-SV-PARSE-155");
  } else {
    const auto built_in =
        keyword("byte") || keyword("shortint")
        || keyword("longint") || keyword("time")
        || keyword("integer") || keyword("int")
        || keyword("logic") || keyword("reg") || keyword("bit")
        || keyword("signed") || keyword("unsigned")
        || at(TokenKind::LeftBracket);
    if (built_in) {
      container.kind =
          SystemVerilogContainerKind::AssociativeArray;
      container.associative_index_type =
          std::make_shared<Type>(parse_parameter_type());
      expect(
          TokenKind::RightBracket,
          "']' after associative-array index type",
          "FSIM-SV-PARSE-157");
    } else if (keyword("string")) {
      const auto unsupported = advance();
      error(
          unsupported,
          "FSIM-SV-SEM-082",
          "string associative-array indices are not supported");
      expect(
          TokenKind::RightBracket,
          "']' after associative-array index type",
          "FSIM-SV-PARSE-157");
      return true;
    } else if (at(TokenKind::Star)) {
      error(
          current(),
          "FSIM-SV-SEM-078",
          "wildcard associative-array indices are not supported");
      (void)advance();
      expect(
          TokenKind::RightBracket,
          "']' after associative-array index type",
          "FSIM-SV-PARSE-157");
      return true;
    } else {
      auto left = parse_expression();
      if (match(TokenKind::Colon)) {
        auto right = parse_expression();
        expect(
            TokenKind::RightBracket,
            "']' after static unpacked range",
            "FSIM-SV-PARSE-158");
        container.kind =
            SystemVerilogContainerKind::StaticArray;
        const auto left_value = simple_integer_constant(left);
        const auto right_value = simple_integer_constant(right);
        if (left_value && right_value) {
          container.static_range = PackedRange{
              *left_value,
              *right_value,
              *left_value >= *right_value};
        }
        container.static_range_expressions.push_back(
            PackedRangeExpression{
                std::move(left),
                std::move(right),
                cover(start.span, previous().span),
                std::nullopt});
      } else {
        expect(
            TokenKind::RightBracket,
            "']' after associative-array index type",
            "FSIM-SV-PARSE-157");
        if (left.kind == ExpressionKind::Identifier) {
          container.kind =
              SystemVerilogContainerKind::AssociativeArray;
          Type index_type{
              ValueDomain::Unknown,
              left.text,
              std::nullopt,
              false};
          index_type.named_type = left.text;
          index_type.named_type_span = left.span;
          container.associative_index_type =
              std::make_shared<Type>(std::move(index_type));
        } else {
          error(
              start,
              "FSIM-SV-SEM-078",
              "static unpacked arrays require a left:right range; "
              "associative arrays require an integral index type");
          return true;
        }
      }
    }
  }
  container.span = cover(start.span, previous().span);
  type.systemverilog_container = std::move(container);
  if (language_ != Language::SystemVerilog2017) {
    error(
        start,
        "FSIM-SV-SEM-077",
        "dynamic arrays, queues, associative arrays, and static unpacked "
        "arrays require SystemVerilog-2017");
  }
  if (type.spelling == "wire"
      || type.domain == ValueDomain::String
      || (type.domain == ValueDomain::Unknown
          && type.named_type.empty())) {
    error(
        start,
        "FSIM-SV-SEM-079",
        "bounded containers require a packed integral variable element "
        "type");
  }
  if (at(TokenKind::LeftBracket)) {
    error(
        current(),
        "FSIM-SV-SEM-080",
        "multidimensional SystemVerilog containers are not supported");
    while (match(TokenKind::LeftBracket)) {
      while (!at_end() && !at(TokenKind::RightBracket)) {
        (void)advance();
      }
      (void)match(TokenKind::RightBracket);
    }
  }
  return true;
}

[[nodiscard]] bool VerilogParser::is_declaration_start() const  {
  return is_direction_keyword() || is_net_type_keyword()
      || keyword("string") || is_named_type_reference_start();
}

void VerilogParser::parse_event_declaration(
  DesignUnit& unit, const Token& start) {
  Type type = default_verilog_type();
  type.spelling = "event";
  type.domain = ValueDomain::Logic4;
  for (;;) {
    const auto name = expect_identifier("named event");
    const auto duplicate_signal = std::find_if(
        unit.signals.begin(),
        unit.signals.end(),
        [&](const SignalDeclaration& signal) {
          return signal.name == name.text;
        });
    const auto duplicate_port = std::find_if(
        unit.ports.begin(),
        unit.ports.end(),
        [&](const SignalDeclaration& port) {
          return port.name == name.text;
        });
    if (duplicate_signal != unit.signals.end()
        || duplicate_port != unit.ports.end()) {
      error(
          name,
          "FSIM-SV-SEM-035",
          "duplicate named event or object declaration '"
              + name.text + "'");
    } else {
      unit.signals.push_back(SignalDeclaration{
          name.text,
          type,
          PortDirection::Unknown,
          false,
          name.span});
    }
    if (!match(TokenKind::Comma)) {
      break;
    }
  }
  expect(
      TokenKind::Semicolon,
      "';' after named event declaration",
      "FSIM-SV-PARSE-117");
  if (!unit.signals.empty()) {
    unit.signals.back().span = span_from(start, previous());
  }
}

void VerilogParser::parse_declaration(DesignUnit& unit) {
  const auto start = current();
  VerilogTypeSpec spec;
  spec.type = default_verilog_type();
  if (is_direction_keyword()) {
    spec.direction = parse_direction();
    spec.type = default_port_net_type();
    const bool explicit_type =
        is_net_type_keyword()
        || is_named_type_reference_start();
    if (is_named_type_reference_start()) {
      spec.type = parse_named_type();
    } else {
      parse_optional_net_type(spec.type);
    }
    require_default_port_net_type(current(), explicit_type);
  } else if (keyword("string")) {
    spec.type = parse_parameter_type();
  } else if (is_named_type_reference_start()) {
    spec.type = parse_named_type();
  } else {
    parse_optional_net_type(spec.type);
  }
  if (spec.type.named_type.empty()) {
    parse_optional_signedness(spec.type);
    parse_optional_range(spec.type);
  }

  for (;;) {
    const auto name = expect_identifier("declared name");
    auto declaration_type = spec.type;
    (void)parse_optional_container_dimension(declaration_type);
    std::optional<Expression> initializer;
    if (match(TokenKind::Assign)) {
      initializer = parse_expression();
    }
    if (initializer
        && declaration_type.domain != ValueDomain::String
        && !declaration_type.systemverilog_container) {
      error(
          name,
          "FSIM-SV-UNSUPPORTED-011",
          "declaration initializers are not executable in this frontend "
          "slice");
      initializer.reset();
    }

    if (declaration_type.domain == ValueDomain::String
        || declaration_type.systemverilog_container) {
      if (declaration_type.systemverilog_container
          && spec.direction != PortDirection::Unknown) {
        SignalDeclaration declaration{
            name.text,
            std::move(declaration_type),
            spec.direction,
            true,
            span_from(start, previous())};
        if (!non_ansi_ports_.contains(declaration.name)
            || !body_port_declarations_.insert(
                    declaration.name).second) {
          error(
              name,
              "FSIM-SV-SEM-004",
              "duplicate port declaration '" + declaration.name + "'");
        }
        update_or_add_port(unit, std::move(declaration));
        if (!match(TokenKind::Comma)) {
          break;
        }
        continue;
      }
      const auto duplicate_variable = std::ranges::any_of(
          unit.variables,
          [&](const VariableDeclaration& variable) {
            return variable.name == name.text;
          });
      const auto object_conflict = std::ranges::any_of(
          unit.signals,
          [&](const SignalDeclaration& signal) {
            return signal.name == name.text;
          });
      const auto port_conflict = std::ranges::any_of(
          unit.ports,
          [&](const SignalDeclaration& port) {
            return port.name == name.text;
          });
      const auto parameter_conflict = std::ranges::any_of(
          unit.parameters,
          [&](const ParameterDeclaration& parameter) {
            return parameter.name == name.text;
          });
      if (duplicate_variable || object_conflict || port_conflict
          || parameter_conflict) {
        error(
            name,
            "FSIM-SV-SEM-006",
            "duplicate string variable declaration '" + name.text + "'");
      } else {
        unit.variables.push_back(VariableDeclaration{
            name.text,
            std::move(declaration_type),
            std::move(initializer),
            span_from(start, previous())});
      }
      if (!match(TokenKind::Comma)) {
        break;
      }
      continue;
    }

    SignalDeclaration declaration{
        name.text, std::move(declaration_type), spec.direction,
        spec.direction != PortDirection::Unknown,
        span_from(start, previous())};
    const auto parameter_conflict = std::any_of(
        unit.parameters.begin(),
        unit.parameters.end(),
        [&](const ParameterDeclaration& parameter) {
          return parameter.name == declaration.name;
        });
    if (declared_genvars_.contains(declaration.name)) {
      error(
          name,
          "FSIM-SV-SEM-022",
          "object '" + declaration.name
              + "' conflicts with a genvar declaration");
      if (!match(TokenKind::Comma)) {
        break;
      }
      continue;
    }
    if (parameter_conflict) {
      error(
          name,
          "FSIM-SV-SEM-020",
          "object '" + declaration.name
              + "' conflicts with a parameter declaration");
      if (!match(TokenKind::Comma)) {
        break;
      }
      continue;
    }
    const auto existing_port = std::find_if(
        unit.ports.begin(),
        unit.ports.end(),
        [&](const SignalDeclaration& port) {
          return port.name == declaration.name;
        });
    if (declaration.is_port) {
      if (!non_ansi_ports_.contains(declaration.name)
          || !body_port_declarations_.insert(declaration.name).second) {
        error(
            name,
            "FSIM-SV-SEM-004",
            "duplicate port declaration '" + declaration.name + "'");
      }
      update_or_add_port(unit, std::move(declaration));
    } else if (existing_port != unit.ports.end()) {
      // In a non-ANSI declaration, `output q; reg q;` describes one port,
      // not a distinct internal signal.
      if (!non_ansi_ports_.contains(declaration.name)
          || !port_type_refinements_.insert(declaration.name).second) {
        error(
            name,
            "FSIM-SV-SEM-005",
            "duplicate declaration of port '" + declaration.name + "'");
      } else {
        (void)update_existing_port_type(unit, declaration);
      }
    } else {
      const auto duplicate = std::find_if(
          unit.signals.begin(),
          unit.signals.end(),
          [&](const SignalDeclaration& signal) {
            return signal.name == declaration.name;
          });
      if (duplicate != unit.signals.end()) {
        error(
            name,
            "FSIM-SV-SEM-006",
            "duplicate signal declaration '" + declaration.name + "'");
      } else {
        unit.signals.push_back(std::move(declaration));
      }
    }
    if (!match(TokenKind::Comma)) {
      break;
    }
  }
  expect(TokenKind::Semicolon, "';' after declaration",
         "FSIM-SV-PARSE-008");
}

void VerilogParser::parse_procedural_declaration(Statement& block) {
  const auto start = current();
  Type type = default_verilog_type();
  if (keyword("string")) {
    type = parse_parameter_type();
  } else if (is_named_type_reference_start()) {
    type = parse_named_type();
  } else {
    parse_optional_net_type(type);
  }
  if (type.named_type.empty() && type.spelling == "wire") {
    error(
        start,
        "FSIM-SV-UNSUPPORTED-014",
        "procedural wire declarations are not supported; use a variable "
        "type");
  }
  if (type.named_type.empty()) {
    parse_optional_signedness(type);
    parse_optional_range(type);
  }

  for (;;) {
    const auto name = expect_identifier("local variable name");
    auto declaration_type = type;
    (void)parse_optional_container_dimension(declaration_type);
    std::optional<Expression> initializer;
    if (match(TokenKind::Assign)) {
      initializer = parse_expression();
    }
    block.declarations.push_back(VariableDeclaration{
        name.text,
        std::move(declaration_type),
        std::move(initializer),
        span_from(name, previous())});
    current_procedural_names_.insert(name.text);
    if (!match(TokenKind::Comma)) {
      break;
    }
  }
  expect(
      TokenKind::Semicolon,
      "';' after local variable declaration",
      "FSIM-SV-PARSE-045");
}

void VerilogParser::update_or_add_port(DesignUnit& unit,
                             SignalDeclaration declaration) {
  for (auto& existing : unit.ports) {
    if (existing.name == declaration.name) {
      existing.type = std::move(declaration.type);
      existing.direction = declaration.direction;
      existing.span = std::move(declaration.span);
      return;
    }
  }
  unit.ports.push_back(std::move(declaration));
}

bool VerilogParser::update_existing_port_type(
  DesignUnit& unit, const SignalDeclaration& declaration) {
  for (auto& existing : unit.ports) {
    if (existing.name == declaration.name) {
      existing.type = declaration.type;
      existing.span = cover(existing.span, declaration.span);
      return true;
    }
  }
  return false;
}

std::optional<Statement> VerilogParser::parse_continuous_assignment(const Token& start) {
  std::optional<Delay> delay;
  if (match(TokenKind::Hash)) {
    delay = parse_verilog_delay(previous(), 3);
  }
  if (!at(TokenKind::Identifier)) {
    error(current(), "FSIM-SV-PARSE-009",
          "expected continuous assignment target");
    skip_to_semicolon();
    return std::nullopt;
  }
  Expression target = parse_lvalue();
  expect(TokenKind::Assign, "'=' in continuous assignment",
         "FSIM-SV-PARSE-010");
  Expression value = parse_expression();
  expect(TokenKind::Semicolon, "';' after continuous assignment",
         "FSIM-SV-PARSE-011");
  Statement statement;
  statement.kind = StatementKind::Assignment;
  statement.assignment_kind = AssignmentKind::Continuous;
  statement.target = std::move(target);
  statement.value = std::move(value);
  statement.delay = std::move(delay);
  statement.span = span_from(start, previous());
  return statement;
}

[[nodiscard]] bool VerilogParser::is_gate_primitive() const  {
  return keyword("buf") || keyword("not")
      || keyword("and") || keyword("nand")
      || keyword("or") || keyword("nor")
      || keyword("xor") || keyword("xnor");
}

void VerilogParser::parse_gate_primitive(std::vector<Statement>& statements) {
  const auto start = advance();
  const auto operation = detail::ascii_lower(start.text);
  const auto strength_keyword =
      [](const std::string_view text) {
        return text == "supply0" || text == "supply1"
            || text == "strong0" || text == "strong1"
            || text == "pull0" || text == "pull1"
            || text == "weak0" || text == "weak1"
            || text == "highz0" || text == "highz1";
      };
  if (at(TokenKind::LeftParen)
      && at(TokenKind::Identifier, 1)
      && strength_keyword(current(1).text)
      && at(TokenKind::Comma, 2)
      && at(TokenKind::Identifier, 3)
      && strength_keyword(current(3).text)
      && at(TokenKind::RightParen, 4)) {
    error(
        current(),
        "FSIM-SV-UNSUPPORTED-030",
        "gate drive strengths are not implemented");
    skip_to_semicolon();
    return;
  }
  std::optional<Delay> delay;
  if (match(TokenKind::Hash)) {
    delay = parse_verilog_delay(previous(), 2);
  }

  const bool unary = operation == "buf" || operation == "not";
  do {
    const auto instance_start = current();
    if (at(TokenKind::Identifier)
        && at(TokenKind::LeftParen, 1)) {
      advance();  // Optional instance name.
    }
    expect(
        TokenKind::LeftParen,
        "'(' after gate primitive name",
        "FSIM-SV-PARSE-091");
    if (!at(TokenKind::Identifier)) {
      error(
          current(),
          "FSIM-SV-PARSE-092",
          "a gate primitive requires an output lvalue first");
      skip_to_semicolon();
      return;
    }
    auto target = parse_lvalue();
    std::vector<Expression> inputs;
    while (match(TokenKind::Comma)) {
      inputs.push_back(parse_expression());
    }
    expect(
        TokenKind::RightParen,
        "')' after gate primitive terminals",
        "FSIM-SV-PARSE-093");

    if ((unary && inputs.size() != 1)
        || (!unary && inputs.size() < 2)) {
      error(
          instance_start,
          "FSIM-SV-SEM-026",
          unary
              ? "buf/not primitives require exactly one input terminal"
              : "logic gate primitives require at least two input terminals");
      continue;
    }

    Expression value = std::move(inputs.front());
    if (unary) {
      if (operation == "not") {
        const auto combined = cover(start.span, value.span);
        value = Expression{
            ExpressionKind::Unary,
            "~",
            {std::move(value)},
            combined};
      }
    } else {
      const auto binary_operation =
          operation == "and" || operation == "nand"
              ? "&"
              : operation == "or" || operation == "nor"
                  ? "|"
                  : "^";
      for (std::size_t index = 1;
           index < inputs.size(); ++index) {
        const auto combined =
            cover(value.span, inputs[index].span);
        value = Expression{
            ExpressionKind::Binary,
            binary_operation,
            {std::move(value), std::move(inputs[index])},
            combined};
      }
      if (operation == "nand" || operation == "nor"
          || operation == "xnor") {
        const auto combined = cover(start.span, value.span);
        value = Expression{
            ExpressionKind::Unary,
            "~",
            {std::move(value)},
            combined};
      }
    }

    Statement statement;
    statement.kind = StatementKind::Assignment;
    statement.assignment_kind = AssignmentKind::Continuous;
    statement.target = std::move(target);
    statement.value = std::move(value);
    statement.delay = delay;
    statement.span = cover(start.span, previous().span);
    statements.push_back(std::move(statement));
  } while (match(TokenKind::Comma));
  expect(
      TokenKind::Semicolon,
      "';' after gate primitive",
      "FSIM-SV-PARSE-094");
}

Process VerilogParser::parse_always() {
  const auto start = advance();
  current_procedural_names_.clear();
  Process process;
  if (start.text == "always_ff") {
    process.kind = ProcessKind::SystemVerilogAlwaysFF;
  } else if (start.text == "always_comb") {
    process.kind = ProcessKind::SystemVerilogAlwaysComb;
  } else if (start.text == "always_latch") {
    process.kind = ProcessKind::SystemVerilogAlwaysLatch;
  } else {
    process.kind = ProcessKind::VerilogAlways;
  }
  if (process.kind == ProcessKind::SystemVerilogAlwaysFF
      && language_ == Language::Verilog2005) {
    error(start, "FSIM-VERILOG-SEM-001",
          "always_ff requires SystemVerilog");
  }
  if (process.kind == ProcessKind::SystemVerilogAlwaysComb
      && language_ == Language::Verilog2005) {
    error(start, "FSIM-VERILOG-SEM-002",
          "always_comb requires SystemVerilog");
  }
  if (process.kind == ProcessKind::SystemVerilogAlwaysLatch
      && language_ == Language::Verilog2005) {
    error(start, "FSIM-VERILOG-SEM-003",
          "always_latch requires SystemVerilog");
  }
  const bool implicit_sensitivity =
      process.kind == ProcessKind::SystemVerilogAlwaysComb
      || process.kind == ProcessKind::SystemVerilogAlwaysLatch;
  if (implicit_sensitivity) {
    if (match(TokenKind::At)) {
      error(
          previous(),
          "FSIM-SV-SEM-011",
          "always_comb/always_latch supplies its own implicit sensitivity "
          "and cannot have an explicit event control");
      (void)parse_sensitivity();
    }
    process.sensitivities.push_back(
        Sensitivity{EdgeKind::Any, "*", start.span});
  } else if (match(TokenKind::At)) {
    process.sensitivities = parse_sensitivity();
  } else {
    error(current(), "FSIM-SV-PARSE-012",
          "always process requires an event control in this frontend "
          "slice");
  }
  auto body = parse_statement();
  if (body) {
    if (body->kind == StatementKind::Block
        && body->label.empty()) {
      process.variables = std::move(body->declarations);
      process.statements = std::move(body->statements);
    } else {
      process.statements.push_back(std::move(*body));
    }
  }
  if (implicit_sensitivity) {
    const auto inspect =
        [&](const auto& self,
            const std::vector<Statement>& statements,
            bool& has_timing,
            bool& has_nonblocking) -> void {
      for (const auto& statement : statements) {
        has_timing =
            has_timing
            || statement.kind == StatementKind::Delay
            || statement.kind == StatementKind::WaitOn
            || (statement.kind == StatementKind::Assignment
                && statement.procedural_assignment_control
                    != ProceduralAssignmentControl::None);
        has_nonblocking =
            has_nonblocking
            || (statement.kind == StatementKind::Assignment
                && statement.assignment_kind
                    == AssignmentKind::NonBlocking);
        self(
            self, statement.statements, has_timing,
            has_nonblocking);
        self(
            self, statement.else_statements, has_timing,
            has_nonblocking);
        for (const auto& alternative : statement.case_alternatives) {
          self(
              self, alternative.statements, has_timing,
              has_nonblocking);
        }
      }
    };
    bool has_timing = false;
    bool has_nonblocking = false;
    inspect(
        inspect, process.statements, has_timing,
        has_nonblocking);
    if (has_timing) {
      error(
          start,
          "FSIM-SV-SEM-012",
          "always_comb/always_latch cannot contain timing controls");
    }
    if (has_nonblocking) {
      error(
          start,
          "FSIM-SV-SEM-013",
          "always_comb/always_latch assignments must be blocking in this "
          "executable slice");
    }
  }
  process.span = span_from(start, previous());
  current_procedural_names_.clear();
  return process;
}

}  // namespace fsim::frontend
