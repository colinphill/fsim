// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/model.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::semantic::vhdl {

enum class UnitKind : std::uint8_t {
    entity,
    architecture,
    configuration,
    package,
    context,
    psl_verification_unit,
};

enum class Direction : std::uint8_t {
    unknown,
    input,
    output,
    inout,
    buffer,
};

enum class ObjectClass : std::uint8_t {
    constant,
    signal,
    variable,
    file,
};

enum class ValueDomain : std::uint8_t {
    unknown,
    bit2,
    logic4,
    logic9,
    boolean,
    integer,
    string,
};

enum class DeclarationForm : std::uint8_t {
    type,
    subtype,
    generic_constant,
    generic_type,
    generic_function,
    generic_procedure,
    generic_package,
    port,
    signal,
    constant,
    variable,
    file,
    alias,
    function,
    procedure,
    generic_function_template,
    generic_procedure_template,
    generic_function_instance,
    generic_procedure_instance,
    package_instance,
    component,
    enumeration_literal,
    generated,
    attribute_declaration,
    attribute_specification,
    group_template,
    group_instance,
    mode_view,
};

enum class TypeForm : std::uint8_t {
    unresolved,
    scalar,
    enumeration,
    array,
    record,
    access,
    file,
    protected_type,
    protected_body,
    physical,
    subtype,
    alias,
};

enum class UnspecifiedTypeClass : std::uint8_t {
    none,
    private_type,
    scalar,
    discrete,
    integer,
    physical,
    floating,
    array,
    access,
    file,
};

enum class RangeKind : std::uint8_t {
    integer,
    enumeration,
    discrete,
    array_index,
};

enum class ContextKind : std::uint8_t {
    library_clause,
    use_clause,
    context_reference,
};

enum class PslVerificationUnitKind : std::uint8_t {
    unit,
    property,
    mode,
};

enum class PslDeclarationKind : std::uint8_t {
    default_clock,
    boolean,
    sequence,
    property,
    endpoint,
};

enum class PslDirectiveKind : std::uint8_t {
    assert_directive,
    assume,
    restrict,
    cover,
};

enum class PslExpressionClass : std::uint8_t {
    invalid,
    boolean,
    sequence,
    property,
    endpoint,
    static_integer,
};

enum class PslTemporalOperatorKind : std::uint8_t {
    sequence_concatenation,
    sequence_fusion,
    consecutive_repetition,
    nonconsecutive_repetition,
    goto_repetition,
    overlapped_suffix_implication,
    nonoverlapped_suffix_implication,
    next,
    previous,
    eventually,
    always,
    until,
    before,
    within,
};

enum class PslUnknownPolicy : std::uint8_t {
    false_value,
};

enum class AssociationKind : std::uint8_t {
    expression,
    type,
    open,
    default_box,
};

enum class BindingKind : std::uint8_t {
    entity,
    configuration,
    open,
};

enum class InstanceSelection : std::uint8_t {
    labels,
    all,
    others,
};

enum class DisconnectionSelection : std::uint8_t {
    explicit_names,
    all,
    others,
};

enum class GenerateKind : std::uint8_t {
    block,
    conditional,
    iterative,
    selection,
};

enum class PredefinedAttribute : std::uint8_t {
    left,
    right,
    high,
    low,
    range,
    reverse_range,
    length,
    ascending,
    pos,
    val,
    succ,
    pred,
    leftof,
    rightof,
    image,
    value,
    // Appended for VHDL-2019 so every retained v3 numeric identity above is
    // stable across object/design serialization.
    index,
    designated_subtype,
    reflect,
};

enum class ExpressionKind : std::uint8_t {
    invalid,
    name,
    integer_literal,
    real_literal,
    boolean_literal,
    logic_literal,
    string_literal,
    unary,
    update,
    binary,
    call,
    index,
    slice,
    aggregate,
    concatenation,
    replication,
    default_choice,
    conditional,
};

