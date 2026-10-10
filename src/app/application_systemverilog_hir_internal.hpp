// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "application_internal.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <unordered_map>

namespace fsim::app::application_detail::systemverilog_hir_detail {

namespace sv = semantic::sv;

[[nodiscard]] sv::UnitKind unit_kind(frontend::UnitKind kind) noexcept;
[[nodiscard]] sv::Direction direction(frontend::PortDirection value) noexcept;
[[nodiscard]] sv::EdgeKind edge_kind(frontend::EdgeKind value) noexcept;
[[nodiscard]] sv::ConcurrentAssertionKind concurrent_assertion_kind(
    frontend::SystemVerilogConcurrentAssertionKind kind) noexcept;
[[nodiscard]] sv::AssertionRegion assertion_region(
    frontend::SystemVerilogAssertionRegion region) noexcept;
[[nodiscard]] sv::Lifetime lifetime(
    bool automatic, bool explicit_lifetime) noexcept;
[[nodiscard]] frontend::FunctionDeclaration class_function(
    const frontend::SystemVerilogClassMethod& input);
[[nodiscard]] frontend::TaskDeclaration class_task(
    const frontend::SystemVerilogClassMethod& input);
[[nodiscard]] semantic::TypeId find_systemverilog_hir_type(
    const semantic::Model& model,
    const sv::Hir& hir,
    semantic::ScopeId scope,
    std::string_view spelling) noexcept;

/// An omitted call argument, positional `f(, 1)` or named `f(.s())`,
/// takes the formal's default (IEEE 1800-2017 13.5.3). The returned flags
/// mark the operands to drop; `names` is replaced by the names of the kept
/// operands, with positional actuals after an omitted one named by their
/// formals. Nothing changes when the callee's formals are unknown.
inline std::vector<bool> omit_systemverilog_call_arguments(
    const frontend::Expression& input,
    const std::optional<semantic::sv::Name>& referenced_name,
    const semantic::sv::Hir& hir,
    std::vector<std::string>& names)
{
    std::vector<bool> omitted(input.operands.size());
    if (input.kind != frontend::ExpressionKind::Call
        || input.text.starts_with('$') || !referenced_name
        || referenced_name->overloads.size() != 1U
        || std::ranges::none_of(input.operands,
            [](const frontend::Expression& operand) {
                return operand.kind == frontend::ExpressionKind::Invalid;
            })) {
        return omitted;
    }
    const auto callable = std::ranges::find(hir.declarations(),
        referenced_name->overloads.front(),
        &semantic::sv::Declaration::id);
    if (callable == hir.declarations().end() || !callable->callable) {
        return omitted;
    }
    std::vector<std::string> formal_names;
    for (const auto formal : callable->callable->formals) {
        const auto record = std::ranges::find(
            hir.declarations(), formal, &semantic::sv::Declaration::id);
        if (record == hir.declarations().end()) {
            return omitted;
        }
        formal_names.push_back(record->name);
    }
    // More actuals than formals, even empty ones, stay an error (13.5).
    if (input.operands.size() > formal_names.size()) {
        return omitted;
    }
    std::vector<std::string> spelled(input.operands.size());
    for (std::size_t index = 0U;
        index < spelled.size() && index < input.call_argument_names.size();
        ++index) {
        spelled[index] = input.call_argument_names[index];
    }
    std::vector<std::string> kept;
    bool shifted = false;
    std::size_t position = 0U;
    for (std::size_t index = 0U; index < spelled.size(); ++index) {
        const bool empty = input.operands[index].kind
            == frontend::ExpressionKind::Invalid;
        if (spelled[index].empty()) {
            if (empty) {
                shifted = true;
            } else if (shifted && position < formal_names.size()) {
                spelled[index] = formal_names[position];
            }
            ++position;
        }
        omitted[index] = empty;
        if (!empty) {
            kept.push_back(spelled[index]);
        }
    }
    names = std::move(kept);
    return omitted;
}

class SystemVerilogHirBuilder final {
public:
    SystemVerilogHirBuilder(
        semantic::Model& model,
        sv::Hir& hir,
        std::span<const frontend::SystemVerilogClassSpecialization>
            class_specializations);

