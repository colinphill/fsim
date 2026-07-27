// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/preprocessor.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fsim::frontend {
namespace {

struct MacroParameter {
  std::string name;
  std::optional<std::vector<Token>> default_value;
};

struct Macro {
  std::string name;
  std::optional<std::vector<MacroParameter>> parameters;
  std::vector<Token> replacement;
  SourceSpan definition;
};

struct Conditional {
  bool parent_active{};
  bool active{};
  bool branch_taken{};
  bool saw_else{};
  SourceSpan opening;
};

[[nodiscard]] bool identifier_spelling(const std::string_view spelling) {
  if (spelling.empty()) {
    return false;
  }
  const auto first = static_cast<unsigned char>(spelling.front());
  if (std::isalpha(first) == 0 && spelling.front() != '_'
      && spelling.front() != '$') {
    return false;
  }
  return std::all_of(
      spelling.begin() + 1,
      spelling.end(),
      [](const char character) {
        const auto value = static_cast<unsigned char>(character);
        return std::isalnum(value) != 0 || character == '_'
            || character == '$';
      });
}

[[nodiscard]] std::string location_text(const SourceSpan& span) {
  return span.source_name + ':' + std::to_string(span.begin.line)
      + ':' + std::to_string(span.begin.column);
}

[[nodiscard]] std::filesystem::path normalized_path(
    const std::filesystem::path& path) {
  std::error_code error;
  auto absolute = std::filesystem::absolute(path, error);
  if (error) {
    return path.lexically_normal();
  }
  auto canonical = std::filesystem::weakly_canonical(absolute, error);
  return error ? absolute.lexically_normal() : canonical;
}

class VerilogPreprocessor {
 public:
  VerilogPreprocessor(
      const Language language,
      const PreprocessorOptions& options)
      : language_(language), options_(options) {
    for (const auto& directory : options_.include_directories) {
      include_directories_.push_back(normalized_path(directory));
    }
  }

  PreprocessResult run(const std::filesystem::path& root) {
    auto compilation = run(
        std::vector<std::filesystem::path>{root});
    return single_result(std::move(compilation));
  }

  PreprocessCompilationUnitResult run(
      const std::vector<std::filesystem::path>& roots) {
    const auto fallback =
        roots.empty()
            ? std::filesystem::path{"<empty>.sv"}
            : roots.back();
    if (!valid_language(fallback.string())) {
      return finish_compilation_unit(fallback);
    }
    if (roots.empty()) {
      diagnose(
          "FSIM-SV-PP-031",
          "a Verilog compilation unit requires at least one root file",
          {fallback.string(), {}, {}});
      return finish_compilation_unit(fallback);
    }
    define_command_line_macros();
    for (const auto& root : roots) {
      process_root(normalized_path(root));
    }
    diagnose_open_conditionals();
    return finish_compilation_unit(roots.back());
  }

  PreprocessResult run(SourceText source) {
    const auto root = normalized_path(
        source.name.empty()
            ? std::filesystem::path{"<memory>.sv"}
            : std::filesystem::path{source.name});
    if (!valid_language(source.name)) {
      return single_result(
          finish_compilation_unit(root));
    }
    define_command_line_macros();
    const auto name = root.generic_string();
    source_snapshots_.emplace(name, std::move(source.text));
    inputs_.push_back(
        {root, source_snapshots_.find(name)->second});
    process_root(root);
    diagnose_open_conditionals();
    return single_result(
        finish_compilation_unit(root));
  }

 private:
  [[nodiscard]] bool valid_language(
      const std::string& source_name) {
    if (language_ == Language::Verilog2005
        || language_ == Language::SystemVerilog2017) {
      return true;
    }
    diagnose(
        "FSIM-SV-PP-001",
        "the Verilog preprocessor requires Verilog-2005 or "
        "SystemVerilog-2017 input",
        {source_name, {}, {}});
    return false;
  }

  void diagnose_open_conditionals() {
    for (const auto& conditional : conditionals_) {
      diagnose(
          "FSIM-SV-PP-002",
          "unterminated conditional compilation block",
          conditional.opening);
    }
    conditionals_.clear();
  }

  void process_root(const std::filesystem::path& root) {
    current_dependency_names_.clear();
    current_dependencies_.clear();
    process_file(root, 0);
    const auto found =
        source_snapshots_.find(root.generic_string());
    roots_.push_back({
        root,
        found == source_snapshots_.end()
            ? std::string{}
            : found->second,
        std::move(current_dependencies_)});
  }

