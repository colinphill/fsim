// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/systemverilog_class_specialization.hpp"

#include <algorithm>
#include <charconv>
#include <functional>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace fsim::semantic::sv {
namespace {

template <typename Id, typename Record>
const Record* record_for(const std::map<Id, const Record*>& records, Id id)
{
    const auto found = records.find(id);
    return found == records.end() ? nullptr : found->second;
}

std::string encoded(const std::string_view value)
{
    return std::to_string(value.size()) + ":" + std::string { value };
}

std::optional<std::int64_t> integer_literal(const std::string_view text)
{
    if (text.empty()) {
        return std::nullopt;
    }
    std::int64_t value { };
    const auto [end, error] = std::from_chars(
        text.data(), text.data() + text.size(), value, 10);
    if (error != std::errc { } || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

struct ValueBinding {
    std::string display;
    std::string canonical;
    std::optional<std::int64_t> integer;
    std::optional<bool> boolean;
    std::optional<ExpressionId> expression;
};

struct TypeBinding {
    TypeReference type;
    std::string canonical;
};

struct Environment {
    std::map<DeclarationId, ValueBinding> values;
    std::map<TypeId, TypeBinding> types;
};

struct ResolvedActual {
    std::optional<std::string> formal;
    ActualKind kind { ActualKind::expression };
    std::optional<ExpressionId> expression;
    std::optional<TypeReference> type;
    std::optional<ValueBinding> resolved_value;
    std::optional<TypeBinding> resolved_type;
    SourceSpanId source;
};

struct BoundParameters {
    Environment environment;
    std::vector<SpecializedClassParameter> parameters;
    std::string specialization_identity;
};

enum class RelationKind : std::uint8_t {
    base,
    extended_interface,
    implemented_interface,
};

class ClassSpecializer final {
public:
    ClassSpecializer(const Model& semantics, const Hir& hir)
        : semantics_(semantics)
        , hir_(hir)
    {
        for (const auto& declaration : hir_.classes()) {
            const auto identity = class_declaration_identity(declaration);
            if (!classes_.emplace(identity, &declaration).second) {
                report(ClassSpecializationErrorKind::duplicate_declaration,
                    identity, declaration.source,
                    "duplicate compiled class declaration '" + identity + "'");
            }
        }
        for (const auto& declaration : hir_.declarations()) {
            declarations_.emplace(declaration.id, &declaration);
        }
        for (const auto& type : hir_.types()) {
            types_.emplace(type.id, &type);
        }
        for (const auto& expression : hir_.expressions()) {
            expressions_.emplace(expression.id, &expression);
        }
        for (const auto& statement : hir_.statements()) {
            statements_.emplace(statement.id, &statement);
        }
        for (const auto& unit : hir_.units()) {
            units_.emplace(unit.id, &unit);
        }
    }

    ClassSpecializationResult run(
        const std::span<const ClassSpecializationRequest> requests)
    {
        std::vector<const ClassDeclaration*> defaults;
        defaults.reserve(classes_.size());
        for (const auto& [identity, declaration] : classes_) {
            (void)identity;
            if (!declaration->forward_declaration) {
                defaults.push_back(declaration);
            }
        }
        for (const auto* declaration : defaults) {
            (void)specialize(*declaration, { }, declaration->source);
        }

        std::vector<ClassSpecializationRequest> ordered {
            requests.begin(), requests.end()
        };
        std::ranges::stable_sort(ordered, [](const auto& left,
                                            const auto& right) {
            return std::tuple { left.declaration_identity, left.source }
                < std::tuple { right.declaration_identity, right.source };
        });
        for (const auto& request : ordered) {
            const auto found = classes_.find(request.declaration_identity);
            if (found == classes_.end()) {
                report(ClassSpecializationErrorKind::unknown_declaration,
                    request.declaration_identity, request.source,
                    "unknown compiled class declaration '"
                        + request.declaration_identity + "'");
                continue;
            }
            std::vector<ResolvedActual> actuals;
            actuals.reserve(request.actuals.size());
            for (const auto& actual : request.actuals) {
                actuals.push_back({ actual.formal, actual.kind,
                    actual.expression, actual.type, std::nullopt,
                    std::nullopt, actual.source });
            }
            (void)specialize(*found->second, actuals, request.source);
        }

        std::ranges::sort(result_.specializations,
            {}, &ClassSpecialization::specialization_identity);
        std::ranges::sort(result_.errors, [](const auto& left,
                                            const auto& right) {
            return std::tuple { left.declaration_identity, left.source,
                       left.kind, left.message }
                < std::tuple { right.declaration_identity, right.source,
                      right.kind, right.message };
        });
        result_.errors.erase(std::unique(
            result_.errors.begin(), result_.errors.end()),
            result_.errors.end());
        return std::move(result_);
    }

private:
    void report(const ClassSpecializationErrorKind kind,
        std::string identity, const SourceSpanId source,
        std::string message)
    {
        result_.errors.push_back(
            { kind, std::move(identity), source, std::move(message) });
    }

    const ClassDeclaration* class_for(const std::string_view identity) const
    {
        const auto found = classes_.find(std::string { identity });
        return found == classes_.end() ? nullptr : found->second;
    }

    const ClassDeclaration* relation_target(const ClassRelation& relation,
        const ClassDeclaration& owner)
    {
        if (!relation.declaration_identity.empty()) {
            if (const auto* declaration = class_for(
                    relation.declaration_identity)) {
                return declaration;
            }
            report(ClassSpecializationErrorKind::incompatible_relation,
                class_declaration_identity(owner), relation.source,
                "class relation '" + relation.name
                    + "' does not resolve to a compiled class");
            return nullptr;
        }
        const ClassDeclaration* selected { };
        for (const auto& [identity, declaration] : classes_) {
            if (declaration->name != relation.name && identity != relation.name
                && !identity.ends_with("::" + relation.name)) {
                continue;
            }
            if (selected != nullptr && selected != declaration) {
                report(ClassSpecializationErrorKind::incompatible_relation,
                    class_declaration_identity(owner), relation.source,
                    "class relation '" + relation.name + "' is ambiguous");
                return nullptr;
            }
            selected = declaration;
        }
        if (selected == nullptr) {
            report(ClassSpecializationErrorKind::incompatible_relation,
                class_declaration_identity(owner), relation.source,
                "class relation '" + relation.name
                    + "' does not resolve to a compiled class");
        }
        return selected;
    }

    std::optional<ValueBinding> value(
        const ExpressionId id, const Environment& environment,
        const std::string_view owner, const SourceSpanId source,
        std::set<ExpressionId>& active)
    {
        const auto* expression = record_for(expressions_, id);
        if (expression == nullptr || !active.insert(id).second) {
            report(ClassSpecializationErrorKind::unresolved_constant,
                std::string { owner }, source,
                "class parameter expression does not resolve to acyclic HIR");
            return std::nullopt;
        }
        const auto release = [&] { active.erase(id); };
        if (expression->referenced_name
            && expression->referenced_name->selected) {
            const auto replacement = environment.values.find(
                *expression->referenced_name->selected);
            if (replacement != environment.values.end()) {
                release();
                return replacement->second;
            }
            const auto* declaration = record_for(
                declarations_, *expression->referenced_name->selected);
            if (declaration != nullptr && declaration->initializer
                && (declaration->form == DeclarationForm::parameter
                    || declaration->form
                        == DeclarationForm::local_parameter)) {
                auto resolved = value(*declaration->initializer,
                    environment, owner, declaration->source, active);
                release();
                return resolved;
            }
        }

        ValueBinding resolved;
        resolved.display = expression->decoded_string.value_or(
            expression->text);
        resolved.expression = id;
        if (expression->kind == ExpressionKind::integer_literal) {
            resolved.integer = integer_literal(expression->text);
            if (!resolved.integer) {
                release();
                report(ClassSpecializationErrorKind::unresolved_constant,
                    std::string { owner }, expression->source,
                    "class integer actual is not a normalized plain literal");
                return std::nullopt;
            }
            resolved.canonical = "integer:" + std::to_string(
                *resolved.integer);
            release();
            return resolved;
        }
        if (expression->kind == ExpressionKind::boolean_literal) {
            if (expression->text == "true" || expression->text == "1") {
                resolved.boolean = true;
            } else if (expression->text == "false"
                || expression->text == "0") {
                resolved.boolean = false;
            } else {
                release();
                report(ClassSpecializationErrorKind::unresolved_constant,
                    std::string { owner }, expression->source,
                    "class boolean actual is not normalized");
                return std::nullopt;
            }
            resolved.canonical = *resolved.boolean
                ? "boolean:true" : "boolean:false";
            release();
            return resolved;
        }
        if (expression->kind == ExpressionKind::string_literal) {
            resolved.canonical = "string:" + encoded(resolved.display);
            release();
            return resolved;
        }
        if (expression->kind == ExpressionKind::logic_literal) {
            resolved.canonical = "logic:" + encoded(expression->text);
            release();
            return resolved;
        }
        if (expression->kind == ExpressionKind::unary
            && expression->operands.size() == 1U) {
            const auto operand = value(
                expression->operands.front(), environment, owner,
                expression->source, active);
            if (operand && operand->integer) {
                if (expression->text == "+") {
                    resolved.integer = *operand->integer;
                } else if (expression->text == "-") {
                    resolved.integer = -*operand->integer;
                } else if (expression->text == "~") {
                    resolved.integer = ~*operand->integer;
                }
                if (resolved.integer) {
                    resolved.display = std::to_string(*resolved.integer);
                    resolved.canonical = "integer:" + resolved.display;
                    release();
                    return resolved;
                }
            }
        }
        if (expression->kind == ExpressionKind::binary
            && expression->operands.size() == 2U) {
            const auto left = value(expression->operands[0], environment,
                owner, expression->source, active);
            const auto right = value(expression->operands[1], environment,
                owner, expression->source, active);
            if (left && right && left->integer && right->integer) {
                const auto lhs = *left->integer;
                const auto rhs = *right->integer;
                if (expression->text == "+")
                    resolved.integer = lhs + rhs;
                else if (expression->text == "-")
                    resolved.integer = lhs - rhs;
                else if (expression->text == "*")
                    resolved.integer = lhs * rhs;
                else if (expression->text == "/" && rhs != 0)
                    resolved.integer = lhs / rhs;
                else if (expression->text == "%" && rhs != 0)
                    resolved.integer = lhs % rhs;
                else if (expression->text == "<<" && rhs >= 0 && rhs < 64)
                    resolved.integer = static_cast<std::int64_t>(
                        static_cast<std::uint64_t>(lhs)
                        << static_cast<unsigned>(rhs));
                else if (expression->text == ">>" && rhs >= 0 && rhs < 64)
                    resolved.integer = lhs >> static_cast<unsigned>(rhs);
                if (resolved.integer) {
                    resolved.display = std::to_string(*resolved.integer);
                    resolved.canonical = "integer:" + resolved.display;
                    release();
                    return resolved;
                }
            }
        }
        release();
        report(ClassSpecializationErrorKind::unresolved_constant,
            std::string { owner }, expression->source,
            "class parameter actual was not folded to a literal");
        return std::nullopt;
    }

    std::optional<ValueBinding> value(const ExpressionId id,
        const Environment& environment, const std::string_view owner,
        const SourceSpanId source)
    {
        std::set<ExpressionId> active;
        return value(id, environment, owner, source, active);
    }

    std::string declaration_identity(const DeclarationId id) const
    {
        const auto* declaration = record_for(declarations_, id);
        if (declaration == nullptr || !declaration->scope.valid()
            || declaration->scope.value()
                >= semantics_.scopes().size()) {
            return { };
        }
        std::vector<std::string_view> scopes;
        auto scope = std::optional<ScopeId> { declaration->scope };
        const auto unit_id = semantics_.scopes()[
            declaration->scope.value()].unit;
        if (!unit_id.valid()
            || unit_id.value() >= semantics_.units().size()) {
            return { };
        }
        const auto& unit = semantics_.units()[unit_id.value()];
        while (scope && *scope != unit.scope) {
            if (!scope->valid()
                || scope->value() >= semantics_.scopes().size()) {
                return { };
            }
            const auto& current = semantics_.scopes()[scope->value()];
            if (!current.name.empty()) {
                scopes.push_back(current.name);
            }
            scope = current.parent;
        }
        std::string identity = unit.library.empty()
            ? "work" : unit.library;
        identity += "::" + unit.name;
        for (auto item = scopes.rbegin(); item != scopes.rend(); ++item) {
            identity += "::" + std::string { *item };
        }
        return identity + "::" + declaration->name;
    }

    std::string type_identity(const TypeReference& type) const
    {
        std::ostringstream identity;
        std::string target_identity = type.target.spelling;
        if (type.target.target.valid()) {
            const auto* definition = record_for(types_, type.target.target);
            if (definition != nullptr) {
                target_identity = declaration_identity(
                    definition->declaration);
            }
        }
        // Built-in packed integral types are identified by their semantic
        // profile below, not by the source keyword which introduced them.
        // In particular, `reg [N:0]` and `logic [N:0]` are the same
        // four-state variable data type.  Net kind remains independently
        // represented by systemverilog_net_type.
        if (!type.target.target.valid()
            && type.value_form == TypeForm::packed_integral
            && type.class_identity.empty()
            && type.interface_type.empty()
            && !type.container_form) {
            target_identity = type.four_state
                ? "@builtin:four-state-integral"
                : "@builtin:two-state-integral";
        }
        identity << "type:" << encoded(target_identity) << ':';
        if (type.value_form) {
            identity << static_cast<unsigned>(*type.value_form);
        } else {
            identity << "none";
        }
        identity << ":class=" << encoded(type.class_identity)
                 << ":virtual-interface=" << type.virtual_interface
                 << ":interface=" << encoded(type.interface_type)
                 << ":modport=" << encoded(type.interface_modport)
                 << ":net=" << encoded(type.systemverilog_net_type)
                 << ":resolution="
                 << encoded(type.systemverilog_resolution_function)
                 << ":signed=" << type.signed_value
                 << ":four-state=" << type.four_state << ":width=";
        if (type.executable_width) {
            identity << *type.executable_width;
        } else {
            identity << "none";
        }
        for (const auto& actual : type.interface_parameter_actuals) {
            identity << ":interface-actual="
                     << encoded(actual.formal.value_or(std::string { }));
            if (actual.expression) {
                identity << ":expression="
                         << actual.expression->value();
            } else if (actual.type) {
                identity << ":type="
                         << encoded(type_identity(*actual.type));
            } else {
                identity << ":default";
            }
        }
        const auto append_range = [&](const PackedRange& range) {
            identity << ":range=";
            if (range.left && range.right) {
                identity << *range.left << ':' << *range.right << ':'
                         << range.descending;
            } else {
                identity << "unresolved";
            }
        };
        if (type.packed_range) {
            append_range(*type.packed_range);
        }
        identity << ":container=";
        if (type.container_form) {
            identity << static_cast<unsigned>(*type.container_form);
        } else {
            identity << "none";
        }
        if (type.associative_index) {
            identity << ":index="
                     << encoded(type.associative_index->spelling);
        }
        for (const auto& dimension : type.unpacked_dimensions) {
            append_range(dimension);
        }
        for (const auto& element : type.container_element_types) {
            identity << ":element=" << encoded(type_identity(element));
        }
        return identity.str();
    }

    bool resolve_range(PackedRange& range, const Environment& environment,
        const std::string_view owner, const SourceSpanId source)
    {
        if (!range.left && range.left_expression) {
            const auto resolved = value(
                *range.left_expression, environment, owner, source);
            if (!resolved || !resolved->integer) {
                return false;
            }
            range.left = *resolved->integer;
        }
        if (!range.right && range.right_expression) {
            const auto resolved = value(
                *range.right_expression, environment, owner, source);
            if (!resolved || !resolved->integer) {
                return false;
            }
            range.right = *resolved->integer;
        }
        return range.left && range.right;
    }

    std::optional<TypeBinding> type(TypeReference input,
        const Environment& environment, const std::string_view owner,
        const SourceSpanId source)
    {
        if (input.target.target.valid()) {
            const auto replacement = environment.types.find(
                input.target.target);
            if (replacement != environment.types.end()) {
                return replacement->second;
            }
        }
        if (input.value_form == TypeForm::class_handle
            && !input.class_identity.empty()) {
            const auto* target = class_for(input.class_identity);
            if (target == nullptr) {
                report(ClassSpecializationErrorKind::unresolved_member,
                    std::string { owner }, source,
                    "class-handle type '" + input.class_identity
                        + "' does not resolve to compiled HIR");
                return std::nullopt;
            }
            if (!target->parameters.empty()) {
                report(ClassSpecializationErrorKind::
                        unsupported_parameterized_class_handle,
                    std::string { owner }, source,
                    "parameterized class-handle type '"
                        + input.class_identity
                        + "' has no actual-association record in compiled HIR");
                return std::nullopt;
            }
        }
        if (input.target.target.valid()
            && record_for(types_, input.target.target) == nullptr) {
            report(ClassSpecializationErrorKind::unresolved_member,
                std::string { owner }, source,
                "class member type refers to a missing HIR type record");
            return std::nullopt;
        }
        if (input.packed_range
            && !resolve_range(*input.packed_range,
                environment, owner, source)) {
            return std::nullopt;
        }
        for (auto& dimension : input.unpacked_dimensions) {
            if (!resolve_range(dimension, environment, owner, source)) {
                return std::nullopt;
            }
        }
        TypeBinding resolved { std::move(input), { } };
        resolved.canonical = type_identity(resolved.type);
        if (resolved.type.target.target.valid()) {
            const auto* definition = record_for(
                types_, resolved.type.target.target);
            if (definition == nullptr
                || declaration_identity(definition->declaration).empty()) {
                report(ClassSpecializationErrorKind::unresolved_member,
                    std::string { owner }, source,
                    "class member type has no canonical HIR identity");
                return std::nullopt;
            }
        }
        return resolved;
    }

    std::optional<std::size_t> storage_width(const TypeReference& type,
        std::set<TypeId>& active) const
    {
        if (type.value_form == TypeForm::class_handle
            || type.value_form == TypeForm::string || type.container_form
            || type.value_form == TypeForm::unpacked_structure
            || type.value_form == TypeForm::unpacked_union) {
            return 32U;
        }
        if (type.packed_range && type.packed_range->left
            && type.packed_range->right) {
            const auto left = *type.packed_range->left;
            const auto right = *type.packed_range->right;
            const auto distance = left >= right
                ? static_cast<std::uint64_t>(left - right)
                : static_cast<std::uint64_t>(right - left);
            if (distance >= std::numeric_limits<std::size_t>::max()) {
                return std::nullopt;
            }
            return static_cast<std::size_t>(distance + 1U);
        }
        if (type.executable_width) {
            if (*type.executable_width
                > std::numeric_limits<std::size_t>::max()) {
                return std::nullopt;
            }
            return static_cast<std::size_t>(*type.executable_width);
        }
        if (!type.target.target.valid()
            || !active.insert(type.target.target).second) {
            return std::nullopt;
        }
        const auto* definition = record_for(types_, type.target.target);
        if (definition == nullptr) {
            return std::nullopt;
        }
        std::optional<std::size_t> result;
        if (!definition->members.empty()) {
            std::size_t total { };
            bool valid = true;
            for (const auto& member : definition->members) {
                const auto width = storage_width(member.type, active);
                if (!width
                    || *width > std::numeric_limits<std::size_t>::max()
                            - total) {
                    valid = false;
                    break;
                }
                total += *width;
            }
            if (valid) {
                result = total;
            }
        } else {
            result = storage_width(definition->base, active);
        }
        active.erase(type.target.target);
        return result;
    }

    std::optional<std::size_t> storage_width(
        const TypeReference& type) const
    {
        std::set<TypeId> active;
        return storage_width(type, active);
    }

    std::optional<BoundParameters> bind(
        const ClassDeclaration& declaration,
        const std::span<const ResolvedActual> actuals)
    {
        const auto identity = class_declaration_identity(declaration);
        std::vector<std::optional<ResolvedActual>> assigned(
            declaration.parameters.size());
        bool named { };
        bool positional { };
        for (std::size_t index = 0; index < actuals.size(); ++index) {
            const auto& actual = actuals[index];
            std::size_t formal_index = index;
            if (actual.formal) {
                named = true;
                const auto found = std::ranges::find(
                    declaration.parameters, *actual.formal,
                    &ClassParameter::name);
                if (found == declaration.parameters.end()) {
                    report(ClassSpecializationErrorKind::unknown_parameter,
                        identity, actual.source,
                        "class '" + identity + "' has no parameter named '"
                            + *actual.formal + "'");
                    return std::nullopt;
                }
                formal_index = static_cast<std::size_t>(std::distance(
                    declaration.parameters.begin(), found));
            } else {
                positional = true;
            }
            if (formal_index >= declaration.parameters.size()) {
                report(ClassSpecializationErrorKind::unknown_parameter,
                    identity, actual.source,
                    "too many parameter actuals for class '" + identity + "'");
                return std::nullopt;
            }
            if (assigned[formal_index]) {
                report(ClassSpecializationErrorKind::duplicate_parameter,
                    identity, actual.source,
                    "duplicate actual for class parameter '"
                        + declaration.parameters[formal_index].name + "'");
                return std::nullopt;
            }
            assigned[formal_index] = actual;
        }
        if (named && positional) {
            report(ClassSpecializationErrorKind::mixed_parameter_style,
                identity, declaration.source,
                "named and positional class parameter actuals cannot be mixed");
            return std::nullopt;
        }

        BoundParameters result;
        std::ostringstream specialization;
        specialization << identity << '<';
        for (std::size_t index = 0;
            index < declaration.parameters.size(); ++index) {
            const auto& parameter = declaration.parameters[index];
            const auto actual = assigned[index];
            SpecializedClassParameter retained;
            retained.declaration = parameter.declaration;
            retained.name = parameter.name;
            retained.type_parameter = parameter.type_parameter;
            retained.source = parameter.source;
            retained.origin = parameter.origin;
            if (parameter.type_parameter) {
                std::optional<TypeBinding> resolved;
                if (actual && actual->kind != ActualKind::default_value) {
                    if (actual->kind != ActualKind::type) {
                        report(ClassSpecializationErrorKind::incompatible_actual,
                            identity, actual->source,
                            "type parameter '" + parameter.name
                                + "' requires a type actual");
                        return std::nullopt;
                    }
                    resolved = actual->resolved_type;
                    if (!resolved && actual->type) {
                        resolved = type(*actual->type, result.environment,
                            identity, actual->source);
                    }
                } else if (parameter.default_type) {
                    resolved = type(*parameter.default_type,
                        result.environment, identity, parameter.source);
                }
                if (!resolved) {
                    report(ClassSpecializationErrorKind::missing_parameter,
                        identity, parameter.source,
                        "type parameter '" + parameter.name
                            + "' has no resolvable actual or default");
                    return std::nullopt;
                }
                retained.type = resolved->type;
                retained.display_identity = resolved->canonical;
                retained.canonical_identity = "type{" + resolved->canonical
                    + "}";
                const auto* member = record_for(
                    declarations_, parameter.declaration);
                if (member != nullptr && member->declared_type) {
                    result.environment.types.insert_or_assign(
                        *member->declared_type, *resolved);
                }
            } else {
                std::optional<ValueBinding> resolved;
                if (actual && actual->kind != ActualKind::default_value) {
                    if (actual->kind != ActualKind::expression) {
                        report(ClassSpecializationErrorKind::incompatible_actual,
                            identity, actual->source,
                            "value parameter '" + parameter.name
                                + "' requires an expression actual");
                        return std::nullopt;
                    }
                    resolved = actual->resolved_value;
                    if (!resolved && actual->expression) {
                        resolved = value(*actual->expression,
                            result.environment, identity, actual->source);
                    }
                } else if (parameter.default_value) {
                    resolved = value(*parameter.default_value,
                        result.environment, identity, parameter.source);
                }
                if (!resolved) {
                    report(ClassSpecializationErrorKind::missing_parameter,
                        identity, parameter.source,
                        "value parameter '" + parameter.name
                            + "' has no locally constant actual or default");
                    return std::nullopt;
                }
                retained.expression = resolved->expression;
                retained.display_identity = resolved->display;
                retained.canonical_identity = "value{" + resolved->canonical
                    + "}";
                result.environment.values.insert_or_assign(
                    parameter.declaration, *resolved);
            }
            specialization << encoded(parameter.name) << '='
                           << encoded(retained.canonical_identity) << ';';
            result.parameters.push_back(std::move(retained));
        }
        specialization << '>';
        result.specialization_identity = specialization.str();
        return result;
    }

    std::optional<std::string> specialize_relation(
        const ClassDeclaration& owner, const ClassRelation& relation,
        const RelationKind kind, const Environment& environment)
    {
        const auto* target = relation_target(relation, owner);
        if (target == nullptr) {
            return std::nullopt;
        }
        const bool interface_relation = kind != RelationKind::base;
        if (interface_relation != target->interface_class
            || (kind == RelationKind::extended_interface
                && !owner.interface_class)
            || (kind == RelationKind::implemented_interface
                && owner.interface_class)) {
            report(ClassSpecializationErrorKind::incompatible_relation,
                class_declaration_identity(owner), relation.source,
                "class relation '" + relation.name
                    + "' has an incompatible class/interface kind");
            return std::nullopt;
        }
        std::vector<ResolvedActual> actuals;
        actuals.reserve(relation.actuals.size());
        for (std::size_t index = 0; index < relation.actuals.size(); ++index) {
            const auto& actual = relation.actuals[index];
            ResolvedActual retained { actual.formal, actual.kind,
                actual.expression, actual.type, std::nullopt,
                std::nullopt, actual.source };
            const ClassParameter* formal { };
            if (actual.formal) {
                const auto found = std::ranges::find(
                    target->parameters, *actual.formal,
                    &ClassParameter::name);
                if (found != target->parameters.end())
                    formal = &*found;
            } else if (index < target->parameters.size()) {
                formal = &target->parameters[index];
            }
            if (formal != nullptr && formal->type_parameter
                && retained.kind == ActualKind::expression
                && retained.expression) {
                const auto* expression = record_for(
                    expressions_, *retained.expression);
                const auto* declaration = expression != nullptr
                        && expression->referenced_name
                        && expression->referenced_name->selected
                    ? record_for(declarations_,
                          *expression->referenced_name->selected)
                    : nullptr;
                if (declaration != nullptr && declaration->declared_type) {
                    TypeReference type_actual;
                    type_actual.target.target = *declaration->declared_type;
                    type_actual.target.source = expression->source;
                    type_actual.target.spelling = expression->text;
                    retained.kind = ActualKind::type;
                    retained.type = std::move(type_actual);
                    retained.expression.reset();
                }
            }
            if (actual.kind == ActualKind::expression
                && retained.kind == ActualKind::expression
                && retained.expression) {
                retained.resolved_value = value(*retained.expression,
                    environment, class_declaration_identity(owner),
                    actual.source);
                if (!retained.resolved_value) {
                    return std::nullopt;
                }
            } else if (retained.kind == ActualKind::type && retained.type) {
                retained.resolved_type = type(*retained.type, environment,
                    class_declaration_identity(owner), actual.source);
                if (!retained.resolved_type) {
                    return std::nullopt;
                }
            }
            actuals.push_back(std::move(retained));
        }
        return specialize(*target, actuals, relation.source);
    }

    bool expression_references_resolve(
        const ExpressionId id, std::set<ExpressionId>& active) const
    {
        const auto* expression = record_for(expressions_, id);
        if (expression == nullptr) {
            return false;
        }
        if (!active.insert(id).second) {
            return true;
        }
        const auto valid_id = [&](const ExpressionId nested) {
            return expression_references_resolve(nested, active);
        };
        bool valid = std::ranges::all_of(expression->operands, valid_id);
        for (const auto& argument : expression->call_arguments) {
            valid = valid && (!argument.actual || valid_id(*argument.actual));
        }
        for (const auto& association : expression->associations) {
            valid = valid && valid_id(association.value)
                && std::ranges::all_of(association.choices, valid_id);
        }
        if (expression->referenced_name) {
            if (expression->referenced_name->selected) {
                valid = valid && record_for(declarations_,
                    *expression->referenced_name->selected) != nullptr;
            }
            valid = valid && std::ranges::all_of(
                expression->referenced_name->overloads,
                [&](const DeclarationId declaration) {
                    return record_for(declarations_, declaration) != nullptr;
                });
        }
        if (!expression->class_member_identity.empty()
            && !expression->class_member_identity.starts_with('@')) {
            valid = valid && std::ranges::any_of(
                hir_.classes(), [&](const ClassDeclaration& declaration) {
                    return std::ranges::any_of(
                               declaration.properties,
                               [&](const ClassProperty& property) {
                                   return property.canonical_identity
                                       == expression->class_member_identity;
                               })
                        || std::ranges::any_of(
                            declaration.methods,
                            [&](const ClassMethod& method) {
                                return method.canonical_identity
                                    == expression->class_member_identity;
                            });
                });
        }
        active.erase(id);
        return valid;
    }

    bool declaration_references_resolve(
        const DeclarationId id, std::set<DeclarationId>& active,
        std::set<StatementId>& statement_active) const
    {
        const auto* declaration = record_for(declarations_, id);
        if (declaration == nullptr) {
            return false;
        }
        if (!active.insert(id).second) {
            return true;
        }
        std::set<ExpressionId> expression_active;
        const auto expression = [&](const std::optional<ExpressionId> value) {
            return !value
                || expression_references_resolve(*value, expression_active);
        };
        const auto type_resolves = [&](const TypeReference& type) {
            const auto range_resolves = [&](const PackedRange& range) {
                return expression(range.left_expression)
                    && expression(range.right_expression);
            };
            return (!type.target.target.valid()
                    || record_for(types_, type.target.target) != nullptr)
                && (!type.packed_range
                    || range_resolves(*type.packed_range))
                && std::ranges::all_of(
                    type.unpacked_dimensions, range_resolves)
                && expression(type.queue_maximum);
        };
        bool valid = expression(declaration->initializer)
            && (!declaration->type || type_resolves(*declaration->type))
            && (!declaration->default_type
                || type_resolves(*declaration->default_type));
        if (declaration->callable) {
            valid = valid
                && type_resolves(declaration->callable->return_type)
                && std::ranges::all_of(
                    declaration->callable->formals,
                    [&](const DeclarationId formal) {
                        return declaration_references_resolve(
                            formal, active, statement_active);
                    });
        }
        valid = valid && std::ranges::all_of(
            declaration->children, [&](const DeclarationId child) {
                return declaration_references_resolve(
                    child, active, statement_active);
            });
        valid = valid && std::ranges::all_of(
            declaration->statements, [&](const StatementId statement) {
                return statement_references_resolve(
                    statement, statement_active);
            });
        active.erase(id);
        return valid;
    }

    bool statement_references_resolve(
        const StatementId id, std::set<StatementId>& active) const
    {
        const auto* statement = record_for(statements_, id);
        if (statement == nullptr) {
            return false;
        }
        if (!active.insert(id).second) {
            return true;
        }
        std::set<ExpressionId> expression_active;
        const auto expression = [&](const std::optional<ExpressionId> value) {
            return !value
                || expression_references_resolve(*value, expression_active);
        };
        bool valid = expression(statement->target)
            && expression(statement->value)
            && expression(statement->condition)
            && expression(statement->loop_initial)
            && expression(statement->loop_limit)
            && expression(statement->loop_update_target)
            && expression(statement->clocking_cycle_count)
            && expression(statement->file_handle);
        std::function<bool(const Delay&)> delay_resolves;
        delay_resolves = [&](const Delay& delay) {
            const auto delay_value = [&](const DelayValue& value) {
                return expression(value.expression);
            };
            return delay_value(delay.primary)
                && (!delay.minimum || delay_value(*delay.minimum))
                && (!delay.typical || delay_value(*delay.typical))
                && (!delay.maximum || delay_value(*delay.maximum))
                && std::ranges::all_of(delay.additional, delay_resolves);
        };
        valid = valid && (!statement->delay
            || delay_resolves(*statement->delay));
        for (const auto& sensitivity : statement->sensitivities) {
            valid = valid && expression(sensitivity.expression);
        }
        for (const auto& argument : statement->task_arguments) {
            valid = valid && expression(argument.actual);
        }
        for (const auto& output : statement->output_values) {
            valid = valid && expression(output.value);
        }
        for (const auto declaration : statement->declarations) {
            valid = valid && record_for(declarations_, declaration) != nullptr;
        }
        const auto nested = [&](const StatementId child) {
            return statement_references_resolve(child, active);
        };
        valid = valid && std::ranges::all_of(statement->loop_updates, nested)
            && std::ranges::all_of(statement->statements, nested)
            && std::ranges::all_of(statement->else_statements, nested);
        for (const auto& alternative : statement->case_alternatives) {
            valid = valid
                && std::ranges::all_of(alternative.choices,
                    [&](const ExpressionId choice) {
                        return expression(choice);
                    })
                && std::ranges::all_of(alternative.statements, nested);
        }
        active.erase(id);
        return valid;
    }

    std::optional<SpecializedClassMethod> method(
        const ClassDeclaration& owner, const ClassMethod& input,
        const Environment& environment,
        const std::optional<std::uint32_t> slot)
    {
        const auto identity = class_declaration_identity(owner);
        const auto* declaration = record_for(
            declarations_, input.declaration);
        if (declaration == nullptr || !declaration->callable) {
            report(ClassSpecializationErrorKind::unresolved_member,
                identity, input.source,
                "class method '" + input.canonical_identity
                    + "' has no callable HIR declaration");
            return std::nullopt;
        }
        SpecializedClassMethod output;
        output.declaration = input.declaration;
        output.name = input.name;
        output.canonical_identity = input.canonical_identity;
        output.owner_identity = input.owner_identity;
        output.declared_profile_identity = input.profile_identity;
        output.kind = input.kind;
        output.visibility = input.visibility;
        output.virtual_slot = slot;
        output.static_method = input.static_method;
        output.virtual_method = input.virtual_method || slot.has_value();
        output.pure = input.pure;
        output.final_method = input.final_method;
        output.external = input.external;
        output.defined = input.defined;
        output.source = input.source;
        output.origin = input.origin;
        if (declaration->callable->function) {
            const auto resolved = type(declaration->callable->return_type,
                environment, identity, input.source);
            if (!resolved) {
                return std::nullopt;
            }
            output.return_type = resolved->type;
        }
        output.formals = declaration->callable->formals;
        for (const auto formal : output.formals) {
            const auto* formal_declaration = record_for(declarations_, formal);
            if (formal_declaration == nullptr || !formal_declaration->type) {
                report(ClassSpecializationErrorKind::unresolved_member,
                    identity, input.source,
                    "class method formal has no typed HIR declaration");
                return std::nullopt;
            }
            const auto resolved = type(*formal_declaration->type,
                environment, identity, formal_declaration->source);
            if (!resolved) {
                return std::nullopt;
            }
            output.formal_types.push_back(resolved->type);
        }
        output.local_declarations = declaration->children;
        for (const auto child : output.local_declarations) {
            std::set<DeclarationId> declaration_active;
            std::set<StatementId> statement_active;
            if (!declaration_references_resolve(
                    child, declaration_active, statement_active)) {
                report(ClassSpecializationErrorKind::unresolved_member,
                    identity, input.source,
                    "class method has an unresolved local declaration closure");
                return std::nullopt;
            }
        }
        output.statements = declaration->statements;
        std::set<StatementId> active;
        if (!std::ranges::all_of(output.statements,
                [&](const StatementId statement) {
                    return statement_references_resolve(statement, active);
                })) {
            report(ClassSpecializationErrorKind::unresolved_member,
                identity, input.source,
                "class method body contains an unresolved HIR reference");
            return std::nullopt;
        }
        std::ostringstream callable;
        callable << input.profile_identity << "(";
        for (std::size_t index = 0;
            index < output.formal_types.size(); ++index) {
            callable << static_cast<unsigned>(
                record_for(declarations_, output.formals[index])->direction)
                     << ':' << encoded(type_identity(
                            output.formal_types[index])) << ';';
        }
        callable << ")->";
        if (output.return_type) {
            callable << encoded(type_identity(*output.return_type));
        } else {
            callable << "task";
        }
        output.callable_identity = callable.str();
        return output;
    }

    std::map<std::string, std::uint32_t> virtual_slots(
        const ClassDeclaration& declaration)
    {
        const auto identity = class_declaration_identity(declaration);
        if (const auto found = slot_tables_.find(identity);
            found != slot_tables_.end()) {
            return found->second;
        }
        if (!active_slot_tables_.insert(identity).second) {
            report(ClassSpecializationErrorKind::inheritance_cycle,
                identity, declaration.source,
                "class virtual-method inheritance is cyclic");
            return { };
        }
        std::map<std::string, std::uint32_t> slots;
        if (declaration.base) {
            if (const auto* base = relation_target(
                    *declaration.base, declaration)) {
                slots = virtual_slots(*base);
            }
        }
        std::uint32_t next { };
        for (const auto& [profile, slot] : slots) {
            (void)profile;
            if (slot == std::numeric_limits<std::uint32_t>::max()) {
                next = slot;
                break;
            }
            next = std::max(next, static_cast<std::uint32_t>(slot + 1U));
        }
        for (const auto& method : declaration.methods) {
            if (method.kind == ClassMethodKind::constructor) {
                continue;
            }
            if (slots.contains(method.profile_identity)) {
                continue;
            }
            if (!method.virtual_method && !method.pure) {
                continue;
            }
            if (next == std::numeric_limits<std::uint32_t>::max()) {
                report(ClassSpecializationErrorKind::layout_overflow,
                    identity, method.source,
                    "class virtual-method slot domain is exhausted");
                continue;
            }
            slots.emplace(method.profile_identity, next++);
        }
        active_slot_tables_.erase(identity);
        slot_tables_.emplace(identity, slots);
        return slots;
    }

    void add_source_dependencies(ClassSpecialization& output,
        const ClassDeclaration& declaration) const
    {
        if (declaration.source.valid()
            && declaration.source.value()
                < semantics_.source_spans().size()) {
            const auto& span = semantics_.source_spans()[
                declaration.source.value()];
            if (!span.logical_name.empty()) {
                output.source_dependencies.push_back(span.logical_name);
            }
        }
        if (!declaration.scope.valid()
            || declaration.scope.value() >= semantics_.scopes().size()) {
            return;
        }
        const auto unit_id = semantics_.scopes()[
            declaration.scope.value()].unit;
        const auto found = units_.find(unit_id);
        if (found == units_.end()) {
            return;
        }
        output.source_dependencies.insert(output.source_dependencies.end(),
            found->second->source_dependencies.begin(),
            found->second->source_dependencies.end());
    }

    const ClassConstraint* constraint_for(
        const std::string_view identity) const
    {
        for (const auto& [class_identity, declaration] : classes_) {
            (void)class_identity;
            const auto found = std::ranges::find(
                declaration->constraints, identity,
                &ClassConstraint::canonical_identity);
            if (found != declaration->constraints.end()) {
                return &*found;
            }
        }
        return nullptr;
    }

    bool constraint_expression_resolves(
        const ConstraintExpression& expression) const
    {
        for (const auto& binding : expression.bindings) {
            if (binding.canonical_identity.empty()) {
                return false;
            }
            if (binding.kind == ConstraintReferenceKind::property) {
                const auto found = std::ranges::any_of(
                    hir_.classes(), [&](const ClassDeclaration& declaration) {
                        return std::ranges::any_of(
                            declaration.properties,
                            [&](const ClassProperty& property) {
                                return property.canonical_identity
                                    == binding.canonical_identity;
                            });
                    });
                if (!found) {
                    return false;
                }
            }
            if (binding.kind == ConstraintReferenceKind::method) {
                const auto found = std::ranges::any_of(
                    hir_.classes(), [&](const ClassDeclaration& declaration) {
                        return std::ranges::any_of(
                            declaration.methods,
                            [&](const ClassMethod& method) {
                                return method.canonical_identity
                                    == binding.canonical_identity;
                            });
                    });
                if (!found) {
                    return false;
                }
            }
        }
        return std::ranges::all_of(expression.operands,
            [&](const ConstraintExpression& operand) {
                return constraint_expression_resolves(operand);
            });
    }

    bool add_interface_closure(ClassSpecialization& output,
        const SpecializedClassRelation& relation,
        std::map<std::string, SpecializedClassRelation>& closure)
    {
        const auto found = closure.find(relation.declaration_identity);
        if (found != closure.end()) {
            if (found->second.specialization_identity
                != relation.specialization_identity) {
                report(ClassSpecializationErrorKind::ambiguous_interface,
                    output.declaration_identity, relation.source,
                    "class inherits distinct specializations of interface '"
                        + relation.declaration_identity + "'");
                return false;
            }
            return true;
        }
        closure.emplace(relation.declaration_identity, relation);
        const auto materialized = materialized_.find(
            relation.specialization_identity);
        if (materialized == materialized_.end()) {
            return false;
        }
        const auto& related = result_.specializations[materialized->second];
        output.source_dependencies.insert(output.source_dependencies.end(),
            related.source_dependencies.begin(),
            related.source_dependencies.end());
        for (const auto& inherited : related.interfaces) {
            if (!add_interface_closure(output, inherited, closure)) {
                return false;
            }
        }
        return true;
    }

    std::optional<std::string> specialize(
        const ClassDeclaration& declaration,
        const std::span<const ResolvedActual> actuals,
        const SourceSpanId reference_source)
    {
        const auto identity = class_declaration_identity(declaration);
        if (declaration.forward_declaration) {
            report(ClassSpecializationErrorKind::forward_declaration,
                identity, reference_source,
                "forward class declaration '" + identity
                    + "' has no compiled definition");
            return std::nullopt;
        }
        auto bound = bind(declaration, actuals);
        if (!bound) {
            return std::nullopt;
        }
        if (materialized_.contains(bound->specialization_identity)) {
            return bound->specialization_identity;
        }
        if (!active_.insert(bound->specialization_identity).second) {
            report(ClassSpecializationErrorKind::inheritance_cycle,
                identity, reference_source,
                "recursive class specialization '"
                    + bound->specialization_identity + "'");
            return std::nullopt;
        }

        ClassSpecialization output;
        output.declaration_scope = declaration.scope;
        output.declaration_identity = identity;
        output.specialization_identity = bound->specialization_identity;
        output.parameters = bound->parameters;
        output.source = declaration.source;
        output.origin = declaration.origin;
        add_source_dependencies(output, declaration);

        if (declaration.base) {
            const auto related = specialize_relation(declaration,
                *declaration.base, RelationKind::base,
                bound->environment);
            if (!related) {
                active_.erase(bound->specialization_identity);
                return std::nullopt;
            }
            const auto* base_declaration = relation_target(
                *declaration.base, declaration);
            output.base = SpecializedClassRelation {
                class_declaration_identity(*base_declaration), *related,
                declaration.base->source, declaration.base->origin
            };
            const auto base = materialized_.find(*related);
            if (base == materialized_.end()) {
                active_.erase(bound->specialization_identity);
                return std::nullopt;
            }
            const auto& specialization = result_.specializations[base->second];
            output.instance_bit_width = specialization.instance_bit_width;
            for (const auto& property : specialization.properties) {
                if (!property.static_storage) {
                    output.properties.push_back(property);
                }
            }
            output.methods = specialization.methods;
            output.constraints = specialization.constraints;
            output.covergroups = specialization.covergroups;
            output.source_dependencies.insert(output.source_dependencies.end(),
                specialization.source_dependencies.begin(),
                specialization.source_dependencies.end());
        }

        std::vector<SpecializedClassRelation> direct_interfaces;
        const auto retain_relation = [&](const ClassRelation& relation,
                                         const RelationKind kind) {
            const auto specialized = specialize_relation(
                declaration, relation, kind, bound->environment);
            if (!specialized) {
                return false;
            }
            const auto* related = relation_target(relation, declaration);
            direct_interfaces.push_back({ class_declaration_identity(*related),
                *specialized, relation.source, relation.origin });
            return true;
        };
        for (const auto& relation : declaration.extended_interfaces) {
            if (!retain_relation(
                    relation, RelationKind::extended_interface)) {
                active_.erase(bound->specialization_identity);
                return std::nullopt;
            }
        }
        for (const auto& relation : declaration.implemented_interfaces) {
            if (!retain_relation(
                    relation, RelationKind::implemented_interface)) {
                active_.erase(bound->specialization_identity);
                return std::nullopt;
            }
        }
        std::map<std::string, SpecializedClassRelation> interface_closure;
        if (output.base) {
            const auto& base = result_.specializations[
                materialized_.at(output.base->specialization_identity)];
            for (const auto& inherited : base.interfaces) {
                if (!add_interface_closure(
                        output, inherited, interface_closure)) {
                    active_.erase(bound->specialization_identity);
                    return std::nullopt;
                }
            }
        }
        for (const auto& relation : direct_interfaces) {
            if (!add_interface_closure(output, relation, interface_closure)) {
                active_.erase(bound->specialization_identity);
                return std::nullopt;
            }
        }
        for (const auto& [interface_identity, relation] : interface_closure) {
            (void)interface_identity;
            output.interfaces.push_back(relation);
        }

        for (const auto& property : declaration.properties) {
            const auto* member = record_for(
                declarations_, property.declaration);
            std::set<ExpressionId> initializer_active;
            if (member == nullptr || !member->type
                || (property.initializer
                    && !expression_references_resolve(
                        *property.initializer, initializer_active))) {
                report(ClassSpecializationErrorKind::unresolved_member,
                    identity, property.source,
                    "class property '" + property.canonical_identity
                        + "' has an unresolved HIR member");
                active_.erase(bound->specialization_identity);
                return std::nullopt;
            }
            const auto specialized_type = type(property.type,
                bound->environment, identity, property.source);
            if (!specialized_type) {
                active_.erase(bound->specialization_identity);
                return std::nullopt;
            }
            const auto width = storage_width(specialized_type->type);
            if (!width) {
                report(ClassSpecializationErrorKind::unresolved_member,
                    identity, property.source,
                    "class property '" + property.canonical_identity
                        + "' has no finite specialized layout");
                active_.erase(bound->specialization_identity);
                return std::nullopt;
            }
            SpecializedClassProperty retained;
            retained.declaration = property.declaration;
            retained.name = property.name;
            retained.canonical_identity = property.canonical_identity;
            retained.owner_identity = property.owner_identity;
            retained.type = specialized_type->type;
            retained.initializer = property.initializer;
            retained.bit_width = *width;
            retained.visibility = property.visibility;
            retained.random_kind = property.random_kind;
            retained.static_storage = property.static_storage;
            retained.constant = property.constant;
            retained.parameter = property.parameter;
            retained.source = property.source;
            retained.origin = property.origin;
            if (property.static_storage) {
                ++output.static_property_count;
            } else {
                if (*width > std::numeric_limits<std::size_t>::max()
                        - output.instance_bit_width) {
                    report(ClassSpecializationErrorKind::layout_overflow,
                        identity, property.source,
                        "class instance layout exceeds host storage");
                    active_.erase(bound->specialization_identity);
                    return std::nullopt;
                }
                retained.bit_offset = output.instance_bit_width;
                output.instance_bit_width += *width;
            }
            output.properties.push_back(std::move(retained));
        }

        const auto slots = virtual_slots(declaration);
        for (const auto& input : declaration.methods) {
            std::optional<std::uint32_t> slot;
            if (const auto found = slots.find(input.profile_identity);
                found != slots.end()) {
                slot = found->second;
            }
            auto retained = method(
                declaration, input, bound->environment, slot);
            if (!retained) {
                active_.erase(bound->specialization_identity);
                return std::nullopt;
            }
            const auto inherited = std::ranges::find(
                output.methods, retained->declared_profile_identity,
                &SpecializedClassMethod::declared_profile_identity);
            if (inherited == output.methods.end()) {
                output.methods.push_back(std::move(*retained));
            } else {
                *inherited = std::move(*retained);
            }
        }

        for (const auto& composed : declaration.composed_constraints) {
            const auto* selected = constraint_for(
                composed.selected_identity);
            if (selected == nullptr) {
                report(ClassSpecializationErrorKind::unresolved_member,
                    identity, declaration.source,
                    "composed class constraint '" + composed.name
                        + "' has no selected HIR declaration");
                active_.erase(bound->specialization_identity);
                return std::nullopt;
            }
            if (!std::ranges::all_of(selected->expressions,
                    [&](const ConstraintExpression& expression) {
                        return constraint_expression_resolves(expression);
                    })) {
                report(ClassSpecializationErrorKind::unresolved_member,
                    identity, selected->source,
                    "class constraint '" + selected->canonical_identity
                        + "' has an unresolved member binding");
                active_.erase(bound->specialization_identity);
                return std::nullopt;
            }
            const auto inherited = std::ranges::find(
                output.constraints, composed.name,
                &SpecializedClassConstraint::name);
            SpecializedClassConstraint retained { composed.name,
                composed.selected_identity, composed.mode_enabled,
                selected->source, selected->origin };
            if (inherited == output.constraints.end()) {
                output.constraints.push_back(std::move(retained));
            } else {
                *inherited = std::move(retained);
            }
        }
        if (declaration.composed_constraints.empty()) {
            for (const auto& constraint : declaration.constraints) {
                const auto inherited = std::ranges::find(
                    output.constraints, constraint.name,
                    &SpecializedClassConstraint::name);
                SpecializedClassConstraint retained { constraint.name,
                    constraint.canonical_identity, true,
                    constraint.source, constraint.origin };
                if (inherited == output.constraints.end()) {
                    output.constraints.push_back(std::move(retained));
                } else {
                    *inherited = std::move(retained);
                }
            }
        }
        for (const auto& covergroup : declaration.covergroups) {
            output.covergroups.push_back({ covergroup.canonical_identity,
                covergroup.runtime_identity_prefix,
                covergroup.source, covergroup.origin });
        }

        std::ranges::sort(output.source_dependencies);
        output.source_dependencies.erase(std::unique(
            output.source_dependencies.begin(),
            output.source_dependencies.end()),
            output.source_dependencies.end());
        active_.erase(bound->specialization_identity);
        const auto specialization_identity = output.specialization_identity;
        materialized_.emplace(
            specialization_identity, result_.specializations.size());
        result_.specializations.push_back(std::move(output));
        return specialization_identity;
    }

    const Model& semantics_;
    const Hir& hir_;
    std::map<std::string, const ClassDeclaration*, std::less<>> classes_;
    std::map<DeclarationId, const Declaration*> declarations_;
    std::map<TypeId, const TypeDefinition*> types_;
    std::map<ExpressionId, const Expression*> expressions_;
    std::map<StatementId, const Statement*> statements_;
    std::map<UnitId, const Unit*> units_;
    std::map<std::string, std::size_t, std::less<>> materialized_;
    std::set<std::string, std::less<>> active_;
    std::map<std::string, std::map<std::string, std::uint32_t>, std::less<>>
        slot_tables_;
    std::set<std::string, std::less<>> active_slot_tables_;
    ClassSpecializationResult result_;
};

} // namespace

ClassSpecializationResult specialize_classes(
    const CompiledDesign& design,
    const std::span<const ClassSpecializationRequest> requests)
{
    return specialize_classes(
        design.semantics, design.systemverilog_hir, requests);
}

ClassSpecializationResult specialize_classes(
    const Model& semantics,
    const Hir& hir,
    const std::span<const ClassSpecializationRequest> requests)
{
    return ClassSpecializer { semantics, hir }.run(requests);
}

} // namespace fsim::semantic::sv
