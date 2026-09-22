// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/compiled_design.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::semantic {

struct SpecializedHirActualIdentity {
    DeclarationId declaration;
    std::string identity;
    // Non-owning semantic target retained for callable and package actuals.
    // It is deliberately separate from identity: cache keys remain based on
    // the canonical identity while direct HIR lowering can follow the bound
    // declaration without reconstructing it from syntax or parsing strings.
    std::optional<DeclarationId> actual_declaration { };
    // Some actuals, including constrained VHDL types and wide SystemVerilog
    // constants, cannot be executed from their canonical identity alone.
    // Retain the non-owning compiled-HIR expression for specialization and
    // lowering without decoding the cache-key identity.
    std::optional<ExpressionId> actual_expression { };
    // Type actuals remain HIR records, not identity strings. Keeping the
    // selected type beside the deterministic cache-key component lets
    // specialization resolve declaration layouts without reparsing either
    // source spelling or the identity serialization.
    std::optional<sv::TypeReference> systemverilog_type { };
    std::optional<vhdl::SubtypeIndication> vhdl_type { };
    // Retain the association-owned span so legality checks can distinguish
    // constraints written on this actual from constraints inherited through
    // its named subtype. This is non-owning compiled-HIR provenance.
    SourceSpanId source { };

    friend bool operator==(const SpecializedHirActualIdentity&,
        const SpecializedHirActualIdentity&) = default;
};

struct SpecializedHirNamedIdentity {
    std::string name;
    std::string identity;

    friend bool operator==(const SpecializedHirNamedIdentity&,
        const SpecializedHirNamedIdentity&) = default;
};

/// Encode an already-decoded SystemVerilog string as a stable specialization
/// identity. The byte count and hexadecimal payload preserve embedded NULs
/// without retaining source spelling or parser-owned expression records.
[[nodiscard]] std::string systemverilog_string_identity(
    std::string_view bytes);

enum class SpecializedHirAssociationSurface : std::uint8_t {
    parameters,
    ports,
};

enum class SpecializedHirAssociationKind : std::uint8_t {
    expression,
    type,
    default_value,
    open,
};

/// One actual after named/positional association has been resolved against a
/// compiled target's parameter, generic, or port surface. The identity is a
/// deterministic, parser-independent specialization-key component. It is
/// empty only for open/default associations.
struct SpecializedHirAssociationBinding {
    DeclarationId formal;
    SpecializedHirAssociationKind kind {
        SpecializedHirAssociationKind::expression
    };
    std::optional<ExpressionId> expression;
    std::optional<DeclarationId> actual_declaration { };
    std::optional<sv::TypeReference> systemverilog_type { };
    std::optional<vhdl::SubtypeIndication> vhdl_type { };
    std::string identity;
    SourceSpanId source;

    friend bool operator==(const SpecializedHirAssociationBinding&,
        const SpecializedHirAssociationBinding&) = default;
};

enum class SpecializedHirAssociationDiagnostic : std::uint8_t {
    invalid_actual,
    ambiguous_name,
    duplicate_actual,
    association_order,
    subtype_constraint,
    missing_type_actual,
    type_actual_for_value_parameter,
    value_actual_for_type_parameter,
    callable_result_profile,
    callable_name_required,
    callable_no_match,
    callable_ambiguous,
    callable_body_missing,
    callable_impure,
    callable_wrong_kind,
    callable_scoped,
    callable_language_mismatch,
    package_language_mismatch,
    callable_time_dependent,
};

struct SpecializedHirAssociationIssue {
    SpecializedHirAssociationDiagnostic diagnostic {
        SpecializedHirAssociationDiagnostic::invalid_actual
    };
    std::string message;
    SourceSpanId source;
    std::optional<DeclarationId> formal;
    std::optional<bool> callable_function;
};

struct SpecializedHirAssociationResult {
    std::vector<SpecializedHirAssociationBinding> bindings;
    // Association validation is intentionally non-fatal: valid bindings are
    // retained so callers can still specialize defaults and report all
    // independent errors in one elaboration pass. The legacy singular fields
    // retain the first issue for callers that have not adopted accumulation.
    std::vector<SpecializedHirAssociationIssue> issues;
    std::string error;
    SourceSpanId error_source;
    std::optional<DeclarationId> error_formal;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error.empty();
    }
};