  [[nodiscard]] bool active() const noexcept {
    return conditionals_.empty() || conditionals_.back().active;
  }

  PreprocessCompilationUnitResult finish_compilation_unit(
      const std::filesystem::path& root) {
    SourceSpan eof_span{normalized_path(root).generic_string(), {}, {}};
    if (root_eof_) {
      eof_span = root_eof_->span;
    }
    output_.push_back(
        Token{TokenKind::EndOfFile, {}, std::move(eof_span), {}});
    return {
        LexResult{std::move(output_), std::move(diagnostics_)},
        std::move(roots_),
        std::move(inputs_)};
  }

  static PreprocessResult single_result(
      PreprocessCompilationUnitResult compilation) {
    PreprocessResult result;
    result.lexed = std::move(compilation.lexed);
    if (!compilation.roots.empty()) {
      auto& root = compilation.roots.front();
      result.dependencies.push_back(
          {std::move(root.path), std::move(root.contents)});
      result.dependencies.insert(
          result.dependencies.end(),
          std::make_move_iterator(root.dependencies.begin()),
          std::make_move_iterator(root.dependencies.end()));
    }
    return result;
  }

  void diagnose(
      std::string code,
      std::string message,
      SourceSpan span,
      std::vector<std::string> expansion_stack = {}) {
    diagnostics_.push_back({
        DiagnosticSeverity::Error,
        std::move(code),
        std::move(message),
        std::move(span),
        std::move(expansion_stack)});
  }

  void define_command_line_macros() {
    for (const auto& definition : options_.defines) {
      const auto separator = definition.find('=');
      const auto name = definition.substr(0, separator);
      const auto replacement =
          separator == std::string::npos
              ? std::string{}
              : definition.substr(separator + 1);
      const SourceSpan span{
          "<command-line:-D" + definition + ">",
          SourceLocation{},
          SourceLocation{}};
      if (!identifier_spelling(name)) {
        diagnose(
            "FSIM-SV-PP-003",
            "invalid command-line macro definition '" + definition + "'",
            span);
        continue;
      }
      auto lexed = lex(
          SourceText{span.source_name, replacement}, language_);
      for (auto& diagnostic : lexed.diagnostics) {
        diagnostics_.push_back(std::move(diagnostic));
      }
      if (!lexed.tokens.empty()) {
        lexed.tokens.pop_back();
      }
      macros_[name] =
          Macro{name, std::nullopt, std::move(lexed.tokens), span};
    }
  }

  [[nodiscard]] std::optional<std::string> read_file(
      const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
      return std::nullopt;
    }
    std::string contents{
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()};
    if (!stream.good() && !stream.eof()) {
      return std::nullopt;
    }
    return contents;
  }

  void process_file(
      const std::filesystem::path& path,
      const std::size_t depth) {
    const auto normalized = normalized_path(path);
    const auto name = normalized.generic_string();
    if (depth > options_.maximum_include_depth) {
      diagnose(
          "FSIM-SV-PP-004",
          "maximum Verilog include depth exceeded while opening '"
              + name + "'",
          include_invocation_.value_or(
              SourceSpan{name, {}, {}}));
      return;
    }
    if (std::find(include_stack_.begin(), include_stack_.end(), normalized)
        != include_stack_.end()) {
      diagnose(
          "FSIM-SV-PP-005",
          "recursive Verilog include of '" + name + "'",
          include_invocation_.value_or(
              SourceSpan{name, {}, {}}));
      return;
    }
    auto snapshot = source_snapshots_.find(name);
    if (snapshot == source_snapshots_.end()) {
      auto contents = read_file(normalized);
      if (!contents) {
        diagnose(
            "FSIM-FE-IO-001",
            "unable to open source file",
            include_invocation_.value_or(
                SourceSpan{name, {}, {}}));
        return;
      }
      snapshot = source_snapshots_
                     .emplace(name, std::move(*contents))
                     .first;
      inputs_.push_back({normalized, snapshot->second});
    }
    if (depth != 0
        && current_dependency_names_.insert(name).second) {
      current_dependencies_.push_back(
          {normalized, snapshot->second});
    }

    auto lexed = lex(
        SourceText{name, snapshot->second}, language_);
    for (auto& diagnostic : lexed.diagnostics) {
      diagnostic.expansion_stack.insert(
          diagnostic.expansion_stack.begin(),
          include_ancestry_.begin(),
          include_ancestry_.end());
      diagnostics_.push_back(std::move(diagnostic));
    }
    if (!include_ancestry_.empty()) {
      for (auto& token : lexed.tokens) {
        token.expansion_stack.insert(
            token.expansion_stack.begin(),
            include_ancestry_.begin(),
            include_ancestry_.end());
      }
    }
    if (depth == 0 && !lexed.tokens.empty()) {
      root_eof_ = lexed.tokens.back();
    }

    include_stack_.push_back(normalized);
    process_tokens(lexed.tokens, depth);
    include_stack_.pop_back();
  }

