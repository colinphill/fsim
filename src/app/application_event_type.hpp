// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/systemverilog_hir.hpp"

#include <cstddef>

namespace fsim::app::application_detail {

// Whether a declared type is `event` after following the typedefs that name
// it (IEEE 1800-2023 6.18, 15.5); the walk ends at a type that names no
// definition.
[[nodiscard]] inline bool type_is_event(const semantic::sv::Hir& hir,
    const semantic::sv::TypeReference& type)
{
    const auto* current = &type;
    for (std::size_t depth = 0U; depth <= hir.types().size(); ++depth) {
        if (current->target.spelling == "event") {
            return true;
        }
        if (!current->target.target.valid()) {
            return false;
        }
        const auto* const definition = hir.find_type(current->target.target);
        if (definition == nullptr || &definition->base == current) {
            return false;
        }
        current = &definition->base;
    }
    return false;
}

} // namespace fsim::app::application_detail
