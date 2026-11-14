// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/vhdl_hir.hpp"

#include <optional>

namespace fsim::semantic::vhdl {

/// The implicit signal GUARD of a block statement with a guard condition
/// (IEEE 1076-2019 11.2), or nothing for any other region. The HIR builder
/// declares it in the block's scope right after the block's generics and
/// ports, so it is the block's first signal declaration that is not a port;
/// signals the block's declarative part declares follow it. `find` maps a
/// declaration id to its record (nullptr when there is none).
template <class Find>
[[nodiscard]] std::optional<DeclarationId> block_guard_declaration(
    const GenerateRegion& region, Find&& find)
{
    if (region.kind != GenerateKind::block || !region.condition) {
        return std::nullopt;
    }
    for (const auto id : region.declarations) {
        const Declaration* const declaration = find(id);
        if (declaration != nullptr
            && declaration->form == DeclarationForm::signal
            && declaration->direction == Direction::unknown
            && !declaration->linkage) {
            return id;
        }
    }
    return std::nullopt;
}

} // namespace fsim::semantic::vhdl
