// SPDX-License-Identifier: Apache-2.0
#include "compiled_design_linker_classes.hpp"

#include <ranges>
#include <set>
#include <unordered_map>

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

// HIR records by id and defined classes by name, built once per pass.
// Ids in a linked design need not be dense, and a missed dense slot made
// every lookup a scan over all records.
class ReferenceLookup {
public:
    explicit ReferenceLookup(const sv::Hir& hir)
    {
        index(hir.statements(), statements_);
        index(hir.declarations(), declarations_);
        index(hir.expressions(), expressions_);
        for (const auto& candidate : hir.classes()) {
            if (candidate.forward_declaration)
                continue;
            // A second definition makes the name ambiguous.
            const auto [found, inserted]
                = classes_.try_emplace(candidate.name, &candidate);
            if (!inserted)
                found->second = nullptr;
        }
    }

    [[nodiscard]] const sv::Statement* statement(const StatementId id) const
    {
        return lookup(statements_, id);
    }
    [[nodiscard]] const sv::Declaration* declaration(
        const DeclarationId id) const
    {
        return lookup(declarations_, id);
    }
    [[nodiscard]] const sv::Expression* expression(
        const ExpressionId id) const
    {
        return lookup(expressions_, id);
    }
    [[nodiscard]] const sv::ClassDeclaration* unique_class(
        const std::string& name) const
    {
        const auto found = classes_.find(name);
        return found == classes_.end() ? nullptr : found->second;
    }

private:
    template <typename Records, typename Record>
    static void index(const Records& records,
        std::unordered_map<std::uint32_t, const Record*>& result)
    {
        result.reserve(records.size());
        for (const auto& record : records) {
            if (record.id.valid())
                result.try_emplace(record.id.value(), &record);
        }
    }
    template <typename Record, typename IdType>
    static const Record* lookup(
        const std::unordered_map<std::uint32_t, const Record*>& records,
        const IdType id)
    {
        if (!id.valid())
            return nullptr;
        const auto found = records.find(id.value());
        return found == records.end() ? nullptr : found->second;
    }

    std::unordered_map<std::uint32_t, const sv::Statement*> statements_;
    std::unordered_map<std::uint32_t, const sv::Declaration*> declarations_;
    std::unordered_map<std::uint32_t, const sv::Expression*> expressions_;
    std::unordered_map<std::string, const sv::ClassDeclaration*> classes_;
};

void append_type_reference(const ReferenceLookup& lookup,
    std::vector<CompiledReference>& references,
    const UnitId owner, const sv::TypeReference& type,
    const SourceSpanId fallback_source)
{
    append_reference(references, owner, type.class_identity,
        type.target.source.valid() ? type.target.source : fallback_source);
    // A class-scoped type (`A::q_t`, IEEE 1800-2017 8.23) is declared in
    // its class, so the design links that class even when nothing else
    // names it.
    if (type.class_identity.empty() && !type.target.resolved()) {
        const auto& spelling = type.target.spelling;
        const auto separator = spelling.rfind("::");
        if (separator != std::string::npos) {
            auto scope = spelling.substr(0, separator);
            if (const auto parameters = scope.find('#');
                parameters != std::string::npos) {
                scope.resize(parameters);
            }
            while (!scope.empty() && scope.back() == ' ')
                scope.pop_back();
            if (const auto* selected
                = lookup.unique_class(reference_name(scope))) {
                append_reference(references, owner,
                    sv::class_declaration_identity(*selected),
                    type.target.source.valid() ? type.target.source
                                               : fallback_source);
            }
        }
    }
    // A virtual interface names an interface that need not be instantiated
    // (IEEE 1800-2017 25.9); the design links its definition.
    if (type.virtual_interface && !type.interface_type.empty()) {
        references.push_back({ CompiledReferenceKind::module, owner,
            reference_library(type.interface_type),
            reference_name(type.interface_type), { },
            type.target.source.valid() ? type.target.source
                                       : fallback_source,
            std::nullopt });
    }
    for (const auto& actual : type.interface_parameter_actuals) {
        if (actual.type) {
            append_type_reference(
                lookup, references, owner, *actual.type, actual.source);
            continue;
        }
        // A class named as a type actual (`C#(Item)`).
        const auto* found = actual.expression
            ? lookup.expression(*actual.expression)
            : nullptr;
        if (found == nullptr
            || found->kind != sv::ExpressionKind::name
            || found->text.find("::") != std::string::npos) {
            continue;
        }
        if (const auto* selected = lookup.unique_class(found->text)) {
            append_reference(references, owner,
                sv::class_declaration_identity(*selected), actual.source);
        }
    }
    for (const auto& element : type.container_element_types) {
        append_type_reference(lookup,
            references, owner, element, fallback_source);
    }
}

