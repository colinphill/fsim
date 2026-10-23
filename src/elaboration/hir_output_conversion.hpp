// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design_core.hpp"
#include "fsim/runtime/output_format.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/semantic/systemverilog_hir.hpp"

#include <stdexcept>

// Conversions between the frontend, SystemVerilog HIR, and SimIR spellings of
// output formats and assertion severities.
namespace fsim::elaboration {

[[nodiscard]] constexpr semantic::sv::OutputFormat hir_output_format(
    const frontend::OutputFormat format) noexcept
{
    using Source = frontend::OutputFormat;
    using Target = semantic::sv::OutputFormat;
    switch (format) {
    case Source::Binary: return Target::binary;
    case Source::Hexadecimal: return Target::hexadecimal;
    case Source::Octal: return Target::octal;
    case Source::Decimal: return Target::decimal;
    case Source::Character: return Target::character;
    case Source::String: return Target::string;
    case Source::RealScientific: return Target::real_scientific;
    case Source::RealFixed: return Target::real_fixed;
    case Source::RealGeneral: return Target::real_general;
    case Source::Hierarchy: return Target::hierarchy;
    case Source::Time: return Target::time;
    case Source::Unformatted2: return Target::unformatted2;
    case Source::Unformatted4: return Target::unformatted4;
    case Source::Pattern: return Target::pattern;
    case Source::Strength: return Target::strength;
    case Source::Library: return Target::library;
    }
    return Target::decimal;
}

/// SimIR has no hierarchy, pattern, or strength value format: %m prints a
/// string and %v/%z print the value's decimal digits.
[[nodiscard]] constexpr runtime::simir::OutputFormat runtime_output_format(
    const semantic::sv::OutputFormat format) noexcept
{
    using Source = semantic::sv::OutputFormat;
    using Target = runtime::simir::OutputFormat;
    switch (format) {
    case Source::binary: return Target::binary;
    case Source::hexadecimal: return Target::hexadecimal;
    case Source::octal: return Target::octal;
    case Source::decimal: return Target::decimal;
    case Source::character: return Target::character;
    case Source::string:
    case Source::hierarchy:
    case Source::library: return Target::string;
    case Source::real_scientific: return Target::real_scientific;
    case Source::real_fixed: return Target::real_fixed;
    case Source::real_general: return Target::real_general;
    case Source::time: return Target::time;
    case Source::unformatted2: return Target::unformatted2;
    case Source::unformatted4: return Target::unformatted4;
    case Source::pattern:
    case Source::strength: return Target::decimal;
    }
    return Target::decimal;
}

/// The SimIR format of a conversion that consumes a value; %m and %t do not.
using runtime::simir::runtime_output_format;

[[nodiscard]] constexpr semantic::sv::AssertionSeverity hir_assertion_severity(
    const frontend::AssertionSeverity severity) noexcept
{
    switch (severity) {
    case frontend::AssertionSeverity::Note:
        return semantic::sv::AssertionSeverity::note;
    case frontend::AssertionSeverity::Warning:
        return semantic::sv::AssertionSeverity::warning;
    case frontend::AssertionSeverity::Error:
        return semantic::sv::AssertionSeverity::error;
    case frontend::AssertionSeverity::Failure:
        return semantic::sv::AssertionSeverity::failure;
    }
    return semantic::sv::AssertionSeverity::error;
}

[[nodiscard]] constexpr runtime::simir::AssertionSeverity
runtime_assertion_severity(
    const semantic::sv::AssertionSeverity severity) noexcept
{
    switch (severity) {
    case semantic::sv::AssertionSeverity::note:
        return runtime::simir::AssertionSeverity::note;
    case semantic::sv::AssertionSeverity::warning:
        return runtime::simir::AssertionSeverity::warning;
    case semantic::sv::AssertionSeverity::error:
        return runtime::simir::AssertionSeverity::error;
    case semantic::sv::AssertionSeverity::failure:
        return runtime::simir::AssertionSeverity::failure;
    }
    return runtime::simir::AssertionSeverity::error;
}

} // namespace fsim::elaboration
