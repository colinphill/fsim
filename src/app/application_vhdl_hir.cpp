// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

#include <functional>

namespace fsim::app::application_detail {
namespace {

    namespace vh = semantic::vhdl;

    [[nodiscard]] std::string canonical_vhdl_name(std::string value)
    {
        if (!value.empty() && value.front() != '\\' && value.front() != '\'') {
            std::transform(value.begin(), value.end(), value.begin(), [](const char c) {
                return static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
            });
        }
        return value;
    }

    [[nodiscard]] vh::UnitKind vhdl_unit_kind(
        const frontend::UnitKind kind) noexcept
    {
        switch (kind) {
        case frontend::UnitKind::VhdlEntity:
            return vh::UnitKind::entity;
        case frontend::UnitKind::VhdlArchitecture:
            return vh::UnitKind::architecture;
        case frontend::UnitKind::VhdlConfiguration:
            return vh::UnitKind::configuration;
        case frontend::UnitKind::VhdlPackage:
            return vh::UnitKind::package;
        case frontend::UnitKind::VhdlContext:
            return vh::UnitKind::context;
        case frontend::UnitKind::VhdlPslVerificationUnit:
            return vh::UnitKind::psl_verification_unit;
        default:
            return vh::UnitKind::entity;
        }
    }

    [[nodiscard]] vh::Direction vhdl_direction(
        const frontend::PortDirection direction) noexcept
    {
        switch (direction) {
        case frontend::PortDirection::Input:
            return vh::Direction::input;
        case frontend::PortDirection::Output:
            return vh::Direction::output;
        case frontend::PortDirection::Inout:
            return vh::Direction::inout;
        case frontend::PortDirection::Buffer:
            return vh::Direction::buffer;
        default:
            return vh::Direction::unknown;
        }
    }

    [[nodiscard]] vh::ObjectClass vhdl_object_class(
        const frontend::InterfaceObjectClass object_class) noexcept
    {
        switch (object_class) {
        case frontend::InterfaceObjectClass::Constant:
            return vh::ObjectClass::constant;
        case frontend::InterfaceObjectClass::Variable:
            return vh::ObjectClass::variable;
        case frontend::InterfaceObjectClass::File:
            return vh::ObjectClass::file;
        }
        return vh::ObjectClass::constant;
    }

    class VhdlHirBuilder final {
    public:
        VhdlHirBuilder(semantic::Model& model, vh::Hir& hir)
            : model_(model)
            , hir_(hir)
        {
        }

        void add_design(const frontend::ParsedDesign& parsed)
        {
            for (std::size_t index = 0; index < parsed.units.size(); ++index) {
                const auto& unit = parsed.units[index];
                if (unit.language == frontend::Language::Vhdl2008) {
                    add_unit(unit, semantic::UnitId::from_index(static_cast<std::uint32_t>(index)));
                }
            }
            link_deferred_package_constants(parsed);
            rebuild_overload_sets();
        }

    private:
        struct Pending {
            std::string_view physical_source;
            std::size_t offset { };
            std::size_t category { };
            std::size_t index { };
            std::function<semantic::DeclarationId()> build;
        };

        [[nodiscard]] semantic::SourceSpanId source(
            const frontend::SourceSpan& span)
        {
            return intern_semantic_span(model_, span);
        }

        [[nodiscard]] semantic::OriginId origin(
            const semantic::SourceSpanId span,
            const semantic::OriginId parent,
            const std::string_view detail)
        {
            return model_.add_origin(
                semantic::OriginKind::parsed, span, parent, std::string { detail });
        }

        [[nodiscard]] semantic::UnitId scope_unit(
            const semantic::ScopeId scope) const
        {
            return model_.scopes()[scope.value()].unit;
        }