void append_declaration_references(const ReferenceLookup& lookup,
    std::vector<CompiledReference>& references, UnitId owner,
    DeclarationId declaration_id,
    std::set<DeclarationId>& visited_declarations,
    std::set<StatementId>& visited_statements);

void append_statement_references(const ReferenceLookup& lookup,
    std::vector<CompiledReference>& references, const UnitId owner,
    const StatementId statement_id,
    std::set<DeclarationId>& visited_declarations,
    std::set<StatementId>& visited_statements)
{
    if (!visited_statements.insert(statement_id).second)
        return;
    const auto* statement = lookup.statement(statement_id);
    if (statement == nullptr)
        return;
    for (const auto declaration : statement->declarations) {
        append_declaration_references(lookup, references, owner,
            declaration, visited_declarations, visited_statements);
    }
    const auto append_statements = [&](const auto& statements) {
        for (const auto child : statements) {
            append_statement_references(lookup, references, owner,
                child, visited_declarations, visited_statements);
        }
    };
    append_statements(statement->statements);
    append_statements(statement->else_statements);
    append_statements(statement->loop_updates);
    for (const auto& alternative : statement->case_alternatives)
        append_statements(alternative.statements);
}

void append_declaration_references(const ReferenceLookup& lookup,
    std::vector<CompiledReference>& references, const UnitId owner,
    const DeclarationId declaration_id,
    std::set<DeclarationId>& visited_declarations,
    std::set<StatementId>& visited_statements)
{
    if (!visited_declarations.insert(declaration_id).second)
        return;
    const auto* declaration = lookup.declaration(declaration_id);
    if (declaration == nullptr)
        return;
    if (declaration->type) {
        append_type_reference(lookup, references, owner,
            *declaration->type, declaration->source);
    }
    if (declaration->default_type) {
        append_type_reference(lookup, references, owner,
            *declaration->default_type, declaration->source);
    }
    if (declaration->callable) {
        append_type_reference(lookup, references, owner,
            declaration->callable->return_type, declaration->source);
        for (const auto formal : declaration->callable->formals) {
            append_declaration_references(
                lookup, references, owner, formal,
                visited_declarations, visited_statements);
        }
    }
    for (const auto child : declaration->children) {
        append_declaration_references(
            lookup, references, owner, child,
            visited_declarations, visited_statements);
    }
    for (const auto statement : declaration->statements) {
        append_statement_references(lookup, references, owner,
            statement, visited_declarations, visited_statements);
    }
}

void append_scoped_class_references(const CompiledDesign& design,
    const ReferenceLookup& lookup,
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
            append_type_reference(lookup, references, *owner, type, source);
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
    const ReferenceLookup lookup { design.systemverilog_hir };
    append_scoped_class_references(design, lookup, references);
    // The class a static selection names, with its actuals
    // (`Qux#(Foo)::get()`), and `type(data_type)` operands.
    const auto& scopes = design.semantics.scopes();
    for (const auto& expression : design.systemverilog_hir.expressions()) {
        if ((expression.class_owner.empty()
                && expression.type_operand.empty())
            || !expression.scope.valid()
            || expression.scope.value() >= scopes.size()) {
            continue;
        }
        const auto owner = scopes[expression.scope.value()].unit;
        for (const auto& type : expression.class_owner) {
            append_type_reference(lookup,
                references, owner, type, expression.source);
        }
        for (const auto& type : expression.type_operand) {
            append_type_reference(lookup,
                references, owner, type, expression.source);
        }
    }
    // The first type each alias declaration declares.
    std::unordered_map<std::uint32_t, const sv::TypeDefinition*>
        types_by_declaration;
    for (const auto& type : design.systemverilog_hir.types()) {
        if (type.declaration.valid()) {
            types_by_declaration.try_emplace(type.declaration.value(), &type);
        }
    }
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
                    append_type_reference(lookup,
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
                append_type_reference(lookup,
                    references, owner, *parameter.type, parameter.source);
            }
            if (parameter.default_type) {
                append_type_reference(lookup, references, owner,
                    *parameter.default_type, parameter.source);
            }
        }
        for (const auto& property : declaration.properties) {
            append_type_reference(lookup,
                references, owner, property.type, property.source);
        }
        for (const auto alias : declaration.type_aliases) {
            const auto found = alias.valid()
                ? types_by_declaration.find(alias.value())
                : types_by_declaration.end();
            if (found == types_by_declaration.end())
                continue;
            const auto* type = found->second;
            append_type_reference(lookup,
                references, owner, type->base, type->source);
            for (const auto& member : type->members) {
                append_type_reference(lookup,
                    references, owner, member.type, member.source);
            }
        }
        std::set<DeclarationId> visited_declarations;
        std::set<StatementId> visited_statements;
        for (const auto member : declaration.member_declarations) {
            append_declaration_references(
                lookup, references, owner, member,
                visited_declarations, visited_statements);
        }
    }
}

} // namespace fsim::semantic
