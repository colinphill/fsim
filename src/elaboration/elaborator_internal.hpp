// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "hierarchy_builder_types.hpp"

#include "elaboration_targets.hpp"
#include "fsim/elaboration/elaborator.hpp"
#include "fsim/frontend/systemverilog_scalar_folding.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
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

    std::string systemverilog_type_identity(const frontend::Type& type);
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
    using ConstantEnvironment = std::unordered_map<std::string, std::int64_t>;
    /// A resource-governed IEEE 1800 integral constant.
    ///
    /// Values remain width-bearing bit patterns until an explicitly
    /// integer-only elaboration consumer requests a checked conversion. X and Z
    /// and the complete nine-state domain are retained in owning packed storage so
    /// legal parameters can be substituted without passing through a host word.
    struct SystemVerilogConstantValue {
        PackedLogic4 packed { 32, Logic4::zero };
        // Changes 5-8 replace these single-word arithmetic fast-path mirrors. The
        // owning packed value above is authoritative for widths above 64 bits.
        std::uint64_t bits { };
        std::uint64_t unknown_bits { };
        std::uint64_t high_impedance_bits { };
        std::uint32_t width { 32 };
        bool is_signed { true };
        bool unsized { };
        // IEEE 1800 symbolic unbounded parameter value (`$`). It is retained
        // independently from the packed payload and is only consumable by
        // `$isunbounded`; ordinary value conversion is ill-formed.
        bool unbounded { };
        frontend::ValueDomain domain { frontend::ValueDomain::Logic4 };
        std::string nominal_type;
        frontend::SourceSpan source;
        // Source-direction packed bounds retained for the IEEE 1800 array query
        // system functions. Values without a declared packed type use the
        // canonical descending [width-1:0] range.
        std::optional<frontend::PackedRange> packed_range;
        SystemVerilogConstantValue() = default;
        SystemVerilogConstantValue(
            std::uint64_t bits,
            std::uint64_t unknown_bits,
            std::uint64_t high_impedance_bits,
            std::uint32_t width,
            bool is_signed,
            bool unsized,
            frontend::SourceSpan source);
        SystemVerilogConstantValue(
            PackedLogic4 packed,
            bool is_signed,
            bool unsized,
            frontend::ValueDomain domain,
            std::string nominal_type,
            frontend::SourceSpan source);
        void refresh_low_word_mirrors() noexcept;
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
    using SystemVerilogConstantEnvironment = std::unordered_map<std::string, SystemVerilogConstantValue>;
    using frontend::SystemVerilogScalarConstant;
    using frontend::SystemVerilogScalarConstantEnvironment;
    frontend::SystemVerilogScalarEvaluationContext
    systemverilog_scalar_evaluation_context(const DesignUnit& unit);
    std::optional<SystemVerilogScalarConstant>
    evaluate_systemverilog_scalar_parameter(
        const Expression& expression,
        const frontend::Type& type,
        const SystemVerilogScalarConstantEnvironment& environment,
        const SystemVerilogConstantEnvironment& integral_environment,
        const ConstantEnvironment& fallback_environment,
        const frontend::SystemVerilogScalarEvaluationContext& context,
        bool& applicable,
        std::string& error);
    void substitute_systemverilog_scalars(
        Expression& expression,
        const SystemVerilogScalarConstantEnvironment& environment);
    void substitute_systemverilog_scalar_parameter_sites(
        DesignUnit& unit,
        const SystemVerilogScalarConstantEnvironment& environment);
    void prepare_systemverilog_scalar_callable_profiles(
        DesignUnit& unit,
        const SystemVerilogScalarConstantEnvironment& environment);
    bool systemverilog_function_profile_matches(
        const frontend::FunctionDeclaration& left,
        const frontend::FunctionDeclaration& right);
    bool systemverilog_task_profile_matches(
        const frontend::TaskDeclaration& left,
        const frontend::TaskDeclaration& right);
    struct SystemVerilogStringValue {
        std::string bytes;
        frontend::SourceSpan source;
        [[nodiscard]] std::string display() const;
        [[nodiscard]] std::string canonical() const;
        [[nodiscard]] Expression expression(
            const frontend::SourceSpan& use_span) const;
    };
    using SystemVerilogStringEnvironment = std::unordered_map<std::string, SystemVerilogStringValue>;
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

    class ConstantFunctionMemoizationScope final {
    public:
        ConstantFunctionMemoizationScope();
        ConstantFunctionMemoizationScope(
            const ConstantFunctionMemoizationScope&) = delete;
        ConstantFunctionMemoizationScope& operator=(
            const ConstantFunctionMemoizationScope&) = delete;
        ~ConstantFunctionMemoizationScope();
    };

    void fold_systemverilog_constant_functions(
        DesignUnit& unit,
        const SystemVerilogConstantEnvironment& environment,
        const ConstantEnvironment& fallback_environment,
        std::vector<Diagnostic>& diagnostics);
    void fold_systemverilog_constant_functions(
        frontend::GenerateBody& body,
        const std::vector<frontend::FunctionDeclaration>& functions,
        const SystemVerilogConstantEnvironment& environment,
        const ConstantEnvironment& fallback_environment);
    std::optional<SystemVerilogConstantValue>
    convert_systemverilog_parameter_value(
        const SystemVerilogConstantValue& value,
        const frontend::Type& type,
        std::string& error);

    [[nodiscard]] bool is_systemverilog_nominal_packed_type(
        const frontend::Type& type) noexcept;
    std::optional<PackedLogic4>
    evaluate_systemverilog_packed_constant(
        const Expression& expression,
        const frontend::Type& type,
        const SystemVerilogConstantEnvironment& environment,
        const ConstantEnvironment& fallback_environment,
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

    void expand_systemverilog_lets(
        DesignUnit& unit,
        std::vector<Diagnostic>& diagnostics);
    [[nodiscard]] bool is_two_state_domain(
        const frontend::ValueDomain domain) noexcept;
#include "elaborator_value_helpers.hpp"

    void prepare_systemverilog_generate_regions(
        std::vector<frontend::GenerateRegion>& regions,
        const SystemVerilogConstantEnvironment& environment,
        const SystemVerilogStringEnvironment& string_environment,
        const ConstantEnvironment& integer_environment,
        const ConstantDomainEnvironment& domains,
        const std::vector<frontend::FunctionDeclaration>& functions,
        std::vector<Diagnostic>& diagnostics);
    void prepare_systemverilog_generate_body(
        frontend::GenerateBody& body,
        const ConstantEnvironment& integer_environment,
        const ConstantDomainEnvironment& domains,
        const std::vector<frontend::FunctionDeclaration>& functions,
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
        std::string nominal_type = { });

    bool fold_vhdl_enumeration_attributes(
        Expression& expression,
        const DesignUnit& unit,
        const ConstantEnvironment& environment,
        std::string& error,
        bool& range_error);

    bool fold_vhdl_static_expressions(
        Expression& expression,
        const DesignUnit& unit,
        const ConstantEnvironment& environment,
        std::string& error,
        bool& range_error);

    void fold_vhdl_static_type_expressions(
        DesignUnit& unit,
        const ConstantEnvironment& environment,
        std::vector<Diagnostic>& diagnostics);

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
        frontend::SignalDeclaration& declaration,
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

    using QualifiedIdentifierMap = std::unordered_map<std::string, frontend::SourceSpan>;

    struct NamedTypeBinding {
        frontend::Type type;
        std::string owner;
        bool interface_formal { };
    };

    using NamedTypeEnvironment = std::unordered_map<std::string, NamedTypeBinding>;

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
        const frontend::GenerateBody& body,
        QualifiedIdentifierMap& identifiers);

    void collect_vhdl_local_qualified_identifiers(
        const frontend::FunctionDeclaration& function,
        QualifiedIdentifierMap& identifiers);

    void collect_vhdl_local_qualified_identifiers(
        const frontend::ProcedureDeclaration& procedure,
        QualifiedIdentifierMap& identifiers);

    void collect_vhdl_local_qualified_identifiers(
        const frontend::Process& process,
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
        Statements& statements, Visitor& visitor)
    {
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

    template <typename Region, typename Visitor>
    void visit_local_region_types(
        Region& region, Visitor& visitor)
    {
        for (auto& alias : region.type_aliases) {
            visitor(alias.type);
        }
        for (auto& alias : region.signal_aliases) {
            visitor(alias.type);
        }
        for (auto& constant : region.constants) {
            visitor(constant.type);
        }
        for (auto& variable : region.variables) {
            visitor(variable.type);
        }
        for (auto& function : region.functions) {
            visitor(function.return_type);
            for (auto& argument : function.arguments) {
                visitor(argument.type);
            }
            visit_local_region_types(function, visitor);
        }
        for (auto& procedure : region.procedures) {
            for (auto& argument : procedure.arguments) {
                visitor(argument.type);
            }
            visit_local_region_types(procedure, visitor);
        }
        for (auto& package : region.package_instances) {
            for (auto& actual : package.generic_map) {
                if (actual.type_value) {
                    visitor(*actual.type_value);
                }
            }
        }
        visit_statement_types(region.statements, visitor);
    }

    template <typename Body, typename Visitor>
    void visit_generate_body_types(
        Body& body, Visitor& visitor)
    {
        for (auto& alias : body.type_aliases) {
            visitor(alias.type);
        }
        for (auto& constant : body.constants) {
            visitor(constant.type);
        }
        for (auto& signal : body.signals) {
            visitor(signal.type);
        }
        for (auto& alias : body.signal_aliases) {
            visitor(alias.type);
        }
        for (auto& function : body.functions) {
            visitor(function.return_type);
            for (auto& argument : function.arguments) {
                visitor(argument.type);
            }
            visit_local_region_types(function, visitor);
        }
        for (auto& task : body.tasks) {
            for (auto& argument : task.arguments) {
                visitor(argument.type);
            }
            for (auto& variable : task.variables) {
                visitor(variable.type);
            }
            visit_statement_types(task.statements, visitor);
        }
        for (auto& procedure : body.procedures) {
            for (auto& argument : procedure.arguments) {
                visitor(argument.type);
            }
            visit_local_region_types(procedure, visitor);
        }
        const auto visit_generic_parameters =
            [&](auto& parameters) {
                for (auto& parameter : parameters) {
                    visitor(parameter.type);
                    if (parameter.default_type) {
                        visitor(*parameter.default_type);
                    }
                    if (parameter.function_profile) {
                        visitor(parameter.function_profile->return_type);
                        for (auto& argument :
                            parameter.function_profile->arguments) {
                            visitor(argument.type);
                        }
                    }
                    if (parameter.procedure_profile) {
                        for (auto& argument :
                            parameter.procedure_profile->arguments) {
                            visitor(argument.type);
                        }
                    }
                }
            };
        for (auto& generic : body.generic_function_templates) {
            visit_generic_parameters(generic.generic_parameters);
            visitor(generic.function.return_type);
            for (auto& argument : generic.function.arguments) {
                visitor(argument.type);
            }
            for (auto& variable : generic.function.variables) {
                visitor(variable.type);
            }
            visit_statement_types(generic.function.statements, visitor);
        }
        for (auto& generic : body.generic_procedure_templates) {
            visit_generic_parameters(generic.generic_parameters);
            for (auto& argument : generic.procedure.arguments) {
                visitor(argument.type);
            }
            for (auto& variable : generic.procedure.variables) {
                visitor(variable.type);
            }
            visit_statement_types(generic.procedure.statements, visitor);
        }
        for (auto& package : body.package_instances) {
            for (auto& actual : package.generic_map) {
                if (actual.type_value) {
                    visitor(*actual.type_value);
                }
            }
        }
        for (auto& component :
            body.vhdl_component_declarations) {
            for (auto& generic : component.generics) {
                visitor(generic.type);
            }
            for (auto& port : component.ports) {
                visitor(port.type);
            }
        }
        for (auto& process : body.processes) {
            visit_local_region_types(process, visitor);
        }
        visit_generate_types(
            body.generate_regions, visitor);
    }

    template <typename Regions, typename Visitor>
    void visit_generate_types(
        Regions& regions, Visitor& visitor)
    {
        for (auto& region : regions) {
            for (auto& generic : region.block_generics) {
                visitor(generic.type);
            }
            for (auto& port : region.block_ports) {
                visitor(port.type);
            }
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
        const bool include_aliases = false)
    {
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
        for (auto& alias : unit.signal_aliases) {
            visitor(alias.type);
        }
        for (auto& component :
            unit.vhdl_component_declarations) {
            for (auto& generic : component.generics) {
                visitor(generic.type);
            }
            for (auto& port : component.ports) {
                visitor(port.type);
            }
        }
        visit_statement_types(
            unit.concurrent_statements, visitor);
        for (auto& function : unit.functions) {
            visitor(function.return_type);
            for (auto& argument : function.arguments) {
                visitor(argument.type);
            }
            visit_local_region_types(function, visitor);
        }
        for (auto& procedure : unit.procedures) {
            for (auto& argument : procedure.arguments) {
                visitor(argument.type);
            }
            visit_local_region_types(procedure, visitor);
        }
        for (auto& process : unit.processes) {
            visit_local_region_types(process, visitor);
        }
        visit_generate_types(
            unit.generate_regions, visitor);
    }

    std::string generated_scope(
        const std::string_view parent_scope,
        const std::string_view local_scope);

    using GeneratedNameEnvironment = std::unordered_map<std::string, std::string>;

    using VhdlBlockInterfacePreparer = std::function<bool(
        frontend::GenerateRegion&,
        frontend::GenerateBody&,
        const ConstantEnvironment&,
        const ConstantDomainEnvironment&,
        std::string_view,
        GeneratedNameEnvironment&,
        DesignUnit&,
        std::vector<Diagnostic>&)>;

    void qualify_generated_expression(
        Expression& expression,
        const GeneratedNameEnvironment& names);
    void qualify_generated_type(
        frontend::Type& type,
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

    bool prepare_vhdl_block_interface(
        const frontend::GenerateRegion& region,
        frontend::GenerateBody& body,
        const GeneratedNameEnvironment& visible_names,
        std::vector<Diagnostic>& diagnostics);

    void evaluate_generated_constants(
        frontend::GenerateBody& body,
        ConstantEnvironment& environment,
        ConstantDomainEnvironment& domains,
        const frontend::Language language,
        const std::vector<frontend::FunctionDeclaration>& functions,
        std::vector<Diagnostic>& diagnostics);

    void append_generated_body(
        frontend::GenerateBody body,
        const ConstantEnvironment& environment,
        const ConstantDomainEnvironment& domains,
        const frontend::Language language,
        const std::string_view scope,
        const GeneratedNameEnvironment& visible_names,
        DesignUnit& unit,
        std::vector<Diagnostic>& diagnostics,
        const VhdlBlockInterfacePreparer* block_preparer = nullptr);

    void expand_generate_regions(
        std::vector<frontend::GenerateRegion>& generates,
        const ConstantEnvironment& environment,
        const ConstantDomainEnvironment& domains,
        const frontend::Language language,
        const std::string_view parent_scope,
        const GeneratedNameEnvironment& visible_names,
        DesignUnit& unit,
        std::vector<Diagnostic>& diagnostics,
        const VhdlBlockInterfacePreparer* block_preparer = nullptr);

    struct PackageBinding {
        std::string template_name;
        DesignUnit unit;
        ConstantEnvironment environment;
        std::vector<std::pair<std::string, std::string>> values;
        std::vector<std::pair<std::string, std::string>> identity_values;
    };

    using PackageEnvironment = std::unordered_map<std::string, PackageBinding>;

    struct SpecializedUnit {
        DesignUnit unit;
        ConstantEnvironment environment;
        ConstantDomainEnvironment domains;
        SystemVerilogConstantEnvironment integral_environment;
        SystemVerilogStringEnvironment string_environment;
        SystemVerilogScalarConstantEnvironment scalar_environment;
        std::vector<std::pair<std::string, std::string>> values;
        std::vector<std::pair<std::string, std::string>> identity_values;
        PackageEnvironment packages;
    };

    struct InterfaceTypeSpecialization {
        DesignUnit unit;
        std::vector<frontend::ParameterOverride> value_overrides;
        std::vector<std::pair<std::string, std::string>> values;
        bool applied { };
    };

    NamedTypeEnvironment local_vhdl_type_environment(
        const DesignUnit& unit);

    std::string vhdl_type_identity(
        const frontend::Type& type);

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

    InterfaceTypeSpecialization specialize_vhdl_interface_types(
        DesignUnit&& source,
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

    InterfaceTypeSpecialization specialize_systemverilog_type_parameters(
        DesignUnit&& source,
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
        std::vector<Diagnostic>& diagnostics,
        bool expand_generates = true,
        const SystemVerilogConstantEnvironment&
            parent_integral_environment = { },
        const std::vector<frontend::FunctionDeclaration>&
            parent_functions = { });

    void expand_specialized_unit_generates(
        SpecializedUnit& specialized,
        std::vector<Diagnostic>& diagnostics,
        const VhdlBlockInterfacePreparer* block_preparer = nullptr);

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

    using SystemVerilogContainerConstantEvaluator = std::function<std::optional<std::int64_t>(
        const frontend::Expression&)>;
    using SystemVerilogContainerReporter = std::function<void(
        std::string,
        std::string,
        frontend::SourceSpan)>;

    [[nodiscard]] std::optional<runtime::simir::ContainerType>
    materialize_systemverilog_container_type(
        const frontend::Type& type,
        const frontend::SourceSpan& span,
        const SystemVerilogContainerConstantEvaluator& evaluate,
        const SystemVerilogContainerReporter& report);

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
        const std::unordered_set<SignalId>& read_only_signals,
        const std::unordered_map<std::string, StringObjectId>&
            string_objects,
        const std::unordered_set<StringObjectId>&
            read_only_string_objects,
        const std::unordered_map<std::string, ContainerObjectId>&
            container_objects,
        const std::unordered_set<std::string>&
            read_only_container_objects,
        const std::unordered_map<
            std::string, const frontend::Type*>& visible_types,
        const std::unordered_map<
            std::string, const frontend::Type*>& visible_type_marks,
        const std::vector<frontend::FunctionDeclaration>& functions,
        const std::vector<frontend::TaskDeclaration>& tasks,
        const std::vector<frontend::ProcedureDeclaration>& procedures,
        frontend::SystemVerilogScalarEvaluationContext scalar_context,
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
    Process lower_concurrent_group(
        std::span<const Statement> statements,
        frontend::Language language,
        const std::string& name,
        std::size_t order);
    [[nodiscard]] std::vector<SignalId> concurrent_sensitivity(
        const Statement& statement) const;
    [[nodiscard]] bool concurrent_trigger_fusion_safe(
        const Statement& statement) const;

    [[nodiscard]] std::vector<Process> take_generated_processes();

    void set_systemverilog_program_owner(
        std::optional<std::uint32_t> owner) noexcept;
    void set_vhdl_standard(frontend::VhdlStandard standard) noexcept;
    void set_vhdl_synopsys_numeric_context(
        bool signed_visible, bool unsigned_visible) noexcept;

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

    [[nodiscard]] const frontend::Type*
    systemverilog_expression_type(
        const Expression& expression) const;

    [[nodiscard]] bool validate_sv_nominal_assignment(
        const frontend::Type* target_type,
        const Expression& value);

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

    [[nodiscard]] std::string debug_scope_name() const;
    [[nodiscard]] std::string vhdl_statement_scope_name(
        const Statement&) const;
    void lower_case_alternative(const frontend::CaseAlternative&);

    void lower_block(const Statement& statement);

    void lower_fork(const Statement& statement);

    void lower_event_trigger(const Statement& statement);
    void lower_wait_order(const Statement& statement);

    static const Statement* recognized_vhdl_edge_guard(
        const frontend::Process& source);

    void lower_statements(const std::vector<Statement>& statements);

    void lower_statement(const Statement& statement);

    [[nodiscard]] std::pair<
        std::vector<SignalId>,
        std::vector<runtime::simir::EdgeKind>>
    resolve_wait_sensitivities(
        const Statement& statement);

    [[nodiscard]] bool emit_event_control_wait(
        const Statement& statement);

    [[nodiscard]] bool emit_single_event_control_wait(
        const Statement& statement);

    void emit_debug_point(
        const DebugPointKind kind,
        const frontend::SourceSpan& span);
    [[nodiscard]] bool lower_delay_wait(
        const frontend::Delay& delay,
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
        std::size_t offset { };
        std::size_t width { };
    };
    std::optional<DynamicIndex> lower_dynamic_index(
        const Expression& source, const Expression& index,
        std::size_t source_width, std::uint32_t base_offset,
        const frontend::SourceSpan& span);
    std::optional<DynamicPartIndex> lower_vhdl_dynamic_slice(
        const Expression& expression, std::size_t source_width,
        std::size_t selected_width, std::uint32_t base_offset);
    std::optional<RegisterId> lower_vhdl_dynamic_slice_expression(
        const Expression& expression, std::size_t source_width,
        std::size_t selected_width);
    std::optional<ConstantSliceSelection>
    constant_slice_selection(
        const Expression& expression,
        const std::size_t source_width);
    std::optional<RegisterId> lower_procedural_update_value(
        const Statement& statement,
        RegisterId captured,
        std::size_t target_width,
        const frontend::Type* contextual_target_type);
    std::optional<RegisterId> lower_procedural_update_expression(
        const Expression& expression,
        std::size_t expected_width,
        const frontend::Type* expected_type);
    void lower_force_release(const Statement& statement);
    void lower_concatenated_force_release(const Statement& statement);
    void lower_procedural_continuous_assignment(const Statement& statement);
    void prepare_procedural_continuous_assignments(
        const std::vector<Statement>& statements);
    void materialize_procedural_continuous_assignments();
    void lower_assignment(const Statement& statement);
    void lower_concatenated_assignment(const Statement& statement);
    bool lower_class_assignment(const Statement& statement);
    bool lower_assignment_selections(
        const Statement& statement,
        std::string_view target_name,
        std::size_t whole_width,
        const std::vector<const Expression*>& selections,
        std::uint32_t& selected_offset,
        bool& has_selected_offset,
        std::optional<std::size_t>& selected_width,
        std::optional<frontend::ValueDomain>& selected_domain,
        std::optional<DynamicIndex>& dynamic_selection,
        std::optional<DynamicPartIndex>& dynamic_part_selection,
        std::optional<frontend::Type>& selected_type);
    void lower_if(const Statement& statement);
    void lower_case(const Statement& statement);
    void lower_qualified_case(const Statement& statement);
    [[nodiscard]] bool validate_vhdl_matching_case(
        const Statement& statement,
        const frontend::Type* selector_type,
        std::size_t selector_width,
        frontend::ValueDomain selector_domain);
    [[nodiscard]] bool validate_vhdl_case_choices(
        const Statement& statement,
        const frontend::Type* selector_type,
        std::size_t selector_width,
        frontend::ValueDomain selector_domain);
    [[nodiscard]] std::optional<std::int64_t> vhdl_case_choice_ordinal(
        const Expression& expression,
        const frontend::Type* selector_type,
        frontend::ValueDomain selector_domain);
    std::optional<RegisterId> lower_vhdl_case_range_condition(
        const Expression& choice,
        RegisterId selector,
        const frontend::Type* selector_type,
        bool selector_signed);
    [[nodiscard]] bool is_bounded_case_pattern_constant(
        const Expression& expression) const;
    void lower_loop(const Statement& statement);
    void lower_runtime_loop(const Statement& statement);

    void lower_runtime_for(const Statement& statement);

    void lower_runtime_repeat(const Statement& statement);

    void lower_loop_control(
        const Statement& statement, const bool is_break);

    struct PackedMemberReference {
        struct UnionContext {
            std::uint64_t payload_offset { };
            std::uint64_t payload_width { };
            std::uint64_t member_width { };
            std::uint64_t tag_offset { };
            std::uint64_t tag_width { };
            std::uint64_t tag { };
        };

        std::string base;
        const frontend::PackedMember* member { };
        std::uint64_t lsb_offset { };
        std::vector<UnionContext> unions;
    };

    std::optional<PackedMemberReference> packed_member_reference(
        const std::string_view name) const;
    [[nodiscard]] RegisterId widen_enumeration_ordinal(
        const RegisterId source);
    [[nodiscard]] RegisterId narrow_enumeration_ordinal(
        const RegisterId source,
        const frontend::Type& type);
    std::optional<RegisterId> lower_vhdl_scalar_attribute(
        const Expression& expression,
        const frontend::Type* expected_type);
    std::optional<RegisterId> lower_vhdl_array_aggregate(
        const Expression& expression,
        const std::size_t expected_width,
        const frontend::Type& expected_type);

    struct ExpressionAttempt {
        ExpressionAttempt();
        ExpressionAttempt(RegisterId result);
        ExpressionAttempt(std::optional<RegisterId> result);
        ExpressionAttempt(std::nullopt_t);

        bool handled { };
        std::optional<RegisterId> value;
    };
    [[nodiscard]] std::optional<frontend::Type>
    vhdl_expression_type(const Expression& expression) const;
    ExpressionAttempt lower_vhdl_array_selection_expression(
        const Expression&, std::size_t, const frontend::Type*);
    ExpressionAttempt lower_vhdl_access_expression(
        const Expression&, std::size_t, const frontend::Type*);
    ExpressionAttempt lower_vhdl_protected_expression(
        const Expression&, std::size_t, const frontend::Type*);
    ExpressionAttempt lower_vhdl_physical_expression(
        const Expression&, std::size_t, const frontend::Type*);
    bool lower_vhdl_protected_procedure_call(const Statement&);
    bool lower_vhdl_file_procedure_call(const Statement&);
    bool lower_vhdl_textio_procedure_call(const Statement&);
    bool lower_vhdl_vital_delay_call(const Statement&);
    bool lower_vhdl_vital_state_table_call(const Statement&);
    bool lower_vhdl_vital_procedure_call(const Statement&);
    bool lower_vhdl_access_assignment(const Statement&);
    bool lower_vhdl_access_deallocation(const Statement&);
    struct VhdlAccessHeap {
        ContainerRegisterId objects { };
        ContainerRegisterId issued_handles { };
        std::uint64_t maximum_live_objects { };
    };
    VhdlAccessHeap* vhdl_access_heap(
        const frontend::Type&, const frontend::SourceSpan&);
    std::optional<RegisterId> vhdl_access_index(const Expression&, const frontend::Type&);
    bool validate_vhdl_access_handle(
        const VhdlAccessHeap&, RegisterId, const frontend::SourceSpan&);
    ExpressionAttempt lower_vhdl_conversion_expression(
        const Expression&, std::size_t, const frontend::Type*);
    ExpressionAttempt lower_vhdl_composite_expression(const Expression&,
        std::size_t, const frontend::Type*);
    bool validate_vhdl_composite_assignment(const frontend::Type*,
        const Expression&);
    std::optional<RegisterId> lower_expression(
        const Expression& expression,
        const std::size_t expected_width,
        const frontend::Type* expected_type = nullptr);
    std::optional<RegisterId> lower_sv_packed_pattern(
        const Expression& expression,
        std::size_t expected_width,
        const frontend::Type& expected_type);
    struct InsideIntegralOperand {
        RegisterId value { };
        std::size_t width { };
        bool signed_value { };
    };
    struct SizedIntegralComparison {
        RegisterId lhs { };
        RegisterId rhs { };
        bool signed_value { };
    };
    struct CasePatternBinding {
        std::string name;
        RegisterId value { };
        bool signed_value { };
        std::optional<frontend::PackedRange> packed_range;
        std::optional<frontend::IntegerRange> integer_range;
        std::vector<frontend::PackedMember> members;
        const frontend::Type* type { };
    };
    struct CasePatternMatch {
        RegisterId condition { };
        std::vector<CasePatternBinding> bindings;
    };
    std::optional<InsideIntegralOperand> lower_inside_integral_operand(
        const Expression& expression,
        std::string_view diagnostic_code,
        std::string_view diagnostic_message);
    SizedIntegralComparison size_integral_comparison(
        InsideIntegralOperand lhs,
        InsideIntegralOperand rhs);
    std::optional<CasePatternMatch> lower_case_match_pattern(
        const Expression& pattern,
        RegisterId value,
        std::size_t width,
        bool signed_value,
        const frontend::Type* type);
    ExpressionAttempt lower_membership_expression(
        const Expression& expression);
    std::optional<std::vector<runtime::SystemVerilogConstraintTemplate>>
    lower_inline_constraints(const Expression& expression);
    ExpressionAttempt lower_class_expression(
        const Expression& expression,
        std::size_t expected_width,
        const frontend::Type* expected_type);
    ExpressionAttempt lower_process_expression(
        const Expression& expression);
    ExpressionAttempt lower_synchronization_expression(
        const Expression& expression,
        std::size_t expected_width,
        const frontend::Type* expected_type);
    bool lower_process_method_statement(
        const Statement& statement);
    bool lower_synchronization_method_statement(
        const Statement& statement);

    std::optional<StringRegisterId> lower_string_expression(
        const Expression& expression);
    bool lower_string_method_statement(const Statement& statement);
    std::optional<StringRegisterId> lower_string_format(
        const std::vector<Expression>& arguments,
        std::size_t format_index,
        std::string_view call_name,
        const frontend::SourceSpan& span,
        std::optional<frontend::OutputFormat> default_format = std::nullopt);
    bool lower_string_format_task(const Statement& statement);
    ExpressionAttempt lower_file_scan(const Expression& expression);
    ExpressionAttempt lower_file_binary_read(const Expression& expression);
    std::optional<ContainerRegisterId> lower_container_expression(
        const Expression& expression);
    std::optional<ContainerRegisterId>
    lower_user_container_function_expression(
        const Expression& expression);
    [[nodiscard]] bool
    is_static_container_slice_candidate(
        const Expression& expression) const;
    struct StaticContainerSlice {
        ContainerType base_type;
        ContainerType selected_type;
    };
    struct LoweredStaticContainer {
        ContainerRegisterId value { };
        ContainerType type;
    };
    std::optional<StaticContainerSlice>
    static_container_slice(
        const Expression& expression);
    std::optional<LoweredStaticContainer>
    lower_static_container_value(
        const Expression& expression);
    std::optional<ContainerRegisterId>
    lower_static_container_assignment_value(
        const Expression& expression,
        const ContainerType& destination_type);
    void lower_nonstatic_container_assignment(
        ContainerRegisterId target,
        const Expression& expression,
        const ContainerType& destination_type);
    bool lower_static_container_slice_assignment(
        const Expression& target_expression,
        const Expression& value_expression,
        ContainerRegisterId target,
        const ContainerType& target_type);
    void copy_static_container_ordinals(
        ContainerRegisterId destination,
        const ContainerType& destination_range,
        ContainerRegisterId source,
        const ContainerType& source_range);
    [[nodiscard]] bool is_container_expression(
        const Expression& expression) const;
    [[nodiscard]] const frontend::Type*
    container_expression_type(
        const Expression& expression) const;
    [[nodiscard]] std::optional<ContainerType>
    container_expression_runtime_type(
        const Expression& expression);
    [[nodiscard]] std::optional<ContainerType> container_type(
        const frontend::Type& type,
        const frontend::SourceSpan& span);
    ExpressionAttempt lower_container_query(
        const Expression& expression);
    std::optional<ContainerRegisterId> lower_container_pattern(
        const Expression& expression,
        const frontend::Type& source_type,
        const ContainerType& runtime_type);
    std::optional<ContainerRegisterId>
    lower_multidimensional_container_pattern(
        const Expression& expression,
        const frontend::Type& source_type,
        const ContainerType& runtime_type);
    [[nodiscard]] std::optional<ContainerType>
    multidimensional_container_subarray_type(
        const Expression& expression);
    std::optional<ContainerRegisterId>
    lower_multidimensional_container_subarray(
        const Expression& expression);
    void copy_multidimensional_container_elements(
        ContainerRegisterId destination,
        RegisterId destination_base,
        ContainerRegisterId source,
        RegisterId source_base,
        const ContainerType& selected_type);
    bool lower_container_locator(
        const Expression& expression,
        ContainerRegisterId destination,
        const ContainerType& destination_type);
    enum class ContainerExpressionPurpose : std::uint8_t {
        predicate,
        reduction_transformation,
        ordering_key,
        locator_transformation,
    };
    std::optional<std::vector<ContainerPredicateNode>>
    lower_container_expression_graph(
        const Expression& expression,
        std::string_view iterator_name,
        const frontend::Type& source_type,
        const ContainerType& runtime_type,
        ContainerExpressionPurpose purpose);
    ContainerRegisterId allocate_container_register(
        const ContainerType& type);
    void lower_container_method(const Statement& statement);

    [[nodiscard]] bool is_string_expression(
        const Expression& expression) const;

    StringRegisterId allocate_string_register();

    ExpressionAttempt lower_primary_expression(
        const Expression& expression,
        std::size_t expected_width,
        const frontend::Type* expected_type);
    ExpressionAttempt lower_multidimensional_container_read(
        const Expression& expression);
    struct UnpackedAggregateMemberReference {
        const frontend::Type* container_type { };
        const frontend::Type* leaf_type { };
        std::vector<std::uint32_t> members;
    };
    [[nodiscard]] std::optional<UnpackedAggregateMemberReference>
    unpacked_aggregate_member_reference(
        const Expression& expression) const;
    ExpressionAttempt lower_unpacked_aggregate_member_read(
        const Expression& expression);
    std::optional<ContainerRegisterId>
    lower_unpacked_aggregate_pattern(
        const Expression& expression,
        const frontend::Type& source_type,
        const ContainerType& runtime_type);
    bool lower_unpacked_aggregate_pattern_value(
        const Expression& expression,
        const frontend::Type& aggregate_type,
        ContainerRegisterId target,
        RegisterId index,
        std::vector<std::uint32_t> members,
        bool signed_index,
        bool linear_index);
    bool lower_unpacked_aggregate_assignment(
        const Statement& statement,
        const frontend::Type& type,
        ContainerRegisterId target,
        std::optional<ContainerObjectId> object);
    bool lower_multidimensional_container_assignment(
        const Statement& statement,
        const frontend::Type& type,
        ContainerRegisterId target,
        std::optional<ContainerObjectId> object);
    std::optional<RegisterId>
    lower_multidimensional_index(
        const Expression& expression,
        const frontend::Type& type,
        bool require_complete = true);
    ExpressionAttempt lower_unary_attribute_expression(
        const Expression& expression,
        std::size_t expected_width,
        const frontend::Type*);
    ExpressionAttempt lower_system_function_expression(
        const Expression&, std::size_t, const frontend::Type*);
    ExpressionAttempt lower_vhdl_logic_function_expression(const Expression&, std::size_t, const frontend::Type*);
    ExpressionAttempt lower_vhdl_vital_expression(const Expression&, std::size_t, const frontend::Type*);
    ExpressionAttempt lower_vhdl_vital_memory_expression(const Expression&, std::size_t, const frontend::Type*);
    ExpressionAttempt lower_vhdl_fixed_function_expression(const Expression&, std::size_t, const frontend::Type*);
    ExpressionAttempt lower_vhdl_float_function_expression(const Expression&, std::size_t, const frontend::Type*);
    ExpressionAttempt lower_vhdl_numeric_function_expression(const Expression&, std::size_t, const frontend::Type*);
    ExpressionAttempt lower_user_function_expression(const Expression& expression,
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

    [[nodiscard]] bool is_synopsys_std_logic_vector_expression(
        const Expression& expression) const;

    [[nodiscard]] bool is_integer_expression(
        const Expression& expression) const;

    [[nodiscard]] bool is_file_handle_expression(
        const Expression& expression) const;

    [[nodiscard]] const frontend::FunctionDeclaration*
    visible_function(std::string_view name) const;

    enum class FunctionResultKind : std::uint8_t {
        Packed,
        String,
        Container,
    };

    struct CallableSelection {
        bool named { };
        std::optional<std::size_t> index;
    };

    [[nodiscard]] CallableSelection select_function_overload(
        const Expression& expression, const frontend::Type* expected_type,
        FunctionResultKind result_kind);

    [[nodiscard]] CallableSelection select_procedure_overload(
        const Statement& statement);

    [[nodiscard]] bool vhdl_callable_type_matches(
        const frontend::Type& formal,
        const frontend::Type& actual) const;

    [[nodiscard]] bool vhdl_expression_matches_type(
        const Expression& expression,
        const frontend::Type& formal) const;

    [[nodiscard]] bool vhdl_function_profile_matches(
        const Expression& expression,
        const frontend::FunctionDeclaration& function,
        const frontend::Type* expected_type) const;

    void initialize_function_support();

    std::optional<std::vector<const Expression*>>
    bind_function_actuals(
        const Expression& expression,
        const frontend::FunctionDeclaration& function);

    bool validate_function_reference_actuals(
        const frontend::FunctionDeclaration& function,
        const std::vector<const Expression*>& actuals);

    void lower_pending_functions();

    void lower_function_body(std::size_t function_index);

    void lower_function_return(const Statement& statement);

    void initialize_task_support();
    void collect_class_tasks(std::span<const Statement> statements);

    std::optional<std::vector<const Expression*>> bind_task_actuals(
        const Statement& statement,
        const frontend::TaskDeclaration& task);

    void lower_callable_copy_out(
        const Expression& target,
        const frontend::Type& type,
        RegisterId value,
        StringRegisterId string_value,
        ContainerRegisterId container_value,
        bool is_string,
        bool is_container,
        std::string temporary);
    std::optional<Expression> capture_callable_copy_out_target(
        const Expression& target,
        std::string temporary_prefix);

    struct CallableVariableRegister;
    std::unordered_map<std::string, CallableVariableRegister>
    allocate_static_callable_variables(
        const std::vector<frontend::VariableDeclaration>& variables,
        const std::vector<frontend::Statement>& statements,
        std::string_view callable_name);

    void bind_static_callable_variables(
        const std::vector<frontend::VariableDeclaration>& variables,
        const std::unordered_map<
            std::string, CallableVariableRegister>& registers);

    void lower_task_call(const Statement& statement);
    [[nodiscard]] bool lower_pla_task(const Statement& statement);
    void lower_pending_tasks();

    void lower_task_body(std::size_t task_index);

    void lower_task_return(const Statement& statement);

    void initialize_procedure_support();
    void lower_procedure_call(const Statement& statement);
    void lower_pending_procedures();
    void lower_procedure_body(std::size_t procedure_index);
    void lower_procedure_return(const Statement& statement);
    [[nodiscard]] bool procedure_dependencies_suspend(
        const std::unordered_set<std::size_t>& roots) const;

    void validate_read_only_signal_writes(
        const frontend::SourceSpan& source);
    void collect_identifiers(
        const Expression& expression,
        std::set<std::string>& output) const;
    void collect_statement_identifiers(
        std::span<const Statement> statements,
        std::set<std::string>& output) const;
    void collect_wildcard_identifiers(
        std::span<const Statement> statements,
        std::set<std::string>& output) const;
    RegisterId allocate_register(
        const std::size_t width,
        const frontend::ValueDomain domain);

    [[nodiscard]] std::size_t register_width(const RegisterId id) const;

    [[nodiscard]] frontend::ValueDomain register_domain(
        const RegisterId id) const;

    [[nodiscard]] std::optional<SignalId> vhdl_implicit_signal_attribute(
        SignalId source, std::string_view attribute,
        runtime::SimulationTick duration, frontend::SourceSpan span);
    void validate_vhdl_driver_attributes(frontend::SourceSpan span);
    [[nodiscard]] RegisterId resize_register(
        RegisterId source,
        std::size_t width,
        bool sign_extend);
    [[nodiscard]] RegisterId convert_to_two_state(RegisterId source);
    void report(std::string code, std::string message,
        frontend::SourceSpan span);
    [[nodiscard]] bool report_unsupported_cross_root_reference(
        std::string_view name,
        frontend::SourceSpan span);
    ElaboratedDesign& design_;
    const std::unordered_map<std::string, SignalId>& signals_;
    const std::unordered_set<SignalId>& read_only_signals_;
    const std::unordered_map<std::string, StringObjectId>&
        string_objects_;
    const std::unordered_set<StringObjectId>&
        read_only_string_objects_;
    const std::unordered_map<std::string, ContainerObjectId>&
        container_objects_;
    const std::unordered_set<std::string>&
        read_only_container_objects_;
    const std::unordered_map<
        std::string, const frontend::Type*>& visible_types_;
    const std::unordered_map<
        std::string, const frontend::Type*>& visible_type_marks_;
    const std::vector<frontend::FunctionDeclaration>& functions_;
    const std::vector<frontend::TaskDeclaration>& tasks_;
    const std::vector<frontend::ProcedureDeclaration>& procedures_;
    frontend::SystemVerilogScalarEvaluationContext scalar_context_;
    std::vector<Diagnostic>& diagnostics_;
    Process process_;
    std::optional<std::uint32_t> systemverilog_program_owner_;
    std::vector<Process> generated_processes_;
    std::vector<SignalId> implicit_signal_dependencies_;
    RegisterId next_register_ { };
    StringRegisterId next_string_register_ { };
    ContainerRegisterId next_container_register_ { };
    std::vector<std::size_t> register_widths_;
    std::vector<frontend::ValueDomain> register_domains_;
    std::unordered_map<std::string, RegisterId> locals_;
    std::optional<RegisterId> procedural_update_result_;
    struct ProceduralContinuousAssignment {
        const Statement* source { };
        Expression target;
        Expression value;
        SignalId active { };
        std::string active_name;
        std::string target_key;
        bool valid { };
    };
    std::vector<ProceduralContinuousAssignment>
        procedural_continuous_assignments_;
    std::unordered_map<const Statement*, std::size_t>
        procedural_continuous_assignment_by_statement_;
    std::unordered_map<std::string, std::vector<std::size_t>>
        procedural_continuous_assignments_by_target_;
    std::unordered_map<std::string, StringRegisterId>
        string_locals_;
    std::unordered_map<std::string, ContainerRegisterId>
        container_locals_;
    std::unordered_map<std::string, VhdlAccessHeap> vhdl_access_heaps_;
    bool active_vhdl_protected_method_ { };
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
    struct BlockControlContext {
        std::string label;
        std::vector<InstructionIndex> disable_jumps;
        std::optional<InstructionIndex> fork_site;
    };
    std::vector<BlockControlContext> block_controls_;
    struct NamedBlockControl {
        std::string label;
        std::vector<std::string> scope;
        InstructionIndex begin { };
        std::optional<InstructionIndex> end;
        std::vector<InstructionIndex> disable_operations;
    };
    std::vector<NamedBlockControl> named_block_controls_;
    struct NamedForkControl {
        std::string label;
        std::vector<std::string> scope;
        InstructionIndex site { };
    };
    std::vector<NamedForkControl> named_fork_controls_;
    struct CallableVariableRegister {
        RegisterId packed { };
        StringRegisterId string { };
        ContainerRegisterId container { };
        bool is_string { };
        bool is_container { };
    };
    struct FunctionFrame {
        const frontend::FunctionDeclaration* source { };
        RegisterId result { };
        StringRegisterId string_result { };
        ContainerRegisterId container_result { };
        ContainerRegisterId container_result_default { };
        bool result_is_string { };
        bool result_is_container { };
        std::vector<RegisterId> arguments;
        std::vector<StringRegisterId> string_arguments;
        std::vector<ContainerRegisterId> container_arguments;
        std::vector<bool> argument_is_string;
        std::vector<bool> argument_is_container;
        std::uint32_t invocation_identity { };
        std::vector<RegisterId> invocation_packed;
        std::vector<StringRegisterId> invocation_strings;
        std::vector<ContainerRegisterId> invocation_containers;
        std::vector<InstructionIndex> invocation_push_sites;
        std::unordered_map<std::string, CallableVariableRegister>
            static_variables;
        std::optional<InstructionIndex> target;
        std::vector<InstructionIndex> call_sites;
        bool allocated { };
        bool queued { };
        bool lowered { };
        bool invocation_layout_finalized { };
    };
    std::deque<frontend::FunctionDeclaration>
        vhdl_function_specializations_;
    std::vector<FunctionFrame> function_frames_;
    std::unordered_map<std::string, std::vector<std::size_t>>
        function_indices_;
    std::unordered_map<std::string, std::size_t>
        vhdl_function_specialization_indices_;
    std::deque<std::size_t> pending_functions_;
    std::vector<std::unordered_set<std::size_t>>
        function_dependencies_;
    std::optional<std::size_t> active_function_;
    std::vector<InstructionIndex> function_return_jumps_;
    CallStack function_call_stack_;
    bool function_support_initialized_ { };
    struct TaskFrame {
        const frontend::TaskDeclaration* source { };
        std::vector<RegisterId> arguments;
        std::vector<StringRegisterId> string_arguments;
        std::vector<ContainerRegisterId> container_arguments;
        std::vector<std::optional<ContainerRegisterId>>
            container_output_defaults;
        std::vector<bool> argument_is_string;
        std::vector<bool> argument_is_container;
        std::uint32_t invocation_identity { };
        std::vector<RegisterId> invocation_packed;
        std::vector<StringRegisterId> invocation_strings;
        std::vector<ContainerRegisterId> invocation_containers;
        std::vector<InstructionIndex> invocation_push_sites;
        std::unordered_map<std::string, CallableVariableRegister>
            static_variables;
        std::optional<InstructionIndex> target;
        std::vector<InstructionIndex> call_sites;
        bool allocated { };
        bool queued { };
        bool lowered { };
        bool invocation_layout_finalized { };
    };
    std::vector<TaskFrame> task_frames_;
    std::deque<frontend::TaskDeclaration> class_tasks_;
    std::unordered_map<std::string, std::size_t> task_indices_;
    std::deque<std::size_t> pending_tasks_;
    std::vector<std::unordered_set<std::size_t>>
        task_dependencies_;
    std::vector<bool> task_suspending_;
    std::optional<std::size_t> active_task_;
    std::vector<InstructionIndex> task_return_jumps_;
    CallStack task_call_stack_;
    bool task_support_initialized_ { };
    std::uint32_t next_callable_invocation_identity_ { 1 };
    struct ProcedureFrame {
        const frontend::ProcedureDeclaration* source { };
        std::vector<RegisterId> arguments;
        std::uint32_t invocation_identity { };
        std::vector<RegisterId> invocation_packed;
        std::vector<InstructionIndex> invocation_push_sites;
        std::optional<InstructionIndex> target;
        std::vector<InstructionIndex> call_sites;
        bool allocated { };
        bool queued { };
        bool lowered { };
        bool invocation_layout_finalized { };
    };
    std::vector<ProcedureFrame> procedure_frames_;
    std::unordered_map<std::string, std::vector<std::size_t>>
        procedure_indices_;
    std::deque<std::size_t> pending_procedures_;
    std::vector<std::unordered_set<std::size_t>>
        procedure_dependencies_;
    std::vector<bool> procedure_suspending_;
    std::unordered_set<std::size_t>
        process_procedure_dependencies_;
    std::vector<std::unordered_set<std::size_t>>
        function_procedure_dependencies_;
    std::optional<std::size_t> active_procedure_;
    std::vector<InstructionIndex> procedure_return_jumps_;
    std::vector<RegisterId> procedure_file_handles_;
    CallStack procedure_call_stack_;
    bool procedure_support_initialized_ { };
    frontend::ProcessKind process_kind_ {
        frontend::ProcessKind::VhdlProcess
    };
    frontend::Language language_ { frontend::Language::Vhdl2008 };
    frontend::VhdlStandard vhdl_standard_ {
        frontend::VhdlStandard::Vhdl2008
    };
    bool vhdl_synopsys_signed_visible_ { };
    bool vhdl_synopsys_unsigned_visible_ { };
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
        SystemCFactoryProvider* systemc_provider,
        std::span<const std::string> search_libraries);

    void build(const DesignUnit& root);

    void build(const SystemCInstanceDescription& root);

    void add_root(const DesignUnit& root, std::string path);

    void add_root(
        const SystemCInstanceDescription& root,
        std::string path);

    /// Predeclare the packed signal surface of an HDL root before any root
    /// process is lowered. SystemVerilog permits a top-level instance name in
    /// a hierarchical reference (the conventional vendor `glbl` module is
    /// the motivating case), so all root surfaces must exist independently
    /// of manifest order.
    void predeclare_root_globals(
        const DesignUnit& root,
        std::string path);

    void finalize();

private:
    using SignalMap = std::unordered_map<std::string, SignalId>;
    using StringMap = std::unordered_map<std::string, StringObjectId>;
    using ContainerMap = std::unordered_map<std::string, ContainerObjectId>;
    using ObjectMap = std::unordered_map<std::uint64_t, SignalId>;

    using PortAliases = HierarchyPortAliases;
    using ContainerBoundaryDriver = HierarchyContainerBoundaryDriver;
    using ConfiguredVhdlInstance = HierarchyConfiguredVhdlInstance;

    struct HierarchyCheckpoint {
        std::string path;
        std::size_t diagnostics { };
        std::size_t signals { };
        std::size_t boundary_conversions { };
        std::size_t strings { };
        std::size_t containers { };
        std::size_t container_aliases { };
        std::size_t protected_objects { };
        std::size_t processes { };
        std::size_t specializations { };
        std::size_t udp_tables { };
        std::size_t specify_paths { };
        std::size_t timing_checks { };
        std::size_t systemc_instances { };
        std::size_t systemc_processes { };
        std::size_t systemc_objects { };
        std::size_t owned_systemc_instances { };
        std::size_t stack_depth { };
        std::size_t boundary_resolver_insertions { };
        std::size_t vhdl_resolution_kind_insertions { };
        std::size_t systemverilog_resolution_kind_insertions { };
        std::uint64_t next_interface_handle { };
    };

    [[nodiscard]] HierarchyCheckpoint hierarchy_checkpoint(
        std::string path) const;
    void rollback_hierarchy(const HierarchyCheckpoint& checkpoint);

    static std::vector<std::string> selected_name_parts(
        const std::string_view name);

    const DesignUnit* select_vhdl_configuration_root(
        const DesignUnit& configuration);

    const DesignUnit* select_systemverilog_configuration_root(
        const DesignUnit& configuration);

    std::string systemverilog_configuration_identity(
        const DesignUnit& configuration) const;

    struct ConfiguredSystemVerilogInstance {
        const DesignUnit* target { };
        const DesignUnit* referenced_configuration { };
        std::string configuration_identity;
        bool applied { };
        bool valid { true };
    };

    ConfiguredSystemVerilogInstance
    configure_systemverilog_instance(
        const DesignUnit& unit,
        const frontend::Instance& instance,
        const std::string& path);

    std::vector<frontend::Instance> systemverilog_bound_instances(
        const DesignUnit& unit,
        const std::string& path,
        const ConstantEnvironment& parameter_environment,
        const ConstantDomainEnvironment& parent_domains);

    void validate_systemverilog_extern_declarations();

    std::string vhdl_configuration_identity(
        const DesignUnit& configuration) const;

    ConfiguredVhdlInstance bind_vhdl_direct_configuration_instance(
        const DesignUnit& unit,
        const frontend::Instance& instance,
        const std::string& path);

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
        const ConstantEnvironment& parent_environment,
        const ConstantDomainEnvironment& parent_domains,
        const NamedTypeEnvironment& parent_types,
        const std::vector<frontend::FunctionDeclaration>&
            parent_functions,
        const std::vector<frontend::ProcedureDeclaration>&
            parent_procedures,
        const PackageEnvironment& parent_packages);

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
        const std::vector<frontend::ParameterOverride>& overrides = { },
        const ConstantEnvironment& parent_environment = { },
        const ConstantDomainEnvironment& parent_domains = { },
        const NamedTypeEnvironment& parent_types = { },
        const std::vector<frontend::FunctionDeclaration>&
            parent_functions = { },
        const std::vector<frontend::ProcedureDeclaration>&
            parent_procedures = { });

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

    void materialize_vhdl_local_declarations(
        SpecializedUnit& specialized);

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

    void validate_systemverilog_exports(
        const DesignUnit& declared_package,
        const DesignUnit& effective_package);

    void validate_verilog_specify(
        const DesignUnit& unit,
        const std::string& path,
        const SignalMap& signals,
        const ConstantEnvironment& parameter_environment);

    std::optional<runtime::simir::ModulePathExpression>
    compile_verilog_specify_expression(
        const frontend::Expression& expression,
        const SignalMap& signals,
        const ConstantEnvironment& parameter_environment,
        std::string_view role);

    void qualify_interface_callable(
        frontend::FunctionDeclaration& callable,
        std::string_view port,
        const frontend::DesignUnit& interface_unit);

    void qualify_interface_callable(
        frontend::TaskDeclaration& callable,
        std::string_view port,
        const frontend::DesignUnit& interface_unit);

    void resolve_named_types(
        DesignUnit& unit,
        const NamedTypeEnvironment& imported_types,
        const bool vhdl = false,
        const bool resolve_ports = true);
    void merge_vhdl_protected_types(DesignUnit&, const DesignUnit&);
    void materialize_vhdl_shared_variable(
        const frontend::VariableDeclaration&,
        const std::string& path,
        SignalMap& signals);

    DesignUnit effective_unit(
        const DesignUnit& selected,
        const DesignUnit* entity_override = nullptr);

    const DesignUnit& resolved_vhdl_entity_interface(
        const DesignUnit& entity);

    SpecializedUnit specialize_selected_unit(
        const DesignUnit& selected,
        const std::vector<frontend::ParameterOverride>& overrides,
        const ConstantEnvironment& parent_environment,
        const SystemVerilogConstantEnvironment&
            parent_integral_environment,
        const ConstantDomainEnvironment& parent_domains,
        const NamedTypeEnvironment& parent_types,
        const std::vector<frontend::FunctionDeclaration>& parent_functions,
        const std::vector<frontend::ProcedureDeclaration>& parent_procedures,
        const PackageEnvironment& parent_packages,
        const frontend::Language association_language);

    bool prepare_vhdl_block_nonvalue_interface(
        frontend::GenerateRegion& region,
        frontend::GenerateBody& body,
        const ConstantEnvironment& environment,
        const ConstantDomainEnvironment& domains,
        std::string_view scope,
        GeneratedNameEnvironment& visible_names,
        DesignUnit& unit,
        PackageEnvironment& packages,
        std::vector<std::pair<std::string, std::string>>& values,
        std::vector<std::pair<std::string, std::string>>& identities);

    void expand_vhdl_block_generates(SpecializedUnit& specialized);

    void finish();

    static ResolutionKind native_resolution(
        const SignalInfo& signal);

    std::optional<ResolutionKind> explicit_resolution(
        const SignalId signal);

    void register_vhdl_resolution_functions(
        const DesignUnit& unit);

    void register_systemverilog_resolution_functions(
        const DesignUnit& unit);

    struct SystemVerilogAliasConnection {
        std::string left;
        std::uint64_t left_offset { };
        std::string right;
        std::uint64_t right_offset { };
        std::uint64_t width { };
        frontend::SourceSpan source;
    };

    struct SystemVerilogAliasPlan {
        std::vector<std::vector<std::string>> whole_groups;
        std::vector<SystemVerilogAliasConnection> connections;
    };

    SystemVerilogAliasPlan
    apply_systemverilog_aliases(
        const DesignUnit& unit,
        std::span<const frontend::SignalDeclaration> ports,
        const SystemVerilogConstantEnvironment& integral_environment,
        const ConstantEnvironment& fallback_environment,
        bool compile_connections,
        bool diagnose);

    void add_systemverilog_alias_connections(
        const SystemVerilogAliasPlan& plan,
        std::string_view path,
        const SignalMap& signals,
        SpecializationInfo& specialization);

    void set_resolution(
        const SignalId signal,
        const ResolutionKind resolution);

    void validate_process_drivers();

    void canonicalize_process_operations(
        runtime::simir::Process& process);

    std::optional<SignalId> add_owned_signal(
        const frontend::SignalDeclaration& declaration,
        const std::string_view path,
        SignalMap& local);

    std::optional<StringObjectId> add_owned_string_port(
        const frontend::SignalDeclaration& declaration,
        const std::string_view path,
        StringMap& local);

    std::optional<StringObjectId> connect_string_port(
        const frontend::SignalDeclaration& port,
        const frontend::PortConnection& connection,
        const std::string& path,
        const StringMap& parent_strings,
        const std::unordered_set<StringObjectId>&
            parent_read_only_strings,
        bool cross_language);

    std::optional<ContainerType> container_port_type(
        const frontend::Type& type,
        const frontend::SourceSpan& source,
        const ConstantEnvironment& environment);

    std::optional<ContainerObjectId> add_owned_container_port(
        const frontend::SignalDeclaration& declaration,
        const std::string_view path,
        ContainerMap& local,
        const ConstantEnvironment& environment);

    std::optional<ContainerObjectId> connect_container_port(
        const frontend::SignalDeclaration& port,
        const frontend::PortConnection& connection,
        const std::string& path,
        const SignalMap& parent_signals,
        const ContainerMap& parent_containers,
        const std::unordered_set<std::string>&
            parent_read_only_containers,
        bool cross_language);

    std::optional<ContainerObjectId>
    connect_cross_language_container_port(
        const frontend::SignalDeclaration& port,
        const frontend::PortConnection& connection,
        const std::string& path,
        const SignalMap& parent_signals,
        const ContainerType& expected);

    const Binding* binding_for(const std::string& path);

    std::vector<UnitResolutionCandidate> resolution_candidates(
        std::string_view library,
        std::string_view name) const;

    std::vector<UnitResolutionCandidate> resolution_candidates(
        std::span<const std::string> libraries,
        std::string_view name,
        std::vector<std::string>& unavailable_libraries) const;

    std::optional<UnitResolutionCandidate> inferred_target(
        std::string_view library,
        std::string_view name,
        const std::string& path,
        frontend::SourceSpan source);

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
        const frontend::SourceSpan& source);

    void validate_vhdl_generic_type(
        const frontend::ParameterDeclaration& generic);

    bool connect_vhdl_expression_port(
        const frontend::SignalDeclaration& port,
        const frontend::PortConnection& connection,
        const std::string& path,
        const SignalMap& parent_signals,
        PortAliases& aliases,
        DesignUnit& dependency_owner);

    bool connect_verilog_memory_word_port(
        const frontend::SignalDeclaration& port,
        const frontend::PortConnection& connection,
        const std::string& path,
        const ContainerMap& parent_containers,
        PortAliases& aliases);

    bool connect_verilog_expression_port(
        const frontend::SignalDeclaration& port,
        const frontend::PortConnection& connection,
        const std::string& path,
        const SignalMap& parent_signals,
        PortAliases& aliases);

    PortAliases connect_ports(
        const frontend::Instance& instance,
        const std::vector<frontend::SignalDeclaration>& ports,
        const std::string& path,
        const SignalMap& parent_signals,
        const StringMap& parent_strings,
        const std::unordered_set<StringObjectId>&
            parent_read_only_strings,
        const ContainerMap& parent_containers,
        const std::unordered_set<std::string>&
            parent_read_only_containers,
        const Binding* binding,
        const bool cross_language,
        const bool require_input_connections = false,
        DesignUnit* dependency_owner = nullptr);

    PortAliases connect_instance(
        const frontend::Instance& instance,
        DesignUnit& target,
        const std::string& path,
        const SignalMap& parent_signals,
        const StringMap& parent_strings,
        const std::unordered_set<StringObjectId>&
            parent_read_only_strings,
        const ContainerMap& parent_containers,
        const std::unordered_set<std::string>&
            parent_read_only_containers,
        const Binding* binding,
        const bool cross_language);

    static frontend::SignalDeclaration external_port_declaration(
        const ExternalPort& port);
    std::pair<SignalMap, ObjectMap> connect_systemc_instance(
        const frontend::Instance& instance,
        const SystemCInstanceDescription& target,
        const std::string& path,
        const SignalMap& parent_signals,
        const Binding* binding);

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
    void instantiate_udp(const frontend::VerilogUdpDeclaration&,
        const frontend::Instance&, const std::string&, const SignalMap&,
        const StringMap&, const std::unordered_set<StringObjectId>&,
        const ContainerMap&, const std::unordered_set<std::string>&,
        const Binding*);
    UdpTableId normalized_udp_table(
        const frontend::VerilogUdpDeclaration&);
    void instantiate(
        DesignUnit& unit,
        const std::string& path,
        SignalMap aliases,
        StringMap string_aliases,
        ContainerMap container_aliases,
        std::unordered_set<SignalId> read_only_signals,
        std::unordered_set<StringObjectId> read_only_strings,
        ConstantEnvironment parameter_environment,
        SystemVerilogConstantEnvironment
            parameter_integral_environment,
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
    const DesignUnit* active_vhdl_configuration_ { };
    std::unordered_map<std::string, const DesignUnit*>
        vhdl_configurations_by_path_;
    const DesignUnit* active_systemverilog_configuration_ { };
    std::unordered_map<std::string, const DesignUnit*>
        systemverilog_configurations_by_path_;
    std::vector<const frontend::SystemVerilogBindDirective*>
        compilation_unit_systemverilog_binds_;
    std::unordered_map<
        const frontend::SystemVerilogBindDirective*, std::string>
        systemverilog_bind_libraries_;
    std::unordered_map<std::string, std::string>
        systemverilog_bound_instance_libraries_;
    std::unordered_set<const frontend::SystemVerilogBindDirective*>
        used_compilation_unit_systemverilog_binds_;
    std::string active_systemverilog_root_name_;
    std::unordered_map<const DesignUnit*, DesignUnit>
        resolved_vhdl_entity_interfaces_;
    std::unordered_map<
        std::string, const SystemCInstanceDescription*>
        systemc_instances_;
    std::deque<SystemCInstanceDescription>
        owned_systemc_instances_;
    SystemCFactoryProvider* systemc_provider_ { };
    const std::vector<SystemCFactoryCandidate> systemc_candidates_;
    const std::vector<std::string> systemc_libraries_;
    const std::vector<std::string> search_libraries_;
    std::string active_root_;
    SignalMap global_root_signals_;
    std::unordered_map<std::string, SignalMap>
        predeclared_root_signals_;
    std::unordered_map<std::string, SpecializedUnit>
        prepared_systemverilog_roots_;
    std::unordered_map<std::string, SpecializedUnit>
        specialized_unit_cache_;
    std::vector<std::string> cached_specialization_lru_;
    std::unordered_set<std::string>
        seen_specialization_keys_;
    std::unordered_set<std::string> used_systemc_instances_;
    std::unordered_set<std::string> instance_paths_;
    std::unordered_map<std::string, UdpTableId> udp_table_by_identity_;
    // Interface instances are registered by canonical hierarchy path after
    // their member signals have been allocated. Later sibling module ports
    // bind modport members through this exact instance identity.
    std::unordered_map<std::string, DesignUnit>
        systemverilog_interface_instances_;
    // Virtual-interface values use deterministic, nonzero identities. Zero
    // remains the language null value; identities never expose host pointers.
    std::unordered_map<std::string, std::uint64_t>
        systemverilog_interface_handles_;
    // The canonical specialization identity travels with concrete instances
    // and forwarded generic interface ports. Virtual-interface declarations
    // with parameter actuals compare against this exact identity.
    std::unordered_map<std::string,
        std::vector<std::pair<std::string, std::string>>>
        systemverilog_interface_parameter_identities_;
    std::uint64_t next_systemverilog_interface_handle_ { 1 };
    std::unordered_set<std::string>
        systemverilog_interface_port_paths_;
    // Non-empty only when a hierarchy alias exposes a restricted modport
    // rather than the complete concrete interface instance.
    std::unordered_map<std::string, std::string>
        systemverilog_interface_modport_views_;
    std::unordered_set<std::string>
        systemverilog_read_only_interface_member_paths_;
    std::vector<std::string> stack_;
    std::unordered_map<SignalId, std::vector<std::string>>
        boundary_driver_paths_;
    std::unordered_set<SignalId> vhdl_1993_shared_signals_;
    std::unordered_map<SignalId, std::string> resolver_by_signal_;
    std::vector<SignalId> boundary_resolver_insertions_;
    std::unordered_map<std::string, ResolutionKind>
        vhdl_resolution_kinds_;
    std::vector<std::string> vhdl_resolution_kind_insertions_;
    std::unordered_map<std::string, ResolutionKind>
        systemverilog_resolution_kinds_;
    std::vector<std::string>
        systemverilog_resolution_kind_insertions_;
    std::unordered_map<
        ContainerObjectId,
        std::vector<ContainerBoundaryDriver>>
        container_boundary_driver_paths_;
    std::unordered_map<StringObjectId, std::vector<std::string>>
        string_boundary_driver_paths_;
    std::unordered_map<std::uint64_t,
        std::vector<runtime::simir::ProcessId>>
        process_operation_representatives_;
    runtime::simir::OperationList::Storage operation_scratch_;
};

} // namespace fsim::elaboration
