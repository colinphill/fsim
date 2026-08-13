// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"
#include "fsim/frontend/diagnostic.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fsim::frontend {

struct SystemVerilogCoverageSampleValue {
    SystemVerilogCoverageSampleValue() = default;
    SystemVerilogCoverageSampleValue(
        const std::int64_t input_value,
        const std::uint64_t input_unknown_mask,
        const std::uint32_t input_width,
        std::string input_value_bits = { },
        std::string input_unknown_bits = { },
        const bool input_signed = false)
        : value(input_value)
        , unknown_mask(input_unknown_mask)
        , width(input_width)
        , value_bits(std::move(input_value_bits))
        , unknown_bits(std::move(input_unknown_bits))
        , signed_value(input_signed)
    {
    }

    std::int64_t value { };
    std::uint64_t unknown_mask { };
    std::uint32_t width { 64U };
    // Canonical MSB-first value/unknown planes for arbitrary-width samples.
    // Empty planes preserve the scalar compatibility projection above.
    std::string value_bits;
    std::string unknown_bits;
    bool signed_value { };
};

struct SystemVerilogCoverageSampleResult {
    std::vector<std::string> hit_bin_identities;
    std::optional<std::string> selected_bin_identity;
    bool ignored { };
    bool illegal { };
    bool zero_weight_excluded { };
    bool threshold_reached { };
    bool hit_count_overflow { };
};

struct SystemVerilogCovergroupSampleInput {
    std::size_t coverage_declaration_index { };
    SystemVerilogCoverageSampleValue value;
};

struct SystemVerilogCovergroupSampledCoverpoint {
    std::size_t coverage_declaration_index { };
    SystemVerilogCoverageSampleResult result;
};

struct SystemVerilogCovergroupSampleResult {
    std::vector<SystemVerilogCovergroupSampledCoverpoint> coverpoints;
    std::vector<std::string> hit_cross_bin_identities;
    std::vector<std::string> excluded_cross_bin_identities;
};

// Evaluates a retained covergroup `with` expression against one candidate
// value. The same evaluator is used while distributing bin arrays and while
// sampling non-array bins.
[[nodiscard]] bool systemverilog_coverage_with_allows(
    std::span<const Token> tokens,
    const SystemVerilogCoverageSampleValue& value);

[[nodiscard]] SystemVerilogCoverageSampleResult
sample_systemverilog_coverpoint(
    SystemVerilogCovergroupInstance& instance,
    const SystemVerilogCovergroupDeclaration& declaration,
    std::size_t coverage_declaration_index,
    const SystemVerilogCoverageSampleValue& value,
    std::vector<Diagnostic>& diagnostics);

[[nodiscard]] SystemVerilogCoverageSampleResult
sample_systemverilog_coverpoint(
    SystemVerilogCovergroupInstance& instance,
    const SystemVerilogCovergroupDeclaration& declaration,
    std::size_t coverage_declaration_index,
    std::int64_t value,
    std::vector<Diagnostic>& diagnostics);

[[nodiscard]] SystemVerilogCovergroupSampleResult
sample_systemverilog_covergroup(
    SystemVerilogCovergroupInstance& instance,
    const SystemVerilogCovergroupDeclaration& declaration,
    std::span<const SystemVerilogCovergroupSampleInput> inputs,
    std::vector<Diagnostic>& diagnostics);

} // namespace fsim::frontend
