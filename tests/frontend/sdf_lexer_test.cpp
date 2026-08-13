// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/sdf.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using fsim::frontend::Diagnostic;
using fsim::frontend::SdfLexResult;
using fsim::frontend::SdfToken;
using fsim::frontend::SdfTokenKind;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error(std::string { message });
    }
}

const SdfToken& require_token(
    const SdfLexResult& result, const SdfTokenKind kind,
    const std::string_view spelling)
{
    const auto token = std::ranges::find_if(result.tokens, [&](const SdfToken& value) {
        return value.kind == kind && value.spelling == spelling;
    });
    require(token != result.tokens.end(), "expected SDF token is missing");
    return *token;
}

[[nodiscard]] std::size_t count_tokens(const SdfLexResult& result,
    const SdfTokenKind kind)
{
    return static_cast<std::size_t>(
        std::ranges::count_if(result.tokens, [&](const SdfToken& token) {
            return token.kind == kind;
        }));
}

const Diagnostic& require_diagnostic(
    const SdfLexResult& result, const std::string_view code)
{
    const auto diagnostic = std::ranges::find_if(result.diagnostics, [&](const Diagnostic& value) {
        return value.code == code;
    });
    if (diagnostic == result.diagnostics.end()) {
        std::string message = "expected SDF diagnostic is missing: ";
        message += code;
        message += " (observed";
        for (const auto& observed : result.diagnostics) {
            message += ' ';
            message += observed.code;
        }
        message += ')';
        throw std::runtime_error(message);
    }
    return *diagnostic;
}

void test_complete_lexical_surface()
{
    using namespace fsim::frontend;
    const auto result = lex_sdf(SourceText { "complete.sdf", R"((DELAYFILE
  // retained line
  /* retained
     block */
  (SDFVERSION "4.0")
  (DIVIDER /)
  (CELL (CELLTYPE "wide cell") (INSTANCE top/u\.core[3])
    (DELAY (ABSOLUTE (IOPATH (posedge A) Z (-1.25:2.50:3.75e+1)))))
)
)" });
    require(result.ok() && !result.resource_exhausted,
        "complete SDF lexical surface must succeed");
    require(count_tokens(result, SdfTokenKind::Comment) == 2U,
        "SDF line and block comments must be retained");
    require(count_tokens(result, SdfTokenKind::Number) == 3U,
        "SDF signed decimal and triple values must be distinct tokens");
    require(count_tokens(result, SdfTokenKind::Colon) == 2U,
        "SDF triple separators must be retained");

    const auto& first = result.tokens.front();
    require(first.kind == SdfTokenKind::LeftParenthesis && first.span.begin.offset == 0U && first.span.begin.line == 1U && first.span.begin.column == 1U && first.span.end.offset == 1U && first.span.end.column == 2U,
        "first SDF delimiter must retain exact byte and line coordinates");
    const auto& keyword = require_token(result, SdfTokenKind::Keyword, "DELAYFILE");
    require(keyword.span.begin.line == 1U && keyword.span.begin.column == 2U,
        "SDF keyword coordinate must be exact");
    const auto& line_comment = require_token(
        result, SdfTokenKind::Comment, "// retained line");
    require(line_comment.span.begin.line == 2U && line_comment.span.begin.column == 3U && line_comment.span.end.line == 2U,
        "SDF line-comment coordinate must exclude the line ending");
    const auto& block_comment = require_token(
        result, SdfTokenKind::Comment, "/* retained\n     block */");
    require(block_comment.span.begin.line == 3U && block_comment.span.end.line == 4U,
        "SDF block-comment span must cross lines exactly");
    require_token(result, SdfTokenKind::String, "\"wide cell\"");
    require_token(result, SdfTokenKind::Slash, "/");
    require_token(result, SdfTokenKind::EscapedIdentifier,
        "top/u\\.core[3]");
    require_token(result, SdfTokenKind::Number, "-1.25");
    require_token(result, SdfTokenKind::Number, "2.50");
    require_token(result, SdfTokenKind::Number, "3.75e+1");
    require(result.tokens.back().kind == SdfTokenKind::EndOfFile && result.tokens.back().span.begin.line == 10U,
        "SDF EOF token must retain the final logical coordinate");
    require(is_sdf_keyword("timingcheck") && !is_sdf_keyword("user_cell_type") && std::string_view { to_string(SdfTokenKind::EscapedIdentifier) } == "escaped-identifier",
        "SDF keyword and token spelling APIs must be deterministic");
}

