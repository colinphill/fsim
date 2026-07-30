// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/elaborator.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <deque>
#include <functional>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace fsim::elaboration {
namespace elaboration_detail {
using frontend::AssignmentKind;
using frontend::DesignUnit;
using frontend::Expression;
using frontend::ExpressionKind;
using frontend::ProcessKind;
using frontend::Statement;
using frontend::StatementKind;
using runtime::Logic4;
using runtime::PackedLogic4;
using namespace runtime::simir;


using frontend::AssignmentKind;
using frontend::DesignUnit;
using frontend::Expression;
using frontend::ExpressionKind;
using frontend::ProcessKind;
using frontend::Statement;
using frontend::StatementKind;
using runtime::Logic4;
using runtime::PackedLogic4;
using namespace runtime::simir;

using ConstantEnvironment =
    std::unordered_map<std::string, std::int64_t>;

/// A bounded IEEE 1800 integral constant.
///
/// Values remain width-bearing bit patterns until an explicitly
/// integer-only elaboration consumer requests a checked conversion. X and Z
/// are retained independently so legal four-state parameters can be
/// substituted without passing through a host integer.
struct SystemVerilogConstantValue {
    std::uint64_t bits{};
    std::uint64_t unknown_bits{};
    std::uint64_t high_impedance_bits{};
    std::uint32_t width{32};
    bool is_signed{true};
    bool unsized{};
    frontend::SourceSpan source;

    [[nodiscard]] std::uint64_t mask() const noexcept;
    [[nodiscard]] bool known() const noexcept;
    [[nodiscard]] std::optional<bool> truth_value() const noexcept;
    [[nodiscard]] std::optional<std::int64_t>
    integer_value() const noexcept;
    [[nodiscard]] std::string display() const;
    [[nodiscard]] std::string canonical() const;
    [[nodiscard]] Expression expression(
        const frontend::SourceSpan& use_span) const;
};

using SystemVerilogConstantEnvironment =
    std::unordered_map<std::string, SystemVerilogConstantValue>;

struct SystemVerilogStringValue {
    std::string bytes;
    frontend::SourceSpan source;

    [[nodiscard]] std::string display() const;
    [[nodiscard]] std::string canonical() const;
    [[nodiscard]] Expression expression(
        const frontend::SourceSpan& use_span) const;
};

using SystemVerilogStringEnvironment =
    std::unordered_map<std::string, SystemVerilogStringValue>;

std::optional<SystemVerilogStringValue>
evaluate_systemverilog_string_expression(
    const Expression& expression,
    const SystemVerilogStringEnvironment& environment,
    const ConstantEnvironment& integer_environment,
    std::string& error);

void substitute_systemverilog_strings(
    Expression& expression,
    const SystemVerilogStringEnvironment& environment,
    const ConstantEnvironment& integer_environment);

void substitute_systemverilog_strings(
    DesignUnit& unit,
    const SystemVerilogStringEnvironment& environment,
    const ConstantEnvironment& integer_environment,
    std::vector<Diagnostic>& diagnostics);

void substitute_systemverilog_strings(
    frontend::GenerateBody& body,
    const SystemVerilogStringEnvironment& environment,
    const ConstantEnvironment& integer_environment,
    std::vector<Diagnostic>& diagnostics);

std::optional<SystemVerilogConstantValue>
evaluate_systemverilog_constant_expression(
    const Expression& expression,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback_environment,
    std::string& error);

std::optional<SystemVerilogConstantValue>
evaluate_systemverilog_constant_function_expression(
    const Expression& expression,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback_environment,
    const std::vector<frontend::FunctionDeclaration>& functions,
    std::string& error);

void fold_systemverilog_constant_functions(
    DesignUnit& unit,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback_environment,
    std::vector<Diagnostic>& diagnostics);

std::optional<SystemVerilogConstantValue>
convert_systemverilog_parameter_value(
    const SystemVerilogConstantValue& value,
    const frontend::Type& type,
    std::string& error);

void substitute_systemverilog_parameters(
    Expression& expression,
    const SystemVerilogConstantEnvironment& environment);

void substitute_systemverilog_parameters(
    DesignUnit& unit,
    const SystemVerilogConstantEnvironment& environment);

void substitute_systemverilog_parameters(
    frontend::GenerateBody& body,
    const SystemVerilogConstantEnvironment& environment);

[[nodiscard]] bool is_two_state_domain(
    const frontend::ValueDomain domain) noexcept;

[[nodiscard]] runtime::simir::ValueKind value_kind(
    const frontend::ValueDomain domain) noexcept;

std::optional<std::int64_t> evaluate_constant_expression(
    const Expression& expression,
    const ConstantEnvironment& environment,
    std::string& error);

struct LoweredLiteral {
    PackedLogic4 value;
    frontend::ValueDomain domain{frontend::ValueDomain::Bit2};
};

std::string simple_top_name(std::string_view top);

std::optional<std::uint64_t> unsigned_decimal(std::string_view text);

std::optional<std::int64_t> constant_index(
    const Expression& expression);

std::uint64_t index_distance(
    const std::int64_t lhs,
    const std::int64_t rhs) noexcept;

PackedLogic4 unsigned_value(const std::uint64_t value, const std::size_t width);

PackedLogic4 integer_value(const std::int64_t value);

PackedLogic4 default_packed_value(
    const frontend::Type& type,
    const std::size_t width);

std::optional<std::int64_t> vhdl_enumeration_ordinal(
    const Expression& expression,
    const frontend::Type& type);

const frontend::Type* vhdl_enumeration_type_mark(
    const DesignUnit& unit,
    const std::string_view name);

const frontend::Type* vhdl_object_type(
    const DesignUnit& unit,
    const std::string_view name);

struct FoldedEnumerationAttribute {
    std::int64_t value{};
    bool enumeration_result{};
    bool boolean_result{};
};

std::optional<FoldedEnumerationAttribute>
evaluate_vhdl_enumeration_attribute(
    const Expression& expression,
    const DesignUnit& unit,
    const ConstantEnvironment& environment,
    std::string& error,
    bool& range_error);

std::optional<LoweredLiteral> literal_value(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Language language);

struct ConstantTypeInfo {
    frontend::ValueDomain domain{frontend::ValueDomain::Unknown};
    bool vhdl_enumeration{};
    std::string nominal_type;