/// One selected VHDL generate-body occurrence. The region remains owned by
/// CompiledDesign; the occurrence contributes only hierarchy-local identity
/// bindings and its deterministic path within the selected unit.
struct SpecializedVhdlGenerateOccurrence {
    const vhdl::GenerateRegion* region { };
    std::string relative_path;
    std::optional<std::int64_t> iteration;
    std::vector<SpecializedHirNamedIdentity> hierarchy_identities;
};

struct SpecializedVhdlGenerateResult {
    std::vector<SpecializedVhdlGenerateOccurrence> occurrences;
    std::string diagnostic_code;
    std::string error;
    SourceSpanId error_source;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error.empty();
    }
};

/// Unit-scoped specialization inputs and the residual HIR records affected by
/// them. This overlay owns only canonical actual identities and semantic IDs;
/// the selected unit and its HIR remain owned by CompiledDesign.
struct SpecializedHirOverlay {
    UnitId unit;
    ScopeId scope;
    Language language { Language::system_verilog };
    std::vector<SpecializedHirActualIdentity> actual_identities;
    // Hierarchy-local constants, such as an iterative-generate variable, are
    // named because they intentionally have no declaration identity in the
    // immutable compiled HIR. They are an in-memory specialization input and
    // never become a second owning IR or a persistent cache key.
    std::vector<SpecializedHirNamedIdentity> hierarchy_identities;
    std::vector<ExpressionId> residual_expressions;
    std::vector<DeclarationId> dependent_generates;

    friend bool operator==(const SpecializedHirOverlay&,
        const SpecializedHirOverlay&) = default;
};

enum class SpecializedHirConstantEffectSeverity : std::uint8_t {
    note,
    warning,
    error,
    failure,
};

/// One ordered, parser-independent side effect emitted while evaluating a
/// SystemVerilog constant callable. Informational effects do not invalidate
/// the value; error and failure effects are translated to elaboration
/// diagnostics by the caller.
struct SpecializedHirConstantEffect {
    SpecializedHirConstantEffectSeverity severity {
        SpecializedHirConstantEffectSeverity::error
    };
    std::string code;
    std::string message;
    SourceSpanId source;
};

struct SpecializedHirIntegralEvaluation {
    std::optional<std::int64_t> value;
    std::vector<SpecializedHirConstantEffect> effects;
};

/// Mutable, unit-scoped HIR replacements for one specialization. Records not
/// replaced here remain owned by, and are resolved through, the immutable
/// CompiledDesign supplied at construction. Replacements never allocate new
/// semantic identities.
class SpecializedHirUnit {
public:
    [[nodiscard]] const CompiledDesign& design() const noexcept;
    [[nodiscard]] const SpecializedHirOverlay& specialization() const noexcept;
    [[nodiscard]] UnitId unit() const noexcept;
    [[nodiscard]] ScopeId scope() const noexcept;
    [[nodiscard]] Language language() const noexcept;

    /// Evaluate the bounded integral HIR subset in this specialization. Name
    /// references use actual_identities before declaration defaults, so a
    /// child association depending on a parent actual is evaluated once per
    /// parent specialization without consulting syntax storage.
    [[nodiscard]] std::optional<std::int64_t>
    evaluate_integral_expression(ExpressionId expression) const;

    /// Evaluate SystemVerilog constant truth using integral, four-state, and
    /// string-comparison rules. A missing result means the expression still
    /// depends on runtime state and cannot select a generate alternative.
    [[nodiscard]] std::optional<bool>
    evaluate_systemverilog_truth_expression(
        ExpressionId expression) const;

    /// Evaluate a SystemVerilog constant while preserving four-state bits.
    /// This is used by case-generate validation, where X and Z are legal
    /// selector and choice values even though they are not integral values.
    [[nodiscard]] std::optional<std::string>
    evaluate_systemverilog_bits_expression(
        ExpressionId expression) const;

    /// Evaluate an integral expression and retain ordered severity-system-task
    /// effects from every constant-callable invocation. Cached calls replay
    /// effects in call order rather than suppressing observable messages.
    [[nodiscard]] SpecializedHirIntegralEvaluation
    evaluate_integral_expression_with_effects(
        ExpressionId expression) const;

    /// Execute the constant statement subset retained directly by a program
    /// unit. These statements are elaboration actions, not runtime processes.
    [[nodiscard]] std::optional<
        std::vector<SpecializedHirConstantEffect>>
    evaluate_systemverilog_program_statements(
        std::span<const StatementId> statements) const;

