// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/systemverilog_hir.hpp"

#include <algorithm>
#include <set>
#include <tuple>
#include <unordered_set>

namespace fsim::semantic::sv {
namespace {

bool udp_language_revision_matches(
    const Language language, const std::string_view standard) noexcept
{
    if (language == Language::verilog) {
        return standard == "1995" || standard == "verilog-1995"
            || standard == "2001" || standard == "verilog-2001"
            || standard == "2001-noconfig"
            || standard == "verilog-2001-noconfig"
            || standard == "2005" || standard == "verilog-2005";
    }
    return language == Language::system_verilog
        && (standard == "2005" || standard == "systemverilog-2005"
            || standard == "2009" || standard == "systemverilog-2009"
            || standard == "2012" || standard == "systemverilog-2012"
            || standard == "2017" || standard == "systemverilog-2017"
            || standard == "2023" || standard == "systemverilog-2023");
}

bool valid_udp_level(const UdpLevel value) noexcept
{
    switch (value) {
    case UdpLevel::zero:
    case UdpLevel::one:
    case UdpLevel::unknown:
    case UdpLevel::dont_care:
    case UdpLevel::binary:
        return true;
    }
    return false;
}

bool valid_udp_edge(const UdpEdge value) noexcept
{
    switch (value) {
    case UdpEdge::none:
    case UdpEdge::rising:
    case UdpEdge::falling:
    case UdpEdge::positive:
    case UdpEdge::negative:
    case UdpEdge::any:
    case UdpEdge::explicit_edge:
        return true;
    }
    return false;
}

bool valid_udp_output(const UdpOutput value) noexcept
{
    switch (value) {
    case UdpOutput::zero:
    case UdpOutput::one:
    case UdpOutput::unknown:
    case UdpOutput::no_change:
        return true;
    }
    return false;
}

} // namespace

bool udp_table_within_resource_budget(
    const std::size_t input_count, const std::size_t row_count) noexcept
{
    if (input_count
        > (maximum_udp_table_storage_bytes - sizeof(UdpTableRow))
            / sizeof(UdpInputPattern)) {
        return false;
    }
    const auto row_bytes
        = sizeof(UdpTableRow) + input_count * sizeof(UdpInputPattern);
    return row_bytes != 0
        && row_count <= maximum_udp_table_storage_bytes / row_bytes;
}

bool udp_declaration_well_formed(
    const UdpDeclaration& declaration) noexcept
{
    if (!udp_language_revision_matches(
            declaration.language, declaration.standard)
        || declaration.compatibility_profile.empty()
        || declaration.name.empty() || declaration.output.empty()
        || declaration.inputs.empty() || declaration.rows.empty()
        || !declaration.source.valid() || !declaration.origin.valid()
        || declaration.sequential != declaration.output_register
        || !udp_table_within_resource_budget(
            declaration.inputs.size(), declaration.rows.size())) {
        return false;
    }
    if (declaration.initial_output
        && (!declaration.sequential
            || !valid_udp_output(*declaration.initial_output)
            || *declaration.initial_output == UdpOutput::no_change)) {
        return false;
    }
    std::unordered_set<std::string> terminals;
    terminals.insert(declaration.output);
    for (const auto& input : declaration.inputs) {
        if (input.empty() || !terminals.insert(input).second) {
            return false;
        }
    }
    const auto same_pattern = [](const UdpInputPattern& left,
                                  const UdpInputPattern& right) {
        return left.level == right.level && left.edge == right.edge
            && left.previous == right.previous
            && left.current == right.current;
    };
    for (std::size_t index = 0; index < declaration.rows.size(); ++index) {
        const auto& row = declaration.rows[index];
        if (row.inputs.size() != declaration.inputs.size()
            || !row.source.valid()
            || row.current_state.has_value() != declaration.sequential
            || (row.current_state && !valid_udp_level(*row.current_state))
            || !valid_udp_output(row.output)
            || (!declaration.sequential
                && row.output == UdpOutput::no_change)) {
            return false;
        }
        const auto edge_count = std::ranges::count_if(
            row.inputs, [](const UdpInputPattern& input) {
                return input.edge != UdpEdge::none;
            });
        if (std::ranges::any_of(
                row.inputs, [](const UdpInputPattern& input) {
                    return !input.source.valid()
                        || !valid_udp_level(input.level)
                        || !valid_udp_edge(input.edge)
                        || !valid_udp_level(input.previous)
                        || !valid_udp_level(input.current);
                })) {
            return false;
        }
        if ((!declaration.sequential && edge_count != 0)
            || (declaration.sequential && edge_count > 1)) {
            return false;
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            const auto& earlier = declaration.rows[prior];
            if (row.current_state == earlier.current_state
                && row.inputs.size() == earlier.inputs.size()
                && std::ranges::equal(
                    row.inputs, earlier.inputs, same_pattern)) {
                return false;
            }
        }
    }
    return true;
}

namespace {

bool valid_record_origin(const Model& semantics,
    const SourceSpanId source, const OriginId origin)
{
    return source.valid() && source.value() < semantics.source_spans().size()
        && origin.valid() && origin.value() < semantics.origins().size()
        && semantics.origins()[origin.value()].source == source;
}

bool valid_source(const Model& semantics, const SourceSpanId source)
{
    return source.valid() && source.value() < semantics.source_spans().size();
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

bool valid_dpi_enums(const DpiDeclaration& declaration)
{
    const auto valid_direction = [&] {
        switch (declaration.direction) {
        case DpiDirection::import:
        case DpiDirection::export_declaration:
            return true;
        }
        return false;
    }();
    const auto valid_owner = [&] {
        switch (declaration.owner_kind) {
        case DpiOwnerKind::compilation_unit:
        case DpiOwnerKind::design_unit:
            return true;
        }
        return false;
    }();
    const auto valid_qualifier = [&] {
        switch (declaration.qualifier) {
        case DpiQualifier::none:
        case DpiQualifier::pure:
        case DpiQualifier::context:
            return true;
        }
        return false;
    }();
    const auto valid_callable = [&] {
        switch (declaration.callable_kind) {
        case DpiCallableKind::function:
        case DpiCallableKind::task:
            return true;
        }
        return false;
    }();
    return valid_direction && valid_owner && valid_qualifier
        && valid_callable;
}

const fsim::semantic::Unit* systemverilog_owner(
    const Model& semantics, const ScopeId scope)
{
    if (!scope.valid() || scope.value() >= semantics.scopes().size()) {
        return nullptr;
    }
    const auto unit = semantics.scopes()[scope.value()].unit;
    if (!unit.valid() || unit.value() >= semantics.units().size()) {
        return nullptr;
    }
    const auto& owner = semantics.units()[unit.value()];
    return owner.language == Language::system_verilog ? &owner : nullptr;
}

bool dpi_owner_kind_matches(
    const fsim::semantic::Unit& owner, const DpiOwnerKind owner_kind)
{
    const auto compilation_unit
        = owner.kind
        == fsim::semantic::UnitKind::systemverilog_compilation_unit;
    return owner_kind == DpiOwnerKind::compilation_unit
        ? compilation_unit
        : !compilation_unit;
}

} // namespace

bool top_level_hir_collections_well_formed(
    const Model& semantics, const TopLevelHirView view)
{
    if (!valid_instance_ids(semantics, view.instances)) {
        return false;
    }

    std::set<std::pair<std::string, std::string>> udp_identities;
    for (const auto& declaration : view.udps) {
        const auto library = declaration.library.empty()
            ? std::string { "work" }
            : declaration.library;
        if (!udp_declaration_well_formed(declaration)
            || !valid_record_origin(
                semantics, declaration.source, declaration.origin)
            || !udp_identities.emplace(library, declaration.name).second) {
            return false;
        }
    }

    using DpiIdentity = std::tuple<DpiOwnerKind, ScopeId, std::string>;
    std::set<DpiIdentity> dpi_identities;
    std::set<std::string> export_linkages;
    std::set<std::uint32_t> dpi_origins;
    std::set<std::uint32_t> dpi_profile_sources;
    for (const auto& declaration : view.dpi_declarations) {
        const auto* owner = systemverilog_owner(
            semantics, declaration.owner_scope);
        if (!owner || !valid_dpi_enums(declaration)
            || !dpi_owner_kind_matches(*owner, declaration.owner_kind)
            || declaration.standard.empty()
            || declaration.owner_identity.empty()
            || declaration.link_name.empty()
            || declaration.systemverilog_name.empty()
            || declaration.linkage_name.empty()
            || (declaration.c_identifier
                && declaration.c_identifier->empty())
            || !valid_record_origin(
                semantics, declaration.source, declaration.origin)
            || !valid_source(semantics, declaration.profile_source)
            || !dpi_profile_sources.insert(
                declaration.profile_source.value()).second
            || !dpi_origins.insert(declaration.origin.value()).second
            || !dpi_identities.emplace(declaration.owner_kind,
                    declaration.owner_scope,
                    declaration.systemverilog_name).second
            || (declaration.direction == DpiDirection::export_declaration
                && !export_linkages.insert(
                    declaration.linkage_name).second)) {
            return false;
        }
    }

    std::set<std::pair<ScopeId, std::string>>
        covergroup_runtime_identities;
    for (const auto& instance : view.covergroup_instances) {
        if (!systemverilog_owner(semantics, instance.owner_scope)
            || instance.name.empty() || instance.owner_identity.empty()
            || instance.declaration_identity.empty()
            || instance.runtime_identity.empty()
            || !valid_record_origin(
                semantics, instance.source, instance.origin)
            || !covergroup_runtime_identities.emplace(
                instance.owner_scope, instance.runtime_identity).second) {
            return false;
        }
    }
    return true;
}

std::string class_declaration_identity(
    const std::string_view canonical_identity,
    const std::string_view alternative_discriminator)
{
    if (alternative_discriminator.empty())
        return std::string { canonical_identity };
    return std::string { canonical_identity } + "@alternative["
        + std::string { alternative_discriminator } + "]";
}

std::string class_declaration_identity(
    const ClassDeclaration& declaration)
{
    return class_declaration_identity(declaration.canonical_identity,
        declaration.alternative_discriminator);
}

namespace {

const Declaration* declaration_for(
    const std::span<const Declaration> declarations,
    const DeclarationId id)
{
    const auto found = std::ranges::find(
        declarations, id, &Declaration::id);
    return found == declarations.end() ? nullptr : &*found;
}

const Statement* statement_for(
    const std::span<const Statement> statements,
    const StatementId id)
{
    const auto found = std::ranges::find(statements, id, &Statement::id);
    return found == statements.end() ? nullptr : &*found;
}

bool scope_within(
    const Model& semantics, ScopeId scope, const ScopeId owner)
{
    std::set<ScopeId> visited;
    while (scope.valid() && scope.value() < semantics.scopes().size()
        && visited.insert(scope).second) {
        if (scope == owner)
            return true;
        const auto parent = semantics.scopes()[scope.value()].parent;
        if (!parent)
            return false;
        scope = *parent;
    }
    return false;
}

bool valid_source_origin(const Model& semantics, const SourceSpanId source,
    const OriginId origin)
{
    return source.valid() && source.value() < semantics.source_spans().size()
        && origin.valid() && origin.value() < semantics.origins().size()
        && semantics.origins()[origin.value()].source == source;
}

bool valid_actual_association(
    const Model& semantics, const ActualAssociation& actual)
{
    if (!actual.source.valid()
        || actual.source.value() >= semantics.source_spans().size()) {
        return false;
    }
    switch (actual.kind) {
    case ActualKind::expression:
        return actual.expression.has_value() && !actual.type;
    case ActualKind::type:
        return actual.type.has_value() && !actual.expression;
    case ActualKind::default_value:
    case ActualKind::open:
        return !actual.expression && !actual.type;
    }
    return false;
}

struct ClassClosure {
    const Model& semantics;
    std::span<const Declaration> declarations;
    std::span<const Statement> statements;
    std::set<DeclarationId> active_declarations;
    std::set<DeclarationId> claimed_declarations;
    std::set<StatementId> active_statements;
    std::set<StatementId> claimed_statements;
    std::set<ScopeId> class_scopes;
    std::set<ScopeId> callable_scopes;
};

bool valid_declaration_closure(ClassClosure& closure,
    DeclarationId id, ScopeId expected_scope,
    OriginId expected_parent);

bool valid_statement_closure(ClassClosure& closure,
    const StatementId id, const ScopeId expected_scope,
    const OriginId expected_parent)
{
    if (closure.claimed_statements.contains(id))
        return false;
    if (!closure.active_statements.insert(id).second)
        return false;
    const auto* statement = statement_for(closure.statements, id);
    if (statement == nullptr || statement->scope != expected_scope
        || !valid_source_origin(
            closure.semantics, statement->source, statement->origin)
        || closure.semantics.origins()[statement->origin.value()].parent
            != std::optional<OriginId> { expected_parent }
        || id.value()
            >= closure.semantics.statement_identities().size()) {
        return false;
    }
    const auto& identity
        = closure.semantics.statement_identities()[id.value()];
    if (identity.id != id || identity.scope != statement->scope
        || identity.source != statement->source
        || identity.origin != statement->origin) {
        return false;
    }
    auto child_scope = statement->scope;
    if (statement->nested_scope
        && (!statement->nested_scope->valid()
            || statement->nested_scope->value()
                >= closure.semantics.scopes().size())) {
        return false;
    }
    if (statement->nested_scope) {
        const auto& nested
            = closure.semantics.scopes()[statement->nested_scope->value()];
        if (nested.id != *statement->nested_scope
            || nested.parent
                != std::optional<ScopeId> { statement->scope }
            || nested.source != statement->source
            || nested.origin != statement->origin
            || closure.semantics.scopes()[statement->scope.value()].unit
                != nested.unit) {
            return false;
        }
        child_scope = *statement->nested_scope;
    } else if (!statement->declarations.empty()) {
        return false;
    }
    std::set<DeclarationId> local_declarations;
    for (const auto declaration : statement->declarations) {
        if (!local_declarations.insert(declaration).second
            || !valid_declaration_closure(closure, declaration,
                child_scope, statement->origin)) {
            return false;
        }
    }
    const auto validate = [&](const StatementId child) {
        return valid_statement_closure(
            closure, child, child_scope, statement->origin);
    };
    if (!std::ranges::all_of(statement->statements, validate)
        || !std::ranges::all_of(statement->else_statements, validate)
        || !std::ranges::all_of(statement->loop_updates, validate)) {
        return false;
    }
    for (const auto& alternative : statement->case_alternatives) {
        if (!std::ranges::all_of(alternative.statements, validate))
            return false;
    }
    closure.active_statements.erase(id);
    closure.claimed_statements.insert(id);
    return true;
}

bool valid_declaration_closure(ClassClosure& closure,
    const DeclarationId id, const ScopeId expected_scope,
    const OriginId expected_parent)
{
    if (closure.claimed_declarations.contains(id)) {
        const auto* declaration = declaration_for(
            closure.declarations, id);
        return declaration != nullptr
            && declaration->scope == expected_scope
            && declaration->origin.valid()
            && declaration->origin.value()
                < closure.semantics.origins().size()
            && closure.semantics.origins()[declaration->origin.value()].parent
                == std::optional<OriginId> { expected_parent };
    }
    if (!closure.active_declarations.insert(id).second)
        return false;
    const auto* declaration = declaration_for(closure.declarations, id);
    if (declaration == nullptr || declaration->scope != expected_scope
        || !valid_source_origin(closure.semantics,
            declaration->source, declaration->origin)
        || closure.semantics.origins()[declaration->origin.value()].parent
            != std::optional<OriginId> { expected_parent }
        || id.value() >= closure.semantics.declarations().size()) {
        return false;
    }
    const auto& identity = closure.semantics.declarations()[id.value()];
    if (identity.id != id || identity.scope != declaration->scope
        || identity.source != declaration->source
        || identity.origin != declaration->origin) {
        return false;
    }

    auto child_scope = declaration->scope;
    if (declaration->callable) {
        if (!declaration->nested_scope
            || !declaration->nested_scope->valid()
            || declaration->nested_scope->value()
                >= closure.semantics.scopes().size()) {
            return false;
        }
        const auto& nested
            = closure.semantics.scopes()[declaration->nested_scope->value()];
        if (nested.id != *declaration->nested_scope
            || nested.parent
                != std::optional<ScopeId> { declaration->scope }
            || nested.source != declaration->source
            || nested.origin != declaration->origin
            || closure.semantics.scopes()[declaration->scope.value()].unit
                != nested.unit) {
            return false;
        }
        child_scope = *declaration->nested_scope;
        closure.callable_scopes.insert(child_scope);
    } else if (declaration->nested_scope
        || !declaration->statements.empty()) {
        return false;
    }

    if (declaration->callable) {
        std::set<DeclarationId> formals;
        for (const auto formal : declaration->callable->formals) {
            if (!formals.insert(formal).second
                || std::ranges::find(declaration->children, formal)
                    == declaration->children.end()
                || !valid_declaration_closure(closure, formal,
                    child_scope, declaration->origin)) {
                return false;
            }
        }
    }
    std::set<DeclarationId> children;
    for (const auto child : declaration->children) {
        if (!children.insert(child).second
            || !valid_declaration_closure(closure, child,
                child_scope, declaration->origin)) {
            return false;
        }
    }
    std::set<StatementId> statements;
    for (const auto statement : declaration->statements) {
        if (!statements.insert(statement).second
            || !valid_statement_closure(closure, statement,
                child_scope, declaration->origin)) {
            return false;
        }
    }
    closure.active_declarations.erase(id);
    closure.claimed_declarations.insert(id);
    return true;
}

std::string generate_class_ownership_error(const Model& semantics,
    const std::span<const Unit> units,
    const std::span<const Declaration> declarations,
    const std::span<const ClassDeclaration> classes)
{
    std::set<std::string> owned;
    std::set<std::string> alternative_discriminators;
    const auto class_for = [&](const std::string_view identity) {
        const auto found = std::ranges::find_if(
            classes, [&](const ClassDeclaration& declaration) {
                return class_declaration_identity(declaration) == identity;
            });
        return found == classes.end() ? nullptr : &*found;
    };
    const auto validate = [&](const auto& self,
                              const GenerateRegion& region) -> std::string {
        const auto* owner = declaration_for(
            declarations, region.declaration);
        if (owner == nullptr || owner->form != DeclarationForm::generated) {
            return "generate region has no generated declaration owner";
        }
        if (!region.scope.valid()
            || region.scope.value() >= semantics.scopes().size()) {
            return "generate region has an invalid semantic scope";
        }
        if (!region.alternative_discriminator.empty()) {
            alternative_discriminators.insert(
                region.alternative_discriminator);
        }
        std::set<std::string> local;
        for (const auto& identity : region.class_declarations) {
            const auto* declaration = class_for(identity);
            if (declaration == nullptr)
                return "generate region names an unknown class '"
                    + identity + "'";
            if (declaration->generate_owner != region.declaration)
                return "generated class '" + identity
                    + "' names a different generate owner";
            if (declaration->alternative_discriminator
                != region.alternative_discriminator) {
                return "generated class '" + identity
                    + "' has a mismatched alternative discriminator";
            }
            if (!scope_within(
                    semantics, declaration->scope, region.scope)) {
                return "generated class '" + identity
                    + "' is outside its generate scope";
            }
            if (!local.insert(identity).second)
                return "generate region repeats class '" + identity + "'";
            if (!owned.insert(identity).second)
                return "generated class '" + identity
                    + "' is owned by multiple generate regions";
        }
        std::set<std::string> alternatives;
        for (const auto& alternative : region.alternatives) {
            if (alternative.alternative_discriminator.empty())
                return "generate alternative has no discriminator";
            if (!alternatives.insert(
                    alternative.alternative_discriminator).second) {
                return "generate region repeats alternative discriminator '"
                    + alternative.alternative_discriminator + "'";
            }
            if (!alternative.scope.valid()
                || alternative.scope.value() >= semantics.scopes().size()) {
                return "generate alternative has an invalid semantic scope";
            }
            const auto matching_nested = std::ranges::count_if(
                region.nested, [&](const GenerateRegion& nested) {
                    return nested.scope == alternative.scope
                        && nested.alternative_discriminator
                            == alternative.alternative_discriminator
                        && nested.class_declarations
                            == alternative.class_declarations;
                });
            if (region.kind != GenerateKind::selection
                || matching_nested != 1) {
                return "generate alternative does not have one matching "
                    "nested selection region";
            }
            alternative_discriminators.insert(
                alternative.alternative_discriminator);
            std::set<std::string> alternative_classes;
            for (const auto& identity : alternative.class_declarations) {
                const auto* declaration = class_for(identity);
                if (declaration == nullptr)
                    return "generate alternative names an unknown class '"
                        + identity + "'";
                if (declaration->alternative_discriminator
                    != alternative.alternative_discriminator) {
                    return "generate alternative class '" + identity
                        + "' has a mismatched discriminator";
                }
                if (!scope_within(
                        semantics, declaration->scope, alternative.scope)) {
                    return "generate alternative class '" + identity
                        + "' is outside its alternative scope";
                }
                if (!alternative_classes.insert(identity).second) {
                    return "generate alternative repeats class '"
                        + identity + "'";
                }
            }
        }
        for (const auto& child : region.nested) {
            if (auto error = self(self, child); !error.empty())
                return error;
        }
        return { };
    };
    for (const auto& unit : units) {
        for (const auto& region : unit.generates) {
            if (auto error = validate(validate, region); !error.empty()) {
                return error;
            }
        }
    }
    for (const auto& declaration : classes) {
        const auto identity = class_declaration_identity(declaration);
        if (!declaration.generate_owner) {
            if (!declaration.alternative_discriminator.empty()) {
                return "ordinary class '" + identity
                    + "' has a generate alternative discriminator";
            }
            continue;
        }
        if (!owned.contains(identity))
            return "generated class '" + identity
                + "' is not owned by a retained generate region";
        if (!declaration.alternative_discriminator.empty()
            && !alternative_discriminators.contains(
                declaration.alternative_discriminator)) {
            return "generated class '" + identity
                + "' names an unknown alternative discriminator";
        }
    }
    return { };
}

} // namespace

std::string class_hir_error(const Model& semantics,
    const std::span<const Unit> units,
    const std::span<const Declaration> declarations,
    const std::span<const Statement> statements,
    const std::span<const ClassDeclaration> classes)
{
    for (const auto& declaration : declarations) {
        if ((declaration.const_reference || declaration.static_reference)
            && (declaration.form != DeclarationForm::port
                || declaration.direction != Direction::ref)) {
            return "SystemVerilog reference qualifier is attached to a "
                   "non-reference formal";
        }
    }
    std::set<std::string> class_identities;
    std::set<std::string> constraint_identities;
    std::set<ScopeId> class_scopes;
    ClassClosure closure {
        semantics, declarations, statements, { }, { }, { }, { }, { }, { }
    };
    const auto class_for = [&](const std::string_view identity) {
        const auto found = std::ranges::find_if(
            classes, [&](const ClassDeclaration& declaration) {
                return class_declaration_identity(declaration) == identity;
            });
        return found == classes.end() ? nullptr : &*found;
    };
    for (const auto& declaration : classes) {
        const auto identity = class_declaration_identity(declaration);
        if (declaration.name.empty() || declaration.canonical_identity.empty()
            || !class_identities.insert(identity).second
            || !declaration.scope.valid()
            || declaration.scope.value() >= semantics.scopes().size()
            || !valid_source_origin(
                semantics, declaration.source, declaration.origin)) {
            return "SystemVerilog class identity/source/origin is invalid";
        }
        const auto& scope = semantics.scopes()[declaration.scope.value()];
        if (scope.id != declaration.scope || scope.name != declaration.name
            || scope.source != declaration.source
            || scope.origin != declaration.origin || !scope.parent
            || !scope.parent->valid()
            || scope.parent->value() >= semantics.scopes().size()
            || !scope.unit.valid()
            || scope.unit.value() >= semantics.units().size()) {
            return "SystemVerilog class scope does not match its semantic owner";
        }
        const auto& origin = semantics.origins()[declaration.origin.value()];
        const auto& parent_scope
            = semantics.scopes()[scope.parent->value()];
        if (origin.parent
                != std::optional<OriginId> { parent_scope.origin }
            || parent_scope.unit != scope.unit
            || origin.detail != identity
            || !class_scopes.insert(declaration.scope).second) {
            return "SystemVerilog class origin/parent is inconsistent";
        }
        const auto& owner = semantics.units()[scope.unit.value()];
        if (owner.language != Language::system_verilog
            && owner.language != Language::verilog) {
            return "SystemVerilog class belongs to a foreign-language unit";
        }
        closure.class_scopes.insert(declaration.scope);

        std::set<DeclarationId> members;
        for (const auto member : declaration.member_declarations) {
            const auto* record = declaration_for(declarations, member);
            if (record == nullptr || record->scope != declaration.scope
                || !valid_source_origin(
                    semantics, record->source, record->origin)
                || semantics.origins()[record->origin.value()].parent
                    != std::optional<OriginId> { declaration.origin }
                || semantics.origins()[record->origin.value()].detail
                    != record->name
                || !members.insert(member).second) {
                return "SystemVerilog class member ownership is invalid";
            }
        }
        const auto member_matches = [&](const DeclarationId id,
                                        const SourceSpanId source,
                                        const OriginId member_origin) {
            const auto* record = declaration_for(declarations, id);
            return record != nullptr && members.contains(id)
                && record->scope == declaration.scope
                && record->source == source
                && record->origin == member_origin
                && valid_source_origin(semantics, source, member_origin)
                && semantics.origins()[member_origin.value()].parent
                    == std::optional<OriginId> { declaration.origin };
        };
        for (const auto& parameter : declaration.parameters) {
            const auto* member = declaration_for(
                declarations, parameter.declaration);
            const auto expected = parameter.type_parameter
                ? DeclarationForm::type_parameter
                : DeclarationForm::parameter;
            if (member == nullptr || parameter.name.empty()
                || member->name != parameter.name
                || (member->form != expected
                    && member->form != DeclarationForm::local_parameter)
                || !member_matches(parameter.declaration,
                    parameter.source, parameter.origin)) {
                return "SystemVerilog class parameter ownership is invalid";
            }
        }
        for (const auto alias : declaration.type_aliases) {
            const auto* member = declaration_for(declarations, alias);
            if (member == nullptr
                || member->form != DeclarationForm::typedef_declaration
                || !members.contains(alias)) {
                return "SystemVerilog class type-alias ownership is invalid";
            }
        }
        std::set<std::string> property_identities;
        for (const auto& property : declaration.properties) {
            const auto* member = declaration_for(
                declarations, property.declaration);
            if (member == nullptr || member->form != DeclarationForm::variable
                || property.name.empty() || member->name != property.name
                || property.owner_identity != identity
                || !property_identities.insert(
                    property.canonical_identity).second
                || !member_matches(property.declaration,
                    property.source, property.origin)) {
                return "SystemVerilog class property ownership is invalid";
            }
        }
        std::set<DeclarationId> method_declarations;
        std::set<std::pair<std::string, std::string>> method_profiles;
        for (const auto& method : declaration.methods) {
            const auto* member = declaration_for(
                declarations, method.declaration);
            const bool function = method.kind != ClassMethodKind::task;
            if (member == nullptr || method.name.empty()
                || member->name != method.name
                || method.owner_identity != identity
                || method.canonical_identity.empty()
                || method.profile_identity.empty()
                || member->form
                    != (function ? DeclarationForm::function
                                 : DeclarationForm::task)
                || !member->callable
                || member->callable->function != function
                || !member->nested_scope
                || !method_declarations.insert(method.declaration).second
                || !method_profiles.insert(
                    { method.owner_identity, method.profile_identity }).second
                || !member_matches(method.declaration,
                    method.source, method.origin)) {
                return "SystemVerilog class method ownership/profile is invalid";
            }
            const auto callable_scope = *member->nested_scope;
            if (!callable_scope.valid()
                || callable_scope.value() >= semantics.scopes().size()
                || semantics.scopes()[callable_scope.value()].parent
                    != std::optional<ScopeId> { declaration.scope }
                || semantics.scopes()[callable_scope.value()].unit
                    != scope.unit
                || semantics.scopes()[callable_scope.value()].source
                    != method.source
                || semantics.scopes()[callable_scope.value()].origin
                    != method.origin) {
                return "SystemVerilog class method callable scope is invalid";
            }
        }
        const auto valid_relation = [&](const ClassRelation& relation) {
            return !relation.name.empty()
                && valid_source_origin(
                    semantics, relation.source, relation.origin)
                && semantics.origins()[relation.origin.value()].parent
                    == std::optional<OriginId> { declaration.origin }
                && std::ranges::all_of(
                    relation.actuals, [&](const ActualAssociation& actual) {
                        return valid_actual_association(semantics, actual);
                    });
        };
        for (const auto& relation : declaration.extended_interfaces) {
            if (!valid_relation(relation)) {
                return "SystemVerilog class interface relation is invalid";
            }
        }
        for (const auto& relation : declaration.implemented_interfaces) {
            if (!valid_relation(relation)) {
                return "SystemVerilog class interface relation is invalid";
            }
        }
        if (declaration.base
            && (!valid_relation(*declaration.base)
                || declaration.base->declaration_identity
                    != declaration.base_declaration_identity)) {
            return "SystemVerilog class base relation is invalid";
        }
        for (const auto& constraint : declaration.constraints) {
            if (constraint.owner_identity != identity
                || constraint.canonical_identity.empty()
                || !valid_source_origin(
                    semantics, constraint.source, constraint.origin)
                || semantics.origins()[constraint.origin.value()].parent
                    != std::optional<OriginId> { declaration.origin }
                || !constraint_identities.insert(
                    constraint.canonical_identity).second) {
                return "SystemVerilog class constraint ownership is invalid";
            }
        }
        for (const auto member : declaration.member_declarations) {
            if (!valid_declaration_closure(closure, member,
                    declaration.scope, declaration.origin)) {
                return "SystemVerilog class declaration/body closure is invalid";
            }
        }
    }

    for (const auto& declaration : classes) {
        const auto identity = class_declaration_identity(declaration);
        const auto& scope = semantics.scopes()[declaration.scope.value()];
        const auto enclosing = std::ranges::find(
            classes, *scope.parent, &ClassDeclaration::scope);
        if (enclosing != classes.end()) {
            if (declaration.enclosing_identity
                    != class_declaration_identity(*enclosing)
                || std::ranges::find(
                       enclosing->nested_class_identities, identity)
                    == enclosing->nested_class_identities.end()) {
                return "SystemVerilog nested class parent is inconsistent";
            }
        } else {
            const auto& owner = semantics.units()[scope.unit.value()];
            const auto expected = std::string {
                owner.library.empty() ? "work" : owner.library }
                + "::" + owner.name;
            if (declaration.enclosing_identity != expected) {
                return "SystemVerilog class enclosing unit is inconsistent";
            }
        }
        std::set<std::string> nested;
        for (const auto& nested_identity
            : declaration.nested_class_identities) {
            const auto* child = class_for(nested_identity);
            if (child == nullptr || !nested.insert(nested_identity).second
                || semantics.scopes()[child->scope.value()].parent
                    != std::optional<ScopeId> { declaration.scope }) {
                return "SystemVerilog nested class ownership is invalid";
            }
        }
        std::set<std::string> composed;
        for (const auto& constraint : declaration.composed_constraints) {
            if (constraint.name.empty()
                || !constraint_identities.contains(
                    constraint.selected_identity)
                || !composed.insert(constraint.name).second
                || (!constraint.overridden_identity.empty()
                    && !constraint_identities.contains(
                        constraint.overridden_identity))) {
                return "SystemVerilog composed class constraint is invalid";
            }
        }
    }
    if (auto error = generate_class_ownership_error(
            semantics, units, declarations, classes);
        !error.empty()) {
        return "SystemVerilog generated-class ownership is invalid: "
            + error;
    }
    const auto class_owned_scope = [&](const ScopeId scope) {
        return closure.class_scopes.contains(scope)
            || std::ranges::any_of(closure.callable_scopes,
                [&](const ScopeId callable) {
                    return scope_within(semantics, scope, callable);
                });
    };
    for (const auto& declaration : declarations) {
        if (declaration.form != DeclarationForm::enumeration_literal
            || !class_owned_scope(declaration.scope)
            || !declaration.origin.valid()
            || declaration.origin.value() >= semantics.origins().size()) {
            continue;
        }
        const auto parent = semantics.origins()[declaration.origin.value()]
                                .parent;
        const auto owner = std::ranges::find_if(
            declarations, [&](const Declaration& candidate) {
                return parent
                    && candidate.scope == declaration.scope
                    && candidate.form
                        == DeclarationForm::typedef_declaration
                    && candidate.origin == *parent
                    && closure.claimed_declarations.contains(candidate.id);
            });
        if (owner == declarations.end()
            || !valid_declaration_closure(closure, declaration.id,
                declaration.scope, owner->origin)) {
            return "SystemVerilog class enumeration closure is invalid";
        }
    }
    for (const auto& declaration : semantics.declarations()) {
        if (class_owned_scope(declaration.scope)
            && !closure.claimed_declarations.contains(declaration.id)) {
            return "SystemVerilog class declaration is orphaned";
        }
    }
    for (const auto& declaration : declarations) {
        if (class_owned_scope(declaration.scope)
            && !closure.claimed_declarations.contains(declaration.id)) {
            return "SystemVerilog class declaration record is orphaned";
        }
    }
    for (const auto& statement : semantics.statement_identities()) {
        if (std::ranges::any_of(closure.callable_scopes,
                [&](const ScopeId callable) {
                    return scope_within(semantics,
                        statement.scope, callable);
                })
            && !closure.claimed_statements.contains(statement.id)) {
            return "SystemVerilog class statement is orphaned";
        }
    }
    for (const auto& statement : statements) {
        if (std::ranges::any_of(closure.callable_scopes,
                [&](const ScopeId callable) {
                    return scope_within(semantics,
                        statement.scope, callable);
                })
            && !closure.claimed_statements.contains(statement.id)) {
            return "SystemVerilog class statement record is orphaned";
        }
    }
    return { };
}

void Hir::note_mutation() noexcept { ++revision_; }

const std::vector<Unit>& Hir::units() const noexcept { return units_; }
const std::vector<Declaration>& Hir::declarations() const noexcept {
    return declarations_;
}
const std::vector<TypeDefinition>& Hir::types() const noexcept { return types_; }
const std::vector<Expression>& Hir::expressions() const noexcept {
    return expressions_;
}
const std::vector<Statement>& Hir::statements() const noexcept {
    return statements_;
}
const std::vector<Process>& Hir::processes() const noexcept {
    return processes_;
}
const std::vector<ClassDeclaration>& Hir::classes() const noexcept {
    return classes_;
}
const std::vector<Instance>& Hir::instances() const noexcept {
    return instances_;
}
const std::vector<UdpDeclaration>& Hir::udps() const noexcept { return udps_; }
const std::vector<DpiDeclaration>& Hir::dpi_declarations() const noexcept {
    return dpi_declarations_;
}
const std::vector<CovergroupInstance>&
Hir::covergroup_instances() const noexcept {
    return covergroup_instances_;
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
std::vector<ClassDeclaration>& Hir::mutable_classes() noexcept {
    note_mutation();
    return classes_;
}
std::vector<Instance>& Hir::mutable_instances() noexcept {
    note_mutation();
    return instances_;
}
std::vector<UdpDeclaration>& Hir::mutable_udps() noexcept {
    note_mutation();
    return udps_;
}
std::vector<DpiDeclaration>& Hir::mutable_dpi_declarations() noexcept {
    note_mutation();
    return dpi_declarations_;
}
std::vector<CovergroupInstance>& Hir::mutable_covergroup_instances() noexcept {
    note_mutation();
    return covergroup_instances_;
}

} // namespace fsim::semantic::sv
