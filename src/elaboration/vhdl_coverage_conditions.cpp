// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/vhdl_coverage_conditions.hpp"

namespace fsim::elaboration {
namespace {

    bool valid_standard(const frontend::VhdlStandard standard) noexcept
    {
        switch (standard) {
        case frontend::VhdlStandard::Vhdl1987:
        case frontend::VhdlStandard::Vhdl1993:
        case frontend::VhdlStandard::Vhdl2000:
        case frontend::VhdlStandard::Vhdl2002:
        case frontend::VhdlStandard::Vhdl2008:
            return true;
        }
        return false;
    }

} // namespace

VhdlCoverageConditionResult discover_vhdl_coverage_conditions(
    const std::span<const frontend::Statement> statements,
    const frontend::Language language,
    const frontend::VhdlStandard standard,
    const std::span<const VhdlCoverageConditionSource> sources,
    const VhdlCoverageConditionLimits limits) noexcept
{
    if (language != frontend::Language::Vhdl2008) {
        VhdlCoverageConditionResult result;
        result.error = VhdlCoverageConditionError::InvalidLanguage;
        return result;
    }
    if (!valid_standard(standard)) {
        VhdlCoverageConditionResult result;
        result.error = VhdlCoverageConditionError::InvalidStandard;
        return result;
    }
    return discover_coverage_conditions(statements,
        frontend::CodeCoverageLanguage::Vhdl, sources, limits);
}

} // namespace fsim::elaboration
