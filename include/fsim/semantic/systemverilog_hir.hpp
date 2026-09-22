// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/model.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fsim::semantic::sv {

enum class UnitKind : std::uint8_t {
    module,
    package,
    interface,
    program,
    configuration,
    bind,
    compilation_unit,
};

enum class DeclarationForm : std::uint8_t {
    parameter,
    local_parameter,
    type_parameter,
    typedef_declaration,
    port,
    net,
    variable,
    function,
    task,
    modport,
    enumeration_literal,
    generated,
    nettype_declaration,
};

enum class Direction : std::uint8_t {
    unknown,
    input,
    output,
    inout,
    ref,
};

enum class Lifetime : std::uint8_t {
    implicit,
    static_lifetime,
    automatic,
};

enum class TypeForm : std::uint8_t {
    unresolved,
    packed_integral,
    enumeration,
    packed_structure,
    packed_union,
    unpacked_structure,
    dynamic_array,
    queue,
    associative_array,
    static_array,
    string,
    alias,
    type_parameter,
    class_handle,
    tagged_union,
    unpacked_union,
};

enum class ModportMemberKind : std::uint8_t {
    signal,
    function_import,
    function_export,
    task_import,
    task_export,
    clocking,
};

enum class GenerateKind : std::uint8_t {
    block,
    conditional,
    iterative,
    selection,
};

enum class ExpressionKind : std::uint8_t {
    invalid,
    name,
    integer_literal,
    boolean_literal,
    logic_literal,
    string_literal,
    unary,
    update,
    binary,
    call,
    index,
    slice,
    assignment_pattern,
    concatenation,
    replication,
    default_choice,
    class_null,
    class_allocation,
    class_cast,
    class_property,
    class_static_property,
    class_method_call,
    class_static_method_call,
};

enum class ScalarKind : std::uint8_t {
    none,
    short_real,
    real,
    realtime,
    time,
    chandle,
};

enum class DecimalLiteralKind : std::uint8_t {
    real,
    time,
};

/// Exact source decimal metadata retained across the AST lifetime boundary.
/// The value is digits * 10^decimal_exponent. Time literals additionally
/// retain their source unit so specialization can scale them in the owning
/// compilation context without consulting parser storage.
struct DecimalLiteral {
    DecimalLiteralKind kind { DecimalLiteralKind::real };
    std::string digits;
    std::int64_t decimal_exponent { };
    std::string time_unit;
};

enum class AssignmentKind : std::uint8_t {
    blocking,
    nonblocking,
    continuous,
};

enum class StatementKind : std::uint8_t {
    assignment,
    force,
    release,
    conditional,
    selection,
    loop,
    break_loop,
    continue_loop,
    return_statement,
    task_call,
    assertion,
    delay_control,
    event_control,
    wait_statement,
    event_trigger,
    fork,
    wait_fork,
    disable_fork,
    disable,
    display,
    file_close,
    file_flush,
    file_display,
    memory_transfer,
    container_method,
    monitor_control,
    pause,
    finish,
    exit_program,
    block,
    null_statement,
    procedural_assign,
    deassign,
    wait_order,
    report,
};

enum class ProcessKind : std::uint8_t {
    always,
    always_ff,
    always_comb,
    always_latch,
    initial,
    final,
};

enum class ForkJoinKind : std::uint8_t { all,
    any,
    none };
enum class AssignmentControl : std::uint8_t { none,
    delay,
    event };
enum class UpdateKind : std::uint8_t { none,
    compound,
    prefix,
    postfix };
enum class CaseMatchKind : std::uint8_t {
    exact,
    wildcard_z,
    wildcard_xz,
    inside,
    matches,
};
enum class CaseQualifier : std::uint8_t {
    none,
    unique,
    unique0,
    priority,
};
enum class AssertionSeverity : std::uint8_t {
    note,
    warning,
    error,
    failure,
};
enum class OutputFormat : std::uint8_t {
    binary,
    hexadecimal,
    octal,
    decimal,
    character,
    string,
    real_scientific,
    real_fixed,
    real_general,
    hierarchy,
    time,
    unformatted2,
    unformatted4,
};

enum class GeneratedTextKind : std::uint8_t {
    none,
    systemverilog_file_macro,
    systemverilog_file_macro_derived,
};

[[nodiscard]] constexpr bool generated_text_kind_valid(
    const GeneratedTextKind kind) noexcept
{
    switch (kind) {
    case GeneratedTextKind::none:
    case GeneratedTextKind::systemverilog_file_macro:
    case GeneratedTextKind::systemverilog_file_macro_derived:
        return true;
    }
    return false;
}
enum class EdgeKind : std::uint8_t { any,
    positive,
    negative };

struct Name {
    std::string spelling;
    SourceSpanId source;
    std::optional<DeclarationId> selected;
    std::vector<DeclarationId> overloads;
};

struct AssignmentPatternAssociation {
    std::vector<ExpressionId> choices;
    std::string choice_spelling;
    ExpressionId value;
    SourceSpanId source;
};

