// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "application_internal.hpp"

#include <functional>

namespace fsim::app::application_detail {
namespace vh = semantic::vhdl;

[[nodiscard]] std::string canonical_vhdl_name(std::string value);
[[nodiscard]] vh::UnitKind vhdl_unit_kind(frontend::UnitKind kind) noexcept;
[[nodiscard]] vh::Direction vhdl_direction(
    frontend::PortDirection direction) noexcept;
[[nodiscard]] vh::ObjectClass vhdl_object_class(
    frontend::InterfaceObjectClass object_class) noexcept;

class VhdlHirBuilder final {

public:
    VhdlHirBuilder(semantic::Model& model, vh::Hir& hir);

    void add_design(const frontend::ParsedDesign& parsed);

private:
    struct Pending {
        std::string_view physical_source;
        std::size_t offset { };
        std::size_t category { };
        std::size_t index { };
        std::function<semantic::DeclarationId()> build;
    };

    [[nodiscard]] semantic::SourceSpanId source(
        const frontend::SourceSpan& span);

    [[nodiscard]] semantic::OriginId origin(
        const semantic::SourceSpanId span,
        const semantic::OriginId parent,
        const std::string_view detail);

    [[nodiscard]] semantic::UnitId scope_unit(
        const semantic::ScopeId scope) const;

    [[nodiscard]] vh::Name name(
        const std::string_view spelling,
        const frontend::SourceSpan& span,
        const semantic::ScopeId scope);

    [[nodiscard]] std::optional<semantic::ExpressionId> expression(
        const frontend::Expression& value,
        const semantic::ScopeId scope,
        const semantic::OriginId parent);

    [[nodiscard]] vh::ExpressionKind expression_kind(
        const frontend::Expression& expression) const noexcept;

    [[nodiscard]] semantic::TypeId find_type(
        const semantic::ScopeId scope,
        const std::string_view name_value,
        const std::optional<semantic::SourceSpanId> exact_source = std::nullopt)
        const noexcept;

    [[nodiscard]] semantic::ValueId find_value(
        const semantic::ScopeId scope,
        const std::string_view name_value,
        const semantic::SourceSpanId exact_source) const noexcept;

    [[nodiscard]] semantic::TypeReference type_reference(
        const frontend::Type& type,
        const frontend::SourceSpan& fallback,
        const semantic::ScopeId scope);

    [[nodiscard]] vh::RangeConstraint concrete_range(
        const frontend::IntegerRange& range,
        const vh::RangeKind kind,
        const semantic::SourceSpanId span) const;

    [[nodiscard]] vh::RangeConstraint concrete_range(
        const frontend::EnumerationRange& range,
        const semantic::SourceSpanId span) const;

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
        const semantic::OriginId parent);

    [[nodiscard]] semantic::DeclarationId add_declaration_record(
        const semantic::ScopeId scope,
        const vh::DeclarationForm form,
        const semantic::DeclarationKind common_kind,
        const std::string_view declaration_name,
        const frontend::SourceSpan& frontend_span,
        const semantic::OriginId parent);

    [[nodiscard]] vh::Declaration& declaration(
        const semantic::DeclarationId id);

    [[nodiscard]] semantic::TypeId ensure_type(
        const std::string_view type_name,
        const semantic::ScopeId scope,
        const semantic::TypeKind kind,
        const vh::SubtypeIndication& base,
        const semantic::SourceSpanId span,
        const semantic::OriginId declaration_origin);

    [[nodiscard]] semantic::ValueId ensure_value(
        const std::string_view value_name,
        const semantic::ScopeId scope,
        const semantic::ValueKind kind,
        const vh::SubtypeIndication& value_type,
        const semantic::SourceSpanId span,
        const semantic::OriginId declaration_origin);

    [[nodiscard]] vh::TypeForm type_form(
        const frontend::TypeDeclarationKind kind) const noexcept;

    [[nodiscard]] semantic::DeclarationId add_mode_view_declaration(
        const frontend::TypeAliasDeclaration& input,
        const semantic::ScopeId scope,
        const semantic::OriginId parent);

