// SPDX-License-Identifier: Apache-2.0
#include "parser_support.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
#include <system_error>
#include <utility>

namespace fsim::frontend::detail {

std::string ascii_lower(const std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        result.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(character))));
    }
    return result;
}

bool iequals(const std::string_view left, const std::string_view right)
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(left[index]))
            != std::tolower(static_cast<unsigned char>(right[index]))) {
            return false;
        }
    }
    return true;
}

std::string vhdl_name(std::string_view raw)
{
    if (raw.size() >= 2 && raw.front() == '\\' && raw.back() == '\\') {
        raw = raw.substr(1, raw.size() - 2);
        std::string result;
        result.reserve(raw.size());
        for (std::size_t index = 0; index < raw.size(); ++index) {
            result.push_back(raw[index]);
            if (raw[index] == '\\' && index + 1 < raw.size()
                && raw[index + 1] == '\\') {
                ++index;
            }
        }
        return result;
    }
    return ascii_lower(raw);
}

std::optional<std::uint64_t> decimal_u64(const std::string_view raw)
{
    std::string cleaned;
    cleaned.reserve(raw.size());
    for (const char character : raw) {
        if (character != '_') {
            cleaned.push_back(character);
        }
    }
    std::uint64_t value { };
    const auto [end, error] = std::from_chars(
        cleaned.data(), cleaned.data() + cleaned.size(), value);
    if (error != std::errc { }
        || end != cleaned.data() + cleaned.size()) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::int64_t> decimal_i64(
    const std::string_view raw, const bool negative)
{
    const auto unsigned_value = decimal_u64(raw);
    if (!unsigned_value) {
        return std::nullopt;
    }
    if (negative) {
        constexpr auto minimum_magnitude
            = static_cast<std::uint64_t>(
                  std::numeric_limits<std::int64_t>::max())
            + 1U;
        if (*unsigned_value > minimum_magnitude) {
            return std::nullopt;
        }
        if (*unsigned_value == minimum_magnitude) {
            return std::numeric_limits<std::int64_t>::min();
        }
        return -static_cast<std::int64_t>(*unsigned_value);
    }
    if (*unsigned_value
        > static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(*unsigned_value);
}

ParserBase::ParserBase(
    std::vector<Token> tokens, std::vector<Diagnostic> diagnostics)
    : tokens_(std::move(tokens))
    , diagnostics_(std::move(diagnostics))
{
    for (auto& token : tokens_) {
        token.span.expansion_stack = token.expansion_stack;
    }
}

const Token& ParserBase::current(const std::size_t lookahead) const
{
    const auto target = std::min(index_ + lookahead, tokens_.size() - 1);
    return tokens_[target];
}

const Token& ParserBase::previous() const
{
    return tokens_[index_ == 0 ? 0 : index_ - 1];
}

bool ParserBase::at_end() const
{
    return current().kind == TokenKind::EndOfFile;
}

bool ParserBase::at(
    const TokenKind kind, const std::size_t lookahead) const
{
    return current(lookahead).kind == kind;
}

bool ParserBase::keyword(const std::string_view text,
    const std::size_t lookahead, const bool case_insensitive) const
{
    const auto& token = current(lookahead);
    if (token.kind != TokenKind::Identifier) {
        return false;
    }
    return case_insensitive ? iequals(token.text, text) : token.text == text;
}

bool ParserBase::any_keyword(
    const std::initializer_list<std::string_view> words,
    const bool case_insensitive) const
{
    return std::any_of(words.begin(), words.end(), [&](const auto word) {
        return keyword(word, 0, case_insensitive);
    });
}

Token ParserBase::advance()
{
    const Token token = current();
    if (!at_end()) {
        ++index_;
    }
    return token;
}

bool ParserBase::match(const TokenKind kind)
{
    if (!at(kind)) {
        return false;
    }
    advance();
    return true;
}

bool ParserBase::match_keyword(
    const std::string_view text, const bool case_insensitive)
{
    if (!keyword(text, 0, case_insensitive)) {
        return false;
    }
    advance();
    return true;
}

Token ParserBase::expect(const TokenKind kind,
    const std::string_view description, std::string code)
{
    if (at(kind)) {
        return advance();
    }
    error(current(), std::move(code),
        "expected " + std::string(description) + ", found "
            + to_string(current().kind));
    return current();
}

Token ParserBase::expect_keyword(const std::string_view word,
    const bool case_insensitive, std::string code)
{
    if (keyword(word, 0, case_insensitive)) {
        return advance();
    }
    error(current(), std::move(code),
        "expected '" + std::string(word) + "'");
    return current();
}

void ParserBase::error(
    const Token& token, std::string code, std::string message)
{
    diagnostics_.push_back(Diagnostic { DiagnosticSeverity::Error,
        std::move(code), std::move(message), token.span,
        token.expansion_stack });
}

void ParserBase::warning(
    const Token& token, std::string code, std::string message)
{
    diagnostics_.push_back(Diagnostic { DiagnosticSeverity::Warning,
        std::move(code), std::move(message), token.span,
        token.expansion_stack });
}

void ParserBase::skip_to_semicolon()
{
    while (!at_end() && !at(TokenKind::Semicolon)) {
        advance();
    }
    match(TokenKind::Semicolon);
}

void ParserBase::skip_balanced(const TokenKind open, const TokenKind close)
{
    if (!match(open)) {
        return;
    }
    std::size_t depth = 1;
    while (!at_end() && depth != 0) {
        if (match(open)) {
            ++depth;
        } else if (match(close)) {
            --depth;
        } else {
            advance();
        }
    }
}

std::size_t ParserBase::position() const noexcept { return index_; }

void ParserBase::rewind(const std::size_t position) noexcept
{
    index_ = position;
}

} // namespace fsim::frontend::detail
