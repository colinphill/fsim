// SPDX-License-Identifier: Apache-2.0
#include "fsim/project/project.hpp"

#include "../diagnostic/artifact_identity.hpp"
#include "fsim/support/path.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace fsim::project {
namespace {

constexpr std::string_view kSyntaxCode = "FSIM-PROJ-0001";
constexpr std::string_view kTypeCode = "FSIM-PROJ-0002";
constexpr std::string_view kUnknownCode = "FSIM-PROJ-0003";
constexpr std::string_view kDuplicateCode = "FSIM-PROJ-0004";
constexpr std::string_view kSchemaCode = "FSIM-PROJ-0005";
constexpr std::string_view kRequiredCode = "FSIM-PROJ-0006";
constexpr std::string_view kValueCode = "FSIM-PROJ-0007";
constexpr std::string_view kIoCode = "FSIM-PROJ-0008";
constexpr std::string_view kSourceCode = "FSIM-PROJ-0009";

enum class TokenKind {
  bare,
  string,
  integer,
  equals,
  left_bracket,
  right_bracket,
  comma,
  dot,
  newline,
  end,
  invalid,
};

struct Token {
  TokenKind kind{TokenKind::invalid};
  std::string text;
  diagnostic::SourceSpan span;
};

class Lexer {
 public:
  Lexer(
      const std::string_view source,
      std::string source_name,
      diagnostic::Engine& diagnostics)
      : source_(source),
        source_name_(std::move(source_name)),
        diagnostics_(diagnostics) {}

  Token next() {
    skip_horizontal_space_and_comments();
    const auto begin = position();
    if (at_end()) {
      return make_token(TokenKind::end, {}, begin);
    }

    const char character = peek();
    switch (character) {
      case '\n':
        advance();
        return make_token(TokenKind::newline, "\n", begin);
      case '=':
        advance();
        return make_token(TokenKind::equals, "=", begin);
      case '[':
        advance();
        return make_token(TokenKind::left_bracket, "[", begin);
      case ']':
        advance();
        return make_token(TokenKind::right_bracket, "]", begin);
      case ',':
        advance();
        return make_token(TokenKind::comma, ",", begin);
      case '.':
        advance();
        return make_token(TokenKind::dot, ".", begin);
      case '"':
        return lex_basic_string(begin);
      case '\'':
        return lex_literal_string(begin);
      default:
        return lex_bare(begin);
    }
  }

 private:
  [[nodiscard]] bool at_end() const noexcept {
    return index_ >= source_.size();
  }

  [[nodiscard]] char peek(const std::size_t lookahead = 0) const noexcept {
    const auto location = index_ + lookahead;
    return location < source_.size() ? source_[location] : '\0';
  }

  char advance() noexcept {
    const char character = source_[index_++];
    if (character == '\n') {
      ++line_;
      column_ = 1;
    } else {
      ++column_;
    }
    return character;
  }

  [[nodiscard]] diagnostic::SourcePosition position() const noexcept {
    return {
        static_cast<std::uint32_t>(line_),
        static_cast<std::uint32_t>(column_),
        static_cast<std::uint64_t>(index_)};
  }

  [[nodiscard]] Token make_token(
      const TokenKind kind,
      std::string text,
      const diagnostic::SourcePosition begin) const {
    return {
        kind,
        std::move(text),
        {source_name_, begin, position()},
    };
  }

  void skip_horizontal_space_and_comments() {
    for (;;) {
      while (!at_end() && (peek() == ' ' || peek() == '\t' || peek() == '\r')) {
        advance();
      }
      if (peek() != '#') {
        return;
      }
      while (!at_end() && peek() != '\n') {
        advance();
      }
    }
  }

  Token lex_basic_string(const diagnostic::SourcePosition begin) {
    advance();
    std::string value;
    while (!at_end()) {
      const char character = advance();
      if (character == '"') {
        return make_token(TokenKind::string, std::move(value), begin);
      }
      if (character == '\n') {
        diagnostics_.error(
            std::string(kSyntaxCode),
            "basic strings may not contain a newline",
            {source_name_, begin, position()});
        return make_token(TokenKind::invalid, std::move(value), begin);
      }
      if (character != '\\') {
        value.push_back(character);
        continue;
      }
      if (at_end()) {
        break;
      }
      switch (const char escaped = advance()) {
        case 'b':
          value.push_back('\b');
          break;
        case 't':
          value.push_back('\t');
          break;
        case 'n':
          value.push_back('\n');
          break;
        case 'f':
          value.push_back('\f');
          break;
        case 'r':
          value.push_back('\r');
          break;
        case '"':
          value.push_back('"');
          break;
        case '\\':
          value.push_back('\\');
          break;
        default:
          diagnostics_.error(
              std::string(kSyntaxCode),
              std::string("unsupported escape sequence \\") + escaped + "'",
              {source_name_, begin, position()});
          return make_token(TokenKind::invalid, std::move(value), begin);
      }
    }
    diagnostics_.error(
        std::string(kSyntaxCode),
        "unterminated basic string",
        {source_name_, begin, position()});
    return make_token(TokenKind::invalid, std::move(value), begin);
  }

  Token lex_literal_string(const diagnostic::SourcePosition begin) {
    advance();
    std::string value;
    while (!at_end()) {
      const char character = advance();
      if (character == '\'') {
        return make_token(TokenKind::string, std::move(value), begin);
      }
      if (character == '\n') {
        diagnostics_.error(
            std::string(kSyntaxCode),
            "literal strings may not contain a newline",
            {source_name_, begin, position()});
        return make_token(TokenKind::invalid, std::move(value), begin);
      }
      value.push_back(character);
    }
    diagnostics_.error(
        std::string(kSyntaxCode),
        "unterminated literal string",
        {source_name_, begin, position()});
    return make_token(TokenKind::invalid, std::move(value), begin);
  }

  Token lex_bare(const diagnostic::SourcePosition begin) {
    std::string value;
    while (!at_end()) {
      const char character = peek();
      if (character == ' ' || character == '\t' || character == '\r' ||
          character == '\n' || character == '#' || character == '=' ||
          character == '[' || character == ']' || character == ',' ||
          character == '.') {
        break;
      }
      value.push_back(advance());
    }

    if (value.empty()) {
      const char invalid = advance();
      diagnostics_.error(
          std::string(kSyntaxCode),
          std::string("unexpected character '") + invalid + "'",
          {source_name_, begin, position()});
      return make_token(TokenKind::invalid, std::string(1, invalid), begin);
    }

    const auto first_digit =
        value.front() == '+' || value.front() == '-' ? std::size_t{1} : std::size_t{0};
    const bool integer =
        first_digit < value.size() &&
        std::all_of(value.begin() + static_cast<std::ptrdiff_t>(first_digit),
                    value.end(),
                    [](const char character) {
                      return std::isdigit(static_cast<unsigned char>(character)) != 0 ||
                             character == '_';
                    });
    return make_token(integer ? TokenKind::integer : TokenKind::bare, std::move(value), begin);
  }

  std::string_view source_;
  std::string source_name_;
  diagnostic::Engine& diagnostics_;
  std::size_t index_{0};
  std::size_t line_{1};
  std::size_t column_{1};
};

struct Value {
  enum class Kind {
    string,
    integer,
    boolean,
    array,
    invalid,
  };

