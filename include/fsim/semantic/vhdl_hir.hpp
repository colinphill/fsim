// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/model.hpp"
#include "fsim/support/vhdl_identifier.hpp"

#include <algorithm>
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

    /// The canonical text, or the source spelling when there is none.
    [[nodiscard]] std::string_view text() const noexcept
    {
        return canonical.empty() ? std::string_view { spelling }
                                 : std::string_view { canonical };
    }

    bool operator==(const Name&) const = default;
};

/// Compiler-owned predefined vector identities whose package declarations
/// may be absent from a compiled-HIR bundle.
enum class BuiltinTypeIdentity : std::uint8_t {
    none,
    ieee_std_logic_1164_std_logic_vector,
    ieee_std_logic_1164_std_ulogic_vector,
    std_standard_string,
    std_standard_bit_vector,
    std_standard_boolean_vector,
    std_standard_integer_vector,
    std_standard_real_vector,
    std_standard_time_vector,
};

/// The simple name of a predefined array type, or empty for none.
[[nodiscard]] constexpr std::string_view builtin_type_name(
    const BuiltinTypeIdentity identity) noexcept
{
    switch (identity) {
    case BuiltinTypeIdentity::ieee_std_logic_1164_std_logic_vector:
        return "std_logic_vector";
    case BuiltinTypeIdentity::ieee_std_logic_1164_std_ulogic_vector:
        return "std_ulogic_vector";
    case BuiltinTypeIdentity::std_standard_string: return "string";
    case BuiltinTypeIdentity::std_standard_bit_vector: return "bit_vector";
    case BuiltinTypeIdentity::std_standard_boolean_vector:
        return "boolean_vector";
    case BuiltinTypeIdentity::std_standard_integer_vector:
        return "integer_vector";
    case BuiltinTypeIdentity::std_standard_real_vector: return "real_vector";
    case BuiltinTypeIdentity::std_standard_time_vector: return "time_vector";
    case BuiltinTypeIdentity::none: break;
    }
    return { };
}

/// Whether a predefined array type is declared in STD.STANDARD.
[[nodiscard]] constexpr bool is_standard_array_builtin(
    const BuiltinTypeIdentity identity) noexcept
{
    return identity >= BuiltinTypeIdentity::std_standard_string
        && identity <= BuiltinTypeIdentity::std_standard_time_vector;
}

/// The STD.STANDARD scalar element type of an integer, boolean, time, or
/// real vector, or empty for other types.
[[nodiscard]] constexpr std::string_view builtin_vector_element_name(
    const BuiltinTypeIdentity identity) noexcept
{
    switch (identity) {
    case BuiltinTypeIdentity::std_standard_integer_vector: return "integer";
    case BuiltinTypeIdentity::std_standard_boolean_vector: return "boolean";
    case BuiltinTypeIdentity::std_standard_time_vector: return "time";
    case BuiltinTypeIdentity::std_standard_real_vector: return "real";
    default: return { };
    }
}

/// The `library.package` declaring a predefined array type, or empty.
[[nodiscard]] constexpr std::string_view builtin_type_package(
    const BuiltinTypeIdentity identity) noexcept
{
    if (is_standard_array_builtin(identity)) {
        return "std.standard";
    }
    return identity == BuiltinTypeIdentity::none ? std::string_view { }
                                                 : "ieee.std_logic_1164";
}

/// Operator overload families with compiler-owned IEEE std_logic_1164
/// provenance. User declarations continue to use Name::selected/overloads.
enum class BuiltinOperatorIdentity : std::uint8_t {
    none,
    ieee_std_logic_1164_not,
    ieee_std_logic_1164_and,
    ieee_std_logic_1164_or,
    ieee_std_logic_1164_nand,
    ieee_std_logic_1164_nor,
    ieee_std_logic_1164_xor,
    ieee_std_logic_1164_xnor,
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
    // Direction is inherited from the referenced range (a range attribute
    // or subtype name used as a constraint). Endpoint order alone cannot
    // recover it, especially for singleton or null ranges.
    bool direction_from_bounds { };

    bool operator==(const RangeConstraint&) const = default;
};

struct AggregateAssociation {
    std::vector<ExpressionId> choices;
    std::string choice_spelling;
    ExpressionId value;
    SourceSpanId source;

    /// A positional element association has no choices.
    [[nodiscard]] bool positional() const noexcept
    {
        return choices.empty() && choice_spelling.empty();
    }
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
    BuiltinOperatorIdentity builtin_operator {
        BuiltinOperatorIdentity::none
    };
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

// An element constraint (IEEE 1076-2008 5.3.2.1, 5.3.3.1): the path walks
// array elements ("") and record elements (simple names) from the subtype.
struct ElementConstraint {
    std::vector<std::string> path;
    std::vector<RangeConstraint> constraints;