  [[nodiscard]] static std::size_t line_end(
      const std::vector<Token>& tokens,
      const std::size_t begin) {
    if (begin >= tokens.size()
        || tokens[begin].kind == TokenKind::EndOfFile) {
      return begin;
    }
    auto line = tokens[begin].span.begin.line;
    std::size_t end = begin;
    while (end < tokens.size()
           && tokens[end].kind != TokenKind::EndOfFile) {
      if (tokens[end].span.begin.line != line) {
        if (end == begin
            || tokens[end - 1].text.empty()
            || tokens[end - 1].text.front() != '\\'
            || tokens[end - 1].text.find('\n')
                == std::string::npos) {
          break;
        }
        line = tokens[end].span.begin.line;
      }
      ++end;
    }
    return end;
  }

  void process_tokens(
      const std::vector<Token>& tokens,
      const std::size_t include_depth) {
    std::size_t index = 0;
    while (index < tokens.size()
           && tokens[index].kind != TokenKind::EndOfFile) {
      if (tokens[index].kind == TokenKind::Backtick
          && index + 1 < tokens.size()
          && tokens[index + 1].kind == TokenKind::Identifier) {
        const auto directive = tokens[index + 1].text;
        if (is_directive(directive)) {
          const auto end = line_end(tokens, index);
          handle_directive(
              tokens, index, end, include_depth);
          index = end;
          continue;
        }
        if (!active()) {
          ++index;
          continue;
        }
        auto expanded =
            expand_invocation(tokens, index, 0, {});
        output_.insert(
            output_.end(),
            std::make_move_iterator(expanded.begin()),
            std::make_move_iterator(expanded.end()));
        continue;
      }
      if (active()) {
        output_.push_back(tokens[index]);
      }
      ++index;
    }
  }

  [[nodiscard]] static bool is_directive(
      const std::string_view name) {
    constexpr std::string_view directives[] = {
        "define",
        "undef",
        "include",
        "ifdef",
        "ifndef",
        "elsif",
        "else",
        "endif",
        "timescale",
        "default_nettype",
        "resetall",
        "celldefine",
        "endcelldefine",
        "begin_keywords",
        "end_keywords",
        "line",
        "pragma",
        "unconnected_drive",
        "nounconnected_drive"};
    return std::find(
               std::begin(directives),
               std::end(directives),
               name)
        != std::end(directives);
  }

