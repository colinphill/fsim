// SPDX-License-Identifier: Apache-2.0
#include "application_design_artifact_codec_validation.hpp"

#include <filesystem>
#include <ranges>
#include <set>

namespace fsim::app::codec_detail {

constexpr std::string_view kCode = "FSIM-ART-0013";

bool portable_semantics(
    const semantic::ModelRecords& records,
    diagnostic::Engine& diagnostics)
{
    for (const auto& file : records.source_files) {
        if (std::filesystem::path(file.physical_name).is_absolute()) {
            diagnostics.error(
                std::string { kCode },
                "semantic state contains a producer-absolute source path: "
                    + file.physical_name);
            return false;
        }
    }
    return true;
}

bool valid_class_state(
    const std::vector<frontend::SystemVerilogClassSpecialization>& classes)
{
    std::set<std::string> identities;
    for (const auto& specialization : classes) {
        if (specialization.declaration_identity.empty()
            || specialization.specialization_identity.empty()
            || !identities.insert(specialization.specialization_identity).second) {
            return false;
        }
        for (const auto& method : specialization.methods) {
            if (method.name.empty() || method.canonical_identity.empty()
                || method.profile_identity.empty()
                || method.lifetime
                    != frontend::SystemVerilogClassLifetime::Automatic) {
                return false;
            }
        }
        std::set<std::string> constraint_identities;
        for (const auto& [identity, enabled] : specialization.constraint_modes) {
            (void)enabled;
            if (identity.empty()
                || !constraint_identities.insert(identity).second) {
                return false;
            }
        }
    }
    return std::ranges::all_of(classes, [&](const auto& specialization) {
        if (!specialization.base_specialization_identity.empty()
            && !identities.contains(
                specialization.base_specialization_identity)) {
            return false;
        }
        std::set<std::string> interfaces;
        return std::ranges::all_of(
            specialization.interface_specialization_identities,
            [&](const std::string& identity) {
                return !identity.empty() && identities.contains(identity)
                    && interfaces.insert(identity).second;
            });
    });
}

bool valid_systemverilog_constraint_classes(
    const std::vector<semantic::sv::ClassDeclaration>& declarations)
{
    std::set<std::string> classes;
    std::set<std::string> constraints;
    for (const auto& declaration : declarations) {
        if (declaration.canonical_identity.empty()
            || !classes.insert(declaration.canonical_identity).second) {
            return false;
        }
        std::set<std::string> properties;
        for (const auto& property : declaration.properties) {
            if (property.canonical_identity.empty()
                || property.owner_identity.empty()
                || !properties.insert(property.canonical_identity).second) {
                return false;
            }
        }
        for (const auto& constraint : declaration.constraints) {
            if (constraint.canonical_identity.empty()
                || constraint.owner_identity.empty()
                || !constraints.insert(constraint.canonical_identity).second) {
                return false;
            }
        }
    }
    for (const auto& declaration : declarations) {
        if (!declaration.base_declaration_identity.empty()
            && !classes.contains(declaration.base_declaration_identity)) {
            return false;
        }
        std::set<std::string> composed_names;
        for (const auto& composed : declaration.composed_constraints) {
            if (composed.name.empty() || composed.selected_identity.empty()
                || !constraints.contains(composed.selected_identity)
                || !composed_names.insert(composed.name).second
                || (!composed.overridden_identity.empty()
                    && !constraints.contains(composed.overridden_identity))) {
                return false;
            }
        }
    }
    return true;
}

} // namespace fsim::app::codec_detail
