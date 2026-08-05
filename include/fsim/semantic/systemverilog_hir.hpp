// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/model.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fsim::semantic::sv {

enum class UnitKind : std::uint8_t {
    module,
    package,
    interface,
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
};

enum class ModportMemberKind : std::uint8_t {
    signal,
    function_import,
    function_export,
    task_import,
    task_export,
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
    display,
    file_close,
    file_flush,
    file_display,
    memory_transfer,
    container_method,
    monitor_control,
    pause,
    finish,
    block,
    null_statement,
};

enum class ProcessKind : std::uint8_t {
    always,
    always_ff,
    always_comb,
    always_latch,
    initial,
    final,
};

enum class ForkJoinKind : std::uint8_t { all, any, none };
enum class AssignmentControl : std::uint8_t { none, delay, event };
enum class UpdateKind : std::uint8_t { none, compound, prefix, postfix };
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
    hierarchy,
    time,
};
enum class EdgeKind : std::uint8_t { any, positive, negative };

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
    ExpressionKind kind{ExpressionKind::invalid};
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
    bool class_checked{};
    std::optional<std::string> decoded_string;
};

struct DelayValue {
    std::uint64_t magnitude{};
    std::uint64_t divisor{1};
    std::string unit;
    std::optional<ExpressionId> expression;
    SourceSpanId source;
};

struct Delay {
    DelayValue primary;
    std::optional<DelayValue> minimum;
    std::optional<DelayValue> typical;
    std::optional<DelayValue> maximum;
    std::vector<DelayValue> additional;
};

struct Sensitivity {
    EdgeKind edge{EdgeKind::any};
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
    ExpressionId value;
    OutputFormat format{OutputFormat::decimal};
    std::string prefix;
    bool suppress_leading_zero{};
    std::uint32_t minimum_width{};
    bool left_justify{};
    bool zero_pad{};
};

struct CaseAlternative {
    std::vector<ExpressionId> choices;
    std::vector<StatementId> statements;
    bool is_default{};
    SourceSpanId source;
};

struct Statement {
    StatementId id;
    ScopeId scope;
    StatementKind kind{StatementKind::null_statement};
    std::string label;
    SourceSpanId source;
    OriginId origin;
    AssignmentKind assignment_kind{AssignmentKind::blocking};
    std::optional<ExpressionId> target;
    std::optional<ExpressionId> value;
    std::optional<ExpressionId> condition;
    Name task;
    std::vector<TaskAssociation> task_arguments;
    std::string loop_variable;
    bool loop_variable_declared{};
    std::optional<ExpressionId> loop_initial;
    std::optional<ExpressionId> loop_limit;
    bool loop_descending{};
    bool loop_limit_exclusive{};
    bool loop_repeat{};
    bool loop_runtime{};
    bool loop_post_test{};
    ForkJoinKind fork_join{ForkJoinKind::all};
    AssignmentControl assignment_control{AssignmentControl::none};
    bool assignment_control_repeated{};
    UpdateKind update_kind{UpdateKind::none};
    std::string update_operator;
    std::optional<Delay> delay;
    std::vector<Sensitivity> sensitivities;
    std::string assertion_message;
    AssertionSeverity assertion_severity{AssertionSeverity::error};
    bool assertion_has_pass_action{};
    bool assertion_has_failure_action{};
    std::string output_text;
    bool output_newline{true};
    bool output_postponed{};
    std::optional<OutputFormat> output_format;
    std::string output_prefix;
    std::string output_suffix;
    bool output_suppress_leading_zero{};
    std::uint32_t output_minimum_width{};
    bool output_left_justify{};
    bool output_zero_pad{};
    bool output_monitor{};
    bool monitor_enabled{};
    std::optional<ExpressionId> file_handle;
    bool memory_hex{};
    bool memory_write{};
    std::vector<OutputValue> output_values;
    std::string output_trailing_text;
    std::vector<StatementId> statements;
    std::vector<StatementId> else_statements;
    CaseMatchKind case_match{CaseMatchKind::exact};
    CaseQualifier case_qualifier{CaseQualifier::none};
    std::vector<CaseAlternative> case_alternatives;
    std::vector<DeclarationId> declarations;
    std::optional<ScopeId> nested_scope;
    bool class_handle_transfer{};
    std::string class_handle_type;
};

struct Process {
    ProcessId id;
    ScopeId scope;
    ProcessKind kind{ProcessKind::always};
    std::string name;
    SourceSpanId source;
    OriginId origin;
    std::vector<DeclarationId> declarations;
    std::vector<Sensitivity> sensitivities;
    std::vector<StatementId> statements;
};

struct PackedRange {
    std::optional<std::int64_t> left;
    std::optional<std::int64_t> right;
    std::optional<ExpressionId> left_expression;
    std::optional<ExpressionId> right_expression;
    bool descending{};
    SourceSpanId source;
};

