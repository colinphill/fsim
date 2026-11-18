// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace fsim::support {

/// A member of the `option` or `type_option` structure of a covergroup,
/// coverpoint or cross (IEEE 1800-2023 19.10): an instance option of Table
/// 19-1 (name through get_inst_coverage) or a type option of Table 19-3
/// (weight, goal and comment, which both tables have, and strobe,
/// merge_instances and distribute_first). The type option real_interval is
/// set only in the covergroup definition and is not a member here.
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
    strobe,
    merge_instances,
    distribute_first,
};

/// The last option, for range checks of an encoded option.
inline constexpr auto last_coverage_option = CoverageOption::distribute_first;

/// Whether `option` is a `bit` (19.10); every other integral option is an
/// `int`.
[[nodiscard]] constexpr bool coverage_option_bit(
    const CoverageOption option) noexcept
{
    return option == CoverageOption::cross_retain_auto_bins
        || option == CoverageOption::detect_overlap
        || option == CoverageOption::per_instance
        || option == CoverageOption::get_inst_coverage
        || option == CoverageOption::strobe
        || option == CoverageOption::merge_instances
        || option == CoverageOption::distribute_first;
}

/// Whether the value of `option` is a string (name, comment).
[[nodiscard]] constexpr bool coverage_option_string(
    const CoverageOption option) noexcept
{
    return option == CoverageOption::name || option == CoverageOption::comment;
}

} // namespace fsim::support
