// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "lowerer_internal.hpp"

#include <optional>
#include <string_view>

// Name helpers shared by the VITAL (IEEE 1076.4) lowerers.
namespace fsim::elaboration::vital_detail {

// The last '.'-separated part of a VITAL subprogram or type name.
[[nodiscard]] constexpr std::string_view vital_simple_name(
    const std::string_view name) noexcept
{
    const auto separator = name.find_last_of('.');
    return name.substr(
        separator == std::string_view::npos ? 0U : separator + 1U);
}

// Whether a type name is a standard-logic type that a VITAL signal may have
// (std_ulogic and std_logic, their vectors, and the subtypes of
// IEEE 1076.4-2000 9.1). A signal of such a type that a SystemVerilog net
// or variable carries is stored in the four-state domain; its VHDL type, not
// the storage of the other language, decides whether VITAL may use it
// (IEEE 1800-2023 23.11 and IEEE 1076-2019 6.5.6.3 give the port mapping).
[[nodiscard]] inline bool vital_standard_logic_type_name(
    const std::string_view name) noexcept
{
    for (const std::string_view candidate :
        { "std_logic", "std_ulogic", "std_logic_vector", "std_ulogic_vector",
            "x01", "x01z", "ux01", "ux01z" }) {
        if (elaboration_detail::equal_ignoring_ascii_case(name, candidate)) {
            return true;
        }
    }
    return false;
}

// Whether a name is unqualified or selected from `package`, optionally
// through library IEEE.
[[nodiscard]] inline bool has_vital_package_qualification(
    const std::string_view name, const std::string_view package)
{
    const auto member_separator = name.find_last_of('.');
    if (member_separator == std::string_view::npos) {
        return true;
    }
    const auto owner = name.substr(0U, member_separator);
    const auto package_separator = owner.find_last_of('.');
    if (package_separator == std::string_view::npos) {
        return elaboration_detail::equal_ignoring_ascii_case(owner, package);
    }
    return elaboration_detail::equal_ignoring_ascii_case(
               owner.substr(package_separator + 1U), package)
        && elaboration_detail::equal_ignoring_ascii_case(
            owner.substr(0U, package_separator), "ieee");
}

// The simple name of the enumeration literal a name expression denotes.
[[nodiscard]] inline std::optional<std::string_view> vital_literal_name(
    const semantic::vhdl::Expression& expression)
{
    if (expression.kind != semantic::vhdl::ExpressionKind::name) {
        return std::nullopt;
    }
    auto name = std::string_view { expression.text };
    constexpr std::string_view prefix { "@fsim-enum:" };
    if (name.starts_with(prefix)) {
        name.remove_prefix(prefix.size());
    }
    return vital_simple_name(name);
}

// The assertion severity a STD.STANDARD.SEVERITY_LEVEL literal names.
[[nodiscard]] inline std::optional<runtime::simir::AssertionSeverity>
vital_severity(const semantic::vhdl::Expression& expression)
{
    using runtime::simir::AssertionSeverity;
    const auto name = vital_literal_name(expression);
    if (!name) {
        return std::nullopt;
    }
    for (const auto& [literal, severity] :
        { std::pair { "note", AssertionSeverity::note },
            std::pair { "warning", AssertionSeverity::warning },
            std::pair { "error", AssertionSeverity::error },
            std::pair { "failure", AssertionSeverity::failure } }) {
        if (elaboration_detail::equal_ignoring_ascii_case(*name, literal)) {
            return severity;
        }
    }
    return std::nullopt;
}

} // namespace fsim::elaboration::vital_detail