    /// Evaluate one integral parameter, generic, or local constant after
    /// applying the declaration's specialized width and signedness.
    [[nodiscard]] std::optional<std::int64_t>
    evaluate_integral_declaration(DeclarationId declaration) const;

    /// Evaluate the bounded string HIR subset in this specialization. The
    /// result contains decoded bytes, including embedded NULs, rather than a
    /// source spelling or serialized expression identity.
    [[nodiscard]] std::optional<std::string>
    evaluate_string_expression(ExpressionId expression) const;

    /// Evaluate one string parameter, generic, or local constant using
    /// canonical actual identities before its compiled-HIR initializer.
    [[nodiscard]] std::optional<std::string>
    evaluate_string_declaration(DeclarationId declaration) const;

    /// Return the deterministic specialization identity for a VHDL callable
    /// or package-instance declaration. Generic subprogram instances retain
    /// their template and generic-map identities instead of degrading to the
    /// spelling of a name expression.
    [[nodiscard]] std::optional<std::string>
    vhdl_declaration_identity(DeclarationId declaration) const;

    /// Units whose source contributes to a bound VHDL callable or package
    /// actual. This includes separate package bodies and the packages they
    /// depend on, so native-code cache keys invalidate with callable bodies.
    [[nodiscard]] std::vector<UnitId>
    vhdl_declaration_dependency_units(DeclarationId declaration) const;

    /// Derive a working specialization for one hierarchy occurrence. This is
    /// used to evaluate and lower a generate template independently for each
    /// genvar value while retaining the template's compiled-HIR identities.
    [[nodiscard]] SpecializedHirUnit with_hierarchy_identities(
        std::span<const SpecializedHirNamedIdentity> identities) const;

    /// Derive a hierarchy-local working specialization with additional
    /// generic actuals. VHDL block interfaces use this path because their
    /// formals are declarations inside the selected architecture rather than
    /// members of the design-unit generic surface.
    [[nodiscard]] SpecializedHirUnit with_local_actual_identities(
        std::span<const SpecializedHirActualIdentity> actuals) const;

    [[nodiscard]] std::span<const DeclarationId>
    selected_generates() const noexcept;
    [[nodiscard]] bool select_generate(DeclarationId declaration);

    [[nodiscard]] bool replace(sv::Declaration replacement);
    [[nodiscard]] bool replace(vhdl::Declaration replacement);
    [[nodiscard]] bool replace(sv::TypeDefinition replacement);
    [[nodiscard]] bool replace(vhdl::TypeDefinition replacement);
    [[nodiscard]] bool replace(sv::Expression replacement);
    [[nodiscard]] bool replace(vhdl::Expression replacement);
    [[nodiscard]] bool replace(sv::Statement replacement);
    [[nodiscard]] bool replace(vhdl::Statement replacement);
    [[nodiscard]] bool replace(sv::Process replacement);
    [[nodiscard]] bool replace(vhdl::Process replacement);
    [[nodiscard]] bool replace(sv::Instance replacement);
    [[nodiscard]] bool replace(vhdl::Instance replacement);

    [[nodiscard]] std::optional<CompiledDeclarationView>
    find_declaration(DeclarationId id) const noexcept;
    [[nodiscard]] std::optional<CompiledTypeView>
    find_type(TypeId id) const noexcept;
    [[nodiscard]] std::optional<CompiledExpressionView>
    find_expression(ExpressionId id) const noexcept;
    [[nodiscard]] std::optional<CompiledStatementView>
    find_statement(StatementId id) const noexcept;
    [[nodiscard]] std::optional<CompiledProcessView>
    find_process(ProcessId id) const noexcept;
    [[nodiscard]] std::optional<CompiledInstanceView>
    find_instance(InstanceId id) const noexcept;
    [[nodiscard]] std::optional<CompiledInstanceView>
    find_instance(ScopeId scope, std::string_view name) const noexcept;

    /// Instance identities declared by the selected unit and any unit that
    /// contributes to its specialization (for example, a VHDL entity paired
    /// with the selected architecture), including instances recursively owned
    /// by nested generate regions. IDs are stable and sorted.
    [[nodiscard]] std::span<const InstanceId>
    instance_ids() const noexcept;

    /// Effective, non-owning HIR records for instance_ids(). Replacement
    /// records shadow their immutable CompiledDesign records.
    [[nodiscard]] std::vector<CompiledInstanceView> instances() const;