    ConstantTypeInfo();
    ConstantTypeInfo(frontend::ValueDomain value);
    ConstantTypeInfo(
        frontend::ValueDomain value,
        bool enumeration,
        std::string nominal = {});
};

using ConstantDomainEnvironment =
    std::unordered_map<std::string, ConstantTypeInfo>;

void prepare_systemverilog_generate_regions(
    std::vector<frontend::GenerateRegion>& regions,
    const SystemVerilogConstantEnvironment& environment,
    const SystemVerilogStringEnvironment& string_environment,
    const ConstantEnvironment& integer_environment,
    const ConstantDomainEnvironment& domains,
    std::vector<Diagnostic>& diagnostics);

std::int64_t normalize_systemverilog_parameter_value(
    std::int64_t value,
    const frontend::Type& type) noexcept;

std::optional<std::int64_t> constant_literal_integer(
    const Expression& expression,
    std::string& error);

bool checked_add(
    const std::int64_t left,
    const std::int64_t right,
    std::int64_t& result);

bool checked_subtract(
    const std::int64_t left,
    const std::int64_t right,
    std::int64_t& result);

bool checked_multiply(
    const std::int64_t left,
    const std::int64_t right,
    std::int64_t& result);

bool checked_power(
    const std::int64_t base,
    const std::int64_t exponent,
    std::int64_t& result);

std::optional<std::int64_t> evaluate_constant_expression(
    const Expression& expression,
    const ConstantEnvironment& environment,
    std::string& error);

Expression constant_expression(
    const std::int64_t value,
    const frontend::SourceSpan& span,
    const frontend::ValueDomain domain,
    const frontend::Language language,
    const bool vhdl_enumeration = false,
    std::string nominal_type = {});

bool fold_vhdl_enumeration_attributes(
    Expression& expression,
    const DesignUnit& unit,
    const ConstantEnvironment& environment,
    std::string& error,
    bool& range_error);

void substitute_parameters(
    Expression& expression,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    const frontend::Language language);

void substitute_parameters(
    frontend::Type& type,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    std::vector<Diagnostic>& diagnostics,
    const frontend::Language language);

void substitute_parameters(
    frontend::VariableDeclaration& declaration,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    std::vector<Diagnostic>& diagnostics,
    const frontend::Language language);

void substitute_parameters(
    std::vector<Statement>& statements,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    std::vector<Diagnostic>& diagnostics,
    const frontend::Language language);

void substitute_parameters(
    std::vector<frontend::Instance>& instances,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    const frontend::Language language);

void substitute_parameters(
    std::vector<frontend::GenerateRegion>& generates,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    frontend::Language language,
    std::vector<Diagnostic>& diagnostics);

void substitute_parameters(
    frontend::GenerateBody& body,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    const frontend::Language language,
    std::vector<Diagnostic>& diagnostics);

void substitute_parameters(
    std::vector<frontend::GenerateRegion>& generates,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    const frontend::Language language,
    std::vector<Diagnostic>& diagnostics);

using QualifiedIdentifierMap =
    std::unordered_map<std::string, frontend::SourceSpan>;

struct NamedTypeBinding {
    frontend::Type type;
    std::string owner;
    bool interface_formal{};
};

using NamedTypeEnvironment =
    std::unordered_map<std::string, NamedTypeBinding>;

void collect_qualified_identifiers(
    const Expression& expression,
    QualifiedIdentifierMap& identifiers);

void collect_qualified_identifiers(
    const frontend::Type& type,
    QualifiedIdentifierMap& identifiers);

void collect_qualified_identifiers(
    const std::vector<Statement>& statements,
    QualifiedIdentifierMap& identifiers);

void collect_qualified_identifiers(
    const std::vector<frontend::GenerateRegion>& generates,
    QualifiedIdentifierMap& identifiers);

void collect_qualified_identifiers(
    const frontend::GenerateBody& body,
    QualifiedIdentifierMap& identifiers);

void collect_qualified_identifiers(
    const std::vector<frontend::GenerateRegion>& generates,
    QualifiedIdentifierMap& identifiers);

QualifiedIdentifierMap qualified_identifiers(
    const DesignUnit& unit);

template <typename Regions, typename Visitor>
void visit_generate_types(
    Regions& regions, Visitor& visitor);

template <typename Statements, typename Visitor>
void visit_statement_types(
    Statements& statements, Visitor& visitor) {
    for (auto& statement : statements) {
        for (auto& declaration : statement.declarations) {
            visitor(declaration.type);
        }
        visit_statement_types(
            statement.statements, visitor);
        visit_statement_types(
            statement.else_statements, visitor);
        for (auto& alternative :
             statement.case_alternatives) {
            visit_statement_types(
                alternative.statements, visitor);
        }
    }
}

template <typename Body, typename Visitor>
void visit_generate_body_types(
    Body& body, Visitor& visitor) {
    for (auto& constant : body.constants) {
        visitor(constant.type);
    }
    for (auto& signal : body.signals) {
        visitor(signal.type);
    }
    for (auto& process : body.processes) {
        for (auto& variable : process.variables) {
            visitor(variable.type);
        }
        visit_statement_types(
            process.statements, visitor);
    }
    visit_generate_types(
        body.generate_regions, visitor);
}

template <typename Regions, typename Visitor>
void visit_generate_types(
    Regions& regions, Visitor& visitor) {
    for (auto& region : regions) {
        visit_generate_body_types(
            region.then_body, visitor);
        visit_generate_body_types(
            region.else_body, visitor);
        for (auto& alternative : region.alternatives) {
            visit_generate_body_types(
                alternative.body, visitor);
        }
    }
}

template <typename Unit, typename Visitor>
void visit_declared_types(
    Unit& unit,
    Visitor visitor,
    const bool include_aliases = false) {
    if (include_aliases) {
        for (auto& alias : unit.type_aliases) {
            visitor(alias.type);
        }
    }
    for (auto& parameter : unit.parameters) {
        visitor(parameter.type);
    }
    for (auto& port : unit.ports) {
        visitor(port.type);
    }
    for (auto& signal : unit.signals) {
        visitor(signal.type);
    }
    visit_statement_types(
        unit.concurrent_statements, visitor);
    for (auto& process : unit.processes) {
        for (auto& variable : process.variables) {
            visitor(variable.type);
        }
        visit_statement_types(
            process.statements, visitor);
    }
    visit_generate_types(
        unit.generate_regions, visitor);
}

std::string generated_scope(
    const std::string_view parent_scope,
    const std::string_view local_scope);

using GeneratedNameEnvironment =
    std::unordered_map<std::string, std::string>;

void qualify_generated_expression(
    Expression& expression,
    const GeneratedNameEnvironment& names);

void qualify_generated_statements(
    std::vector<Statement>& statements,
    const GeneratedNameEnvironment& names);

void qualify_generated_statement(
    Statement& statement,
    const GeneratedNameEnvironment& names);

void qualify_generated_statements(
    std::vector<Statement>& statements,
    const GeneratedNameEnvironment& names);

void qualify_generated_process(
    frontend::Process& process,
    const GeneratedNameEnvironment& names,
    const std::string_view scope);

void qualify_generated_instance(
    frontend::Instance& instance,
    const GeneratedNameEnvironment& names,
    const std::string_view scope);

void expand_generate_regions(
    const std::vector<frontend::GenerateRegion>& generates,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    frontend::Language language,
    std::string_view parent_scope,
    const GeneratedNameEnvironment& visible_names,
    DesignUnit& unit,
    std::vector<Diagnostic>& diagnostics);

void evaluate_generated_constants(
    frontend::GenerateBody& body,
    ConstantEnvironment& environment,
    ConstantDomainEnvironment& domains,
    const frontend::Language language,
    std::vector<Diagnostic>& diagnostics);

void append_generated_body(
    frontend::GenerateBody body,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    const frontend::Language language,
    const std::string_view scope,
    const GeneratedNameEnvironment& visible_names,
    DesignUnit& unit,
    std::vector<Diagnostic>& diagnostics);

void expand_generate_regions(
    const std::vector<frontend::GenerateRegion>& generates,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    const frontend::Language language,
    const std::string_view parent_scope,
    const GeneratedNameEnvironment& visible_names,
    DesignUnit& unit,
    std::vector<Diagnostic>& diagnostics);

struct PackageBinding {
    std::string template_name;
    DesignUnit unit;
    ConstantEnvironment environment;
    std::vector<std::pair<std::string, std::string>> values;
    std::vector<std::pair<std::string, std::string>> identity_values;
};

using PackageEnvironment =
    std::unordered_map<std::string, PackageBinding>;

struct SpecializedUnit {
    DesignUnit unit;
    ConstantEnvironment environment;
    SystemVerilogStringEnvironment string_environment;
    std::vector<std::pair<std::string, std::string>> values;
    std::vector<std::pair<std::string, std::string>> identity_values;
    PackageEnvironment packages;
};

struct InterfaceTypeSpecialization {
    DesignUnit unit;
    std::vector<frontend::ParameterOverride> value_overrides;
    std::vector<std::pair<std::string, std::string>> values;
    bool applied{};
};

NamedTypeEnvironment local_vhdl_type_environment(
    const DesignUnit& unit);

NamedTypeEnvironment local_systemverilog_type_environment(
    const DesignUnit& unit);

InterfaceTypeSpecialization specialize_vhdl_interface_types(
    const DesignUnit& source,
    const std::vector<frontend::ParameterOverride>& overrides,
    const ConstantEnvironment& parent_environment,
    const ConstantDomainEnvironment& parent_domains,
    const NamedTypeEnvironment& parent_types,
    const std::vector<frontend::FunctionDeclaration>& parent_functions,
    const std::vector<frontend::ProcedureDeclaration>& parent_procedures,
    frontend::Language association_language,
    std::vector<Diagnostic>& diagnostics);

InterfaceTypeSpecialization specialize_systemverilog_type_parameters(
    const DesignUnit& source,
    const std::vector<frontend::ParameterOverride>& overrides,
    const ConstantEnvironment& parent_environment,
    const NamedTypeEnvironment& parent_types,
    frontend::Language association_language,
    std::vector<Diagnostic>& diagnostics);

std::optional<std::string> systemverilog_type_parameter_identity(
    const frontend::Type& type);

enum class SpecializationDiagnostic {
    invalid_actual,
    duplicate_actual,
    association_order,
    actual_evaluation,
    default_evaluation,
    subtype_constraint,
    ambiguous_name,
};

const char* specialization_diagnostic_code(
    const bool is_vhdl,
    const bool is_package,
    const bool is_systemverilog_package,
    const SpecializationDiagnostic diagnostic);

bool parameter_name_matches(
    const std::string_view formal,
    const std::string_view actual,
    const frontend::Language target_language,
    const frontend::Language association_language);

SpecializedUnit specialize_unit(
    const DesignUnit& source,
    const std::vector<frontend::ParameterOverride>& overrides,
    const ConstantEnvironment& parent_environment,
    const frontend::Language association_language,
    std::vector<Diagnostic>& diagnostics);

bool valid_systemc_construction_value(
    const fsim_sc_construction_type_v1 type,
    const std::int64_t value);

std::optional<std::vector<std::pair<std::string, std::int64_t>>>
specialize_systemc_construction(
    const std::vector<SystemCConstructionParameter>& schema,
    const std::vector<frontend::ParameterOverride>& overrides,
    const ConstantEnvironment& parent_environment,
    const frontend::Language association_language,
    std::vector<Diagnostic>& diagnostics);




const DesignUnit* choose_unit(
    const frontend::ParsedDesign& parsed, const std::string& requested);

struct TargetSpec {
    std::string language;
    std::string library;
    std::string unit;
    std::optional<std::string> architecture;
};

std::optional<TargetSpec> parse_target(const std::string_view spelling);

const DesignUnit* find_vhdl_entity(
    const frontend::ParsedDesign& parsed, const DesignUnit& architecture);

const std::vector<frontend::SignalDeclaration>* unit_ports(
    const frontend::ParsedDesign& parsed,
    const DesignUnit& unit);

const DesignUnit* choose_bound_unit(
    const frontend::ParsedDesign& parsed,
    const TargetSpec& target);

const DesignUnit* choose_top_unit(
    const frontend::ParsedDesign& parsed,
    const std::string_view spelling);

const DesignUnit* choose_same_language_instance(
    const frontend::ParsedDesign& parsed,
    const DesignUnit& parent,
    const std::string_view name);

std::string unit_identity(const DesignUnit& unit);


} // namespace elaboration_detail

