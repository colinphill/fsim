// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"
#include "fsim/frontend/token.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace fsim::frontend {

[[nodiscard]] std::vector<std::vector<Token>> split_top_level(
    std::span<const Token> tokens, TokenKind separator);
[[nodiscard]] std::optional<std::size_t> find_top_level(
    std::span<const Token> tokens, TokenKind sought);
[[nodiscard]] bool assertion_local_type_keyword(std::string_view text);
[[nodiscard]] bool valid_named_local_type_prefix(
    std::span<const Token> tokens);
[[nodiscard]] bool assertion_reference_keyword(std::string_view text);
[[nodiscard]] std::unordered_set<std::string> assertion_design_unit_names(
    const DesignUnit& unit);

/// The directive a concurrent assertion statement checks (16.14).
[[nodiscard]] support::SvaDirective sva_directive(
    SystemVerilogConcurrentAssertionKind kind) noexcept;
/// A marker statement of a concurrent assertion's generated monitor.
[[nodiscard]] Statement sva_marker_statement(support::SvaMarkerKind kind,
    support::SvaOutcome outcome, support::SvaDirective directive,
    std::uint32_t slot, std::string name, const SourceSpan& span);
/// The replay record of a display or report action (16.12.2).
[[nodiscard]] SvaReplayedAction sva_replayed_action(const Statement& action);

namespace assertion_resolution_detail {

[[nodiscard]] std::optional<Expression> scalar_expression(
    std::span<const Token> tokens);
[[nodiscard]] std::optional<Type> assertion_local_type(
    std::span<const Token> tokens);
[[nodiscard]] std::vector<Statement> action_statements(
    std::span<const Token> tokens);

} // namespace assertion_resolution_detail

} // namespace fsim::frontend
