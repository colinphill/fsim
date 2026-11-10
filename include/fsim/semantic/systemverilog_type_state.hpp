// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/systemverilog_hir.hpp"

#include <algorithm>
#include <vector>

namespace fsim::semantic::sv {

/// Whether a type holds 4-state values: its own flag, or for a named type
/// that of the type the name selects, so `typedef logic [3:0] t`, an
/// enumeration over `logic` and a packed structure with a 4-state member are
/// 4-state (IEEE 1800-2023 6.11, 6.18, 6.19, 7.2.1) and their variables
/// start at X (6.8 Table 6-7). A type parameter is what each specialization
/// binds it to, which the binding's own reference carries; class handles,
/// strings and unpacked aggregates are not 4-state vectors. `find_type`
/// maps a TypeId to its TypeDefinition (or nullptr).
template <typename FindType>
[[nodiscard]] bool named_type_four_state(
    const TypeReference& type, const FindType& find_type)
{
    std::vector<TypeId> visiting;
    const auto resolve = [&](const auto& self,
                             const TypeReference& reference) -> bool {
        if (reference.four_state) {
            return true;
        }
        if (!reference.class_identity.empty() || reference.virtual_interface
            || reference.value_form == TypeForm::class_handle
            || reference.value_form == TypeForm::string
            || !reference.target.target.valid()
            || std::ranges::find(visiting, reference.target.target)
                != visiting.end()) {
            return false;
        }
        const TypeDefinition* definition = find_type(reference.target.target);
        if (definition == nullptr || definition->form == TypeForm::type_parameter
            || definition->form == TypeForm::class_handle
            || definition->form == TypeForm::string
            || definition->form == TypeForm::unpacked_structure
            || definition->form == TypeForm::unpacked_union) {
            return false;
        }
        visiting.push_back(reference.target.target);
        const bool result = self(self, definition->base)
            || std::ranges::any_of(definition->members,
                [&](const PackedMember& member) {
                    return self(self, member.type);
                });
        visiting.pop_back();
        return result;
    };
    return resolve(resolve, type);
}

[[nodiscard]] inline bool named_type_four_state(
    const TypeReference& type, const Hir& hir)
{
    return named_type_four_state(type,
        [&hir](const TypeId id) { return hir.find_type(id); });
}

} // namespace fsim::semantic::sv