using frontend::AssignmentKind;
using frontend::DesignUnit;
using frontend::Expression;
using frontend::ExpressionKind;
using frontend::ProcessKind;
using frontend::Statement;
using frontend::StatementKind;
using runtime::Logic4;
using runtime::PackedLogic4;
using namespace runtime::simir;
using namespace elaboration_detail;

class Lowerer final {
public:
    Lowerer(
        ElaboratedDesign& design,
        const std::unordered_map<std::string, SignalId>& signals,
        const std::unordered_map<
            std::string, const frontend::Type*>& visible_types,
        const std::unordered_map<
            std::string, const frontend::Type*>& visible_type_marks,
        const std::vector<frontend::FunctionDeclaration>& functions,
        const std::vector<frontend::TaskDeclaration>& tasks,
        const std::vector<frontend::ProcedureDeclaration>& procedures,
        std::vector<Diagnostic>& diagnostics);

    Process lower_process(
        const frontend::Process& source,
        const frontend::Language language,
        const std::string_view hierarchy);

    Process lower_concurrent(
        const Statement& statement,
        const frontend::Language language,
        const std::string& name,
        const std::size_t order);

private:
    [[nodiscard]] const frontend::Type* visible_type(
        const std::string_view name) const;

    [[nodiscard]] const frontend::Type* object_type(
        const std::string_view name) const;