enum class StatementKind : std::uint8_t {
    signal_assignment,
    variable_assignment,
    conditional,
    selection,
    loop,
    exit_loop,
    next_loop,
    return_statement,
    procedure_call,
    assertion,
    report,
    wait_statement,
    block,
    force,
    release,
    null_statement,
};

enum class DelayMechanism : std::uint8_t {
    implicit_inertial,
    inertial,
    transport,
};

/// A source-level name plus the semantic declarations visible for it.
/// `canonical` is lower-case for ordinary VHDL identifiers and preserves
/// extended/character-literal spelling.
struct Name {
    std::string spelling;
    std::string canonical;
    SourceSpanId source;
    std::optional<DeclarationId> selected;
    std::vector<DeclarationId> overloads;

    bool operator==(const Name&) const = default;
};

struct RangeConstraint {
    RangeKind kind { RangeKind::discrete };
    std::optional<std::int64_t> left;
    std::optional<std::int64_t> right;
    std::optional<ExpressionId> left_expression;
    std::optional<ExpressionId> right_expression;
    bool descending { };
    bool null { };
    SourceSpanId source;

    bool operator==(const RangeConstraint&) const = default;
};

struct AggregateAssociation {
    std::vector<ExpressionId> choices;
    std::string choice_spelling;
    ExpressionId value;
    SourceSpanId source;
};

struct Expression {
    ExpressionId id;
    ScopeId scope;
    ExpressionKind kind { ExpressionKind::invalid };
    std::string text;
    SourceSpanId source;
    OriginId origin;
    std::optional<Name> referenced_name;
    std::vector<ExpressionId> operands;
    std::vector<std::string> argument_names;
    std::vector<AggregateAssociation> associations;
    std::string nominal_type;
    std::optional<std::string> decoded_string;
    // VHDL-2019 unspecified interface types are inferred from associated
    // typed actuals. These identities are populated only when exactly one
    // callable profile yields one consistent type per implicit formal.
    std::vector<std::string> inferred_type_identities;
    bool unspecified_type_inference_unique { };
    ResidualDependencies dependencies;
    bool folded { };
};

struct DelayValue {
    std::uint64_t magnitude { };
    std::uint64_t divisor { 1 };
    std::string unit;
    std::optional<ExpressionId> expression;
    SourceSpanId source;
};

struct Delay {
    DelayValue primary;
    std::optional<DelayValue> minimum;
    std::optional<DelayValue> typical;
    std::optional<DelayValue> maximum;
    std::vector<Delay> additional;
};

struct DisconnectionSpecification {
    DisconnectionSelection selection {
        DisconnectionSelection::explicit_names
    };
    std::vector<Name> signals;
    Name type_mark;
    Delay delay;
    SourceSpanId source;
    OriginId origin;
};

struct WaveformElement {
    ExpressionId value;
    std::optional<Delay> delay;
    bool disconnect { };
    SourceSpanId source;
};

struct Sensitivity {
    std::string signal;
    std::optional<ExpressionId> expression;
    SourceSpanId source;
};

struct ProcedureAssociation {
    std::optional<Name> formal;
    ExpressionId actual;
    SourceSpanId source;
};

struct CaseAlternative {
    std::vector<ExpressionId> choices;
    std::vector<StatementId> statements;
    bool is_default { };
    SourceSpanId source;
};

struct Statement {
    StatementId id;
    ScopeId scope;
    StatementKind kind { StatementKind::null_statement };
    std::string label;
    SourceSpanId source;
    OriginId origin;
    std::optional<ExpressionId> target;
    std::optional<ExpressionId> value;
    std::optional<ExpressionId> condition;
    std::vector<Sensitivity> sensitivities;
    Name procedure;
    std::vector<ProcedureAssociation> procedure_arguments;
    std::string loop_variable;
    std::string loop_label;
    std::string loop_control_label;
    std::optional<ExpressionId> loop_initial;
    std::optional<ExpressionId> loop_limit;
    bool loop_descending { };
    std::optional<Delay> delay;
    std::optional<DelayMechanism> delay_mechanism;
    std::optional<Delay> rejection_limit;
    bool force_driving_value { };
    bool postponed { };
    bool conditional_assignment { };
    bool matching_case { };
    bool guarded_assignment { };
    std::optional<ExpressionId> guard;
    std::optional<Delay> disconnection_delay;
    std::vector<WaveformElement> waveform;
    bool unaffected { };
    std::optional<ExpressionId> report;
    std::optional<ExpressionId> severity;
    std::vector<StatementId> statements;
    std::vector<StatementId> else_statements;
    std::vector<CaseAlternative> alternatives;
    std::vector<DeclarationId> declarations;
    std::optional<ScopeId> nested_scope;
};