  void handle_directive(
      const std::vector<Token>& tokens,
      const std::size_t begin,
      const std::size_t end,
      const std::size_t include_depth) {
    const auto& tick = tokens[begin];
    const auto& name_token = tokens[begin + 1];
    const auto name = name_token.text;
    const auto arguments = begin + 2;

    if (name == "ifdef" || name == "ifndef") {
      const auto macro_name =
          directive_macro_name(tokens, arguments, end, name_token);
      const bool parent = active();
      const bool defined =
          macro_name && macros_.contains(*macro_name);
      const bool take =
          macro_name && (name == "ifdef" ? defined : !defined);
      conditionals_.push_back(
          {parent, parent && take, parent && take, false,
           cover(tick.span, name_token.span)});
      return;
    }
    if (name == "elsif") {
      if (conditionals_.empty()) {
        diagnose(
            "FSIM-SV-PP-006",
            "`elsif without a matching `ifdef or `ifndef",
            name_token.span);
        return;
      }
      auto& conditional = conditionals_.back();
      if (conditional.saw_else) {
        diagnose(
            "FSIM-SV-PP-007",
            "`elsif cannot follow `else in one conditional block",
            name_token.span);
      }
      const auto macro_name =
          directive_macro_name(tokens, arguments, end, name_token);
      const bool take =
          conditional.parent_active && !conditional.branch_taken
          && macro_name && macros_.contains(*macro_name);
      conditional.active = take;
      conditional.branch_taken = conditional.branch_taken || take;
      return;
    }
    if (name == "else") {
      if (conditionals_.empty()) {
        diagnose(
            "FSIM-SV-PP-008",
            "`else without a matching `ifdef or `ifndef",
            name_token.span);
        return;
      }
      auto& conditional = conditionals_.back();
      if (conditional.saw_else) {
        diagnose(
            "FSIM-SV-PP-009",
            "duplicate `else in one conditional block",
            name_token.span);
      }
      conditional.saw_else = true;
      conditional.active =
          conditional.parent_active && !conditional.branch_taken;
      conditional.branch_taken = true;
      reject_extra_directive_tokens(
          tokens, arguments, end, name_token);
      return;
    }
    if (name == "endif") {
      if (conditionals_.empty()) {
        diagnose(
            "FSIM-SV-PP-010",
            "`endif without a matching `ifdef or `ifndef",
            name_token.span);
        return;
      }
      reject_extra_directive_tokens(
          tokens, arguments, end, name_token);
      conditionals_.pop_back();
      return;
    }

    if (!active()) {
      return;
    }
    if (name == "define") {
      define_macro(tokens, arguments, end, name_token);
      return;
    }
    if (name == "undef") {
      const auto macro_name =
          directive_macro_name(tokens, arguments, end, name_token);
      if (macro_name) {
        macros_.erase(*macro_name);
      }
      return;
    }
    if (name == "include") {
      include_file(
          tokens, arguments, end, name_token, include_depth);
      return;
    }
    if (name == "timescale" || name == "default_nettype"
        || name == "resetall" || name == "celldefine"
        || name == "endcelldefine" || name == "begin_keywords"
        || name == "end_keywords" || name == "unconnected_drive"
        || name == "nounconnected_drive") {
      output_.push_back(tokens[begin]);
      output_.push_back(tokens[begin + 1]);
      const std::vector<Token> directive_arguments{
          tokens.begin() + arguments, tokens.begin() + end};
      auto expanded =
          expand_sequence(directive_arguments, 0, {});
      output_.insert(
          output_.end(),
          std::make_move_iterator(expanded.begin()),
          std::make_move_iterator(expanded.end()));
      return;
    }
    diagnose(
        "FSIM-SV-PP-011",
        "recognized but unsupported compiler directive `" + name + "'",
        name_token.span);
  }

  [[nodiscard]] std::optional<std::string> directive_macro_name(
      const std::vector<Token>& tokens,
      const std::size_t begin,
      const std::size_t end,
      const Token& directive) {
    if (begin >= end || tokens[begin].kind != TokenKind::Identifier) {
      diagnose(
          "FSIM-SV-PP-012",
          "compiler directive `" + directive.text
              + " requires a macro identifier",
          directive.span);
      return std::nullopt;
    }
    if (begin + 1 != end) {
      diagnose(
          "FSIM-SV-PP-013",
          "unexpected tokens after `" + directive.text
              + " macro identifier",
          tokens[begin + 1].span);
    }
    return tokens[begin].text;
  }

  void reject_extra_directive_tokens(
      const std::vector<Token>& tokens,
      const std::size_t begin,
      const std::size_t end,
      const Token& directive) {
    if (begin < end) {
      diagnose(
          "FSIM-SV-PP-014",
          "compiler directive `" + directive.text
              + " does not accept arguments",
          tokens[begin].span);
    }
  }

