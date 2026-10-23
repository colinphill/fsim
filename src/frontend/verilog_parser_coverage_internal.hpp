// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "verilog_parser_assertion_support.hpp"
#include "verilog_parser_internal.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
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


[[nodiscard]] std::optional<std::size_t> find_top_level_keyword(
    std::span<const Token> tokens,
    std::string_view keyword);

[[nodiscard]] std::optional<CoverageLiteral> coverage_literal(
    std::span<const Token> tokens);

// Folds the tokens of a constant integral expression to its value (IEEE
// 1800-2017 19.5, 19.5.1: bin values, ranges, transition items and
// repetition counts are constant expressions).
using CoverageConstantFolder
    = std::function<std::optional<std::int64_t>(std::span<const Token>)>;

// While alive, coverage_literal folds the tokens of a constant expression
// through `folder`. Without one a bin item is a bare literal.
class CoverageConstantScope {
public:
    explicit CoverageConstantScope(const CoverageConstantFolder* folder) noexcept;
    ~CoverageConstantScope();
    CoverageConstantScope(const CoverageConstantScope&) = delete;
    CoverageConstantScope& operator=(const CoverageConstantScope&) = delete;

private:
    const CoverageConstantFolder* previous_;
};

// The value of an integral constant expression over literals and the named
// value parameters, with the operators of IEEE 1800-2017 11.4 on 64-bit
// two's-complement values. Empty when the expression is not such a
// constant or overflows.
[[nodiscard]] std::optional<std::int64_t> fold_coverage_constant_expression(
    const Expression& expression,
    const std::unordered_map<std::string, std::int64_t>& parameters);

[[nodiscard]] std::optional<SystemVerilogCoverageBinValue> coverage_bin_value(
    std::span<const Token> tokens,
    bool wildcard_bin);

[[nodiscard]] std::optional<
    std::vector<SystemVerilogCoverageTransitionSequence>>
structure_transition_sequences(
    std::span<const Token> tokens,
    bool wildcard_bin);

} // namespace fsim::frontend::coverage_parser_detail
