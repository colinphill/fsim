// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/coverage_options.hpp"

#include <array>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>

namespace fsim::frontend {
namespace {

    struct OptionEntry {
        std::string_view name;
        SystemVerilogCoverageOption option;
    };

    constexpr std::array option_entries {
        OptionEntry { "name", SystemVerilogCoverageOption::name },
        OptionEntry { "weight", SystemVerilogCoverageOption::weight },
        OptionEntry { "goal", SystemVerilogCoverageOption::goal },
        OptionEntry { "comment", SystemVerilogCoverageOption::comment },
        OptionEntry { "at_least", SystemVerilogCoverageOption::at_least },
        OptionEntry { "auto_bin_max",
            SystemVerilogCoverageOption::auto_bin_max },
        OptionEntry { "cross_num_print_missing",
            SystemVerilogCoverageOption::cross_num_print_missing },
        OptionEntry { "cross_retain_auto_bins",
            SystemVerilogCoverageOption::cross_retain_auto_bins },
        OptionEntry { "detect_overlap",
            SystemVerilogCoverageOption::detect_overlap },
        OptionEntry { "per_instance",
            SystemVerilogCoverageOption::per_instance },
        OptionEntry { "get_inst_coverage",
            SystemVerilogCoverageOption::get_inst_coverage },
    };

    // The values of item `item` of `options` (0 is the covergroup's own).
    template <typename Options>
    [[nodiscard]] auto& values_of(Options& options, const std::uint32_t item)
    {
        if (item == 0U) {
            return options.covergroup;
        }
        if (item > options.items.size()) {
            throw std::out_of_range { "covergroup option item is unavailable" };
        }
        return options.items[item - 1U];
    }

    // Applies the constant instance option assignments in `assignments` of
    // scope `scope` to `values`.
    void apply_assignments(
        SystemVerilogCoverageOptionValues& values,
        const std::vector<SystemVerilogCovergroupOptionAssignment>& assignments,
        const SystemVerilogCovergroupOptionScope scope)
    {
        for (const auto& assignment : assignments) {
            if (assignment.scope != scope) {
                continue;
            }
            const auto option = systemverilog_coverage_option(assignment.name);
            if (option == SystemVerilogCoverageOption::comment
                && assignment.evaluated_text) {
                values.comment = *assignment.evaluated_text;
                continue;
            }
            if (!option || systemverilog_coverage_option_string(*option)
                || !assignment.evaluated_value) {
                continue;
            }
            const auto value = *assignment.evaluated_value;
            switch (*option) {
            case SystemVerilogCoverageOption::weight:
                values.weight = static_cast<std::uint32_t>(value);
                break;
            case SystemVerilogCoverageOption::goal:
                values.goal = static_cast<std::uint32_t>(value);
                break;
            case SystemVerilogCoverageOption::at_least:
                values.at_least = value;
                break;
            case SystemVerilogCoverageOption::auto_bin_max:
                values.auto_bin_max = static_cast<std::uint32_t>(value);
                break;
            case SystemVerilogCoverageOption::cross_num_print_missing:
                values.cross_num_print_missing
                    = static_cast<std::uint32_t>(value);
                break;
            case SystemVerilogCoverageOption::cross_retain_auto_bins:
                values.cross_retain_auto_bins = value != 0U;
                break;
            case SystemVerilogCoverageOption::detect_overlap:
                values.detect_overlap = value != 0U;
                break;
            case SystemVerilogCoverageOption::name:
            case SystemVerilogCoverageOption::comment:
            case SystemVerilogCoverageOption::per_instance:
            case SystemVerilogCoverageOption::get_inst_coverage:
                break;
            }
        }
    }

