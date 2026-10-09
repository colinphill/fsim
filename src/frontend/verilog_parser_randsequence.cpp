// SPDX-License-Identifier: Apache-2.0
// `randcase` (IEEE 1800-2017 18.16) and `randsequence` (18.17) are rewritten
// into ordinary procedural statements: weighted alternatives select with
// `$urandom`, productions expand inline in named blocks, and `break` and
// `return` in a code block disable the randsequence or production block.
#include "verilog_parser_internal.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <set>

namespace fsim::frontend {

namespace {

struct RsItem {
  Token name;
  std::vector<Token> arguments;
  bool has_arguments{};
};

struct RsCaseItem {
  std::vector<Token> labels;
  bool is_default{};
  RsItem item;
};

struct RsProd {
  enum class Kind { Code, Item, If, Repeat, Case, Join } kind{Kind::Code};
  std::vector<Token> tokens;
  RsItem item;
  std::optional<RsItem> else_item;
  std::vector<RsCaseItem> cases;
  std::vector<RsItem> join_items;
};

struct RsRule {
  std::vector<RsProd> prods;
  std::vector<Token> weight;
  std::vector<Token> weight_code;
};

struct RsProduction {
  Token name;
  std::vector<Token> return_type;
  std::vector<Token> formals;
  std::vector<RsRule> rules;
};

[[nodiscard]] bool loop_keyword(const Token& token) {
  return token.kind == TokenKind::Identifier
      && (token.text == "for" || token.text == "foreach"
          || token.text == "while" || token.text == "do"
          || token.text == "repeat" || token.text == "forever");
}

}  // namespace

std::vector<Token> VerilogParser::synthetic_tokens(
    const std::string_view text, const SourceSpan& span) const {
  auto lexed = lex(SourceText{"<generated>", std::string{text}}, language_);
  std::vector<Token> tokens;
  tokens.reserve(lexed.tokens.size());
  for (auto& token : lexed.tokens) {
    if (token.kind == TokenKind::EndOfFile) break;
    token.span = span;
    tokens.push_back(std::move(token));
  }
  return tokens;
}

std::optional<Statement> VerilogParser::parse_generated_statement(
    std::vector<Token> tokens, const SourceSpan& span) {
  Token end;
  end.kind = TokenKind::EndOfFile;
  end.span = span;
  tokens.push_back(std::move(end));
  auto saved_tokens = std::move(tokens_);
  const auto saved_index = index_;
  tokens_ = std::move(tokens);
  index_ = 0;
  auto statement = parse_statement();
  tokens_ = std::move(saved_tokens);
  index_ = saved_index;
  return statement;
}

std::vector<Token> VerilogParser::balanced_tokens(
    const TokenKind open, const TokenKind close) {
  std::vector<Token> inner;
  if (!at(open)) {
    error(current(), "FSIM-SV-PARSE-389",
        std::string{"expected '"} + to_string(open) + "' in randsequence");
    return inner;
  }
  (void)advance();
  std::size_t depth = 1U;
  while (!at_end()) {
    if (at(open)) {
      ++depth;
    } else if (at(close) && --depth == 0U) {
      (void)advance();
      return inner;
    }
    inner.push_back(advance());
  }
  error(current(), "FSIM-SV-PARSE-389",
      std::string{"unterminated '"} + to_string(open) + "' in randsequence");
  return inner;
}

std::optional<Statement> VerilogParser::parse_randcase(const Token& start) {
  const auto suffix = std::to_string(generated_statement_counter_++);
  const auto sum = "__fsim_rc_s" + suffix;
  const auto pick = "__fsim_rc_r" + suffix;
  std::vector<std::vector<Token>> weights;
  std::vector<std::vector<Token>> bodies;
  while (!at_end() && !keyword("endcase")) {
    std::vector<Token> weight;
    std::size_t depth{};
    while (!at_end() && !(depth == 0U && at(TokenKind::Colon))) {
      if (at(TokenKind::LeftParen) || at(TokenKind::LeftBracket)
          || at(TokenKind::LeftBrace)) {
        ++depth;
      } else if ((at(TokenKind::RightParen) || at(TokenKind::RightBracket)
                     || at(TokenKind::RightBrace))
          && depth != 0U) {
        --depth;
      }
      weight.push_back(advance());
    }
    expect(TokenKind::Colon, "':' after randcase weight", "FSIM-SV-PARSE-390");
    // The statement is parsed once to find its extent; the rewritten
    // statement is parsed again below and reports any diagnostics.
    const auto body_begin = index_;
    const auto saved_diagnostics = diagnostics_.size();
    (void)parse_statement();
    diagnostics_.resize(saved_diagnostics);
    std::vector<Token> body(
        tokens_.begin() + static_cast<std::ptrdiff_t>(body_begin),
        tokens_.begin() + static_cast<std::ptrdiff_t>(index_));
    if (weight.empty() || body.empty()) break;
    weights.push_back(std::move(weight));
    bodies.push_back(std::move(body));
  }
  expect_keyword("endcase", false, "FSIM-SV-PARSE-390");
  const auto span = span_from(start, previous());
  std::vector<Token> out;
  const auto emit = [&](const std::string_view text) {
    auto tokens = synthetic_tokens(text, start.span);
    out.insert(out.end(), tokens.begin(), tokens.end());
  };
  const auto emit_weight_sum = [&](const std::size_t count) {
    emit("0");
    for (std::size_t index = 0; index < count; ++index) {
      emit("+ (");
      out.insert(out.end(), weights[index].begin(), weights[index].end());
      emit(")");
    }
  };
  emit("begin int " + sum + ", " + pick + "; " + sum + " = ");
  emit_weight_sum(weights.size());
  emit("; if (" + sum + " > 0) begin " + pick + " = $urandom % " + sum
      + ";");
  for (std::size_t index = 0; index < bodies.size(); ++index) {
    emit(std::string{index == 0U ? "" : "else "} + "if (" + pick + " < ");
    emit_weight_sum(index + 1U);
    emit(") begin");
    out.insert(out.end(), bodies[index].begin(), bodies[index].end());
    emit("end");
  }
  emit("end end");
  return parse_generated_statement(std::move(out), span);
}

std::optional<Statement> VerilogParser::parse_randsequence(const Token& start) {
  expect(TokenKind::LeftParen, "'(' after randsequence", "FSIM-SV-PARSE-389");
  std::optional<Token> top;
  if (at(TokenKind::Identifier)) top = advance();
  expect(TokenKind::RightParen, "')' after randsequence production",
      "FSIM-SV-PARSE-389");

  std::vector<RsProduction> productions;
  const auto parse_item = [&]() {
    RsItem item;
    item.name = expect_identifier("randsequence production name");
    if (at(TokenKind::LeftParen)) {
      item.has_arguments = true;
      item.arguments =
          balanced_tokens(TokenKind::LeftParen, TokenKind::RightParen);
    }
    return item;
  };
  const auto rule_end = [&]() {
    return at_end() || at(TokenKind::Pipe) || at(TokenKind::Semicolon)
        || at(TokenKind::ColonEqual) || keyword("endsequence");
  };
  const auto parse_prod = [&]() {
    RsProd prod;
    if (at(TokenKind::LeftBrace)) {
      prod.kind = RsProd::Kind::Code;
      prod.tokens = balanced_tokens(TokenKind::LeftBrace, TokenKind::RightBrace);
    } else if (match_keyword("if")) {
      prod.kind = RsProd::Kind::If;
      prod.tokens = balanced_tokens(TokenKind::LeftParen, TokenKind::RightParen);
      prod.item = parse_item();
      if (match_keyword("else")) prod.else_item = parse_item();
    } else if (match_keyword("repeat")) {
      prod.kind = RsProd::Kind::Repeat;
      prod.tokens = balanced_tokens(TokenKind::LeftParen, TokenKind::RightParen);
      prod.item = parse_item();
    } else if (match_keyword("case")) {
      prod.kind = RsProd::Kind::Case;
      prod.tokens = balanced_tokens(TokenKind::LeftParen, TokenKind::RightParen);
      while (!at_end() && !keyword("endcase")) {
        RsCaseItem item;
        if (match_keyword("default")) {
          item.is_default = true;
          (void)match(TokenKind::Colon);
        } else {
          std::size_t depth{};
          while (!at_end() && !(depth == 0U && at(TokenKind::Colon))) {
            if (at(TokenKind::LeftParen) || at(TokenKind::LeftBracket)
                || at(TokenKind::LeftBrace)) {
              ++depth;
            } else if ((at(TokenKind::RightParen)
                           || at(TokenKind::RightBracket)
                           || at(TokenKind::RightBrace))
                && depth != 0U) {
              --depth;
            }
            item.labels.push_back(advance());
          }
          expect(TokenKind::Colon, "':' after randsequence case item",
              "FSIM-SV-PARSE-389");
        }
        item.item = parse_item();
        expect(TokenKind::Semicolon, "';' after randsequence case item",
            "FSIM-SV-PARSE-389");
        prod.cases.push_back(std::move(item));
      }
      expect_keyword("endcase", false, "FSIM-SV-PARSE-389");
    } else {
      prod.kind = RsProd::Kind::Item;
      prod.item = parse_item();
    }
    return prod;
  };

  while (!at_end() && !keyword("endsequence")) {
    const auto progress = position();
    RsProduction production;
    while (!at_end() && !(at(TokenKind::Identifier)
               && (at(TokenKind::Colon, 1) || at(TokenKind::LeftParen, 1))
               && !keyword("void"))) {
      production.return_type.push_back(advance());
    }
    production.name = expect_identifier("randsequence production name");
    if (at(TokenKind::LeftParen)) {
      production.formals =
          balanced_tokens(TokenKind::LeftParen, TokenKind::RightParen);
    }
    expect(TokenKind::Colon, "':' after randsequence production name",
        "FSIM-SV-PARSE-389");
    do {
      RsRule rule;
      if (keyword("rand") && keyword("join", 1)) {
        (void)advance();
        (void)advance();
        if (at(TokenKind::LeftParen)) {
          (void)balanced_tokens(TokenKind::LeftParen, TokenKind::RightParen);
        }
        RsProd join;
        join.kind = RsProd::Kind::Join;
        while (!rule_end()) join.join_items.push_back(parse_item());
        rule.prods.push_back(std::move(join));
      } else {
        while (!rule_end()) {
          const auto before = position();
          rule.prods.push_back(parse_prod());
          if (position() == before) {
            (void)advance();
          }
        }
      }
      if (match(TokenKind::ColonEqual)) {
        if (at(TokenKind::LeftParen)) {
          rule.weight = balanced_tokens(TokenKind::LeftParen, TokenKind::RightParen);
        } else {
          rule.weight.push_back(advance());
        }
        if (at(TokenKind::LeftBrace)) {
          rule.weight_code =
              balanced_tokens(TokenKind::LeftBrace, TokenKind::RightBrace);
        }
      }
      production.rules.push_back(std::move(rule));
    } while (match(TokenKind::Pipe));
    expect(TokenKind::Semicolon, "';' after randsequence production",
        "FSIM-SV-PARSE-389");
    productions.push_back(std::move(production));
    if (position() == progress) (void)advance();
  }
  expect_keyword("endsequence", false, "FSIM-SV-PARSE-389");
  const auto span = span_from(start, previous());
  if (productions.empty()) {
    Statement empty;
    empty.kind = StatementKind::Null;
    empty.span = span;
    return empty;
  }

  const auto suffix = std::to_string(generated_statement_counter_++);
  const auto top_label = "__fsim_rs" + suffix;
  std::size_t block_counter{};
  std::vector<Token> out;
  std::vector<Token> hoisted;
  bool valid = true;
  const auto emit = [&](const std::string_view text) {
    auto tokens = synthetic_tokens(text, start.span);
    out.insert(out.end(), tokens.begin(), tokens.end());
  };
  // Each expansion renames its formals so that repeated expansions in one
  // callable declare distinct variables.
  std::vector<std::map<std::string, std::string>> renames(1);
  const auto renamed = [&](Token token,
                           const std::map<std::string, std::string>& names) {
    if (token.kind == TokenKind::Identifier) {
      if (const auto found = names.find(token.text); found != names.end()) {
        token.text = found->second;
      }
    }
    return token;
  };
  const auto append = [&](const std::vector<Token>& tokens) {
    for (const auto& token : tokens) {
      out.push_back(renamed(token, renames.back()));
    }
  };
  std::vector<std::string> active;
  std::function<void(const RsItem&)> expand_item;
  const auto emit_code = [&](const std::vector<Token>& code,
                             const std::string& production_label) {
    emit("begin");
    bool in_loop = false;
    for (std::size_t index = 0; index < code.size(); ++index) {
      const auto& token = code[index];
      in_loop = in_loop || loop_keyword(token);
      if (!in_loop && token.kind == TokenKind::Identifier
          && token.text == "break") {
        emit("disable " + top_label);
        continue;
      }
      if (!in_loop && token.kind == TokenKind::Identifier
          && token.text == "return") {
        if (index + 1U < code.size()
            && code[index + 1U].kind != TokenKind::Semicolon) {
          error(token, "FSIM-SV-UNSUPPORTED-046",
              "a randsequence production returning a value is not "
              "supported");
          valid = false;
        }
        emit("disable " + production_label);
        continue;
      }
      out.push_back(renamed(token, renames.back()));
    }
    emit("end");
  };
  const auto expand_production = [&](const RsProduction& production,
                                     const RsItem* call) {
    if (std::ranges::find(active, production.name.text) != active.end()) {
      error(production.name, "FSIM-SV-UNSUPPORTED-046",
          "recursive randsequence production '" + production.name.text
              + "' is not supported");
      valid = false;
      return;
    }
    if (!production.return_type.empty()
        && !(production.return_type.size() == 1U
            && production.return_type.front().text == "void")) {
      error(production.name, "FSIM-SV-UNSUPPORTED-046",
          "a randsequence production returning a value is not supported");
      valid = false;
      return;
    }
    active.push_back(production.name.text);
    const auto label = "__fsim_rs" + suffix + "_"
        + std::to_string(block_counter++) + "_" + production.name.text;
    emit("begin : " + label);
    // Formals become block variables initialized from the actuals.
    std::vector<std::vector<Token>> formals(1);
    for (const auto& token : production.formals) {
      if (token.kind == TokenKind::Comma) {
        formals.emplace_back();
      } else {
        formals.back().push_back(token);
      }
    }
    std::vector<std::vector<Token>> actuals(1);
    std::size_t depth{};
    if (call != nullptr) {
      for (const auto& token : call->arguments) {
        if (token.kind == TokenKind::LeftParen
            || token.kind == TokenKind::LeftBracket
            || token.kind == TokenKind::LeftBrace) {
          ++depth;
        } else if (token.kind == TokenKind::RightParen
            || token.kind == TokenKind::RightBracket
            || token.kind == TokenKind::RightBrace) {
          --depth;
        }
        if (depth == 0U && token.kind == TokenKind::Comma) {
          actuals.emplace_back();
        } else {
          actuals.back().push_back(token);
        }
      }
    }
    std::map<std::string, std::string> local;
    for (std::size_t index = 0; index < formals.size(); ++index) {
      auto formal = formals[index];
      if (formal.empty()) continue;
      if (formal.front().text == "input") formal.erase(formal.begin());
      const auto assign = std::ranges::find(
          formal, TokenKind::Assign, &Token::kind);
      std::vector<Token> declaration(formal.begin(), assign);
      std::vector<Token> fallback;
      if (assign != formal.end()) fallback.assign(assign + 1, formal.end());
      const auto& actual = index < actuals.size() && !actuals[index].empty()
          ? actuals[index]
          : fallback;
      const auto name = std::ranges::find_if(
          declaration.rbegin(), declaration.rend(), [](const Token& token) {
            return token.kind == TokenKind::Identifier;
          });
      if (name != declaration.rend()) {
        local[name->text] = label + "_" + name->text;
      }
      // The declaration is hoisted to the randsequence block; a loop may
      // repeat this expansion.
      for (const auto& token : declaration) {
        hoisted.push_back(renamed(token, local));
      }
      auto semicolon = synthetic_tokens(";", start.span);
      hoisted.insert(hoisted.end(), semicolon.begin(), semicolon.end());
      if (!actual.empty() && name != declaration.rend()) {
        emit(local[name->text] + " = (");
        append(actual);
        emit(");");
      }
    }
    renames.push_back(std::move(local));
    const auto emit_rule = [&](const RsRule& rule) {
      if (!rule.weight_code.empty()) emit_code(rule.weight_code, label);
      for (const auto& prod : rule.prods) {
        switch (prod.kind) {
        case RsProd::Kind::Code:
          emit_code(prod.tokens, label);
          break;
        case RsProd::Kind::Item:
          expand_item(prod.item);
          break;
        case RsProd::Kind::If:
          emit("if (");
          append(prod.tokens);
          emit(")");
          expand_item(prod.item);
          if (prod.else_item) {
            emit("else");
            expand_item(*prod.else_item);
          }
          break;
        case RsProd::Kind::Repeat:
          emit("repeat (");
          append(prod.tokens);
          emit(")");
          expand_item(prod.item);
          break;
        case RsProd::Kind::Case:
          emit("case (");
          append(prod.tokens);
          emit(")");
          for (const auto& item : prod.cases) {
            if (item.is_default) {
              emit("default");
            } else {
              append(item.labels);
            }
            emit(":");
            expand_item(item.item);
          }
          emit("endcase");
          break;
        case RsProd::Kind::Join:
          // Running the productions in order is one of the interleavings
          // `rand join` may select.
          for (const auto& item : prod.join_items) expand_item(item);
          break;
        }
      }
    };
    if (production.rules.size() == 1U) {
      emit_rule(production.rules.front());
    } else {
      const auto index_suffix = suffix + "_" + std::to_string(block_counter++);
      const auto sum = "__fsim_rs_s" + index_suffix;
      const auto pick = "__fsim_rs_r" + index_suffix;
      const auto emit_weight_sum = [&](const std::size_t count) {
        emit("0");
        for (std::size_t index = 0; index < count; ++index) {
          emit("+ (");
          if (production.rules[index].weight.empty()) {
            emit("1");
          } else {
            append(production.rules[index].weight);
          }
          emit(")");
        }
      };
      {
        auto declaration = synthetic_tokens(
            "int " + sum + ", " + pick + ";", start.span);
        hoisted.insert(hoisted.end(), declaration.begin(), declaration.end());
      }
      emit("begin " + sum + " = ");
      emit_weight_sum(production.rules.size());
      emit("; if (" + sum + " > 0) begin " + pick + " = $urandom % " + sum
          + ";");
      for (std::size_t index = 0; index < production.rules.size(); ++index) {
        emit(std::string{index == 0U ? "" : "else "} + "if (" + pick + " < ");
        emit_weight_sum(index + 1U);
        emit(") begin");
        emit_rule(production.rules[index]);
        emit("end");
      }
      emit("end end");
    }
    emit("end");
    renames.pop_back();
    active.pop_back();
  };
  expand_item = [&](const RsItem& item) {
    const auto production = std::ranges::find_if(
        productions, [&](const RsProduction& candidate) {
          return candidate.name.text == item.name.text;
        });
    if (production == productions.end()) {
      error(item.name, "FSIM-SV-SEM-403",
          "randsequence production '" + item.name.text + "' is not declared");
      valid = false;
      return;
    }
    expand_production(*production, item.has_arguments ? &item : nullptr);
  };

  const RsProduction* first = &productions.front();
  if (top) {
    const auto found = std::ranges::find_if(
        productions, [&](const RsProduction& candidate) {
          return candidate.name.text == top->text;
        });
    if (found == productions.end()) {
      error(*top, "FSIM-SV-SEM-403",
          "randsequence production '" + top->text + "' is not declared");
      return std::nullopt;
    }
    first = &*found;
  }
  expand_production(*first, nullptr);
  if (!valid) return std::nullopt;
  auto statement = synthetic_tokens("begin : " + top_label, start.span);
  statement.insert(statement.end(), hoisted.begin(), hoisted.end());
  statement.insert(statement.end(), out.begin(), out.end());
  auto end = synthetic_tokens("end", start.span);
  statement.insert(statement.end(), end.begin(), end.end());
  return parse_generated_statement(std::move(statement), span);
}

}  // namespace fsim::frontend
