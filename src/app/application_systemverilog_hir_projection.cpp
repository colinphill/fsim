// SPDX-License-Identifier: Apache-2.0
#include "application_systemverilog_hir_internal.hpp"

namespace fsim::app::application_detail::systemverilog_hir_detail {

namespace {

    void append_signature_component(
        std::string& output, const std::string_view value)
    {
        output += std::to_string(value.size());
        output += ':';
        output += value;
        output += ';';
    }

    template <typename Value>
    void append_signature_value(std::string& output, const Value value)
    {
        append_signature_component(
            output, std::to_string(static_cast<std::int64_t>(value)));
    }

    [[nodiscard]] bool append_anonymous_type_signature(
        std::string& output, const frontend::Type& type)
    {
        if ((type.packed_range_expression && !type.packed_range)
            || !type.systemverilog_enumeration_values.empty()) {
            return false;
        }
        append_signature_value(output, type.domain);
        append_signature_value(output, type.systemverilog_scalar);
        append_signature_value(output, type.packed_aggregate);
        append_signature_component(output, type.spelling);
        append_signature_component(output, type.named_type);
        append_signature_value(output, type.is_signed);
        append_signature_value(output, type.packed_range.has_value());
        if (type.packed_range) {
            append_signature_value(output, type.packed_range->left);
            append_signature_value(output, type.packed_range->right);
            append_signature_value(output, type.packed_range->descending);
        }
        if (type.systemverilog_container) {
            const auto& container = *type.systemverilog_container;
            if (container.queue_maximum
                || container.associative_index_type
                || !container.static_range_expressions.empty()) {
                return false;
            }
            append_signature_value(output, container.kind);
            append_signature_value(
                output, container.static_range.has_value());
            if (container.static_range) {
                append_signature_value(
                    output, container.static_range->left);
                append_signature_value(
                    output, container.static_range->right);
                append_signature_value(
                    output, container.static_range->descending);
            }
            append_signature_value(output, container.element_types.size());
            for (const auto& element : container.element_types) {
                if (!append_anonymous_type_signature(output, element)) {
                    return false;
                }
            }
        } else {
            append_signature_component(output, "no-container");
        }
        append_signature_value(output, type.packed_members.size());
        for (const auto& member : type.packed_members) {
            if ((member.packed_range_expression
                    && !member.packed_range)
                || member.initializer) {
                return false;
            }
            append_signature_component(output, member.name);
            append_signature_value(output, member.domain);
            append_signature_component(output, member.spelling);
            append_signature_value(output, member.is_signed);
            append_signature_value(output, member.packed_range.has_value());
            if (member.packed_range) {
                append_signature_value(output, member.packed_range->left);
                append_signature_value(output, member.packed_range->right);
                append_signature_value(
                    output, member.packed_range->descending);
            }
            append_signature_value(output, member.nested_types.size());
            for (const auto& nested : member.nested_types) {
                if (!append_anonymous_type_signature(output, nested)) {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] std::optional<std::string> anonymous_aggregate_signature(
        const frontend::Type& type, const semantic::ScopeId scope)
    {
        std::string result = std::to_string(scope.value()) + '|';
        if (!append_anonymous_type_signature(result, type)) {
            return std::nullopt;
        }
        return result;
    }

    [[nodiscard]] bool systemverilog_net(const frontend::Type& type)
    {
        if (!type.systemverilog_net_type.empty()) {
            return true;
        }
        return type.spelling == "wire" || type.spelling == "tri"
            || type.spelling == "tri0" || type.spelling == "tri1"
            || type.spelling == "wand" || type.spelling == "triand"
            || type.spelling == "wor" || type.spelling == "trior"
            || type.spelling == "trireg" || type.spelling == "uwire"
            || type.spelling == "supply0" || type.spelling == "supply1";
    }

} // namespace

[[nodiscard]] sv::PackedRange SystemVerilogHirBuilder::packed_range(
    const frontend::PackedRangeExpression& range,
    const semantic::ScopeId scope,
    const semantic::OriginId parent)
{
    return {
        std::nullopt,
        std::nullopt,
        expression(range.left, scope, parent),
        expression(range.right, scope, parent),
        range.descending.value_or(true),
        source(range.span)
    };
}

[[nodiscard]] sv::TypeReference SystemVerilogHirBuilder::type_reference(
    const frontend::Type& input,
    const frontend::SourceSpan& fallback,
    const semantic::ScopeId scope,
    const semantic::OriginId parent)
{
    const auto spelling = input.named_type.empty()
        ? input.spelling
        : input.named_type;
    sv::TypeReference output;
    output.target = {
        input.named_type.empty() ? semantic::TypeId { }
                                 : find_type(scope, input.named_type),
        source(input.named_type.empty() ? fallback : input.named_type_span),
        spelling
    };
    output.value_form = type_form(input);
    if (!input.systemverilog_class_declaration.empty()) {
        output.value_form = sv::TypeForm::class_handle;
        output.class_identity = input.systemverilog_class_declaration;
    }
    output.virtual_interface = input.systemverilog_virtual_interface;
    output.interface_type = input.systemverilog_interface_type;
    output.interface_modport = input.systemverilog_interface_modport;
    for (const auto& input_actual :
        input.systemverilog_class_parameter_actuals) {
        sv::ActualAssociation actual;
        actual.formal = input_actual.name;
        actual.source = source(input_actual.span);
        if (input_actual.type_actual) {
            actual.kind = sv::ActualKind::type;
            actual.type = type_reference(
                *input_actual.type_actual, input_actual.span,
                scope, parent);
        } else if (input_actual.value.kind
            == frontend::ExpressionKind::Invalid) {
            actual.kind = sv::ActualKind::default_value;
        } else {
            actual.expression = expression(
                input_actual.value, scope, parent);
        }
        output.interface_parameter_actuals.push_back(
            std::move(actual));
    }
    output.systemverilog_net_type = input.systemverilog_net_type;
    output.systemverilog_resolution_function
        = input.systemverilog_resolution_function;
    output.signed_value = input.is_signed;
    output.executable_width = input.width();
    output.four_state = input.domain == frontend::ValueDomain::Logic4
        || input.domain == frontend::ValueDomain::Integer;
    if (input.packed_range) {
        output.packed_range = packed_range(
            *input.packed_range, source(fallback));
    } else if (input.packed_range_expression) {
        output.packed_range = packed_range(
            *input.packed_range_expression, scope, parent);
    }
    if (input.systemverilog_container) {
        const auto& container = *input.systemverilog_container;
        output.container_form = type_form(input);
        output.queue_maximum = container.queue_maximum
            ? expression(*container.queue_maximum, scope, parent)
            : std::nullopt;
        if (container.associative_index_type) {
            output.associative_index = type_reference(
                *container.associative_index_type,
                container.span,
                scope,
                parent)
                                           .target;
        }
        if (!container.static_range_expressions.empty()) {
            for (const auto& range : container.static_range_expressions) {
                output.unpacked_dimensions.push_back(packed_range(
                    range, scope, parent));
            }
        } else if (container.static_range) {
            output.unpacked_dimensions.push_back(packed_range(
                *container.static_range, source(container.span)));
        }
        for (const auto& element : container.element_types) {
            output.container_element_types.push_back(type_reference(
                element, container.span, scope, parent));
        }
    }
    const auto aggregate_form = output.value_form.value_or(
        sv::TypeForm::unresolved);
    const bool anonymous_aggregate = input.named_type.empty()
        && !input.packed_members.empty()
        && (aggregate_form == sv::TypeForm::packed_structure
            || aggregate_form == sv::TypeForm::packed_union
            || aggregate_form == sv::TypeForm::tagged_union
            || aggregate_form == sv::TypeForm::unpacked_structure
            || aggregate_form == sv::TypeForm::unpacked_union);
    if (anonymous_aggregate) {
        const auto signature = anonymous_aggregate_signature(input, scope);
        if (signature) {
            const auto existing = anonymous_aggregate_types_.find(*signature);
            if (existing != anonymous_aggregate_types_.end()) {
                output.target.target = existing->second;
                return output;
            }
        }
        const auto type_source = output.target.source;
        const auto type_origin = origin(
            type_source, parent, "anonymous SystemVerilog aggregate type");
        const auto name = "@anonymous-aggregate:"
            + std::to_string(type_source.value()) + ":"
            + std::to_string(parent.value());
        const auto base = output;
        const auto type_id = model_.add_type(
            scope,
            semantic::TypeKind::implicit,
            name,
            base.target,
            type_source,
            type_origin);
        output.target.target = type_id;

        sv::TypeDefinition definition;
        definition.id = type_id;
        definition.form = aggregate_form;
        definition.name = name;
        definition.base = base;
        definition.source = type_source;
        definition.origin = type_origin;
        add_packed_members(
            input, definition, scope, type_origin);
        hir_.mutable_types().push_back(std::move(definition));
        if (signature) {
            anonymous_aggregate_types_.emplace(*signature, type_id);
        }
    }
    return output;
}

[[nodiscard]] semantic::DeclarationId SystemVerilogHirBuilder::add_declaration_record(
    const semantic::ScopeId scope,
    const sv::DeclarationForm form,
    const semantic::DeclarationKind kind,
    const std::string_view declaration_name,
    const frontend::SourceSpan& frontend_span,
    const semantic::OriginId parent)
{
    const auto span = source(frontend_span);
    const auto declaration_origin = origin(span, parent, declaration_name);
    const auto id = model_.add_declaration(
        scope,
        kind,
        std::string { declaration_name },
        span,
        declaration_origin);
    sv::Declaration output;
    output.id = id;
    output.scope = scope;
    output.form = form;
    output.name = declaration_name;
    output.source = span;
    output.origin = declaration_origin;
    hir_.mutable_declarations().push_back(std::move(output));
    return id;
}

[[nodiscard]] sv::Declaration& SystemVerilogHirBuilder::declaration(
    const semantic::DeclarationId id)
{
    const auto found = std::ranges::find_if(
        hir_.mutable_declarations(), [&](const sv::Declaration& declaration) {
            return declaration.id == id;
        });
    if (found == hir_.mutable_declarations().end()) {
        throw std::logic_error { "missing SystemVerilog HIR declaration" };
    }
    return *found;
}

[[nodiscard]] semantic::TypeId SystemVerilogHirBuilder::ensure_type(
    const std::string_view type_name,
    const semantic::ScopeId scope,
    const semantic::TypeKind kind,
    const sv::TypeReference& base,
    const semantic::SourceSpanId span,
    const semantic::OriginId declaration_origin)
{
    auto id = find_type(scope, type_name, span);
    if (!id.valid()) {
        id = model_.add_type(
            scope,
            kind,
            std::string { type_name },
            base.target,
            span,
            declaration_origin);
    }
    return id;
}

[[nodiscard]] semantic::ValueId SystemVerilogHirBuilder::ensure_value(
    const std::string_view value_name,
    const semantic::ScopeId scope,
    const semantic::ValueKind kind,
    const sv::TypeReference& type,
    const semantic::SourceSpanId span,
    const semantic::OriginId declaration_origin)
{
    auto id = find_value(scope, value_name, span);
    if (!id.valid()) {
        id = model_.add_value(
            scope,
            kind,
            std::string { value_name },
            type.target,
            span,
            declaration_origin);
    }
    return id;
}

[[nodiscard]] sv::TypeForm SystemVerilogHirBuilder::type_form(
    const frontend::Type& input) const noexcept
{
    if (input.systemverilog_container) {
        switch (input.systemverilog_container->kind) {
        case frontend::SystemVerilogContainerKind::DynamicArray:
            return sv::TypeForm::dynamic_array;
        case frontend::SystemVerilogContainerKind::Queue:
            return sv::TypeForm::queue;
        case frontend::SystemVerilogContainerKind::AssociativeArray:
            return sv::TypeForm::associative_array;
        case frontend::SystemVerilogContainerKind::StaticArray:
            return sv::TypeForm::static_array;
        }
    }
    switch (input.packed_aggregate) {
    case frontend::PackedAggregateKind::Struct:
        return sv::TypeForm::packed_structure;
    case frontend::PackedAggregateKind::Union:
        return sv::TypeForm::packed_union;
    case frontend::PackedAggregateKind::TaggedUnion:
        return sv::TypeForm::tagged_union;
    case frontend::PackedAggregateKind::UnpackedStruct:
        return sv::TypeForm::unpacked_structure;
    case frontend::PackedAggregateKind::UnpackedUnion:
        return sv::TypeForm::unpacked_union;
    case frontend::PackedAggregateKind::None:
        break;
    }
    if (!input.enumeration_literals.empty()) {
        return sv::TypeForm::enumeration;
    }
    if (input.domain == frontend::ValueDomain::String) {
        return sv::TypeForm::string;
    }
    return sv::TypeForm::packed_integral;
}

[[nodiscard]] semantic::DeclarationId SystemVerilogHirBuilder::add_type_declaration(
    const frontend::TypeAliasDeclaration& input,
    const semantic::ScopeId scope,
    const semantic::OriginId parent)
{
    const auto id = add_declaration_record(
        scope,
        input.declaration_kind
                == frontend::TypeDeclarationKind::SystemVerilogNettype
            ? sv::DeclarationForm::nettype_declaration
            : sv::DeclarationForm::typedef_declaration,
        semantic::DeclarationKind::type,
        input.name,
        input.span,
        parent);
    const auto declaration_origin = declaration(id).origin;
    const auto base = type_reference(
        input.type, input.span, scope, declaration_origin);
    const auto type_id = ensure_type(
        input.name,
        scope,
        semantic::TypeKind::alias,
        base,
        declaration(id).source,
        declaration_origin);
    declaration(id).declared_type = type_id;
    declaration(id).type = base;
    sv::TypeDefinition output;
    output.id = type_id;
    output.declaration = id;
    output.form = type_form(input.type);
    output.name = input.name;
    output.base = base;
    output.resolution_function = input.systemverilog_resolution_function;
    output.source = declaration(id).source;
    output.origin = declaration_origin;
    add_type_payload(input, output, scope, declaration_origin);
    hir_.mutable_types().push_back(std::move(output));
    return id;
}

void SystemVerilogHirBuilder::add_type_payload(
    const frontend::TypeAliasDeclaration& input,
    sv::TypeDefinition& output,
    const semantic::ScopeId scope,
    const semantic::OriginId parent)
{
    add_packed_members(input.type, output, scope, parent);
    for (const auto& literal : input.enum_literals) {
        const auto literal_id = add_declaration_record(
            scope,
            sv::DeclarationForm::enumeration_literal,
            semantic::DeclarationKind::enumeration_literal,
            literal.name,
            literal.span,
            parent);
        auto& literal_declaration = declaration(literal_id);
        sv::TypeReference literal_type;
        literal_type.target = {
            output.id, literal_declaration.source, output.name
        };
        const auto literal_initializer = expression(
            literal.value, scope, literal_declaration.origin);
        const auto literal_value = ensure_value(
            literal.name,
            scope,
            semantic::ValueKind::enumeration_literal,
            literal_type,
            literal_declaration.source,
            literal_declaration.origin);
        literal_declaration.type = literal_type;
        literal_declaration.initializer = literal_initializer;
        literal_declaration.declared_value = literal_value;
        output.enumeration_literals.push_back({ literal_id,
            literal_value,
            literal.name,
            literal_initializer,
            literal_declaration.source });
    }
    if (input.type.systemverilog_container) {
        const auto& container = *input.type.systemverilog_container;
        sv::ContainerType converted;
        converted.form = type_form(input.type);
        converted.queue_maximum = container.queue_maximum
            ? expression(*container.queue_maximum, scope, parent)
            : std::nullopt;
        if (container.associative_index_type) {
            converted.associative_index = type_reference(
                *container.associative_index_type,
                container.span,
                scope,
                parent);
        }
        if (!container.static_range_expressions.empty()) {
            for (const auto& range : container.static_range_expressions) {
                converted.static_dimensions.push_back(packed_range(
                    range, scope, parent));
            }
        } else if (container.static_range) {
            converted.static_dimensions.push_back(packed_range(
                *container.static_range, source(container.span)));
        }
        converted.source = source(container.span);
        output.container = std::move(converted);
    }
}

void SystemVerilogHirBuilder::add_packed_members(
    const frontend::Type& input,
    sv::TypeDefinition& output,
    const semantic::ScopeId scope,
    const semantic::OriginId parent)
{
    for (const auto& member : input.packed_members) {
        frontend::Type member_type;
        member_type.domain = member.domain;
        member_type.spelling = member.spelling;
        member_type.packed_range = member.packed_range;
        member_type.is_signed = member.is_signed;
        member_type.packed_range_expression = member.packed_range_expression;
        if (!member.nested_types.empty()) {
            member_type = member.nested_types.front();
        }
        auto converted = type_reference(
            member_type, member.span, scope, parent);
        if (!converted.target.target.valid()
            && member.nested_types.empty()) {
            converted.target.target = find_type(
                scope, member.spelling);
        }
        output.members.push_back({ member.name,
            std::move(converted),
            member.lsb_offset,
            source(member.span),
            member.initializer
                ? expression(*member.initializer, scope, parent)
                : std::nullopt });
    }
}

template <typename Input>
[[nodiscard]] semantic::DeclarationId SystemVerilogHirBuilder::add_object(
    const Input& input,
    const semantic::ScopeId scope,
    const semantic::OriginId parent,
    const sv::DeclarationForm form,
    const semantic::DeclarationKind declaration_kind,
    const semantic::ValueKind value_kind,
    const sv::Direction object_direction,
    const std::optional<frontend::Expression>& initializer)
{
    const auto id = add_declaration_record(
        scope, form, declaration_kind, input.name, input.span, parent);
    auto& output = declaration(id);
    output.type = type_reference(
        input.type, input.span, scope, output.origin);
    output.direction = object_direction;
    output.declared_value = ensure_value(
        input.name,
        scope,
        value_kind,
        *output.type,
        output.source,
        output.origin);
    if (initializer) {
        output.initializer = expression(*initializer, scope, output.origin);
    }
    return id;
}

[[nodiscard]] semantic::DeclarationId SystemVerilogHirBuilder::add_parameter(
    const frontend::ParameterDeclaration& input,
    const semantic::ScopeId scope,
    const semantic::OriginId parent)
{
    const auto type_parameter = input.kind == frontend::ParameterKind::Type;
    const auto id = add_declaration_record(
        scope,
        type_parameter
            ? sv::DeclarationForm::type_parameter
            : (input.local ? sv::DeclarationForm::local_parameter
                           : sv::DeclarationForm::parameter),
        type_parameter ? semantic::DeclarationKind::type
                       : semantic::DeclarationKind::generic,
        input.name,
        input.span,
        parent);
    auto& output = declaration(id);
    output.type = type_reference(
        input.type, input.span, scope, output.origin);
    if (input.default_type) {
        output.default_type = type_reference(
            *input.default_type, input.span, scope, output.origin);
    }
    if (type_parameter) {
        output.declared_type = ensure_type(
            input.name,
            scope,
            semantic::TypeKind::declaration,
            *output.type,
            output.source,
            output.origin);
    } else {
        output.declared_value = ensure_value(
            input.name,
            scope,
            semantic::ValueKind::parameter,
            *output.type,
            output.source,
            output.origin);
        output.initializer = expression(
            input.default_value, scope, output.origin);
    }
    return id;
}

[[nodiscard]] semantic::DeclarationId SystemVerilogHirBuilder::add_signal(
    const frontend::SignalDeclaration& input,
    const semantic::ScopeId scope,
    const semantic::OriginId parent)
{
    const auto port = input.is_port;
    auto net = !port && systemverilog_net(input.type);
    if (!port && !net && !input.type.named_type.empty()) {
        const auto type_id = find_type(scope, input.type.named_type);
        const auto type = std::ranges::find(
            hir_.types(), type_id, &sv::TypeDefinition::id);
        if (type != hir_.types().end()) {
            net = declaration(type->declaration).form
                == sv::DeclarationForm::nettype_declaration;
        }
    }
    const auto id = add_object(
        input,
        scope,
        parent,
        port ? sv::DeclarationForm::port
             : net ? sv::DeclarationForm::net
                   : sv::DeclarationForm::variable,
        port ? semantic::DeclarationKind::port
             : net ? semantic::DeclarationKind::signal
                   : semantic::DeclarationKind::variable,
        port ? semantic::ValueKind::port
             : net ? semantic::ValueKind::signal
                   : semantic::ValueKind::variable,
        direction(input.direction),
        input.default_value);
    declaration(id).interface_type = input.interface_type;
    declaration(id).modport = input.modport;
    if (input.net_delay) {
        declaration(id).delay = instance_delay(
            *input.net_delay, scope, declaration(id).origin);
    }
    if (input.drive_strength) {
        declaration(id).drive_zero = static_cast<std::uint8_t>(
            input.drive_strength->zero);
        declaration(id).drive_one = static_cast<std::uint8_t>(
            input.drive_strength->one);
    }
    if (input.charge_strength) {
        declaration(id).charge_strength = static_cast<std::uint8_t>(
            input.charge_strength->rank);
    }
    if (input.charge_decay) {
        declaration(id).charge_decay = instance_delay(
            *input.charge_decay, scope, declaration(id).origin);
    }
    return id;
}

[[nodiscard]] semantic::DeclarationId SystemVerilogHirBuilder::add_variable(
    const frontend::VariableDeclaration& input,
    const semantic::ScopeId scope,
    const semantic::OriginId parent)
{
    const auto id = add_object(
        input,
        scope,
        parent,
        sv::DeclarationForm::variable,
        semantic::DeclarationKind::variable,
        semantic::ValueKind::variable,
        sv::Direction::unknown,
        input.initializer);
    if (input.type.systemverilog_virtual_interface) {
        declaration(id).interface_type
            = input.type.systemverilog_interface_type;
        declaration(id).modport
            = input.type.systemverilog_interface_modport;
    }
    return id;
}

[[nodiscard]] semantic::DeclarationId SystemVerilogHirBuilder::add_function_argument(
    const frontend::FunctionArgument& input,
    const semantic::ScopeId scope,
    const semantic::OriginId parent)
{
    const auto id = add_object(
        input,
        scope,
        parent,
        sv::DeclarationForm::port,
        semantic::DeclarationKind::port,
        semantic::ValueKind::parameter,
        input.reference ? sv::Direction::ref : direction(input.direction),
        input.default_value);
    declaration(id).const_reference = input.const_reference;
    declaration(id).static_reference = input.static_reference;
    return id;
}

[[nodiscard]] semantic::DeclarationId SystemVerilogHirBuilder::add_task_argument(
    const frontend::TaskArgument& input,
    const semantic::ScopeId scope,
    const semantic::OriginId parent)
{
    const auto id = add_object(
        input,
        scope,
        parent,
        sv::DeclarationForm::port,
        semantic::DeclarationKind::port,
        semantic::ValueKind::parameter,
        input.reference ? sv::Direction::ref : direction(input.direction),
        input.default_value);
    declaration(id).const_reference = input.const_reference;
    declaration(id).static_reference = input.static_reference;
    return id;
}

[[nodiscard]] semantic::DeclarationId SystemVerilogHirBuilder::add_function(
    const frontend::FunctionDeclaration& input,
    const semantic::ScopeId scope,
    const semantic::OriginId parent)
{
    const auto id = add_declaration_record(
        scope,
        sv::DeclarationForm::function,
        semantic::DeclarationKind::function,
        input.name,
        input.span,
        parent);
    const auto declaration_source = declaration(id).source;
    const auto declaration_origin = declaration(id).origin;
    const auto function_scope = nested_scope(
        scope, input.name, declaration_source, declaration_origin);
    auto result_type = type_reference(
        input.return_type, input.span, scope, declaration_origin);
    auto& output = declaration(id);
    output.type = result_type;
    output.nested_scope = function_scope;
    const auto callable_lifetime = lifetime(
        input.automatic, input.lifetime_explicit);
    output.lifetime = callable_lifetime;
    output.declared_value = ensure_value(
        input.name,
        scope,
        semantic::ValueKind::function,
        result_type,
        declaration_source,
        declaration_origin);
    sv::CallableProfile profile;
    profile.function = true;
    profile.return_type = result_type;
    profile.lifetime = callable_lifetime;
    for (const auto& argument : input.arguments) {
        profile.formals.push_back(add_function_argument(
            argument, function_scope, declaration_origin));
    }
    declaration(id).callable = std::move(profile);
    auto children = declaration(id).callable->formals;
    add_children(
        input.constants,
        function_scope,
        declaration_origin,
        children,
        &SystemVerilogHirBuilder::add_parameter);
    add_children(
        input.type_aliases,
        function_scope,
        declaration_origin,
        children,
        &SystemVerilogHirBuilder::add_type_declaration);
    add_children(
        input.variables,
        function_scope,
        declaration_origin,
        children,
        &SystemVerilogHirBuilder::add_variable);
    add_children(
        input.functions,
        function_scope,
        declaration_origin,
        children,
        &SystemVerilogHirBuilder::add_function);
    declaration(id).children = std::move(children);
    return id;
}

[[nodiscard]] semantic::DeclarationId SystemVerilogHirBuilder::add_task(
    const frontend::TaskDeclaration& input,
    const semantic::ScopeId scope,
    const semantic::OriginId parent)
{
    const auto id = add_declaration_record(
        scope,
        sv::DeclarationForm::task,
        semantic::DeclarationKind::task,
        input.name,
        input.span,
        parent);
    const auto declaration_source = declaration(id).source;
    const auto declaration_origin = declaration(id).origin;
    const auto task_scope = nested_scope(
        scope, input.name, declaration_source, declaration_origin);
    auto& output = declaration(id);
    output.nested_scope = task_scope;
    const auto callable_lifetime = lifetime(
        input.automatic, input.lifetime_explicit);
    output.lifetime = callable_lifetime;
    sv::TypeReference no_type;
    no_type.target.source = declaration_source;
    output.declared_value = ensure_value(
        input.name,
        scope,
        semantic::ValueKind::task,
        no_type,
        declaration_source,
        declaration_origin);
    sv::CallableProfile profile;
    profile.function = false;
    profile.lifetime = callable_lifetime;
    for (const auto& argument : input.arguments) {
        profile.formals.push_back(add_task_argument(
            argument, task_scope, declaration_origin));
    }
    declaration(id).callable = std::move(profile);
    auto children = declaration(id).callable->formals;
    add_children(
        input.variables,
        task_scope,
        declaration_origin,
        children,
        &SystemVerilogHirBuilder::add_variable);
    declaration(id).children = std::move(children);
    return id;
}

[[nodiscard]] semantic::DeclarationId SystemVerilogHirBuilder::add_modport(
    const frontend::SystemVerilogModport& input,
    const semantic::ScopeId scope,
    const semantic::OriginId parent)
{
    return add_declaration_record(
        scope,
        sv::DeclarationForm::modport,
        semantic::DeclarationKind::port,
        input.name,
        input.span,
        parent);
}

[[nodiscard]] sv::Delay SystemVerilogHirBuilder::instance_delay(
    const frontend::Delay& input,
    const semantic::ScopeId scope,
    const semantic::OriginId parent)
{
    const auto make_value = [&](const std::uint64_t magnitude,
                                const std::uint64_t divisor,
                                const std::string& unit,
                                const std::optional<frontend::Expression>& value,
                                const frontend::SourceSpan& span) {
        return sv::DelayValue { magnitude, divisor, unit,
            value ? expression(*value, scope, parent) : std::nullopt,
            source(span) };
    };
    sv::Delay output;
    output.primary = make_value(input.magnitude, input.divisor, input.unit,
        input.expression, input.span);
    const auto alternative = [&](const auto& value)
        -> std::optional<sv::DelayValue> {
        if (!value) {
            return std::nullopt;
        }
        return make_value(value->magnitude, value->divisor, value->unit,
            value->expression, value->span);
    };
    output.minimum = alternative(input.minimum);
    output.typical = alternative(input.typical);
    output.maximum = alternative(input.maximum);
    for (const auto& value : input.additional_values) {
        output.additional.push_back(instance_delay(value, scope, parent));
    }
    return output;
}

[[nodiscard]] sv::TimingRecord SystemVerilogHirBuilder::timing_record(
    const frontend::VerilogSpecifyBlock& input,
    const semantic::ScopeId scope,
    const semantic::OriginId parent,
    const std::size_t index)
{
    const auto specify_edge = [](const frontend::VerilogSpecifyEdge edge) {
        switch (edge) {
        case frontend::VerilogSpecifyEdge::None:
            return sv::SpecifyEdge::none;
        case frontend::VerilogSpecifyEdge::Posedge:
            return sv::SpecifyEdge::positive;
        case frontend::VerilogSpecifyEdge::Negedge:
            return sv::SpecifyEdge::negative;
        case frontend::VerilogSpecifyEdge::Edge:
            return sv::SpecifyEdge::any;
        }
        return sv::SpecifyEdge::none;
    };
    const auto path_kind = [](const frontend::VerilogModulePathKind kind) {
        return kind == frontend::VerilogModulePathKind::Full
            ? sv::ModulePathKind::full
            : sv::ModulePathKind::parallel;
    };
    const auto path_polarity = [](const frontend::VerilogPathPolarity polarity) {
        switch (polarity) {
        case frontend::VerilogPathPolarity::None:
            return sv::PathPolarity::none;
        case frontend::VerilogPathPolarity::Positive:
            return sv::PathPolarity::positive;
        case frontend::VerilogPathPolarity::Negative:
            return sv::PathPolarity::negative;
        }
        return sv::PathPolarity::none;
    };
    const auto pulse_style = [](const frontend::VerilogPulseStyle style) {
        return style == frontend::VerilogPulseStyle::Ondetect
            ? sv::PulseStyle::ondetect
            : sv::PulseStyle::onevent;
    };
    const auto timing_check_kind
        = [](const frontend::VerilogTimingCheckKind kind) {
        switch (kind) {
        case frontend::VerilogTimingCheckKind::Setup:
            return sv::TimingCheckKind::setup;
        case frontend::VerilogTimingCheckKind::Hold:
            return sv::TimingCheckKind::hold;
        case frontend::VerilogTimingCheckKind::SetupHold:
            return sv::TimingCheckKind::setup_hold;
        case frontend::VerilogTimingCheckKind::Recovery:
            return sv::TimingCheckKind::recovery;
        case frontend::VerilogTimingCheckKind::Removal:
            return sv::TimingCheckKind::removal;
        case frontend::VerilogTimingCheckKind::RecRem:
            return sv::TimingCheckKind::recovery_removal;
        case frontend::VerilogTimingCheckKind::Skew:
            return sv::TimingCheckKind::skew;
        case frontend::VerilogTimingCheckKind::TimeSkew:
            return sv::TimingCheckKind::time_skew;
        case frontend::VerilogTimingCheckKind::FullSkew:
            return sv::TimingCheckKind::full_skew;
        case frontend::VerilogTimingCheckKind::Period:
            return sv::TimingCheckKind::period;
        case frontend::VerilogTimingCheckKind::Width:
            return sv::TimingCheckKind::width;
        case frontend::VerilogTimingCheckKind::NoChange:
            return sv::TimingCheckKind::no_change;
        }
        return sv::TimingCheckKind::setup;
    };

    sv::TimingRecord output;
    output.source = source(input.span);
    output.origin = origin(output.source, parent,
        "specify block " + std::to_string(index));
    output.specparams.reserve(input.specparams.size());
    for (const auto& input_specparam : input.specparams) {
        sv::SpecparamDeclaration specparam;
        specparam.name = input_specparam.name;
        specparam.path_pulse = input_specparam.path_pulse;
        specparam.path_pulse_input = input_specparam.path_pulse_input;
        specparam.path_pulse_output = input_specparam.path_pulse_output;
        specparam.source = source(input_specparam.span);
        specparam.origin = origin(specparam.source, output.origin,
            "specparam " + specparam.name);
        specparam.value
            = expression(input_specparam.value, scope, specparam.origin)
                  .value_or(semantic::ExpressionId { });
        const auto optional_expression = [&](const auto& value) {
            return value
                ? expression(*value, scope, specparam.origin)
                : std::nullopt;
        };
        specparam.minimum = optional_expression(input_specparam.minimum);
        specparam.typical = optional_expression(input_specparam.typical);
        specparam.maximum = optional_expression(input_specparam.maximum);
        specparam.path_pulse_error_limit = optional_expression(
            input_specparam.path_pulse_error_limit);
        if (input_specparam.path_pulse_reject_delay) {
            specparam.path_pulse_reject_delay = instance_delay(
                *input_specparam.path_pulse_reject_delay,
                scope,
                specparam.origin);
        }
        if (input_specparam.path_pulse_error_delay) {
            specparam.path_pulse_error_delay = instance_delay(
                *input_specparam.path_pulse_error_delay,
                scope,
                specparam.origin);
        }
        output.specparams.push_back(std::move(specparam));
    }

    output.module_paths.reserve(input.module_paths.size());
    for (std::size_t path_index = 0;
        path_index < input.module_paths.size(); ++path_index) {
        const auto& input_path = input.module_paths[path_index];
        sv::ModulePathDeclaration path;
        path.kind = path_kind(input_path.kind);
        path.source_edge = specify_edge(input_path.source_edge);
        path.polarity = path_polarity(input_path.polarity);
        path.conditional = input_path.conditional;
        path.ifnone = input_path.ifnone;
        path.source = source(input_path.span);
        path.origin = origin(path.source, output.origin,
            "module path " + std::to_string(path_index));
        for (const auto& input_source : input_path.sources) {
            if (const auto value = expression(
                    input_source, scope, path.origin)) {
                path.sources.push_back(*value);
            }
        }
        for (const auto& input_destination : input_path.destinations) {
            if (const auto value = expression(
                    input_destination, scope, path.origin)) {
                path.destinations.push_back(*value);
            }
        }
        path.destination_data_source = expression(
            input_path.destination_data_source, scope, path.origin);
        path.condition = expression(
            input_path.condition, scope, path.origin);
        for (const auto& input_delay : input_path.delays) {
            path.delays.push_back(instance_delay(
                input_delay, scope, path.origin));
        }
        output.module_paths.push_back(std::move(path));
    }

    output.pulse_declarations.reserve(input.pulse_declarations.size());
    for (std::size_t pulse_index = 0;
        pulse_index < input.pulse_declarations.size(); ++pulse_index) {
        const auto& input_pulse = input.pulse_declarations[pulse_index];
        sv::SpecifyPulseDeclaration pulse;
        pulse.style = pulse_style(input_pulse.style);
        pulse.controls_style = input_pulse.controls_style;
        pulse.show_cancelled = input_pulse.show_cancelled;
        pulse.source = source(input_pulse.span);
        pulse.origin = origin(pulse.source, output.origin,
            "specify pulse " + std::to_string(pulse_index));
        for (const auto& input_terminal : input_pulse.terminals) {
            if (const auto terminal = expression(
                    input_terminal, scope, pulse.origin)) {
                pulse.terminals.push_back(*terminal);
            }
        }
        output.pulse_declarations.push_back(std::move(pulse));
    }

    output.timing_checks.reserve(input.timing_checks.size());
    for (std::size_t check_index = 0;
        check_index < input.timing_checks.size(); ++check_index) {
        const auto& input_check = input.timing_checks[check_index];
        sv::TimingCheckDeclaration check;
        check.kind = timing_check_kind(input_check.kind);
        check.source = source(input_check.span);
        check.origin = origin(check.source, output.origin,
            "timing check " + std::to_string(check_index));
        const auto timing_event = [&](const auto& input_event,
                                      const std::string_view role) {
            sv::TimingCheckEvent event;
            event.source = input_event.span.empty()
                ? check.source
                : source(input_event.span);
            event.origin = origin(event.source, check.origin, role);
            event.expression
                = expression(input_event.expression, scope, event.origin)
                      .value_or(semantic::ExpressionId { });
            event.edge = specify_edge(input_event.edge);
            event.edge_descriptors = input_event.edge_descriptors;
            event.condition = expression(
                input_event.condition, scope, event.origin);
            return event;
        };
        check.reference_event = timing_event(
            input_check.reference_event, "reference event");
        if (input_check.data_event.expression.valid()) {
            check.data_event = timing_event(
                input_check.data_event, "data event");
        }
        for (const auto& input_limit : input_check.limits) {
            if (const auto limit = expression(
                    input_limit, scope, check.origin)) {
                check.limits.push_back(*limit);
            }
        }
        for (const auto& input_limit : input_check.normalized_limits) {
            check.normalized_limits.push_back(instance_delay(
                input_limit, scope, check.origin));
        }
        check.threshold = expression(
            input_check.threshold, scope, check.origin);
        if (input_check.normalized_threshold) {
            check.normalized_threshold = instance_delay(
                *input_check.normalized_threshold,
                scope,
                check.origin);
        }
        check.notifier = expression(
            input_check.notifier, scope, check.origin);
        check.timestamp_condition = expression(
            input_check.timestamp_condition, scope, check.origin);
        check.timecheck_condition = expression(
            input_check.timecheck_condition, scope, check.origin);
        check.delayed_reference = expression(
            input_check.delayed_reference, scope, check.origin);
        check.delayed_data = expression(
            input_check.delayed_data, scope, check.origin);
        check.event_based_flag = expression(
            input_check.event_based_flag, scope, check.origin);
        check.remain_active_flag = expression(
            input_check.remain_active_flag, scope, check.origin);
        output.timing_checks.push_back(std::move(check));
    }
    return output;
}

[[nodiscard]] sv::SourceToken SystemVerilogHirBuilder::source_token(
    const frontend::Token& input)
{
    return {
        static_cast<std::uint16_t>(input.kind),
        input.text,
        source(input.span),
        static_cast<sv::GeneratedTextKind>(input.generated_text)
    };
}

[[nodiscard]] std::vector<sv::SourceToken> SystemVerilogHirBuilder::source_tokens(
    const std::vector<frontend::Token>& input)
{
    std::vector<sv::SourceToken> output;
    output.reserve(input.size());
    for (const auto& token : input) {
        output.push_back(source_token(token));
    }
    return output;
}

[[nodiscard]] sv::CovergroupFormal SystemVerilogHirBuilder::covergroup_formal(
    const frontend::SystemVerilogCovergroupFormal& input)
{
    return {
        direction(input.direction),
        input.const_ref,
        source_tokens(input.type_tokens),
        input.name,
        source(input.name_span),
        source_tokens(input.default_tokens),
        source(input.span)
    };
}

[[nodiscard]] sv::CovergroupOptionAssignment SystemVerilogHirBuilder::covergroup_option(
    const frontend::SystemVerilogCovergroupOptionAssignment& input)
{
    sv::CovergroupOptionAssignment output;
    output.scope = static_cast<sv::CovergroupOptionScope>(input.scope);
    output.name = input.name;
    output.name_source = source(input.name_span);
    output.value_tokens = source_tokens(input.value_tokens);
    output.evaluated_value = input.evaluated_value;
    output.evaluated_real_bits = input.evaluated_real_bits;
    output.inherited = input.inherited;
    output.source = source(input.span);
    return output;
}

[[nodiscard]] sv::CoverageBinValue SystemVerilogHirBuilder::coverage_bin_value(
    const frontend::SystemVerilogCoverageBinValue& input)
{
    sv::CoverageBinValue output;
    output.tokens = source_tokens(input.tokens);
    output.exact_value = input.exact_value;
    output.range_left = input.range_left;
    output.range_right = input.range_right;
    output.wildcard_value = input.wildcard_value;
    output.wildcard_mask = input.wildcard_mask;
    output.width = input.width;
    output.wildcard = input.wildcard;
    output.exact_bits = input.exact_bits;
    output.exact_unknown_bits = input.exact_unknown_bits;
    output.range_left_bits = input.range_left_bits;
    output.range_right_bits = input.range_right_bits;
    output.wildcard_value_bits = input.wildcard_value_bits;
    output.wildcard_mask_bits = input.wildcard_mask_bits;
    output.exact_signed = input.exact_signed;
    output.range_left_signed = input.range_left_signed;
    output.range_right_signed = input.range_right_signed;
    output.exact_real_bits = input.exact_real_bits;
    output.range_left_real_bits = input.range_left_real_bits;
    output.range_right_real_bits = input.range_right_real_bits;
    output.range_left_inclusive = input.range_left_inclusive;
    output.range_right_inclusive = input.range_right_inclusive;
    output.source = source(input.span);
    return output;
}

[[nodiscard]] sv::CoverageTransitionSequence SystemVerilogHirBuilder::coverage_transition(
    const frontend::SystemVerilogCoverageTransitionSequence& input)
{
    sv::CoverageTransitionSequence output;
    output.source = source(input.span);
    for (const auto& input_step : input.steps) {
        sv::CoverageTransitionStep step;
        for (const auto& input_value : input_step.values) {
            step.values.push_back(coverage_bin_value(input_value));
        }
        step.repetition.kind =
            static_cast<sv::CoverageTransitionRepetitionKind>(
                input_step.repetition.kind);
        step.repetition.minimum = input_step.repetition.minimum;
        step.repetition.maximum = input_step.repetition.maximum;
        step.repetition.source = source(input_step.repetition.span);
        step.source = source(input_step.span);
        output.steps.push_back(std::move(step));
    }
    for (const auto& input_delay : input.delays) {
        output.delays.push_back({
            input_delay.minimum,
            input_delay.maximum,
            source(input_delay.span)
        });
    }
    return output;
}

[[nodiscard]] sv::CoverageBin SystemVerilogHirBuilder::coverage_bin(
    const frontend::SystemVerilogCoverageBin& input,
    const semantic::OriginId parent)
{
    sv::CoverageBin output;
    output.kind = static_cast<sv::CoverageBinKind>(input.kind);
    output.selection = static_cast<sv::CoverageBinSelection>(input.selection);
    output.name = input.name;
    output.declaration_index = input.declaration_index;
    output.source_name = input.source_name;
    if (input.array_index) {
        output.array_index = static_cast<std::uint64_t>(*input.array_index);
    }
    if (input.declared_array_size) {
        output.declared_array_size = static_cast<std::uint64_t>(
            *input.declared_array_size);
    }
    output.wildcard = input.wildcard;
    for (const auto& input_value : input.values) {
        output.values.push_back(coverage_bin_value(input_value));
    }
    for (const auto& input_transition : input.transitions) {
        output.transitions.push_back(coverage_transition(input_transition));
    }
    output.cross_selection_tokens = source_tokens(
        input.cross_selection_tokens);
    output.with_tokens = source_tokens(input.with_tokens);
    output.with_source = source(input.with_span);
    output.iff_tokens = source_tokens(input.iff_tokens);
    output.iff_source = source(input.iff_span);
    output.weight = input.weight;
    output.goal = input.goal;
    output.at_least = input.at_least;
    output.source = source(input.span);
    output.origin = origin(output.source, parent, input.name);
    return output;
}

[[nodiscard]] sv::CoverageItem SystemVerilogHirBuilder::coverage_item(
    const frontend::SystemVerilogCoverageDeclaration& input,
    const semantic::ScopeId scope,
    const semantic::OriginId parent)
{
    sv::CoverageItem output;
    output.kind = static_cast<sv::CoverageItemKind>(input.kind);
    output.name = input.name;
    output.name_source = source(input.name_span);
    output.explicit_name = input.explicit_name;
    output.declaration_index = input.declaration_index;
    output.origin_covergroup_identity = input.origin_covergroup_identity;
    output.inherited = input.inherited;
    output.sampled_scalar_kind = static_cast<sv::CoverageScalarKind>(
        input.sampled_scalar_kind);
    output.effective_real_interval_bits = input.effective_real_interval_bits;
    output.expression_tokens = source_tokens(input.expression_tokens);
    output.expression_source = source(input.expression_span);
    for (const auto& input_operand : input.cross_operands) {
        sv::CoverageCrossOperand operand;
        operand.target = name(input_operand.name, input_operand.span, scope);
        operand.tokens = source_tokens(input_operand.tokens);
        if (input_operand.resolved_declaration_index) {
            operand.resolved_declaration_index =
                static_cast<std::uint64_t>(
                    *input_operand.resolved_declaration_index);
        }
        operand.implicit_coverpoint = input_operand.implicit_coverpoint;
        operand.source = source(input_operand.span);
        output.cross_operands.push_back(std::move(operand));
    }
    output.iff_tokens = source_tokens(input.iff_tokens);
    output.iff_source = source(input.iff_span);
    output.body_tokens = source_tokens(input.body_tokens);
    output.body_source = source(input.body_span);
    for (const auto& input_option : input.option_assignments) {
        output.option_assignments.push_back(covergroup_option(input_option));
    }
    output.effective_weight = input.effective_weight;
    output.effective_goal = input.effective_goal;
    output.effective_at_least = input.effective_at_least;
    output.source = source(input.span);
    output.origin = origin(output.source, parent, input.name);
    for (const auto& input_bin : input.bins) {
        output.bins.push_back(coverage_bin(input_bin, output.origin));
    }
    for (const auto& input_reference : input.references) {
        output.references.push_back({
            static_cast<sv::CoverageReferenceKind>(input_reference.kind),
            name(input_reference.canonical_name, input_reference.span, scope),
            source_tokens(input_reference.tokens),
            source(input_reference.span)
        });
    }
    return output;
}

[[nodiscard]] sv::CovergroupDeclaration SystemVerilogHirBuilder::covergroup_declaration(
    const frontend::SystemVerilogCovergroupDeclaration& input,
    const semantic::ScopeId scope,
    const std::optional<semantic::OriginId> parent)
{
    sv::CovergroupDeclaration output;
    output.owner_kind = static_cast<sv::CovergroupOwnerKind>(input.owner_kind);
    output.standard = std::string {
        frontend::revision_string(input.standard_revision)
    };
    output.name = input.name;
    output.extends_parent = input.extends_parent;
    output.resolved_base_identity = input.resolved_base_identity;
    output.owner_identity = input.owner_identity;
    output.canonical_identity = input.canonical_identity;
    output.specialization_identity = input.specialization_identity;
    output.runtime_identity_prefix = input.runtime_identity_prefix;
    output.name_source = source(input.name_span);
    output.header_tokens = source_tokens(input.header_tokens);
    output.header_source = source(input.header_span);
    for (const auto& input_formal : input.formals) {
        output.formals.push_back(covergroup_formal(input_formal));
    }
    if (input.sampling) {
        sv::CovergroupSampling sampling;
        sampling.kind = static_cast<sv::CovergroupSamplingKind>(
            input.sampling->kind);
        sampling.tokens = source_tokens(input.sampling->tokens);
        for (const auto& input_formal : input.sampling->formals) {
            sampling.formals.push_back(covergroup_formal(input_formal));
        }
        sampling.source = source(input.sampling->span);
        output.sampling = std::move(sampling);
    }
    output.body_tokens = source_tokens(input.body_tokens);
    output.body_source = source(input.body_span);
    for (const auto& input_option : input.option_assignments) {
        output.option_assignments.push_back(covergroup_option(input_option));
    }
    output.effective_instance_weight = input.effective_instance_weight;
    output.effective_instance_goal = input.effective_instance_goal;
    output.effective_type_weight = input.effective_type_weight;
    output.effective_type_goal = input.effective_type_goal;
    output.effective_per_instance = input.effective_per_instance;
    output.effective_merge_instances = input.effective_merge_instances;
    output.effective_cross_retain_auto_bins =
        input.effective_cross_retain_auto_bins;
    output.effective_real_interval_bits = input.effective_real_interval_bits;
    output.end_name = input.end_name;
    output.end_name_source = source(input.end_name_span);
    output.source = source(input.span);
    output.origin = model_.add_origin(
        semantic::OriginKind::parsed,
        output.source,
        parent,
        input.name);
    for (const auto& input_item : input.coverage_declarations) {
        output.items.push_back(coverage_item(
            input_item, scope, output.origin));
    }
    return output;
}

[[nodiscard]] semantic::ScopeId SystemVerilogHirBuilder::coverage_scope(
    const std::string_view owner_identity) const
{
    const auto unit = std::ranges::find_if(
        hir_.units(), [&](const sv::Unit& candidate_unit) {
            const auto library = candidate_unit.library.empty()
                ? std::string_view { "work" }
                : std::string_view { candidate_unit.library };
            return owner_identity
                == std::string { library } + "." + candidate_unit.name;
        });
    if (unit != hir_.units().end())
        return unit->scope;
    const auto declaration = std::ranges::find_if(
        hir_.classes(), [&](const sv::ClassDeclaration& candidate) {
            return owner_identity == candidate.canonical_identity
                || owner_identity.ends_with(
                    "." + candidate.canonical_identity);
        });
    if (declaration != hir_.classes().end())
        return declaration->scope;
    throw std::logic_error {
        "SystemVerilog coverage record has no semantic owner scope"
    };
}

[[nodiscard]] sv::CovergroupInstance SystemVerilogHirBuilder::covergroup_instance(
    const frontend::SystemVerilogCovergroupInstance& input,
    const frontend::SystemVerilogCovergroupInstanceSyntax& syntax)
{
    sv::CovergroupInstance output;
    output.name = input.name;
    output.owner_identity = input.owner_identity;
    output.declaration_identity = input.declaration_identity;
    output.specialization_identity = input.specialization_identity;
    output.runtime_identity = input.runtime_identity;
    output.source = source(input.span);
    const auto scope = coverage_scope(input.owner_identity);
    output.owner_scope = scope;
    const auto parent = model_.scopes().at(scope.value()).origin;
    output.origin = model_.add_origin(
        semantic::OriginKind::parsed,
        output.source,
        parent,
        input.runtime_identity);
    for (const auto& input_actual : syntax.constructor_actuals) {
        if (const auto actual = expression(input_actual, scope, output.origin)) {
            output.constructor_actuals.push_back(*actual);
        }
    }
    for (const auto& input_option : input.initial_option_state) {
        output.initial_option_state.push_back(covergroup_option(input_option));
    }
    for (const auto& input_call : syntax.sample_calls) {
        sv::CovergroupSampleCall call;
        call.declaration_identity = input_call.declaration_identity;
        call.instance_identity = input_call.instance_identity;
        call.source = source(input_call.span);
        for (const auto& input_actual : input_call.actuals) {
            if (const auto actual = expression(
                    input_actual, scope, output.origin)) {
                call.actuals.push_back(*actual);
            }
        }
        output.sample_calls.push_back(std::move(call));
    }
    for (const auto& input_hit : input.bin_hits) {
        output.bin_hits.push_back({
            input_hit.coverage_declaration_index,
            input_hit.bin_declaration_index,
            input_hit.identity,
            input_hit.automatic_value,
            input_hit.automatic_value_bits,
            input_hit.automatic_unknown_bits,
            input_hit.automatic_width,
            input_hit.automatic_signed,
            input_hit.hit_count,
            input_hit.at_least,
            input_hit.covered
        });
    }
    for (const auto& input_progress : input.transition_progress) {
        output.transition_progress.push_back({
            input_progress.coverage_declaration_index,
            input_progress.bin_declaration_index,
            input_progress.sequence_index,
            input_progress.step_index,
            input_progress.repetition_count,
            input_progress.samples_since_step
        });
    }
    for (const auto& input_sample : input.previous_samples) {
        output.previous_samples.push_back({
            input_sample.coverage_declaration_index,
            input_sample.value,
            input_sample.unknown_mask,
            input_sample.width,
            input_sample.value_bits,
            input_sample.unknown_bits,
            input_sample.signed_value
        });
    }
    for (const auto& input_state : input.cross_bin_state) {
        sv::CoverageCrossBinState state;
        state.coverage_declaration_index =
            input_state.coverage_declaration_index;
        if (input_state.bin_declaration_index) {
            state.bin_declaration_index = static_cast<std::uint64_t>(
                *input_state.bin_declaration_index);
        }
        state.identity = input_state.identity;
        state.operand_bin_identities = input_state.operand_bin_identities;
        state.hit_count = input_state.hit_count;
        state.exclusion_count = input_state.exclusion_count;
        state.weight = input_state.weight;
        state.goal = input_state.goal;
        state.at_least = input_state.at_least;
        state.covered = input_state.covered;
        state.excluded = input_state.excluded;
        output.cross_bin_state.push_back(std::move(state));
    }
    for (const auto& input_report : input.illegal_bin_reports) {
        output.illegal_bin_reports.push_back({
            input_report.bin_identity,
            input_report.sampled_value,
            input_report.sampled_unknown_mask,
            input_report.sampled_width,
            input_report.sampled_value_bits,
            input_report.sampled_unknown_bits,
            input_report.sampled_signed,
            static_cast<sv::CoverageScalarKind>(
                input_report.sampled_scalar_kind),
            input_report.sampled_scalar_bits,
            source(input_report.span)
        });
    }
    output.class_member_template = input.class_member_template;
    output.cross_inventory_initialized = input.cross_inventory_initialized;
    return output;
}

[[nodiscard]] sv::DpiDeclaration SystemVerilogHirBuilder::dpi_declaration(
    const frontend::SystemVerilogDpiDeclaration& input,
    const semantic::ScopeId scope,
    const std::optional<semantic::OriginId> parent)
{
    sv::DpiDeclaration output;
    output.standard = std::string {
        frontend::revision_string(input.standard_revision)
    };
    output.direction = static_cast<sv::DpiDirection>(input.direction);
    output.owner_kind = static_cast<sv::DpiOwnerKind>(input.owner_kind);
    output.owner_identity = input.owner_identity;
    output.owner_scope = scope;
    output.link_name = input.link_name;
    output.qualifier = static_cast<sv::DpiQualifier>(input.qualifier);
    output.callable_kind = static_cast<sv::DpiCallableKind>(input.callable_kind);
    output.systemverilog_name = input.systemverilog_name;
    output.c_identifier = input.c_identifier;
    output.return_type_tokens = source_tokens(input.return_type_tokens);
    output.formal_tokens = source_tokens(input.formal_tokens);
    for (const auto& input_formal : input.formals) {
        output.formals.push_back({ direction(input_formal.direction),
            input_formal.const_reference,
            source_tokens(input_formal.type_tokens),
            input_formal.name,
            source_tokens(input_formal.dimension_tokens),
            source_tokens(input_formal.default_tokens),
            source_tokens(input_formal.tokens),
            source(input_formal.span) });
    }
    output.profile_tokens = source_tokens(input.profile_tokens);
    output.profile_source = source(input.profile_span);
    output.linkage_name = input.linkage_name;
    output.validated = input.validated;
    if (input.resolved_profile) {
        sv::DpiResolvedProfile profile;
        profile.callable_source = source(
            input.resolved_profile->callable_span);
        if (input.resolved_profile->return_type) {
            profile.return_type = type_reference(
                *input.resolved_profile->return_type,
                input.resolved_profile->callable_span,
                scope,
                parent.value_or(semantic::OriginId { }));
        }
        for (const auto& input_formal : input.resolved_profile->formals) {
            profile.formals.push_back({ direction(input_formal.direction),
                type_reference(input_formal.type, input_formal.span, scope,
                    parent.value_or(semantic::OriginId { })),
                input_formal.name,
                input_formal.reference,
                source(input_formal.span) });
        }
        output.resolved_profile = std::move(profile);
    }
    output.tokens = source_tokens(input.tokens);
    output.source = source(input.span);
    output.origin = model_.add_origin(
        semantic::OriginKind::parsed,
        output.source,
        parent,
        input.systemverilog_name);
    return output;
}

[[nodiscard]] sv::ClockingSkew SystemVerilogHirBuilder::clocking_skew(
    const frontend::SystemVerilogClockingSkew& input,
    const semantic::ScopeId scope,
    const semantic::OriginId parent)
{
    return {
        edge_kind(input.edge),
        input.delay
            ? std::optional<sv::Delay> {
                  instance_delay(*input.delay, scope, parent) }
            : std::nullopt,
        input.one_step, source(input.span)
    };
}

[[nodiscard]] sv::ClockingBlock SystemVerilogHirBuilder::clocking_block(
    const frontend::SystemVerilogClockingBlock& input,
    const semantic::ScopeId scope,
    const semantic::OriginId parent)
{
    sv::ClockingBlock output;
    output.name = input.name;
    output.source = source(input.span);
    output.origin = origin(output.source, parent, input.name);
    for (const auto& input_event : input.event) {
        output.event.push_back({ edge_kind(input_event.edge),
            input_event.signal,
            expression(input_event.expression, scope, output.origin),
            source(input_event.span) });
    }
    if (input.default_input_skew) {
        output.default_input_skew = clocking_skew(
            *input.default_input_skew, scope, output.origin);
    }
    if (input.default_output_skew) {
        output.default_output_skew = clocking_skew(
            *input.default_output_skew, scope, output.origin);
    }
    for (const auto& input_signal : input.signals) {
        sv::ClockingSignal signal;
        signal.name = name(input_signal.name, input_signal.span, scope);
        signal.direction = direction(input_signal.direction);
        if (input_signal.skew) {
            signal.skew = clocking_skew(
                *input_signal.skew, scope, output.origin);
        }
        if (input_signal.expression) {
            signal.expression = expression(
                *input_signal.expression, scope, output.origin);
        }
        signal.source = source(input_signal.span);
        output.signals.push_back(std::move(signal));
    }
    return output;
}

[[nodiscard]] sv::SequenceRange SystemVerilogHirBuilder::sequence_range(
    const frontend::SystemVerilogSequenceRange& input)
{
    return {
        source_tokens(input.minimum_tokens),
        source_tokens(input.maximum_tokens),
        source(input.span)
    };
}

[[nodiscard]] sv::SequenceExpression SystemVerilogHirBuilder::sequence_expression(
    const frontend::SystemVerilogSequenceExpression& input)
{
    sv::SequenceExpression output;
    output.source = source(input.span);
    for (const auto& input_element : input.elements) {
        sv::SequenceElement element;
        element.expression_tokens = source_tokens(
            input_element.expression_tokens);
        element.repetition = static_cast<sv::SequenceRepetitionKind>(
            input_element.repetition);
        if (input_element.repetition_range) {
            element.repetition_range = sequence_range(
                *input_element.repetition_range);
        }
        element.source = source(input_element.span);
        output.elements.push_back(std::move(element));
    }
    for (const auto& input_delay : input.delays) {
        output.delays.push_back({ sequence_range(input_delay.range),
            input_delay.fusion,
            source(input_delay.span) });
    }
    for (const auto& input_operand : input.intersection_operands) {
        output.intersection_operands.push_back({ source_tokens(input_operand.tokens),
            source(input_operand.span) });
    }
    for (const auto& input_operation : input.binary_operations) {
        const auto kind = static_cast<sv::SequenceBinaryKind>(
            input_operation.kind);
        output.binary_operations.push_back({ kind,
            source_tokens(input_operation.left_tokens),
            source_tokens(input_operation.right_tokens),
            source(input_operation.span) });
    }
    for (const auto& input_match : input.first_matches) {
        output.first_matches.push_back({ source_tokens(input_match.sequence_tokens),
            source_tokens(input_match.match_item_tokens),
            source(input_match.span) });
    }
    return output;
}

[[nodiscard]] sv::PropertyExpression SystemVerilogHirBuilder::property_expression(
    const frontend::SystemVerilogPropertyExpression& input)
{
    sv::PropertyExpression output;
    output.source = source(input.span);
    for (const auto& input_implication : input.implications) {
        output.implications.push_back({ static_cast<sv::PropertyImplicationKind>(
                                            input_implication.kind),
            source_tokens(input_implication.antecedent_tokens),
            source_tokens(input_implication.consequent_tokens),
            source(input_implication.span) });
    }
    for (const auto& input_delay : input.delays) {
        output.delays.push_back({ sequence_range(input_delay.range),
            input_delay.fusion,
            source(input_delay.span) });
    }
    for (const auto& input_operation : input.until_operations) {
        const auto kind = static_cast<sv::PropertyUntilKind>(
            input_operation.kind);
        output.until_operations.push_back({ kind,
            source_tokens(input_operation.left_tokens),
            source_tokens(input_operation.right_tokens),
            source(input_operation.span) });
    }
    for (const auto& input_nexttime : input.nexttimes) {
        output.nexttimes.push_back({ static_cast<sv::PropertyNexttimeKind>(input_nexttime.kind),
            source_tokens(input_nexttime.count_tokens),
            source_tokens(input_nexttime.operand_tokens),
            source(input_nexttime.span) });
    }
    for (const auto& input_recurrence : input.recurrences) {
        sv::PropertyRecurrence recurrence;
        recurrence.kind = static_cast<sv::PropertyRecurrenceKind>(
            input_recurrence.kind);
        if (input_recurrence.range) {
            recurrence.range = sequence_range(*input_recurrence.range);
        }
        recurrence.operand_tokens = source_tokens(
            input_recurrence.operand_tokens);
        recurrence.source = source(input_recurrence.span);
        output.recurrences.push_back(std::move(recurrence));
    }
    for (const auto& input_strength : input.sequence_strengths) {
        output.sequence_strengths.push_back({ static_cast<sv::PropertySequenceStrengthKind>(
                                                  input_strength.kind),
            source_tokens(input_strength.sequence_tokens),
            source(input_strength.span) });
    }
    for (const auto& input_abort : input.aborts) {
        output.aborts.push_back({ static_cast<sv::PropertyAbortOutcome>(input_abort.outcome),
            input_abort.synchronous,
            source_tokens(input_abort.condition_tokens),
            source_tokens(input_abort.property_tokens),
            source(input_abort.span) });
    }
    return output;
}

[[nodiscard]] sv::ConcurrentAssertion SystemVerilogHirBuilder::concurrent_assertion(
    const frontend::SystemVerilogConcurrentAssertion& input,
    const std::size_t index,
    const semantic::OriginId parent)
{
    sv::ConcurrentAssertion output;
    output.kind = concurrent_assertion_kind(input.kind);
    output.form = input.form
            == frontend::SystemVerilogConcurrentAssertionForm::Sequence
        ? sv::ConcurrentAssertionForm::sequence
        : sv::ConcurrentAssertionForm::property;
    output.explicit_label = !input.label.empty();
    output.name = output.explicit_label
        ? input.label
        : "$assertion$" + std::to_string(index + 1U);
    output.property_tokens = source_tokens(input.property_tokens);
    output.has_pass_action = input.has_pass_action;
    output.pass_action_tokens = source_tokens(input.pass_action_tokens);
    output.has_failure_action = input.has_failure_action;
    output.failure_action_tokens = source_tokens(input.failure_action_tokens);
    output.sampling_region = assertion_region(input.sampling_region);
    output.evaluation_region = assertion_region(input.evaluation_region);
    output.action_region = assertion_region(input.action_region);
    output.observers.callback_on_failure = input.kind
            != frontend::SystemVerilogConcurrentAssertionKind::Cover
        && input.kind
            != frontend::SystemVerilogConcurrentAssertionKind::Restrict;
    output.coverage_slot = static_cast<std::uint32_t>(index);
    output.source = source(input.span);
    if (!input.label.empty()) {
        output.label_source = source(input.label_span);
    }
    if (input.has_pass_action && !input.pass_action_tokens.empty()) {
        output.pass_action_source = source(input.pass_action_span);
    }
    if (input.has_failure_action && !input.failure_action_tokens.empty()) {
        output.failure_action_source = source(input.failure_action_span);
    }
    output.origin = origin(output.source, parent, output.name);
    return output;
}

[[nodiscard]] sv::AssertionDeclaration SystemVerilogHirBuilder::assertion_declaration(
    const frontend::SystemVerilogAssertionDeclaration& input,
    const semantic::ScopeId scope,
    const semantic::OriginId parent)
{
    sv::AssertionDeclaration output;
    output.kind = static_cast<sv::AssertionDeclarationKind>(input.kind);
    output.name = input.name;
    output.name_source = source(input.name_span);
    output.header_tokens = source_tokens(input.header_tokens);
    output.header_source = source(input.header_span);
    output.body_tokens = source_tokens(input.body_tokens);
    output.body_source = source(input.body_span);
    output.expression_tokens = source_tokens(input.expression_tokens);
    output.expression_source = source(input.expression_span);
    output.source = source(input.span);
    output.origin = origin(output.source, parent, input.name);
    for (const auto& input_formal : input.formals) {
        output.formals.push_back({ static_cast<sv::AssertionFormalKind>(input_formal.kind),
            direction(input_formal.direction),
            input_formal.local,
            source_tokens(input_formal.type_tokens),
            input_formal.name,
            source(input_formal.name_span),
            source_tokens(input_formal.default_tokens),
            source(input_formal.span) });
    }
    for (const auto& input_variable : input.local_variables) {
        output.local_variables.push_back({ source_tokens(input_variable.type_tokens),
            input_variable.name,
            source(input_variable.name_span),
            source_tokens(input_variable.declarator_tokens),
            source_tokens(input_variable.initializer_tokens),
            source(input_variable.span) });
    }
    if (input.clock) {
        output.clock = sv::AssertionClock {
            source_tokens(input.clock->event_tokens),
            source(input.clock->span)
        };
    }
    if (input.disable) {
        output.disable = sv::AssertionDisable {
            source_tokens(input.disable->condition_tokens),
            source(input.disable->span)
        };
    }
    for (const auto& input_reference : input.references) {
        output.references.push_back({ static_cast<sv::AssertionReferenceKind>(input_reference.kind),
            name(input_reference.canonical_name, input_reference.span, scope),
            input_reference.path,
            source_tokens(input_reference.tokens),
            source(input_reference.span) });
    }
    if (input.sequence_expression) {
        output.sequence_expression = sequence_expression(
            *input.sequence_expression);
    }
    if (input.property_expression) {
        output.property_expression = property_expression(
            *input.property_expression);
    }
    for (const auto& input_endpoint : input.sequence_endpoints) {
        const auto kind = static_cast<sv::SequenceEndpointKind>(
            input_endpoint.kind);
        output.sequence_endpoints.push_back({ kind,
            name(input_endpoint.receiver_name, input_endpoint.span, scope),
            source_tokens(input_endpoint.receiver_tokens),
            input_endpoint.method_parentheses,
            source(input_endpoint.span) });
    }
    for (const auto& input_declaration : input.checker_declarations) {
        output.checker_declarations.push_back(assertion_declaration(
            input_declaration, scope, output.origin));
    }
    for (std::size_t index = 0;
        index < input.checker_assertions.size(); ++index) {
        output.checker_assertions.push_back(concurrent_assertion(
            input.checker_assertions[index], index, output.origin));
    }
    return output;
}

} // namespace fsim::app::application_detail::systemverilog_hir_detail