    /// Instance identities active for this specialization. Unit-direct
    /// instances are always present. Resolved generate regions contribute
    /// only their selected branch; a generate whose residual expression is
    /// not supported yet conservatively contributes its complete subtree.
    [[nodiscard]] std::span<const InstanceId>
    active_instance_ids() const noexcept;

    /// Effective, non-owning HIR records for active_instance_ids().
    [[nodiscard]] std::vector<CompiledInstanceView> active_instances() const;

    /// Instance identities declared directly in one generate region. Nested
    /// regions have independent declaration identities and are queried
    /// separately. IDs are stable and sorted.
    [[nodiscard]] std::span<const InstanceId>
    generate_instance_ids(DeclarationId declaration) const noexcept;

    /// Union of the direct instances owned by selected_generates(). Selecting
    /// a parent does not implicitly select a dependent nested alternative.
    [[nodiscard]] std::span<const InstanceId>
    selected_generate_instance_ids() const noexcept;

    /// Effective, non-owning HIR records for generate_instance_ids().
    [[nodiscard]] std::vector<CompiledInstanceView>
    generate_instances(DeclarationId declaration) const;

    /// Effective, non-owning HIR records for
    /// selected_generate_instance_ids().
    [[nodiscard]] std::vector<CompiledInstanceView>
    selected_generate_instances() const;

    [[nodiscard]] std::span<const sv::Declaration>
    systemverilog_declarations() const noexcept;
    [[nodiscard]] std::span<const vhdl::Declaration>
    vhdl_declarations() const noexcept;
    [[nodiscard]] std::span<const sv::TypeDefinition>
    systemverilog_types() const noexcept;
    [[nodiscard]] std::span<const vhdl::TypeDefinition>
    vhdl_types() const noexcept;
    [[nodiscard]] std::span<const sv::Expression>
    systemverilog_expressions() const noexcept;
    [[nodiscard]] std::span<const vhdl::Expression>
    vhdl_expressions() const noexcept;
    [[nodiscard]] std::span<const sv::Statement>
    systemverilog_statements() const noexcept;
    [[nodiscard]] std::span<const vhdl::Statement>
    vhdl_statements() const noexcept;
    [[nodiscard]] std::span<const sv::Process>
    systemverilog_processes() const noexcept;
    [[nodiscard]] std::span<const vhdl::Process>
    vhdl_processes() const noexcept;
    [[nodiscard]] std::span<const sv::Instance>
    systemverilog_instances() const noexcept;
    [[nodiscard]] std::span<const vhdl::Instance>
    vhdl_instances() const noexcept;

private:
    struct GenerateInstances {
        DeclarationId declaration;
        std::vector<InstanceId> instances;
    };

    SpecializedHirUnit(const CompiledDesign& design,
        SpecializedHirOverlay specialization,
        std::vector<UnitId> replacement_units,
        bool validated_lookup_indexes);

    friend struct SpecializedHirUnitFactory;

    const CompiledDesign* design_ { };
    // True only when construction received ValidatedCompiledDesign's proof
    // that the immutable design's non-owning indexes are current.
    bool validated_lookup_indexes_ { };
    SpecializedHirOverlay specialization_;
    std::vector<UnitId> replacement_units_;
    std::vector<DeclarationId> selected_generates_;
    std::vector<InstanceId> instance_ids_;
    std::vector<InstanceId> active_instance_ids_;
    std::vector<GenerateInstances> generate_instances_;
    std::vector<InstanceId> selected_generate_instance_ids_;
    std::vector<sv::Declaration> systemverilog_declarations_;
    std::vector<vhdl::Declaration> vhdl_declarations_;
    std::vector<sv::TypeDefinition> systemverilog_types_;
    std::vector<vhdl::TypeDefinition> vhdl_types_;
    std::vector<sv::Expression> systemverilog_expressions_;
    std::vector<vhdl::Expression> vhdl_expressions_;
    std::vector<sv::Statement> systemverilog_statements_;
    std::vector<vhdl::Statement> vhdl_statements_;
    std::vector<sv::Process> systemverilog_processes_;
    std::vector<vhdl::Process> vhdl_processes_;
    std::vector<sv::Instance> systemverilog_instances_;
    std::vector<vhdl::Instance> vhdl_instances_;
};

