// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "elaborator_internal.hpp"

namespace fsim::elaboration {

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

struct SystemVerilogInterfaceLiteral {
    std::uint64_t handle { };
    frontend::Type type;
    std::vector<std::pair<std::string, std::string>>
        specialization_identity;
};

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
    void set_systemverilog_standard(
        frontend::StandardRevision standard) noexcept;
    void set_vhdl_standard(frontend::VhdlStandard standard) noexcept;
    void set_vhdl_synopsys_numeric_context(
        bool signed_visible, bool unsigned_visible) noexcept;
    void set_systemverilog_interface_literals(
        std::unordered_map<std::string,
            SystemVerilogInterfaceLiteral> literals);
    void set_systemverilog_interface_type_identities(
        std::unordered_map<const frontend::Type*,
            std::vector<std::pair<std::string, std::string>>>
            identities);

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

    [[nodiscard]] static std::optional<std::string>
    systemverilog_interface_literal_name(
        const Expression& expression);

    [[nodiscard]] bool validate_sv_nominal_assignment(
        const frontend::Type* target_type,
        const Expression& value);

    [[nodiscard]] std::pair<std::int64_t, std::int64_t>
    integer_bounds(
        const std::optional<frontend::IntegerRange>& range) const;

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

    [[nodiscard]] std::string declaration_key(
        const frontend::VariableDeclaration& variable) const;

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
    std::optional<RegisterId> lower_vhdl_conditional_expression(
        const Expression& expression,
        std::size_t expected_width,
        const frontend::Type* expected_type);
    void lower_assert(const Statement& statement);
    void emit_deferred_assertion_action_handoff();
    std::optional<InstructionIndex> finish_deferred_assertion_action_handoff();
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
    bool lower_special_assignment_target(const Statement& statement);
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

    void lower_runtime_foreach(const Statement& statement);

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
    ExpressionAttempt lower_vhdl_reflection_expression(
        const Expression&, std::size_t, const frontend::Type*);
    std::optional<StringRegisterId> lower_vhdl_reflection_string_expression(
        const Expression&);
    ExpressionAttempt lower_vhdl_physical_expression(
        const Expression&, std::size_t, const frontend::Type*);
    bool lower_vhdl_simulator_procedure_call(const Statement&);
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
    void emit_vhdl_access_reclamation(
        const frontend::Type&, const VhdlAccessHeap&, RegisterId);
    void emit_vhdl_access_scope_cleanup(
        const std::vector<frontend::VariableDeclaration>&);
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
    ExpressionAttempt lower_primary_cast_expression(
        const Expression& expression,
        std::size_t expected_width,
        const frontend::Type* expected_type);
    ExpressionAttempt lower_multidimensional_container_read(
        const Expression& expression);
    struct UnpackedAggregateMemberReference {
        frontend::Type leaf_type;
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
    ExpressionAttempt lower_vhdl_simulator_function_expression(
        const Expression&, std::size_t, const frontend::Type*);
    ExpressionAttempt lower_vhdl_environment_binary_expression(
        const Expression&, const frontend::Type*);
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

    [[nodiscard]] bool static_reference_actual(
        const Expression& expression) const;

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
    [[nodiscard]] bool validate_vhdl_mode_view_write(
        runtime::simir::SignalId signal,
        const frontend::Expression& target,
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
    std::unordered_map<std::string,
        SystemVerilogInterfaceLiteral>
        systemverilog_interface_literals_;
    std::unordered_map<const frontend::Type*,
        std::vector<std::pair<std::string, std::string>>>
        systemverilog_interface_type_identities_;
    const std::vector<frontend::FunctionDeclaration>& functions_;
    const std::vector<frontend::TaskDeclaration>& tasks_;
    const std::vector<frontend::ProcedureDeclaration>& procedures_;
    frontend::SystemVerilogScalarEvaluationContext scalar_context_;
    std::vector<Diagnostic>& diagnostics_;
    Process process_;
    bool sample_concurrent_assertion_reads_ { };
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
    std::optional<runtime::SchedulerPhase> deferred_assertion_action_phase_;
    struct DeferredAssertionActionHandoff {
        InstructionIndex fork { }, continuation_jump { }, branch { };
    };
    std::optional<DeferredAssertionActionHandoff> deferred_assertion_action_handoff_;
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
    std::deque<frontend::ProcedureDeclaration>
        vhdl_procedure_specializations_;
    std::vector<ProcedureFrame> procedure_frames_;
    std::unordered_map<std::string, std::vector<std::size_t>>
        procedure_indices_;
    std::unordered_map<std::string, std::size_t>
        vhdl_procedure_specialization_indices_;
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
    frontend::StandardRevision systemverilog_standard_ {
        frontend::StandardRevision::SystemVerilog2017
    };
    frontend::VhdlStandard vhdl_standard_ {
        frontend::VhdlStandard::Vhdl2008
    };
    bool vhdl_synopsys_signed_visible_ { };
    bool vhdl_synopsys_unsigned_visible_ { };
    std::string hierarchy_;
};

} // namespace fsim::elaboration