  void define_macro(
      const std::vector<Token>& tokens,
      const std::size_t begin,
      const std::size_t end,
      const Token& directive) {
    if (begin >= end || tokens[begin].kind != TokenKind::Identifier) {
      diagnose(
          "FSIM-SV-PP-015",
          "`define requires a macro identifier",
          directive.span);
      return;
    }
    const auto& name = tokens[begin];
    std::size_t replacement_begin = begin + 1;
    std::optional<std::vector<MacroParameter>> parameters;
    if (replacement_begin < end
        && tokens[replacement_begin].kind == TokenKind::LeftParen
        && name.span.source_name
            == tokens[replacement_begin].span.source_name
        && name.span.end.offset
            == tokens[replacement_begin].span.begin.offset) {
      parameters.emplace();
      ++replacement_begin;
      while (replacement_begin < end
             && tokens[replacement_begin].kind
                 != TokenKind::RightParen) {
        if (tokens[replacement_begin].kind
            != TokenKind::Identifier) {
          diagnose(
              "FSIM-SV-PP-016",
              "function-like macro parameter list is malformed",
              tokens[replacement_begin].span);
          return;
        }
        MacroParameter parameter;
        parameter.name = tokens[replacement_begin++].text;
        if (std::any_of(
                parameters->begin(),
                parameters->end(),
                [&](const MacroParameter& existing) {
                  return existing.name == parameter.name;
                })) {
          diagnose(
              "FSIM-SV-PP-017",
              "duplicate macro parameter '" + parameter.name + "'",
              tokens[replacement_begin - 1].span);
          return;
        }
        if (replacement_begin < end
            && tokens[replacement_begin].kind
                == TokenKind::Assign) {
          ++replacement_begin;
          parameter.default_value.emplace();
          std::size_t parentheses = 0;
          std::size_t brackets = 0;
          std::size_t braces = 0;
          while (replacement_begin < end) {
            const auto& token = tokens[replacement_begin];
            if (parentheses == 0 && brackets == 0 && braces == 0
                && (token.kind == TokenKind::Comma
                    || token.kind == TokenKind::RightParen)) {
              break;
            }
            if (token.kind == TokenKind::LeftParen) {
              ++parentheses;
            } else if (
                token.kind == TokenKind::RightParen
                && parentheses != 0) {
              --parentheses;
            } else if (token.kind == TokenKind::LeftBracket) {
              ++brackets;
            } else if (
                token.kind == TokenKind::RightBracket
                && brackets != 0) {
              --brackets;
            } else if (token.kind == TokenKind::LeftBrace) {
              ++braces;
            } else if (
                token.kind == TokenKind::RightBrace
                && braces != 0) {
              --braces;
            }
            if (!tokens[replacement_begin].text.empty()
                && tokens[replacement_begin].text.front() == '\\'
                && tokens[replacement_begin].text.find('\n')
                    != std::string::npos) {
              ++replacement_begin;
              continue;
            }
            parameter.default_value->push_back(
                tokens[replacement_begin++]);
          }
        }
        parameters->push_back(std::move(parameter));
        if (replacement_begin < end
            && tokens[replacement_begin].kind == TokenKind::Comma) {
          ++replacement_begin;
          if (replacement_begin >= end
              || tokens[replacement_begin].kind
                  == TokenKind::RightParen) {
            diagnose(
                "FSIM-SV-PP-016",
                "function-like macro parameter list has a trailing comma",
                tokens[replacement_begin - 1].span);
            return;
          }
        } else if (
            replacement_begin < end
            && tokens[replacement_begin].kind
                != TokenKind::RightParen) {
          diagnose(
              "FSIM-SV-PP-016",
              "function-like macro parameters must be comma-separated",
              tokens[replacement_begin].span);
          return;
        }
      }
      if (replacement_begin >= end
          || tokens[replacement_begin].kind
              != TokenKind::RightParen) {
        diagnose(
            "FSIM-SV-PP-018",
            "unterminated function-like macro parameter list",
            name.span);
        return;
      }
      ++replacement_begin;
    }
    std::vector<Token> replacement;
    for (auto position = replacement_begin;
         position < end; ++position) {
      // The lexer represents a preprocessor line continuation as an escaped
      // identifier containing the backslash and newline. It joins physical
      // lines but contributes no replacement token.
      if (!tokens[position].text.empty()
          && tokens[position].text.front() == '\\'
          && tokens[position].text.find('\n') != std::string::npos) {
        continue;
      }
      replacement.push_back(tokens[position]);
    }
    macros_[name.text] =
        Macro{name.text, std::move(parameters), std::move(replacement),
              name.span};
  }

  [[nodiscard]] std::optional<std::filesystem::path> resolve_include(
      const std::string_view requested,
      const bool quoted) const {
    std::error_code error;
    if (quoted && !include_stack_.empty()) {
      const auto local = normalized_path(
          include_stack_.back().parent_path()
          / std::filesystem::path{requested});
      if (std::filesystem::is_regular_file(local, error) && !error) {
        return local;
      }
      error.clear();
    }
    for (const auto& directory : include_directories_) {
      const auto candidate =
          normalized_path(directory / std::filesystem::path{requested});
      if (std::filesystem::is_regular_file(candidate, error) && !error) {
        return candidate;
      }
      error.clear();
    }
    return std::nullopt;
  }

