// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"
#include "fsim/support/coverage_option.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::frontend {

/// An option member of a covergroup, coverpoint or cross (IEEE 1800-2023
/// 19.7, 19.7.1, 19.10): an instance option of Table 19-1 or a type option.
using SystemVerilogCoverageOption = support::CoverageOption;

/// The syntactic level an instance option belongs to (Table 19-2).
enum class SystemVerilogCoverageOptionLevel : std::uint8_t {
    covergroup,
    coverpoint,
    cross,
};

/// The instance option the member `option.member` names; none for a member
/// Table 19-1 does not list (a type-only member is not an instance option).
[[nodiscard]] std::optional<SystemVerilogCoverageOption>
systemverilog_coverage_option(std::string_view member) noexcept;

/// A type option of a covergroup, coverpoint or cross (19.7.1, Table 19-3).
enum class SystemVerilogCoverageTypeOption : std::uint8_t {
    weight,
    goal,
    comment,
    strobe,
    merge_instances,
    distribute_first,
    real_interval,
};

/// The type option the member `type_option.member` names; none for a member
/// Table 19-3 does not list.
[[nodiscard]] std::optional<SystemVerilogCoverageTypeOption>
systemverilog_coverage_type_option(std::string_view member) noexcept;

/// Whether type option `option` exists at `level` (Table 19-4): strobe,
/// merge_instances and distribute_first only for a covergroup,
/// real_interval for a covergroup or coverpoint (19.7.1).
[[nodiscard]] bool systemverilog_coverage_type_option_allowed(
    SystemVerilogCoverageTypeOption option,
    SystemVerilogCoverageOptionLevel level) noexcept;

/// The option member a type option is (support::CoverageOption); none for
/// real_interval, which only the covergroup definition sets.
[[nodiscard]] std::optional<SystemVerilogCoverageOption>
systemverilog_coverage_type_option_member(
    SystemVerilogCoverageTypeOption option) noexcept;

/// Whether a procedural assignment may set type option `option` (19.7.1):
/// strobe and real_interval are set only in the covergroup definition.
[[nodiscard]] bool systemverilog_coverage_type_option_procedural(
    SystemVerilogCoverageTypeOption option) noexcept;

/// The values of the type options weight, goal and comment of a covergroup
/// type, or of one of its coverpoints or crosses (19.7.1, Table 19-3).
struct SystemVerilogCoverageTypeOptionValues {
    std::uint32_t weight { 1U };
    std::uint32_t goal { 100U };
    std::string comment;
};

/// The type options of a covergroup type as its definition and later
/// procedural assignments left them (19.7.1): the covergroup's own, then
/// one entry per coverage declaration, in declaration order.
struct SystemVerilogCovergroupTypeOptions {
    SystemVerilogCoverageTypeOptionValues covergroup;
    bool strobe { };
    bool merge_instances { };
    bool distribute_first { };
    std::vector<SystemVerilogCoverageTypeOptionValues> items;
};

/// The type options `declaration` defines (19.7.1): its constant type
/// option assignments, Table 19-3's defaults otherwise.
[[nodiscard]] SystemVerilogCovergroupTypeOptions
systemverilog_covergroup_initial_type_options(
    const SystemVerilogCovergroupDeclaration& declaration);

/// The value of integral type option `option` (weight, goal, strobe,
/// merge_instances, distribute_first) of item `item` (as
/// systemverilog_coverage_option_level numbers it).
[[nodiscard]] std::int64_t systemverilog_coverage_type_option_value(
    const SystemVerilogCovergroupTypeOptions& options,
    std::uint32_t item,
    SystemVerilogCoverageOption option);

/// The value of type option comment of item `item`.
[[nodiscard]] std::string systemverilog_coverage_type_option_text(
    const SystemVerilogCovergroupTypeOptions& options,
    std::uint32_t item,
    SystemVerilogCoverageOption option);

/// Sets integral type option `option` of item `item` to `value`; the
/// message of the error when the value is outside the option's range or
/// the option cannot be assigned procedurally, which leaves the options
/// unchanged.
[[nodiscard]] std::optional<std::string>
assign_systemverilog_coverage_type_option(
    SystemVerilogCovergroupTypeOptions& options,
    std::uint32_t item,
    SystemVerilogCoverageOption option,
    std::int64_t value);