struct Process {
    ProcessId id;
    ScopeId scope;
    std::string name;
    bool postponed { };
    SourceSpanId source;
    OriginId origin;
    std::vector<DeclarationId> declarations;
    std::vector<Sensitivity> sensitivities;
    std::vector<StatementId> statements;
};

struct SubtypeIndication {
    TypeReference type_mark;
    ValueDomain domain { ValueDomain::unknown };
    std::optional<std::uint64_t> executable_width;
    Name resolution_function;
    std::optional<PredefinedAttribute> predefined_attribute;
    std::optional<ExpressionId> predefined_attribute_dimension;
    std::vector<RangeConstraint> constraints;
    bool signed_value { };
    bool unconstrained { };
    std::uint8_t integer_storage_width { };
    UnspecifiedTypeClass unspecified_class {
        UnspecifiedTypeClass::none
    };
    std::vector<UnspecifiedTypeClass> unspecified_component_classes;
    std::vector<std::string> unspecified_component_type_marks;
    std::size_t unspecified_array_index_count { };
    std::string unspecified_inference_identity;

    bool operator==(const SubtypeIndication&) const = default;
};

struct RecordElement {
    std::string name;
    SubtypeIndication subtype;
    SourceSpanId source;
};

struct ArrayDimension {
    Name index_subtype;
    std::optional<RangeConstraint> constraint;
    bool unconstrained { };
    SourceSpanId source;
};

struct EnumerationLiteral {
    DeclarationId declaration;
    std::string spelling;
    std::uint32_t ordinal { };
    SourceSpanId source;
};

struct PhysicalUnit {
    DeclarationId declaration;
    std::string name;
    std::optional<ExpressionId> scale;
    std::optional<std::int64_t> scale_factor;
    SourceSpanId source;
};

struct TypeDefinition {
    TypeId id;
    DeclarationId declaration;
    TypeForm form { TypeForm::unresolved };
    std::string name;
    SubtypeIndication base;
    std::vector<EnumerationLiteral> enumeration_literals;
    std::vector<ArrayDimension> array_dimensions;
    std::optional<SubtypeIndication> element_subtype;
    std::vector<RecordElement> record_elements;
    std::optional<SubtypeIndication> designated_subtype;
    std::uint32_t maximum_objects {
        std::numeric_limits<std::uint32_t>::max()
    };
    bool deallocate_releases_storage { true };
    bool reclaim_when_unreachable { };
    std::vector<DeclarationId> protected_members;
    std::vector<PhysicalUnit> physical_units;
    std::vector<PredefinedAttribute> attributes;
    std::optional<RangeConstraint> scalar_range;
    SourceSpanId source;
    OriginId origin;
};

struct Association {
    std::optional<Name> formal;
    AssociationKind kind { AssociationKind::expression };
    std::optional<ExpressionId> expression;
    std::optional<SubtypeIndication> type;
    SourceSpanId source;
};

struct Instance {
    InstanceId id;
    ScopeId scope;
    Name target;
    std::string name;
    std::vector<Association> generic_map;
    std::vector<Association> port_map;
    bool component { };
    bool configuration { };
    SourceSpanId source;
    OriginId origin;
};

struct CallableProfile {
    bool function { };
    bool pure { };
    bool defined { };
    std::optional<Name> default_callable;
    bool default_box { };
    std::optional<SubtypeIndication> return_type;
    std::optional<DeclarationId> return_identifier;
    std::vector<DeclarationId> formals;
};