    [[nodiscard]] std::optional<std::uint64_t> covergroup_flag(
        const SystemVerilogCovergroupDeclaration& declaration,
        const SystemVerilogCoverageOption option)
    {
        std::optional<std::uint64_t> result;
        for (const auto& assignment : declaration.option_assignments) {
            if (assignment.scope == SystemVerilogCovergroupOptionScope::Instance
                && assignment.evaluated_value
                && systemverilog_coverage_option(assignment.name) == option) {
                result = assignment.evaluated_value;
            }
        }
        return result;
    }

} // namespace

std::optional<SystemVerilogCoverageOption> systemverilog_coverage_option(
    const std::string_view member) noexcept
{
    for (const auto& entry : option_entries) {
        if (entry.name == member) {
            return entry.option;
        }
    }
    return std::nullopt;
}

std::optional<SystemVerilogCoverageTypeOption>
systemverilog_coverage_type_option(const std::string_view member) noexcept
{
    using Option = SystemVerilogCoverageTypeOption;
    struct Entry {
        std::string_view name;
        Option option;
    };
    static constexpr std::array<Entry, 7U> entries { {
        { "weight", Option::weight },
        { "goal", Option::goal },
        { "comment", Option::comment },
        { "strobe", Option::strobe },
        { "merge_instances", Option::merge_instances },
        { "distribute_first", Option::distribute_first },
        { "real_interval", Option::real_interval },
    } };
    for (const auto& entry : entries) {
        if (entry.name == member) {
            return entry.option;
        }
    }
    return std::nullopt;
}

bool systemverilog_coverage_type_option_allowed(
    const SystemVerilogCoverageTypeOption option,
    const SystemVerilogCoverageOptionLevel level) noexcept
{
    using Option = SystemVerilogCoverageTypeOption;
    switch (option) {
    case Option::weight:
    case Option::goal:
    case Option::comment:
        return true;
    case Option::strobe:
    case Option::merge_instances:
    case Option::distribute_first:
        return level == SystemVerilogCoverageOptionLevel::covergroup;
    case Option::real_interval:
        return level != SystemVerilogCoverageOptionLevel::cross;
    }
    return false;
}

std::string_view systemverilog_coverage_option_name(
    const SystemVerilogCoverageOption option) noexcept
{
    for (const auto& entry : option_entries) {
        if (entry.option == option) {
            return entry.name;
        }
    }
    return { };
}

bool systemverilog_coverage_option_allowed(
    const SystemVerilogCoverageOption option,
    const SystemVerilogCoverageOptionLevel level) noexcept
{
    using Level = SystemVerilogCoverageOptionLevel;
    switch (option) {
    case SystemVerilogCoverageOption::weight:
    case SystemVerilogCoverageOption::goal:
    case SystemVerilogCoverageOption::comment:
    case SystemVerilogCoverageOption::at_least:
        return true;
    case SystemVerilogCoverageOption::auto_bin_max:
    case SystemVerilogCoverageOption::detect_overlap:
        return level != Level::cross;
    case SystemVerilogCoverageOption::cross_num_print_missing:
    case SystemVerilogCoverageOption::cross_retain_auto_bins:
        return level != Level::coverpoint;
    case SystemVerilogCoverageOption::name:
    case SystemVerilogCoverageOption::per_instance:
    case SystemVerilogCoverageOption::get_inst_coverage:
        return level == Level::covergroup;
    }
    return false;
}

bool systemverilog_coverage_option_procedural(
    const SystemVerilogCoverageOption option) noexcept
{
    switch (option) {
    case SystemVerilogCoverageOption::per_instance:
    case SystemVerilogCoverageOption::get_inst_coverage:
    case SystemVerilogCoverageOption::auto_bin_max:
    case SystemVerilogCoverageOption::detect_overlap:
    case SystemVerilogCoverageOption::cross_retain_auto_bins:
        return false;
    case SystemVerilogCoverageOption::name:
    case SystemVerilogCoverageOption::weight:
    case SystemVerilogCoverageOption::goal:
    case SystemVerilogCoverageOption::comment:
    case SystemVerilogCoverageOption::at_least:
    case SystemVerilogCoverageOption::cross_num_print_missing:
        return true;
    }
    return false;
}

bool systemverilog_coverage_option_string(
    const SystemVerilogCoverageOption option) noexcept
{
    return support::coverage_option_string(option);
}

std::optional<SystemVerilogCoverageOptionLevel>
systemverilog_coverage_option_level(
    const SystemVerilogCovergroupDeclaration& declaration,
    const std::uint32_t item) noexcept
{
    if (item == 0U) {
        return SystemVerilogCoverageOptionLevel::covergroup;
    }
    if (item > declaration.coverage_declarations.size()) {
        return std::nullopt;
    }
    return declaration.coverage_declarations[item - 1U].kind
            == SystemVerilogCoverageDeclarationKind::Coverpoint
        ? SystemVerilogCoverageOptionLevel::coverpoint
        : SystemVerilogCoverageOptionLevel::cross;
}

SystemVerilogCovergroupInstanceOptions systemverilog_covergroup_initial_options(
    const SystemVerilogCovergroupDeclaration& declaration, std::string name)
{
    using Scope = SystemVerilogCovergroupOptionScope;
    SystemVerilogCovergroupInstanceOptions result;
    result.name = std::move(name);
    // A constant option.name of the definition names every instance (19.7).
    for (const auto& assignment : declaration.option_assignments) {
        if (assignment.scope == Scope::Instance && assignment.evaluated_text
            && systemverilog_coverage_option(assignment.name)
                == SystemVerilogCoverageOption::name) {
            result.name = *assignment.evaluated_text;
        }
    }
    result.per_instance
        = covergroup_flag(declaration, SystemVerilogCoverageOption::per_instance)
              .value_or(0U)
        != 0U;
    result.get_inst_coverage
        = covergroup_flag(
              declaration, SystemVerilogCoverageOption::get_inst_coverage)
              .value_or(0U)
        != 0U;
    apply_assignments(
        result.covergroup, declaration.option_assignments, Scope::Instance);
    result.items.reserve(declaration.coverage_declarations.size());
    for (const auto& item : declaration.coverage_declarations) {
        // weight, goal and comment of the covergroup are its own; the other
        // options are the defaults of its coverpoints and crosses.
        SystemVerilogCoverageOptionValues values;
        values.at_least = result.covergroup.at_least;
        values.auto_bin_max = result.covergroup.auto_bin_max;
        values.cross_num_print_missing
            = result.covergroup.cross_num_print_missing;
        values.cross_retain_auto_bins = result.covergroup.cross_retain_auto_bins;
        values.detect_overlap = result.covergroup.detect_overlap;
        apply_assignments(values, item.option_assignments, Scope::Instance);
        result.items.push_back(std::move(values));
    }
    return result;
}

SystemVerilogCovergroupInstanceOptions systemverilog_covergroup_type_options(
    const SystemVerilogCovergroupDeclaration& declaration)
{
    auto result = systemverilog_covergroup_initial_options(declaration, { });
    result.covergroup.goal = declaration.effective_type_goal;
    result.covergroup.weight = declaration.effective_type_weight;
    for (std::size_t index = 0U; index < result.items.size(); ++index) {
        auto& values = result.items[index];
        values.weight = 1U;
        values.goal = 100U;
        for (const auto& assignment :
            declaration.coverage_declarations[index].option_assignments) {
            if (assignment.scope != SystemVerilogCovergroupOptionScope::Type
                || !assignment.evaluated_value) {
                continue;
            }
            const auto option = systemverilog_coverage_option(assignment.name);
            if (option == SystemVerilogCoverageOption::weight) {
                values.weight
                    = static_cast<std::uint32_t>(*assignment.evaluated_value);
            } else if (option == SystemVerilogCoverageOption::goal) {
                values.goal
                    = static_cast<std::uint32_t>(*assignment.evaluated_value);
            }
        }
    }
    return result;
}

SystemVerilogCovergroupInstanceOptions systemverilog_covergroup_options(
    const SystemVerilogCovergroupDeclaration& declaration,
    const SystemVerilogCovergroupInstance& instance)
{
    if (instance.options
        && instance.options->items.size()
            == declaration.coverage_declarations.size()) {
        return *instance.options;
    }
    if (instance.options) {
        throw std::logic_error {
            "covergroup instance options do not match its declaration"
        };
    }
    return systemverilog_covergroup_initial_options(
        declaration, instance.owner_identity + "." + instance.name);
}

std::int64_t systemverilog_coverage_option_value(
    const SystemVerilogCovergroupInstanceOptions& options,
    const std::uint32_t item,
    const SystemVerilogCoverageOption option)
{
    const auto& values = values_of(options, item);
    switch (option) {
    case SystemVerilogCoverageOption::weight:
        return values.weight;
    case SystemVerilogCoverageOption::goal:
        return values.goal;
    case SystemVerilogCoverageOption::at_least:
        return static_cast<std::int64_t>(values.at_least);
    case SystemVerilogCoverageOption::auto_bin_max:
        return values.auto_bin_max;
    case SystemVerilogCoverageOption::cross_num_print_missing:
        return values.cross_num_print_missing;
    case SystemVerilogCoverageOption::cross_retain_auto_bins:
        return values.cross_retain_auto_bins ? 1 : 0;
    case SystemVerilogCoverageOption::detect_overlap:
        return values.detect_overlap ? 1 : 0;
    case SystemVerilogCoverageOption::per_instance:
        return options.per_instance ? 1 : 0;
    case SystemVerilogCoverageOption::get_inst_coverage:
        return options.get_inst_coverage ? 1 : 0;
    case SystemVerilogCoverageOption::name:
    case SystemVerilogCoverageOption::comment:
        break;
    }
    throw std::invalid_argument { "covergroup option is not integral" };
}

std::string systemverilog_coverage_option_text(
    const SystemVerilogCovergroupInstanceOptions& options,
    const std::uint32_t item,
    const SystemVerilogCoverageOption option)
{
    if (option == SystemVerilogCoverageOption::name && item == 0U) {
        return options.name;
    }
    if (option == SystemVerilogCoverageOption::comment) {
        return values_of(options, item).comment;
    }
    throw std::invalid_argument { "covergroup option is not a string" };
}

std::optional<std::string> assign_systemverilog_coverage_option(
    SystemVerilogCovergroupInstanceOptions& options,
    const std::uint32_t item,
    const SystemVerilogCoverageOption option,
    const std::int64_t value)
{
    auto& values = values_of(options, item);
    const auto outside = [&](const std::int64_t low, const std::int64_t high)
        -> std::optional<std::string> {
        if (value >= low && value <= high) {
            return std::nullopt;
        }
        return "covergroup option '"
            + std::string { systemverilog_coverage_option_name(option) }
            + "' value " + std::to_string(value)
            + " is outside its range " + std::to_string(low) + " to "
            + std::to_string(high);
    };
    constexpr auto int_max = std::numeric_limits<std::int32_t>::max();
    switch (option) {
    case SystemVerilogCoverageOption::weight:
        if (auto error = outside(0, 65'536)) {
            return error;
        }
        values.weight = static_cast<std::uint32_t>(value);
        return std::nullopt;
    case SystemVerilogCoverageOption::goal:
        if (auto error = outside(0, 100)) {
            return error;
        }
        values.goal = static_cast<std::uint32_t>(value);
        return std::nullopt;
    case SystemVerilogCoverageOption::at_least:
        if (auto error = outside(1, int_max)) {
            return error;
        }
        values.at_least = static_cast<std::uint64_t>(value);
        return std::nullopt;
    case SystemVerilogCoverageOption::auto_bin_max:
        if (auto error = outside(1, int_max)) {
            return error;
        }
        values.auto_bin_max = static_cast<std::uint32_t>(value);
        return std::nullopt;
    case SystemVerilogCoverageOption::cross_num_print_missing:
        if (auto error = outside(0, int_max)) {
            return error;
        }
        values.cross_num_print_missing = static_cast<std::uint32_t>(value);
        return std::nullopt;
    case SystemVerilogCoverageOption::cross_retain_auto_bins:
        values.cross_retain_auto_bins = value != 0;
        return std::nullopt;
    case SystemVerilogCoverageOption::detect_overlap:
        values.detect_overlap = value != 0;
        return std::nullopt;
    case SystemVerilogCoverageOption::per_instance:
        options.per_instance = value != 0;
        return std::nullopt;
    case SystemVerilogCoverageOption::get_inst_coverage:
        options.get_inst_coverage = value != 0;
        return std::nullopt;
    case SystemVerilogCoverageOption::name:
    case SystemVerilogCoverageOption::comment:
        break;
    }
    throw std::invalid_argument { "covergroup option is not integral" };
}

void assign_systemverilog_coverage_option_text(
    SystemVerilogCovergroupInstanceOptions& options,
    const std::uint32_t item,
    const SystemVerilogCoverageOption option,
    std::string text)
{
    if (option == SystemVerilogCoverageOption::name && item == 0U) {
        options.name = std::move(text);
        return;
    }
    if (option == SystemVerilogCoverageOption::comment) {
        values_of(options, item).comment = std::move(text);
        return;
    }
    throw std::invalid_argument { "covergroup option is not a string" };
}

void apply_systemverilog_coverage_at_least(
    SystemVerilogCovergroupInstance& instance,
    const SystemVerilogCovergroupDeclaration& declaration)
{
    const auto options = systemverilog_covergroup_options(declaration, instance);
    const auto at_least_of = [&](const std::size_t declaration_index)
        -> std::optional<std::uint64_t> {
        for (std::size_t index = 0U;
            index < declaration.coverage_declarations.size(); ++index) {
            if (declaration.coverage_declarations[index].declaration_index
                == declaration_index) {
                return options.items[index].at_least;
            }
        }
        return std::nullopt;
    };
    for (auto& hit : instance.bin_hits) {
        if (const auto at_least = at_least_of(hit.coverage_declaration_index)) {
            hit.at_least = *at_least;
            hit.covered = hit.hit_count >= hit.at_least;
        }
    }
    for (auto& state : instance.cross_bin_state) {
        if (const auto at_least
            = at_least_of(state.coverage_declaration_index)) {
            state.at_least = *at_least;
            state.covered = !state.excluded && state.hit_count >= state.at_least;
        }
    }
}

} // namespace fsim::frontend