  Kind kind{Kind::invalid};
  std::string text;
  bool boolean{false};
  std::vector<Value> elements;
  diagnostic::SourceSpan span;
};

enum class Context {
  root,
  project,
  top,
  source_set,
  binding,
  library_map,
  elaboration,
  build,
  run,
  systemc,
  unknown,
};

std::string lowercase(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(), [](const unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return result;
}

bool valid_library_name(const std::string_view value) {
  if (value.empty()) {
    return false;
  }
  const auto identifier_character = [](const unsigned char character) {
    return std::isalnum(character) != 0 || character == '_';
  };
  const auto first = static_cast<unsigned char>(value.front());
  if (std::isalpha(first) == 0 && first != '_') {
    return false;
  }
  return std::ranges::all_of(
      value.substr(1),
      [&](const char character) {
        return identifier_character(static_cast<unsigned char>(character));
      });
}

bool parse_unsigned(const std::string_view spelling, std::uint64_t& result) {
  if (spelling.empty() || spelling.front() == '-') {
    return false;
  }
  std::string normalized;
  normalized.reserve(spelling.size());
  for (const char character : spelling) {
    if (character != '_' && character != '+') {
      normalized.push_back(character);
    }
  }
  if (normalized.empty()) {
    return false;
  }
  const auto [end, error] =
      std::from_chars(normalized.data(), normalized.data() + normalized.size(), result);
  return error == std::errc{} && end == normalized.data() + normalized.size();
}

class Parser {
 public:
  Parser(
      const std::string_view source,
      std::string source_name,
      const std::filesystem::path& base_directory,
      diagnostic::Engine& diagnostics)
      : lexer_(source, source_name, diagnostics),
        source_name_(std::move(source_name)),
        base_directory_(base_directory),
        diagnostics_(diagnostics) {
    current_ = lexer_.next();
  }

  std::optional<Config> run() {
    config_.base_directory = base_directory_;
    while (current_.kind != TokenKind::end) {
      if (consume(TokenKind::newline)) {
        continue;
      }
      if (current_.kind == TokenKind::left_bracket) {
        parse_header();
      } else {
        parse_assignment();
      }
    }
    validate();
    if (diagnostics_.has_error()) {
      return std::nullopt;
    }
    return std::move(config_);
  }

 private:
  void advance() {
    current_ = lexer_.next();
  }

  bool consume(const TokenKind kind) {
    if (current_.kind != kind) {
      return false;
    }
    advance();
    return true;
  }

  bool expect(const TokenKind kind, const std::string_view description) {
    if (consume(kind)) {
      return true;
    }
    diagnostics_.error(
        std::string(kSyntaxCode),
        "expected " + std::string(description),
        current_.span);
    return false;
  }

  void recover_line() {
    while (current_.kind != TokenKind::newline && current_.kind != TokenKind::end) {
      advance();
    }
    consume(TokenKind::newline);
  }

  std::optional<std::string> parse_key() {
    if (current_.kind != TokenKind::bare && current_.kind != TokenKind::string) {
      diagnostics_.error(std::string(kSyntaxCode), "expected a key", current_.span);
      return std::nullopt;
    }
    std::string result = current_.text;
    advance();
    while (consume(TokenKind::dot)) {
      if (current_.kind != TokenKind::bare && current_.kind != TokenKind::string) {
        diagnostics_.error(
            std::string(kSyntaxCode), "expected a key after '.'", current_.span);
        return std::nullopt;
      }
      result += '.';
      result += current_.text;
      advance();
    }
    return result;
  }

  void parse_header() {
    const auto header_span = current_.span;
    advance();
    const bool array_table = consume(TokenKind::left_bracket);
    const auto name = parse_key();
    if (!name.has_value()) {
      recover_line();
      return;
    }
    if (!expect(TokenKind::right_bracket, "']'")) {
      recover_line();
      return;
    }
    if (array_table && !expect(TokenKind::right_bracket, "a second ']'")) {
      recover_line();
      return;
    }
    if (current_.kind != TokenKind::newline && current_.kind != TokenKind::end) {
      diagnostics_.error(
          std::string(kSyntaxCode), "unexpected text after table header", current_.span);
      recover_line();
      return;
    }
    consume(TokenKind::newline);

    const auto normalized = lowercase(*name);
    if (array_table) {
      if (normalized == "source_set") {
        config_.source_sets.emplace_back();
        source_has_language_.push_back(false);
        source_has_files_.push_back(false);
        context_ = Context::source_set;
        context_index_ = config_.source_sets.size() - 1;
      } else if (normalized == "binding") {
        config_.bindings.emplace_back();
        binding_has_instance_.push_back(false);
        context_ = Context::binding;
        context_index_ = config_.bindings.size() - 1;
      } else if (normalized == "library_map") {
        config_.library_mappings.emplace_back();
        library_map_has_library_.push_back(false);
        library_map_has_path_.push_back(false);
        context_ = Context::library_map;
        context_index_ = config_.library_mappings.size() - 1;
      } else if (normalized == "project.top") {
        config_.project.tops.emplace_back();
        top_has_target_.push_back(false);
        top_has_alias_.push_back(false);
        context_ = Context::top;
        context_index_ = config_.project.tops.size() - 1;
      } else {
        diagnostics_.error(
            std::string(kUnknownCode),
            "unknown array table '[[" + *name + "]]'",
            header_span);
        context_ = Context::unknown;
      }
      return;
    }

    if (normalized == "project") {
      select_single_table(Context::project, "project", header_span);
    } else if (normalized == "build") {
      select_single_table(Context::build, "build", header_span);
    } else if (normalized == "elaboration") {
      select_single_table(Context::elaboration, "elaboration", header_span);
    } else if (normalized == "run") {
      select_single_table(Context::run, "run", header_span);
    } else if (normalized == "systemc") {
      select_single_table(Context::systemc, "systemc", header_span);
    } else {
      diagnostics_.error(
          std::string(kUnknownCode), "unknown table '[" + *name + "]'", header_span);
      context_ = Context::unknown;
    }
  }

  void select_single_table(
      const Context context,
      const std::string_view name,
      const diagnostic::SourceSpan& span) {
    if (!seen_tables_.insert(std::string(name)).second) {
      diagnostics_.error(
          std::string(kDuplicateCode),
          "table '[" + std::string(name) + "]' is declared more than once",
          span);
    }
    context_ = context;
    context_index_ = 0;
  }

  Value parse_value() {
    const auto span = current_.span;
    if (current_.kind == TokenKind::string) {
      Value value{Value::Kind::string, current_.text, false, {}, span};
      advance();
      return value;
    }
    if (current_.kind == TokenKind::integer) {
      Value value{Value::Kind::integer, current_.text, false, {}, span};
      advance();
      return value;
    }
    if (current_.kind == TokenKind::bare &&
        (current_.text == "true" || current_.text == "false")) {
      Value value{Value::Kind::boolean, {}, current_.text == "true", {}, span};
      advance();
      return value;
    }
    if (consume(TokenKind::left_bracket)) {
      Value value{Value::Kind::array, {}, false, {}, span};
      while (consume(TokenKind::newline)) {
      }
      if (consume(TokenKind::right_bracket)) {
        return value;
      }
      for (;;) {
        auto element = parse_value();
        if (element.kind == Value::Kind::invalid) {
          return element;
        }
        value.elements.push_back(std::move(element));
        while (consume(TokenKind::newline)) {
        }
        if (consume(TokenKind::right_bracket)) {
          return value;
        }
        if (!expect(TokenKind::comma, "',' or ']'")) {
          return {Value::Kind::invalid, {}, false, {}, span};
        }
        while (consume(TokenKind::newline)) {
        }
        if (consume(TokenKind::right_bracket)) {
          return value;
        }
      }
    }

    diagnostics_.error(
        std::string(kSyntaxCode),
        "expected a string, integer, boolean, or array value",
        current_.span);
    return {Value::Kind::invalid, {}, false, {}, current_.span};
  }

  void parse_assignment() {
    const auto key_span = current_.span;
    const auto key = parse_key();
    if (!key.has_value() || !expect(TokenKind::equals, "'='")) {
      recover_line();
      return;
    }
    auto value = parse_value();
    if (value.kind == Value::Kind::invalid) {
      recover_line();
      return;
    }
    if (current_.kind != TokenKind::newline && current_.kind != TokenKind::end) {
      diagnostics_.error(
          std::string(kSyntaxCode), "unexpected text after value", current_.span);
      recover_line();
      return;
    }
    consume(TokenKind::newline);

    const std::string normalized_key = lowercase(*key);
    const std::string identity = context_identity() + "." + normalized_key;
    if (!seen_keys_.insert(identity).second) {
      diagnostics_.error(
          std::string(kDuplicateCode),
          "key '" + *key + "' is assigned more than once in this table",
          key_span);
      return;
    }
    assign(normalized_key, std::move(value), key_span);
  }

  [[nodiscard]] std::string context_identity() const {
    switch (context_) {
      case Context::root:
        return "root";
      case Context::project:
        return "project";
      case Context::top:
        return "project.top#" + std::to_string(context_index_);
      case Context::source_set:
        return "source_set#" + std::to_string(context_index_);
      case Context::binding:
        return "binding#" + std::to_string(context_index_);
      case Context::library_map:
        return "library_map#" + std::to_string(context_index_);
      case Context::elaboration:
        return "elaboration";
      case Context::build:
        return "build";
      case Context::run:
        return "run";
      case Context::systemc:
        return "systemc";
      case Context::unknown:
        return "unknown#" + std::to_string(unknown_index_);
    }
    return "unknown";
  }

  bool require_kind(
      const Value& value,
      const Value::Kind kind,
      const std::string_view key,
      const std::string_view expected) {
    if (value.kind == kind) {
      return true;
    }
    diagnostics_.error(
        std::string(kTypeCode),
        "key '" + std::string(key) + "' requires " + std::string(expected),
        value.span);
    return false;
  }

  std::optional<std::vector<std::string>> string_array(
      const Value& value,
      const std::string_view key) {
    if (!require_kind(value, Value::Kind::array, key, "an array of strings")) {
      return std::nullopt;
    }
    std::vector<std::string> result;
    result.reserve(value.elements.size());
    for (const auto& element : value.elements) {
      if (element.kind != Value::Kind::string) {
        diagnostics_.error(
            std::string(kTypeCode),
            "all elements of '" + std::string(key) + "' must be strings",
            element.span);
        return std::nullopt;
      }
      result.push_back(element.text);
    }
    return result;
  }

  std::optional<std::uint64_t> unsigned_integer(
      const Value& value,
      const std::string_view key) {
    if (!require_kind(value, Value::Kind::integer, key, "a non-negative integer")) {
      return std::nullopt;
    }
    std::uint64_t result = 0;
    if (!parse_unsigned(value.text, result)) {
      diagnostics_.error(
          std::string(kValueCode),
          "key '" + std::string(key) + "' is outside the unsigned 64-bit range",
          value.span);
      return std::nullopt;
    }
    return result;
  }

  void unknown_key(
      const std::string_view key,
      const diagnostic::SourceSpan& span) {
    diagnostics_.error(
        std::string(kUnknownCode),
        "unknown key '" + std::string(key) + "' in " + context_identity(),
        span);
  }

  void assign(
      const std::string& key,
      Value value,
      const diagnostic::SourceSpan& key_span) {
    switch (context_) {
      case Context::root:
        assign_root(key, value, key_span);
        break;
      case Context::project:
        assign_project(key, value, key_span);
        break;
      case Context::top:
        assign_top(key, value, key_span);
        break;
      case Context::source_set:
        assign_source_set(key, value, key_span);
        break;
      case Context::binding:
        assign_binding(key, value, key_span);
        break;
      case Context::library_map:
        assign_library_map(key, value, key_span);
        break;
      case Context::elaboration:
        assign_elaboration(key, value, key_span);
        break;
      case Context::build:
        assign_build(key, value, key_span);
        break;
      case Context::run:
        assign_run(key, value, key_span);
        break;
      case Context::systemc:
        assign_systemc(key, value, key_span);
        break;
      case Context::unknown:
        break;
    }
  }

  void assign_root(
      const std::string& key,
      const Value& value,
      const diagnostic::SourceSpan& span) {
    if (key != "schema") {
      unknown_key(key, span);
      return;
    }
    schema_seen_ = true;
    if (const auto schema = unsigned_integer(value, key)) {
      if (*schema > std::numeric_limits<std::uint32_t>::max()) {
          diagnostics_.error(
              std::string(kSchemaCode),
              diagnostic::unsupported_artifact_identity(
                  "project manifest", "schema outside the uint32 range",
                  "schema 2", "fsim.toml"),
              value.span);
      } else {
        config_.schema = static_cast<std::uint32_t>(*schema);
      }
    }
  }

  void assign_project(
      const std::string& key,
      const Value& value,
      const diagnostic::SourceSpan& span) {
    if (key == "name" || key == "top" || key == "time_resolution") {
      if (!require_kind(value, Value::Kind::string, key, "a string")) {
        return;
      }
      if (key == "name") {
        config_.project.name = value.text;
      } else if (key == "top") {
        config_.project.top = value.text;
      } else {
        config_.project.time_resolution = value.text;
      }
      return;
    }
    if (key == "seed") {
      if (value.kind == Value::Kind::string && lowercase(value.text) == "random") {
        config_.project.random_seed = true;
        return;
      }
      if (const auto seed = unsigned_integer(value, key)) {
        config_.project.seed = *seed;
        config_.project.random_seed = false;
      }
      return;
    }
    unknown_key(key, span);
  }

  void assign_top(
      const std::string& key,
      const Value& value,
      const diagnostic::SourceSpan& span) {
    if (key != "target" && key != "alias") {
      unknown_key(key, span);
      return;
    }
    if (!require_kind(value, Value::Kind::string, key, "a string")) {
      return;
    }
    auto& top = config_.project.tops[context_index_];
    if (key == "target") {
      top.target = value.text;
      top_has_target_[context_index_] = true;
    } else {
      top.alias = value.text;
      top_has_alias_[context_index_] = true;
    }
  }

  void assign_source_set(
      const std::string& key,
      const Value& value,
      const diagnostic::SourceSpan& span) {
    auto& source_set = config_.source_sets[context_index_];
    if (key == "language") {
      if (!require_kind(value, Value::Kind::string, key, "a string")) {
        return;
      }
      const auto language = parse_language(value.text);
      if (!language.has_value()) {
        diagnostics_.error(
            std::string(kValueCode),
            "unsupported language '" + value.text +
                "'; expected vhdl, verilog, systemverilog, or systemc",
            value.span);
        return;
      }
      source_set.language = *language;
      source_has_language_[context_index_] = true;
      return;
    }
    if (key == "standard" || key == "library" || key == "compilation_unit"
        || key == "uvm_release") {
      if (!require_kind(value, Value::Kind::string, key, "a string")) {
        return;
      }
      if (key == "uvm_release") {
        const auto release = parse_systemverilog_uvm_release(value.text);
        if (!release.has_value() || *release == SystemVerilogUvmRelease::none) {
          diagnostics_.error(
              std::string(kValueCode),
              "unsupported UVM release '" + value.text
                  + "'; expected 1.2 or 2020.3.1",
              value.span);
          return;
        }
        source_set.uvm_release = *release;
      } else if (key == "standard") {
        source_set.standard = value.text;
      } else if (key == "library") {
        source_set.library = value.text;
      } else {
        source_set.compilation_unit = value.text;
      }
      return;
    }
    if (key == "files" || key == "include_dirs" ||
        key == "include_directories" || key == "defines"
        || key == "compatibility") {
      const auto values = string_array(value, key);
      if (!values.has_value()) {
        return;
      }
      if (key == "files") {
        source_set.file_patterns.reserve(values->size());
        for (const auto& item : *values) {
          source_set.file_patterns.push_back(
              fsim::support::path_from_utf8(item));
        }
        source_has_files_[context_index_] = true;
      } else if (key == "defines") {
        source_set.defines = *values;
      } else if (key == "compatibility") {
        for (const auto& item : *values) {
          const auto canonical = parse_compatibility_switch(item);
          if (!canonical) {
            diagnostics_.error(
                std::string(kValueCode),
                "unsupported compatibility switch '" + item + "'",
                value.span);
            continue;
          }
          if (std::ranges::find(
                  source_set.compatibility_switches, *canonical)
              != source_set.compatibility_switches.end()) {
            diagnostics_.error(
                std::string(kDuplicateCode),
                "duplicate compatibility switch '"
                    + std::string { *canonical } + "'",
                value.span);
            continue;
          }
          source_set.compatibility_switches.emplace_back(*canonical);
        }
      } else {
        source_set.include_directories.reserve(values->size());
        for (const auto& item : *values) {
          source_set.include_directories.push_back(
              fsim::support::path_from_utf8(item));
        }
      }
      return;
    }
    unknown_key(key, span);
  }

  void assign_binding(
      const std::string& key,
      const Value& value,
      const diagnostic::SourceSpan& span) {
    if (key != "instance" && key != "target" && key != "resolver") {
      unknown_key(key, span);
      return;
    }
    if (!require_kind(value, Value::Kind::string, key, "a string")) {
      return;
    }
    auto& binding = config_.bindings[context_index_];
    if (key == "instance") {
      binding.instance = value.text;
      binding_has_instance_[context_index_] = true;
    } else if (key == "target") {
      binding.target = value.text;
    } else {
      binding.resolver = value.text;
    }
  }

  void assign_library_map(
      const std::string& key,
      const Value& value,
      const diagnostic::SourceSpan& span) {
    if (key != "library" && key != "path") {
      unknown_key(key, span);
      return;
    }
    if (!require_kind(value, Value::Kind::string, key, "a string")) {
      return;
    }
    auto& mapping = config_.library_mappings[context_index_];
    if (key == "library") {
      mapping.library = value.text;
      library_map_has_library_[context_index_] = true;
    } else {
      mapping.path = fsim::support::path_from_utf8(value.text);
      library_map_has_path_[context_index_] = true;
    }
  }

  void assign_build(
      const std::string& key,
      const Value& value,
      const diagnostic::SourceSpan& span) {
    if (key == "optimization") {
      if (!require_kind(value, Value::Kind::string, key, "a string")) {
        return;
      }
      const auto spelling = lowercase(value.text);
      if (spelling == "o0" || spelling == "0") {
        config_.build.optimization = Optimization::o0;
      } else if (spelling == "o1" || spelling == "1") {
        config_.build.optimization = Optimization::o1;
      } else if (spelling == "o2" || spelling == "2") {
        config_.build.optimization = Optimization::o2;
      } else if (spelling == "o3" || spelling == "3") {
        config_.build.optimization = Optimization::o3;
      } else {
        diagnostics_.error(
            std::string(kValueCode),
            "optimization must be O0, O1, O2, or O3",
            value.span);
      }
      return;
    }
    if (key == "jobs") {
      if (const auto jobs = unsigned_integer(value, key)) {
        if (*jobs > std::numeric_limits<std::uint32_t>::max()) {
          diagnostics_.error(
              std::string(kValueCode), "jobs exceeds the 32-bit limit", value.span);
        } else {
          config_.build.jobs = static_cast<std::uint32_t>(*jobs);
        }
      }
      return;
    }
    if (key == "cache" || key == "cache_path") {
      if (require_kind(value, Value::Kind::string, key, "a string")) {
        config_.build.cache_path =
            fsim::support::path_from_utf8(value.text);
      }
      return;
    }
    unknown_key(key, span);
  }

  void assign_elaboration(
      const std::string& key,
      const Value& value,
      const diagnostic::SourceSpan& span) {
    if (key != "search_libraries") {
      unknown_key(key, span);
      return;
    }
    if (const auto libraries = string_array(value, key)) {
      config_.elaboration.search_libraries = *libraries;
    }
  }

  void assign_run(
      const std::string& key,
      const Value& value,
      const diagnostic::SourceSpan& span) {
    if (key == "duration") {
      if (require_kind(value, Value::Kind::string, key, "a string")) {
        config_.run.duration = value.text;
      }
      return;
    }
    if (key == "max_deltas") {
      if (const auto max_deltas = unsigned_integer(value, key)) {
        config_.run.max_deltas = *max_deltas;
      }
      return;
    }
    if (key == "delay_mode") {
      if (require_kind(value, Value::Kind::string, key, "a string")) {
        const auto mode = parse_delay_mode(value.text);
        if (mode) {
          config_.run.delay_mode = *mode;
        } else {
          diagnostics_.error(
              std::string(kValueCode),
              "[run].delay_mode must be min, typ, or max",
              value.span);
        }
      }
      return;
    }
    if (key == "trace_file") {
      if (require_kind(value, Value::Kind::string, key, "a string")) {
        config_.run.trace_file =
            fsim::support::path_from_utf8(value.text);
      }
      return;
    }
    if (key == "trace_format") {
      if (require_kind(value, Value::Kind::string, key, "a string")) {
        const auto format = parse_trace_format(value.text);
        if (format) {
          config_.run.trace_format = *format;
        } else {
          diagnostics_.error(
              std::string(kValueCode),
              "[run].trace_format must be auto, vcd, or fst",
              value.span);
        }
      }
      return;
    }
    if (key == "trace_compression") {
      if (require_kind(value, Value::Kind::string, key, "a string")) {
        const auto compression = parse_trace_compression(value.text);
        if (compression) {
          config_.run.trace_compression = *compression;
        } else {
          diagnostics_.error(
              std::string(kValueCode),
              "[run].trace_compression must be auto, none, or deterministic",
              value.span);
        }
      }
      return;
    }
    if (key == "trace_filters") {
      if (const auto filters = string_array(value, key)) {
        config_.run.trace_filters = *filters;
      }
      return;
    }
    if (key == "trace_report_limit") {
      if (const auto limit = unsigned_integer(value, key)) {
        if (*limit == 0U) {
          diagnostics_.error(
              std::string(kValueCode),
              "[run].trace_report_limit must be positive",
              value.span);
        } else if (*limit > std::numeric_limits<std::size_t>::max()) {
          diagnostics_.error(
              std::string(kValueCode),
              "[run].trace_report_limit is too large",
              value.span);
        } else {
          config_.run.trace_report_limit = static_cast<std::size_t>(*limit);
        }
      }
      return;
    }
    if (key == "trace_enabled") {
      if (require_kind(value, Value::Kind::boolean, key, "a boolean")) {
        config_.run.trace_enabled = value.boolean;
      }
      return;
    }
    unknown_key(key, span);
  }

  void assign_systemc(
      const std::string& key,
      const Value& value,
      const diagnostic::SourceSpan& span) {
    if (key == "compiler") {
      if (require_kind(value, Value::Kind::string, key, "a string")) {
        config_.systemc.compiler = value.text;
      }
      return;
    }
    if (key == "include_dirs" || key == "include_directories" ||
        key == "includes" || key == "defines" || key == "compile_options" ||
        key == "link_options" || key == "libraries") {
      const auto values = string_array(value, key);
      if (!values.has_value()) {
        return;
      }
      if (key == "include_dirs" || key == "include_directories" || key == "includes") {
        config_.systemc.include_directories.reserve(values->size());
        for (const auto& item : *values) {
          config_.systemc.include_directories.push_back(
              fsim::support::path_from_utf8(item));
        }
      } else if (key == "defines") {
        config_.systemc.defines = *values;
      } else if (key == "compile_options") {
        config_.systemc.compile_options = *values;
      } else if (key == "link_options") {
        config_.systemc.link_options = *values;
      } else {
        config_.systemc.libraries = *values;
      }
      return;
    }
    unknown_key(key, span);
  }

  void validate() {
    const diagnostic::SourceSpan document_span{
        source_name_, {1, 1, 0}, {1, 1, 0}};
    if (!schema_seen_) {
        diagnostics_.error(
            std::string(kSchemaCode),
            diagnostic::unsupported_artifact_identity(
                "project manifest", "no schema", "schema 2", "fsim.toml"),
            document_span);
    } else if (config_.schema != kSchemaVersion) {
        diagnostics_.error(
            std::string(kSchemaCode),
            diagnostic::unsupported_artifact_identity(
                "project manifest", "schema " + std::to_string(config_.schema),
                "schema " + std::to_string(kSchemaVersion), "fsim.toml"),
            document_span);
    }
    if (!config_.project.top.empty() && !config_.project.tops.empty()) {
      diagnostics_.error(
          std::string(kDuplicateCode),
          "[project].top and [[project.top]] cannot be used together",
          document_span);
    }
    if (config_.project.top.empty() && config_.project.tops.empty()) {
      diagnostics_.error(
          std::string(kRequiredCode),
          "[project].top or [[project.top]] must name at least one design top",
          document_span);
    }
    if (!config_.project.top.empty() && config_.project.tops.empty()) {
      config_.project.tops.push_back(
          {config_.project.top, default_top_alias(config_.project.top)});
    } else if (config_.project.top.empty()
               && !config_.project.tops.empty()) {
      std::unordered_set<std::string> aliases;
      for (std::size_t index = 0; index < config_.project.tops.size(); ++index) {
        auto& top = config_.project.tops[index];
        if (!top_has_target_[index] || top.target.empty()) {
          diagnostics_.error(
              std::string(kRequiredCode),
              "[[project.top]] #" + std::to_string(index + 1)
                  + " is missing a non-empty 'target'",
              document_span);
        }
        if (!top_has_alias_[index] || top.alias.empty()) {
          if (config_.project.tops.size() == 1 && !top.target.empty()) {
            top.alias = default_top_alias(top.target);
          } else {
            diagnostics_.error(
                std::string(kRequiredCode),
                "[[project.top]] #" + std::to_string(index + 1)
                    + " is missing a non-empty 'alias'",
                document_span);
          }
        }
        if (!top.alias.empty() && !valid_top_alias(top.alias)) {
          diagnostics_.error(
              std::string(kValueCode),
              "top alias '" + top.alias
                  + "' must start with a letter or underscore and contain "
                    "only letters, digits, and underscores",
              document_span);
        } else if (!top.alias.empty() && !aliases.insert(top.alias).second) {
          diagnostics_.error(
              std::string(kDuplicateCode),
              "duplicate top alias '" + top.alias + "'",
              document_span);
        }
      }
      if (config_.project.tops.size() == 1) {
        config_.project.top = config_.project.tops.front().target;
      }
    }
    if (config_.project.name.empty()) {
      config_.project.name = fsim::support::path_to_utf8(
          fsim::support::path_from_utf8(source_name_).stem());
      if (config_.project.name.empty()) {
        config_.project.name = "fsim-project";
      }
    }
    if (!valid_time_value(config_.project.time_resolution, true)) {
      diagnostics_.error(
          std::string(kValueCode),
          "[project].time_resolution must be 'auto' or an integer followed by fs, ps, ns, us, ms, or s",
          document_span);
    }
    if (config_.run.duration.has_value() &&
        !valid_time_value(*config_.run.duration, false)) {
      diagnostics_.error(
          std::string(kValueCode),
          "[run].duration must be an integer followed by fs, ps, ns, us, ms, or s",
          document_span);
    }
    if (config_.run.max_deltas == 0) {
      diagnostics_.error(
          std::string(kValueCode), "[run].max_deltas must be greater than zero", document_span);
    }
    if (config_.source_sets.empty() && config_.library_mappings.empty()) {
      diagnostics_.error(
          std::string(kRequiredCode),
          "the project must declare at least one [[source_set]] or [[library_map]]",
          document_span);
    }

    for (const auto& library : config_.elaboration.search_libraries) {
      if (library.empty()) {
        diagnostics_.error(
            std::string(kValueCode),
            "[elaboration].search_libraries contains an empty library name",
            document_span);
      }
    }

    std::unordered_set<std::string> built_libraries;
    for (const auto& source_set : config_.source_sets) {
      built_libraries.insert(lowercase(source_set.library));
    }
    std::unordered_set<std::string> mapped_libraries;
    for (std::size_t index = 0; index < config_.library_mappings.size(); ++index) {
      const auto& mapping = config_.library_mappings[index];
      const auto number = std::to_string(index + 1);
      if (!library_map_has_library_[index] || mapping.library.empty()) {
        diagnostics_.error(
            std::string(kRequiredCode),
            "[[library_map]] #" + number
                + " is missing a non-empty 'library'",
            document_span);
      } else if (!valid_library_name(mapping.library)) {
        diagnostics_.error(
            std::string(kValueCode),
            "mapped logical library '" + mapping.library
                + "' must start with a letter or underscore and contain only "
                  "letters, digits, and underscores",
            document_span);
      } else {
        const auto normalized = lowercase(mapping.library);
        if (normalized == "std" || normalized == "ieee"
            || normalized == "fsim") {
          diagnostics_.error(
              std::string(kValueCode),
              "mapped logical library '" + mapping.library
                  + "' is reserved by fsim",
              document_span);
        } else if (!mapped_libraries.insert(normalized).second) {
          diagnostics_.error(
              std::string(kDuplicateCode),
              "duplicate mapped logical library '" + mapping.library + "'",
              document_span);
        } else if (built_libraries.contains(normalized)) {
          diagnostics_.error(
              std::string(kDuplicateCode),
              "mapped logical library '" + mapping.library
                  + "' collides with a project-built source library",
              document_span);
        }
      }
      if (!library_map_has_path_[index] || mapping.path.empty()) {
        diagnostics_.error(
            std::string(kRequiredCode),
            "[[library_map]] #" + number + " is missing a non-empty 'path'",
            document_span);
      }
    }

    for (std::size_t index = 0; index < config_.source_sets.size(); ++index) {
      auto& source_set = config_.source_sets[index];
      if (!source_has_language_[index]) {
        diagnostics_.error(
            std::string(kRequiredCode),
            "[[source_set]] #" + std::to_string(index + 1) + " is missing 'language'",
            document_span);
      }
      if (!source_has_files_[index] || source_set.file_patterns.empty()) {
        diagnostics_.error(
            std::string(kRequiredCode),
            "[[source_set]] #" + std::to_string(index + 1) +
                " must contain a non-empty 'files' array",
            document_span);
      }
      if (source_set.standard.empty()) {
        source_set.standard = default_standard(source_set.language);
      }
      validate_standard(source_set, index, document_span);
      if (!source_set.compatibility_switches.empty()
          && source_set.language != Language::verilog
          && source_set.language != Language::system_verilog) {
        diagnostics_.error(
            std::string(kValueCode),
            "[[source_set]] #" + std::to_string(index + 1)
                + " compatibility switches are valid only for Verilog/SystemVerilog",
            document_span);
      }
      if (source_set.uvm_release != SystemVerilogUvmRelease::none
          && source_set.language != Language::system_verilog) {
        diagnostics_.error(
            std::string(kValueCode),
            "[[source_set]] #" + std::to_string(index + 1)
                + " uvm_release is valid only for SystemVerilog",
            document_span);
      }
      if (source_set.library.empty()) {
        diagnostics_.error(
            std::string(kValueCode),
            "[[source_set]] #" + std::to_string(index + 1) +
                " has an empty library name",
            document_span);
      }
      if (source_set.compilation_unit != "file" &&
          source_set.compilation_unit != "source-set" &&
          source_set.compilation_unit != "combined") {
        diagnostics_.error(
            std::string(kValueCode),
            "[[source_set]] #" + std::to_string(index + 1) +
                " compilation_unit must be 'file', 'source-set', or 'combined'",
            document_span);
      }
    }

    for (std::size_t index = 0; index < config_.bindings.size(); ++index) {
      const auto& binding = config_.bindings[index];
      if (!binding_has_instance_[index] || binding.instance.empty()) {
        diagnostics_.error(
            std::string(kRequiredCode),
            "[[binding]] #" + std::to_string(index + 1) + " is missing 'instance'",
            document_span);
      }
      if (binding.target.has_value() && binding.target->empty()) {
        diagnostics_.error(
            std::string(kValueCode),
            "[[binding]] #" + std::to_string(index + 1) + " has an empty 'target'",
            document_span);
      } else if (binding.target.has_value()
                 && !has_binding_prefix(*binding.target)) {
        diagnostics_.error(
            std::string(kValueCode),
            "binding target '" + *binding.target +
                "' must start with vhdl:, sv:, or systemc:",
            document_span);
      }
      if (binding.resolver.has_value() && *binding.resolver != "std_logic" &&
          *binding.resolver != "sv_wire") {
        diagnostics_.error(
            std::string(kValueCode),
            "binding resolver must be 'std_logic' or 'sv_wire'",
            document_span);
      }
      if (!binding.target.has_value() && !binding.resolver.has_value()) {
        diagnostics_.error(
            std::string(kRequiredCode),
            "[[binding]] #" + std::to_string(index + 1)
                + " must contain 'target', 'resolver', or both",
            document_span);
      }
    }
  }

  static bool valid_time_value(const std::string_view value, const bool allow_auto) {
    if (allow_auto && lowercase(value) == "auto") {
      return true;
    }
    std::size_t index = 0;
    bool nonzero = false;
    while (index < value.size() &&
           std::isdigit(static_cast<unsigned char>(value[index])) != 0) {
      nonzero = nonzero || value[index] != '0';
      ++index;
    }
    if (index == 0 || index == value.size() || (allow_auto && !nonzero)) {
      return false;
    }
    const auto unit = value.substr(index);
    return unit == "fs" || unit == "ps" || unit == "ns" || unit == "us" ||
           unit == "ms" || unit == "s";
  }

  static bool has_binding_prefix(const std::string_view target) {
    return target.starts_with("vhdl:") || target.starts_with("sv:") ||
           target.starts_with("systemc:");
  }

  static std::string default_top_alias(const std::string_view target) {
    auto spelling = target;
    if (const auto colon = spelling.find(':');
        colon != std::string_view::npos) {
      spelling.remove_prefix(colon + 1);
    }
    if (const auto dot = spelling.rfind('.');
        dot != std::string_view::npos) {
      spelling.remove_prefix(dot + 1);
    }
    if (const auto architecture = spelling.find('(');
        architecture != std::string_view::npos) {
      spelling = spelling.substr(0, architecture);
    }
    return std::string{spelling};
  }

  static bool valid_top_alias(const std::string_view alias_name) {
    if (alias_name.empty()
        || (std::isalpha(static_cast<unsigned char>(alias_name.front())) == 0
            && alias_name.front() != '_')) {
      return false;
    }
    return std::ranges::all_of(
        alias_name,
        [](const unsigned char character) {
            return std::isalnum(character) != 0 || character == '_';
        });
  }

  void validate_standard(
      SourceSet& source_set,
      const std::size_t index,
      const diagnostic::SourceSpan& span)
  {
      const auto canonical = canonical_standard(
          source_set.language, source_set.standard);
      if (!canonical) {
          diagnostics_.error(
              std::string(kValueCode),
              "unsupported standard '" + source_set.standard + "' for " + std::string(to_string(source_set.language)) + " source set #" + std::to_string(index + 1),
              span);
          return;
      }
      source_set.standard = *canonical;
  }

  Lexer lexer_;
  std::string source_name_;
  std::filesystem::path base_directory_;
  diagnostic::Engine& diagnostics_;
  Token current_;
  Config config_;
  Context context_ { Context::root };
  std::size_t context_index_ { 0 };
  std::size_t unknown_index_ { 0 };
  bool schema_seen_ { false };
  std::unordered_set<std::string> seen_tables_;
  std::unordered_set<std::string> seen_keys_;
  std::vector<bool> source_has_language_;
  std::vector<bool> source_has_files_;
  std::vector<bool> binding_has_instance_;
  std::vector<bool> library_map_has_library_;
  std::vector<bool> library_map_has_path_;
  std::vector<bool> top_has_target_;
  std::vector<bool> top_has_alias_;
};

std::filesystem::path absolute_normalized(
    const std::filesystem::path& path,
    const std::filesystem::path& base) {
  if (path.is_absolute()) {
    return path.lexically_normal();
  }
  std::error_code error;
  auto absolute_base = base;
  if (!absolute_base.is_absolute()) {
    absolute_base = std::filesystem::absolute(absolute_base, error);
    if (error) {
      absolute_base = base;
    }
  }
  return (absolute_base / path).lexically_normal();
}

bool has_glob(const std::string_view pattern) {
  return pattern.find('*') != std::string_view::npos ||
         pattern.find('?') != std::string_view::npos;
}

bool glob_match_impl(
    const std::string_view pattern,
    const std::string_view path,
    const std::size_t pattern_index,
    const std::size_t path_index,
    std::vector<std::int8_t>& memo) {
  const std::size_t width = path.size() + 1;
  auto& cached = memo[pattern_index * width + path_index];
  if (cached != -1) {
    return cached != 0;
  }

  bool matches = false;
  if (pattern_index == pattern.size()) {
    matches = path_index == path.size();
  } else if (pattern[pattern_index] == '*') {
    const bool recursive =
        pattern_index + 1 < pattern.size() && pattern[pattern_index + 1] == '*';
    const std::size_t next_pattern = pattern_index + (recursive ? 2 : 1);
    if (recursive && next_pattern < pattern.size() &&
        pattern[next_pattern] == '/') {
      matches =
          glob_match_impl(pattern, path, next_pattern + 1, path_index, memo);
    }
    if (!matches) {
      matches = glob_match_impl(pattern, path, next_pattern, path_index, memo);
    }
    if (!matches && path_index < path.size() &&
        (recursive || path[path_index] != '/')) {
      matches = glob_match_impl(pattern, path, pattern_index, path_index + 1, memo);
    }
  } else if (
      pattern[pattern_index] == '?' && path_index < path.size() &&
      path[path_index] != '/') {
    matches =
        glob_match_impl(pattern, path, pattern_index + 1, path_index + 1, memo);
  } else if (
      path_index < path.size() && pattern[pattern_index] == path[path_index]) {
    matches =
        glob_match_impl(pattern, path, pattern_index + 1, path_index + 1, memo);
  }
  cached = static_cast<std::int8_t>(matches ? 1 : 0);
  return matches;
}

bool glob_match(const std::string_view pattern, const std::string_view path) {
  std::vector<std::int8_t> memo((pattern.size() + 1) * (path.size() + 1), -1);
  return glob_match_impl(pattern, path, 0, 0, memo);
}

std::filesystem::path glob_root(const std::filesystem::path& pattern) {
  std::filesystem::path root;
  for (const auto& component : pattern) {
    const auto spelling = fsim::support::path_to_utf8(component);
    if (has_glob(spelling)) {
      break;
    }
    root /= component;
  }
  if (root.empty()) {
    root = ".";
  }
  return root;
}

std::vector<std::filesystem::path> expand_pattern(
    const std::filesystem::path& pattern,
    diagnostic::Engine& diagnostics,
    const std::string& source_name) {
  std::vector<std::filesystem::path> result;
  const auto pattern_text = fsim::support::path_to_utf8(pattern);
  if (!has_glob(pattern_text)) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(pattern, error)) {
      diagnostics.error(
          std::string(kSourceCode),
          "source file does not exist: "
              + fsim::support::path_to_utf8(pattern),
          {source_name, {1, 1, 0}, {1, 1, 0}});
      return result;
    }
    result.push_back(pattern);
    return result;
  }