  void include_file(
      const std::vector<Token>& tokens,
      const std::size_t begin,
      const std::size_t end,
      const Token& directive,
      const std::size_t include_depth) {
    if (begin >= end) {
      diagnose(
          "FSIM-SV-PP-019",
          "`include requires a quoted or angle-bracket file name",
          directive.span);
      return;
    }
    std::vector<Token> replacement;
    std::size_t cursor = begin;
    if (tokens[cursor].kind == TokenKind::Backtick) {
      replacement = expand_invocation(tokens, cursor, 0, {});
      if (cursor != end || replacement.size() != 1) {
        diagnose(
            "FSIM-SV-PP-020",
            "macro-expanded `include name must produce exactly one token",
            tokens[begin].span);
        return;
      }
    } else {
      replacement.assign(tokens.begin() + begin, tokens.begin() + end);
      cursor = end;
    }

    bool quoted = false;
    std::string requested;
    if (replacement.size() == 1
        && replacement.front().kind == TokenKind::StringLiteral
        && replacement.front().text.size() >= 2) {
      quoted = true;
      requested = replacement.front().text.substr(
          1, replacement.front().text.size() - 2);
    } else if (
        replacement.size() >= 3
        && replacement.front().kind == TokenKind::Less
        && replacement.back().kind == TokenKind::Greater) {
      for (std::size_t index = 1;
           index + 1 < replacement.size(); ++index) {
        requested += replacement[index].text;
      }
    } else {
      diagnose(
          "FSIM-SV-PP-021",
          "`include name must be a string literal or <file>",
          replacement.front().span,
          replacement.front().expansion_stack);
      return;
    }
    const auto resolved = resolve_include(requested, quoted);
    if (!resolved) {
      diagnose(
          "FSIM-SV-PP-022",
          "cannot resolve Verilog include '" + requested + "'",
          directive.span);
      return;
    }
    const auto previous_invocation = include_invocation_;
    include_invocation_ = directive.span;
    include_ancestry_.push_back(
        "included '" + resolved->generic_string() + "' from "
        + location_text(directive.span));
    process_file(*resolved, include_depth + 1);
    include_ancestry_.pop_back();
    include_invocation_ = previous_invocation;
  }

  [[nodiscard]] std::vector<std::vector<Token>> parse_arguments(
      const std::vector<Token>& tokens,
      std::size_t& index,
      const Macro& macro,
      const Token& invocation) {
    std::vector<std::vector<Token>> arguments;
    if (index >= tokens.size()
        || tokens[index].kind != TokenKind::LeftParen) {
      diagnose(
          "FSIM-SV-PP-023",
          "function-like macro `" + macro.name
              + " requires an argument list",
          invocation.span,
          invocation.expansion_stack);
      return arguments;
    }
    ++index;
    std::vector<Token> current;
    std::size_t parentheses = 1;
    std::size_t brackets = 0;
    std::size_t braces = 0;
    bool closed = false;
    while (index < tokens.size()
           && tokens[index].kind != TokenKind::EndOfFile) {
      const auto& token = tokens[index++];
      if (token.kind == TokenKind::LeftParen) {
        ++parentheses;
      } else if (token.kind == TokenKind::RightParen) {
        if (parentheses == 1 && brackets == 0 && braces == 0) {
          closed = true;
          break;
        }
        --parentheses;
      } else if (token.kind == TokenKind::LeftBracket) {
        ++brackets;
      } else if (token.kind == TokenKind::RightBracket
                 && brackets != 0) {
        --brackets;
      } else if (token.kind == TokenKind::LeftBrace) {
        ++braces;
      } else if (token.kind == TokenKind::RightBrace && braces != 0) {
        --braces;
      }
      if (token.kind == TokenKind::Comma && parentheses == 1
          && brackets == 0 && braces == 0) {
        arguments.push_back(std::move(current));
        current.clear();
      } else {
        current.push_back(token);
      }
    }
    if (!closed) {
      diagnose(
          "FSIM-SV-PP-024",
          "unterminated argument list for macro `" + macro.name + "'",
          invocation.span,
          invocation.expansion_stack);
      return {};
    }
    if (!current.empty() || !arguments.empty()
        || (macro.parameters && !macro.parameters->empty())) {
      arguments.push_back(std::move(current));
    }
    return arguments;
  }

  [[nodiscard]] std::vector<Token> concatenate_tokens(
      std::vector<Token> tokens,
      const Token& invocation,
      const std::vector<std::string>& expansion_stack) {
    std::vector<Token> result;
    for (std::size_t index = 0; index < tokens.size(); ++index) {
      if (index + 1 < tokens.size()
          && tokens[index].kind == TokenKind::Backtick
          && tokens[index + 1].kind == TokenKind::Backtick) {
        if (result.empty() || index + 2 >= tokens.size()) {
          diagnose(
              "FSIM-SV-PP-025",
              "token concatenation requires tokens on both sides",
              invocation.span,
              expansion_stack);
          index += 1;
          continue;
        }
        const auto joined =
            result.back().text + tokens[index + 2].text;
        auto lexed =
            lex(SourceText{invocation.span.source_name, joined}, language_);
        if (!lexed.ok() || lexed.tokens.size() != 2
            || lexed.tokens.front().kind == TokenKind::EndOfFile) {
          diagnose(
              "FSIM-SV-PP-026",
              "token concatenation produced invalid token '" + joined + "'",
              invocation.span,
              expansion_stack);
          index += 2;
          continue;
        }
        result.back().kind = lexed.tokens.front().kind;
        result.back().text = joined;
        result.back().span = invocation.span;
        result.back().expansion_stack = expansion_stack;
        index += 2;
        continue;
      }
      result.push_back(std::move(tokens[index]));
    }
    return result;
  }

