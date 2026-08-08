// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

namespace fsim::frontend {

namespace {

[[nodiscard]] bool dpi_word(
    const Token& token, const std::string_view word) {
  return token.kind == TokenKind::Identifier && token.text == word;
}

[[nodiscard]] std::optional<std::size_t>
matching_right_parenthesis(
    const std::span<const Token> tokens,
    const std::size_t left) {
  std::size_t depth{};
  for (std::size_t position = left; position < tokens.size(); ++position) {
    if (tokens[position].kind == TokenKind::LeftParen) {
      ++depth;
    } else if (tokens[position].kind == TokenKind::RightParen) {
      if (depth == 0 || --depth == 0) {
        return depth == 0 ? std::optional{position} : std::nullopt;
      }
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool valid_c_identifier(const std::string_view name) {
  const auto letter = [](const char character) {
    return (character >= 'a' && character <= 'z')
        || (character >= 'A' && character <= 'Z');
  };
  if (name.empty() || (!letter(name.front()) && name.front() != '_')) {
    return false;
  }
  return std::ranges::all_of(name.substr(1), [&](const char character) {
    return letter(character)
        || (character >= '0' && character <= '9')
        || character == '_';
  });
}

}  // namespace

bool VerilogParser::dpi_declaration_start(
    const std::string_view direction) const {
  return keyword(direction) && at(TokenKind::StringLiteral, 1);
}

void VerilogParser::parse_dpi_declaration(
    std::vector<SystemVerilogDpiDeclaration>& declarations,
    const Token& start,
    const SystemVerilogDpiDirection direction,
    const SystemVerilogDpiOwnerKind owner_kind,
    std::string owner_identity) {
  const auto diagnostics_before = diagnostics_.size();
  SystemVerilogDpiDeclaration declaration;
  declaration.direction = direction;
  declaration.owner_kind = owner_kind;
  declaration.owner_identity = std::move(owner_identity);
  declaration.tokens.push_back(start);

  std::size_t parenthesis_depth{};
  std::size_t bracket_depth{};
  std::size_t brace_depth{};
  bool terminated = false;
  while (!at_end()) {
    const auto token = advance();
    declaration.tokens.push_back(token);
    switch (token.kind) {
    case TokenKind::LeftParen:
      ++parenthesis_depth;
      break;
    case TokenKind::RightParen:
      if (parenthesis_depth > 0) {
        --parenthesis_depth;
      }
      break;
    case TokenKind::LeftBracket:
      ++bracket_depth;
      break;
    case TokenKind::RightBracket:
      if (bracket_depth > 0) {
        --bracket_depth;
      }
      break;
    case TokenKind::LeftBrace:
      ++brace_depth;
      break;
    case TokenKind::RightBrace:
      if (brace_depth > 0) {
        --brace_depth;
      }
      break;
    case TokenKind::Semicolon:
      terminated = parenthesis_depth == 0
          && bracket_depth == 0
          && brace_depth == 0;
      break;
    default:
      break;
    }
    if (terminated) {
      break;
    }
  }

  if (!terminated) {
    error(
        start,
        "FSIM-SV-PARSE-324",
        "unterminated DPI declaration; expected ';'");
  }
  declaration.span = span_from(
      start, declaration.tokens.back());
  structure_dpi_declaration(declaration);
  declaration.validated = diagnostics_.size() == diagnostics_before;
  declarations.push_back(std::move(declaration));
}

void VerilogParser::structure_dpi_declaration(
    SystemVerilogDpiDeclaration& declaration) {
  if (declaration.tokens.size() < 2) {
    return;
  }
  const auto& tokens = declaration.tokens;
  declaration.link_name_token = tokens[1];
  declaration.link_name = string_literal_text(tokens[1]);
  if (declaration.link_name != "DPI-C") {
    error(
        tokens[1],
        "FSIM-SV-SEM-222",
        "DPI declarations require the link string \"DPI-C\"");
  }

  std::size_t position = 2;
  if (position < tokens.size()
      && (dpi_word(tokens[position], "pure")
          || dpi_word(tokens[position], "context"))) {
    declaration.qualifier_token = tokens[position];
    declaration.qualifier = tokens[position].text == "pure"
        ? SystemVerilogDpiQualifier::Pure
        : SystemVerilogDpiQualifier::Context;
    ++position;
  }

  if (position + 1 < tokens.size()
      && tokens[position].kind == TokenKind::Identifier
      && tokens[position + 1].kind == TokenKind::Assign) {
    declaration.c_identifier_token = tokens[position];
    declaration.c_identifier = tokens[position].text;
    position += 2;
  }
  if (declaration.c_identifier
      && !valid_c_identifier(*declaration.c_identifier)) {
    error(
        *declaration.c_identifier_token,
        "FSIM-SV-SEM-226",
        "a DPI C linkage name must be a portable C identifier");
  }

  if (position >= tokens.size()
      || (!dpi_word(tokens[position], "function")
          && !dpi_word(tokens[position], "task"))) {
    const auto& location = position < tokens.size()
        ? tokens[position]
        : tokens.back();
    error(
        location,
        "FSIM-SV-PARSE-325",
        "expected 'function' or 'task' in DPI declaration");
    return;
  }
  declaration.callable_token = tokens[position];
  const auto callable_position = position;
  declaration.callable_kind = tokens[position].text == "function"
      ? SystemVerilogDpiCallableKind::Function
      : SystemVerilogDpiCallableKind::Task;
  ++position;

  std::optional<std::size_t> name_position;
  if (declaration.direction == SystemVerilogDpiDirection::Export
      || declaration.callable_kind == SystemVerilogDpiCallableKind::Task) {
    if (position < tokens.size()
        && tokens[position].kind == TokenKind::Identifier
        && !keyword_reserved(keyword_set_, tokens[position].text)) {
      name_position = position;
    }
  } else {
    for (std::size_t index = position; index < tokens.size(); ++index) {
      if (tokens[index].kind == TokenKind::LeftParen) {
        if (index > position
            && tokens[index - 1].kind == TokenKind::Identifier
            && !keyword_reserved(
                keyword_set_, tokens[index - 1].text)) {
          name_position = index - 1;
        }
        break;
      }
      if (tokens[index].kind == TokenKind::Semicolon) {
        for (std::size_t candidate = index;
             candidate > position; --candidate) {
          if (tokens[candidate - 1].kind == TokenKind::Identifier) {
            if (!keyword_reserved(
                    keyword_set_, tokens[candidate - 1].text)) {
              name_position = candidate - 1;
              break;
            }
          }
        }
        break;
      }
    }
  }
  if (!name_position) {
    const auto& location = position < tokens.size()
        ? tokens[position]
        : tokens.back();
    error(
        location,
        "FSIM-SV-PARSE-326",
        "expected a SystemVerilog callable name in DPI declaration");
    return;
  }
  declaration.systemverilog_name_token = tokens[*name_position];
  declaration.systemverilog_name = tokens[*name_position].text;
  structure_dpi_profile(
      declaration, callable_position, *name_position);
}

void VerilogParser::structure_dpi_profile(
    SystemVerilogDpiDeclaration& declaration,
    const std::size_t callable_position,
    const std::size_t name_position) {
  const auto& tokens = declaration.tokens;
  declaration.profile_tokens.assign(
      tokens.begin() + static_cast<std::ptrdiff_t>(callable_position),
      tokens.end());
  declaration.profile_span = span_from(
      tokens[callable_position], tokens.back());

  if (declaration.direction == SystemVerilogDpiDirection::Export) {
    if (declaration.qualifier != SystemVerilogDpiQualifier::None) {
      error(
          *declaration.qualifier_token,
          "FSIM-SV-SEM-223",
          "a DPI export cannot have a pure or context qualifier");
    }
    if (name_position + 1U >= tokens.size()
        || tokens[name_position + 1U].kind != TokenKind::Semicolon) {
      error(
          tokens[name_position],
          "FSIM-SV-PARSE-327",
          "a DPI export contains only its callable kind and name");
    }
    return;
  }

  if (declaration.qualifier == SystemVerilogDpiQualifier::Pure
      && declaration.callable_kind == SystemVerilogDpiCallableKind::Task) {
    error(
        *declaration.qualifier_token,
        "FSIM-SV-SEM-224",
        "an imported DPI task cannot be pure");
  }
  if (declaration.callable_kind == SystemVerilogDpiCallableKind::Function) {
    declaration.return_type_tokens.assign(
        tokens.begin()
            + static_cast<std::ptrdiff_t>(callable_position + 1U),
        tokens.begin() + static_cast<std::ptrdiff_t>(name_position));
    if (declaration.return_type_tokens.empty()) {
      error(
          tokens[callable_position],
          "FSIM-SV-PARSE-328",
          "an imported DPI function requires a return type");
    }
  }

  const auto left = name_position + 1U;
  if (left >= tokens.size()
      || tokens[left].kind != TokenKind::LeftParen) {
    error(
        tokens[name_position],
        "FSIM-SV-PARSE-327",
        "an imported DPI callable requires a parenthesized formal profile");
    return;
  }
  const auto right = matching_right_parenthesis(tokens, left);
  if (!right || *right + 1U >= tokens.size()
      || tokens[*right + 1U].kind != TokenKind::Semicolon
      || *right + 2U != tokens.size()) {
    error(
        tokens[left],
        "FSIM-SV-PARSE-327",
        "an imported DPI formal profile must be balanced and end at ';'");
    return;
  }
  declaration.formal_tokens.assign(
      tokens.begin() + static_cast<std::ptrdiff_t>(left + 1U),
      tokens.begin() + static_cast<std::ptrdiff_t>(*right));
  if (declaration.formal_tokens.empty()) {
    return;
  }

  const auto parse_formal = [&](const std::size_t first,
                                const std::size_t last) {
    if (first == last) {
      error(
          tokens[left],
          "FSIM-SV-PARSE-328",
          "a DPI formal profile contains an empty formal");
      return;
    }
    SystemVerilogDpiFormal formal;
    formal.tokens.assign(
        tokens.begin() + static_cast<std::ptrdiff_t>(first),
        tokens.begin() + static_cast<std::ptrdiff_t>(last));
    formal.span = span_from(tokens[first], tokens[last - 1U]);
    std::size_t position = first;
    if (position + 1U < last
        && dpi_word(tokens[position], "const")
        && dpi_word(tokens[position + 1U], "ref")) {
      formal.const_reference = true;
      formal.direction = PortDirection::Ref;
      formal.direction_token = tokens[position + 1U];
      position += 2U;
    } else if (position < last
               && contains_word(
                   {"input", "output", "inout", "ref"},
                   tokens[position].text)) {
      formal.direction_token = tokens[position];
      formal.direction = tokens[position].text == "output"
          ? PortDirection::Output
          : tokens[position].text == "inout"
              ? PortDirection::Inout
              : tokens[position].text == "ref"
                  ? PortDirection::Ref
                  : PortDirection::Input;
      ++position;
    }

    std::size_t parentheses{};
    std::size_t brackets{};
    std::size_t braces{};
    std::size_t assign = last;
    std::optional<std::size_t> formal_name;
    for (std::size_t index = position; index < last; ++index) {
      const bool top_level = parentheses == 0
          && brackets == 0 && braces == 0;
      if (top_level && tokens[index].kind == TokenKind::Assign) {
        assign = index;
        break;
      }
      if (top_level && tokens[index].kind == TokenKind::Identifier
          && !keyword_reserved(keyword_set_, tokens[index].text)) {
        formal_name = index;
      }
      if (tokens[index].kind == TokenKind::LeftParen) ++parentheses;
      else if (tokens[index].kind == TokenKind::RightParen
               && parentheses > 0) --parentheses;
      else if (tokens[index].kind == TokenKind::LeftBracket) ++brackets;
      else if (tokens[index].kind == TokenKind::RightBracket
               && brackets > 0) --brackets;
      else if (tokens[index].kind == TokenKind::LeftBrace) ++braces;
      else if (tokens[index].kind == TokenKind::RightBrace
               && braces > 0) --braces;
    }
    if (!formal_name || *formal_name == position) {
      error(
          tokens[first],
          "FSIM-SV-PARSE-328",
          "a DPI formal requires an explicit type and name");
      return;
    }
    formal.type_tokens.assign(
        tokens.begin() + static_cast<std::ptrdiff_t>(position),
        tokens.begin() + static_cast<std::ptrdiff_t>(*formal_name));
    formal.name = tokens[*formal_name].text;
    formal.name_token = tokens[*formal_name];
    formal.dimension_tokens.assign(
        tokens.begin() + static_cast<std::ptrdiff_t>(*formal_name + 1U),
        tokens.begin() + static_cast<std::ptrdiff_t>(assign));
    if (assign < last) {
      formal.default_tokens.assign(
          tokens.begin() + static_cast<std::ptrdiff_t>(assign + 1U),
          tokens.begin() + static_cast<std::ptrdiff_t>(last));
      error(
          tokens[assign],
          "FSIM-SV-SEM-227",
          "a DPI formal cannot have a default value");
    }
    declaration.formals.push_back(std::move(formal));
  };

  std::size_t first = left + 1U;
  std::size_t parentheses{};
  std::size_t brackets{};
  std::size_t braces{};
  for (std::size_t position = first; position < *right; ++position) {
    if (tokens[position].kind == TokenKind::Comma
        && parentheses == 0 && brackets == 0 && braces == 0) {
      parse_formal(first, position);
      first = position + 1U;
      continue;
    }
    if (tokens[position].kind == TokenKind::LeftParen) ++parentheses;
    else if (tokens[position].kind == TokenKind::RightParen
             && parentheses > 0) --parentheses;
    else if (tokens[position].kind == TokenKind::LeftBracket) ++brackets;
    else if (tokens[position].kind == TokenKind::RightBracket
             && brackets > 0) --brackets;
    else if (tokens[position].kind == TokenKind::LeftBrace) ++braces;
    else if (tokens[position].kind == TokenKind::RightBrace
             && braces > 0) --braces;
  }
  parse_formal(first, *right);
  if (declaration.qualifier == SystemVerilogDpiQualifier::Pure
      && std::ranges::any_of(
          declaration.formals,
          [](const SystemVerilogDpiFormal& formal) {
            return formal.direction != PortDirection::Input;
          })) {
    error(
        *declaration.qualifier_token,
        "FSIM-SV-SEM-225",
        "a pure DPI function can have only input formals");
  }
}

bool VerilogParser::compilation_unit_class_method_definition_start() const {
  std::optional<std::size_t> candidate;
  std::size_t parentheses{};
  std::size_t brackets{};
  for (std::size_t lookahead{};
       !at(TokenKind::EndOfFile, lookahead); ++lookahead) {
    const bool top_level = parentheses == 0 && brackets == 0;
    if (top_level
        && (at(TokenKind::LeftParen, lookahead)
            || at(TokenKind::Semicolon, lookahead))) {
      break;
    }
    if (top_level
        && ((at(TokenKind::Identifier, lookahead)
                && !keyword_reserved(
                    keyword_set_, current(lookahead).text))
            || current(lookahead).text == "new")) {
      candidate = lookahead;
    }
    if (at(TokenKind::LeftParen, lookahead)) ++parentheses;
    else if (at(TokenKind::RightParen, lookahead)
             && parentheses > 0) --parentheses;
    else if (at(TokenKind::LeftBracket, lookahead)) ++brackets;
    else if (at(TokenKind::RightBracket, lookahead)
             && brackets > 0) --brackets;
  }
  return candidate && *candidate > 0
      && at(TokenKind::Scope, *candidate - 1U);
}

bool VerilogParser::compilation_unit_dpi_export_definition_start(
    const std::vector<SystemVerilogDpiDeclaration>& declarations,
    const SystemVerilogDpiCallableKind kind) const {
  std::optional<std::size_t> candidate;
  std::size_t brackets{};
  for (std::size_t lookahead{};
       !at(TokenKind::EndOfFile, lookahead); ++lookahead) {
    const bool top_level = brackets == 0;
    if (top_level
        && (at(TokenKind::LeftParen, lookahead)
            || at(TokenKind::Semicolon, lookahead))) {
      break;
    }
    if (top_level && at(TokenKind::Identifier, lookahead)
        && !keyword_reserved(keyword_set_, current(lookahead).text)) {
      candidate = lookahead;
    }
    if (at(TokenKind::LeftBracket, lookahead)) ++brackets;
    else if (at(TokenKind::RightBracket, lookahead) && brackets > 0) {
      --brackets;
    }
  }
  if (!candidate
      || (*candidate > 0 && at(TokenKind::Scope, *candidate - 1U))) {
    return false;
  }
  const auto name = current(*candidate).text;
  return std::ranges::any_of(
      declarations,
      [&](const SystemVerilogDpiDeclaration& declaration) {
        return declaration.direction == SystemVerilogDpiDirection::Export
            && declaration.callable_kind == kind
            && declaration.systemverilog_name == name;
      });
}

void VerilogParser::resolve_dpi_declarations(
    std::vector<SystemVerilogDpiDeclaration>& declarations,
    const std::vector<FunctionDeclaration>& functions,
    const std::vector<TaskDeclaration>& tasks) {
  std::unordered_set<std::string> systemverilog_names;
  std::unordered_set<std::string> export_linkage_names;
  for (auto& declaration : declarations) {
    const auto before = diagnostics_.size();
    if (declaration.systemverilog_name.empty()) {
      continue;
    }
    declaration.linkage_name = declaration.c_identifier.value_or(
        declaration.systemverilog_name);
    if (!systemverilog_names.insert(
            declaration.systemverilog_name).second) {
      error(
          *declaration.systemverilog_name_token,
          "FSIM-SV-SEM-228",
          "duplicate DPI declaration for SystemVerilog name '"
              + declaration.systemverilog_name + "'");
    }

    const auto function = std::ranges::find(
        functions,
        declaration.systemverilog_name,
        &FunctionDeclaration::name);
    const auto task = std::ranges::find(
        tasks,
        declaration.systemverilog_name,
        &TaskDeclaration::name);
    if (declaration.direction == SystemVerilogDpiDirection::Import) {
      if (function != functions.end() || task != tasks.end()) {
        error(
            *declaration.systemverilog_name_token,
            "FSIM-SV-SEM-229",
            "DPI import conflicts with native callable '"
                + declaration.systemverilog_name + "'");
      }
    } else {
      if (!export_linkage_names.insert(declaration.linkage_name).second) {
        error(
            *declaration.systemverilog_name_token,
            "FSIM-SV-SEM-228",
            "duplicate DPI export C linkage name '"
                + declaration.linkage_name + "'");
      }
      const bool function_export = declaration.callable_kind
          == SystemVerilogDpiCallableKind::Function;
      if ((function_export && function == functions.end())
          || (!function_export && task == tasks.end())) {
        error(
            *declaration.systemverilog_name_token,
            "FSIM-SV-SEM-230",
            "DPI export does not resolve to a same-kind native callable '"
                + declaration.systemverilog_name + "'");
      } else {
        SystemVerilogDpiResolvedProfile profile;
        if (function_export) {
          profile.return_type = function->return_type;
          profile.callable_span = function->span;
          for (const auto& argument : function->arguments) {
            profile.formals.push_back({
                argument.reference ? PortDirection::Ref
                                   : argument.direction,
                argument.type,
                argument.name,
                argument.reference,
                argument.span});
          }
        } else {
          profile.callable_span = task->span;
          for (const auto& argument : task->arguments) {
            profile.formals.push_back({
                argument.reference ? PortDirection::Ref
                                   : argument.direction,
                argument.type,
                argument.name,
                argument.reference,
                argument.span});
          }
        }
        declaration.resolved_profile = std::move(profile);
      }
    }
    declaration.validated = declaration.validated
        && diagnostics_.size() == before;
  }
}

}  // namespace fsim::frontend
