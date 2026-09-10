// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "verilog_parser_internal.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::frontend::coverage_parser_detail {

struct CoverageLiteral {
    std::optional<std::int64_t> exact;
    std::uint64_t value { };
    std::uint64_t mask { };
    std::uint32_t width { };
    bool wildcard { };
    bool signed_value { };
    std::string value_bits;
    std::string unknown_bits;
    std::string mask_bits;
    std::optional<std::uint64_t> real_bits;
};

[[nodiscard]] std::optional<std::size_t> matching_right_parenthesis(
    std::span<const Token> tokens,
    std::size_t left);

[[nodiscard]] std::optional<std::size_t> matching_right_brace(
    std::span<const Token> tokens,
    std::size_t left);

[[nodiscard]] std::vector<std::vector<Token>> split_top_level(
    std::span<const Token> tokens,
    TokenKind separator);

[[nodiscard]] std::optional<std::size_t> find_top_level(
    std::span<const Token> tokens,
    TokenKind sought);

[[nodiscard]] std::optional<std::size_t> find_top_level_keyword(
    std::span<const Token> tokens,
    std::string_view keyword);

[[nodiscard]] std::optional<CoverageLiteral> coverage_literal(
    std::span<const Token> tokens);

[[nodiscard]] std::optional<SystemVerilogCoverageBinValue> coverage_bin_value(
    std::span<const Token> tokens,
    bool wildcard_bin);

[[nodiscard]] std::optional<
    std::vector<SystemVerilogCoverageTransitionSequence>>
structure_transition_sequences(
    std::span<const Token> tokens,
    bool wildcard_bin);

} // namespace fsim::frontend::coverage_parser_detail