  const auto root = glob_root(pattern);
  std::error_code error;
  if (!std::filesystem::is_directory(root, error)) {
    diagnostics.error(
        std::string(kSourceCode),
        "source glob root does not exist: "
            + fsim::support::path_to_utf8(root),
        {source_name, {1, 1, 0}, {1, 1, 0}});
    return result;
  }

  std::filesystem::recursive_directory_iterator iterator(
      root, std::filesystem::directory_options::skip_permission_denied, error);
  const std::filesystem::recursive_directory_iterator end;
  while (!error && iterator != end) {
    if (iterator->is_regular_file(error) &&
        glob_match(
            pattern_text,
            fsim::support::path_to_utf8(
                iterator->path().lexically_normal()))) {
      result.push_back(iterator->path().lexically_normal());
    }
    iterator.increment(error);
  }
  if (error) {
    diagnostics.error(
        std::string(kIoCode),
        "failed while expanding source glob '" + pattern_text + "': " + error.message(),
        {source_name, {1, 1, 0}, {1, 1, 0}});
    return {};
  }
  std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
    return fsim::support::path_to_utf8(left)
        < fsim::support::path_to_utf8(right);
  });
  if (result.empty()) {
    diagnostics.error(
        std::string(kSourceCode),
        "source glob matched no files: " + pattern_text,
        {source_name, {1, 1, 0}, {1, 1, 0}});
  }
  return result;
}

