// SPDX-License-Identifier: Apache-2.0
#include "application_systemverilog_hir_internal.hpp"

#include <set>
#include <sstream>

namespace fsim::app::application_detail {
namespace systemverilog_hir_detail {

namespace sv = semantic::sv;

namespace {

sv::ClassLifetime class_lifetime(
    const frontend::SystemVerilogClassLifetime lifetime) noexcept
{
    switch (lifetime) {
    case frontend::SystemVerilogClassLifetime::Static:
        return sv::ClassLifetime::static_lifetime;
    case frontend::SystemVerilogClassLifetime::Automatic:
        return sv::ClassLifetime::automatic;
    case frontend::SystemVerilogClassLifetime::Inherited:
        return sv::ClassLifetime::inherited;
    }
    return sv::ClassLifetime::inherited;
}

sv::ClassMethodKind class_method_kind(
    const frontend::SystemVerilogClassMethodKind kind) noexcept
{
    switch (kind) {
    case frontend::SystemVerilogClassMethodKind::Constructor:
        return sv::ClassMethodKind::constructor;
    case frontend::SystemVerilogClassMethodKind::Task:
        return sv::ClassMethodKind::task;
    case frontend::SystemVerilogClassMethodKind::Function:
        return sv::ClassMethodKind::function;
    }
    return sv::ClassMethodKind::function;
}

std::string type_signature(const frontend::Type& type)
{
    if (!type.systemverilog_class_declaration.empty()) {
        return type.systemverilog_class_declaration;
    }
    if (!type.named_type.empty()) {
        return type.named_type;
    }
    if (!type.spelling.empty()) {
        return type.spelling;
    }
    if (const auto width = type.width()) {
        return std::to_string(*width);
    }
    return "?";
}

std::string method_profile_identity(
    const frontend::SystemVerilogClassMethod& method)
{
    std::ostringstream output;
    output << static_cast<unsigned>(method.kind) << ':' << method.name << '(';
    for (const auto& argument : method.arguments) {
        output << static_cast<unsigned>(argument.direction) << ':'
               << type_signature(argument.type) << ';';
    }
    output << ")->" << type_signature(method.return_type);
    return output.str();
}

frontend::TaskArgument task_argument(
    const frontend::FunctionArgument& input)
{
    return { input.name, input.type, input.direction, input.span,
        input.reference, input.default_value, input.const_reference,
        input.static_reference };
}

} // namespace

frontend::FunctionDeclaration class_function(
    const frontend::SystemVerilogClassMethod& input)
{
    frontend::FunctionDeclaration output;
    output.name = input.name;
    output.return_type = input.return_type;
    output.arguments = input.arguments;
    output.type_aliases = input.type_aliases;
    output.variables = input.variables;
    output.statements = input.statements;
    output.automatic = true;
    output.lifetime_explicit = true;
    output.pure = input.is_pure;
    output.defined = input.defined;
    output.span = input.span;
    return output;
}

frontend::TaskDeclaration class_task(
    const frontend::SystemVerilogClassMethod& input)
{
    frontend::TaskDeclaration output;
    output.name = input.name;
    output.type_aliases = input.type_aliases;
    output.variables = input.variables;
    output.statements = input.statements;
    output.automatic = true;
    output.lifetime_explicit = true;
    output.span = input.span;
    for (const auto& argument : input.arguments) {
        output.arguments.push_back(task_argument(argument));
    }
    return output;
}

sv::ClassVisibility SystemVerilogHirBuilder::class_visibility(
    const frontend::SystemVerilogClassVisibility visibility) noexcept
{
    switch (visibility) {
    case frontend::SystemVerilogClassVisibility::Public:
        return sv::ClassVisibility::public_access;
    case frontend::SystemVerilogClassVisibility::Protected:
        return sv::ClassVisibility::protected_access;
    case frontend::SystemVerilogClassVisibility::Local:
        return sv::ClassVisibility::local_access;
    }
    return sv::ClassVisibility::public_access;
}

sv::ConstraintExpressionKind SystemVerilogHirBuilder::constraint_kind(
    const frontend::Expression& input) noexcept
{
    using FrontendKind = frontend::ExpressionKind;
    using HirKind = sv::ConstraintExpressionKind;
    switch (input.kind) {
    case FrontendKind::Invalid:
        return HirKind::invalid;
    case FrontendKind::Identifier:
        return HirKind::name;
    case FrontendKind::IntegerLiteral:
        return HirKind::integer_literal;
    case FrontendKind::BooleanLiteral:
        return HirKind::boolean_literal;
    case FrontendKind::LogicLiteral:
        return HirKind::logic_literal;
    case FrontendKind::StringLiteral:
        return HirKind::string_literal;
    case FrontendKind::Unary:
    case FrontendKind::Update:
        return HirKind::unary;
    case FrontendKind::Binary:
        return HirKind::binary;
    case FrontendKind::Index:
        return HirKind::index;
    case FrontendKind::Slice:
        return HirKind::slice;
    case FrontendKind::Aggregate:
    case FrontendKind::DefaultChoice:
        return HirKind::assignment_pattern;
    case FrontendKind::Concatenation:
        return HirKind::concatenation;
    case FrontendKind::Replication:
        return HirKind::replication;
    case FrontendKind::Conditional:
        return HirKind::conditional;
    case FrontendKind::Call:
        if (input.text == "?:")
            return HirKind::conditional;
        if (input.text == "inside")
            return HirKind::inside_set;
        if (input.text == "@inside-range")
            return HirKind::inside_range;
        if (input.text == "dist")
            return HirKind::distribution;
        if (input.text == "@dist-:=" || input.text == "@dist-:/")
            return HirKind::distribution_item;
        if (input.text == "soft")
            return HirKind::soft;
        if (input.text == "@constraint-block")
            return HirKind::constraint_block;
        if (input.text == "@constraint-implies")
            return HirKind::implication;
        if (input.text == "@constraint-if")
            return HirKind::conditional_constraint;
        if (input.text == "@constraint-foreach")
            return HirKind::foreach_constraint;
        if (input.text == "@solve-before")
            return HirKind::solve_before;
        if (input.text == "@solve-list")
            return HirKind::solve_list;
        if (input.text == "@constraint-unique")
            return HirKind::unique_constraint;
        return HirKind::call;
    }
    return HirKind::invalid;
}

sv::ConstraintExpression SystemVerilogHirBuilder::constraint_expression(
    const frontend::Expression& input)
{
    sv::ConstraintExpression output;
    output.kind = constraint_kind(input);
    output.text = input.text;
    output.source = source(input.span);
    for (const auto& operand : input.operands) {
        output.operands.push_back(constraint_expression(operand));
    }
    return output;
}

sv::TypeReference SystemVerilogHirBuilder::class_property_type(
    const frontend::Type& input,
    const semantic::SourceSpanId type_source)
{
    sv::TypeReference output;
    const auto spelling = input.named_type.empty()
        ? input.spelling
        : input.named_type;
    output.target = { { }, type_source, spelling };
    output.signed_value = input.is_signed;
    output.executable_width = input.width();
    output.four_state = input.domain == frontend::ValueDomain::Logic4
        || input.domain == frontend::ValueDomain::Integer;
    if (!input.systemverilog_class_declaration.empty()) {
        output.value_form = sv::TypeForm::class_handle;
        output.class_identity = input.systemverilog_class_declaration;
    } else if (input.domain == frontend::ValueDomain::String) {
        output.value_form = sv::TypeForm::string;
    } else if (input.systemverilog_container) {
        switch (input.systemverilog_container->kind) {
        case frontend::SystemVerilogContainerKind::DynamicArray:
            output.container_form = sv::TypeForm::dynamic_array;
            break;
        case frontend::SystemVerilogContainerKind::Queue:
            output.container_form = sv::TypeForm::queue;
            break;
        case frontend::SystemVerilogContainerKind::AssociativeArray:
            output.container_form = sv::TypeForm::associative_array;
            break;
        case frontend::SystemVerilogContainerKind::StaticArray:
            output.container_form = sv::TypeForm::static_array;
            break;
        }
    } else if (!input.enumeration_literals.empty()) {
        output.value_form = sv::TypeForm::enumeration;
    } else {
        output.value_form = sv::TypeForm::packed_integral;
    }
    if (input.packed_range) {
        output.packed_range = packed_range(*input.packed_range, type_source);
    }
    return output;
}

bool SystemVerilogHirBuilder::owner_matches(
    const std::string_view owner,
    const std::string_view selected) noexcept
{
    return owner == selected
        || (owner.size() > selected.size() + 2U
            && owner.ends_with(selected)
            && owner[owner.size() - selected.size() - 1U] == ':');
}

bool SystemVerilogHirBuilder::specializes_or_derives(
    const frontend::SystemVerilogClassSpecialization& specialization,
    const std::string_view declaration_identity) const
{
    const auto* current = &specialization;
    while (current != nullptr) {
        if (current->declaration_identity == declaration_identity)
            return true;
        if (current->base_specialization_identity.empty())
            return false;
        const auto base = std::ranges::find(
            class_specializations_, current->base_specialization_identity,
            &frontend::SystemVerilogClassSpecialization::specialization_identity);
        current = base == class_specializations_.end() ? nullptr : &*base;
    }
    return false;
}

std::optional<sv::ConstraintBinding> SystemVerilogHirBuilder::property_binding(
    const frontend::SystemVerilogClassDeclaration& declaration,
    const frontend::SystemVerilogClassSpecialization& specialization,
    std::string spelling,
    const semantic::SourceSpanId binding_source)
{
    enum class Selection {
        ordinary,
        this_object,
        super_object,
        qualified,
    };
    auto selection = Selection::ordinary;
    std::string owner;
    if (spelling.starts_with("this.")) {
        spelling.erase(0, 5U);
        selection = Selection::this_object;
    } else if (spelling.starts_with("super.")) {
        spelling.erase(0, 6U);
        selection = Selection::super_object;
    } else if (const auto separator = spelling.rfind("::");
        separator != std::string::npos) {
        owner = spelling.substr(0, separator);
        spelling.erase(0, separator + 2U);
        selection = Selection::qualified;
    }
    const auto found = std::ranges::find_if(
        specialization.properties.rbegin(),
        specialization.properties.rend(),
        [&](const frontend::SystemVerilogClassPropertyLayout& property) {
            if (property.name != spelling)
                return false;
            if (selection == Selection::super_object) {
                return property.owner_identity
                    != declaration.canonical_identity;
            }
            if (selection == Selection::qualified)
                return owner_matches(property.owner_identity, owner);
            return true;
        });
    if (found == specialization.properties.rend())
        return std::nullopt;
    sv::ConstraintBinding binding;
    binding.kind = sv::ConstraintReferenceKind::property;
    binding.specialization_identity = specialization.specialization_identity;
    binding.canonical_identity = found->owner_identity + "::" + found->name;
    binding.type = class_property_type(found->type, binding_source);
    return binding;
}

std::optional<sv::ConstraintBinding> SystemVerilogHirBuilder::parameter_binding(
    const frontend::SystemVerilogClassDeclaration& declaration,
    const frontend::SystemVerilogClassSpecialization& specialization,
    const std::string_view spelling,
    const semantic::SourceSpanId binding_source)
{
    const auto value = std::ranges::find(
        specialization.parameter_values,
        spelling,
        &std::pair<std::string, std::string>::first);
    if (value == specialization.parameter_values.end())
        return std::nullopt;
    const auto formal = std::ranges::find(
        declaration.parameters,
        spelling,
        &frontend::ParameterDeclaration::name);
    if (formal == declaration.parameters.end())
        return std::nullopt;
    sv::ConstraintBinding binding;
    binding.kind = sv::ConstraintReferenceKind::parameter;
    binding.specialization_identity = specialization.specialization_identity;
    binding.canonical_identity = declaration.canonical_identity
        + "::" + std::string { spelling };
    binding.type = class_property_type(formal->type, binding_source);
    binding.constant_value = value->second;
    return binding;
}

std::optional<sv::ConstraintBinding> SystemVerilogHirBuilder::method_binding(
    const frontend::SystemVerilogClassSpecialization& specialization,
    std::string spelling,
    const semantic::SourceSpanId binding_source)
{
    if (spelling.starts_with('.'))
        spelling.erase(0, 1U);
    if (const auto separator = spelling.rfind("::");
        separator != std::string::npos) {
        spelling.erase(0, separator + 2U);
    }
    const auto found = std::ranges::find(
        specialization.methods,
        spelling,
        &frontend::SystemVerilogClassMethodProfile::name);
    if (found == specialization.methods.end())
        return std::nullopt;
    sv::ConstraintBinding binding;
    binding.kind = sv::ConstraintReferenceKind::method;
    binding.specialization_identity = specialization.specialization_identity;
    binding.canonical_identity = found->canonical_identity;
    binding.type = class_property_type(found->return_type, binding_source);
    return binding;
}

void SystemVerilogHirBuilder::resolve_constraint_expression(
    sv::ConstraintExpression& expression,
    const frontend::SystemVerilogClassDeclaration& declaration,
    const std::optional<std::string_view> enclosing_foreach)
{
    auto foreach_iterator = enclosing_foreach;
    if (expression.kind == sv::ConstraintExpressionKind::foreach_constraint
        && expression.operands.size() == 2U
        && expression.operands[0].kind
            == sv::ConstraintExpressionKind::index
        && expression.operands[0].operands.size() == 2U
        && expression.operands[0].operands[1].kind
            == sv::ConstraintExpressionKind::name) {
        foreach_iterator = expression.operands[0].operands[1].text;
    }
    for (auto& operand : expression.operands) {
        resolve_constraint_expression(operand, declaration, foreach_iterator);
    }
    for (const auto& specialization : class_specializations_) {
        if (!specializes_or_derives(
                specialization, declaration.canonical_identity)) {
            continue;
        }
        std::optional<sv::ConstraintBinding> binding;
        if (expression.kind == sv::ConstraintExpressionKind::name) {
            if (foreach_iterator && expression.text == *foreach_iterator) {
                sv::ConstraintBinding local;
                local.kind = sv::ConstraintReferenceKind::local_variable;
                local.specialization_identity
                    = specialization.specialization_identity;
                local.canonical_identity = declaration.canonical_identity
                    + "::$foreach::" + expression.text;
                local.type.target = { { }, expression.source, "int" };
                local.type.value_form = sv::TypeForm::packed_integral;
                local.type.signed_value = true;
                local.type.executable_width = 32;
                local.type.four_state = true;
                binding = std::move(local);
            } else {
                binding = property_binding(
                    declaration, specialization, expression.text,
                    expression.source);
                if (!binding) {
                    binding = parameter_binding(
                        declaration, specialization, expression.text,
                        expression.source);
                }
            }
        } else if (expression.kind == sv::ConstraintExpressionKind::call) {
            binding = method_binding(
                specialization, expression.text, expression.source);
        }
        if (binding)
            expression.bindings.push_back(std::move(*binding));
    }
}

semantic::ScopeId SystemVerilogHirBuilder::ensure_compilation_unit(
    const frontend::SourceSpan& span,
    const std::string_view source_library,
    const std::string_view compilation_unit_identity,
    const frontend::StandardRevision standard_revision,
    const std::string_view compatibility_profile,
    const std::string_view canonical_parent)
{
    const auto separator = canonical_parent.find("::");
    const auto library = separator == std::string_view::npos
        ? source_library
        : canonical_parent.substr(0, separator);
    const auto name = separator == std::string_view::npos
        ? canonical_parent
        : canonical_parent.substr(separator + 2U);
    const auto existing = std::ranges::find_if(
        hir_.units(), [&](const sv::Unit& unit) {
            const auto unit_library = unit.library.empty()
                ? std::string_view { "work" }
                : std::string_view { unit.library };
            return unit.kind == sv::UnitKind::compilation_unit
                && unit_library == library && unit.name == name;
        });
    if (existing != hir_.units().end())
        return existing->scope;
    if (!name.starts_with("$unit")) {
        throw std::logic_error {
            "SystemVerilog declaration has no semantic owner scope"
        };
    }
    const auto unit_source = source(span);
    const auto unit_origin = model_.add_origin(
        semantic::OriginKind::parsed,
        unit_source,
        std::nullopt,
        std::string { canonical_parent });
    const auto unit_id = model_.add_unit(
        semantic::Language::system_verilog,
        semantic::UnitKind::systemverilog_compilation_unit,
        std::string { library },
        std::string { name },
        { },
        unit_source,
        unit_origin);
    const auto& common = model_.units()[unit_id.value()];
    sv::Unit output;
    output.id = unit_id;
    output.scope = common.scope;
    output.kind = sv::UnitKind::compilation_unit;
    output.library = common.library;
    output.name = common.name;
    output.source = common.source;
    output.origin = common.origin;
    output.compilation_unit_identity = compilation_unit_identity;
    output.standard = std::string {
        frontend::revision_string(standard_revision)
    };
    output.compatibility_profile = compatibility_profile;
    hir_.mutable_units().push_back(std::move(output));
    return common.scope;
}

semantic::ScopeId SystemVerilogHirBuilder::class_parent_scope(
    const frontend::SystemVerilogClassDeclaration& input)
{
    const auto separator = input.canonical_identity.rfind("::");
    if (separator == std::string::npos) {
        throw std::logic_error {
            "SystemVerilog class has no canonical owner identity"
        };
    }
    const auto parent = std::string_view { input.canonical_identity }
                            .substr(0, separator);
    if (const auto enclosing_class = class_scopes_.find(parent);
        enclosing_class != class_scopes_.end()) {
        return enclosing_class->second;
    }
    const auto enclosing_unit = std::ranges::find_if(
        hir_.units(), [&](const sv::Unit& unit) {
            const auto library = unit.library.empty()
                ? std::string_view { "work" }
                : std::string_view { unit.library };
            return parent == std::string { library } + "::" + unit.name;
        });
    return enclosing_unit == hir_.units().end()
        ? ensure_compilation_unit(
              input.span,
              input.library,
              input.compilation_unit_identity,
              input.standard_revision,
              input.verilog_compatibility_profile,
              parent)
        : enclosing_unit->scope;
}

std::string SystemVerilogHirBuilder::class_identity(
    const frontend::SystemVerilogClassDeclaration& input,
    const semantic::ScopeId parent_scope) const
{
    // Class resolution assigns library-qualified identities to ordinary
    // unit and compilation-unit declarations. Generate-local declarations
    // do not pass through that inventory, so derive their identity from the
    // semantic owner scope rather than retaining the parser-local label.
    if (!input.canonical_identity.empty() && !input.library.empty())
        return input.canonical_identity;
    const auto enclosing_class = std::ranges::find_if(
        class_scopes_, [&](const auto& item) {
            return item.second == parent_scope;
        });
    if (enclosing_class != class_scopes_.end()) {
        return enclosing_class->first + "::" + input.name;
    }
    const auto unit_id = scope_unit(parent_scope);
    const auto& unit = model_.units().at(unit_id.value());
    std::vector<std::string_view> scopes;
    auto scope = std::optional<semantic::ScopeId> { parent_scope };
    while (scope && *scope != unit.scope) {
        const auto& current = model_.scopes().at(scope->value());
        if (!current.name.empty())
            scopes.push_back(current.name);
        scope = current.parent;
    }
    std::string identity = unit.library.empty() ? "work" : unit.library;
    identity += "::" + unit.name + "::";
    for (auto item = scopes.rbegin(); item != scopes.rend(); ++item) {
        identity += std::string { *item } + '.';
    }
    identity += input.name;
    return identity;
}

sv::ClassRelation SystemVerilogHirBuilder::class_relation(
    const frontend::SystemVerilogClassBase& input,
    const semantic::ScopeId scope,
    const semantic::OriginId parent)
{
    sv::ClassRelation output;
    output.name = input.name;
    output.declaration_identity = input.declaration_identity;
    output.source = source(input.span);
    output.origin = origin(output.source, parent, input.name);
    for (const auto& input_actual : input.parameter_actuals) {
        sv::ActualAssociation actual;
        if (!input_actual.name.empty())
            actual.formal = input_actual.name;
        actual.source = source(input_actual.span);
        if (input_actual.type_actual) {
            actual.kind = sv::ActualKind::type;
            actual.type = type_reference(
                *input_actual.type_actual, input_actual.span,
                scope, output.origin);
        } else if (input_actual.value.kind
            == frontend::ExpressionKind::Invalid) {
            actual.kind = sv::ActualKind::default_value;
        } else {
            actual.expression = expression(
                input_actual.value, scope, output.origin);
        }
        output.actuals.push_back(std::move(actual));
    }
    return output;
}

void SystemVerilogHirBuilder::add_class(
    const frontend::SystemVerilogClassDeclaration& input,
    const std::optional<semantic::ScopeId> explicit_parent_scope,
    const std::optional<semantic::DeclarationId> generate_owner,
    const std::string_view alternative_discriminator)
{
    const auto parent_scope = explicit_parent_scope
        ? *explicit_parent_scope
        : class_parent_scope(input);
    sv::ClassDeclaration output;
    const auto generate_separator = input.name.rfind('.');
    output.name = generate_separator == std::string::npos
        ? input.name
        : input.name.substr(generate_separator + 1U);
    output.canonical_identity = class_identity(input, parent_scope);
    output.alternative_discriminator = alternative_discriminator;
    const auto enclosing_class = std::ranges::find(
        hir_.classes(), parent_scope, &sv::ClassDeclaration::scope);
    if (enclosing_class != hir_.classes().end()) {
        output.enclosing_identity
            = sv::class_declaration_identity(*enclosing_class);
    } else if (const auto separator
        = output.canonical_identity.rfind("::");
        separator != std::string::npos) {
        output.enclosing_identity = output.canonical_identity.substr(
            0, separator);
    }
    const auto owner_identity = sv::class_declaration_identity(output);
    output.generate_owner = generate_owner;
    output.lifetime = class_lifetime(input.lifetime);
    output.virtual_class = input.is_virtual;
    output.interface_class = input.is_interface;
    output.forward_declaration = input.is_forward_declaration;
    output.end_name = input.end_name;
    output.source = source(input.span);
    const auto parent_origin = model_.scopes()[parent_scope.value()].origin;
    output.origin = origin(
        output.source, parent_origin, owner_identity);
    output.scope = nested_scope(
        parent_scope, output.name, output.source, output.origin);
    class_scopes_.emplace(output.canonical_identity, output.scope);

    for (const auto& parameter : input.parameters) {
        const auto id = add_parameter(parameter, output.scope, output.origin);
        const auto& retained_declaration = declaration(id);
        sv::ClassParameter retained;
        retained.declaration = id;
        retained.name = parameter.name;
        retained.type_parameter
            = parameter.kind == frontend::ParameterKind::Type;
        retained.type = retained_declaration.type;
        retained.default_type = retained_declaration.default_type;
        retained.default_value = retained_declaration.initializer;
        retained.source = retained_declaration.source;
        retained.origin = retained_declaration.origin;
        output.parameters.push_back(std::move(retained));
        output.member_declarations.push_back(id);
    }
    if (input.base) {
        if (input.is_interface) {
            output.extended_interfaces.push_back(class_relation(
                *input.base, output.scope, output.origin));
        } else {
            output.base = class_relation(
                *input.base, output.scope, output.origin);
            output.base_declaration_identity
                = input.base->declaration_identity;
        }
    }
    for (const auto& relation : input.extended_interfaces) {
        output.extended_interfaces.push_back(class_relation(
            relation, output.scope, output.origin));
    }
    for (const auto& relation : input.implemented_interfaces) {
        output.implemented_interfaces.push_back(class_relation(
            relation, output.scope, output.origin));
    }
    for (const auto& alias : input.type_aliases) {
        const auto id = add_type_declaration(
            alias, output.scope, output.origin);
        output.type_aliases.push_back(id);
        output.member_declarations.push_back(id);
    }
    for (const auto& property : input.properties) {
        const auto id = add_variable(
            property.declaration, output.scope, output.origin);
        const auto& retained_declaration = declaration(id);
        sv::ClassProperty retained;
        retained.declaration = id;
        retained.name = property.declaration.name;
        retained.owner_identity = owner_identity;
        retained.canonical_identity = owner_identity + "::"
            + property.declaration.name;
        retained.type = *retained_declaration.type;
        retained.initializer = retained_declaration.initializer;
        retained.visibility = class_visibility(property.visibility);
        retained.random_kind = property.is_rand
            ? sv::ClassRandomKind::rand
            : property.is_randc
            ? sv::ClassRandomKind::randc
            : sv::ClassRandomKind::none;
        retained.static_storage = property.is_static;
        retained.constant = property.is_const;
        retained.parameter = property.is_parameter;
        retained.source = retained_declaration.source;
        retained.origin = retained_declaration.origin;
        output.properties.push_back(std::move(retained));
        output.member_declarations.push_back(id);
    }
    for (const auto& method : input.methods) {
        const auto id = method.kind
                == frontend::SystemVerilogClassMethodKind::Task
            ? add_task(class_task(method), output.scope, output.origin)
            : add_function(class_function(method), output.scope, output.origin);
        const auto& retained_declaration = declaration(id);
        sv::ClassMethod retained;
        retained.declaration = id;
        retained.name = method.name;
        retained.canonical_identity = method.canonical_identity.empty()
                || !output.alternative_discriminator.empty()
            ? owner_identity + "::" + method.name
            : method.canonical_identity;
        retained.owner_identity = owner_identity;
        retained.profile_identity = method_profile_identity(method);
        retained.kind = class_method_kind(method.kind);
        retained.visibility = class_visibility(method.visibility);
        retained.lifetime = class_lifetime(method.lifetime);
        retained.static_method = method.is_static;
        retained.virtual_method = method.is_virtual;
        retained.pure = method.is_pure;
        retained.final_method = method.is_final;
        retained.external = method.is_extern;
        retained.out_of_block_definition = method.out_of_block_definition;
        retained.defined = method.defined;
        retained.source = retained_declaration.source;
        retained.origin = retained_declaration.origin;
        output.methods.push_back(std::move(retained));
        output.member_declarations.push_back(id);
    }
    for (const auto& constraint : input.constraints) {
        sv::ClassConstraint retained;
        retained.name = constraint.name;
        retained.canonical_identity = owner_identity + "::"
            + constraint.name;
        retained.owner_identity = owner_identity;
        retained.visibility = class_visibility(constraint.visibility);
        retained.static_constraint = constraint.is_static;
        retained.pure = constraint.is_pure;
        retained.external = constraint.is_extern;
        retained.defined = constraint.defined;
        retained.source = source(constraint.span);
        retained.origin = origin(
            retained.source, output.origin, retained.canonical_identity);
        for (const auto& input_expression : constraint.expressions) {
            retained.expressions.push_back(
                constraint_expression(input_expression));
        }
        for (auto& retained_expression : retained.expressions) {
            resolve_constraint_expression(retained_expression, input);
        }
        output.constraints.push_back(std::move(retained));
    }
    for (const auto& input_covergroup : input.covergroups) {
        output.covergroups.push_back(covergroup_declaration(
            input_covergroup, output.scope, output.origin));
    }
    for (const auto& nested : input.nested_classes) {
        output.nested_class_identities.push_back(
            sv::class_declaration_identity(
                nested.canonical_identity.empty()
                    ? output.canonical_identity + "::" + nested.name
                    : nested.canonical_identity,
                output.alternative_discriminator));
    }
    const auto class_scope = output.scope;
    hir_.mutable_classes().push_back(std::move(output));
    add_classes(input.nested_classes, class_scope, generate_owner,
        alternative_discriminator);
}

void SystemVerilogHirBuilder::add_classes(
    const std::vector<frontend::SystemVerilogClassDeclaration>& inputs,
    const std::optional<semantic::ScopeId> parent_scope,
    const std::optional<semantic::DeclarationId> generate_owner,
    const std::string_view alternative_discriminator)
{
    for (const auto& input : inputs)
        add_class(input, parent_scope, generate_owner,
            alternative_discriminator);
}

void SystemVerilogHirBuilder::synchronize_generate_class_ownership()
{
    const auto synchronize = [&](const auto& self,
                                 sv::GenerateRegion& region) -> void {
        for (auto& nested : region.nested) {
            self(self, nested);
        }
        region.class_declarations.clear();
        for (const auto& declaration : hir_.classes()) {
            if (declaration.generate_owner == region.declaration) {
                region.class_declarations.push_back(
                    sv::class_declaration_identity(declaration));
            }
        }
        for (auto& alternative : region.alternatives) {
            const auto nested = std::ranges::find_if(
                region.nested, [&](const sv::GenerateRegion& candidate) {
                    return candidate.scope == alternative.scope
                        && candidate.alternative_discriminator
                            == alternative.alternative_discriminator;
                });
            alternative.class_declarations = nested == region.nested.end()
                ? std::vector<std::string> { }
                : nested->class_declarations;
        }
    };
    for (auto& unit : hir_.mutable_units()) {
        for (auto& generate : unit.generates) {
            synchronize(synchronize, generate);
        }
    }
}

void SystemVerilogHirBuilder::compose_constraints()
{
    std::set<std::string> complete;
    std::set<std::string> active;
    std::function<void(sv::ClassDeclaration&)> compose;
    compose = [&](sv::ClassDeclaration& declaration) {
        const auto declaration_identity
            = sv::class_declaration_identity(declaration);
        if (complete.contains(declaration_identity))
            return;
        if (!active.insert(declaration_identity).second)
            return;
        if (!declaration.base_declaration_identity.empty()) {
            const auto same_base_identity = [&](const auto& candidate) {
                return candidate.canonical_identity
                    == declaration.base_declaration_identity;
            };
            auto base = std::ranges::find_if(
                hir_.mutable_classes(), [&](const auto& candidate) {
                    return same_base_identity(candidate)
                        && candidate.alternative_discriminator
                            == declaration.alternative_discriminator;
                });
            if (base == hir_.mutable_classes().end()) {
                base = std::ranges::find_if(
                    hir_.mutable_classes(), [&](const auto& candidate) {
                        return same_base_identity(candidate)
                            && candidate.alternative_discriminator.empty();
                    });
            }
            if (base != hir_.mutable_classes().end()) {
                compose(*base);
                declaration.composed_constraints
                    = base->composed_constraints;
            }
        }
        for (const auto& constraint : declaration.constraints) {
            sv::ComposedClassConstraint selected;
            selected.name = constraint.name;
            selected.selected_identity = constraint.canonical_identity;
            selected.mode_enabled = constraint.defined
                && !constraint.pure && !constraint.external;
            const auto inherited = std::ranges::find(
                declaration.composed_constraints,
                constraint.name,
                &sv::ComposedClassConstraint::name);
            if (inherited == declaration.composed_constraints.end()) {
                declaration.composed_constraints.push_back(
                    std::move(selected));
                continue;
            }
            const auto inherited_owner = std::ranges::find_if(
                hir_.classes(), [&](const sv::ClassDeclaration& candidate) {
                    return std::ranges::any_of(
                        candidate.constraints,
                        [&](const sv::ClassConstraint& block) {
                            return block.canonical_identity
                                == inherited->selected_identity;
                        });
                });
            const sv::ClassConstraint* inherited_block = nullptr;
            if (inherited_owner != hir_.classes().end()) {
                const auto block = std::ranges::find(
                    inherited_owner->constraints,
                    inherited->selected_identity,
                    &sv::ClassConstraint::canonical_identity);
                if (block != inherited_owner->constraints.end())
                    inherited_block = &*block;
            }
            selected.overrides = true;
            selected.overridden_identity = inherited->selected_identity;
            selected.override_legal = inherited_block == nullptr
                || inherited_block->static_constraint
                    == constraint.static_constraint;
            *inherited = std::move(selected);
        }
        active.erase(declaration_identity);
        complete.insert(declaration_identity);
    };
    for (auto& declaration : hir_.mutable_classes())
        compose(declaration);
}

} // namespace systemverilog_hir_detail
} // namespace fsim::app::application_detail