struct CallAssociation {
    std::optional<std::string> formal;
    std::optional<ExpressionId> actual;
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
    std::vector<CallAssociation> call_arguments;
    std::vector<AssignmentPatternAssociation> associations;
    std::string nominal_type;
    std::string class_identity;
    std::string class_member_identity;
    bool class_checked { };
    bool signed_value { };
    EdgeKind clocking_edge { EdgeKind::any };
    std::optional<std::string> decoded_string;
    std::optional<DecimalLiteral> decimal_literal;
    ScalarKind scalar_kind { ScalarKind::none };
    ResidualDependencies dependencies;
    bool folded { };
    GeneratedTextKind generated_text { GeneratedTextKind::none };
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

struct Sensitivity {
    EdgeKind edge { EdgeKind::any };
    std::string signal;
    std::optional<ExpressionId> expression;
    SourceSpanId source;
};

struct TaskAssociation {
    std::optional<std::string> formal;
    std::optional<ExpressionId> actual;
    SourceSpanId source;
};

struct OutputValue {
    // Hierarchy (%m) and implicit simulation-time (%t) conversions do not
    // consume a source expression. Keep their exact position in the format
    // stream without manufacturing a placeholder expression identity.
    std::optional<ExpressionId> value;
    OutputFormat format { OutputFormat::decimal };
    std::string prefix;
    bool suppress_leading_zero { };
    std::uint32_t minimum_width { };
    bool left_justify { };
    bool zero_pad { };
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
    AssignmentKind assignment_kind { AssignmentKind::blocking };
    std::optional<ExpressionId> target;
    std::optional<ExpressionId> value;
    std::optional<ExpressionId> condition;
    Name task;
    std::vector<TaskAssociation> task_arguments;
    std::string loop_variable;
    bool loop_variable_declared { };
    std::optional<ExpressionId> loop_initial;
    std::optional<ExpressionId> loop_limit;
    std::optional<ExpressionId> loop_update_target;
    std::vector<StatementId> loop_updates;
    bool loop_descending { };
    bool loop_limit_exclusive { };
    bool loop_repeat { };
    bool loop_runtime { };
    bool loop_post_test { };
    ForkJoinKind fork_join { ForkJoinKind::all };
    AssignmentControl assignment_control { AssignmentControl::none };
    bool assignment_control_repeated { };
    UpdateKind update_kind { UpdateKind::none };
    std::string update_operator;
    std::optional<Delay> delay;
    // A ## control counts occurrences of the enclosing/default clocking
    // event. Keep the count expression separate from ordinary repeated
    // event controls so specialization and lowering never need syntax.
    bool clocking_cycle_delay { };
    std::optional<ExpressionId> clocking_cycle_count;
    std::optional<std::uint8_t> drive_zero;
    std::optional<std::uint8_t> drive_one;
    bool verilog_switch_driver { };
    bool verilog_switch_bidirectional { };
    bool verilog_switch_resistive { };
    std::optional<ExpressionId> verilog_switch_source;
    std::optional<ExpressionId> verilog_switch_control;
    bool verilog_switch_active_high { true };
    std::vector<Sensitivity> sensitivities;
    std::string assertion_message;
    AssertionSeverity assertion_severity { AssertionSeverity::error };
    bool assertion_has_pass_action { };
    bool assertion_has_failure_action { };
    std::string output_text;
    bool output_newline { true };
    bool output_postponed { };
    std::optional<OutputFormat> output_format;
    std::string output_prefix;
    std::string output_suffix;
    bool output_suppress_leading_zero { };
    std::uint32_t output_minimum_width { };
    bool output_left_justify { };
    bool output_zero_pad { };
    bool output_monitor { };
    bool monitor_enabled { };
    std::optional<ExpressionId> file_handle;
    bool memory_hex { };
    bool memory_write { };
    std::vector<OutputValue> output_values;
    std::string output_trailing_text;
    std::vector<StatementId> statements;
    std::vector<StatementId> else_statements;
    CaseMatchKind case_match { CaseMatchKind::exact };
    CaseQualifier case_qualifier { CaseQualifier::none };
    std::vector<CaseAlternative> case_alternatives;
    std::vector<DeclarationId> declarations;
    std::optional<ScopeId> nested_scope;
    bool class_handle_transfer { };
    std::string class_handle_type;
    GeneratedTextKind output_generated_text { GeneratedTextKind::none };
};

struct Process {
    ProcessId id;
    ScopeId scope;
    ProcessKind kind { ProcessKind::always };
    std::string name;
    SourceSpanId source;
    OriginId origin;
    std::vector<DeclarationId> declarations;
    std::vector<Sensitivity> sensitivities;
    std::vector<StatementId> statements;
    bool concurrent_assertion { };
};

struct PackedRange {
    std::optional<std::int64_t> left;
    std::optional<std::int64_t> right;
    std::optional<ExpressionId> left_expression;
    std::optional<ExpressionId> right_expression;
    bool descending { };
    SourceSpanId source;

    friend bool operator==(const PackedRange&, const PackedRange&) = default;
};

struct ActualAssociation;

struct TypeReference {
    semantic::TypeReference target;
    std::optional<TypeForm> value_form;
    std::string class_identity;
    bool virtual_interface { };
    std::string interface_type;
    std::string interface_modport;
    std::vector<ActualAssociation> interface_parameter_actuals;
    std::string systemverilog_net_type;
    std::string systemverilog_resolution_function;
    std::optional<PackedRange> packed_range;
    bool signed_value { };
    std::optional<TypeForm> container_form;
    std::optional<ExpressionId> queue_maximum;
    std::optional<semantic::TypeReference> associative_index;
    std::vector<PackedRange> unpacked_dimensions;
    std::vector<TypeReference> container_element_types;
    std::optional<std::uint64_t> executable_width;
    bool four_state { };

    friend bool operator==(
        const TypeReference&, const TypeReference&) = default;
};

enum class ActualKind : std::uint8_t {
    expression,
    type,
    default_value,
    open,
};

enum class UnconnectedDrive : std::uint8_t {
    none,
    pull_zero,
    pull_one,
};

struct ActualAssociation {
    std::optional<std::string> formal;
    ActualKind kind { ActualKind::expression };
    std::optional<ExpressionId> expression;
    std::optional<TypeReference> type;
    SourceSpanId source;