void resolve_paths(Config& config, diagnostic::Engine& diagnostics) {
  const auto base = config.base_directory;
  for (auto& source_set : config.source_sets) {
    for (auto& directory : source_set.include_directories) {
      directory = absolute_normalized(directory, base);
    }
    for (auto& pattern : source_set.file_patterns) {
      pattern = absolute_normalized(pattern, base);
      auto expanded =
          expand_pattern(
              pattern,
              diagnostics,
              fsim::support::path_to_utf8(config.manifest_path));
      source_set.files.insert(
          source_set.files.end(),
          std::make_move_iterator(expanded.begin()),
          std::make_move_iterator(expanded.end()));
    }
  }
  for (auto& mapping : config.library_mappings) {
    mapping.path = absolute_normalized(mapping.path, base);
  }
  config.build.cache_path = absolute_normalized(config.build.cache_path, base);
  if (config.run.trace_file.has_value()) {
    config.run.trace_file = absolute_normalized(*config.run.trace_file, base);
  }
  for (auto& directory : config.systemc.include_directories) {
    directory = absolute_normalized(directory, base);
  }
}

}  // namespace

std::string_view to_string(const Language language) noexcept {
  switch (language) {
    case Language::vhdl:
      return "vhdl";
    case Language::verilog:
        return "verilog";
    case Language::system_verilog:
        return "systemverilog";
    case Language::systemc:
        return "systemc";
    }
    return "systemverilog";
}