    [[nodiscard]] const frontend::Type* visible_type_mark(
        const std::string_view name) const;

    [[nodiscard]] const frontend::Type*
    enumeration_expression_type(
        const Expression& expression) const;

    [[nodiscard]] static std::pair<std::int32_t, std::int32_t>
    integer_bounds(
        const std::optional<frontend::IntegerRange>& range);

    void emit_integer_check(
        const RegisterId source,
        const std::optional<frontend::IntegerRange>& range);

    [[nodiscard]] static std::pair<std::int32_t, std::int32_t>
    enumeration_bounds(const frontend::Type& type);

    void emit_enumeration_check(
        const RegisterId source,
        const frontend::Type& type);

    [[nodiscard]] bool
    validate_static_enumeration_assignment(
        const Expression& expression,
        const frontend::Type& type,
        const frontend::SourceSpan& span);

    [[nodiscard]] bool validate_static_integer_assignment(
        const Expression& expression,
        const std::optional<frontend::IntegerRange>& range,
        const frontend::SourceSpan& span);

    static bool contains_explicit_wait(
        const std::vector<Statement>& statements);

    void initialize_variables(
        const std::vector<frontend::VariableDeclaration>& variables);

    [[nodiscard]] static std::string declaration_key(
        const frontend::VariableDeclaration& variable);