void test_signed_realtime_and_crlf_coordinates()
{
    using namespace fsim::frontend;
    const auto result = lex_sdf(SourceText {
        "coordinates.sdf",
        "(VOLTAGE -1.25e+03)\r\n(TEMPERATURE +85.0)\n" });
    require(result.ok(), "signed SDF realtime values must lex");
    const auto& voltage = require_token(result, SdfTokenKind::Number, "-1.25e+03");
    const auto& temperature = require_token(result, SdfTokenKind::Number, "+85.0");
    require(voltage.span.begin.line == 1U && voltage.span.begin.column == 10U && temperature.span.begin.line == 2U && temperature.span.begin.column == 14U,
        "CRLF must advance one SDF logical line while retaining byte offsets");
    require(temperature.span.begin.offset == 34U,
        "CRLF SDF byte offset must include both physical bytes");
}

void test_lexical_diagnostics_and_recovery()
{
    using namespace fsim::frontend;
    const auto string = lex_sdf(SourceText { "string.sdf", "(\"unterminated" });
    require_diagnostic(string, "FSIM-SDF-LEX-003");
    require_diagnostic(string, "FSIM-SDF-LEX-010");

    const auto comment = lex_sdf(SourceText { "comment.sdf", "(/* unterminated" });
    require_diagnostic(comment, "FSIM-SDF-LEX-002");

    const auto exponent = lex_sdf(SourceText { "exponent.sdf", "(1e+)" });
    const auto& exponent_diagnostic = require_diagnostic(exponent, "FSIM-SDF-LEX-005");
    require(exponent_diagnostic.span.begin.line == 1U && exponent_diagnostic.span.begin.column == 3U && exponent_diagnostic.span.end.column == 5U,
        "malformed SDF exponent must retain its exact coordinate");

    const auto escape = lex_sdf(SourceText { "escape.sdf", "(top\\)" });
    require_diagnostic(escape, "FSIM-SDF-LEX-004");

    std::string invalid { "(A" };
    invalid.push_back('\x01');
    invalid.push_back(')');
    const auto byte = lex_sdf(SourceText { "byte.sdf", std::move(invalid) });
    require_diagnostic(byte, "FSIM-SDF-LEX-001");
    require(require_token(byte, SdfTokenKind::Identifier, "A").span.begin.column == 2U,
        "SDF lexer must recover after an invalid byte");

    const auto closing = lex_sdf(SourceText { "closing.sdf", ")" });
    require_diagnostic(closing, "FSIM-SDF-LEX-010");
}

void test_governed_resource_limits()
{
    using namespace fsim::frontend;
    SdfLexerLimits limits;
    limits.max_source_bytes = 3U;
    auto result = lex_sdf(SourceText { "source-limit.sdf", "(AB)" }, limits);
    require(result.resource_exhausted,
        "SDF source-byte limit must stop lexical work");
    require_diagnostic(result, "FSIM-SDF-LEX-007");

    limits = { };
    limits.max_tokens = 2U;
    result = lex_sdf(SourceText { "token-limit.sdf", "(A)" }, limits);
    require(result.resource_exhausted,
        "SDF token-count limit must stop lexical work");
    require_diagnostic(result, "FSIM-SDF-LEX-008");

    limits = { };
    limits.max_parenthesis_depth = 2U;
    result = lex_sdf(SourceText { "depth-limit.sdf", "((()))" }, limits);
    require(result.resource_exhausted,
        "SDF parenthesis-depth limit must stop lexical work");
    require_diagnostic(result, "FSIM-SDF-LEX-009");

    limits = { };
    limits.max_token_bytes = 3U;
    result = lex_sdf(SourceText { "token-bytes.sdf", "(ABCD)" }, limits);
    require(result.resource_exhausted,
        "SDF token-byte limit must stop lexical work");
    require_diagnostic(result, "FSIM-SDF-LEX-011");

    limits = { };
    limits.max_numeric_bytes = 3U;
    result = lex_sdf(SourceText { "numeric-bytes.sdf", "(1234)" }, limits);
    require(result.resource_exhausted,
        "SDF numeric-byte limit must stop lexical work");
    require_diagnostic(result, "FSIM-SDF-LEX-006");
}

} // namespace

int main()
{
    try {
        test_complete_lexical_surface();
        test_signed_realtime_and_crlf_coordinates();
        test_lexical_diagnostics_and_recovery();
        test_governed_resource_limits();
        std::cout << "SDF lexer tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "SDF lexer test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
