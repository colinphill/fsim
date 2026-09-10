// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/preprocessor.hpp"

namespace fsim::frontend::preprocessor_detail {

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
    bool parent_active { };
    bool active { };
    bool branch_taken { };
    bool saw_else { };
    SourceSpan opening;
};

struct SourceMapping {
    std::string physical_source;
    std::string logical_source;
    std::size_t physical_anchor_line { 1 };
    std::size_t logical_anchor_line { 1 };
    unsigned level { };
};

struct ProcessedTokens {
    std::vector<SourceMapping> mappings;
    Token eof;
};

struct ProtectedPayloadRange {
    std::string source;
    std::size_t begin { }, end { };
};

[[nodiscard]] StandardRevision default_standard_revision(Language language);
[[nodiscard]] bool is_verilog_standard(StandardRevision standard);
[[nodiscard]] bool is_system_verilog_standard(StandardRevision standard);
[[nodiscard]] unsigned standard_rank(StandardRevision standard);
[[nodiscard]] std::optional<std::int32_t> systemverilog_coverage_constant(
    std::string_view name) noexcept;
[[nodiscard]] bool is_line_continuation(const Token& token);
[[nodiscard]] bool same_tokens(
    const std::vector<Token>& left,
    const std::vector<Token>& right);
[[nodiscard]] bool same_macro(const Macro& left, const Macro& right);
[[nodiscard]] bool identifier_spelling(std::string_view spelling);
[[nodiscard]] std::string string_literal_spelling(std::string_view value);
[[nodiscard]] std::optional<std::string> decode_string_literal(
    std::string_view spelling);
[[nodiscard]] std::optional<std::size_t> decimal_number(
    const Token& token,
    bool require_positive);
[[nodiscard]] SourceLocation remap_location(
    SourceLocation location,
    const SourceMapping& mapping);
[[nodiscard]] SourceSpan remap_span(
    SourceSpan span,
    const SourceMapping& mapping);
[[nodiscard]] std::string location_text(const SourceSpan& span);
[[nodiscard]] std::filesystem::path normalized_path(
    const std::filesystem::path& path);

} // namespace fsim::frontend::preprocessor_detail