    [[nodiscard]] std::string scoped_local_name(
        const std::string_view name) const;

    [[nodiscard]] static std::string block_scope_name(
        const Statement& statement);

    void lower_block(const Statement& statement);

    void lower_event_trigger(const Statement& statement);

    static const Statement* recognized_vhdl_edge_guard(
        const frontend::Process& source);

    void lower_statements(const std::vector<Statement>& statements);

    void lower_statement(const Statement& statement);

    [[nodiscard]] std::pair<
        std::vector<SignalId>,
        std::vector<runtime::simir::EdgeKind>>
    resolve_wait_sensitivities(
        const Statement& statement);

    void emit_debug_point(
        const DebugPointKind kind,
        const frontend::SourceSpan& span);

    void lower_wait_until(const Statement& statement);

    void lower_immediate_condition_wait(
        const Statement& statement);

    std::optional<RegisterId> lower_condition(
        const Expression& expression,
        std::string diagnostic_code,
        std::string_view construct);

    void lower_assert(const Statement& statement);

    [[nodiscard]] const frontend::Type*
    vhdl_array_attribute_prefix_type(
        const Expression& expression) const;

    [[nodiscard]] static bool is_vhdl_array_like(
        const frontend::Type& type);

    std::optional<frontend::PackedRange>
    vhdl_array_attribute_range(
        const Expression& expression,
        const bool report_errors);

    std::optional<std::int64_t>
    static_integer_value(const Expression& expression);

    struct ConstantSliceSelection {
        std::size_t offset{};
        std::size_t width{};
    };

    std::optional<DynamicIndex>
    lower_dynamic_index(
        const Expression& source,
        const Expression& index,
        const std::size_t source_width,
        const std::uint32_t base_offset,
        const frontend::SourceSpan& span);

    std::optional<ConstantSliceSelection>
    constant_slice_selection(
        const Expression& expression,
        const std::size_t source_width);

    void lower_assignment(const Statement& statement);

    void lower_if(const Statement& statement);

    void lower_case(const Statement& statement);

    void lower_loop(const Statement& statement);

    void lower_runtime_loop(const Statement& statement);

    void lower_loop_control(
        const Statement& statement, const bool is_break);

    struct PackedMemberReference {
        std::string base;
        const frontend::PackedMember* member{};
    };

    std::optional<PackedMemberReference> packed_member_reference(
        const std::string_view name) const;

    [[nodiscard]] RegisterId widen_enumeration_ordinal(
        const RegisterId source);

    [[nodiscard]] RegisterId narrow_enumeration_ordinal(
        const RegisterId source,
        const frontend::Type& type);

    std::optional<RegisterId> lower_enumeration_attribute(
        const Expression& expression,
        const frontend::Type& type);

    std::optional<RegisterId> lower_vhdl_array_aggregate(
        const Expression& expression,
        const std::size_t expected_width,
        const frontend::Type& expected_type);

    struct ExpressionAttempt {
        ExpressionAttempt();
        ExpressionAttempt(RegisterId result);
        ExpressionAttempt(std::optional<RegisterId> result);
        ExpressionAttempt(std::nullopt_t);

        bool handled{};
        std::optional<RegisterId> value;
    };

    std::optional<RegisterId> lower_expression(
        const Expression& expression,
        const std::size_t expected_width,
        const frontend::Type* expected_type = nullptr);

    ExpressionAttempt lower_primary_expression(
        const Expression& expression,
        std::size_t expected_width,
        const frontend::Type* expected_type);
    ExpressionAttempt lower_unary_attribute_expression(
        const Expression& expression,
        std::size_t expected_width,
        const frontend::Type*);
    ExpressionAttempt lower_system_function_expression(
        const Expression& expression,
        std::size_t expected_width,
        const frontend::Type* expected_type);
    ExpressionAttempt lower_user_function_expression(
        const Expression& expression,
        std::size_t expected_width,
        const frontend::Type* expected_type);
    ExpressionAttempt lower_binary_expression(
        const Expression& expression,
        std::size_t expected_width,
        const frontend::Type*);

    std::optional<std::size_t> infer_width(const Expression& expression) const;

    std::optional<frontend::PackedRange> expression_range(
        const Expression& expression,
        const std::size_t width) const;

