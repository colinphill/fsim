// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/vhdl_hir.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace fsim::semantic::vhdl {

void Hir::note_mutation() noexcept { ++revision_; }

const std::vector<Unit>& Hir::units() const noexcept { return units_; }
const std::vector<Declaration>& Hir::declarations() const noexcept {
    return declarations_;
}
const std::vector<TypeDefinition>& Hir::types() const noexcept { return types_; }
const std::vector<OverloadSet>& Hir::overload_sets() const noexcept {
    return overload_sets_;
}
const std::vector<Expression>& Hir::expressions() const noexcept {
    return expressions_;
}
const std::vector<Statement>& Hir::statements() const noexcept {
    return statements_;
}
const std::vector<Process>& Hir::processes() const noexcept {
    return processes_;
}
const std::vector<Instance>& Hir::instances() const noexcept {
    return instances_;
}

std::vector<Unit>& Hir::mutable_units() noexcept {
    note_mutation();
    return units_;
}
std::vector<Declaration>& Hir::mutable_declarations() noexcept {
    note_mutation();
    return declarations_;
}
std::vector<TypeDefinition>& Hir::mutable_types() noexcept {
    note_mutation();
    return types_;
}
std::vector<OverloadSet>& Hir::mutable_overload_sets() noexcept {
    note_mutation();
    return overload_sets_;
}
std::vector<Expression>& Hir::mutable_expressions() noexcept {
    note_mutation();
    return expressions_;
}
std::vector<Statement>& Hir::mutable_statements() noexcept {
    note_mutation();
    return statements_;
}
std::vector<Process>& Hir::mutable_processes() noexcept {
    note_mutation();
    return processes_;
}
std::vector<Instance>& Hir::mutable_instances() noexcept {
    note_mutation();
    return instances_;
}

namespace {

std::string canonical_name(std::string value)
{
    if (!value.empty() && value.front() != '\\'
        && value.front() != '\'') {
        std::ranges::transform(value, value.begin(), [](const char byte) {
            return static_cast<char>(
                std::tolower(static_cast<unsigned char>(byte)));
        });
    }
    return value;
}

bool valid_instance_ids(
    const Model& semantics, const std::span<const Instance> instances)
{
    std::set<std::uint32_t> identities;
    return std::ranges::all_of(instances, [&](const Instance& instance) {
        return instance.id.valid()
            && instance.id.value() < semantics.instances().size()
            && identities.insert(instance.id.value()).second;
    });
}

} // namespace

bool top_level_hir_collections_well_formed(
    const Model& semantics, const TopLevelHirView view)
{
    if (!valid_instance_ids(semantics, view.instances)) {
        return false;
    }

    std::map<std::uint32_t, const Declaration*> declarations;
    for (const auto& declaration : view.declarations) {
        if (!declaration.id.valid()
            || declaration.id.value() >= semantics.declarations().size()
            || !declarations.emplace(
                    declaration.id.value(), &declaration).second) {
            return false;
        }
    }

    std::set<std::pair<std::uint32_t, std::string>> identities;
    std::set<std::uint32_t> members;
    for (const auto& overload : view.overload_sets) {
        if (!overload.scope.valid()
            || overload.scope.value() >= semantics.scopes().size()
            || overload.canonical_name.empty()
            || canonical_name(overload.canonical_name)
                != overload.canonical_name
            || overload.declarations.empty()
            || !identities.emplace(
                    overload.scope.value(), overload.canonical_name).second) {
            return false;
        }
        std::set<std::uint32_t> local_members;
        for (const auto member : overload.declarations) {
            const auto declaration = member.valid()
                ? declarations.find(member.value())
                : declarations.end();
            if (declaration == declarations.end()
                || declaration->second->scope != overload.scope
                || canonical_name(declaration->second->name)
                    != overload.canonical_name
                || !local_members.insert(member.value()).second
                || !members.insert(member.value()).second) {
                return false;
            }
        }
    }
    return true;
}

} // namespace fsim::semantic::vhdl