std::string_view to_string(const VhdlStandard standard) noexcept
{
    switch (standard) {
    case VhdlStandard::vhdl_1987:
        return "1987";
    case VhdlStandard::vhdl_1993:
        return "1993";
    case VhdlStandard::vhdl_2000:
        return "2000";
    case VhdlStandard::vhdl_2002:
        return "2002";
    case VhdlStandard::vhdl_2008:
        return "2008";
    }
    return "2008";
}

std::string_view to_string(const VerilogStandard standard) noexcept
{
    switch (standard) {
    case VerilogStandard::verilog_1995:
        return "1995";
    case VerilogStandard::verilog_2001:
        return "2001";
    case VerilogStandard::verilog_2001_noconfig:
        return "2001-noconfig";
    case VerilogStandard::verilog_2005:
        return "2005";
    }
    return "2005";
}

std::string_view to_string(const SystemVerilogStandard standard) noexcept
{
    switch (standard) {
    case SystemVerilogStandard::systemverilog_2005:
        return "2005";
    case SystemVerilogStandard::systemverilog_2009:
        return "2009";
    case SystemVerilogStandard::systemverilog_2012:
        return "2012";
    case SystemVerilogStandard::systemverilog_2017:
        return "2017";
    }
    return "2017";
}

std::string_view to_string(const Optimization optimization) noexcept
{
    switch (optimization) {
    case Optimization::o0:
        return "O0";
    case Optimization::o1:
        return "O1";
    case Optimization::o2:
        return "O2";
    case Optimization::o3:
        return "O3";
    }
    return "O2";
}

