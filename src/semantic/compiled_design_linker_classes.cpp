// SPDX-License-Identifier: Apache-2.0
#include "compiled_design_linker_classes.hpp"

#include <ranges>
#include <set>

namespace fsim::semantic {
namespace {

std::string reference_library(const std::string_view identity)
{
    const auto separator = identity.find("::");
    return separator == std::string_view::npos
        ? std::string { "work" }
        : std::string { identity.substr(0, separator) };
}

std::string reference_name(const std::string_view identity)
{
    const auto separator = identity.rfind("::");
    return separator == std::string_view::npos
        ? std::string { identity }
        : std::string { identity.substr(separator + 2U) };
}

void append_reference(std::vector<CompiledReference>& references,
    const UnitId owner, const std::string_view identity,
    const SourceSpanId source)
{
    if (identity.empty())
        return;
    references.push_back({ CompiledReferenceKind::class_declaration,
        owner, reference_library(identity), reference_name(identity),
        std::string { identity }, source, std::nullopt });
}

void append_type_reference(std::vector<CompiledReference>& references,
    const UnitId owner, const sv::TypeReference& type,
    const SourceSpanId fallback_source)
{
    append_reference(references, owner, type.class_identity,
        type.target.source.valid() ? type.target.source : fallback_source);
    for (const auto& actual : type.interface_parameter_actuals) {
        if (actual.type) {
            append_type_reference(
                references, owner, *actual.type, actual.source);
        }
    }
    for (const auto& element : type.container_element_types) {
        append_type_reference(
            references, owner, element, fallback_source);
    }
}

const sv::Declaration* declaration_for(
    const CompiledDesign& design, const DeclarationId id)
{
    const auto found = std::ranges::find(
        design.systemverilog_hir.declarations(), id,
        &sv::Declaration::id);
    return found == design.systemverilog_hir.declarations().end()
        ? nullptr
        : &*found;
}

const sv::Statement* statement_for(
    const CompiledDesign& design, const StatementId id)
{
    const auto found = std::ranges::find(
        design.systemverilog_hir.statements(), id,
        &sv::Statement::id);
    return found == design.systemverilog_hir.statements().end()
        ? nullptr
        : &*found;
}

void append_declaration_references(const CompiledDesign& design,
    std::vector<CompiledReference>& references, UnitId owner,
    DeclarationId declaration_id,
    std::set<DeclarationId>& visited_declarations,
    std::set<StatementId>& visited_statements);

void append_statement_references(const CompiledDesign& design,
    std::vector<CompiledReference>& references, const UnitId owner,
    const StatementId statement_id,
    std::set<DeclarationId>& visited_declarations,
    std::set<StatementId>& visited_statements)
{
    if (!visited_statements.insert(statement_id).second)
        return;
    const auto* statement = statement_for(design, statement_id);
    if (statement == nullptr)
        return;
    for (const auto declaration : statement->declarations) {
        append_declaration_references(design, references, owner,
            declaration, visited_declarations, visited_statements);
    }
    const auto append_statements = [&](const auto& statements) {
        for (const auto child : statements) {
            append_statement_references(design, references, owner,
                child, visited_declarations, visited_statements);
        }
    };
    append_statements(statement->statements);
    append_statements(statement->else_statements);
    append_statements(statement->loop_updates);
    for (const auto& alternative : statement->case_alternatives)
        append_statements(alternative.statements);
}

void append_declaration_references(const CompiledDesign& design,
    std::vector<CompiledReference>& references, const UnitId owner,
    const DeclarationId declaration_id,
    std::set<DeclarationId>& visited_declarations,
    std::set<StatementId>& visited_statements)
{
    if (!visited_declarations.insert(declaration_id).second)
        return;
    const auto* declaration = declaration_for(design, declaration_id);
    if (declaration == nullptr)
        return;
    if (declaration->type) {
        append_type_reference(references, owner,
            *declaration->type, declaration->source);
    }
    if (declaration->default_type) {
        append_type_reference(references, owner,
            *declaration->default_type, declaration->source);
    }
    if (declaration->callable) {
        append_type_reference(references, owner,
            declaration->callable->return_type, declaration->source);
        for (const auto formal : declaration->callable->formals) {
            append_declaration_references(
                design, references, owner, formal,
                visited_declarations, visited_statements);
        }
    }
    for (const auto child : declaration->children) {
        append_declaration_references(
            design, references, owner, child,
            visited_declarations, visited_statements);
    }
    for (const auto statement : declaration->statements) {
        append_statement_references(design, references, owner,
            statement, visited_declarations, visited_statements);
    }
}

void append_scoped_class_references(const CompiledDesign& design,
    std::vector<CompiledReference>& references)
{
    const auto& model = design.semantics;
    const auto owner_for = [&](const ScopeId scope) -> std::optional<UnitId> {
        if (!scope.valid() || scope.value() >= model.scopes().size())
            return std::nullopt;
        const auto owner = model.scopes()[scope.value()].unit;
        return owner.valid() && owner.value() < model.units().size()
            ? std::optional { owner } : std::nullopt;
    };
    const auto append_type = [&](const ScopeId scope,
                                 const sv::TypeReference& type,
                                 const SourceSpanId source) {
        if (const auto owner = owner_for(scope))
            append_type_reference(references, *owner, type, source);
    };
    for (const auto& declaration : design.systemverilog_hir.declarations()) {
        if (declaration.type)
            append_type(declaration.scope, *declaration.type, declaration.source);
        if (declaration.default_type)
            append_type(declaration.scope, *declaration.default_type, declaration.source);
        if (declaration.callable)
            append_type(declaration.scope, declaration.callable->return_type,
                declaration.source);
    }
    for (const auto& type : design.systemverilog_hir.types()) {
        if (!type.id.valid() || type.id.value() >= model.types().size())
            continue;
        const auto scope = model.types()[type.id.value()].scope;
        append_type(scope, type.base, type.source);
        for (const auto& member : type.members)
            append_type(scope, member.type, member.source);
        if (type.container && type.container->associative_index)
            append_type(scope, *type.container->associative_index, type.source);
    }
    for (const auto& instance : design.systemverilog_hir.instances()) {
        const auto append_actuals = [&](const auto& actuals) {
            for (const auto& actual : actuals) {
                if (actual.type)
                    append_type(instance.scope, *actual.type, actual.source);
            }
        };
        append_actuals(instance.parameters);
        append_actuals(instance.ports);
    }
    for (const auto& statement : design.systemverilog_hir.statements()) {
        const auto owner = owner_for(statement.scope);
        if (!owner)
            continue;
        append_reference(references, *owner, statement.class_handle_type,
            statement.source);
        auto spelling = std::string_view { statement.task.spelling };
        constexpr auto static_prefix = std::string_view { "@sv-static-task:" };
        constexpr auto instance_prefix = std::string_view { "@sv-task:" };
        if (spelling.starts_with(static_prefix))
            spelling.remove_prefix(static_prefix.size());
        else if (spelling.starts_with(instance_prefix))
            spelling.remove_prefix(instance_prefix.size());
        else
            continue;
        const auto member = spelling.rfind("::");
        if (member != std::string_view::npos)
            append_reference(references, *owner, spelling.substr(0U, member),
                statement.source);
    }
}

} // namespace

std::optional<UnitId> compiled_class_owner(
    const CompiledDesign& design, const sv::ClassDeclaration& declaration)
{
    if (declaration.scope.valid()
        && declaration.scope.value() < design.semantics.scopes().size()) {
        const auto unit
            = design.semantics.scopes()[declaration.scope.value()].unit;
        return unit.valid() ? std::optional<UnitId> { unit }
                            : std::nullopt;
    }
    const auto first = declaration.canonical_identity.find("::");
    const auto second = first == std::string::npos
        ? std::string::npos
        : declaration.canonical_identity.find("::", first + 2U);
    if (first != std::string::npos && second != std::string::npos) {
        const auto package = design.find_unit(
            UnitKind::systemverilog_package,
            declaration.canonical_identity.substr(0, first),
            declaration.canonical_identity.substr(
                first + 2U, second - first - 2U));
        if (package)
            return package->identity->id;
    }
    if (!declaration.source.valid()
        || declaration.source.value()
            >= design.semantics.source_spans().size()) {
        return std::nullopt;
    }
    const auto file
        = design.semantics.source_spans()[declaration.source.value()].file;
    const auto unit = std::ranges::find_if(
        design.systemverilog_hir.units(), [&](const sv::Unit& item) {
            return item.source.valid()
                && item.source.value() < design.semantics.source_spans().size()
                && design.semantics.source_spans()[item.source.value()].file
                    == file;
        });
    return unit == design.systemverilog_hir.units().end()
        ? std::nullopt
        : std::optional<UnitId> { unit->id };
}

void append_compiled_class_references(
    const CompiledDesign& design,
    std::vector<CompiledReference>& references)
{
    append_scoped_class_references(design, references);
    for (const auto& declaration : design.systemverilog_hir.classes()) {
        const auto selected_owner = compiled_class_owner(
            design, declaration);
        if (!selected_owner) {
            continue;
        }
        const auto owner = *selected_owner;
        const auto append_relation = [&](const sv::ClassRelation& relation) {
            append_reference(references, owner,
                relation.declaration_identity, relation.source);
            for (const auto& actual : relation.actuals) {
                if (actual.type) {
                    append_type_reference(
                        references, owner, *actual.type, actual.source);
                }
            }
        };
        if (declaration.base) {
            append_relation(*declaration.base);
        } else {
            append_reference(references, owner,
                declaration.base_declaration_identity, declaration.source);
        }
        for (const auto& relation : declaration.extended_interfaces)
            append_relation(relation);
        for (const auto& relation : declaration.implemented_interfaces)
            append_relation(relation);
        for (const auto& parameter : declaration.parameters) {
            if (parameter.type) {
                append_type_reference(
                    references, owner, *parameter.type, parameter.source);
            }
            if (parameter.default_type) {
                append_type_reference(references, owner,
                    *parameter.default_type, parameter.source);
            }
        }
        for (const auto& property : declaration.properties) {
            append_type_reference(
                references, owner, property.type, property.source);
        }
        for (const auto alias : declaration.type_aliases) {
            const auto type = std::ranges::find(
                design.systemverilog_hir.types(), alias,
                &sv::TypeDefinition::declaration);
            if (type == design.systemverilog_hir.types().end())
                continue;
            append_type_reference(
                references, owner, type->base, type->source);
            for (const auto& member : type->members) {
                append_type_reference(
                    references, owner, member.type, member.source);
            }
        }
        std::set<DeclarationId> visited_declarations;
        std::set<StatementId> visited_statements;
        for (const auto member : declaration.member_declarations) {
            append_declaration_references(
                design, references, owner, member,
                visited_declarations, visited_statements);
        }
    }
}

} // namespace fsim::semantic
