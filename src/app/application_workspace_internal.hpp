// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "application_workspace.hpp"
#include "application_workspace_objects.hpp"
#include "application_workspace_store.hpp"
#include "fsim/support/vhdl_identifier.hpp"

#include <span>

namespace fsim::app::application_detail {

using WorkspaceCatalogs = std::vector<workspace::LibraryCatalog>;

/// A unit name compares as a VHDL identifier in VHDL and exactly otherwise.
[[nodiscard]] inline bool workspace_name_equal(const std::string_view left,
    const std::string_view right, const std::string_view language)
{
    return language == "vhdl" ? support::same_vhdl_identifier(left, right)
                              : left == right;
}

[[nodiscard]] inline library::VhdlIdentifierForm vhdl_identifier_form(
    const bool extended) noexcept
{
    return extended ? library::VhdlIdentifierForm::extended
                    : library::VhdlIdentifierForm::basic;
}

/// Whether a decoded unit name in `unit_form` is `expected_name` in
/// `expected_form`.
[[nodiscard]] inline bool vhdl_identifier_matches(
    const std::string_view unit_name,
    const library::VhdlIdentifierForm unit_form,
    const std::string_view expected_name,
    const library::VhdlIdentifierForm expected_form)
{
    return expected_form != library::VhdlIdentifierForm::unknown
        && unit_form == expected_form
        && support::same_vhdl_identifier(unit_name,
            unit_form == library::VhdlIdentifierForm::extended, expected_name,
            expected_form == library::VhdlIdentifierForm::extended);
}

bool workspace_error(diagnostic::Engine&, std::string);
[[nodiscard]] std::optional<WorkspaceCatalogs> workspace_catalogs(
    const workspace::Store&, const workspace::LibraryCatalog* target,
    diagnostic::Engine&);
[[nodiscard]] std::vector<WorkspaceObjectSelection> workspace_object_selections(
    const workspace::Store&, const WorkspaceCatalogs&,
    std::string_view replacement_library, std::span<const std::string> replaced_sources,
    diagnostic::Engine&);
bool validate_workspace_dependencies(const WorkspaceCatalogs&, diagnostic::Engine&);
[[nodiscard]] std::vector<workspace::UnitDependency> workspace_dependencies(
    const semantic::CompiledDesign&, std::span<const semantic::UnitId>,
    const WorkspaceCatalogs&);
bool resolve_workspace_tops(project::Config&, const WorkspaceCatalogs&,
    diagnostic::Engine&);

// Shared phase implementation preserves the AOT receipt path after managed
// object selection and transactional snapshot publication.
int elaborate_built_workspace(const cli::Invocation&, const project::Config&,
    std::optional<BuiltProject>, diagnostic::Engine&, std::ostream&);

} // namespace fsim::app::application_detail