std::string_view to_string(const DelayMode mode) noexcept {
  switch (mode) {
    case DelayMode::minimum:
      return "min";
    case DelayMode::typical:
      return "typ";
    case DelayMode::maximum:
      return "max";
  }
  return "typ";
}

std::string_view to_string(const TraceFormat format) noexcept {
  switch (format) {
    case TraceFormat::automatic:
      return "auto";
    case TraceFormat::vcd:
      return "vcd";
    case TraceFormat::fst:
      return "fst";
  }
  return "auto";
}

std::string_view to_string(const TraceCompression compression) noexcept {
  switch (compression) {
    case TraceCompression::automatic:
      return "auto";
    case TraceCompression::none:
      return "none";
    case TraceCompression::deterministic:
      return "deterministic";
  }
  return "auto";
}

std::string_view to_string(const SystemVerilogUvmRelease release) noexcept {
  switch (release) {
    case SystemVerilogUvmRelease::none:
      return "none";
    case SystemVerilogUvmRelease::uvm_1_2:
      return "1.2";
    case SystemVerilogUvmRelease::ieee_1800_2_2020_3_1:
      return "2020.3.1";
  }
  return "none";
}

std::optional<Language> parse_language(const std::string_view spelling) noexcept
{
    const auto normalized = lowercase(spelling);
    if (normalized == "vhdl" || normalized == "vhdl-87"
        || normalized == "vhdl-1987" || normalized == "vhdl-93"
        || normalized == "vhdl-1993" || normalized == "vhdl-00"
        || normalized == "vhdl-2000" || normalized == "vhdl-02"
        || normalized == "vhdl-2002" || normalized == "vhdl-08"
        || normalized == "vhdl-2008") {
        return Language::vhdl;
    }
    if (normalized == "verilog" || normalized == "v"
        || normalized == "verilog-95" || normalized == "verilog-1995"
        || normalized == "verilog-01" || normalized == "verilog-2001"
        || normalized == "verilog-2001-noconfig"
        || normalized == "verilog-05" || normalized == "verilog-2005") {
        return Language::verilog;
    }
    if (normalized == "systemverilog" || normalized == "system-verilog"
        || normalized == "sv" || normalized == "sv-05"
        || normalized == "sv-2005" || normalized == "systemverilog-2005"
        || normalized == "sv-09" || normalized == "sv-2009"
        || normalized == "systemverilog-2009" || normalized == "sv-12"
        || normalized == "sv-2012" || normalized == "systemverilog-2012"
        || normalized == "sv-17" || normalized == "sv-2017"
        || normalized == "systemverilog-2017") {
        return Language::system_verilog;
    }
    if (normalized == "systemc" || normalized == "sc") {
        return Language::systemc;
    }
    return std::nullopt;
}

