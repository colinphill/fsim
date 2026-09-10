// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "hierarchy_builder_types.hpp"

#include "elaboration_targets.hpp"
#include "fsim/elaboration/elaborator.hpp"
#include "fsim/frontend/parser.hpp"
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
    bool evaluate_systemverilog_elaboration_report(
        const Statement& statement,
        const SystemVerilogConstantEnvironment& environment,
        const ConstantEnvironment& fallback_environment,
        const std::vector<frontend::FunctionDeclaration>& functions,
        std::string& error);

    class ConstantFunctionMemoizationScope final {
    public:
        ConstantFunctionMemoizationScope(
            std::vector<Diagnostic>& diagnostics,
            std::vector<frontend::Diagnostic>& messages);
        ConstantFunctionMemoizationScope(
            const ConstantFunctionMemoizationScope&) = delete;
        ConstantFunctionMemoizationScope& operator=(
            const ConstantFunctionMemoizationScope&) = delete;
        ~ConstantFunctionMemoizationScope();

    private:
        std::vector<Diagnostic>* previous_diagnostics_ { };
        std::vector<frontend::Diagnostic>* previous_messages_ { };
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

    void qualify_generated_classes(
        std::vector<frontend::SystemVerilogClassDeclaration>& declarations,
        GeneratedNameEnvironment& names,
        std::string_view scope,
        std::string_view unit_name);

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

} // namespace fsim::elaboration
