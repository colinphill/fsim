// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace fsim::support {

/// An instance option of a covergroup, coverpoint or cross (IEEE 1800-2023
/// 19.7, Table 19-1; the members of the `option` structures of 19.10).
enum class CoverageOption : std::uint8_t {
    name,
    weight,
    goal,
    comment,
    at_least,
    auto_bin_max,
    cross_num_print_missing,
    cross_retain_auto_bins,
    detect_overlap,
    per_instance,
    get_inst_coverage,
};

/// The last option, for range checks of an encoded option.
inline constexpr auto last_coverage_option = CoverageOption::get_inst_coverage;

/// Whether `option` is a `bit` (19.10); every other integral option is an
/// `int`.
[[nodiscard]] constexpr bool coverage_option_bit(
    const CoverageOption option) noexcept
{
    return option == CoverageOption::cross_retain_auto_bins
        || option == CoverageOption::detect_overlap
        || option == CoverageOption::per_instance
        || option == CoverageOption::get_inst_coverage;
}

/// Whether the value of `option` is a string (name, comment).
[[nodiscard]] constexpr bool coverage_option_string(
    const CoverageOption option) noexcept
{
    return option == CoverageOption::name || option == CoverageOption::comment;
}

} // namespace fsim::support