std::optional<VhdlStandard> parse_vhdl_standard(
    const std::string_view spelling) noexcept
{
    const auto normalized = lowercase(spelling);
    if (normalized == "87" || normalized == "1987"
        || normalized == "vhdl-87" || normalized == "vhdl-1987") {
        return VhdlStandard::vhdl_1987;
    }
    if (normalized == "93" || normalized == "1993"
        || normalized == "vhdl-93" || normalized == "vhdl-1993") {
        return VhdlStandard::vhdl_1993;
    }
    if (normalized == "00" || normalized == "2000"
        || normalized == "vhdl-00" || normalized == "vhdl-2000") {
        return VhdlStandard::vhdl_2000;
    }
    if (normalized == "02" || normalized == "2002"
        || normalized == "vhdl-02" || normalized == "vhdl-2002") {
        return VhdlStandard::vhdl_2002;
    }
    if (normalized == "08" || normalized == "2008"
        || normalized == "vhdl-08" || normalized == "vhdl-2008") {
        return VhdlStandard::vhdl_2008;
    }
    return std::nullopt;
}

std::optional<VerilogStandard> parse_verilog_standard(
    const std::string_view spelling) noexcept
{
    const auto normalized = lowercase(spelling);
    if (normalized == "95" || normalized == "1995"
        || normalized == "v95" || normalized == "verilog-95"
        || normalized == "verilog-1995") {
        return VerilogStandard::verilog_1995;
    }
    if (normalized == "01" || normalized == "2001"
        || normalized == "v2001" || normalized == "verilog-01"
        || normalized == "verilog-2001") {
        return VerilogStandard::verilog_2001;
    }
    if (normalized == "2001-noconfig"
        || normalized == "v2001-noconfig"
        || normalized == "verilog-2001-noconfig") {
        return VerilogStandard::verilog_2001_noconfig;
    }
    if (normalized == "05" || normalized == "2005"
        || normalized == "v2005" || normalized == "verilog-05"
        || normalized == "verilog-2005") {
        return VerilogStandard::verilog_2005;
    }
    return std::nullopt;
}

