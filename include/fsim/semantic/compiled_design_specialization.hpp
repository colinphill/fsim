// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/compiled_design.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace fsim::semantic {

namespace detail {

/// Convert one nonnegative VHDL real physical magnitude to its rounded
/// primary-unit integer value using exact decimal arithmetic.
[[nodiscard]] std::optional<std::uint64_t> decimal_physical_magnitude(
    std::string_view text, std::uint64_t scale);

} // namespace detail

/// A fully known VHDL packed vector produced by bounded constant evaluation.
/// `bits` are ordered from the left bound through the right bound.
struct SpecializedHirVhdlPackedValue {
    std::string bits;
    std::int64_t left_bound { };
    std::int64_t right_bound { };

    friend bool operator==(const SpecializedHirVhdlPackedValue&,
        const SpecializedHirVhdlPackedValue&) = default;
};

/// A statically evaluated record value whose elements are all integer or
/// enumeration scalars, in element declaration order.
struct SpecializedHirVhdlRecordValue {
    TypeId record_type;
    std::vector<std::int64_t> elements;

    friend bool operator==(const SpecializedHirVhdlRecordValue&,
        const SpecializedHirVhdlRecordValue&) = default;
};

/// A bounded, statically evaluated one-dimensional array whose element type
/// is the predefined VHDL BOOLEAN type. The array and element identities are
/// retained separately from the values so a Boolean array cannot be
/// reinterpreted as a packed logic vector or as another nominal array type.
struct SpecializedHirVhdlBooleanArrayValue {
    TypeId array_type;
    TypeId element_type;
    std::int64_t left_bound { };
    std::int64_t right_bound { };
    bool descending { };
    bool null_range { };
    std::vector<std::uint8_t> elements;

    friend bool operator==(const SpecializedHirVhdlBooleanArrayValue&,
        const SpecializedHirVhdlBooleanArrayValue&) = default;
};

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
    // A transient, subtype-checked packed VHDL value for this formal. It is
    // not part of `identity` or the specialization cache key; it lets the
    // target unit evaluate lexical references to packed generic actuals
    // without re-evaluating a parent expression in the child's context.
    std::optional<SpecializedHirVhdlPackedValue> vhdl_packed_value { };
    // A typed Boolean array actual is kept as Boolean elements with its
    // nominal array identity and bounds. It is not encoded as packed bits.
    std::optional<SpecializedHirVhdlBooleanArrayValue>
        vhdl_boolean_array_value { };
    // A record actual of scalar elements, evaluated in the parent so the
    // target unit can select its elements (IEEE 1076-2008 6.5.6.2, 8.3).
    std::optional<SpecializedHirVhdlRecordValue> vhdl_record_value { };
    // A parameter of another unit that a type actual's declaration depends
    // on (`.T(d_t)` with `typedef logic [W-1:0] d_t;` in a parameterized
    // parent): the parent's value, so the type's shape evaluates in this
    // unit as it does in the parent (IEEE 1800-2017 6.20.3). Not a formal
    // of this unit.
    bool type_context { };

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

using SpecializedHirIntegralBinding =
    std::function<std::optional<ExpressionId>(DeclarationId)>;

/// A bounded one-dimensional VHDL array whose elements are packed values.
/// `elements` are ordered from the outer left bound through the right bound;
/// each element retains its own packed bounds and the array's element domain.
struct SpecializedHirVhdlPackedArrayValue {
    std::int64_t left_bound { };
    std::int64_t right_bound { };
    vhdl::ValueDomain element_domain { vhdl::ValueDomain::unknown };
    std::vector<SpecializedHirVhdlPackedValue> elements;

    friend bool operator==(const SpecializedHirVhdlPackedArrayValue&,
        const SpecializedHirVhdlPackedArrayValue&) = default;
};

/// Integral or packed VHDL value returned by typed constant evaluation.
using SpecializedHirVhdlConstantValue =
    std::variant<std::int64_t, SpecializedHirVhdlPackedValue>;

class SpecializedHirUnit;

/// The shared SystemVerilog constant evaluator, which elaboration provides:
/// an integral value, or nullopt when the expression is not constant.
using SystemVerilogConstantIntegral = std::optional<std::int64_t> (*)(
    const SpecializedHirUnit& unit, ExpressionId expression);

/// Register the shared constant evaluator, which integral evaluation and
/// generate selection use where the integral fast path cannot evaluate
/// (IEEE 1800-2017 11.2.1, 27.5).
void set_systemverilog_constant_integral(
    SystemVerilogConstantIntegral evaluator) noexcept;

/// Mutable, unit-scoped HIR replacements for one specialization. Records not
/// replaced here remain owned by, and are resolved through, the immutable
/// CompiledDesign supplied at construction. Replacements never allocate new
/// semantic identities.
class SpecializedHirUnit {
public:
    /// Derived data a client caches for this exact specialization (for
    /// example elaboration's constant memo). Copies share it; replace()
    /// detaches it. It is not part of the specialization's value.
    [[nodiscard]] std::shared_ptr<void>& client_cache() const noexcept
    {
        return client_cache_;
    }

    [[nodiscard]] const CompiledDesign& design() const noexcept;
    [[nodiscard]] const SpecializedHirOverlay& specialization() const noexcept;
    [[nodiscard]] UnitId unit() const noexcept;
    [[nodiscard]] ScopeId scope() const noexcept;
    [[nodiscard]] Language language() const noexcept;
    /// Whether this specialization replaces any VHDL type definition, so
    /// a replacement may carry a name the design's type index lacks.
    [[nodiscard]] bool replaces_vhdl_types() const noexcept
    {
        return !vhdl_types_.empty();
    }