struct ComponentProfile {
    std::vector<DeclarationId> generics;
    std::vector<DeclarationId> ports;
    std::optional<std::string> end_name;
    std::string owner_library;
    std::string owner_name;
    std::string scope_path;
};

struct PackageProfile {
    Name template_name;
    std::vector<Association> generic_map;
    bool generic_map_box { };
};

struct AttributeProfile {
    bool specification { };
    std::optional<SubtypeIndication> subtype;
    std::vector<Name> entity_names;
    std::string entity_class;
    std::optional<ExpressionId> value;
};

struct GroupProfile {
    bool template_declaration { };
    std::optional<Name> template_name;
    std::vector<Name> entries;
};

enum class ModeViewElementForm : std::uint8_t {
    direction,
    record_view,
    array_view,
};

enum class ModeViewCompositionState : std::uint8_t {
    uncomposed,
    complete,
    invalid,
    recursive,
};

struct ModeViewElement {
    Name element;
    ModeViewElementForm form { ModeViewElementForm::direction };
    Direction direction { Direction::unknown };
    std::optional<Name> referenced_view;
    std::optional<SubtypeIndication> subtype;
    std::vector<ModeViewElement> elements;
};

struct ModeViewProfile {
    SubtypeIndication record_subtype;
    std::vector<ModeViewElement> elements;
    std::optional<Name> converse_of;
    ModeViewCompositionState composition {
        ModeViewCompositionState::uncomposed
    };
};

struct ModeViewInterfaceProfile {
    ModeViewElementForm form { ModeViewElementForm::record_view };
    Name view;
    bool explicit_subtype { };
    std::vector<ModeViewElement> elements;
    ModeViewCompositionState composition {
        ModeViewCompositionState::uncomposed
    };
};

struct Declaration {
    DeclarationId id;
    ScopeId scope;
    DeclarationForm form { DeclarationForm::constant };
    std::string name;
    SourceSpanId source;
    OriginId origin;
    std::optional<TypeId> declared_type;
    std::optional<ValueId> declared_value;
    std::optional<SubtypeIndication> subtype;
    std::optional<SubtypeIndication> default_type;
    std::optional<ExpressionId> initializer;
    std::optional<ExpressionId> file_open_kind;
    ObjectClass object_class { ObjectClass::constant };
    Direction direction { Direction::unknown };
    bool shared { };
    bool local { };
    // Present for an alias declaration. Protected method aliases are untyped,
    // so the target is retained independently from the optional subtype.
    std::optional<Name> alias_target;
    std::optional<ScopeId> nested_scope;
    std::optional<CallableProfile> callable;
    std::optional<ComponentProfile> component;
    std::optional<PackageProfile> package;
    std::optional<AttributeProfile> attribute;
    std::optional<GroupProfile> group;
    std::optional<ModeViewProfile> mode_view;
    std::optional<ModeViewInterfaceProfile> interface_view;
    bool deferred { };
    std::optional<DeclarationId> completion;
    std::optional<SourceSpanId> completion_source;
    std::vector<DeclarationId> children;
    std::vector<StatementId> statements;
};

struct OverloadSet {
    ScopeId scope;
    std::string canonical_name;
    std::vector<DeclarationId> declarations;
};

struct ContextItem {
    ContextKind kind { ContextKind::library_clause };
    std::vector<Name> selected_names;
    SourceSpanId source;
};

struct BindingIndication {
    BindingKind kind { BindingKind::entity };
    Name entity;
    std::string architecture;
    Name configuration;
    std::vector<Association> generic_map;
    std::vector<Association> port_map;
    SourceSpanId source;
};

struct ComponentConfiguration {
    InstanceSelection selection { InstanceSelection::labels };
    std::vector<std::string> labels;
    Name component;
    BindingIndication binding;
    SourceSpanId source;
};