    friend bool operator==(
        const ActualAssociation&, const ActualAssociation&) = default;
};

struct Instance {
    InstanceId id;
    ScopeId scope;
    Name target;
    std::string name;
    bool anonymous { };
    bool udp { };
    std::vector<std::int64_t> array_indices;
    std::vector<ActualAssociation> parameters;
    std::vector<ActualAssociation> ports;
    std::optional<Delay> udp_delay;
    std::optional<std::uint8_t> drive_zero;
    std::optional<std::uint8_t> drive_one;
    UnconnectedDrive unconnected_drive { UnconnectedDrive::none };
    SourceSpanId source;
    OriginId origin;
};

enum class ClassVisibility : std::uint8_t {
    public_access,
    protected_access,
    local_access,
};

enum class ClassRandomKind : std::uint8_t {
    none,
    rand,
    randc,
};

enum class ClassLifetime : std::uint8_t {
    inherited,
    static_lifetime,
    automatic,
};

enum class ClassMethodKind : std::uint8_t {
    constructor,
    function,
    task,
};

/// Source-normalized expression forms admitted by the executable
/// SystemVerilog constraint subset. Name and type bindings remain empty
/// until specialization-aware constraint resolution.
enum class ConstraintExpressionKind : std::uint8_t {
    invalid,
    name,
    integer_literal,
    boolean_literal,
    logic_literal,
    string_literal,
    unary,
    binary,
    conditional,
    call,
    index,
    slice,
    concatenation,
    replication,
    assignment_pattern,
    inside_set,
    inside_range,
    distribution,
    distribution_item,
    soft,
    constraint_block,
    implication,
    conditional_constraint,
    foreach_constraint,
    solve_before,
    solve_list,
    unique_constraint,
};

enum class ConstraintReferenceKind : std::uint8_t {
    property,
    parameter,
    local_variable,
    method,
};

struct ConstraintBinding {
    ConstraintReferenceKind kind { ConstraintReferenceKind::property };
    std::string specialization_identity;
    std::string canonical_identity;
    TypeReference type;
    std::string constant_value;
};

struct ConstraintExpression {
    ConstraintExpressionKind kind { ConstraintExpressionKind::invalid };
    std::string text;
    std::string resolved_identity;
    SourceSpanId source;
    std::vector<ConstraintExpression> operands;
    std::vector<ConstraintBinding> bindings;
};

struct ClassParameter {
    DeclarationId declaration;
    std::string name;
    bool type_parameter { };
    std::optional<TypeReference> type;
    std::optional<TypeReference> default_type;
    std::optional<ExpressionId> default_value;
    SourceSpanId source;
    OriginId origin;
    ResidualDependencies dependencies;
};

struct ClassRelation {
    std::string name;
    std::string declaration_identity;
    std::vector<ActualAssociation> actuals;
    SourceSpanId source;
    OriginId origin;
    ResidualDependencies dependencies;
};

struct ClassProperty {
    DeclarationId declaration;
    std::string name;
    std::string canonical_identity;
    std::string owner_identity;
    TypeReference type;
    std::optional<ExpressionId> initializer;
    ClassVisibility visibility { ClassVisibility::public_access };
    ClassRandomKind random_kind { ClassRandomKind::none };
    bool static_storage { };
    bool constant { };
    bool parameter { };
    SourceSpanId source;
    OriginId origin;
    ResidualDependencies dependencies;
};

struct ClassMethod {
    DeclarationId declaration;
    std::string name;
    std::string canonical_identity;
    std::string owner_identity;
    std::string profile_identity;
    ClassMethodKind kind { ClassMethodKind::function };
    ClassVisibility visibility { ClassVisibility::public_access };
    ClassLifetime lifetime { ClassLifetime::inherited };
    bool static_method { };
    bool virtual_method { };
    bool pure { };
    bool final_method { };
    bool external { };
    bool out_of_block_definition { };
    bool defined { true };
    SourceSpanId source;
    OriginId origin;
    ResidualDependencies dependencies;
};

struct ClassConstraint {
    std::string name;
    std::string canonical_identity;
    std::string owner_identity;
    ClassVisibility visibility { ClassVisibility::public_access };
    std::vector<ConstraintExpression> expressions;
    bool static_constraint { };
    bool pure { };
    bool external { };
    bool defined { true };
    SourceSpanId source;
    OriginId origin;
};

struct ComposedClassConstraint {
    std::string name;
    std::string selected_identity;
    std::string overridden_identity;
    bool overrides { };
    bool override_legal { true };
    bool mode_enabled { true };
};

struct CovergroupDeclaration;

/// One flattened class declaration. Declared members retain their exact
/// owner while base_declaration_identity records the inherited ownership edge;
/// later composition can therefore walk base-to-derived without copying or
/// renaming source declarations.
struct ClassDeclaration {
    ScopeId scope;
    std::string name;
    std::string canonical_identity;
    /// Stable structural discriminator for declarations in mutually exclusive
    /// generate alternatives which intentionally share one logical scope.
    std::string alternative_discriminator;
    std::string enclosing_identity;
    std::string base_declaration_identity;
    std::vector<ClassParameter> parameters;
    std::optional<ClassRelation> base;
    std::vector<ClassRelation> extended_interfaces;
    std::vector<ClassRelation> implemented_interfaces;
    std::vector<DeclarationId> type_aliases;
    std::vector<ClassProperty> properties;
    std::vector<ClassMethod> methods;
    std::vector<ClassConstraint> constraints;
    std::vector<ComposedClassConstraint> composed_constraints;
    std::vector<CovergroupDeclaration> covergroups;
    std::vector<DeclarationId> member_declarations;
    std::vector<std::string> nested_class_identities;
    std::optional<DeclarationId> generate_owner;
    ClassLifetime lifetime { ClassLifetime::inherited };
    bool virtual_class { };
    bool interface_class { };
    bool forward_declaration { };
    std::optional<std::string> end_name;
    SourceSpanId source;
    OriginId origin;
    ResidualDependencies dependencies;
};

struct PackedMember {
    std::string name;
    TypeReference type;
    std::uint64_t lsb_offset { };
    SourceSpanId source;
    std::optional<ExpressionId> initializer;
};

struct ContainerType {
    TypeForm form { TypeForm::dynamic_array };
    std::optional<ExpressionId> queue_maximum;
    std::optional<TypeReference> associative_index;
    std::vector<PackedRange> static_dimensions;
    SourceSpanId source;
};

struct EnumerationLiteral {
    DeclarationId declaration;
    ValueId declared_value;
    std::string name;
    std::optional<ExpressionId> value;
    SourceSpanId source;
};

struct TypeDefinition {
    TypeId id;
    DeclarationId declaration;
    TypeForm form { TypeForm::unresolved };
    std::string name;
    TypeReference base;
    std::vector<PackedMember> members;
    std::vector<EnumerationLiteral> enumeration_literals;
    std::optional<ContainerType> container;
    std::string resolution_function;
    SourceSpanId source;
    OriginId origin;
};

struct CallableProfile {
    bool function { };
    TypeReference return_type;
    std::vector<DeclarationId> formals;
    Lifetime lifetime { Lifetime::implicit };
};

struct Declaration {
    DeclarationId id;
    ScopeId scope;
    DeclarationForm form { DeclarationForm::variable };
    std::string name;
    SourceSpanId source;
    OriginId origin;
    std::optional<TypeId> declared_type;
    std::optional<ValueId> declared_value;
    std::optional<TypeReference> type;
    std::optional<TypeReference> default_type;
    std::optional<ExpressionId> initializer;
    std::optional<Delay> delay;
    std::optional<std::uint8_t> drive_zero;
    std::optional<std::uint8_t> drive_one;
    std::optional<std::uint8_t> charge_strength;
    std::optional<Delay> charge_decay;
    Direction direction { Direction::unknown };
    bool const_reference { };
    bool static_reference { };
    std::string interface_type;
    std::string modport;
    Lifetime lifetime { Lifetime::implicit };
    std::optional<ScopeId> nested_scope;
    std::optional<CallableProfile> callable;
    std::vector<DeclarationId> children;
    std::vector<StatementId> statements;
};

struct Import {
    Name package;
    std::optional<Name> member;
    bool wildcard { };
    SourceSpanId source;
};

struct Export {
    Name package;
    std::optional<Name> member;
    bool wildcard { };
    SourceSpanId source;
};

struct Alias {
    std::vector<ExpressionId> terminals;
    SourceSpanId source;
    OriginId origin;
};

struct LetPort {
    std::string name;
    std::optional<TypeReference> type;
    std::optional<ExpressionId> default_value;
    SourceSpanId source;
};

struct LetDeclaration {
    std::string name;
    std::vector<LetPort> ports;
    ExpressionId expression;
    SourceSpanId source;
    OriginId origin;
};

struct ModportMember {
    ModportMemberKind kind { ModportMemberKind::signal };
    Name name;
    Direction direction { Direction::unknown };
    SourceSpanId source;
};

struct Modport {
    DeclarationId declaration;
    std::string name;
    std::vector<ModportMember> members;
    SourceSpanId source;
    OriginId origin;
};

struct GenerateChoice {
    ExpressionId left;
    std::optional<ExpressionId> right;
    bool descending { };
    SourceSpanId source;
};

struct GenerateAlternative {
    ScopeId scope;
    std::string label;
    std::string alternative_discriminator;
    std::vector<GenerateChoice> choices;
    std::vector<std::string> class_declarations;
    bool is_default { };
    SourceSpanId source;
};

struct DefparamPathSegment {
    std::string name;
    std::vector<ExpressionId> indices;
    SourceSpanId source;
};

struct Defparam {
    std::vector<DefparamPathSegment> path;
    ExpressionId value;
    SourceSpanId source;
    OriginId origin;
    ResidualDependencies dependencies;
};

struct GenerateRegion {
    DeclarationId declaration;
    ScopeId scope;
    GenerateKind kind { GenerateKind::conditional };
    std::string label;
    std::string alternative_label;
    std::string alternative_discriminator;
    std::string iterator;
    std::optional<ExpressionId> initial;
    std::optional<ExpressionId> condition;
    std::optional<ExpressionId> iteration;
    std::vector<Alias> aliases;
    std::vector<LetDeclaration> lets;
    std::vector<DeclarationId> declarations;
    std::vector<InstanceId> instances;
    std::vector<ProcessId> processes;
    std::vector<StatementId> concurrent_statements;
    std::vector<Defparam> defparams;
    std::vector<std::string> class_declarations;
    std::vector<GenerateAlternative> alternatives;
    std::vector<GenerateRegion> nested;
    SourceSpanId source;
    OriginId origin;
    ResidualDependencies dependencies;
};

struct BindDirective {
    Name target;
    std::vector<InstanceId> instances;
    SourceSpanId source;
    OriginId origin;
};

enum class ConfigurationRuleKind : std::uint8_t {
    instance,
    cell,
};

enum class ConfigurationSelectionKind : std::uint8_t {
    use,
    liblist,
};

struct ConfigurationDesign {
    std::string library;
    std::string cell;
    std::optional<UnitId> target;
    SourceSpanId source;
    OriginId origin;
};

struct ConfigurationRule {
    ConfigurationRuleKind kind { ConfigurationRuleKind::instance };
    ConfigurationSelectionKind selection {
        ConfigurationSelectionKind::use
    };
    std::string selector;
    std::string use_library;
    std::string use_cell;
    bool use_configuration { };
    std::vector<std::string> liblist;
    std::optional<UnitId> target;
    SourceSpanId source;
    OriginId origin;
};

struct ConfigurationDeclaration {
    std::vector<ConfigurationDesign> designs;
    std::vector<std::string> default_liblist;
    std::vector<ConfigurationRule> rules;
    SourceSpanId source;
    OriginId origin;
};

enum class SpecifyEdge : std::uint8_t {
    none,
    positive,
    negative,
    any,
};

enum class ModulePathKind : std::uint8_t {
    parallel,
    full,
};

enum class PathPolarity : std::uint8_t {
    none,
    positive,
    negative,
};

enum class PulseStyle : std::uint8_t {
    onevent,
    ondetect,
};

enum class TimingCheckKind : std::uint8_t {
    setup,
    hold,
    setup_hold,
    recovery,
    removal,
    recovery_removal,
    skew,
    time_skew,
    full_skew,
    period,
    width,
    no_change,
};

struct SpecparamDeclaration {
    std::string name;
    ExpressionId value;
    std::optional<ExpressionId> minimum;
    std::optional<ExpressionId> typical;
    std::optional<ExpressionId> maximum;
    bool path_pulse { };
    std::string path_pulse_input;
    std::string path_pulse_output;
    std::optional<ExpressionId> path_pulse_error_limit;
    std::optional<Delay> path_pulse_reject_delay;
    std::optional<Delay> path_pulse_error_delay;
    SourceSpanId source;
    OriginId origin;
};

struct ModulePathDeclaration {
    ModulePathKind kind { ModulePathKind::parallel };
    std::vector<ExpressionId> sources;
    std::vector<ExpressionId> destinations;
    SpecifyEdge source_edge { SpecifyEdge::none };
    PathPolarity polarity { PathPolarity::none };
    std::optional<ExpressionId> destination_data_source;
    std::optional<ExpressionId> condition;
    bool conditional { };
    bool ifnone { };
    std::vector<Delay> delays;
    SourceSpanId source;
    OriginId origin;
};

struct SpecifyPulseDeclaration {
    std::vector<ExpressionId> terminals;
    PulseStyle style { PulseStyle::onevent };
    bool controls_style { };
    bool show_cancelled { };
    SourceSpanId source;
    OriginId origin;
};

struct TimingCheckEvent {
    ExpressionId expression;
    SpecifyEdge edge { SpecifyEdge::none };
    std::vector<std::string> edge_descriptors;
    std::optional<ExpressionId> condition;
    SourceSpanId source;
    OriginId origin;
};

struct TimingCheckDeclaration {
    TimingCheckKind kind { TimingCheckKind::setup };
    TimingCheckEvent reference_event;
    std::optional<TimingCheckEvent> data_event;
    std::vector<ExpressionId> limits;
    std::vector<Delay> normalized_limits;
    std::optional<ExpressionId> threshold;
    std::optional<Delay> normalized_threshold;
    std::optional<ExpressionId> notifier;
    std::optional<ExpressionId> timestamp_condition;
    std::optional<ExpressionId> timecheck_condition;
    std::optional<ExpressionId> delayed_reference;
    std::optional<ExpressionId> delayed_data;
    std::optional<ExpressionId> event_based_flag;
    std::optional<ExpressionId> remain_active_flag;
    SourceSpanId source;
    OriginId origin;
};

struct TimingRecord {
    std::vector<SpecparamDeclaration> specparams;
    std::vector<ModulePathDeclaration> module_paths;
    std::vector<SpecifyPulseDeclaration> pulse_declarations;
    std::vector<TimingCheckDeclaration> timing_checks;
    SourceSpanId source;
    OriginId origin;
};

enum class UdpLevel : std::uint8_t {
    zero,
    one,
    unknown,
    dont_care,
    binary,
};

enum class UdpEdge : std::uint8_t {
    none,
    rising,
    falling,
    positive,
    negative,
    any,
    explicit_edge,
};

enum class UdpOutput : std::uint8_t {
    zero,
    one,
    unknown,
    no_change,
};

struct UdpInputPattern {
    UdpLevel level { UdpLevel::unknown };
    UdpEdge edge { UdpEdge::none };
    UdpLevel previous { UdpLevel::unknown };
    UdpLevel current { UdpLevel::unknown };
    SourceSpanId source;
};

struct UdpTableRow {
    std::vector<UdpInputPattern> inputs;
    std::optional<UdpLevel> current_state;
    UdpOutput output { UdpOutput::unknown };
    SourceSpanId source;
};

struct UdpDeclaration {
    Language language { Language::verilog };
    std::string standard;
    std::string compatibility_profile;
    std::string library;
    std::string name;
    std::string output;
    std::vector<std::string> inputs;
    bool sequential { };
    bool output_register { };
    std::optional<UdpOutput> initial_output;
    std::vector<UdpTableRow> rows;
    std::string time_unit;
    std::string time_precision;
    SourceSpanId source;
    OriginId origin;
};

/// Owning metadata budget for one compiled UDP table. This is a host resource
/// guard rather than an IEEE 1364 terminal- or row-count limit.
inline constexpr std::size_t maximum_udp_table_storage_bytes
    = 256U * 1024U * 1024U;

[[nodiscard]] bool udp_table_within_resource_budget(
    std::size_t input_count, std::size_t row_count) noexcept;

/// Validate a parser-independent UDP declaration before it crosses a
/// compiled-HIR serialization or linking boundary.
[[nodiscard]] bool udp_declaration_well_formed(
    const UdpDeclaration& declaration) noexcept;

struct SourceToken {
    std::uint16_t kind { };
    std::string text;
    SourceSpanId source;
    GeneratedTextKind generated_text { GeneratedTextKind::none };
};

enum class CovergroupOwnerKind : std::uint8_t {
    design_unit,
    class_declaration,
};

enum class CovergroupSamplingKind : std::uint8_t {
    event,
    with_function_sample,
};

enum class CovergroupOptionScope : std::uint8_t {
    instance,
    type,
};

enum class CoverageItemKind : std::uint8_t {
    coverpoint,
    cross,
};

enum class CoverageReferenceKind : std::uint8_t {
    constructor_formal,
    sample_formal,
    owner_object,
    coverpoint,
    qualified,
};

enum class CoverageBinKind : std::uint8_t {
    regular,
    ignore,
    illegal,
};

enum class CoverageBinSelection : std::uint8_t {
    explicit_selection,
    automatic,
    default_selection,
    default_sequence,
};

enum class CoverageTransitionRepetitionKind : std::uint8_t {
    none,
    consecutive,
    goto_repetition,
    nonconsecutive,
};

enum class CoverageScalarKind : std::uint8_t {
    none,
    short_real,
    real,
    realtime,
    time,
    chandle,
};

struct CovergroupFormal {
    Direction direction { Direction::unknown };
    bool const_reference { };
    std::vector<SourceToken> type_tokens;
    std::string name;
    SourceSpanId name_source;
    std::vector<SourceToken> default_tokens;
    SourceSpanId source;
};

struct CovergroupSampling {
    CovergroupSamplingKind kind { CovergroupSamplingKind::event };
    std::vector<SourceToken> tokens;
    std::vector<CovergroupFormal> formals;
    SourceSpanId source;
};

struct CovergroupOptionAssignment {
    CovergroupOptionScope scope { CovergroupOptionScope::instance };
    std::string name;
    SourceSpanId name_source;
    std::vector<SourceToken> value_tokens;
    std::optional<std::uint64_t> evaluated_value;
    std::optional<std::uint64_t> evaluated_real_bits;
    bool inherited { };
    SourceSpanId source;
};

struct CoverageReference {
    CoverageReferenceKind kind { CoverageReferenceKind::owner_object };
    Name target;
    std::vector<SourceToken> tokens;
    SourceSpanId source;
};

struct CoverageCrossOperand {
    Name target;
    std::vector<SourceToken> tokens;
    std::optional<std::uint64_t> resolved_declaration_index;
    bool implicit_coverpoint { };
    SourceSpanId source;
};

struct CoverageBinValue {
    std::vector<SourceToken> tokens;
    std::optional<std::int64_t> exact_value;
    std::optional<std::int64_t> range_left;
    std::optional<std::int64_t> range_right;
    std::uint64_t wildcard_value { };
    std::uint64_t wildcard_mask { };
    std::uint32_t width { };
    bool wildcard { };
    std::string exact_bits;
    std::string exact_unknown_bits;
    std::string range_left_bits;
    std::string range_right_bits;
    std::string wildcard_value_bits;
    std::string wildcard_mask_bits;
    bool exact_signed { };
    bool range_left_signed { };
    bool range_right_signed { };
    std::optional<std::uint64_t> exact_real_bits;
    std::optional<std::uint64_t> range_left_real_bits;
    std::optional<std::uint64_t> range_right_real_bits;
    bool range_left_inclusive { true };
    bool range_right_inclusive { true };
    SourceSpanId source;
};

struct CoverageTransitionRepetition {
    CoverageTransitionRepetitionKind kind {
        CoverageTransitionRepetitionKind::none
    };
    std::uint32_t minimum { 1 };
    std::optional<std::uint32_t> maximum { 1 };
    SourceSpanId source;
};

struct CoverageTransitionStep {
    std::vector<CoverageBinValue> values;
    CoverageTransitionRepetition repetition;
    SourceSpanId source;
};

struct CoverageTransitionDelay {
    std::uint32_t minimum { 1 };
    std::uint32_t maximum { 1 };
    SourceSpanId source;
};

struct CoverageTransitionSequence {
    std::vector<CoverageTransitionStep> steps;
    std::vector<CoverageTransitionDelay> delays;
    SourceSpanId source;
};

struct CoverageBin {
    CoverageBinKind kind { CoverageBinKind::regular };
    CoverageBinSelection selection {
        CoverageBinSelection::explicit_selection
    };
    std::string name;
    std::uint64_t declaration_index { };
    std::string source_name;
    std::optional<std::uint64_t> array_index;
    std::optional<std::uint64_t> declared_array_size;
    bool wildcard { };
    std::vector<CoverageBinValue> values;
    std::vector<CoverageTransitionSequence> transitions;
    std::vector<SourceToken> cross_selection_tokens;
    std::vector<SourceToken> with_tokens;
    SourceSpanId with_source;
    std::vector<SourceToken> iff_tokens;
    SourceSpanId iff_source;
    std::uint32_t weight { 1 };
    std::uint32_t goal { 100 };
    std::uint64_t at_least { 1 };
    SourceSpanId source;
    OriginId origin;
};

struct CoverageItem {
    CoverageItemKind kind { CoverageItemKind::coverpoint };
    std::string name;
    SourceSpanId name_source;
    bool explicit_name { };
    std::uint64_t declaration_index { };
    std::string origin_covergroup_identity;
    bool inherited { };
    CoverageScalarKind sampled_scalar_kind { CoverageScalarKind::none };
    std::optional<std::uint64_t> effective_real_interval_bits;
    std::vector<SourceToken> expression_tokens;
    SourceSpanId expression_source;
    std::vector<CoverageCrossOperand> cross_operands;
    std::vector<SourceToken> iff_tokens;
    SourceSpanId iff_source;
    std::vector<SourceToken> body_tokens;
    SourceSpanId body_source;
    std::vector<CovergroupOptionAssignment> option_assignments;
    std::uint32_t effective_weight { 1 };
    std::uint32_t effective_goal { 100 };
    std::uint64_t effective_at_least { 1 };
    std::vector<CoverageBin> bins;
    std::vector<CoverageReference> references;
    SourceSpanId source;
    OriginId origin;
};

struct CovergroupDeclaration {
    CovergroupOwnerKind owner_kind { CovergroupOwnerKind::design_unit };
    std::string standard;
    std::string name;
    bool extends_parent { };
    std::string resolved_base_identity;
    std::string owner_identity;
    std::string canonical_identity;
    std::string specialization_identity;
    std::string runtime_identity_prefix;
    SourceSpanId name_source;
    std::vector<SourceToken> header_tokens;
    SourceSpanId header_source;
    std::vector<CovergroupFormal> formals;
    std::optional<CovergroupSampling> sampling;
    std::vector<SourceToken> body_tokens;
    SourceSpanId body_source;
    std::vector<CovergroupOptionAssignment> option_assignments;
    std::uint32_t effective_instance_weight { 1 };
    std::uint32_t effective_instance_goal { 100 };
    std::uint32_t effective_type_weight { 1 };
    std::uint32_t effective_type_goal { 100 };
    bool effective_per_instance { };
    bool effective_merge_instances { };
    bool effective_cross_retain_auto_bins { true };
    std::optional<std::uint64_t> effective_real_interval_bits;
    std::vector<CoverageItem> items;
    std::optional<std::string> end_name;
    SourceSpanId end_name_source;
    SourceSpanId source;
    OriginId origin;
};

struct CovergroupSampleCall {
    std::string declaration_identity;
    std::string instance_identity;
    std::vector<ExpressionId> actuals;
    SourceSpanId source;
};

struct CoverageBinHit {
    std::uint64_t coverage_declaration_index { };
    std::uint64_t bin_declaration_index { };
    std::string identity;
    std::optional<std::int64_t> automatic_value;
    std::string automatic_value_bits;
    std::string automatic_unknown_bits;
    std::uint32_t automatic_width { 64 };
    bool automatic_signed { };
    std::uint64_t hit_count { };
    std::uint64_t at_least { 1 };
    bool covered { };
};

struct CoverageTransitionProgress {
    std::uint64_t coverage_declaration_index { };
    std::uint64_t bin_declaration_index { };
    std::uint64_t sequence_index { };
    std::uint64_t step_index { };
    std::uint32_t repetition_count { };
    std::uint32_t samples_since_step { };
};

struct CoveragePreviousSample {
    std::uint64_t coverage_declaration_index { };
    std::int64_t value { };
    std::uint64_t unknown_mask { };
    std::uint32_t width { 64 };
    std::string value_bits;
    std::string unknown_bits;
    bool signed_value { };
};

struct CoverageCrossBinState {
    std::uint64_t coverage_declaration_index { };
    std::optional<std::uint64_t> bin_declaration_index;
    std::string identity;
    std::vector<std::string> operand_bin_identities;
    std::uint64_t hit_count { };
    std::uint64_t exclusion_count { };
    std::uint32_t weight { 1 };
    std::uint32_t goal { 100 };
    std::uint64_t at_least { 1 };
    bool covered { };
    bool excluded { };
};

struct CoverageIllegalBinReport {
    std::string bin_identity;
    std::int64_t sampled_value { };
    std::uint64_t sampled_unknown_mask { };
    std::uint32_t sampled_width { 64 };
    std::string sampled_value_bits;
    std::string sampled_unknown_bits;
    bool sampled_signed { };
    CoverageScalarKind sampled_scalar_kind { CoverageScalarKind::none };
    std::uint64_t sampled_scalar_bits { };
    SourceSpanId source;
};

struct CovergroupInstance {
    std::string name;
    ScopeId owner_scope;
    std::string owner_identity;
    std::string declaration_identity;
    std::string specialization_identity;
    std::string runtime_identity;
    std::vector<ExpressionId> constructor_actuals;
    std::vector<CovergroupOptionAssignment> initial_option_state;
    std::vector<CovergroupSampleCall> sample_calls;
    std::vector<CoverageBinHit> bin_hits;
    std::vector<CoverageTransitionProgress> transition_progress;
    std::vector<CoveragePreviousSample> previous_samples;
    std::vector<CoverageCrossBinState> cross_bin_state;
    std::vector<CoverageIllegalBinReport> illegal_bin_reports;
    bool class_member_template { };
    bool cross_inventory_initialized { };
    SourceSpanId source;
    OriginId origin;
};

struct CompilationContext {
    std::string time_unit;
    std::string time_precision;
    std::string default_nettype;
    bool cell { };
};

enum class ConcurrentAssertionKind : std::uint8_t {
    assertion,
    assumption,
    cover,
    restriction,
};

enum class ConcurrentAssertionForm : std::uint8_t {
    property,
    sequence,
};

enum class AssertionRegion : std::uint8_t {
    preponed,
    observed,
    reactive,
};

struct AssertionObserverPolicy {
    bool callback_on_failure { true };
    bool debugger_visible { true };
    bool trace_visible { true };
    bool coverage_enabled { true };
};

struct ConcurrentAssertion {
    ConcurrentAssertionKind kind { ConcurrentAssertionKind::assertion };
    ConcurrentAssertionForm form { ConcurrentAssertionForm::property };
    std::string name;
    bool explicit_label { };
    std::vector<SourceToken> property_tokens;
    bool has_pass_action { };
    std::vector<SourceToken> pass_action_tokens;
    bool has_failure_action { };
    std::vector<SourceToken> failure_action_tokens;
    AssertionRegion sampling_region { AssertionRegion::preponed };
    AssertionRegion evaluation_region { AssertionRegion::observed };
    AssertionRegion action_region { AssertionRegion::reactive };
    AssertionObserverPolicy observers;
    std::uint32_t coverage_slot { };
    SourceSpanId source;
    std::optional<SourceSpanId> label_source;
    std::optional<SourceSpanId> pass_action_source;
    std::optional<SourceSpanId> failure_action_source;
    OriginId origin;
};

enum class DesignProcessRegion : std::uint8_t {
    active,
    reactive,
};

struct DesignSchedulingDeclaration {
    DesignProcessRegion process_region { DesignProcessRegion::active };
    bool prototype { };
    SourceSpanId source;
};

struct StandardPackageProvenance {
    std::string revision;
    std::string declaration_identity;
};

enum class DpiDirection : std::uint8_t {
    import,
    export_declaration,
};

enum class DpiOwnerKind : std::uint8_t {
    compilation_unit,
    design_unit,
};

enum class DpiQualifier : std::uint8_t {
    none,
    pure,
    context,
};

enum class DpiCallableKind : std::uint8_t {
    function,
    task,
};

struct DpiFormal {
    Direction direction { Direction::input };
    bool const_reference { };
    std::vector<SourceToken> type_tokens;
    std::string name;
    std::vector<SourceToken> dimension_tokens;
    std::vector<SourceToken> default_tokens;
    std::vector<SourceToken> tokens;
    SourceSpanId source;
};

struct DpiResolvedFormal {
    Direction direction { Direction::input };
    TypeReference type;
    std::string name;
    bool reference { };
    SourceSpanId source;
};

struct DpiResolvedProfile {
    std::optional<TypeReference> return_type;
    std::vector<DpiResolvedFormal> formals;
    SourceSpanId callable_source;
};

struct DpiDeclaration {
    std::string standard;
    DpiDirection direction { DpiDirection::import };
    DpiOwnerKind owner_kind { DpiOwnerKind::compilation_unit };
    ScopeId owner_scope;
    std::string owner_identity;
    std::string link_name;
    DpiQualifier qualifier { DpiQualifier::none };
    DpiCallableKind callable_kind { DpiCallableKind::function };
    std::string systemverilog_name;
    std::optional<std::string> c_identifier;
    std::vector<SourceToken> return_type_tokens;
    std::vector<SourceToken> formal_tokens;
    std::vector<DpiFormal> formals;
    std::vector<SourceToken> profile_tokens;
    SourceSpanId profile_source;
    std::string linkage_name;
    bool validated { };
    std::optional<DpiResolvedProfile> resolved_profile;
    std::vector<SourceToken> tokens;
    SourceSpanId source;
    OriginId origin;
};

struct ClockingSkew {
    EdgeKind edge { EdgeKind::any };
    std::optional<Delay> delay;
    bool one_step { };
    SourceSpanId source;
};

struct ClockingSignal {
    Name name;
    Direction direction { Direction::unknown };
    std::optional<ClockingSkew> skew;
    std::optional<ExpressionId> expression;
    SourceSpanId source;
};

struct ClockingBlock {
    std::string name;
    std::vector<Sensitivity> event;
    std::optional<ClockingSkew> default_input_skew;
    std::optional<ClockingSkew> default_output_skew;
    std::vector<ClockingSignal> signals;
    SourceSpanId source;
    OriginId origin;
};

struct DefaultClockingReference {
    Name block;
    SourceSpanId source;
    OriginId origin;
};

enum class AssertionDeclarationKind : std::uint8_t {
    sequence,
    property,
    checker,
};

enum class AssertionFormalKind : std::uint8_t {
    value,
    sequence,
    property,
    untyped,
};

struct AssertionFormal {
    AssertionFormalKind kind { AssertionFormalKind::untyped };
    Direction direction { Direction::unknown };
    bool local { };
    std::vector<SourceToken> type_tokens;
    std::string name;
    SourceSpanId name_source;
    std::vector<SourceToken> default_tokens;
    SourceSpanId source;
};

struct AssertionLocalVariable {
    std::vector<SourceToken> type_tokens;
    std::string name;
    SourceSpanId name_source;
    std::vector<SourceToken> declarator_tokens;
    std::vector<SourceToken> initializer_tokens;
    SourceSpanId source;
};

struct AssertionClock {
    std::vector<SourceToken> event_tokens;
    SourceSpanId source;
};

struct AssertionDisable {
    std::vector<SourceToken> condition_tokens;
    SourceSpanId source;
};

enum class AssertionReferenceKind : std::uint8_t {
    formal,
    local_variable,
    design_unit_object,
    assertion_declaration,
    hierarchical,
    package,
};

struct AssertionReference {
    AssertionReferenceKind kind { AssertionReferenceKind::design_unit_object };
    Name target;
    std::vector<std::string> path;
    std::vector<SourceToken> tokens;
    SourceSpanId source;
};

enum class SequenceRepetitionKind : std::uint8_t {
    none,
    consecutive,
    nonconsecutive,
    goto_repetition,
};

struct SequenceRange {
    std::vector<SourceToken> minimum_tokens;
    std::vector<SourceToken> maximum_tokens;
    SourceSpanId source;
};

struct SequenceElement {
    std::vector<SourceToken> expression_tokens;
    SequenceRepetitionKind repetition { SequenceRepetitionKind::none };
    std::optional<SequenceRange> repetition_range;
    SourceSpanId source;
};

struct SequenceDelay {
    SequenceRange range;
    bool fusion { };
    SourceSpanId source;
};

struct SequenceIntersectionOperand {
    std::vector<SourceToken> tokens;
    SourceSpanId source;
};

enum class SequenceBinaryKind : std::uint8_t {
    throughout,
    within,
};

struct SequenceBinaryOperation {
    SequenceBinaryKind kind { SequenceBinaryKind::throughout };
    std::vector<SourceToken> left_tokens;
    std::vector<SourceToken> right_tokens;
    SourceSpanId source;
};

struct SequenceFirstMatch {
    std::vector<SourceToken> sequence_tokens;
    std::vector<SourceToken> match_item_tokens;
    SourceSpanId source;
};

enum class SequenceEndpointKind : std::uint8_t {
    matched,
    triggered,
};

struct SequenceEndpoint {
    SequenceEndpointKind kind { SequenceEndpointKind::matched };
    Name receiver;
    std::vector<SourceToken> receiver_tokens;
    bool method_parentheses { };
    SourceSpanId source;
};

struct SequenceExpression {
    std::vector<SequenceElement> elements;
    std::vector<SequenceDelay> delays;
    std::vector<SequenceIntersectionOperand> intersection_operands;
    std::vector<SequenceBinaryOperation> binary_operations;
    std::vector<SequenceFirstMatch> first_matches;
    SourceSpanId source;
};

enum class PropertyImplicationKind : std::uint8_t {
    overlapped,
    nonoverlapped,
};

struct PropertyImplication {
    PropertyImplicationKind kind { PropertyImplicationKind::overlapped };
    std::vector<SourceToken> antecedent_tokens;
    std::vector<SourceToken> consequent_tokens;
    SourceSpanId source;
};

enum class PropertyUntilKind : std::uint8_t {
    until,
    strong_until,
    until_with,
    strong_until_with,
};

struct PropertyUntilOperation {
    PropertyUntilKind kind { PropertyUntilKind::until };
    std::vector<SourceToken> left_tokens;
    std::vector<SourceToken> right_tokens;
    SourceSpanId source;
};

enum class PropertyNexttimeKind : std::uint8_t {
    nexttime,
    strong_nexttime,
};

struct PropertyNexttime {
    PropertyNexttimeKind kind { PropertyNexttimeKind::nexttime };
    std::vector<SourceToken> count_tokens;
    std::vector<SourceToken> operand_tokens;
    SourceSpanId source;
};

enum class PropertyRecurrenceKind : std::uint8_t {
    always,
    strong_always,
    eventually,
    strong_eventually,
};

struct PropertyRecurrence {
    PropertyRecurrenceKind kind { PropertyRecurrenceKind::always };
    std::optional<SequenceRange> range;
    std::vector<SourceToken> operand_tokens;
    SourceSpanId source;
};

enum class PropertySequenceStrengthKind : std::uint8_t {
    strong,
    weak,
};

struct PropertySequenceStrength {
    PropertySequenceStrengthKind kind {
        PropertySequenceStrengthKind::strong
    };
    std::vector<SourceToken> sequence_tokens;
    SourceSpanId source;
};

enum class PropertyAbortOutcome : std::uint8_t {
    vacuous_success,
    failure,
};

struct PropertyAbort {
    PropertyAbortOutcome outcome { PropertyAbortOutcome::vacuous_success };
    bool synchronous { };
    std::vector<SourceToken> condition_tokens;
    std::vector<SourceToken> property_tokens;
    SourceSpanId source;
};

struct PropertyExpression {
    std::vector<PropertyImplication> implications;
    std::vector<SequenceDelay> delays;
    std::vector<PropertyUntilOperation> until_operations;
    std::vector<PropertyNexttime> nexttimes;
    std::vector<PropertyRecurrence> recurrences;
    std::vector<PropertySequenceStrength> sequence_strengths;
    std::vector<PropertyAbort> aborts;
    SourceSpanId source;
};

struct ConcurrentAssertion;

struct AssertionDeclaration {
    AssertionDeclarationKind kind { AssertionDeclarationKind::sequence };
    std::string name;
    SourceSpanId name_source;
    std::vector<SourceToken> header_tokens;
    SourceSpanId header_source;
    std::vector<AssertionFormal> formals;
    std::vector<SourceToken> body_tokens;
    SourceSpanId body_source;
    std::vector<AssertionLocalVariable> local_variables;
    std::optional<AssertionClock> clock;
    std::optional<AssertionDisable> disable;
    std::vector<SourceToken> expression_tokens;
    SourceSpanId expression_source;
    std::vector<AssertionReference> references;
    std::optional<SequenceExpression> sequence_expression;
    std::optional<PropertyExpression> property_expression;
    std::vector<SequenceEndpoint> sequence_endpoints;
    std::vector<AssertionDeclaration> checker_declarations;
    std::vector<ConcurrentAssertion> checker_assertions;
    SourceSpanId source;
    OriginId origin;
};

struct CheckerConnection {
    std::string formal_name;
    std::vector<SourceToken> actual_tokens;
    bool open { };
    SourceSpanId source;
};

struct CheckerInstance {
    Name declaration;
    std::string name;
    std::vector<CheckerConnection> connections;
    SourceSpanId source;
    OriginId origin;
};

struct Unit {
    UnitId id;
    ScopeId scope;
    UnitKind kind { UnitKind::module };
    // True for an extern module, interface, or program prototype. This is
    // unit-selection metadata and must remain available after syntax storage
    // has been destroyed.
    bool external { };
    std::string library;
    std::string name;
    SourceSpanId source;
    OriginId origin;
    CompilationContext compilation;
    std::vector<Import> imports;
    std::vector<Export> exports;
    std::vector<Alias> aliases;
    std::vector<LetDeclaration> lets;
    std::vector<DeclarationId> declarations;
    std::vector<Modport> modports;
    std::vector<InstanceId> instances;
    std::vector<ProcessId> processes;
    std::vector<StatementId> concurrent_statements;
    std::vector<ConcurrentAssertion> concurrent_assertions;
    std::vector<GenerateRegion> generates;
    std::vector<Defparam> defparams;
    std::vector<BindDirective> binds;
    std::optional<ConfigurationDeclaration> configuration;
    std::vector<TimingRecord> timing;
    std::vector<CovergroupDeclaration> coverage;
    std::vector<std::string> source_dependencies;
    std::string compilation_unit_identity;
    std::string standard;
    std::string compatibility_profile;
    std::optional<DesignSchedulingDeclaration> scheduling_declaration;
    std::optional<StandardPackageProvenance> standard_package;
    std::vector<DpiDeclaration> dpi_declarations;
    std::vector<ClockingBlock> clocking_blocks;
    std::optional<DefaultClockingReference> default_clocking;
    std::vector<AssertionDeclaration> assertion_declarations;
    std::vector<CheckerInstance> checker_instances;
};

/// Owning SystemVerilog semantic HIR, including opaque class-handle operations.
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
    [[nodiscard]] const std::vector<Expression>& expressions() const noexcept;
    [[nodiscard]] const std::vector<Statement>& statements() const noexcept;
    [[nodiscard]] const std::vector<Process>& processes() const noexcept;
    [[nodiscard]] const std::vector<ClassDeclaration>& classes() const noexcept;
    [[nodiscard]] const std::vector<Instance>& instances() const noexcept;
    [[nodiscard]] const std::vector<UdpDeclaration>& udps() const noexcept;
    [[nodiscard]] const std::vector<DpiDeclaration>&
    dpi_declarations() const noexcept;
    [[nodiscard]] const std::vector<CovergroupInstance>&
    covergroup_instances() const noexcept;