    /// Evaluate the bounded integral HIR subset in this specialization. Name
    /// references use actual_identities before declaration defaults, so a
    /// child association depending on a parent actual is evaluated once per
    /// parent specialization without consulting syntax storage.
    [[nodiscard]] std::optional<std::int64_t>
    evaluate_integral_expression(ExpressionId expression) const;

    /// Evaluate one integral expression with an explicit, caller-owned
    /// formal-to-actual context. The context is consulted only for selected
    /// VHDL names that are not already active evaluated locals.
    [[nodiscard]] std::optional<std::int64_t>
    evaluate_integral_expression(
        ExpressionId expression,
        const SpecializedHirIntegralBinding& binding) const;

    /// Evaluate the bounded pure VHDL constant subset while preserving
    /// packed-vector width and index bounds. Unsupported or runtime-dependent
    /// expressions return no value.
    [[nodiscard]] std::optional<SpecializedHirVhdlConstantValue>
    evaluate_vhdl_constant_expression(ExpressionId expression) const;

    /// Evaluate one bounded one-dimensional array of packed VHDL constants.
    /// Unsupported shapes, mismatched bounds, and partially initialized
    /// arrays return no value. Elements are ordered from the declared left
    /// index through the right index.
    [[nodiscard]] std::optional<SpecializedHirVhdlPackedArrayValue>
    evaluate_vhdl_packed_array_expression(ExpressionId expression) const;

    /// Evaluate a record value whose elements are all integer or
    /// enumeration scalars. Other values return no value.
    [[nodiscard]] std::optional<SpecializedHirVhdlRecordValue>
    evaluate_vhdl_record_expression(ExpressionId expression) const;

    /// Whether a subtype is a one-dimensional array of the predefined
    /// BOOLEAN type, proven from retained type identity or an unshadowed
    /// compiler-owned STD.STANDARD spelling.
    [[nodiscard]] bool is_vhdl_boolean_array_subtype(
        const vhdl::SubtypeIndication& subtype, ScopeId use_scope) const;

    /// Evaluate a bounded Boolean array expression and apply its expected
    /// subtype. Positional Boolean aggregates, array concatenation, and pure
    /// function results are supported; dynamic or nominally incompatible
    /// expressions return no value.
    [[nodiscard]] std::optional<SpecializedHirVhdlBooleanArrayValue>
    evaluate_vhdl_boolean_array_expression(ExpressionId expression,
        const vhdl::SubtypeIndication& expected_subtype,
        ScopeId use_scope) const;

    /// Evaluate and coerce a bounded packed-vector VHDL constant declaration
    /// to its effective subtype. Unsupported or mismatched values return no
    /// value.
    [[nodiscard]] std::optional<SpecializedHirVhdlPackedValue>
    evaluate_vhdl_packed_value_declaration(
        DeclarationId declaration) const;

    /// Evaluate and coerce a VHDL constant declaration to its effective
    /// declared subtype before projecting a bounded packed array. Runtime,
    /// malformed, or partially initialized declarations return no value.
    [[nodiscard]] std::optional<SpecializedHirVhdlPackedArrayValue>
    evaluate_vhdl_packed_array_declaration(
        DeclarationId declaration) const;

    /// Evaluate SystemVerilog constant truth using integral, four-state, and
    /// string-comparison rules. A missing result means the expression still
    /// depends on runtime state and cannot select a generate alternative.
    [[nodiscard]] std::optional<bool>
    evaluate_systemverilog_truth_expression(
        ExpressionId expression) const;
    /// A generate condition, where an x or z value is false (IEEE 1800-2017
    /// 12.4, 27.5).
    [[nodiscard]] std::optional<bool>
    evaluate_systemverilog_condition_truth(ExpressionId expression) const;

    /// Evaluate a SystemVerilog constant while preserving four-state bits.
    /// This is used by case-generate validation, where X and Z are legal
    /// selector and choice values even though they are not integral values.
    [[nodiscard]] std::optional<std::string>
    evaluate_systemverilog_bits_expression(
        ExpressionId expression) const;

    /// The matching identity of a `type(...)` operator expression (IEEE
    /// 1800-2017 6.23, 6.22.1); nullopt for any other expression.
    [[nodiscard]] std::optional<std::string>
    systemverilog_type_identity(ExpressionId expression) const;

    /// Evaluate the packed bit width of a type in this specialization.
    /// Parameterized ranges and packed members use this unit's actuals.
    [[nodiscard]] std::optional<std::size_t>
    evaluate_systemverilog_type_width(const sv::TypeReference& type) const;

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

    /// The element values an instance gave an unpacked array parameter
    /// (IEEE 1800-2017 6.20.2), leftmost element first through every
    /// dimension; nullopt when the parameter keeps its default.
    [[nodiscard]] std::optional<std::vector<std::int64_t>>
    systemverilog_array_actual(DeclarationId declaration) const;

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
    // This unit with another overlay and the same replacement records.
    [[nodiscard]] SpecializedHirUnit with_overlay(
        SpecializedHirOverlay overlay) const;

    struct GenerateInstances {
        DeclarationId declaration;
        std::vector<InstanceId> instances;
    };

    SpecializedHirUnit(const CompiledDesign& design,
        SpecializedHirOverlay specialization,
        std::vector<UnitId> replacement_units,
        bool validated_lookup_indexes,
        std::shared_ptr<detail::VhdlInitializerMemoContext>
            initializer_memo_context = { });

    friend struct SpecializedHirUnitFactory;

    const CompiledDesign* design_ { };
    std::shared_ptr<detail::VhdlInitializerMemoContext>
        initializer_memo_context_;
    mutable std::shared_ptr<void> client_cache_;
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