    std::optional<std::size_t> select_offset(
        const Expression& expression,
        const std::int64_t index,
        const std::size_t width) const;

    [[nodiscard]] bool is_signed_expression(
        const Expression& expression) const;

    [[nodiscard]] bool is_integer_expression(
        const Expression& expression) const;

    [[nodiscard]] const frontend::FunctionDeclaration*
    visible_function(std::string_view name) const;

    void initialize_function_support();

    void lower_pending_functions();

    void lower_function_body(std::size_t function_index);

    void lower_function_return(const Statement& statement);

    void diagnose_function_cycles();

    void initialize_task_support();

    void lower_task_call(const Statement& statement);

    void lower_pending_tasks();

    void lower_task_body(std::size_t task_index);

    void lower_task_return(const Statement& statement);

    void diagnose_task_cycles();

    void initialize_procedure_support();

    void lower_procedure_call(const Statement& statement);

    void lower_pending_procedures();

    void lower_procedure_body(std::size_t procedure_index);

    void lower_procedure_return(const Statement& statement);

    void diagnose_procedure_cycles();

    void collect_identifiers(
        const Expression& expression,
        std::set<std::string>& output) const;

    void collect_statement_identifiers(
        const std::vector<Statement>& statements,
        std::set<std::string>& output) const;

    RegisterId allocate_register(
        const std::size_t width,
        const frontend::ValueDomain domain);

    [[nodiscard]] std::size_t register_width(const RegisterId id) const;

    [[nodiscard]] frontend::ValueDomain register_domain(
        const RegisterId id) const;

    [[nodiscard]] RegisterId resize_register(
        RegisterId source,
        std::size_t width,
        bool sign_extend);

    void report(std::string code, std::string message, frontend::SourceSpan span);

    ElaboratedDesign& design_;
    const std::unordered_map<std::string, SignalId>& signals_;
    const std::unordered_map<
        std::string, const frontend::Type*>& visible_types_;
    const std::unordered_map<
        std::string, const frontend::Type*>& visible_type_marks_;
    const std::vector<frontend::FunctionDeclaration>& functions_;
    const std::vector<frontend::TaskDeclaration>& tasks_;
    const std::vector<frontend::ProcedureDeclaration>& procedures_;
    std::vector<Diagnostic>& diagnostics_;
    Process process_;
    RegisterId next_register_{};
    std::vector<std::size_t> register_widths_;
    std::vector<frontend::ValueDomain> register_domains_;
    std::unordered_map<std::string, RegisterId> locals_;
    std::unordered_map<std::string, bool> local_signed_;
    std::unordered_map<
        std::string, std::optional<frontend::PackedRange>>
        local_ranges_;
    std::unordered_map<
        std::string, std::optional<frontend::IntegerRange>>
        local_integer_ranges_;
    std::unordered_map<
        std::string, std::vector<frontend::PackedMember>>
        local_members_;
    std::unordered_map<std::string, const frontend::Type*>
        local_types_;
    std::unordered_map<std::string, RegisterId>
        declaration_registers_;
    std::unordered_set<std::string> debug_local_names_;
    std::vector<std::string> local_scope_;
    struct LoopControlContext {
        std::optional<InstructionIndex> continue_target;
        std::vector<InstructionIndex> continue_jumps;
        std::vector<InstructionIndex> break_jumps;
        std::string label;
    };
    std::vector<LoopControlContext> loop_controls_;
    struct FunctionFrame {
        const frontend::FunctionDeclaration* source{};
        RegisterId result{};
        std::vector<RegisterId> arguments;
        std::optional<InstructionIndex> target;
        std::vector<InstructionIndex> call_sites;
        bool allocated{};
        bool queued{};
        bool lowered{};
    };
    std::vector<FunctionFrame> function_frames_;
    std::unordered_map<std::string, std::size_t>
        function_indices_;
    std::deque<std::size_t> pending_functions_;
    std::vector<std::unordered_set<std::size_t>>
        function_dependencies_;
    std::optional<std::size_t> active_function_;
    std::vector<InstructionIndex> function_return_jumps_;
    CallStack function_call_stack_;
    bool function_support_initialized_{};
    struct TaskFrame {
        const frontend::TaskDeclaration* source{};
        std::vector<RegisterId> arguments;
        std::optional<InstructionIndex> target;
        std::vector<InstructionIndex> call_sites;
        bool allocated{};
        bool queued{};
        bool lowered{};
    };
    std::vector<TaskFrame> task_frames_;
    std::unordered_map<std::string, std::size_t> task_indices_;
    std::deque<std::size_t> pending_tasks_;
    std::vector<std::unordered_set<std::size_t>>
        task_dependencies_;
    std::vector<bool> task_suspending_;
    std::optional<std::size_t> active_task_;
    std::vector<InstructionIndex> task_return_jumps_;
    CallStack task_call_stack_;
    bool task_support_initialized_{};
    struct ProcedureFrame {
        const frontend::ProcedureDeclaration* source{};
        std::vector<RegisterId> arguments;
        std::optional<InstructionIndex> target;
        std::vector<InstructionIndex> call_sites;
        bool allocated{};
        bool queued{};
        bool lowered{};
    };
    std::vector<ProcedureFrame> procedure_frames_;
    std::unordered_map<std::string, std::size_t>
        procedure_indices_;
    std::deque<std::size_t> pending_procedures_;
    std::vector<std::unordered_set<std::size_t>>
        procedure_dependencies_;
    std::optional<std::size_t> active_procedure_;
    std::vector<InstructionIndex> procedure_return_jumps_;
    CallStack procedure_call_stack_;
    bool procedure_support_initialized_{};
    frontend::ProcessKind process_kind_{
        frontend::ProcessKind::VhdlProcess};
    frontend::Language language_{frontend::Language::Vhdl2008};
    std::string hierarchy_;
};

