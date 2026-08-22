// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/verilog_coverage_conditions.hpp"

namespace fsim::elaboration {

VerilogCoverageConditionResult discover_verilog_coverage_conditions(
    const std::span<const frontend::Statement> statements,
    const frontend::Language language,
    const std::span<const VerilogCoverageConditionSource> sources,
    const VerilogCoverageConditionLimits limits) noexcept
{
    frontend::CodeCoverageLanguage point_language;
    switch (language) {
    case frontend::Language::Verilog2005:
        point_language = frontend::CodeCoverageLanguage::Verilog;
        break;
    case frontend::Language::SystemVerilog2017:
        point_language = frontend::CodeCoverageLanguage::SystemVerilog;
        break;
    case frontend::Language::Vhdl2008: {
        VerilogCoverageConditionResult result;
        result.error = VerilogCoverageConditionError::InvalidLanguage;
        return result;
    }
    default: {
        VerilogCoverageConditionResult result;
        result.error = VerilogCoverageConditionError::InvalidLanguage;
        return result;
    }
    }
    return discover_coverage_conditions(
        statements, point_language, sources, limits);
}

} // namespace fsim::elaboration
