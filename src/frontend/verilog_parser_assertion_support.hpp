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

namespace assertion_resolution_detail {

[[nodiscard]] std::optional<Expression> scalar_expression(
    std::span<const Token> tokens);
[[nodiscard]] std::optional<Type> assertion_local_type(
    std::span<const Token> tokens);
[[nodiscard]] std::vector<Statement> action_statements(
    std::span<const Token> tokens);

} // namespace assertion_resolution_detail

} // namespace fsim::frontend