    void add_design(const frontend::ParsedDesign& parsed);

private:
    // True while projecting a typedef's own base type, which must not
    // resolve an anonymous enumeration to its hidden typedef (itself).
    bool projecting_typedef_base_ = false;
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
        semantic::SourceSpanId span,
        semantic::OriginId parent,
        std::string_view detail);
    [[nodiscard]] semantic::UnitId scope_unit(
        semantic::ScopeId scope) const;
    [[nodiscard]] semantic::ScopeId nested_scope(
        semantic::ScopeId parent_scope,
        std::string_view name,
        semantic::SourceSpanId span,
        semantic::OriginId scope_origin);
    [[nodiscard]] sv::Name name(
        std::string_view spelling,
        const frontend::SourceSpan& span,
        semantic::ScopeId scope);
    [[nodiscard]] std::optional<semantic::ExpressionId> expression(
        const frontend::Expression& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] static sv::ExpressionKind expression_kind(
        const frontend::Expression& expression) noexcept;
    [[nodiscard]] semantic::TypeId find_type(
        semantic::ScopeId scope,
        std::string_view type_name,
        std::optional<semantic::SourceSpanId> exact_source = std::nullopt)
        const noexcept;
    [[nodiscard]] semantic::ValueId find_value(
        semantic::ScopeId scope,
        std::string_view value_name,
        semantic::SourceSpanId exact_source) const noexcept;
    [[nodiscard]] sv::PackedRange packed_range(
        const frontend::PackedRange& range,
        semantic::SourceSpanId span) const;
    [[nodiscard]] static sv::ClassVisibility class_visibility(
        frontend::SystemVerilogClassVisibility visibility) noexcept;
    [[nodiscard]] static sv::ConstraintExpressionKind constraint_kind(
        const frontend::Expression& input) noexcept;
    [[nodiscard]] sv::ConstraintExpression constraint_expression(
        const frontend::Expression& input);
    [[nodiscard]] sv::TypeReference class_property_type(
        const frontend::Type& input,
        semantic::SourceSpanId type_source);
    [[nodiscard]] static bool owner_matches(
        std::string_view owner, std::string_view selected) noexcept;
    [[nodiscard]] bool specializes_or_derives(
        const frontend::SystemVerilogClassSpecialization& specialization,
        std::string_view declaration_identity) const;
    [[nodiscard]] std::optional<sv::ConstraintBinding> property_binding(
        const frontend::SystemVerilogClassDeclaration& declaration,
        const frontend::SystemVerilogClassSpecialization& specialization,
        std::string spelling,
        semantic::SourceSpanId binding_source);
    [[nodiscard]] std::optional<sv::ConstraintBinding> parameter_binding(
        const frontend::SystemVerilogClassDeclaration& declaration,
        const frontend::SystemVerilogClassSpecialization& specialization,
        std::string_view spelling,
        semantic::SourceSpanId binding_source);
    [[nodiscard]] std::optional<sv::ConstraintBinding> method_binding(
        const frontend::SystemVerilogClassSpecialization& specialization,
        std::string spelling,
        semantic::SourceSpanId binding_source);
    void resolve_constraint_expression(
        sv::ConstraintExpression& expression,
        const frontend::SystemVerilogClassDeclaration& declaration,
        std::optional<std::string_view> enclosing_foreach = std::nullopt);
    [[nodiscard]] semantic::ScopeId ensure_compilation_unit(
        const frontend::SourceSpan& span,
        std::string_view source_library,
        std::string_view compilation_unit_identity,
        frontend::StandardRevision standard_revision,
        std::string_view compatibility_profile,
        std::string_view canonical_parent);
    [[nodiscard]] semantic::ScopeId class_parent_scope(
        const frontend::SystemVerilogClassDeclaration& input);
    [[nodiscard]] std::string class_identity(
        const frontend::SystemVerilogClassDeclaration& input,
        semantic::ScopeId parent_scope) const;
    [[nodiscard]] sv::ClassRelation class_relation(
        const frontend::SystemVerilogClassBase& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    void add_class(
        const frontend::SystemVerilogClassDeclaration& input,
        std::optional<semantic::ScopeId> parent_scope = std::nullopt,
        std::optional<semantic::DeclarationId> generate_owner = std::nullopt,
        std::string_view alternative_discriminator = {});
    void add_classes(
        const std::vector<frontend::SystemVerilogClassDeclaration>& inputs,
        std::optional<semantic::ScopeId> parent_scope = std::nullopt,
        std::optional<semantic::DeclarationId> generate_owner = std::nullopt,
        std::string_view alternative_discriminator = {});
    void synchronize_generate_class_ownership();
    void compose_constraints();

    [[nodiscard]] sv::PackedRange packed_range(
        const frontend::PackedRangeExpression& range,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] sv::TypeReference type_reference(
        const frontend::Type& input,
        const frontend::SourceSpan& fallback,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] semantic::DeclarationId add_declaration_record(
        semantic::ScopeId scope,
        sv::DeclarationForm form,
        semantic::DeclarationKind kind,
        std::string_view declaration_name,
        const frontend::SourceSpan& frontend_span,
        semantic::OriginId parent);
    [[nodiscard]] sv::Declaration& declaration(
        semantic::DeclarationId id);
    [[nodiscard]] semantic::TypeId ensure_type(
        std::string_view type_name,
        semantic::ScopeId scope,
        semantic::TypeKind kind,
        const sv::TypeReference& base,
        semantic::SourceSpanId span,
        semantic::OriginId declaration_origin);
    [[nodiscard]] semantic::ValueId ensure_value(
        std::string_view value_name,
        semantic::ScopeId scope,
        semantic::ValueKind kind,
        const sv::TypeReference& type,
        semantic::SourceSpanId span,
        semantic::OriginId declaration_origin);
    [[nodiscard]] sv::TypeForm type_form(
        const frontend::Type& input) const noexcept;
    [[nodiscard]] semantic::DeclarationId add_type_declaration(
        const frontend::TypeAliasDeclaration& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    void add_packed_members(
        const frontend::Type& input,
        sv::TypeDefinition& output,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    void add_type_payload(
        const frontend::TypeAliasDeclaration& input,
        sv::TypeDefinition& output,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    template <typename Input>
    [[nodiscard]] semantic::DeclarationId add_object(
        const Input& input,
        semantic::ScopeId scope,
        semantic::OriginId parent,
        sv::DeclarationForm form,
        semantic::DeclarationKind declaration_kind,
        semantic::ValueKind value_kind,
        sv::Direction object_direction,
        const std::optional<frontend::Expression>& initializer);
    [[nodiscard]] semantic::DeclarationId add_parameter(
        const frontend::ParameterDeclaration& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] semantic::DeclarationId add_signal(
        const frontend::SignalDeclaration& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] semantic::DeclarationId add_variable(
        const frontend::VariableDeclaration& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] semantic::DeclarationId add_function_argument(
        const frontend::FunctionArgument& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] semantic::DeclarationId add_task_argument(
        const frontend::TaskArgument& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    template <typename Range, typename Builder>
    void add_children(
        const Range& inputs,
        semantic::ScopeId scope,
        semantic::OriginId parent,
        std::vector<semantic::DeclarationId>& children,
        Builder builder)
    {
        for (const auto& input : inputs) {
            children.push_back((this->*builder)(input, scope, parent));
        }
    }
    [[nodiscard]] semantic::DeclarationId add_function(
        const frontend::FunctionDeclaration& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] semantic::DeclarationId add_task(
        const frontend::TaskDeclaration& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] semantic::DeclarationId add_modport(
        const frontend::SystemVerilogModport& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] sv::Delay instance_delay(
        const frontend::Delay& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] sv::TimingRecord timing_record(
        const frontend::VerilogSpecifyBlock& input,
        semantic::ScopeId scope,
        semantic::OriginId parent,
        std::size_t index);
    [[nodiscard]] sv::SourceToken source_token(
        const frontend::Token& input);
  [[nodiscard]] std::vector<sv::SourceToken> source_tokens(
      const std::vector<frontend::Token>& input);
  [[nodiscard]] sv::CovergroupFormal covergroup_formal(
      const frontend::SystemVerilogCovergroupFormal& input);
  [[nodiscard]] sv::CovergroupOptionAssignment covergroup_option(
      const frontend::SystemVerilogCovergroupOptionAssignment& input);
  [[nodiscard]] sv::CoverageBinValue coverage_bin_value(
      const frontend::SystemVerilogCoverageBinValue& input);
  [[nodiscard]] sv::CoverageTransitionSequence coverage_transition(
      const frontend::SystemVerilogCoverageTransitionSequence& input);
  [[nodiscard]] sv::CoverageBin coverage_bin(
      const frontend::SystemVerilogCoverageBin& input,
      semantic::OriginId parent);
  [[nodiscard]] sv::CoverageItem coverage_item(
      const frontend::SystemVerilogCoverageDeclaration& input,
      semantic::ScopeId scope,
      semantic::OriginId parent);
  [[nodiscard]] sv::CovergroupDeclaration covergroup_declaration(
      const frontend::SystemVerilogCovergroupDeclaration& input,
      semantic::ScopeId scope,
      std::optional<semantic::OriginId> parent);
  [[nodiscard]] semantic::ScopeId coverage_scope(
      std::string_view owner_identity) const;
  [[nodiscard]] sv::CovergroupInstance covergroup_instance(
      const frontend::SystemVerilogCovergroupInstance& input,
      const frontend::SystemVerilogCovergroupInstanceSyntax& syntax);
  [[nodiscard]] sv::DpiDeclaration dpi_declaration(
        const frontend::SystemVerilogDpiDeclaration& input,
        semantic::ScopeId scope,
        std::optional<semantic::OriginId> parent);
    [[nodiscard]] sv::ClockingSkew clocking_skew(
        const frontend::SystemVerilogClockingSkew& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] sv::ClockingBlock clocking_block(
        const frontend::SystemVerilogClockingBlock& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] sv::SequenceRange sequence_range(
        const frontend::SystemVerilogSequenceRange& input);
    [[nodiscard]] sv::SequenceExpression sequence_expression(
        const frontend::SystemVerilogSequenceExpression& input);
    [[nodiscard]] sv::PropertyExpression property_expression(
        const frontend::SystemVerilogPropertyExpression& input);
    [[nodiscard]] sv::ConcurrentAssertion concurrent_assertion(
        const frontend::SystemVerilogConcurrentAssertion& input,
        std::size_t index,
        semantic::OriginId parent);
    [[nodiscard]] sv::AssertionDeclaration assertion_declaration(
        const frontend::SystemVerilogAssertionDeclaration& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);

    [[nodiscard]] semantic::InstanceId ensure_instance(
        const frontend::Instance& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] semantic::ProcessId add_process_skeleton(
        const frontend::Process& input,
        semantic::ScopeId parent_scope,
        semantic::OriginId parent_origin);
    [[nodiscard]] sv::Alias add_alias(
        const frontend::SystemVerilogAliasDeclaration& input,
        semantic::ScopeId scope,
        semantic::OriginId parent,
        std::size_t index);
    [[nodiscard]] sv::LetDeclaration add_let(
        const frontend::SystemVerilogLetDeclaration& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    [[nodiscard]] std::optional<sv::Defparam> add_defparam(
        const frontend::VerilogDefparamDeclaration& input,
        semantic::ScopeId scope,
        semantic::OriginId parent);
    void add_generate_body(
        const frontend::GenerateBody& input,
        sv::GenerateRegion& output);
    [[nodiscard]] sv::GenerateRegion add_generate(
        const frontend::GenerateRegion& input,
        semantic::ScopeId parent_scope,
        semantic::OriginId parent_origin,
        std::string alternative_discriminator = {});
    template <typename Input, typename Builder>
    void queue(
        const std::vector<Input>& inputs,
        std::size_t category,
        semantic::ScopeId scope,
        semantic::OriginId parent,
        std::vector<Pending>& pending,
        Builder builder);
    void queue_declarations(
        const frontend::GenerateBody& input,
        semantic::ScopeId scope,
        semantic::OriginId parent,
        std::vector<Pending>& pending);
    [[nodiscard]] static bool pending_less(
        const Pending& left, const Pending& right) noexcept;
    void add_unit(
        const frontend::DesignUnit& input, semantic::UnitId unit_id);

    semantic::Model& model_;
    sv::Hir& hir_;
    std::span<const frontend::SystemVerilogClassSpecialization>
        class_specializations_;
    std::span<const frontend::SystemVerilogImport> current_imports_;
    std::multimap<std::string, semantic::ScopeId, std::less<>> class_scopes_;
    std::unordered_map<const frontend::Expression*, semantic::ExpressionId>
        expressions_;
    std::unordered_map<std::string, semantic::TypeId>
        anonymous_aggregate_types_;
};

} // namespace fsim::app::application_detail::systemverilog_hir_detail
