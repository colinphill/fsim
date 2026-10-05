// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../elaboration/elaborated_design_process_access.hpp"

#include <memory>
#include <stdexcept>

namespace fsim::app::sdf_detail {

[[nodiscard]] inline std::shared_ptr<const elaboration::detail::RuntimeProcessProgramTable>
retain_published_process_rows(
    const elaboration::ElaboratedDesign& elaborated)
{
    auto rows
        = elaboration::detail::ElaboratedDesignProcessAccess::process_table(
            elaborated);
    if (elaboration::detail::ElaboratedDesignProcessAccess::row_backed(
            elaborated)
        && !rows) {
        throw std::logic_error(
            "fresh elaboration process rows are not yet published");
    }
    return rows;
}

} // namespace fsim::app::sdf_detail