  [[nodiscard]] std::vector<Token> expand_sequence(
      const std::vector<Token>& tokens,
      const std::size_t depth,
      const std::vector<std::string>& expansion_stack) {
    std::vector<Token> result;
    std::size_t index = 0;
    while (index < tokens.size()) {
      if (tokens[index].kind == TokenKind::Backtick
          && index + 1 < tokens.size()
          && tokens[index + 1].kind == TokenKind::Identifier) {
        auto expanded = expand_invocation(
            tokens, index, depth, expansion_stack);
        result.insert(
            result.end(),
            std::make_move_iterator(expanded.begin()),
            std::make_move_iterator(expanded.end()));
      } else {
        result.push_back(tokens[index++]);
      }
    }
    return result;
  }

  [[nodiscard]] std::vector<Token> expand_invocation(
      const std::vector<Token>& tokens,
      std::size_t& index,
      const std::size_t depth,
      const std::vector<std::string>& inherited_stack) {
    const auto tick = tokens[index++];
    if (index >= tokens.size()
        || tokens[index].kind != TokenKind::Identifier) {
      diagnose(
          "FSIM-SV-PP-027",
          "a backtick must be followed by a macro identifier",
          tick.span,
          inherited_stack);
      return {};
    }
    const auto name_token = tokens[index++];
    const auto invocation_span = cover(tick.span, name_token.span);

    if (name_token.text == "__FILE__") {
      Token token{
          TokenKind::StringLiteral,
          '"' + invocation_span.source_name + '"',
          invocation_span,
          inherited_stack};
      return {std::move(token)};
    }
    if (name_token.text == "__LINE__") {
      Token token{
          TokenKind::Number,
          std::to_string(invocation_span.begin.line),
          invocation_span,
          inherited_stack};
      return {std::move(token)};
    }
    const auto found = macros_.find(name_token.text);
    if (found == macros_.end()) {
      diagnose(
          "FSIM-SV-PP-028",
          "undefined Verilog macro or compiler directive `"
              + name_token.text + "'",
          invocation_span,
          inherited_stack);
      return {};
    }
    const auto& macro = found->second;
    if (depth >= options_.maximum_macro_expansion_depth
        || expanding_.contains(macro.name)) {
      diagnose(
          "FSIM-SV-PP-029",
          "recursive or excessively deep expansion of macro `"
              + macro.name + "'",
          invocation_span,
          inherited_stack);
      return {};
    }

    std::vector<std::vector<Token>> arguments;
    if (macro.parameters) {
      arguments =
          parse_arguments(tokens, index, macro, name_token);
      const auto provided_arguments = arguments.size();
      if (provided_arguments > macro.parameters->size()) {
        diagnose(
            "FSIM-SV-PP-030",
            "macro `" + macro.name + "' expects "
                + std::to_string(macro.parameters->size())
                + " argument(s), received "
                + std::to_string(provided_arguments),
            invocation_span,
            inherited_stack);
        return {};
      }
      arguments.resize(macro.parameters->size());
      for (std::size_t parameter = 0;
           parameter < macro.parameters->size(); ++parameter) {
        const bool missing = parameter >= provided_arguments;
        const bool omitted =
            missing || arguments[parameter].empty();
        if (omitted
            && (*macro.parameters)[parameter].default_value) {
          arguments[parameter] =
              *(*macro.parameters)[parameter].default_value;
        } else if (missing) {
          diagnose(
              "FSIM-SV-PP-030",
              "macro `" + macro.name
                  + "' is missing required argument '"
                  + (*macro.parameters)[parameter].name + "'",
              invocation_span,
              inherited_stack);
          return {};
        }
      }
    }

    auto expansion_stack = inherited_stack;
    expansion_stack.push_back(
        "macro `" + macro.name + "' defined at "
        + location_text(macro.definition) + ", expanded at "
        + location_text(invocation_span));
    std::vector<Token> substituted;
    for (std::size_t replacement_index = 0;
         replacement_index < macro.replacement.size();
         ++replacement_index) {
      const auto& replacement =
          macro.replacement[replacement_index];
      if (replacement.kind == TokenKind::Backtick
          && replacement_index + 1 < macro.replacement.size()
          && macro.replacement[replacement_index + 1].kind
              == TokenKind::StringLiteral) {
        const auto& string_token =
            macro.replacement[replacement_index + 1];
        if (string_token.text.size() >= 3
            && string_token.text[string_token.text.size() - 2]
                == '`') {
          const auto parameter_name = string_token.text.substr(
              1, string_token.text.size() - 3);
          const auto parameter_match =
              macro.parameters
                  ? std::find_if(
                        macro.parameters->begin(),
                        macro.parameters->end(),
                        [&](const MacroParameter& parameter) {
                          return parameter.name == parameter_name;
                        })
                  : std::vector<MacroParameter>::const_iterator{};
          if (macro.parameters
              && parameter_match != macro.parameters->end()) {
            const auto parameter = static_cast<std::size_t>(
                std::distance(
                    macro.parameters->begin(), parameter_match));
            std::string stringified{"\""};
            for (std::size_t argument_index = 0;
                 argument_index < arguments[parameter].size();
                 ++argument_index) {
              if (argument_index != 0) {
                stringified.push_back(' ');
              }
              for (const auto character :
                   arguments[parameter][argument_index].text) {
                if (character == '\\' || character == '"') {
                  stringified.push_back('\\');
                }
                stringified.push_back(character);
              }
            }
            stringified.push_back('"');
            substituted.push_back({
                TokenKind::StringLiteral,
                std::move(stringified),
                invocation_span,
                expansion_stack});
            ++replacement_index;
            continue;
          }
        }
      }
      std::optional<std::size_t> parameter;
      if (macro.parameters
          && replacement.kind == TokenKind::Identifier) {
        const auto match = std::find_if(
            macro.parameters->begin(),
            macro.parameters->end(),
            [&](const MacroParameter& candidate) {
              return candidate.name == replacement.text;
            });
        if (match != macro.parameters->end()) {
          parameter = static_cast<std::size_t>(
              std::distance(macro.parameters->begin(), match));
        }
      }
      if (parameter) {
        for (auto argument : arguments[*parameter]) {
          argument.expansion_stack = expansion_stack;
          substituted.push_back(std::move(argument));
        }
      } else {
        auto token = replacement;
        token.span = invocation_span;
        token.expansion_stack = expansion_stack;
        substituted.push_back(std::move(token));
      }
    }
    substituted = concatenate_tokens(
        std::move(substituted), name_token, expansion_stack);
    expanding_.insert(macro.name);
    auto expanded =
        expand_sequence(substituted, depth + 1, expansion_stack);
    expanding_.erase(macro.name);
    return expanded;
  }