struct BlockConfiguration {
    Name block;
    std::optional<ExpressionId> generate_index;
    std::vector<ComponentConfiguration> components;
    std::vector<BlockConfiguration> blocks;
    SourceSpanId source;
};

struct GenerateRegion {
    DeclarationId declaration;
    ScopeId scope;
    GenerateKind kind { GenerateKind::conditional };
    std::string label;
    std::string alternative_label;
    std::string iterator;
    std::optional<ExpressionId> initial;
    std::optional<ExpressionId> condition;
    std::optional<ExpressionId> iteration;
    std::vector<Association> generic_map;
    std::vector<Association> port_map;
    std::vector<DeclarationId> declarations;
    std::vector<DisconnectionSpecification> disconnection_specifications;
    std::vector<InstanceId> instances;
    std::vector<ProcessId> processes;
    std::vector<StatementId> concurrent_statements;
    std::vector<GenerateRegion> nested;
    struct Choice {
        ExpressionId left;
        std::optional<ExpressionId> right;
        bool descending { };
        SourceSpanId source;
    };
    struct Alternative {
        ScopeId scope;
        std::string label;
        std::vector<Choice> choices;
        bool is_default { };
        SourceSpanId source;
    };
    std::vector<Alternative> alternatives;
    SourceSpanId source;
    OriginId origin;
    ResidualDependencies dependencies;
};

struct PslFormal {
    std::string name;
    std::vector<std::string> profile_tokens;
    PslExpressionClass expression_class { PslExpressionClass::invalid };
    std::optional<std::uint64_t> static_default;
    SourceSpanId source;
};

struct PslStaticRange {
    std::optional<std::uint64_t> minimum;
    std::optional<std::uint64_t> maximum;
    std::string minimum_formal;
    std::string maximum_formal;
    bool unbounded { };
    SourceSpanId source;
};

struct PslClock {
    std::vector<std::string> expression_tokens;
    std::string canonical_identity;
    bool explicit_override { };
    bool unknown_is_no_edge { true };
    SourceSpanId source;
};

struct PslTemporalOperator {
    PslTemporalOperatorKind kind {
        PslTemporalOperatorKind::sequence_concatenation
    };
    std::vector<std::string> left_tokens;
    std::vector<std::string> right_tokens;
    std::optional<PslStaticRange> range;
    SourceSpanId source;
};

struct PslReference {
    std::string name;
    PslExpressionClass expression_class { PslExpressionClass::invalid };
    SourceSpanId source;
};

struct PslAnalyzedExpression {
    PslExpressionClass expression_class { PslExpressionClass::invalid };
    std::vector<std::string> expression_tokens;
    std::vector<PslTemporalOperator> temporal_operators;
    std::vector<PslReference> references;
    std::vector<std::string> sampled_names;
    std::optional<PslClock> clock;
    PslUnknownPolicy unknown_policy { PslUnknownPolicy::false_value };
    SourceSpanId source;
};

struct PslDeclaration {
    PslDeclarationKind kind { PslDeclarationKind::boolean };
    std::string name;
    std::vector<PslFormal> formals;
    std::vector<std::string> body_tokens;
    std::optional<PslAnalyzedExpression> analyzed_expression;
    bool comment_embedded { };
    SourceSpanId source;
};

struct PslDirective {
    PslDirectiveKind kind { PslDirectiveKind::assert_directive };
    std::string label;
    std::vector<std::string> property_tokens;
    std::optional<PslAnalyzedExpression> analyzed_property;
    bool comment_embedded { };
    SourceSpanId source;
};

struct PslVerificationUnit {
    PslVerificationUnitKind kind { PslVerificationUnitKind::unit };
    std::vector<std::string> target_tokens;
    bool comment_embedded { };
};

struct PredefinedEnvironment {
    std::string identity;
    std::string working_library;
    std::vector<std::string> implicit_libraries;
    std::vector<std::string> implicit_packages;
    std::vector<std::string> declarations;
    std::vector<std::string> operator_profiles;
    std::vector<std::string> time_units;
    std::string default_time_unit;
    std::vector<std::string> attributes;
};