class HierarchyBuilder final {
public:
    HierarchyBuilder(
        const frontend::ParsedDesign& parsed,
        ElaboratedDesign& design,
        std::vector<Diagnostic>& diagnostics,
        const std::span<const Binding> bindings,
        const std::span<const SystemCInstanceDescription>
            systemc_instances,
        SystemCFactoryProvider* systemc_provider);

    void build(const DesignUnit& root);

    void build(const SystemCInstanceDescription& root);

private:
    using SignalMap = std::unordered_map<std::string, SignalId>;
    using ObjectMap = std::unordered_map<std::uint64_t, SignalId>;

    struct ConfiguredVhdlInstance {
        frontend::Instance instance;
        std::optional<DesignUnit> target;
        const frontend::VhdlComponentConfiguration*
            configuration_rule{};
        const DesignUnit* referenced_configuration{};
        std::string component_name;
        std::string component_identity;
        std::string configuration_identity;
        bool applied{};
        bool valid{true};
    };

    static std::vector<std::string> selected_name_parts(
        const std::string_view name);

    const DesignUnit* select_vhdl_configuration_root(
        const DesignUnit& configuration);

    std::string vhdl_configuration_identity(
        const DesignUnit& configuration) const;

    void validate_vhdl_component_configurations(
        const DesignUnit& unit,
        const std::string& path);

    ConfiguredVhdlInstance configure_vhdl_component_instance(
        const DesignUnit& unit,
        const frontend::Instance& instance,
        const std::string& path);

    ConfiguredVhdlInstance bind_vhdl_component_instance(
        const DesignUnit& unit,
        const frontend::Instance& instance,
        const std::string& path,
        const ConstantEnvironment& parent_environment);

    void expand_vhdl_context_references(
        DesignUnit& unit,
        const std::span<const frontend::VhdlContextItem> context,
        std::vector<frontend::VhdlContextItem>& expanded,
        std::vector<const DesignUnit*>& context_stack,
        const std::string_view visibility_library);

    void import_vhdl_package_constants(
        DesignUnit& unit,
        const std::span<const frontend::VhdlContextItem>
            context,
        std::vector<const DesignUnit*>& import_stack,
        NamedTypeEnvironment& imported_types);

    std::optional<SpecializedUnit> specialize_vhdl_package(
        const DesignUnit& package,
        std::vector<const DesignUnit*>& import_stack,
        const frontend::SourceSpan& reference_span,
        const std::vector<frontend::ParameterOverride>& overrides = {},
        const ConstantEnvironment& parent_environment = {},
        const ConstantDomainEnvironment& parent_domains = {},
        const NamedTypeEnvironment& parent_types = {},
        const std::vector<frontend::FunctionDeclaration>&
            parent_functions = {},
        const std::vector<frontend::ProcedureDeclaration>&
            parent_procedures = {});

    void bind_vhdl_interface_packages(
        DesignUnit& unit,
        std::vector<frontend::ParameterOverride>& overrides,
        const PackageEnvironment& parent_packages,
        const ConstantEnvironment& parent_environment,
        const ConstantDomainEnvironment& parent_domains,
        const NamedTypeEnvironment& parent_types,
        const std::vector<frontend::FunctionDeclaration>&
            parent_functions,
        const std::vector<frontend::ProcedureDeclaration>&
            parent_procedures,
        frontend::Language association_language,
        PackageEnvironment& bindings,
        std::vector<std::pair<std::string, std::string>>&
            identity_values);

    void instantiate_vhdl_local_packages(
        SpecializedUnit& specialized,
        const PackageEnvironment& inherited_packages);

    void instantiate_vhdl_generic_subprograms(
        SpecializedUnit& specialized);

    void materialize_vhdl_package_binding(
        DesignUnit& unit,
        std::string_view prefix,
        const PackageBinding& binding);

    void import_qualified_vhdl_package_constants(
        DesignUnit& unit,
        std::vector<const DesignUnit*>& import_stack);

    void import_qualified_vhdl_package_types(
        DesignUnit& unit,
        NamedTypeEnvironment& imported_types,
        std::vector<const DesignUnit*>& import_stack);

    std::optional<SpecializedUnit>
    specialize_systemverilog_package(
        const DesignUnit& package,
        std::vector<const DesignUnit*>& import_stack,
        const frontend::SourceSpan& reference_span);

    const DesignUnit* find_systemverilog_package(
        const DesignUnit& owner,
        const std::string_view name) const;

    void append_package_dependencies(
        DesignUnit& unit,
        const DesignUnit& package,
        const SpecializedUnit& specialized);