    [[nodiscard]] semantic::DeclarationId add_type_declaration(
        const frontend::TypeAliasDeclaration& input,
        const semantic::ScopeId scope,
        const semantic::OriginId parent);

    void add_type_payload(
        const frontend::TypeAliasDeclaration& input,
        vh::TypeDefinition& output,
        const semantic::ScopeId scope,
        const semantic::OriginId parent);

    void add_predefined_attributes(
        const frontend::Type& input,
        vh::TypeDefinition& output) const;

    void add_array_payload(
        const frontend::Type& input,
        vh::TypeDefinition& output,
        const semantic::ScopeId scope,
        const semantic::OriginId parent);

    void add_record_payload(
        const frontend::Type& input,
        vh::TypeDefinition& output,
        const semantic::ScopeId scope,
        const semantic::OriginId parent);

    void add_indirect_type_payload(
        const frontend::Type& input,
        vh::TypeDefinition& output,
        const semantic::ScopeId scope,
        const semantic::OriginId parent);

    void add_protected_payload(
        const frontend::Type& input,
        vh::TypeDefinition& output,
        const semantic::DeclarationId owner,
        const semantic::ScopeId parent_scope,
        const semantic::OriginId parent_origin);

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

    template <typename Input>
    void attach_interface_view(
        const semantic::DeclarationId id,
        const Input& input,
        const semantic::ScopeId scope)
    {
        if (!input.vhdl_mode_view) {
            return;
        }
        vh::ModeViewInterfaceProfile profile;
        profile.form = input.vhdl_mode_view->kind
                == frontend::VhdlModeViewIndicationKind::array
            ? vh::ModeViewElementForm::array_view
            : vh::ModeViewElementForm::record_view;
        profile.view = name(
            input.vhdl_mode_view->view,
            input.vhdl_mode_view->span,
            scope);
        profile.explicit_subtype = input.vhdl_mode_view->explicit_subtype;
        declaration(id).interface_view = std::move(profile);
    }

    [[nodiscard]] semantic::DeclarationId add_parameter(
        const frontend::ParameterDeclaration& input,
        const semantic::ScopeId scope,
        const semantic::OriginId parent);

    [[nodiscard]] semantic::DeclarationId add_signal(
        const frontend::SignalDeclaration& input,
        const semantic::ScopeId scope,
        const semantic::OriginId parent);

    [[nodiscard]] semantic::DeclarationId add_variable(
        const frontend::VariableDeclaration& input,
        const semantic::ScopeId scope,
        const semantic::OriginId parent);

    [[nodiscard]] semantic::DeclarationId add_alias(
        const frontend::SignalAliasDeclaration& input,
        const semantic::ScopeId scope,
        const semantic::OriginId parent);

    [[nodiscard]] std::vector<vh::Association> associations(
        const std::vector<frontend::ParameterOverride>& inputs,
        const semantic::ScopeId scope,
        const semantic::OriginId parent);

    [[nodiscard]] std::vector<vh::Association> associations(
        const std::vector<frontend::PortConnection>& inputs,
        const semantic::ScopeId scope,
        const semantic::OriginId parent);

    [[nodiscard]] semantic::ScopeId nested_scope(
        const semantic::ScopeId parent_scope,
        const std::string_view scope_name,
        const semantic::SourceSpanId span,
        const semantic::OriginId scope_origin);

    [[nodiscard]] semantic::DeclarationId add_function_argument(
        const frontend::FunctionArgument& input,
        const semantic::ScopeId scope,
        const semantic::OriginId parent);