/// Construct a parser-independent specialization overlay for one compiled
/// unit. Actual declarations must belong to the selected unit's parameter or
/// generic surface. A VHDL architecture also admits its primary entity's
/// generics and includes that entity when collecting dependent HIR records.
[[nodiscard]] std::optional<SpecializedHirOverlay>
make_specialized_hir_overlay(
    const CompiledDesign& design,
    UnitId selected_unit,
    std::span<const SpecializedHirActualIdentity> actuals);

/// Resolve elaboration identity bookkeeping against the selected compiled-HIR
/// surface. Canonical identities take precedence over fallback display values;
/// entries that are not parameters or generics of the selected unit are
/// ignored. This intentionally excludes local parameters, specparams, and
/// local package instances without losing mixed value/non-value actuals.
[[nodiscard]] std::optional<SpecializedHirOverlay>
make_specialized_hir_overlay(
    const CompiledDesign& design,
    UnitId selected_unit,
    std::span<const SpecializedHirNamedIdentity> canonical_actuals,
    std::span<const SpecializedHirNamedIdentity> fallback_actuals);

/// Resolve one compiled instance's actual list against a compiled target.
/// Named and positional rules are applied in source order. A VHDL
/// architecture draws its generic/port surface from its primary entity.
/// Parent-dependent integral expressions are evaluated through parent when
/// available; other residuals receive a deterministic structural identity.
[[nodiscard]] SpecializedHirAssociationResult
resolve_specialized_hir_associations(
    const CompiledDesign& design,
    UnitId target_unit,
    CompiledInstanceView instance,
    SpecializedHirAssociationSurface surface,
    const SpecializedHirUnit* parent = nullptr);

/// Resolve one retained VHDL block's generic map against the generic
/// declarations owned by that block. The returned bindings can be attached
/// to a hierarchy-local working specialization without promoting the block
/// to a second owning unit IR.
[[nodiscard]] SpecializedHirAssociationResult
resolve_specialized_hir_vhdl_block_associations(
    const CompiledDesign& design,
    const vhdl::GenerateRegion& block,
    const SpecializedHirUnit& parent);

/// Evaluate every VHDL generate owned by the selected unit and expose its
/// selected body occurrences in hierarchy order. Iterative regions produce
/// one occurrence per genvar value; each occurrence retains the complete set
/// of enclosing hierarchy identities so callers can derive a working HIR
/// specialization with with_hierarchy_identities(). No HIR record is copied
/// or reconstructed from syntax.
[[nodiscard]] SpecializedVhdlGenerateResult
specialize_vhdl_generate_occurrences(const SpecializedHirUnit& unit);

/// Construct an empty working replacement overlay using the same validated,
/// canonical specialization metadata as make_specialized_hir_overlay().
[[nodiscard]] std::optional<SpecializedHirUnit>
make_specialized_hir_unit(
    const CompiledDesign& design,
    UnitId selected_unit,
    std::span<const SpecializedHirActualIdentity> actuals);
[[nodiscard]] std::optional<SpecializedHirUnit>
make_specialized_hir_unit(
    const CompiledDesign& design,
    UnitId selected_unit,
    std::span<const SpecializedHirNamedIdentity> canonical_actuals,
    std::span<const SpecializedHirNamedIdentity> fallback_actuals);
/// Construct a specialization after the caller has validated and frozen the
/// compiled design for one elaboration. This preserves the checked public
/// entry point while avoiding repeated whole-design validation per occurrence.
[[nodiscard]] std::optional<SpecializedHirUnit>
make_specialized_hir_unit(
    const ValidatedCompiledDesign& design,
    UnitId selected_unit,
    std::span<const SpecializedHirActualIdentity> actuals);
[[nodiscard]] std::optional<SpecializedHirUnit>
make_specialized_hir_unit(
    const ValidatedCompiledDesign& design,
    UnitId selected_unit,
    std::span<const SpecializedHirNamedIdentity> canonical_actuals,
    std::span<const SpecializedHirNamedIdentity> fallback_actuals);
std::optional<SpecializedHirUnit> make_specialized_hir_unit(
    CompiledDesign&&,
    UnitId,
    std::span<const SpecializedHirActualIdentity>) = delete;
std::optional<SpecializedHirUnit> make_specialized_hir_unit(
    CompiledDesign&&,
    UnitId,
    std::span<const SpecializedHirNamedIdentity>,
    std::span<const SpecializedHirNamedIdentity>) = delete;

} // namespace fsim::semantic
