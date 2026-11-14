// SPDX-License-Identifier: Apache-2.0
#include "virtual_interface_definition.hpp"

namespace fsim::elaboration {

const semantic::sv::Unit* virtual_interface_definition(
    const semantic::CompiledDesign& design, const semantic::UnitId owner,
    const semantic::sv::TypeReference& type)
{
    if (type.interface_type.empty()) {
        return nullptr;
    }
    // The interface a type designates is the one of the library of the unit
    // that writes the type (3.13, 33.3): the same identity elaboration binds
    // the virtual interface variable to, never another library's interface
    // of the same name.
    const auto owner_unit = design.find_unit(owner);
    if (!owner_unit || owner_unit->systemverilog == nullptr) {
        return nullptr;
    }
    const auto& library = owner_unit->systemverilog->library;
    const auto unit = design.find_unit(
        semantic::UnitKind::systemverilog_interface,
        library.empty() ? std::string_view { "work" }
                        : std::string_view { library },
        type.interface_type);
    return unit && unit->systemverilog != nullptr ? unit->systemverilog
                                                  : nullptr;
}

const semantic::sv::Unit* virtual_interface_definition(
    const semantic::CompiledDesign& design, const semantic::ScopeId scope,
    const semantic::sv::TypeReference& type)
{
    const auto* const found = scope.valid()
            && scope.value() < design.semantics.scopes().size()
        ? &design.semantics.scopes()[scope.value()]
        : nullptr;
    return found == nullptr ? nullptr
                            : virtual_interface_definition(
                                  design, found->unit, type);
}

} // namespace fsim::elaboration