    bool operator==(const ElementConstraint&) const = default;
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
    BuiltinTypeIdentity builtin_type { BuiltinTypeIdentity::none };
    std::vector<ElementConstraint> element_constraints;

    bool operator==(const SubtypeIndication&) const = default;

    /// Whether any part of the subtype is still an unspecified generic
    /// (anonymous type) placeholder.
    [[nodiscard]] bool has_unspecified_parts() const noexcept
    {
        return unspecified_class != UnspecifiedTypeClass::none
            || !unspecified_component_classes.empty()
            || !unspecified_component_type_marks.empty()
            || unspecified_array_index_count != 0U
            || !unspecified_inference_identity.empty();
    }
};

// Applies `owner`'s element constraints addressed to `step` ("" for its
// array element, a record element's simple name otherwise) to `element`, the
// element's declared subtype.  Deeper constraints move onto the element.
[[nodiscard]] SubtypeIndication constrained_element_subtype(
    const SubtypeIndication& owner,
    std::string_view step,
    SubtypeIndication element);

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
    // Case-folded lexical elements of the specification (IEEE 1076-1993
    // 2.7 conformance); empty when unknown.
    std::string specification_lexemes;
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
    // A port of mode linkage (its direction is inout).
    bool linkage { };
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
    // A guarded signal's kind: 1 for bus, 2 for register (6.4.2.3).
    std::uint8_t guarded_kind { };
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

struct ComponentConfiguration;

struct BlockConfiguration {
    Name block;
    std::optional<ExpressionId> generate_index;
    std::vector<ComponentConfiguration> components;
    std::vector<BlockConfiguration> blocks;
    SourceSpanId source;
};

struct ComponentConfiguration {
    InstanceSelection selection { InstanceSelection::labels };
    std::vector<std::string> labels;
    Name component;
    BindingIndication binding;
    // A nested block configuration applies to the entity aspect selected by
    // this component configuration (IEEE 1076-2008 3.4.3).
    std::optional<BlockConfiguration> block_configuration;
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

/// Return the compiler-owned STD.STANDARD identity for an exact visible name
/// when the selected language profile exposes that declaration. Unqualified
/// names require implicit STD.STANDARD visibility; qualified names accept
/// only the exact STANDARD or STD.STANDARD package path. Callers must first
/// rule out a visible source declaration or an ambiguous lookup.
[[nodiscard]] inline BuiltinTypeIdentity
standard_array_builtin_type_identity(
    const std::string_view spelling,
    const PredefinedEnvironment& environment) noexcept
{
    constexpr auto standard_profile =
        std::string_view { "ieee-1076-standard:" };
    if (!environment.identity.starts_with(standard_profile)
        || spelling.empty() || spelling.find('@') != std::string_view::npos) {
        return BuiltinTypeIdentity::none;
    }
    const auto same_identifier = [](const std::string_view left,
                                    const std::string_view right) {
        return support::equal_ignoring_ascii_case(left, right);
    };
    const auto listed = [&](const std::span<const std::string> names,
                            const std::string_view name) {
        for (const auto& candidate : names) {
            if (same_identifier(candidate, name)) {
                return true;
            }
        }
        return false;
    };
    auto simple = spelling;
    const auto separator = spelling.find_last_of(".:");
    if (separator != std::string_view::npos) {
        const auto qualifier = spelling.substr(0U, separator);
        if (!same_identifier(qualifier, "standard")
            && !same_identifier(qualifier, "std.standard")
            && !same_identifier(qualifier, "std:standard")) {
            return BuiltinTypeIdentity::none;
        }
        simple = spelling.substr(separator + 1U);
    } else if (!listed(environment.implicit_packages, "std.standard.all")) {
        return BuiltinTypeIdentity::none;
    }
    if (same_identifier(simple, "string")
        && listed(environment.declarations, "string")) {
        return BuiltinTypeIdentity::std_standard_string;
    }
    if (same_identifier(simple, "bit_vector")
        && listed(environment.declarations, "bit_vector")) {
        return BuiltinTypeIdentity::std_standard_bit_vector;
    }
    if (same_identifier(simple, "boolean_vector")
        && listed(environment.declarations, "boolean_vector")) {
        return BuiltinTypeIdentity::std_standard_boolean_vector;
    }
    if (same_identifier(simple, "integer_vector")
        && listed(environment.declarations, "integer_vector")) {
        return BuiltinTypeIdentity::std_standard_integer_vector;
    }
    if (same_identifier(simple, "real_vector")
        && listed(environment.declarations, "real_vector")) {
        return BuiltinTypeIdentity::std_standard_real_vector;
    }
    if (same_identifier(simple, "time_vector")
        && listed(environment.declarations, "time_vector")) {
        return BuiltinTypeIdentity::std_standard_time_vector;
    }
    return BuiltinTypeIdentity::none;
}

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

/// Whether an aggregate has element associations that are all positional.
// A constant-class `in` interface object, whose value a call cannot
// change (IEEE 1076-2008 6.5.2).
[[nodiscard]] constexpr bool is_constant_input(
    const Declaration& declaration) noexcept
{
    return declaration.object_class == ObjectClass::constant
        && declaration.direction == Direction::input;
}

// A subprogram formal declared as a constant-class `in` port.
[[nodiscard]] constexpr bool is_constant_input_port(
    const Declaration& declaration) noexcept
{
    return declaration.form == DeclarationForm::port
        && is_constant_input(declaration);
}

// A signal, variable, constant, generic constant or port: a value object
// named directly by a simple name (IEEE 1076-2008 6.4.2).
[[nodiscard]] constexpr bool is_value_object_form(
    const DeclarationForm form) noexcept
{
    return form == DeclarationForm::signal
        || form == DeclarationForm::variable
        || form == DeclarationForm::constant
        || form == DeclarationForm::generic_constant
        || form == DeclarationForm::port;
}

// A generic constant, type, function, procedure or package: an interface
// declaration of a generic clause (IEEE 1076-2008 6.5.6.2).
[[nodiscard]] constexpr bool is_generic_formal(
    const DeclarationForm form) noexcept
{
    return form == DeclarationForm::generic_constant
        || form == DeclarationForm::generic_type
        || form == DeclarationForm::generic_function
        || form == DeclarationForm::generic_procedure
        || form == DeclarationForm::generic_package;
}

[[nodiscard]] inline bool positional_aggregate(
    const Expression& aggregate) noexcept
{
    return !aggregate.associations.empty()
        && std::ranges::all_of(aggregate.associations,
            &AggregateAssociation::positional);
}

/// Spelling prefix of a compiler-supplied (built-in) declaration name.
inline constexpr std::string_view builtin_prefix = "@builtin:";

/// Call text prefix of a qualified expression `T'(e)`; the call's single
/// operand is `e` and the text after the prefix names `T`.
inline constexpr std::string_view qualified_expression_prefix
    = "@vhdl-qualified:";

/// `spelling` without a leading builtin_prefix.
[[nodiscard]] constexpr std::string_view without_builtin_prefix(
    const std::string_view spelling) noexcept
{
    return spelling.starts_with(builtin_prefix)
        ? spelling.substr(builtin_prefix.size())
        : spelling;
}

/// Whether `unit` is a unit of the VHDL package `library.name` (basic
/// identifiers): the declaration or its body.
/// Whether an attribute designator is 'LEFT, 'RIGHT, 'LOW or 'HIGH.
[[nodiscard]] constexpr bool is_scalar_bound_attribute(
    const std::string_view attribute) noexcept
{
    return attribute == "'left" || attribute == "'right"
        || attribute == "'low" || attribute == "'high";
}

[[nodiscard]] inline bool is_package(const Unit* unit,
    const std::string_view library, const std::string_view name) noexcept
{
    return unit != nullptr && unit->kind == UnitKind::package
        && support::equal_ignoring_ascii_case(unit->library, library)
        && support::equal_ignoring_ascii_case(unit->name, name);
}

/// Whether a use-clause selected name of `part_count` parts starts with a
/// library: a three-part name always does; the first of two parts does when
/// it is WORK, STD, or a library named by one of the unit's library clauses
/// (IEEE 1076-2019 12.4).
[[nodiscard]] inline bool use_name_has_library_prefix(const Unit& unit,
    const std::size_t part_count, const std::string_view first)
{
    if (part_count >= 3U) {
        return true;
    }
    if (part_count != 2U) {
        return false;
    }
    if (support::same_vhdl_identifier(first, "work")
        || support::same_vhdl_identifier(first, "std")) {
        return true;
    }
    for (const auto& item : unit.context) {
        if (item.kind != ContextKind::library_clause) {
            continue;
        }
        for (const auto& name : item.selected_names) {
            if (support::same_vhdl_identifier(
                    name.canonical.empty() ? name.spelling : name.canonical,
                    first)) {
                return true;
            }
        }
    }
    return false;
}

/// Whether `spelling` (possibly an \extended\ identifier) names this unit.
[[nodiscard]] inline bool unit_name_matches(
    const Unit& unit, const std::string_view spelling)
{
    return support::vhdl_name_matches_spelling(
        unit.name, unit.extended_name, spelling);
}

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
    /// The declaration or type with `id`, or nullptr.
    [[nodiscard]] const Declaration* find_declaration(
        DeclarationId id) const noexcept;
    [[nodiscard]] const TypeDefinition* find_type(TypeId id) const noexcept;
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