  Language language_;
  PreprocessorOptions options_;
  std::vector<std::filesystem::path> include_directories_;
  std::unordered_map<std::string, Macro> macros_;
  std::unordered_set<std::string> expanding_;
  std::vector<Conditional> conditionals_;
  std::vector<std::filesystem::path> include_stack_;
  std::optional<SourceSpan> include_invocation_;
  std::vector<std::string> include_ancestry_;
  std::unordered_map<std::string, std::string> source_snapshots_;
  std::unordered_set<std::string> current_dependency_names_;
  std::vector<PreprocessedDependency> current_dependencies_;
  std::vector<PreprocessedRoot> roots_;
  std::vector<PreprocessedDependency> inputs_;
  std::vector<Token> output_;
  std::vector<Diagnostic> diagnostics_;
  std::optional<Token> root_eof_;
};

}  // namespace

PreprocessResult preprocess_verilog_file(
    const std::filesystem::path& path,
    const Language language,
    const PreprocessorOptions& options) {
  return VerilogPreprocessor(language, options).run(path);
}

PreprocessResult preprocess_verilog(
    SourceText source,
    const Language language,
    const PreprocessorOptions& options) {
  return VerilogPreprocessor(language, options).run(
      std::move(source));
}

PreprocessCompilationUnitResult preprocess_verilog_compilation_unit(
    const std::vector<std::filesystem::path>& paths,
    const Language language,
    const PreprocessorOptions& options) {
  return VerilogPreprocessor(language, options).run(paths);
}

}  // namespace fsim::frontend
