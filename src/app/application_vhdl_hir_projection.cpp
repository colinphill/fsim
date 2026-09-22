// SPDX-License-Identifier: Apache-2.0
#include "application_vhdl_hir_internal.hpp"


#include <charconv>

namespace fsim::app::application_detail {

std::optional<std::int64_t> VhdlHirBuilder::static_integer(
    const frontend::Expression& expression)
{
    if (expression.kind == frontend::ExpressionKind::Unary
        && expression.operands.size() == 1U
        && (expression.text == "+" || expression.text == "-")) {
        const auto operand = static_integer(expression.operands.front());
        if (!operand) {
            return std::nullopt;
        }
        return expression.text == "-" ? -*operand : *operand;
    }
    if (expression.kind != frontend::ExpressionKind::IntegerLiteral) {
        return std::nullopt;
    }
    std::string spelling;
    spelling.reserve(expression.text.size());
    std::ranges::copy_if(expression.text, std::back_inserter(spelling),
        [](const char character) { return character != '_'; });
    std::int64_t value { };
    const auto [end, error] = std::from_chars(
        spelling.data(), spelling.data() + spelling.size(), value);
    if (error != std::errc { } || end != spelling.data() + spelling.size()) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] semantic::SourceSpanId VhdlHirBuilder::source(
    const frontend::SourceSpan& span)
{
    return intern_semantic_span(model_, span);
}

[[nodiscard]] semantic::OriginId VhdlHirBuilder::origin(
    const semantic::SourceSpanId span,
    const semantic::OriginId parent,
    const std::string_view detail)
{
    return model_.add_origin(
        semantic::OriginKind::parsed, span, parent, std::string { detail });
}

[[nodiscard]] semantic::UnitId VhdlHirBuilder::scope_unit(
    const semantic::ScopeId scope) const
{
    return model_.scopes()[scope.value()].unit;
}

[[nodiscard]] vh::Name VhdlHirBuilder::name(
    const std::string_view spelling,
    const frontend::SourceSpan& span,
    const semantic::ScopeId scope)
{
    vh::Name result {
        std::string { spelling }, canonical_vhdl_name(std::string { spelling }),
        source(span), std::nullopt, { }
    };
    for (const auto& declaration : hir_.declarations()) {
        if (declaration.scope == scope
            && canonical_vhdl_name(declaration.name) == result.canonical) {
            result.overloads.push_back(declaration.id);
        }
    }
    if (result.overloads.size() == 1) {
        result.selected = result.overloads.front();
    }
    return result;
}

[[nodiscard]] std::optional<semantic::ExpressionId> VhdlHirBuilder::expression(
    const frontend::Expression& value,
    const semantic::ScopeId scope,
    const semantic::OriginId parent)
{
    if (value.kind == frontend::ExpressionKind::Invalid) {
        return std::nullopt;
    }
    const auto span = source(value.span);
    const auto expression_origin = origin(span, parent, "VHDL expression");
    const auto id = model_.add_expression_identity(
        scope, span, expression_origin);
    vh::Expression output;
    output.id = id;
    output.scope = scope;
    output.kind = expression_kind(value);
    output.text = value.text;
    output.source = span;
    output.origin = expression_origin;
    output.nominal_type = value.nominal_type;
    if (value.kind == frontend::ExpressionKind::Call
        && value.text == "@vhdl-external"
        && value.operands.size() == 2U
        && value.operands[1].call_result_width != 0U) {
        output.nominal_type = "@fsim-vhdl-external:"
            + std::to_string(value.operands[1].call_result_width) + ":"
            + std::to_string(static_cast<unsigned>(
                value.operands[1].call_result_domain));
    }
    output.decoded_string = value.decoded_string;
    if (value.kind == frontend::ExpressionKind::Identifier
        || value.kind == frontend::ExpressionKind::LogicLiteral
        || value.kind == frontend::ExpressionKind::Call
        || value.kind == frontend::ExpressionKind::Unary
        || value.kind == frontend::ExpressionKind::Binary) {
        constexpr auto qualified_prefix
            = std::string_view { "@vhdl-qualified:" };
        const auto spelling = value.text.starts_with(qualified_prefix)
            ? std::string_view { value.text }.substr(
                  qualified_prefix.size())
            : std::string_view { value.text };
        output.referenced_name = name(spelling, value.span, scope);
    }
    output.argument_names = value.call_argument_names;
    for (const auto& operand : value.operands) {
        if (const auto operand_id = expression(
                operand, scope, expression_origin)) {
            output.operands.push_back(*operand_id);
        }
    }
    if (value.kind == frontend::ExpressionKind::Aggregate) {
        for (std::size_t index = 0; index < output.operands.size(); ++index) {
            vh::AggregateAssociation association;
            association.value = output.operands[index];
            association.source = index < value.operands.size()
                ? source(value.operands[index].span)
                : span;
            if (index < value.aggregate_choices.size()) {
                association.choice_spelling = value.aggregate_choices[index];
            }
            if (index < value.aggregate_choice_expressions.size()) {
                for (const auto& choice :
                    value.aggregate_choice_expressions[index]) {
                    if (const auto choice_id = expression(
                            choice, scope, expression_origin)) {
                        association.choices.push_back(*choice_id);
                    }
                }
            }
            output.associations.push_back(std::move(association));
        }
    }
    hir_.mutable_expressions().push_back(std::move(output));
    return id;
}

[[nodiscard]] vh::ExpressionKind VhdlHirBuilder::expression_kind(
    const frontend::Expression& expression) const noexcept
{
    switch (expression.kind) {
    case frontend::ExpressionKind::Invalid:
        return vh::ExpressionKind::invalid;
    case frontend::ExpressionKind::Identifier:
        return vh::ExpressionKind::name;
    case frontend::ExpressionKind::IntegerLiteral:
        return expression.systemverilog_scalar_kind
                == frontend::SystemVerilogScalarKind::Real
            ? vh::ExpressionKind::real_literal
            : vh::ExpressionKind::integer_literal;
    case frontend::ExpressionKind::BooleanLiteral:
        return vh::ExpressionKind::boolean_literal;
    case frontend::ExpressionKind::LogicLiteral:
        return vh::ExpressionKind::logic_literal;
    case frontend::ExpressionKind::StringLiteral:
        return vh::ExpressionKind::string_literal;
    case frontend::ExpressionKind::Unary:
        return vh::ExpressionKind::unary;
    case frontend::ExpressionKind::Update:
        return vh::ExpressionKind::update;
    case frontend::ExpressionKind::Binary:
        return vh::ExpressionKind::binary;
    case frontend::ExpressionKind::Call:
        return vh::ExpressionKind::call;
    case frontend::ExpressionKind::Index:
        return vh::ExpressionKind::index;
    case frontend::ExpressionKind::Slice:
        return vh::ExpressionKind::slice;
    case frontend::ExpressionKind::Aggregate:
        return vh::ExpressionKind::aggregate;
    case frontend::ExpressionKind::Concatenation:
        return vh::ExpressionKind::concatenation;
    case frontend::ExpressionKind::Replication:
        return vh::ExpressionKind::replication;
    case frontend::ExpressionKind::DefaultChoice:
        return vh::ExpressionKind::default_choice;
    case frontend::ExpressionKind::Conditional:
        return vh::ExpressionKind::conditional;
    }
    return vh::ExpressionKind::invalid;
}

[[nodiscard]] vh::DelayValue VhdlHirBuilder::delay_value(
    const std::uint64_t magnitude,
    const std::uint64_t divisor,
    const std::string& unit,
    const std::optional<frontend::Expression>& input_expression,
    const frontend::SourceSpan& span,
    const semantic::ScopeId scope,
    const semantic::OriginId parent)
{
    return {
        magnitude,
        divisor,
        unit,
        input_expression
            ? expression(*input_expression, scope, parent)
            : std::nullopt,
        source(span)
    };
}

[[nodiscard]] vh::Delay VhdlHirBuilder::delay(
    const frontend::Delay& input,
    const semantic::ScopeId scope,
    const semantic::OriginId parent)
{
    vh::Delay output;
    output.primary = delay_value(
        input.magnitude,
        input.divisor,
        input.unit,
        input.expression,
        input.span,
        scope,
        parent);
    const auto alternative =
        [&](const std::optional<frontend::DelayAlternative>& value)
        -> std::optional<vh::DelayValue> {
        if (!value) {
            return std::nullopt;
        }
        return delay_value(
            value->magnitude,
            value->divisor,
            value->unit,
            value->expression,
            value->span,
            scope,
            parent);
    };
    output.minimum = alternative(input.minimum);
    output.typical = alternative(input.typical);
    output.maximum = alternative(input.maximum);
    for (const auto& additional : input.additional_values) {
        output.additional.push_back(delay(additional, scope, parent));
    }
    return output;
}

[[nodiscard]] vh::DisconnectionSpecification
VhdlHirBuilder::disconnection_specification(
    const frontend::VhdlDisconnectionSpecification& input,
    const semantic::ScopeId scope,
    const semantic::OriginId parent)
{
    const auto specification_source = source(input.span);
    const auto specification_origin = origin(
        specification_source,
        parent,
        "VHDL disconnection specification");
    vh::DisconnectionSpecification output;
    if (input.all) {
        output.selection = vh::DisconnectionSelection::all;
    } else if (input.others) {
        output.selection = vh::DisconnectionSelection::others;
    }
    for (const auto& signal : input.signals) {
        output.signals.push_back(name(signal, input.span, scope));
    }
    output.type_mark = name(input.type_mark, input.span, scope);
    output.delay = delay(
        input.delay, scope, specification_origin);
    output.source = specification_source;
    output.origin = specification_origin;
    return output;
}

void VhdlHirBuilder::add_disconnection_specifications(
    const std::vector<frontend::VhdlDisconnectionSpecification>& inputs,
    const semantic::ScopeId scope,
    const semantic::OriginId parent,
    std::vector<vh::DisconnectionSpecification>& output)
{
    for (const auto& input : inputs) {
        output.push_back(disconnection_specification(
            input, scope, parent));
    }
}

[[nodiscard]] semantic::TypeId VhdlHirBuilder::find_type(
    const semantic::ScopeId scope,
    const std::string_view name_value,
    const std::optional<semantic::SourceSpanId> exact_source)
    const noexcept
{
    const auto canonical = canonical_vhdl_name(
        std::string { name_value });
    auto visible_scope = std::optional<semantic::ScopeId> { scope };
    while (visible_scope) {
        for (const auto& type : model_.types()) {
            if (type.scope == *visible_scope
                && canonical_vhdl_name(type.name) == canonical
                && (!exact_source || type.source == *exact_source)) {
                return type.id;
            }
        }
        if (exact_source || !visible_scope->valid()
            || visible_scope->value() >= model_.scopes().size()) {
            break;
        }
        visible_scope = model_.scopes()[visible_scope->value()].parent;
    }

    const auto separator = name_value.find_last_of(".:");
    const auto simple = separator == std::string_view::npos
        ? name_value
        : name_value.substr(separator + 1U);
    const auto simple_canonical = canonical_vhdl_name(
        std::string { simple });

    const auto current_library = canonical_vhdl_name(
        current_unit_ == nullptr || current_unit_->library.empty()
            ? std::string { "work" }
            : current_unit_->library);

    // An architecture sees the declarations of its entity, and a package
    // body sees the declarations of its package specification.  Those units
    // have separate semantic scopes, so a lexical parent walk alone cannot
    // preserve the already-resolved identity of interface type formals.
    if (current_unit_ != nullptr
        && !current_unit_->primary_name.empty()
        && separator == std::string_view::npos) {
        semantic::TypeId selected;
        const auto primary_name = canonical_vhdl_name(
            current_unit_->primary_name);
        for (const auto& unit : model_.units()) {
            const auto unit_library = canonical_vhdl_name(
                unit.library.empty()
                    ? std::string { "work" }
                    : unit.library);
            const auto matching_kind
                = current_unit_->kind == vh::UnitKind::architecture
                ? unit.kind == semantic::UnitKind::vhdl_entity
                : current_unit_->kind == vh::UnitKind::package
                ? unit.kind == semantic::UnitKind::vhdl_package
                    && unit.secondary_name.empty()
                : false;
            if (!matching_kind || unit_library != current_library
                || canonical_vhdl_name(unit.name) != primary_name) {
                continue;
            }
            for (const auto& type : model_.types()) {
                if (type.scope != unit.scope
                    || canonical_vhdl_name(type.name) != canonical) {
                    continue;
                }
                if (selected.valid() && selected != type.id) {
                    return { };
                }
                selected = type.id;
            }
        }
        if (selected.valid()) {
            return selected;
        }
    }

    const auto package_type = [&](const std::string_view library_name,
                                  const std::string_view package_name) {
        semantic::TypeId selected;
        for (const auto& unit : model_.units()) {
            const auto unit_library = canonical_vhdl_name(
                unit.library.empty()
                    ? std::string { "work" }
                    : unit.library);
            if (unit.language != semantic::Language::vhdl
                || unit.kind != semantic::UnitKind::vhdl_package
                || !unit.secondary_name.empty()
                || unit_library != library_name
                || canonical_vhdl_name(unit.name) != package_name) {
                continue;
            }
            for (const auto& type : model_.types()) {
                if (type.scope != unit.scope
                    || canonical_vhdl_name(type.name)
                        != simple_canonical) {
                    continue;
                }
                if (selected.valid() && selected != type.id) {
                    return semantic::TypeId { };
                }
                selected = type.id;
            }
        }
        return selected;
    };

    // A resolved VHDL frontend type retains the declaration identity as
    // `<source>:<offset>:<name>`.  That identity is stronger than lexical
    // visibility and remains stable when a package type is copied into a use
    // clause consumer.  Resolve it directly from semantic source metadata so
    // the compiled HIR does not defer an already-resolved type name to
    // elaboration.
    if (separator != std::string_view::npos
        && name_value[separator] == ':' && separator != 0U) {
        const auto offset_separator = name_value.find_last_of(
            ':', separator - 1U);
        if (offset_separator != std::string_view::npos) {
            if (offset_separator + 1U >= separator) {
                return { };
            }
            const auto source_name = name_value.substr(
                0U, offset_separator);
            const auto offset_spelling = name_value.substr(
                offset_separator + 1U,
                separator - offset_separator - 1U);
            std::uint64_t offset { };
            const auto parsed = std::from_chars(
                offset_spelling.data(),
                offset_spelling.data() + offset_spelling.size(),
                offset);
            if (parsed.ec != std::errc { }
                || parsed.ptr
                    != offset_spelling.data() + offset_spelling.size()) {
                return { };
            }
            semantic::TypeId selected;
            for (const auto& type : model_.types()) {
                if (!type.source.valid()
                    || type.source.value()
                        >= model_.source_spans().size()
                    || canonical_vhdl_name(type.name)
                        != simple_canonical) {
                    continue;
                }
                const auto& span
                    = model_.source_spans()[type.source.value()];
                if (span.logical_name != source_name
                    || span.begin.offset != offset) {
                    continue;
                }
                if (selected.valid() && selected != type.id) {
                    return { };
                }
                selected = type.id;
            }
            // Do not reinterpret a stale source identity as a selected name.
            // Missing and ambiguous identities are both rejected here.
            return selected;
        }
    }
    if (exact_source) {
        semantic::TypeId selected;
        for (const auto& type : model_.types()) {
            if (type.source != *exact_source
                || canonical_vhdl_name(type.name)
                    != simple_canonical) {
                continue;
            }
            if (selected.valid() && selected != type.id) {
                return { };
            }
            selected = type.id;
        }
        if (selected.valid()) {
            return selected;
        }
    }

    // A frontend type may retain only its simple spelling after resolution
    // through a use clause.  The unit under construction is not yet present
    // in hir_, so resolve its converted context against the complete semantic
    // unit table.  Reject ambiguous imports rather than making the result
    // depend on HIR projection order.
    if (current_unit_ != nullptr
        && separator == std::string_view::npos) {
        semantic::TypeId selected;
        for (const auto& item : current_unit_->context) {
            if (item.kind != vh::ContextKind::use_clause) {
                continue;
            }
            for (const auto& imported : item.selected_names) {
                const auto imported_name = canonical_vhdl_name(
                    imported.canonical.empty()
                        ? imported.spelling
                        : imported.canonical);
                const auto first_dot = imported_name.find('.');
                const auto second_dot = first_dot == std::string::npos
                    ? std::string::npos
                    : imported_name.find('.', first_dot + 1U);
                if (first_dot == std::string::npos
                    || second_dot == std::string::npos) {
                    continue;
                }
                const auto member = imported_name.substr(second_dot + 1U);
                if (member != "all" && member != simple_canonical) {
                    continue;
                }
                auto library = imported_name.substr(0U, first_dot);
                if (library == "work") {
                    library = current_library;
                }
                const auto package = imported_name.substr(
                    first_dot + 1U, second_dot - first_dot - 1U);
                const auto imported_type = package_type(library, package);
                if (!imported_type.valid()) {
                    continue;
                }
                if (selected.valid() && selected != imported_type) {
                    return { };
                }
                selected = imported_type;
            }
        }
        if (selected.valid()) {
            return selected;
        }
    }

    if (separator != std::string_view::npos) {
        const auto selected_name = canonical_vhdl_name(
            std::string { name_value });
        const auto first_dot = selected_name.find('.');
        const auto last_dot = selected_name.rfind('.');
        if (last_dot != std::string::npos) {
            auto library = current_library;
            std::size_t package_begin { };
            if (first_dot != last_dot) {
                library = selected_name.substr(0U, first_dot);
                if (library == "work") {
                    library = current_library;
                }
                package_begin = first_dot + 1U;
            }
            const auto package = selected_name.substr(
                package_begin, last_dot - package_begin);
            if (const auto selected = package_type(library, package);
                selected.valid()) {
                return selected;
            }
        }
        auto qualifier = name_value.substr(0U, separator);
        if (const auto nested = qualifier.find_last_of(".:");
            nested != std::string_view::npos) {
            qualifier = qualifier.substr(nested + 1U);
        }
        const auto qualifier_canonical = canonical_vhdl_name(
            std::string { qualifier });

        // A locally instantiated generic package exports the declarations of
        // its template under the instance name.  The frontend has already
        // resolved the selected name, but the retained type spelling names
        // the instance (for example `service.exported_type`) rather than the
        // template unit.  Preserve that resolved identity in HIR instead of
        // deferring it to hierarchy validation.
        visible_scope = scope;
        while (visible_scope) {
            semantic::TypeId selected;
            for (const auto& declaration : hir_.declarations()) {
                const auto declaration_scope_valid
                    = declaration.scope.valid()
                    && declaration.scope.value()
                        < model_.scopes().size();
                const auto declaration_unit
                    = declaration_scope_valid
                    ? model_.scopes()[declaration.scope.value()].unit
                    : semantic::UnitId { };
                const auto visible_from_associated_primary
                    = current_unit_ != nullptr
                    && !current_unit_->primary_name.empty()
                    && current_unit_->scope.valid()
                    && current_unit_->scope.value()
                        < model_.scopes().size()
                    && declaration_unit.valid()
                    && std::ranges::any_of(
                        model_.units(), [&](const semantic::Unit& unit) {
                            const auto library = canonical_vhdl_name(
                                unit.library.empty()
                                    ? std::string { "work" }
                                    : unit.library);
                            return unit.id == declaration_unit
                                && unit.language
                                    == semantic::Language::vhdl
                                && library == current_library
                                && canonical_vhdl_name(unit.name)
                                    == canonical_vhdl_name(
                                        current_unit_->primary_name)
                                && ((current_unit_->kind
                                            == vh::UnitKind::architecture
                                        && unit.kind
                                            == semantic::UnitKind::
                                                vhdl_entity)
                                    || (current_unit_->kind
                                            == vh::UnitKind::package
                                        && unit.kind
                                            == semantic::UnitKind::
                                                vhdl_package
                                        && unit.secondary_name.empty()));
                        });
                if ((declaration.scope != *visible_scope
                        && !visible_from_associated_primary)
                    || (declaration.form
                            != vh::DeclarationForm::package_instance
                        && declaration.form
                            != vh::DeclarationForm::generic_package)
                    || canonical_vhdl_name(declaration.name)
                        != qualifier_canonical
                    || !declaration.package) {
                    continue;
                }
                auto template_name = declaration.package->template_name
                        .canonical.empty()
                    ? std::string_view {
                        declaration.package->template_name.spelling }
                    : std::string_view {
                        declaration.package->template_name.canonical };
                auto template_library = current_library;
                if (const auto template_separator
                        = template_name.find_last_of(".:");
                    template_separator != std::string_view::npos) {
                    template_library = canonical_vhdl_name(std::string {
                        template_name.substr(0U, template_separator) });
                    if (template_library == "work") {
                        template_library = current_library;
                    }
                    template_name.remove_prefix(template_separator + 1U);
                }
                const auto candidate = package_type(
                    template_library,
                    canonical_vhdl_name(std::string { template_name }));
                if (!candidate.valid()) {
                    continue;
                }
                if (selected.valid() && selected != candidate) {
                    return { };
                }
                selected = candidate;
            }
            if (selected.valid()) {
                return selected;
            }
            if (!visible_scope->valid()
                || visible_scope->value() >= model_.scopes().size()) {
                break;
            }
            visible_scope = model_.scopes()[visible_scope->value()].parent;
        }

        semantic::TypeId selected;
        for (const auto& unit : model_.units()) {
            if (unit.language != semantic::Language::vhdl
                || unit.kind != semantic::UnitKind::vhdl_package
                || !unit.secondary_name.empty()
                || canonical_vhdl_name(unit.name)
                != qualifier_canonical) {
                continue;
            }
            for (const auto& type : model_.types()) {
                if (type.scope != unit.scope
                    || canonical_vhdl_name(type.name)
                        != simple_canonical) {
                    continue;
                }
                if (selected.valid() && selected != type.id) {
                    return { };
                }
                selected = type.id;
            }
        }
        if (selected.valid()) {
            return selected;
        }
    }
    return { };
}

[[nodiscard]] semantic::ValueId VhdlHirBuilder::find_value(
    const semantic::ScopeId scope,
    const std::string_view name_value,
    const semantic::SourceSpanId exact_source) const noexcept
{
    const auto canonical = canonical_vhdl_name(std::string { name_value });
    for (const auto& value : model_.values()) {
        if (value.scope == scope && value.source == exact_source
            && canonical_vhdl_name(value.name) == canonical) {
            return value.id;
        }
    }
    return { };
}

[[nodiscard]] semantic::TypeReference VhdlHirBuilder::type_reference(
    const frontend::Type& type,
    const frontend::SourceSpan& fallback,
    const semantic::ScopeId scope)
{
    const auto& type_span = type.named_type.empty()
        ? fallback
        : type.named_type_span;
    const auto spelling = !type.vhdl_type_declaration.empty()
        ? type.vhdl_type_declaration
        : (!type.named_type.empty() ? type.named_type : type.spelling);
    auto target = find_type(scope, spelling);
    if (!target.valid() && !type.nominal_type.empty()) {
        target = find_type(scope, type.nominal_type);
    }
    return { target, source(type_span), spelling };
}

[[nodiscard]] vh::RangeConstraint VhdlHirBuilder::concrete_range(
    const frontend::IntegerRange& range,
    const vh::RangeKind kind,
    const semantic::SourceSpanId span) const
{
    const bool null = range.descending
        ? range.left < range.right
        : range.left > range.right;
    return {
        kind, range.left, range.right, std::nullopt, std::nullopt,
        range.descending, null, span
    };
}

[[nodiscard]] vh::RangeConstraint VhdlHirBuilder::concrete_range(
    const frontend::EnumerationRange& range,
    const semantic::SourceSpanId span) const
{
    const bool null = range.descending
        ? range.left < range.right
        : range.left > range.right;
    return {
        vh::RangeKind::enumeration,
        range.left,
        range.right,
        std::nullopt,
        std::nullopt,
        range.descending,
        null,
        span
    };
}

[[nodiscard]] vh::SubtypeIndication VhdlHirBuilder::subtype(
    const frontend::Type& type,
    const frontend::SourceSpan& fallback,
    const semantic::ScopeId scope,
    const semantic::OriginId parent)
{
    vh::SubtypeIndication result;
    result.type_mark = type_reference(type, fallback, scope);
    switch (type.domain) {
    case frontend::ValueDomain::Bit2:
        result.domain = vh::ValueDomain::bit2;
        break;
    case frontend::ValueDomain::Logic4:
        result.domain = vh::ValueDomain::logic4;
        break;
    case frontend::ValueDomain::Logic9:
        result.domain = vh::ValueDomain::logic9;
        break;
    case frontend::ValueDomain::Boolean:
        result.domain = vh::ValueDomain::boolean;
        break;
    case frontend::ValueDomain::Integer:
        result.domain = vh::ValueDomain::integer;
        break;
    case frontend::ValueDomain::String:
        result.domain = vh::ValueDomain::string;
        break;
    case frontend::ValueDomain::Unknown:
        result.domain = vh::ValueDomain::unknown;
        break;
    }
    // User-defined VHDL enumerations are represented by their ordinal in
    // executable storage.  The frontend deliberately leaves their scalar
    // domain nominal (Unknown), so make that two-state representation
    // explicit in HIR while the enumeration identity remains carried by the
    // type mark and TypeDefinition.
    if (result.domain == vh::ValueDomain::unknown
        && !type.enumeration_literals.empty()) {
        result.domain = vh::ValueDomain::bit2;
    }
    result.executable_width = type.width();
    switch (type.vhdl_predefined_subtype_attribute) {
    case frontend::VhdlPredefinedSubtypeAttribute::Index:
        result.predefined_attribute = vh::PredefinedAttribute::index;
        break;
    case frontend::VhdlPredefinedSubtypeAttribute::DesignatedSubtype:
        result.predefined_attribute = vh::PredefinedAttribute::designated_subtype;
        break;
    case frontend::VhdlPredefinedSubtypeAttribute::None:
        break;
    }
    if (type.vhdl_predefined_subtype_attribute_dimension) {
        result.predefined_attribute_dimension = expression(
            *type.vhdl_predefined_subtype_attribute_dimension,
            scope,
            parent);
    }
    if (!type.vhdl_resolution_function.empty()) {
        result.resolution_function = name(
            type.vhdl_resolution_function, fallback, scope);
    }
    result.signed_value = type.is_signed;
    result.unconstrained = type.vhdl_array
        && type.vhdl_array->unconstrained;
    result.integer_storage_width = type.vhdl_integer_storage_width;
    if (type.vhdl_unspecified) {
        const auto unspecified_class = [](
                                           const frontend::VhdlUnspecifiedTypeClass input) {
            using Frontend = frontend::VhdlUnspecifiedTypeClass;
            switch (input) {
            case Frontend::Private:
                return vh::UnspecifiedTypeClass::private_type;
            case Frontend::Scalar:
                return vh::UnspecifiedTypeClass::scalar;
            case Frontend::Discrete:
                return vh::UnspecifiedTypeClass::discrete;
            case Frontend::Integer:
                return vh::UnspecifiedTypeClass::integer;
            case Frontend::Physical:
                return vh::UnspecifiedTypeClass::physical;
            case Frontend::Floating:
                return vh::UnspecifiedTypeClass::floating;
            case Frontend::Array:
                return vh::UnspecifiedTypeClass::array;
            case Frontend::Access:
                return vh::UnspecifiedTypeClass::access;
            case Frontend::File:
                return vh::UnspecifiedTypeClass::file;
            case Frontend::None:
                return vh::UnspecifiedTypeClass::none;
            }
            return vh::UnspecifiedTypeClass::none;
        };
        result.unspecified_class = unspecified_class(
            type.vhdl_unspecified->type_class);
        result.unspecified_array_index_count = type.vhdl_unspecified->array_index_count;
        result.unspecified_inference_identity = type.vhdl_unspecified->inference_identity;
        for (const auto& component :
            type.vhdl_unspecified->component_types) {
            result.unspecified_component_classes.push_back(
                component.vhdl_unspecified
                    ? unspecified_class(
                          component.vhdl_unspecified->type_class)
                    : vh::UnspecifiedTypeClass::none);
            result.unspecified_component_type_marks.push_back(
                component.spelling);
        }
    }
    if (type.integer_range) {
        result.constraints.push_back(concrete_range(
            *type.integer_range, vh::RangeKind::integer, source(fallback)));
    } else if (type.integer_range_expression) {
        result.constraints.push_back(expression_range(
            *type.integer_range_expression,
            vh::RangeKind::integer,
            scope,
            parent));
    } else if (type.enumeration_range) {
        result.constraints.push_back(
            concrete_range(*type.enumeration_range, source(fallback)));
    } else if (type.enumeration_range_expression) {
        result.constraints.push_back(expression_range(
            *type.enumeration_range_expression,
            vh::RangeKind::enumeration,
            scope,
            parent));
    } else if (type.discrete_range_expression) {
        result.constraints.push_back(expression_range(
            *type.discrete_range_expression,
            vh::RangeKind::discrete,
            scope,
            parent));
    } else if (type.vhdl_array_constraints.empty()
        && type.packed_aggregate == frontend::PackedAggregateKind::None
        && type.packed_range_expression) {
        const auto& packed = *type.packed_range_expression;
        const auto descending = packed.descending.value_or(
            type.packed_range
                ? type.packed_range->descending
                : true);
        result.constraints.push_back(expression_range(
            frontend::DiscreteRangeExpression {
                packed.left,
                packed.right,
                packed.span,
                descending,
            },
            vh::RangeKind::array_index,
            scope,
            parent));
    } else if (type.vhdl_array_constraints.empty()
        && type.packed_aggregate == frontend::PackedAggregateKind::None
        && type.packed_range) {
        result.constraints.push_back(concrete_range(
            frontend::IntegerRange {
                type.packed_range->left,
                type.packed_range->right,
                type.packed_range->descending,
            },
            vh::RangeKind::array_index,
            source(fallback)));
    }
    for (const auto& constraint : type.vhdl_array_constraints) {
        result.constraints.push_back(expression_range(
            constraint, vh::RangeKind::array_index, scope, parent));
    }
    if (!result.unconstrained && result.constraints.empty()) {
        auto mark = canonical_vhdl_name(result.type_mark.spelling);
        if (const auto separator = mark.find_last_of(".:");
            separator != std::string::npos) {
            mark.erase(0U, separator + 1U);
        }
        result.unconstrained = mark == "bit_vector"
            || mark == "std_logic_vector"
            || mark == "std_ulogic_vector"
            || mark == "signed" || mark == "unsigned"
            || mark == "string";
    }
    return result;
}

[[nodiscard]] semantic::DeclarationId VhdlHirBuilder::add_declaration_record(
    const semantic::ScopeId scope,
    const vh::DeclarationForm form,
    const semantic::DeclarationKind common_kind,
    const std::string_view declaration_name,
    const frontend::SourceSpan& frontend_span,
    const semantic::OriginId parent)
{
    const auto span = source(frontend_span);
    const auto declaration_origin = origin(span, parent, declaration_name);
    const auto id = model_.add_declaration(
        scope,
        common_kind,
        std::string { declaration_name },
        span,
        declaration_origin);
    vh::Declaration declaration_record;
    declaration_record.id = id;
    declaration_record.scope = scope;
    declaration_record.form = form;
    declaration_record.name = declaration_name;
    declaration_record.source = span;
    declaration_record.origin = declaration_origin;
    hir_.mutable_declarations().push_back(std::move(declaration_record));
    return id;
}

[[nodiscard]] vh::Declaration& VhdlHirBuilder::declaration(
    const semantic::DeclarationId id)
{
    auto& declarations = hir_.mutable_declarations();
    const auto found = std::find_if(
        declarations.begin(), declarations.end(),
        [&](const vh::Declaration& candidate) {
            return candidate.id == id;
        });
    if (found == declarations.end()) {
        throw std::logic_error { "missing VHDL HIR declaration" };
    }
    return *found;
}

[[nodiscard]] semantic::TypeId VhdlHirBuilder::ensure_type(
    const std::string_view type_name,
    const semantic::ScopeId scope,
    const semantic::TypeKind kind,
    const vh::SubtypeIndication& base,
    const semantic::SourceSpanId span,
    const semantic::OriginId declaration_origin)
{
    auto id = find_type(scope, type_name, span);
    if (!id.valid()) {
        id = model_.add_type(
            scope,
            kind,
            std::string { type_name },
            base.type_mark,
            span,
            declaration_origin);
    }
    return id;
}

[[nodiscard]] semantic::ValueId VhdlHirBuilder::ensure_value(
    const std::string_view value_name,
    const semantic::ScopeId scope,
    const semantic::ValueKind kind,
    const vh::SubtypeIndication& value_type,
    const semantic::SourceSpanId span,
    const semantic::OriginId declaration_origin)
{
    auto id = find_value(scope, value_name, span);
    if (!id.valid()) {
        id = model_.add_value(
            scope,
            kind,
            std::string { value_name },
            value_type.type_mark,
            span,
            declaration_origin);
    }
    return id;
}

[[nodiscard]] vh::TypeForm VhdlHirBuilder::type_form(
    const frontend::TypeDeclarationKind kind) const noexcept
{
    switch (kind) {
    case frontend::TypeDeclarationKind::VhdlEnumeration:
        return vh::TypeForm::enumeration;
    case frontend::TypeDeclarationKind::VhdlArray:
        return vh::TypeForm::array;
    case frontend::TypeDeclarationKind::VhdlAccess:
        return vh::TypeForm::access;
    case frontend::TypeDeclarationKind::VhdlFile:
        return vh::TypeForm::file;
    case frontend::TypeDeclarationKind::VhdlProtected:
        return vh::TypeForm::protected_type;
    case frontend::TypeDeclarationKind::VhdlProtectedBody:
        return vh::TypeForm::protected_body;
    case frontend::TypeDeclarationKind::VhdlPhysical:
        return vh::TypeForm::physical;
    case frontend::TypeDeclarationKind::VhdlRecord:
        return vh::TypeForm::record;
    case frontend::TypeDeclarationKind::VhdlSubtype:
        return vh::TypeForm::subtype;
    case frontend::TypeDeclarationKind::Alias:
        return vh::TypeForm::alias;
    default:
        return vh::TypeForm::unresolved;
    }
}

} // namespace fsim::app::application_detail