    void import_systemverilog_package_items(
        DesignUnit& unit,
        std::vector<const DesignUnit*>& import_stack,
        NamedTypeEnvironment& type_environment);

    void import_qualified_systemverilog_package_items(
        DesignUnit& unit,
        std::vector<const DesignUnit*>& import_stack,
        NamedTypeEnvironment& type_environment);

    void resolve_named_types(
        DesignUnit& unit,
        const NamedTypeEnvironment& imported_types,
        const bool vhdl = false,
        const bool resolve_ports = true);

    DesignUnit effective_unit(
        const DesignUnit& selected,
        const DesignUnit* entity_override = nullptr);

    SpecializedUnit specialize_selected_unit(
        const DesignUnit& selected,
        const std::vector<frontend::ParameterOverride>& overrides,
        const ConstantEnvironment& parent_environment,
        const ConstantDomainEnvironment& parent_domains,
        const NamedTypeEnvironment& parent_types,
        const std::vector<frontend::FunctionDeclaration>& parent_functions,
        const std::vector<frontend::ProcedureDeclaration>& parent_procedures,
        const PackageEnvironment& parent_packages,
        const frontend::Language association_language);

    void finish();

    static ResolutionKind native_resolution(
        const SignalInfo& signal);

    std::optional<ResolutionKind> explicit_resolution(
        const SignalId signal);

    void set_resolution(
        const SignalId signal,
        const ResolutionKind resolution);

    void validate_process_drivers();

    std::optional<SignalId> add_owned_signal(
        const frontend::SignalDeclaration& declaration,
        const std::string_view path,
        SignalMap& local);

    const Binding* binding_for(const std::string& path);

    const DesignUnit* bound_target(
        const frontend::Instance& instance,
        const DesignUnit& parent,
        const std::string& path,
        const Binding* binding);

    void validate_boundary_type(
        const frontend::SignalDeclaration& port,
        const SignalInfo& actual,
        const std::string& path,
        const frontend::SourceSpan& source,
        const bool cross_language);

    void note_boundary_driver(
        const SignalId signal,
        const Binding* binding,
        const std::string& path,
        const frontend::SourceSpan& source,
        const bool cross_language);

    SignalMap connect_ports(
        const frontend::Instance& instance,
        const std::vector<frontend::SignalDeclaration>& ports,
        const std::string& path,
        const SignalMap& parent_signals,
        const Binding* binding,
        const bool cross_language);

    SignalMap connect_instance(
        const frontend::Instance& instance,
        const DesignUnit& target,
        const std::string& path,
        const SignalMap& parent_signals,
        const Binding* binding,
        const bool cross_language);

    static frontend::SignalDeclaration external_port_declaration(
        const ExternalPort& port);

    static frontend::SignalDeclaration foreign_port_declaration(
        const ForeignPort& port);

    std::pair<SignalMap, ObjectMap> connect_systemc_instance(
        const frontend::Instance& instance,
        const SystemCInstanceDescription& target,
        const std::string& path,
        const SignalMap& parent_signals,
        const Binding* binding);

    SignalMap connect_foreign_child(
        const ForeignChild& child,
        const DesignUnit& target,
        const std::string& path,
        const ObjectMap& objects);

    const SystemCInstanceDescription* systemc_description(
        const std::string& path,
        const std::string_view target,
        const frontend::SourceSpan& source);

    const SystemCInstanceDescription* construct_systemc_description(
        const frontend::Instance& instance,
        const std::string& path,
        const std::string_view target,
        const ConstantEnvironment& parent_environment,
        const frontend::Language association_language);

    void instantiate_systemc(
        const SystemCInstanceDescription& instance,
        const std::string& path,
        SignalMap aliases,
        ObjectMap objects,
        const bool native_child = false);

    void instantiate(
        const DesignUnit& unit,
        const std::string& path,
        SignalMap aliases,
        ConstantEnvironment parameter_environment,
        std::vector<std::pair<std::string, std::string>>
            parameter_values,
        std::vector<std::pair<std::string, std::string>>
            parameter_identity_values,
        PackageEnvironment package_environment);

    void report(
        std::string code,
        std::string message,
        frontend::SourceSpan source);

    const frontend::ParsedDesign& parsed_;
    ElaboratedDesign& design_;
    std::vector<Diagnostic>& diagnostics_;
    std::unordered_map<std::string, const Binding*> bindings_;
    std::unordered_set<std::string> used_bindings_;
    const DesignUnit* active_vhdl_configuration_{};
    std::unordered_map<std::string, const DesignUnit*>
        vhdl_configurations_by_path_;
    std::unordered_map<
        std::string, const SystemCInstanceDescription*>
        systemc_instances_;
    std::deque<SystemCInstanceDescription>
        owned_systemc_instances_;
    SystemCFactoryProvider* systemc_provider_{};
    std::unordered_set<std::string> used_systemc_instances_;
    std::unordered_set<std::string> instance_paths_;
    std::vector<std::string> stack_;
    std::unordered_map<SignalId, std::size_t> boundary_driver_count_;
    std::unordered_set<SignalId> cross_language_boundary_signals_;
    std::unordered_map<SignalId, std::string> resolver_by_signal_;
};

} // namespace fsim::elaboration
