// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/parser.hpp"
#include "fsim/semantic/compiled_design.hpp"

#include <filesystem>
#include <optional>
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

struct VhdlUnitIdentifierForms {
    bool available { };
    bool name_extended { };
    bool primary_name_extended { };
};

struct CompiledSourceUnitIdentity {
    semantic::Unit unit;
    bool external { };
    std::filesystem::path source;
    VhdlUnitIdentifierForms vhdl_forms;
};

struct CompiledReferenceIdentity {
    semantic::Unit owner;
    semantic::Unit target;
    semantic::CompiledReference reference;
    VhdlUnitIdentifierForms owner_vhdl_forms;
    VhdlUnitIdentifierForms target_vhdl_forms;
};

// What `member` of a compiled package names (IEEE 1800-2023 26.3, 26.6), as
// frontend::SystemVerilogPackageMembers answers it for a parsed package.
[[nodiscard]] frontend::SystemVerilogPackageMember compiled_package_member(
    const semantic::CompiledDesign&, std::string_view library,
    std::string_view package, std::string_view member);

// The sequence and property declarations of a compiled package (IEEE
// 1800-2023 26.3, 16.8, 16.12) as their source tokens, for the importing
// parser to parse again; none for a package the design does not hold. The
// source files those tokens come from are added to `files`, for the
// importing model to record them as the library does.
[[nodiscard]] std::optional<frontend::SystemVerilogPackageAssertions>
compiled_package_assertions(const semantic::CompiledDesign&,
    std::string_view library, std::string_view package,
    std::vector<semantic::SourceFile>& files);

// A compiled source span and token as the frontend reports them.
[[nodiscard]] frontend::SourceSpan frontend_source_span(
    const semantic::Model&, semantic::SourceSpanId);
[[nodiscard]] frontend::Token frontend_source_token(
    const semantic::Model&, const semantic::sv::SourceToken&);

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
