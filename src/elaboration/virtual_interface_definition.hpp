// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/compiled_design.hpp"
#include "fsim/semantic/systemverilog_hir.hpp"

#include <string>

namespace fsim::elaboration {

/// The interface an elaborated interface instance is an instance of: the
/// definition's identity (compared wherever a virtual interface is matched to
/// instances) and its name (for diagnostics only).
struct SystemVerilogInterfaceInstanceType {
    std::string name;
    semantic::UnitId unit;
};

/// The interface definition a virtual interface type written in unit `owner`
/// designates (IEEE 1800-2023 25.9, 3.13, 33): the interface of that name in
/// the library of the writing unit, the identity elaboration binds the
/// virtual interface variable to. Null when the type is not a virtual
/// interface or no such interface is defined.
[[nodiscard]] const semantic::sv::Unit* virtual_interface_definition(
    const semantic::CompiledDesign& design, semantic::UnitId owner,
    const semantic::sv::TypeReference& type);

/// The same for a type written in scope `scope` (the unit that owns the
/// scope is the writing unit).
[[nodiscard]] const semantic::sv::Unit* virtual_interface_definition(
    const semantic::CompiledDesign& design, semantic::ScopeId scope,
    const semantic::sv::TypeReference& type);

} // namespace fsim::elaboration