    std::vector<Unit>& mutable_units() noexcept;
    std::vector<Declaration>& mutable_declarations() noexcept;
    std::vector<TypeDefinition>& mutable_types() noexcept;
    std::vector<Expression>& mutable_expressions() noexcept;
    std::vector<Statement>& mutable_statements() noexcept;
    std::vector<Process>& mutable_processes() noexcept;
    std::vector<ClassDeclaration>& mutable_classes() noexcept;
    std::vector<Instance>& mutable_instances() noexcept;
    std::vector<UdpDeclaration>& mutable_udps() noexcept;
    std::vector<DpiDeclaration>& mutable_dpi_declarations() noexcept;
    std::vector<CovergroupInstance>& mutable_covergroup_instances() noexcept;

private:
    void note_mutation() noexcept;

    std::vector<Unit> units_;
    std::vector<Declaration> declarations_;
    std::vector<TypeDefinition> types_;
    std::vector<Expression> expressions_;
    std::vector<Statement> statements_;
    std::vector<Process> processes_;
    std::vector<ClassDeclaration> classes_;
    std::vector<Instance> instances_;
    std::vector<UdpDeclaration> udps_;
    std::vector<DpiDeclaration> dpi_declarations_;
    std::vector<CovergroupInstance> covergroup_instances_;
    std::uint64_t revision_ { };
};