struct Unit {
    UnitId id;
    ScopeId scope;
    UnitKind kind { UnitKind::entity };
    std::string library;
    std::string name;
    bool extended_name { };
    std::string primary_name;
    bool extended_primary_name { };
    SourceSpanId source;
    OriginId origin;
    std::vector<ContextItem> context;
    std::optional<PslVerificationUnit> psl_verification_unit;
    std::vector<PslDeclaration> psl_declarations;
    std::vector<PslDirective> psl_directives;
    std::vector<DeclarationId> declarations;
    std::vector<DisconnectionSpecification> disconnection_specifications;
    std::vector<ProcessId> processes;
    std::vector<InstanceId> instances;
    std::vector<StatementId> concurrent_statements;
    std::vector<GenerateRegion> generates;
    std::vector<ComponentConfiguration> component_configurations;
    std::optional<BlockConfiguration> configuration;
    std::vector<std::string> source_dependencies;
    std::string compilation_unit_identity;
    std::string standard;
    std::string compatibility_profile;
    PredefinedEnvironment predefined_environment;
    std::string standard_package_revision;
    std::vector<std::string> standard_package_declarations;
    std::vector<std::string> standard_package_operator_profiles;
    // False when parsing retained a recovery node for syntax unavailable in
    // this unit's selected VHDL revision. Such HIR is inspectable and
    // serializable, but elaboration must not make it executable.
    bool profile_compatible { true };
};

/// Owning VHDL semantic HIR. It contains no frontend nodes, pointers, or
/// string views; every cross-record relationship uses a shared semantic ID.
class Hir final {
public:
    /// Changes whenever mutable access to an owning collection is requested.
    /// Consumers may use this to invalidate non-owning lookup acceleration;
    /// the revision is intentionally not part of the serialized HIR schema.
    [[nodiscard]] std::uint64_t revision() const noexcept
    {
        return revision_;
    }

    [[nodiscard]] const std::vector<Unit>& units() const noexcept;
    [[nodiscard]] const std::vector<Declaration>& declarations() const noexcept;
    [[nodiscard]] const std::vector<TypeDefinition>& types() const noexcept;
    [[nodiscard]] const std::vector<OverloadSet>& overload_sets() const noexcept;
    [[nodiscard]] const std::vector<Expression>& expressions() const noexcept;
    [[nodiscard]] const std::vector<Statement>& statements() const noexcept;
    [[nodiscard]] const std::vector<Process>& processes() const noexcept;
    [[nodiscard]] const std::vector<Instance>& instances() const noexcept;

    std::vector<Unit>& mutable_units() noexcept;
    std::vector<Declaration>& mutable_declarations() noexcept;
    std::vector<TypeDefinition>& mutable_types() noexcept;
    std::vector<OverloadSet>& mutable_overload_sets() noexcept;
    std::vector<Expression>& mutable_expressions() noexcept;
    std::vector<Statement>& mutable_statements() noexcept;
    std::vector<Process>& mutable_processes() noexcept;
    std::vector<Instance>& mutable_instances() noexcept;

private:
    void note_mutation() noexcept;

    std::vector<Unit> units_;
    std::vector<Declaration> declarations_;
    std::vector<TypeDefinition> types_;
    std::vector<OverloadSet> overload_sets_;
    std::vector<Expression> expressions_;
    std::vector<Statement> statements_;
    std::vector<Process> processes_;
    std::vector<Instance> instances_;
    std::uint64_t revision_ { };
};

/// The independently serialized top-level collections whose relationships
/// cannot be validated by checking individual ID ranges alone.
struct TopLevelHirView {
    std::span<const Declaration> declarations;
    std::span<const OverloadSet> overload_sets;
    std::span<const Instance> instances;
};

/// Validate instance occurrence IDs and overload-set identities and members.
/// This is shared by CompiledDesign and the standalone HIR codec.
[[nodiscard]] bool top_level_hir_collections_well_formed(
    const Model& semantics, TopLevelHirView view);

} // namespace fsim::semantic::vhdl