/// Sets type option comment of item `item` to `text`.
void assign_systemverilog_coverage_type_option_text(
    SystemVerilogCovergroupTypeOptions& options,
    std::uint32_t item,
    SystemVerilogCoverageOption option,
    std::string text);

/// The member name of `option`.
[[nodiscard]] std::string_view systemverilog_coverage_option_name(
    SystemVerilogCoverageOption option) noexcept;

/// Whether `option` exists at `level` (Table 19-2).
[[nodiscard]] bool systemverilog_coverage_option_allowed(
    SystemVerilogCoverageOption option,
    SystemVerilogCoverageOptionLevel level) noexcept;

/// Whether a procedural assignment may set `option` after the covergroup is
/// instantiated (19.7): per_instance and get_inst_coverage are set only in
/// the covergroup definition, and auto_bin_max, detect_overlap and
/// cross_retain_auto_bins only in the covergroup or coverpoint definition.
[[nodiscard]] bool systemverilog_coverage_option_procedural(
    SystemVerilogCoverageOption option) noexcept;

/// Whether the value of `option` is a string (name, comment).
[[nodiscard]] bool systemverilog_coverage_option_string(
    SystemVerilogCoverageOption option) noexcept;

/// The level of coverage item `item` of `declaration`: 0 is the covergroup,
/// n its coverage declaration n - 1. None for an item it does not have.
[[nodiscard]] std::optional<SystemVerilogCoverageOptionLevel>
systemverilog_coverage_option_level(
    const SystemVerilogCovergroupDeclaration& declaration,
    std::uint32_t item) noexcept;

/// The options an instance of `declaration` starts with (19.7): the values
/// of the definition's constant instance option assignments, the
/// covergroup's at_least, auto_bin_max, cross_num_print_missing,
/// cross_retain_auto_bins and detect_overlap as the default of its
/// coverpoints and crosses (Table 19-2), Table 19-1's defaults otherwise.
/// An assignment of a non-constant value is made at construction.
[[nodiscard]] SystemVerilogCovergroupInstanceOptions
systemverilog_covergroup_initial_options(
    const SystemVerilogCovergroupDeclaration& declaration, std::string name);

/// The options of the merged instance that type coverage is computed from
/// when type_option.merge_instances is set (19.11.3): the covergroup and
/// each coverpoint and cross take the type_option weight and goal of
/// `type_options` (Table 19-4).
[[nodiscard]] SystemVerilogCovergroupInstanceOptions
systemverilog_covergroup_type_options(
    const SystemVerilogCovergroupDeclaration& declaration,
    const SystemVerilogCovergroupTypeOptions& type_options);

/// The options of `instance`: those recorded for it, else the initial ones
/// of `declaration`.
[[nodiscard]] SystemVerilogCovergroupInstanceOptions
systemverilog_covergroup_options(
    const SystemVerilogCovergroupDeclaration& declaration,
    const SystemVerilogCovergroupInstance& instance);

/// The value of integral option `option` of item `item` (as
/// systemverilog_coverage_option_level numbers it).
[[nodiscard]] std::int64_t systemverilog_coverage_option_value(
    const SystemVerilogCovergroupInstanceOptions& options,
    std::uint32_t item,
    SystemVerilogCoverageOption option);

/// The value of string option `option` of item `item`.
[[nodiscard]] std::string systemverilog_coverage_option_text(
    const SystemVerilogCovergroupInstanceOptions& options,
    std::uint32_t item,
    SystemVerilogCoverageOption option);

/// Sets integral option `option` of item `item` to `value`; the message of
/// the error when the value is outside the option's range, which leaves the
/// options unchanged.
[[nodiscard]] std::optional<std::string> assign_systemverilog_coverage_option(
    SystemVerilogCovergroupInstanceOptions& options,
    std::uint32_t item,
    SystemVerilogCoverageOption option,
    std::int64_t value);

/// Sets string option `option` of item `item` to `text`.
void assign_systemverilog_coverage_option_text(
    SystemVerilogCovergroupInstanceOptions& options,
    std::uint32_t item,
    SystemVerilogCoverageOption option,
    std::string text);

/// Gives every bin and cross bin of `instance` the at_least of its
/// coverpoint or cross, and the covered state that follows: a bin whose hit
/// count is less than at_least is not covered (Table 19-1).
void apply_systemverilog_coverage_at_least(
    SystemVerilogCovergroupInstance& instance,
    const SystemVerilogCovergroupDeclaration& declaration);

} // namespace fsim::frontend