    [[nodiscard]] semantic::DeclarationId add_procedure_argument(
        const frontend::ProcedureArgument& input,
        const semantic::ScopeId scope,
        const semantic::OriginId parent);

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
        const semantic::OriginId parent);

    [[nodiscard]] semantic::DeclarationId add_procedure(
        const frontend::ProcedureDeclaration& input,
        const semantic::ScopeId scope,
        const semantic::OriginId parent);

    void add_statement_regions(
        const std::span<const frontend::Statement> statements,
        const semantic::ScopeId enclosing_scope,
        const semantic::OriginId enclosing_origin);

    [[nodiscard]] semantic::DeclarationId add_component_port(
        const frontend::VhdlComponentPort& input,
        const semantic::ScopeId scope,
        const semantic::OriginId parent);

    [[nodiscard]] semantic::DeclarationId add_component(
        const frontend::VhdlComponentDeclaration& input,
        const semantic::ScopeId scope,
        const semantic::OriginId parent);

    [[nodiscard]] semantic::DeclarationId add_package_instance(
        const frontend::PackageInstantiation& input,
        const semantic::ScopeId scope,
        const semantic::OriginId parent);

    [[nodiscard]] semantic::DeclarationId add_attribute(
        const frontend::VhdlAttributeDeclaration& input,
        const semantic::ScopeId scope,
        const semantic::OriginId parent);

    [[nodiscard]] semantic::DeclarationId add_group(
        const frontend::VhdlGroupDeclaration& input,
        const semantic::ScopeId scope,
        const semantic::OriginId parent);

    [[nodiscard]] semantic::DeclarationId add_generic_function_instance(
        const frontend::GenericSubprogramInstantiation& input,
        const semantic::ScopeId scope,
        const semantic::OriginId parent);

    [[nodiscard]] semantic::DeclarationId add_generic_procedure_instance(
        const frontend::GenericSubprogramInstantiation& input,
        const semantic::ScopeId scope,
        const semantic::OriginId parent);

    [[nodiscard]] semantic::DeclarationId add_generic_function_template(
        const frontend::GenericFunctionTemplate& input,
        const semantic::ScopeId scope,
        const semantic::OriginId parent);

    [[nodiscard]] semantic::DeclarationId add_generic_procedure_template(
        const frontend::GenericProcedureTemplate& input,
        const semantic::ScopeId scope,
        const semantic::OriginId parent);

    [[nodiscard]] vh::BindingIndication binding(
        const frontend::VhdlBindingIndication& input,
        const semantic::ScopeId scope,
        const semantic::OriginId parent);

    [[nodiscard]] vh::ComponentConfiguration component_configuration(
        const frontend::VhdlComponentConfiguration& input,
        const semantic::ScopeId scope,
        const semantic::OriginId parent);

    [[nodiscard]] vh::BlockConfiguration block_configuration(
        const frontend::VhdlBlockConfiguration& input,
        const semantic::ScopeId scope,
        const semantic::OriginId parent);

    void queue_body_declarations(
        const frontend::GenerateBody& input,
        const semantic::ScopeId scope,
        const semantic::OriginId parent,
        std::vector<semantic::DeclarationId>& declarations);

    void add_generate_body(
        const frontend::GenerateBody& input,
        vh::GenerateRegion& output);

    [[nodiscard]] semantic::ProcessId add_process_skeleton(
        const frontend::Process& input,
        const semantic::ScopeId parent_scope,
        const semantic::OriginId parent_origin);

    [[nodiscard]] vh::GenerateRegion add_generate(
        const frontend::GenerateRegion& input,
        const semantic::ScopeId parent_scope,
        const semantic::OriginId parent_origin);

    [[nodiscard]] vh::PslExpressionClass psl_expression_class(
        const frontend::VhdlPslExpressionClass kind) const noexcept;

    [[nodiscard]] vh::PslTemporalOperatorKind psl_operator_kind(
        const frontend::VhdlPslTemporalOperatorKind kind) const noexcept;

    [[nodiscard]] std::vector<std::string> psl_token_texts(
        const std::vector<frontend::Token>& tokens) const;

    [[nodiscard]] vh::PslClock psl_clock(
        const frontend::VhdlPslClock& input);

    [[nodiscard]] vh::PslAnalyzedExpression psl_expression(
        const frontend::VhdlPslAnalyzedExpression& input);

    void add_unit(
        const frontend::DesignUnit& input,
        const semantic::UnitId unit_id);

    void add_unit_declarations(
        const frontend::DesignUnit& input,
        vh::Unit& output);

    void link_deferred_package_constants(
        const frontend::ParsedDesign& parsed);

    void rebuild_overload_sets();

    semantic::Model& model_;

    vh::Hir& hir_;

    frontend::VhdlStandard current_vhdl_standard_ {
        frontend::VhdlStandard::Vhdl2008
    };
};

} // namespace fsim::app::application_detail