        [[nodiscard]] vh::Name name(
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

        [[nodiscard]] std::optional<semantic::ExpressionId> expression(
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
            output.kind = expression_kind(value.kind);
            output.text = value.text;
            output.source = span;
            output.origin = expression_origin;
            output.nominal_type = value.nominal_type;
            output.decoded_string = value.decoded_string;
            if (value.kind == frontend::ExpressionKind::Identifier
                || value.kind == frontend::ExpressionKind::Call) {
                output.referenced_name = name(value.text, value.span, scope);
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

        [[nodiscard]] vh::ExpressionKind expression_kind(
            const frontend::ExpressionKind kind) const noexcept
        {
            switch (kind) {
            case frontend::ExpressionKind::Invalid:
                return vh::ExpressionKind::invalid;
            case frontend::ExpressionKind::Identifier:
                return vh::ExpressionKind::name;
            case frontend::ExpressionKind::IntegerLiteral:
                return vh::ExpressionKind::integer_literal;
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
            }
            return vh::ExpressionKind::invalid;
        }

        [[nodiscard]] semantic::TypeId find_type(
            const semantic::ScopeId scope,
            const std::string_view name_value,
            const std::optional<semantic::SourceSpanId> exact_source = std::nullopt)
            const noexcept
        {
            const auto canonical = canonical_vhdl_name(std::string { name_value });
            for (const auto& type : model_.types()) {
                if (type.scope == scope
                    && canonical_vhdl_name(type.name) == canonical
                    && (!exact_source || type.source == *exact_source)) {
                    return type.id;
                }
            }
            return { };
        }

        [[nodiscard]] semantic::ValueId find_value(
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

        [[nodiscard]] semantic::TypeReference type_reference(
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

        [[nodiscard]] vh::RangeConstraint concrete_range(
            const frontend::IntegerRange& range,
            const vh::RangeKind kind,
            const semantic::SourceSpanId span) const
        {
            return {
                kind, range.left, range.right, std::nullopt, std::nullopt,
                range.descending, false, span
            };
        }

        [[nodiscard]] vh::RangeConstraint concrete_range(
            const frontend::EnumerationRange& range,
            const semantic::SourceSpanId span) const
        {
            return {
                vh::RangeKind::enumeration,
                range.left,
                range.right,
                std::nullopt,
                std::nullopt,
                range.descending,
                false,
                span
            };
        }

        template <typename RangeExpression>
        [[nodiscard]] vh::RangeConstraint expression_range(
            const RangeExpression& range,
            const vh::RangeKind kind,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            return {
                kind,
                std::nullopt,
                std::nullopt,
                expression(range.left, scope, parent),
                expression(range.right, scope, parent),
                range.descending,
                false,
                source(range.span)
            };
        }

        [[nodiscard]] vh::SubtypeIndication subtype(
            const frontend::Type& type,
            const frontend::SourceSpan& fallback,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            vh::SubtypeIndication result;
            result.type_mark = type_reference(type, fallback, scope);
            if (!type.vhdl_resolution_function.empty()) {
                result.resolution_function = name(
                    type.vhdl_resolution_function, fallback, scope);
            }
            result.signed_value = type.is_signed;
            result.unconstrained = type.vhdl_array
                && type.vhdl_array->unconstrained;
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
            }
            for (const auto& constraint : type.vhdl_array_constraints) {
                result.constraints.push_back(expression_range(
                    constraint, vh::RangeKind::array_index, scope, parent));
            }
            return result;
        }

        [[nodiscard]] semantic::DeclarationId add_declaration_record(
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

        [[nodiscard]] vh::Declaration& declaration(
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

        [[nodiscard]] semantic::TypeId ensure_type(
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

        [[nodiscard]] semantic::ValueId ensure_value(
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

        [[nodiscard]] vh::TypeForm type_form(
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

        [[nodiscard]] semantic::DeclarationId add_type_declaration(
            const frontend::TypeAliasDeclaration& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            const auto subtype_form = input.declaration_kind == frontend::TypeDeclarationKind::VhdlSubtype;
            const auto id = add_declaration_record(
                scope,
                subtype_form ? vh::DeclarationForm::subtype
                             : vh::DeclarationForm::type,
                subtype_form ? semantic::DeclarationKind::subtype
                             : semantic::DeclarationKind::type,
                input.name,
                input.span,
                parent);
            auto& output = declaration(id);
            output.subtype = subtype(
                input.type, input.span, scope, output.origin);
            const auto type_id = ensure_type(
                input.name,
                scope,
                subtype_form ? semantic::TypeKind::subtype
                             : semantic::TypeKind::declaration,
                *output.subtype,
                output.source,
                output.origin);
            output.declared_type = type_id;

            vh::TypeDefinition definition;
            definition.id = type_id;
            definition.declaration = id;
            definition.form = type_form(input.declaration_kind);
            definition.name = input.name;
            definition.base = *output.subtype;
            definition.source = output.source;
            definition.origin = output.origin;
            const auto declaration_origin = output.origin;
            add_type_payload(input, definition, scope, declaration_origin);
            add_protected_payload(
                input.type, definition, id, scope, declaration_origin);
            add_predefined_attributes(input.type, definition);
            hir_.mutable_types().push_back(std::move(definition));
            return id;
        }

        void add_type_payload(
            const frontend::TypeAliasDeclaration& input,
            vh::TypeDefinition& output,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            for (std::size_t index = 0; index < input.enum_literals.size(); ++index) {
                const auto& literal = input.enum_literals[index];
                const auto literal_id = add_declaration_record(
                    scope,
                    vh::DeclarationForm::enumeration_literal,
                    semantic::DeclarationKind::enumeration_literal,
                    literal.name,
                    literal.span,
                    parent);
                auto& literal_declaration = declaration(literal_id);
                literal_declaration.subtype = output.base;
                literal_declaration.declared_value = ensure_value(
                    literal.name,
                    scope,
                    semantic::ValueKind::enumeration_literal,
                    output.base,
                    literal_declaration.source,
                    literal_declaration.origin);
                output.enumeration_literals.push_back({ literal_id,
                    literal.name,
                    static_cast<std::uint32_t>(index),
                    literal_declaration.source });
            }
            if (input.type.integer_range) {
                output.scalar_range = concrete_range(
                    *input.type.integer_range,
                    vh::RangeKind::integer,
                    output.source);
            } else if (input.type.integer_range_expression) {
                output.scalar_range = expression_range(
                    *input.type.integer_range_expression,
                    vh::RangeKind::integer,
                    scope,
                    parent);
            } else if (input.type.enumeration_range) {
                output.scalar_range = concrete_range(
                    *input.type.enumeration_range, output.source);
            } else if (input.type.enumeration_range_expression) {
                output.scalar_range = expression_range(
                    *input.type.enumeration_range_expression,
                    vh::RangeKind::enumeration,
                    scope,
                    parent);
            }
            add_array_payload(input.type, output, scope, parent);
            add_record_payload(input.type, output, scope, parent);
            add_indirect_type_payload(input.type, output, scope, parent);
        }

        void add_predefined_attributes(
            const frontend::Type& input,
            vh::TypeDefinition& output) const
        {
            if (input.vhdl_array || !input.vhdl_array_constraints.empty()) {
                output.attributes = {
                    vh::PredefinedAttribute::left,
                    vh::PredefinedAttribute::right,
                    vh::PredefinedAttribute::high,
                    vh::PredefinedAttribute::low,
                    vh::PredefinedAttribute::range,
                    vh::PredefinedAttribute::length
                };
                if (current_vhdl_standard_
                    >= frontend::VhdlStandard::Vhdl1993) {
                    output.attributes.push_back(
                        vh::PredefinedAttribute::reverse_range);
                    output.attributes.push_back(
                        vh::PredefinedAttribute::ascending);
                }
            }
            if (!input.enumeration_literals.empty()) {
                output.attributes.insert(
                    output.attributes.end(),
                    { vh::PredefinedAttribute::left,
                        vh::PredefinedAttribute::right,
                        vh::PredefinedAttribute::high,
                        vh::PredefinedAttribute::low,
                        vh::PredefinedAttribute::pos,
                        vh::PredefinedAttribute::val,
                        vh::PredefinedAttribute::succ,
                        vh::PredefinedAttribute::pred,
                        vh::PredefinedAttribute::leftof,
                        vh::PredefinedAttribute::rightof });
                if (current_vhdl_standard_
                    >= frontend::VhdlStandard::Vhdl1993) {
                    output.attributes.push_back(
                        vh::PredefinedAttribute::ascending);
                    output.attributes.push_back(
                        vh::PredefinedAttribute::image);
                    output.attributes.push_back(
                        vh::PredefinedAttribute::value);
                }
            }
        }

        void add_array_payload(
            const frontend::Type& input,
            vh::TypeDefinition& output,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            if (!input.vhdl_array) {
                return;
            }
            const auto& array = *input.vhdl_array;
            if (array.dimensions.empty() && !array.index_subtype.empty()) {
                vh::ArrayDimension converted;
                converted.index_subtype = name(
                    array.index_subtype, array.index_span, scope);
                converted.unconstrained = array.unconstrained;
                converted.source = source(array.index_span);
                if (array.index_base_range) {
                    converted.constraint = concrete_range(
                        *array.index_base_range,
                        vh::RangeKind::array_index,
                        converted.source);
                }
                output.array_dimensions.push_back(std::move(converted));
            }
            for (const auto& dimension : array.dimensions) {
                vh::ArrayDimension converted;
                converted.index_subtype = name(
                    dimension.index_subtype, dimension.index_span, scope);
                converted.unconstrained = dimension.unconstrained;
                converted.source = source(dimension.index_span);
                if (dimension.range) {
                    converted.constraint = concrete_range(
                        *dimension.range,
                        vh::RangeKind::array_index,
                        converted.source);
                    converted.constraint->null = dimension.null;
                } else if (dimension.constraint) {
                    converted.constraint = expression_range(
                        *dimension.constraint,
                        vh::RangeKind::array_index,
                        scope,
                        parent);
                }
                output.array_dimensions.push_back(std::move(converted));
            }
            if (!array.element_types.empty()) {
                output.element_subtype = subtype(
                    array.element_types.front(), array.element_span, scope, parent);
            }
        }

        void add_record_payload(
            const frontend::Type& input,
            vh::TypeDefinition& output,
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
                output.record_elements.push_back({ member.name,
                    subtype(member_type, member.span, scope, parent),
                    source(member.span) });
            }
        }

        void add_indirect_type_payload(
            const frontend::Type& input,
            vh::TypeDefinition& output,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            if (input.vhdl_access && !input.vhdl_access->designated_types.empty()) {
                output.designated_subtype = subtype(
                    input.vhdl_access->designated_types.front(),
                    input.vhdl_access->designated_span,
                    scope,
                    parent);
            }
            if (input.vhdl_file && !input.vhdl_file->element_types.empty()) {
                output.element_subtype = subtype(
                    input.vhdl_file->element_types.front(),
                    input.vhdl_file->element_span,
                    scope,
                    parent);
            }
            if (input.vhdl_physical) {
                if (input.vhdl_physical->resolved_range) {
                    output.scalar_range = concrete_range(
                        *input.vhdl_physical->resolved_range,
                        vh::RangeKind::integer,
                        output.source);
                } else if (input.vhdl_physical->range) {
                    output.scalar_range = expression_range(
                        *input.vhdl_physical->range,
                        vh::RangeKind::integer,
                        scope,
                        parent);
                }
                for (const auto& unit : input.vhdl_physical->units) {
                    const auto unit_id = add_declaration_record(
                        scope,
                        vh::DeclarationForm::constant,
                        semantic::DeclarationKind::constant,
                        unit.name,
                        unit.span,
                        parent);
                    const auto& unit_declaration = declaration(unit_id);
                    output.physical_units.push_back({ unit_id,
                        unit.name,
                        unit.scale ? expression(*unit.scale, scope, parent) : std::nullopt,
                        unit.scale_factor,
                        unit_declaration.source });
                }
            }
        }

        void add_protected_payload(
            const frontend::Type& input,
            vh::TypeDefinition& output,
            const semantic::DeclarationId owner,
            const semantic::ScopeId parent_scope,
            const semantic::OriginId parent_origin)
        {
            if (!input.vhdl_protected) {
                return;
            }
            const auto member_scope = nested_scope(
                parent_scope, output.name, output.source, parent_origin);
            declaration(owner).nested_scope = member_scope;
            add_children(
                input.vhdl_protected->variables,
                member_scope,
                parent_origin,
                output.protected_members,
                &VhdlHirBuilder::add_variable);
            add_children(
                input.vhdl_protected->functions,
                member_scope,
                parent_origin,
                output.protected_members,
                &VhdlHirBuilder::add_function);
            add_children(
                input.vhdl_protected->procedures,
                member_scope,
                parent_origin,
                output.protected_members,
                &VhdlHirBuilder::add_procedure);
            declaration(owner).children = output.protected_members;
        }

        template <typename Input>
        [[nodiscard]] semantic::DeclarationId add_object(
            const Input& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent,
            const vh::DeclarationForm form,
            const semantic::DeclarationKind declaration_kind,
            const semantic::ValueKind value_kind,
            const vh::ObjectClass object_class,
            const vh::Direction direction,
            const std::optional<frontend::Expression>& initializer)
        {
            const auto id = add_declaration_record(
                scope, form, declaration_kind, input.name, input.span, parent);
            auto& output = declaration(id);
            output.subtype = subtype(input.type, input.span, scope, output.origin);
            output.object_class = object_class;
            output.direction = direction;
            output.declared_value = ensure_value(
                input.name,
                scope,
                value_kind,
                *output.subtype,
                output.source,
                output.origin);
            if (initializer) {
                output.initializer = expression(*initializer, scope, output.origin);
            }
            return id;
        }

        [[nodiscard]] semantic::DeclarationId add_parameter(
            const frontend::ParameterDeclaration& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            vh::DeclarationForm form = vh::DeclarationForm::generic_constant;
            semantic::DeclarationKind kind = semantic::DeclarationKind::generic;
            switch (input.kind) {
            case frontend::ParameterKind::Type:
                form = vh::DeclarationForm::generic_type;
                kind = semantic::DeclarationKind::type;
                break;
            case frontend::ParameterKind::Function:
                form = vh::DeclarationForm::generic_function;
                kind = semantic::DeclarationKind::function;
                break;
            case frontend::ParameterKind::Procedure:
                form = vh::DeclarationForm::generic_procedure;
                kind = semantic::DeclarationKind::procedure;
                break;
            case frontend::ParameterKind::Package:
                form = vh::DeclarationForm::generic_package;
                kind = semantic::DeclarationKind::package_instance;
                break;
            case frontend::ParameterKind::Value:
                if (input.local) {
                    form = vh::DeclarationForm::constant;
                    kind = semantic::DeclarationKind::constant;
                }
                break;
            }
            const auto id = add_declaration_record(
                scope, form, kind, input.name, input.span, parent);
            auto& output = declaration(id);
            output.local = input.local;
            output.deferred = input.vhdl_deferred;
            output.object_class = vhdl_object_class(input.object_class);
            output.direction = vhdl_direction(input.direction);
            output.subtype = subtype(input.type, input.span, scope, output.origin);
            if (input.default_type) {
                output.default_type = subtype(
                    *input.default_type, input.span, scope, output.origin);
            }
            if (input.kind == frontend::ParameterKind::Type) {
                output.declared_type = ensure_type(
                    input.name,
                    scope,
                    semantic::TypeKind::declaration,
                    *output.subtype,
                    output.source,
                    output.origin);
            } else if (input.kind == frontend::ParameterKind::Value) {
                output.declared_value = ensure_value(
                    input.name,
                    scope,
                    input.local ? semantic::ValueKind::constant
                                : semantic::ValueKind::parameter,
                    *output.subtype,
                    output.source,
                    output.origin);
                if (input.default_value.valid()) {
                    output.initializer = expression(
                        input.default_value, scope, output.origin);
                }
            }
            if (input.function_profile) {
                const auto parameter_origin = output.origin;
                const auto parameter_source = output.source;
                const auto profile_scope = nested_scope(
                    scope, input.name, parameter_source, parameter_origin);
                vh::CallableProfile profile;
                profile.function = true;
                profile.pure = input.function_profile->pure;
                profile.defined = false;
                profile.return_type = subtype(
                    input.function_profile->return_type,
                    input.function_profile->span,
                    profile_scope,
                    parameter_origin);
                for (const auto& argument : input.function_profile->arguments) {
                    profile.formals.push_back(add_function_argument(
                        argument, profile_scope, parameter_origin));
                }
                auto& updated = declaration(id);
                updated.nested_scope = profile_scope;
                updated.children = profile.formals;
                updated.callable = std::move(profile);
            } else if (input.procedure_profile) {
                const auto parameter_origin = output.origin;
                const auto parameter_source = output.source;
                const auto profile_scope = nested_scope(
                    scope, input.name, parameter_source, parameter_origin);
                vh::CallableProfile profile;
                profile.function = false;
                profile.defined = false;
                for (const auto& argument : input.procedure_profile->arguments) {
                    profile.formals.push_back(add_procedure_argument(
                        argument, profile_scope, parameter_origin));
                }
                auto& updated = declaration(id);
                updated.nested_scope = profile_scope;
                updated.children = profile.formals;
                updated.callable = std::move(profile);
            } else if (input.package_profile) {
                vh::PackageProfile profile;
                profile.template_name = name(
                    input.package_profile->template_name,
                    input.package_profile->span,
                    scope);
                profile.generic_map = associations(
                    input.package_profile->generic_map, scope, output.origin);
                profile.generic_map_box = input.package_profile->generic_map_box;
                declaration(id).package = std::move(profile);
            }
            return id;
        }

        [[nodiscard]] semantic::DeclarationId add_signal(
            const frontend::SignalDeclaration& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            return add_object(
                input,
                scope,
                parent,
                input.is_port ? vh::DeclarationForm::port
                              : vh::DeclarationForm::signal,
                input.is_port ? semantic::DeclarationKind::port
                              : semantic::DeclarationKind::signal,
                input.is_port ? semantic::ValueKind::port
                              : semantic::ValueKind::signal,
                vh::ObjectClass::signal,
                vhdl_direction(input.direction),
                input.default_value);
        }

        [[nodiscard]] semantic::DeclarationId add_variable(
            const frontend::VariableDeclaration& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            const auto form = input.vhdl_file
                ? vh::DeclarationForm::file
                : vh::DeclarationForm::variable;
            const auto id = add_object(
                input,
                scope,
                parent,
                form,
                input.vhdl_file ? semantic::DeclarationKind::file
                                : semantic::DeclarationKind::variable,
                semantic::ValueKind::variable,
                input.vhdl_file ? vh::ObjectClass::file
                                : vh::ObjectClass::variable,
                vh::Direction::unknown,
                input.initializer);
            declaration(id).shared = input.vhdl_shared;
            return id;
        }

        [[nodiscard]] semantic::DeclarationId add_alias(
            const frontend::SignalAliasDeclaration& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            const auto id = add_declaration_record(
                scope,
                vh::DeclarationForm::alias,
                semantic::DeclarationKind::alias,
                input.name,
                input.span,
                parent);
            auto& output = declaration(id);
            output.subtype = subtype(input.type, input.span, scope, output.origin);
            output.direction = vhdl_direction(input.direction);
            return id;
        }

        [[nodiscard]] std::vector<vh::Association> associations(
            const std::vector<frontend::ParameterOverride>& inputs,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            std::vector<vh::Association> result;
            result.reserve(inputs.size());
            for (const auto& input : inputs) {
                vh::Association output;
                output.source = source(input.span);
                if (input.name) {
                    output.formal = name(*input.name, input.span, scope);
                }
                if (input.default_box) {
                    output.kind = vh::AssociationKind::default_box;
                } else if (input.type_value) {
                    output.kind = vh::AssociationKind::type;
                    output.type = subtype(
                        *input.type_value, input.span, scope, parent);
                } else {
                    output.kind = vh::AssociationKind::expression;
                    output.expression = expression(input.value, scope, parent);
                }
                result.push_back(std::move(output));
            }
            return result;
        }

        [[nodiscard]] std::vector<vh::Association> associations(
            const std::vector<frontend::PortConnection>& inputs,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            std::vector<vh::Association> result;
            result.reserve(inputs.size());
            for (const auto& input : inputs) {
                vh::Association output;
                output.source = source(input.span);
                if (input.port) {
                    output.formal = name(*input.port, input.span, scope);
                }
                switch (input.kind) {
                case frontend::PortActualKind::Open:
                    output.kind = vh::AssociationKind::open;
                    break;
                case frontend::PortActualKind::Default:
                    output.kind = vh::AssociationKind::default_box;
                    break;
                case frontend::PortActualKind::Expression:
                    output.kind = vh::AssociationKind::expression;
                    output.expression = expression(input.value, scope, parent);
                    break;
                }
                result.push_back(std::move(output));
            }
            return result;
        }

        [[nodiscard]] semantic::ScopeId nested_scope(
            const semantic::ScopeId parent_scope,
            const std::string_view scope_name,
            const semantic::SourceSpanId span,
            const semantic::OriginId scope_origin)
        {
            return model_.add_scope(
                scope_unit(parent_scope),
                parent_scope,
                std::string { scope_name },
                span,
                scope_origin);
        }

        [[nodiscard]] semantic::DeclarationId add_function_argument(
            const frontend::FunctionArgument& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            return add_object(
                input,
                scope,
                parent,
                vh::DeclarationForm::port,
                semantic::DeclarationKind::port,
                semantic::ValueKind::parameter,
                input.vhdl_file ? vh::ObjectClass::file
                                : vh::ObjectClass::constant,
                vhdl_direction(input.direction),
                input.default_value);
        }

        [[nodiscard]] semantic::DeclarationId add_procedure_argument(
            const frontend::ProcedureArgument& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            return add_object(
                input,
                scope,
                parent,
                vh::DeclarationForm::port,
                semantic::DeclarationKind::port,
                semantic::ValueKind::parameter,
                vhdl_object_class(input.object_class),
                vhdl_direction(input.direction),
                input.default_value);
        }

        template <typename Range, typename Builder>
        void add_children(
            const Range& inputs,
            const semantic::ScopeId scope,
            const semantic::OriginId parent,
            std::vector<semantic::DeclarationId>& children,
            Builder builder)
        {
            for (const auto& input : inputs) {
                children.push_back((this->*builder)(input, scope, parent));
            }
        }

        [[nodiscard]] semantic::DeclarationId add_function(
            const frontend::FunctionDeclaration& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            const auto id = add_declaration_record(
                scope,
                vh::DeclarationForm::function,
                semantic::DeclarationKind::function,
                input.name,
                input.span,
                parent);
            const auto declaration_span = declaration(id).source;
            const auto declaration_origin = declaration(id).origin;
            const auto function_scope = nested_scope(
                scope, input.name, declaration_span, declaration_origin);
            auto result_type = subtype(
                input.return_type, input.span, scope, declaration_origin);
            auto& output = declaration(id);
            output.nested_scope = function_scope;
            output.subtype = result_type;
            output.declared_value = ensure_value(
                input.name,
                scope,
                semantic::ValueKind::function,
                result_type,
                declaration_span,
                declaration_origin);
            vh::CallableProfile profile;
            profile.function = true;
            profile.pure = input.pure;
            profile.defined = input.defined;
            profile.return_type = result_type;
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
                &VhdlHirBuilder::add_parameter);
            add_children(
                input.type_aliases,
                function_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_type_declaration);
            add_children(
                input.signal_aliases,
                function_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_alias);
            add_children(
                input.package_instances,
                function_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_package_instance);
            add_children(
                input.variables,
                function_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_variable);
            add_children(
                input.functions,
                function_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_function);
            add_children(
                input.procedures,
                function_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_procedure);
            add_children(
                input.vhdl_attributes,
                function_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_attribute);
            add_children(
                input.vhdl_groups,
                function_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_group);
            declaration(id).children = std::move(children);
            return id;
        }

        [[nodiscard]] semantic::DeclarationId add_procedure(
            const frontend::ProcedureDeclaration& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            const auto id = add_declaration_record(
                scope,
                vh::DeclarationForm::procedure,
                semantic::DeclarationKind::procedure,
                input.name,
                input.span,
                parent);
            const auto declaration_span = declaration(id).source;
            const auto declaration_origin = declaration(id).origin;
            const auto procedure_scope = nested_scope(
                scope, input.name, declaration_span, declaration_origin);
            vh::SubtypeIndication no_type;
            no_type.type_mark.source = declaration_span;
            auto& output = declaration(id);
            output.nested_scope = procedure_scope;
            output.declared_value = ensure_value(
                input.name,
                scope,
                semantic::ValueKind::procedure,
                no_type,
                declaration_span,
                declaration_origin);
            vh::CallableProfile profile;
            profile.function = false;
            profile.pure = false;
            profile.defined = input.defined;
            for (const auto& argument : input.arguments) {
                profile.formals.push_back(add_procedure_argument(
                    argument, procedure_scope, declaration_origin));
            }
            declaration(id).callable = std::move(profile);
            auto children = declaration(id).callable->formals;
            add_children(
                input.constants,
                procedure_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_parameter);
            add_children(
                input.type_aliases,
                procedure_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_type_declaration);
            add_children(
                input.signal_aliases,
                procedure_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_alias);
            add_children(
                input.package_instances,
                procedure_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_package_instance);
            add_children(
                input.variables,
                procedure_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_variable);
            add_children(
                input.functions,
                procedure_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_function);
            add_children(
                input.procedures,
                procedure_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_procedure);
            add_children(
                input.vhdl_attributes,
                procedure_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_attribute);
            add_children(
                input.vhdl_groups,
                procedure_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_group);
            declaration(id).children = std::move(children);
            return id;
        }

        [[nodiscard]] semantic::DeclarationId add_component_port(
            const frontend::VhdlComponentPort& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            return add_object(
                input,
                scope,
                parent,
                vh::DeclarationForm::port,
                semantic::DeclarationKind::port,
                semantic::ValueKind::port,
                vh::ObjectClass::signal,
                vhdl_direction(input.direction),
                input.default_value);
        }

        [[nodiscard]] semantic::DeclarationId add_component(
            const frontend::VhdlComponentDeclaration& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            const auto id = add_declaration_record(
                scope,
                vh::DeclarationForm::component,
                semantic::DeclarationKind::component,
                input.name,
                input.span,
                parent);
            const auto declaration_span = declaration(id).source;
            const auto declaration_origin = declaration(id).origin;
            const auto component_scope = nested_scope(
                scope, input.name, declaration_span, declaration_origin);
            vh::ComponentProfile profile;
            profile.end_name = input.end_name;
            profile.owner_library = input.owner_library;
            profile.owner_name = input.owner_name;
            profile.scope_path = input.scope_path;
            for (const auto& generic : input.generics) {
                profile.generics.push_back(add_parameter(
                    generic, component_scope, declaration_origin));
            }
            for (const auto& port : input.ports) {
                profile.ports.push_back(add_component_port(
                    port, component_scope, declaration_origin));
            }
            auto& output = declaration(id);
            output.nested_scope = component_scope;
            output.children = profile.generics;
            output.children.insert(
                output.children.end(), profile.ports.begin(), profile.ports.end());
            output.component = std::move(profile);
            return id;
        }

        [[nodiscard]] semantic::DeclarationId add_package_instance(
            const frontend::PackageInstantiation& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            const auto id = add_declaration_record(
                scope,
                vh::DeclarationForm::package_instance,
                semantic::DeclarationKind::package_instance,
                input.name,
                input.span,
                parent);
            const auto declaration_origin = declaration(id).origin;
            vh::PackageProfile profile;
            profile.template_name = name(input.template_name, input.span, scope);
            profile.generic_map = associations(
                input.generic_map, scope, declaration_origin);
            profile.generic_map_box = input.generic_map_box;
            declaration(id).package = std::move(profile);
            return id;
        }

        [[nodiscard]] semantic::DeclarationId add_attribute(
            const frontend::VhdlAttributeDeclaration& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            const auto id = add_declaration_record(
                scope,
                input.specification
                    ? vh::DeclarationForm::attribute_specification
                    : vh::DeclarationForm::attribute_declaration,
                semantic::DeclarationKind::attribute,
                input.name,
                input.span,
                parent);
            const auto declaration_origin = declaration(id).origin;
            vh::AttributeProfile profile;
            profile.specification = input.specification;
            if (!input.specification) {
                profile.subtype = subtype(
                    input.type, input.span, scope, declaration_origin);
            } else {
                profile.entity_class = input.entity_class;
                for (const auto& entity_name : input.entity_names) {
                    profile.entity_names.push_back(name(entity_name, input.span, scope));
                }
                if (input.value.valid()) {
                    profile.value = expression(input.value, scope, declaration_origin);
                }
            }
            declaration(id).attribute = std::move(profile);
            return id;
        }

        [[nodiscard]] semantic::DeclarationId add_group(
            const frontend::VhdlGroupDeclaration& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            const auto id = add_declaration_record(
                scope,
                input.template_declaration ? vh::DeclarationForm::group_template
                                           : vh::DeclarationForm::group_instance,
                semantic::DeclarationKind::group,
                input.name,
                input.span,
                parent);
            vh::GroupProfile profile;
            profile.template_declaration = input.template_declaration;
            if (!input.template_name.empty()) {
                profile.template_name = name(input.template_name, input.span, scope);
            }
            for (const auto& entry : input.entries) {
                profile.entries.push_back(name(entry, input.span, scope));
            }
            declaration(id).group = std::move(profile);
            return id;
        }

        [[nodiscard]] semantic::DeclarationId add_generic_function_instance(
            const frontend::GenericSubprogramInstantiation& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            const auto id = add_declaration_record(
                scope,
                vh::DeclarationForm::generic_function_instance,
                semantic::DeclarationKind::function,
                input.name,
                input.span,
                parent);
            const auto declaration_origin = declaration(id).origin;
            vh::PackageProfile profile;
            profile.template_name = name(input.template_name, input.span, scope);
            profile.generic_map = associations(
                input.generic_map, scope, declaration_origin);
            profile.generic_map_box = input.generic_map_box;
            declaration(id).package = std::move(profile);
            return id;
        }

        [[nodiscard]] semantic::DeclarationId add_generic_procedure_instance(
            const frontend::GenericSubprogramInstantiation& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            const auto id = add_generic_function_instance(input, scope, parent);
            declaration(id).form = vh::DeclarationForm::generic_procedure_instance;
            return id;
        }

        [[nodiscard]] semantic::DeclarationId add_generic_function_template(
            const frontend::GenericFunctionTemplate& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            const auto id = add_declaration_record(
                scope,
                vh::DeclarationForm::generic_function_template,
                semantic::DeclarationKind::function,
                input.function.name,
                input.span,
                parent);
            const auto declaration_span = declaration(id).source;
            const auto declaration_origin = declaration(id).origin;
            const auto template_scope = nested_scope(
                scope, input.function.name, declaration_span, declaration_origin);
            std::vector<semantic::DeclarationId> children;
            add_children(
                input.generic_parameters,
                template_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_parameter);
            children.push_back(add_function(
                input.function, template_scope, declaration_origin));
            auto& output = declaration(id);
            output.nested_scope = template_scope;
            output.children = std::move(children);
            return id;
        }

        [[nodiscard]] semantic::DeclarationId add_generic_procedure_template(
            const frontend::GenericProcedureTemplate& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            const auto id = add_declaration_record(
                scope,
                vh::DeclarationForm::generic_procedure_template,
                semantic::DeclarationKind::procedure,
                input.procedure.name,
                input.span,
                parent);
            const auto declaration_span = declaration(id).source;
            const auto declaration_origin = declaration(id).origin;
            const auto template_scope = nested_scope(
                scope, input.procedure.name, declaration_span, declaration_origin);
            std::vector<semantic::DeclarationId> children;
            add_children(
                input.generic_parameters,
                template_scope,
                declaration_origin,
                children,
                &VhdlHirBuilder::add_parameter);
            children.push_back(add_procedure(
                input.procedure, template_scope, declaration_origin));
            auto& output = declaration(id);
            output.nested_scope = template_scope;
            output.children = std::move(children);
            return id;
        }

        [[nodiscard]] vh::BindingIndication binding(
            const frontend::VhdlBindingIndication& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            vh::BindingIndication output;
            switch (input.kind) {
            case frontend::VhdlBindingAspectKind::Entity:
                output.kind = vh::BindingKind::entity;
                break;
            case frontend::VhdlBindingAspectKind::Configuration:
                output.kind = vh::BindingKind::configuration;
                break;
            case frontend::VhdlBindingAspectKind::Open:
                output.kind = vh::BindingKind::open;
                break;
            }
            output.entity = name(input.entity_name, input.span, scope);
            output.architecture = input.architecture_name;
            output.configuration = name(
                input.configuration_name, input.span, scope);
            output.generic_map = associations(input.generic_map, scope, parent);
            output.port_map = associations(input.port_map, scope, parent);
            output.source = source(input.span);
            return output;
        }

        [[nodiscard]] vh::ComponentConfiguration component_configuration(
            const frontend::VhdlComponentConfiguration& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            vh::ComponentConfiguration output;
            switch (input.selection) {
            case frontend::VhdlInstantiationSelectionKind::Labels:
                output.selection = vh::InstanceSelection::labels;
                break;
            case frontend::VhdlInstantiationSelectionKind::All:
                output.selection = vh::InstanceSelection::all;
                break;
            case frontend::VhdlInstantiationSelectionKind::Others:
                output.selection = vh::InstanceSelection::others;
                break;
            }
            output.labels = input.labels;
            output.component = name(input.component_name, input.span, scope);
            output.binding = binding(input.binding, scope, parent);
            output.source = source(input.span);
            return output;
        }

        [[nodiscard]] vh::BlockConfiguration block_configuration(
            const frontend::VhdlBlockConfiguration& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent)
        {
            vh::BlockConfiguration output;
            output.block = name(input.block_name, input.span, scope);
            if (input.generate_index) {
                output.generate_index = expression(
                    *input.generate_index, scope, parent);
            }
            for (const auto& component : input.component_configurations) {
                output.components.push_back(component_configuration(
                    component, scope, parent));
            }
            for (const auto& block : input.block_configurations) {
                output.blocks.push_back(block_configuration(block, scope, parent));
            }
            output.source = source(input.span);
            return output;
        }

        void queue_body_declarations(
            const frontend::GenerateBody& input,
            const semantic::ScopeId scope,
            const semantic::OriginId parent,
            std::vector<semantic::DeclarationId>& declarations)
        {
            std::vector<Pending> pending;
            const auto queue = [&]<typename Range, typename Builder>(
                                   const Range& range,
                                   const std::size_t category,
                                   Builder builder) {
                for (std::size_t index = 0; index < range.size(); ++index) {
                    const auto& item = range[index];
                    pending.push_back({ frontend::physical_source(item.span),
                        item.span.begin.offset,
                        category,
                        index,
                        [&, item_ptr = &item, builder] {
                            return (this->*builder)(*item_ptr, scope, parent);
                        } });
                }
            };
            queue(input.constants, 0, &VhdlHirBuilder::add_parameter);
            queue(input.type_aliases, 1, &VhdlHirBuilder::add_type_declaration);
            queue(input.signals, 2, &VhdlHirBuilder::add_signal);
            queue(input.signal_aliases, 3, &VhdlHirBuilder::add_alias);
            queue(input.variables, 4, &VhdlHirBuilder::add_variable);
            queue(input.functions, 5, &VhdlHirBuilder::add_function);
            queue(input.procedures, 6, &VhdlHirBuilder::add_procedure);
            queue(
                input.generic_function_templates,
                7,
                &VhdlHirBuilder::add_generic_function_template);
            queue(
                input.generic_procedure_templates,
                8,
                &VhdlHirBuilder::add_generic_procedure_template);
            queue(
                input.generic_function_instances,
                9,
                &VhdlHirBuilder::add_generic_function_instance);
            queue(
                input.generic_procedure_instances,
                10,
                &VhdlHirBuilder::add_generic_procedure_instance);
            queue(input.package_instances, 11, &VhdlHirBuilder::add_package_instance);
            queue(
                input.vhdl_component_declarations,
                12,
                &VhdlHirBuilder::add_component);
            queue(input.vhdl_attributes, 13, &VhdlHirBuilder::add_attribute);
            queue(input.vhdl_groups, 14, &VhdlHirBuilder::add_group);
            std::stable_sort(pending.begin(), pending.end(), [](const auto& left, const auto& right) {
                return std::tuple {
                    left.physical_source, left.offset, left.category, left.index
                }
                < std::tuple {
                      right.physical_source,
                      right.offset,
                      right.category,
                      right.index
                  };
            });
            for (auto& item : pending) {
                declarations.push_back(item.build());
            }
        }

        void add_generate_body(
            const frontend::GenerateBody& input,
            vh::GenerateRegion& output)
        {
            queue_body_declarations(
                input, output.scope, output.origin, output.declarations);
            for (const auto& instance : input.instances) {
                const auto instance_source = source(instance.span);
                const auto instance_origin = origin(
                    instance_source, output.origin, instance.name);
                output.instances.push_back(model_.add_instance(
                    output.scope,
                    instance.name,
                    instance.unit_name,
                    instance_source,
                    instance_origin));
            }
            for (const auto& process : input.processes) {
                output.processes.push_back(add_process_skeleton(
                    process, output.scope, output.origin));
            }
            for (const auto& nested : input.generate_regions) {
                output.nested.push_back(add_generate(
                    nested, output.scope, output.origin));
            }
        }

        [[nodiscard]] semantic::ProcessId add_process_skeleton(
            const frontend::Process& input,
            const semantic::ScopeId parent_scope,
            const semantic::OriginId parent_origin)
        {
            const auto process_source = source(input.span);
            const auto process_name = input.name.empty()
                ? "<process>"
                : input.name;
            const auto process_origin = origin(
                process_source, parent_origin, process_name);
            const auto process_scope = nested_scope(
                parent_scope, process_name, process_source, process_origin);
            const auto id = model_.add_process_identity(
                process_scope,
                process_name,
                process_source,
                process_origin);
            vh::Process output;
            output.id = id;
            output.scope = process_scope;
            output.name = process_name;
            output.postponed = input.vhdl_postponed;
            output.source = process_source;
            output.origin = process_origin;
            add_children(
                input.constants,
                process_scope,
                process_origin,
                output.declarations,
                &VhdlHirBuilder::add_parameter);
            add_children(
                input.type_aliases,
                process_scope,
                process_origin,
                output.declarations,
                &VhdlHirBuilder::add_type_declaration);
            add_children(
                input.signal_aliases,
                process_scope,
                process_origin,
                output.declarations,
                &VhdlHirBuilder::add_alias);
            add_children(
                input.package_instances,
                process_scope,
                process_origin,
                output.declarations,
                &VhdlHirBuilder::add_package_instance);
            add_children(
                input.functions,
                process_scope,
                process_origin,
                output.declarations,
                &VhdlHirBuilder::add_function);
            add_children(
                input.procedures,
                process_scope,
                process_origin,
                output.declarations,
                &VhdlHirBuilder::add_procedure);
            add_children(
                input.variables,
                process_scope,
                process_origin,
                output.declarations,
                &VhdlHirBuilder::add_variable);
            add_children(
                input.vhdl_attributes,
                process_scope,
                process_origin,
                output.declarations,
                &VhdlHirBuilder::add_attribute);
            add_children(
                input.vhdl_groups,
                process_scope,
                process_origin,
                output.declarations,
                &VhdlHirBuilder::add_group);
            hir_.mutable_processes().push_back(std::move(output));
            return id;
        }

        [[nodiscard]] vh::GenerateRegion add_generate(
            const frontend::GenerateRegion& input,
            const semantic::ScopeId parent_scope,
            const semantic::OriginId parent_origin)
        {
            const auto label = !input.then_scope.empty()
                ? input.then_scope
                : (!input.variable.empty() ? input.variable : "<generate>");
            const auto declaration_id = add_declaration_record(
                parent_scope,
                vh::DeclarationForm::generated,
                semantic::DeclarationKind::generate,
                label,
                input.span,
                parent_origin);
            const auto declaration_source = declaration(declaration_id).source;
            const auto declaration_origin = declaration(declaration_id).origin;
            vh::GenerateRegion output;
            output.declaration = declaration_id;
            output.scope = nested_scope(
                parent_scope, label, declaration_source, declaration_origin);
            switch (input.kind) {
            case frontend::GenerateKind::StaticBlock:
                output.kind = vh::GenerateKind::block;
                break;
            case frontend::GenerateKind::Conditional:
                output.kind = vh::GenerateKind::conditional;
                break;
            case frontend::GenerateKind::Iterative:
                output.kind = vh::GenerateKind::iterative;
                break;
            case frontend::GenerateKind::Selection:
                output.kind = vh::GenerateKind::selection;
                break;
            }
            output.label = input.then_scope;
            output.alternative_label = input.else_scope;
            output.iterator = input.variable;
            output.initial = expression(
                input.initial, output.scope, declaration_origin);
            output.condition = expression(
                input.condition, output.scope, declaration_origin);
            output.iteration = expression(
                input.iteration, output.scope, declaration_origin);
            output.generic_map = associations(
                input.block_generic_map, output.scope, declaration_origin);
            output.port_map = associations(
                input.block_port_map, output.scope, declaration_origin);
            output.source = declaration_source;
            output.origin = declaration_origin;
            declaration(declaration_id).nested_scope = output.scope;
            for (const auto& generic : input.block_generics) {
                output.declarations.push_back(add_parameter(
                    generic, output.scope, declaration_origin));
            }
            for (const auto& port : input.block_ports) {
                output.declarations.push_back(add_signal(
                    port, output.scope, declaration_origin));
            }
            add_generate_body(input.then_body, output);

            if (!input.else_scope.empty()
                || !input.else_body.constants.empty()
                || !input.else_body.signals.empty()
                || !input.else_body.generate_regions.empty()) {
                frontend::GenerateRegion synthetic;
                synthetic.kind = frontend::GenerateKind::StaticBlock;
                synthetic.then_scope = input.else_scope.empty()
                    ? label + ".else"
                    : input.else_scope;
                synthetic.then_body = input.else_body;
                synthetic.span = input.span;
                output.nested.push_back(add_generate(
                    synthetic, output.scope, declaration_origin));
            }
            for (const auto& alternative : input.alternatives) {
                frontend::GenerateRegion synthetic;
                synthetic.kind = frontend::GenerateKind::StaticBlock;
                synthetic.then_scope = alternative.scope.empty()
                    ? label + ".alternative"
                    : alternative.scope;
                synthetic.then_body = alternative.body;
                synthetic.span = alternative.span;
                output.nested.push_back(add_generate(
                    synthetic, output.scope, declaration_origin));
            }
            declaration(declaration_id).children = output.declarations;
            return output;
        }

        [[nodiscard]] vh::PslExpressionClass psl_expression_class(
            const frontend::VhdlPslExpressionClass kind) const noexcept
        {
            switch (kind) {
            case frontend::VhdlPslExpressionClass::Invalid:
                return vh::PslExpressionClass::invalid;
            case frontend::VhdlPslExpressionClass::Boolean:
                return vh::PslExpressionClass::boolean;
            case frontend::VhdlPslExpressionClass::Sequence:
                return vh::PslExpressionClass::sequence;
            case frontend::VhdlPslExpressionClass::Property:
                return vh::PslExpressionClass::property;
            case frontend::VhdlPslExpressionClass::Endpoint:
                return vh::PslExpressionClass::endpoint;
            case frontend::VhdlPslExpressionClass::StaticInteger:
                return vh::PslExpressionClass::static_integer;
            }
            return vh::PslExpressionClass::invalid;
        }

        [[nodiscard]] vh::PslTemporalOperatorKind psl_operator_kind(
            const frontend::VhdlPslTemporalOperatorKind kind) const noexcept
        {
            switch (kind) {
            case frontend::VhdlPslTemporalOperatorKind::SequenceConcatenation:
                return vh::PslTemporalOperatorKind::sequence_concatenation;
            case frontend::VhdlPslTemporalOperatorKind::SequenceFusion:
                return vh::PslTemporalOperatorKind::sequence_fusion;
            case frontend::VhdlPslTemporalOperatorKind::ConsecutiveRepetition:
                return vh::PslTemporalOperatorKind::consecutive_repetition;
            case frontend::VhdlPslTemporalOperatorKind::NonconsecutiveRepetition:
                return vh::PslTemporalOperatorKind::nonconsecutive_repetition;
            case frontend::VhdlPslTemporalOperatorKind::GotoRepetition:
                return vh::PslTemporalOperatorKind::goto_repetition;
            case frontend::VhdlPslTemporalOperatorKind::OverlappedSuffixImplication:
                return vh::PslTemporalOperatorKind::overlapped_suffix_implication;
            case frontend::VhdlPslTemporalOperatorKind::NonoverlappedSuffixImplication:
                return vh::PslTemporalOperatorKind::nonoverlapped_suffix_implication;
            case frontend::VhdlPslTemporalOperatorKind::Next:
                return vh::PslTemporalOperatorKind::next;
            case frontend::VhdlPslTemporalOperatorKind::Previous:
                return vh::PslTemporalOperatorKind::previous;
            case frontend::VhdlPslTemporalOperatorKind::Eventually:
                return vh::PslTemporalOperatorKind::eventually;
            case frontend::VhdlPslTemporalOperatorKind::Always:
                return vh::PslTemporalOperatorKind::always;
            case frontend::VhdlPslTemporalOperatorKind::Until:
                return vh::PslTemporalOperatorKind::until;
            case frontend::VhdlPslTemporalOperatorKind::Before:
                return vh::PslTemporalOperatorKind::before;
            case frontend::VhdlPslTemporalOperatorKind::Within:
                return vh::PslTemporalOperatorKind::within;
            }
            return vh::PslTemporalOperatorKind::sequence_concatenation;
        }

        [[nodiscard]] std::vector<std::string> psl_token_texts(
            const std::vector<frontend::Token>& tokens) const
        {
            std::vector<std::string> result;
            result.reserve(tokens.size());
            for (const auto& token : tokens) {
                result.push_back(token.text);
            }
            return result;
        }

        [[nodiscard]] vh::PslClock psl_clock(
            const frontend::VhdlPslClock& input)
        {
            return vh::PslClock {
                psl_token_texts(input.expression_tokens),
                input.canonical_identity,
                input.explicit_override,
                input.unknown_is_no_edge,
                source(input.span)
            };
        }

        [[nodiscard]] vh::PslAnalyzedExpression psl_expression(
            const frontend::VhdlPslAnalyzedExpression& input)
        {
            vh::PslAnalyzedExpression output;
            output.expression_class = psl_expression_class(input.expression_class);
            output.expression_tokens = psl_token_texts(input.expression_tokens);
            output.sampled_names = input.sampled_names;
            output.source = source(input.span);
            if (input.clock) {
                output.clock = psl_clock(*input.clock);
            }
            for (const auto& reference : input.references) {
                output.references.push_back(vh::PslReference {
                    reference.name,
                    psl_expression_class(reference.expression_class),
                    source(reference.span) });
            }
            for (const auto& operation : input.temporal_operators) {
                vh::PslTemporalOperator converted;
                converted.kind = psl_operator_kind(operation.kind);
                converted.left_tokens = psl_token_texts(operation.left_tokens);
                converted.right_tokens = psl_token_texts(operation.right_tokens);
                converted.source = source(operation.span);
                if (operation.range) {
                    converted.range = vh::PslStaticRange {
                        operation.range->minimum,
                        operation.range->maximum,
                        operation.range->minimum_formal,
                        operation.range->maximum_formal,
                        operation.range->unbounded,
                        source(operation.range->span)
                    };
                }
                output.temporal_operators.push_back(std::move(converted));
            }
            return output;
        }

        void add_unit(
            const frontend::DesignUnit& input,
            const semantic::UnitId unit_id)
        {
            current_vhdl_standard_ = input.vhdl_standard;
            const auto& common_unit = model_.units()[unit_id.value()];
            vh::Unit output;
            output.id = unit_id;
            output.scope = common_unit.scope;
            output.kind = vhdl_unit_kind(input.kind);
            output.library = input.library;
            output.name = input.name;
            output.primary_name = input.primary_name;
            output.source = common_unit.source;
            output.origin = common_unit.origin;
            for (const auto& item : input.vhdl_context) {
                vh::ContextItem converted;
                switch (item.kind) {
                case frontend::VhdlContextItemKind::LibraryClause:
                    converted.kind = vh::ContextKind::library_clause;
                    break;
                case frontend::VhdlContextItemKind::UseClause:
                    converted.kind = vh::ContextKind::use_clause;
                    break;
                case frontend::VhdlContextItemKind::ContextReference:
                    converted.kind = vh::ContextKind::context_reference;
                    break;
                }
                converted.source = source(item.span);
                for (const auto& selected : item.selected_names) {
                    converted.selected_names.push_back(
                        name(selected, item.span, common_unit.scope));
                }
                output.context.push_back(std::move(converted));
            }
            if (input.vhdl_psl_verification_unit) {
                vh::PslVerificationUnit converted;
                switch (input.vhdl_psl_verification_unit->kind) {
                case frontend::VhdlPslVerificationUnitKind::Unit:
                    converted.kind = vh::PslVerificationUnitKind::unit;
                    break;
                case frontend::VhdlPslVerificationUnitKind::Property:
                    converted.kind = vh::PslVerificationUnitKind::property;
                    break;
                case frontend::VhdlPslVerificationUnitKind::Mode:
                    converted.kind = vh::PslVerificationUnitKind::mode;
                    break;
                }
                converted.comment_embedded = input.vhdl_psl_verification_unit->comment_embedded;
                for (const auto& token :
                    input.vhdl_psl_verification_unit->target_tokens) {
                    converted.target_tokens.push_back(token.text);
                }
                output.psl_verification_unit = std::move(converted);
            }
            for (const auto& declaration : input.vhdl_psl_declarations) {
                vh::PslDeclaration converted;
                switch (declaration.kind) {
                case frontend::VhdlPslDeclarationKind::DefaultClock:
                    converted.kind = vh::PslDeclarationKind::default_clock;
                    break;
                case frontend::VhdlPslDeclarationKind::Boolean:
                    converted.kind = vh::PslDeclarationKind::boolean;
                    break;
                case frontend::VhdlPslDeclarationKind::Sequence:
                    converted.kind = vh::PslDeclarationKind::sequence;
                    break;
                case frontend::VhdlPslDeclarationKind::Property:
                    converted.kind = vh::PslDeclarationKind::property;
                    break;
                case frontend::VhdlPslDeclarationKind::Endpoint:
                    converted.kind = vh::PslDeclarationKind::endpoint;
                    break;
                }
                converted.name = declaration.name;
                converted.comment_embedded = declaration.comment_embedded;
                converted.source = source(declaration.span);
                for (const auto& formal : declaration.formals) {
                    vh::PslFormal converted_formal;
                    converted_formal.name = formal.name;
                    converted_formal.expression_class = psl_expression_class(formal.expression_class);
                    converted_formal.static_default = formal.static_default;
                    converted_formal.source = source(formal.span);
                    for (const auto& token : formal.profile_tokens) {
                        converted_formal.profile_tokens.push_back(token.text);
                    }
                    converted.formals.push_back(std::move(converted_formal));
                }
                for (const auto& token : declaration.body_tokens) {
                    converted.body_tokens.push_back(token.text);
                }
                if (declaration.analyzed_expression) {
                    converted.analyzed_expression = psl_expression(*declaration.analyzed_expression);
                }
                output.psl_declarations.push_back(std::move(converted));
            }
            for (const auto& directive : input.vhdl_psl_directives) {
                vh::PslDirective converted;
                switch (directive.kind) {
                case frontend::VhdlPslDirectiveKind::Assert:
                    converted.kind = vh::PslDirectiveKind::assert_directive;
                    break;
                case frontend::VhdlPslDirectiveKind::Assume:
                    converted.kind = vh::PslDirectiveKind::assume;
                    break;
                case frontend::VhdlPslDirectiveKind::Restrict:
                    converted.kind = vh::PslDirectiveKind::restrict;
                    break;
                case frontend::VhdlPslDirectiveKind::Cover:
                    converted.kind = vh::PslDirectiveKind::cover;
                    break;
                }
                converted.label = directive.label;
                converted.comment_embedded = directive.comment_embedded;
                converted.source = source(directive.span);
                for (const auto& token : directive.property_tokens) {
                    converted.property_tokens.push_back(token.text);
                }
                if (directive.analyzed_property) {
                    converted.analyzed_property = psl_expression(*directive.analyzed_property);
                }
                output.psl_directives.push_back(std::move(converted));
            }
            add_unit_declarations(input, output);
            for (const auto& generate : input.generate_regions) {
                output.generates.push_back(add_generate(
                    generate, output.scope, output.origin));
            }
            for (const auto& process : input.processes) {
                output.processes.push_back(add_process_skeleton(
                    process, output.scope, output.origin));
            }
            for (const auto& configuration :
                input.vhdl_configuration_specifications) {
                output.component_configurations.push_back(component_configuration(
                    configuration, output.scope, output.origin));
            }
            if (input.vhdl_configuration) {
                output.configuration = block_configuration(
                    input.vhdl_configuration->block, output.scope, output.origin);
            }
            hir_.mutable_units().push_back(std::move(output));
        }

        void add_unit_declarations(
            const frontend::DesignUnit& input,
            vh::Unit& output)
        {
            std::vector<Pending> pending;
            const auto queue = [&]<typename Range, typename Builder>(
                                   const Range& range,
                                   const std::size_t category,
                                   Builder builder) {
                for (std::size_t index = 0; index < range.size(); ++index) {
                    const auto& item = range[index];
                    pending.push_back({ frontend::physical_source(item.span),
                        item.span.begin.offset,
                        category,
                        index,
                        [&, item_ptr = &item, builder] {
                            return (this->*builder)(
                                *item_ptr, output.scope, output.origin);
                        } });
                }
            };
            queue(input.parameters, 0, &VhdlHirBuilder::add_parameter);
            queue(input.ports, 1, &VhdlHirBuilder::add_signal);
            queue(input.type_aliases, 2, &VhdlHirBuilder::add_type_declaration);
            queue(input.signals, 3, &VhdlHirBuilder::add_signal);
            queue(input.signal_aliases, 4, &VhdlHirBuilder::add_alias);
            queue(input.variables, 5, &VhdlHirBuilder::add_variable);
            queue(input.functions, 6, &VhdlHirBuilder::add_function);
            queue(input.procedures, 7, &VhdlHirBuilder::add_procedure);
            queue(
                input.generic_function_templates,
                8,
                &VhdlHirBuilder::add_generic_function_template);
            queue(
                input.generic_procedure_templates,
                9,
                &VhdlHirBuilder::add_generic_procedure_template);
            queue(
                input.generic_function_instances,
                10,
                &VhdlHirBuilder::add_generic_function_instance);
            queue(
                input.generic_procedure_instances,
                11,
                &VhdlHirBuilder::add_generic_procedure_instance);
            queue(input.package_instances, 12, &VhdlHirBuilder::add_package_instance);
            queue(
                input.vhdl_component_declarations,
                13,
                &VhdlHirBuilder::add_component);
            queue(input.vhdl_attributes, 14, &VhdlHirBuilder::add_attribute);
            queue(input.vhdl_groups, 15, &VhdlHirBuilder::add_group);
            std::stable_sort(pending.begin(), pending.end(), [](const auto& left, const auto& right) {
                return std::tuple {
                    left.physical_source, left.offset, left.category, left.index
                }
                < std::tuple {
                      right.physical_source,
                      right.offset,
                      right.category,
                      right.index
                  };
            });
            for (auto& declaration_builder : pending) {
                output.declarations.push_back(declaration_builder.build());
            }
        }

        void link_deferred_package_constants(
            const frontend::ParsedDesign& parsed)
        {
            const auto find_unit = [&](const std::size_t index) -> const vh::Unit* {
                const auto id = semantic::UnitId::from_index(
                    static_cast<std::uint32_t>(index));
                const auto found = std::ranges::find_if(
                    hir_.units(), [&](const vh::Unit& unit) { return unit.id == id; });
                return found == hir_.units().end() ? nullptr : &*found;
            };
            const auto find_constant = [&](const vh::Unit& unit,
                                           const std::string_view name_value)
                -> std::optional<semantic::DeclarationId> {
                const auto found = std::ranges::find_if(
                    unit.declarations, [&](const semantic::DeclarationId id) {
                        const auto& candidate = hir_.declarations()[id.value()];
                        return candidate.form == vh::DeclarationForm::constant
                            && candidate.name == name_value;
                    });
                return found == unit.declarations.end()
                    ? std::nullopt
                    : std::optional { *found };
            };
            for (std::size_t spec_index = 0; spec_index < parsed.units.size();
                ++spec_index) {
                const auto& spec = parsed.units[spec_index];
                if (spec.kind != frontend::UnitKind::VhdlPackage
                    || !spec.primary_name.empty()) {
                    continue;
                }
                const auto body_index = std::ranges::find_if(
                    parsed.units, [&](const frontend::DesignUnit& candidate) {
                        const auto candidate_library = candidate.library.empty()
                            ? std::string_view { "work" }
                            : std::string_view { candidate.library };
                        const auto spec_library = spec.library.empty()
                            ? std::string_view { "work" }
                            : std::string_view { spec.library };
                        return candidate.kind == frontend::UnitKind::VhdlPackage
                            && candidate.primary_name == spec.name
                            && candidate.name == spec.name
                            && candidate_library == spec_library;
                    });
                if (body_index == parsed.units.end()) {
                    continue;
                }
                const auto body_offset = static_cast<std::size_t>(
                    std::distance(parsed.units.begin(), body_index));
                const auto* spec_unit = find_unit(spec_index);
                const auto* body_unit = find_unit(body_offset);
                if (spec_unit == nullptr || body_unit == nullptr) {
                    continue;
                }
                for (const auto& parameter : spec.parameters) {
                    if (!parameter.vhdl_deferred) {
                        continue;
                    }
                    const auto spec_declaration = find_constant(*spec_unit, parameter.name);
                    const auto body_declaration = find_constant(*body_unit, parameter.name);
                    if (!spec_declaration || !body_declaration) {
                        continue;
                    }
                    auto& output = declaration(*spec_declaration);
                    output.completion = *body_declaration;
                    output.completion_source = declaration(*body_declaration).source;
                }
            }
        }

        void rebuild_overload_sets()
        {
            std::map<std::pair<semantic::ScopeId, std::string>,
                std::vector<semantic::DeclarationId>>
                groups;
            for (const auto& declaration : hir_.declarations()) {
                groups[{ declaration.scope, canonical_vhdl_name(declaration.name) }]
                    .push_back(declaration.id);
            }
            auto& overloads = hir_.mutable_overload_sets();
            overloads.clear();
            overloads.reserve(groups.size());
            for (auto& [key, declarations] : groups) {
                overloads.push_back(
                    { key.first, std::move(key.second), std::move(declarations) });
            }
        }

        semantic::Model& model_;
        vh::Hir& hir_;
        frontend::VhdlStandard current_vhdl_standard_ {
            frontend::VhdlStandard::Vhdl2008
        };
    };

} // namespace

semantic::vhdl::Hir build_vhdl_hir(
    const frontend::ParsedDesign& parsed,
    semantic::Model& semantics)
{
    semantic::vhdl::Hir result;
    VhdlHirBuilder builder { semantics, result };
    builder.add_design(parsed);
    complete_vhdl_executable_hir(parsed, semantics, result);
    return result;
}

} // namespace fsim::app::application_detail
