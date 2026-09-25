// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/compiled_design.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::diagnostic {
class Engine;
}

namespace fsim::project {
struct Config;
}

namespace fsim::app::application_detail {

struct CompilationWorkspace;

struct ParsedSourceUnitOwner {
    std::string physical_name;
    std::size_t offset { };
    std::filesystem::path source;
};

struct CompiledSourceUnitIdentity {
    semantic::Unit unit;
    bool external { };
    std::filesystem::path source;
};

struct CompiledReferenceIdentity {
    semantic::Unit owner;
    semantic::Unit target;
    semantic::CompiledReference reference;
};

[[nodiscard]] bool compiled_package_has_member(const semantic::CompiledDesign&,
    std::string_view library, std::string_view package, std::string_view member);

// Unit IDs are relocated when compiled inputs are linked. Remember the source
// definitions by identity and recover their final IDs after the merge.
[[nodiscard]] std::vector<CompiledSourceUnitIdentity>
compiled_source_unit_identities(CompilationWorkspace&,
    const project::Config&, std::span<const ParsedSourceUnitOwner>);

[[nodiscard]] bool install_compiled_environment(
    CompilationWorkspace&, const semantic::CompiledDesign&,
    diagnostic::Engine&);

[[nodiscard]] bool identify_compiled_source_units(
    CompilationWorkspace&, std::span<const CompiledSourceUnitIdentity>,
    diagnostic::Engine&);

// Constant folding can erase the name that selected a package declaration.
// Keep its resolved dependency through ID relocation until the workspace
// catalog has recorded the provider used by this compilation.
void remember_compiled_references(const semantic::CompiledDesign&,
    std::vector<CompiledReferenceIdentity>&);

void restore_compiled_references(semantic::CompiledDesign&,
    std::span<const CompiledReferenceIdentity>);

} // namespace fsim::app::application_detail