struct TypeReference {
    semantic::TypeReference target;
    std::optional<TypeForm> value_form;
    std::string class_identity;
    std::optional<PackedRange> packed_range;
    bool signed_value{};
    std::optional<TypeForm> container_form;
    std::optional<ExpressionId> queue_maximum;
    std::optional<semantic::TypeReference> associative_index;
    std::vector<PackedRange> unpacked_dimensions;
};

struct PackedMember {
    std::string name;
    TypeReference type;
    std::uint64_t lsb_offset{};
    SourceSpanId source;
};

struct ContainerType {
    TypeForm form{TypeForm::dynamic_array};
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
    TypeForm form{TypeForm::unresolved};
    std::string name;
    TypeReference base;
    std::vector<PackedMember> members;
    std::vector<EnumerationLiteral> enumeration_literals;
    std::optional<ContainerType> container;
    SourceSpanId source;
    OriginId origin;
};

struct CallableProfile {
    bool function{};
    TypeReference return_type;
    std::vector<DeclarationId> formals;
    Lifetime lifetime{Lifetime::implicit};
};

struct Declaration {
    DeclarationId id;
    ScopeId scope;
    DeclarationForm form{DeclarationForm::variable};
    std::string name;
    SourceSpanId source;
    OriginId origin;
    std::optional<TypeId> declared_type;
    std::optional<ValueId> declared_value;
    std::optional<TypeReference> type;
    std::optional<TypeReference> default_type;
    std::optional<ExpressionId> initializer;
    std::optional<Delay> delay;
    Direction direction{Direction::unknown};
    std::string interface_type;
    std::string modport;
    Lifetime lifetime{Lifetime::implicit};
    std::optional<ScopeId> nested_scope;
    std::optional<CallableProfile> callable;
    std::vector<DeclarationId> children;
    std::vector<StatementId> statements;
};

struct Import {
    Name package;
    std::optional<Name> member;
    bool wildcard{};
    SourceSpanId source;
};

struct Export {
    Name package;
    std::optional<Name> member;
    bool wildcard{};
    SourceSpanId source;
};

struct ModportMember {
    ModportMemberKind kind{ModportMemberKind::signal};
    Name name;
    Direction direction{Direction::unknown};
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
    bool descending{};
    SourceSpanId source;
};

struct GenerateAlternative {
    ScopeId scope;
    std::string label;
    std::vector<GenerateChoice> choices;
    bool is_default{};
    SourceSpanId source;
};

struct GenerateRegion {
    DeclarationId declaration;
    ScopeId scope;
    GenerateKind kind{GenerateKind::conditional};
    std::string label;
    std::string alternative_label;
    std::string iterator;
    std::optional<ExpressionId> initial;
    std::optional<ExpressionId> condition;
    std::optional<ExpressionId> iteration;
    std::vector<DeclarationId> declarations;
    std::vector<InstanceId> instances;
    std::vector<ProcessId> processes;
    std::vector<StatementId> concurrent_statements;
    std::vector<GenerateAlternative> alternatives;
    std::vector<GenerateRegion> nested;
    SourceSpanId source;
    OriginId origin;
};

struct CompilationContext {
    std::string time_unit;
    std::string time_precision;
    std::string default_nettype;
    bool cell{};
};

struct Unit {
    UnitId id;
    ScopeId scope;
    UnitKind kind{UnitKind::module};
    std::string library;
    std::string name;
    SourceSpanId source;
    OriginId origin;
    CompilationContext compilation;
    std::vector<Import> imports;
    std::vector<Export> exports;
    std::vector<DeclarationId> declarations;
    std::vector<Modport> modports;
    std::vector<InstanceId> instances;
    std::vector<ProcessId> processes;
    std::vector<StatementId> concurrent_statements;
    std::vector<GenerateRegion> generates;
};

/// Owning SystemVerilog semantic HIR, including opaque class-handle operations.
class Hir final {
public:
    [[nodiscard]] const std::vector<Unit>& units() const noexcept;
    [[nodiscard]] const std::vector<Declaration>& declarations() const noexcept;
    [[nodiscard]] const std::vector<TypeDefinition>& types() const noexcept;
    [[nodiscard]] const std::vector<Expression>& expressions() const noexcept;
    [[nodiscard]] const std::vector<Statement>& statements() const noexcept;
    [[nodiscard]] const std::vector<Process>& processes() const noexcept;

    std::vector<Unit>& mutable_units() noexcept;
    std::vector<Declaration>& mutable_declarations() noexcept;
    std::vector<TypeDefinition>& mutable_types() noexcept;
    std::vector<Expression>& mutable_expressions() noexcept;
    std::vector<Statement>& mutable_statements() noexcept;
    std::vector<Process>& mutable_processes() noexcept;

private:
    std::vector<Unit> units_;
    std::vector<Declaration> declarations_;
    std::vector<TypeDefinition> types_;
    std::vector<Expression> expressions_;
    std::vector<Statement> statements_;
    std::vector<Process> processes_;
};

} // namespace fsim::semantic::sv