std::optional<SystemVerilogStandard> parse_systemverilog_standard(
    const std::string_view spelling) noexcept
{
    const auto normalized = lowercase(spelling);
    if (normalized == "05" || normalized == "2005"
        || normalized == "sv-05" || normalized == "sv-2005"
        || normalized == "systemverilog-2005") {
        return SystemVerilogStandard::systemverilog_2005;
    }
    if (normalized == "09" || normalized == "2009"
        || normalized == "sv-09" || normalized == "sv-2009"
        || normalized == "systemverilog-2009") {
        return SystemVerilogStandard::systemverilog_2009;
    }
    if (normalized == "12" || normalized == "2012"
        || normalized == "sv-12" || normalized == "sv-2012"
        || normalized == "systemverilog-2012") {
        return SystemVerilogStandard::systemverilog_2012;
    }
    if (normalized == "17" || normalized == "2017"
        || normalized == "sv-17" || normalized == "sv-2017"
        || normalized == "systemverilog-2017") {
        return SystemVerilogStandard::systemverilog_2017;
    }
    return std::nullopt;
}

std::string_view default_standard(const Language language) noexcept
{
    switch (language) {
    case Language::vhdl:
        return "2008";
    case Language::verilog:
        return "2005";
    case Language::system_verilog:
        return "2017";
    case Language::systemc:
        return "2023-subset";
    }
    return "2017";
}

std::optional<std::string_view> canonical_standard(
    const Language language,
    const std::string_view spelling) noexcept
{
    const auto normalized = lowercase(spelling);
    switch (language) {
    case Language::vhdl:
        if (const auto standard = parse_vhdl_standard(normalized)) {
            return to_string(*standard);
        }
        break;
    case Language::verilog:
        if (const auto standard = parse_verilog_standard(normalized)) {
            return to_string(*standard);
        }
        break;
    case Language::system_verilog:
        if (const auto standard = parse_systemverilog_standard(normalized)) {
            return to_string(*standard);
        }
        break;
    case Language::systemc:
        if (normalized == "2023-subset" || normalized == "2023") {
            return "2023-subset";
        }
        break;
    }
    return std::nullopt;
}

std::optional<std::string_view> parse_compatibility_switch(
    const std::string_view spelling) noexcept
{
    auto normalized = lowercase(spelling);
    std::ranges::replace(normalized, '_', '-');
    constexpr std::array switches {
        std::string_view { "keyword-profile" },
        std::string_view { "implicit-net" },
        std::string_view { "port-connection" },
        std::string_view { "sizing" },
        std::string_view { "lifetime" },
        std::string_view { "scheduler-assertion" },
        std::string_view { "configuration" },
    };
    const auto found = std::ranges::find(switches, normalized);
    return found == switches.end()
        ? std::nullopt
        : std::optional<std::string_view> { *found };
}

std::string compatibility_profile(
    const std::vector<std::string>& switches)
{
    if (switches.empty()) {
        return "none";
    }
    std::vector<std::string_view> canonical;
    canonical.reserve(switches.size());
    for (const auto& spelling : switches) {
        if (const auto parsed = parse_compatibility_switch(spelling)) {
            canonical.push_back(*parsed);
        }
    }
    constexpr std::array order {
        std::string_view { "keyword-profile" },
        std::string_view { "implicit-net" },
        std::string_view { "port-connection" },
        std::string_view { "sizing" },
        std::string_view { "lifetime" },
        std::string_view { "scheduler-assertion" },
        std::string_view { "configuration" },
    };
    std::ranges::sort(canonical, [&](const auto left, const auto right) {
        return std::ranges::find(order, left)
            < std::ranges::find(order, right);
    });
    canonical.erase(std::unique(canonical.begin(), canonical.end()), canonical.end());
    std::string result;
    for (const auto item : canonical) {
        if (!result.empty()) {
            result.push_back(',');
        }
        result.append(item);
    }
    return result.empty() ? "none" : result;
}

std::optional<SystemVerilogUvmRelease> parse_systemverilog_uvm_release(
    const std::string_view spelling) noexcept
{
    const auto normalized = lowercase(spelling);
    if (normalized.empty() || normalized == "none") {
        return SystemVerilogUvmRelease::none;
    }
    if (normalized == "1.2" || normalized == "uvm-1.2"
        || normalized == "uvm_1_2") {
        return SystemVerilogUvmRelease::uvm_1_2;
    }
    if (normalized == "2020.3.1" || normalized == "uvm-2020.3.1"
        || normalized == "uvm_2020_3_1"
        || normalized == "ieee-1800.2-2020-3.1") {
        return SystemVerilogUvmRelease::ieee_1800_2_2020_3_1;
    }
    return std::nullopt;
}

SystemVerilogUvmCompatibility systemverilog_uvm_compatibility(
    const SystemVerilogUvmRelease release) noexcept
{
    switch (release) {
    case SystemVerilogUvmRelease::uvm_1_2:
        return {
            release, 1, 2,
            true, true, true, true,
            true, true,
            false, false, false
        };
    case SystemVerilogUvmRelease::ieee_1800_2_2020_3_1:
      return {
          release, 2020, 3,
          false, false, false, false,
          true, true,
          true, true, true};
    case SystemVerilogUvmRelease::none:
      return {};
    }
  return {};
}

std::optional<DelayMode> parse_delay_mode(
    const std::string_view spelling) noexcept {
  const auto normalized = lowercase(spelling);
  if (normalized == "min" || normalized == "minimum") {
    return DelayMode::minimum;
  }
  if (normalized == "typ" || normalized == "typical") {
    return DelayMode::typical;
  }
  if (normalized == "max" || normalized == "maximum") {
    return DelayMode::maximum;
  }
  return std::nullopt;
}

std::optional<TraceFormat> parse_trace_format(
    const std::string_view spelling) noexcept {
  const auto normalized = lowercase(spelling);
  if (normalized == "auto" || normalized == "automatic") {
    return TraceFormat::automatic;
  }
  if (normalized == "vcd") {
    return TraceFormat::vcd;
  }
  if (normalized == "fst") {
    return TraceFormat::fst;
  }
  return std::nullopt;
}

std::optional<TraceCompression> parse_trace_compression(
    const std::string_view spelling) noexcept {
  const auto normalized = lowercase(spelling);
  if (normalized == "auto" || normalized == "automatic") {
    return TraceCompression::automatic;
  }
  if (normalized == "none" || normalized == "off") {
    return TraceCompression::none;
  }
  if (normalized == "deterministic" || normalized == "fixed" ||
      normalized == "fst-v1") {
    return TraceCompression::deterministic;
  }
  return std::nullopt;
}

std::optional<Config> parse(
    const std::string_view source,
    std::string source_name,
    const std::filesystem::path& base_directory,
    diagnostic::Engine& diagnostics) {
  Parser parser(source, source_name, base_directory, diagnostics);
  auto config = parser.run();
  if (!config.has_value()) {
    return std::nullopt;
  }
  config->manifest_path = absolute_normalized(
      fsim::support::path_from_utf8(source_name), base_directory);
  resolve_paths(*config, diagnostics);
  if (diagnostics.has_error()) {
    return std::nullopt;
  }
  return config;
}

std::optional<Config> load(
    const std::filesystem::path& manifest,
    diagnostic::Engine& diagnostics) {
  std::ifstream stream(manifest, std::ios::binary);
  if (!stream) {
    diagnostics.error(
        std::string(kIoCode),
        "unable to open project manifest: "
            + fsim::support::path_to_utf8(manifest),
        {fsim::support::path_to_utf8(manifest), {1, 1, 0}, {1, 1, 0}});
    return std::nullopt;
  }
  std::ostringstream contents;
  contents << stream.rdbuf();
  if (stream.bad()) {
    diagnostics.error(
        std::string(kIoCode),
        "failed while reading project manifest: "
            + fsim::support::path_to_utf8(manifest),
        {fsim::support::path_to_utf8(manifest), {1, 1, 0}, {1, 1, 0}});
    return std::nullopt;
  }
  std::error_code error;
  auto absolute_manifest = std::filesystem::absolute(manifest, error);
  if (error) {
    absolute_manifest = manifest;
  }
  absolute_manifest = absolute_manifest.lexically_normal();
  return parse(
      contents.str(),
      fsim::support::path_to_utf8(absolute_manifest),
      absolute_manifest.parent_path(),
      diagnostics);
}

}  // namespace fsim::project