/// The independently serialized top-level collections whose logical
/// identities cannot be validated by checking ID ranges alone.
struct TopLevelHirView {
    std::span<const Instance> instances;
    std::span<const UdpDeclaration> udps;
    std::span<const DpiDeclaration> dpi_declarations;
    std::span<const CovergroupInstance> covergroup_instances;
};

/// Validate top-level occurrence IDs and parser-independent logical
/// identities. This is shared by CompiledDesign and the standalone HIR codec
/// so neither persistence boundary accepts a weaker collection model.
[[nodiscard]] bool top_level_hir_collections_well_formed(
    const Model& semantics, TopLevelHirView view);

[[nodiscard]] std::string class_declaration_identity(
    const ClassDeclaration& declaration);
[[nodiscard]] std::string class_declaration_identity(
    std::string_view canonical_identity,
    std::string_view alternative_discriminator);

/// Returns an empty string for a structurally consistent class graph. This is
/// shared by decoded-bundle validation and compiled-design linking so neither
/// boundary accepts a weaker class ownership model.
[[nodiscard]] std::string class_hir_error(const Model& semantics,
    std::span<const Unit> units,
    std::span<const Declaration> declarations,
    std::span<const Statement> statements,
    std::span<const ClassDeclaration> classes);

} // namespace fsim::semantic::sv
