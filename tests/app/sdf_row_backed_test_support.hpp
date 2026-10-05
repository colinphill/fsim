// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../../src/elaboration/elaborated_design_process_access.hpp"

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fsim::app::sdf_row_backed_test {

using ProcessTable
    = elaboration::detail::RuntimeProcessProgramTable;
using ProcessTableAccess
    = elaboration::detail::ElaboratedDesignProcessAccess;

struct ProcessRowIdentity {
    std::shared_ptr<const ProcessTable> owner;
    std::vector<const void*> common_programs;
};

inline elaboration::ElaboratedDesign make_row_backed_design(
    elaboration::ElaboratedDesignState state)
{
    elaboration::detail::RuntimeProcessProgramTableBuilder builder;
    for (auto& process : state.processes)
        builder.append(std::move(process));
    state.processes.clear();
    auto rows = std::move(builder).freeze();
    auto design = ProcessTableAccess::from_state(
        std::move(state), std::move(rows), false);
    if (!design)
        throw std::logic_error("SDF row-backed fixture is invalid");
    return std::move(*design);
}

inline ProcessRowIdentity capture_process_row_identity(
    const elaboration::ElaboratedDesign& design)
{
    ProcessRowIdentity result;
    result.owner = ProcessTableAccess::process_table(design);
    result.common_programs.reserve(ProcessTableAccess::process_count(design));
    for (std::size_t index = 0;
        index < ProcessTableAccess::process_count(design); ++index) {
        const auto process = ProcessTableAccess::process_view(design, index);
        result.common_programs.push_back(process.common_identity());
    }
    return result;
}

inline bool same_process_row_identity(
    const elaboration::ElaboratedDesign& design,
    const ProcessRowIdentity& original)
{
    if (!ProcessTableAccess::row_backed(design)
        || ProcessTableAccess::process_table(design) != original.owner
        || ProcessTableAccess::process_count(design)
            != original.common_programs.size()) {
        return false;
    }
    for (std::size_t index = 0;
        index < original.common_programs.size(); ++index) {
        const auto process = ProcessTableAccess::process_view(design, index);
        if (!process.valid()
            || process.common_identity() != original.common_programs[index]) {
            return false;
        }
    }
    return true;
}

} // namespace fsim::app::sdf_row_backed_test
