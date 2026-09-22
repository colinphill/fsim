// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "elaborator_internal.hpp"
#include "fsim/frontend/parser.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"

namespace fsim::elaboration {

using frontend::ProcessKind;
using runtime::Logic4;
using runtime::PackedLogic4;
using namespace runtime::simir;
using namespace elaboration_detail;

[[nodiscard]] std::optional<double>
systemverilog_real_literal(std::string_view spelling) noexcept;
[[nodiscard]] std::optional<runtime::simir::RandomDistributionKind>
systemverilog_random_distribution_kind(std::string_view spelling) noexcept;
[[nodiscard]] bool systemverilog_sampled_value_call(
    std::string_view spelling) noexcept;
[[nodiscard]] bool systemverilog_sampled_value_preserves_signal(
    std::string_view spelling) noexcept;
[[nodiscard]] std::optional<std::string_view>
vhdl_logic_string_function_name(std::string_view spelling) noexcept;
[[nodiscard]] bool vhdl_environment_time_record_subtype(
    const semantic::SpecializedHirUnit&,
    const semantic::vhdl::SubtypeIndication&,
    semantic::ScopeId use_scope);

[[nodiscard]] constexpr bool use_systemverilog_timeformat_width(
    const std::uint32_t minimum_width,
    const bool suppress_leading_zero) noexcept
{
    return minimum_width == 0U && !suppress_leading_zero;
}

struct VhdlVitalNamedConstant {
    std::string canonical_name;
    std::size_t default_width { };
    frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
    bool exact_width { };
    std::optional<PackedLogic4> value;
};

/// Resolve a compiler-owned VITAL named constant. User declarations continue
/// to shadow the governed package surface; a missing value means the supplied
/// contextual width does not satisfy the constant's VITAL type.
[[nodiscard]] std::optional<VhdlVitalNamedConstant>
resolve_vhdl_vital_named_constant(
    const semantic::SpecializedHirUnit& specialization,
    semantic::ExpressionId expression,
    std::size_t contextual_width,
    std::span<const semantic::CompiledBindingFrame> binding_frames = { });

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
        std::vector<Diagnostic>& diagnostics);

    /// Select the parser-independent working HIR for subsequent ID-based
    /// lowering. The caller retains ownership for the lifetime of this
    /// Lowerer.
    void set_specialized_hir_unit(
        const semantic::SpecializedHirUnit* unit) noexcept;
    void set_systemverilog_interface_handles(
        const std::unordered_map<std::string, std::uint64_t>* handles)
        noexcept;
    [[nodiscard]] bool can_lower_hir_process(
        semantic::ProcessId process) const;
    void diagnose_hir_systemverilog_file_process(
        semantic::ProcessId process);
    [[nodiscard]] std::optional<semantic::SourceSpanId>
    hir_vhdl_unspecified_inference_failure(
        semantic::ProcessId process) const;
    [[nodiscard]] std::optional<Process> lower_hir_process(
        semantic::ProcessId process,
        frontend::Language language,
        std::string_view hierarchy);
    [[nodiscard]] bool can_lower_hir_concurrent_statement(
        semantic::StatementId statement) const;
    [[nodiscard]] bool diagnose_hir_vhdl_block_guard(
        semantic::ExpressionId expression);
    [[nodiscard]] std::optional<semantic::SourceSpanId>
    hir_vhdl_unspecified_inference_failure(
        semantic::StatementId statement) const;
    [[nodiscard]] std::optional<Process>
    lower_hir_concurrent_statement(
        semantic::StatementId statement,
        frontend::Language language,
        std::string_view hierarchy,
        std::size_t order,
        std::optional<semantic::ExpressionId> enclosing_vhdl_guard
        = std::nullopt);
    [[nodiscard]] std::optional<Process> lower_hir_input_actual(
        semantic::ExpressionId expression,
        SignalId destination,
        frontend::Language language,
        std::string_view hierarchy,
        std::size_t order,
        std::optional<semantic::vhdl::SubtypeIndication>
            vhdl_context = std::nullopt);
    [[nodiscard]] std::optional<Process> lower_hir_output_actual(
        SignalId source,
        semantic::ExpressionId expression,
        frontend::Language language,
        std::string_view hierarchy,
        std::size_t order);

    [[nodiscard]] std::vector<Process> take_generated_processes();

    void set_systemverilog_program_owner(
        std::optional<std::uint32_t> owner) noexcept;

private:
    struct HirProcessDescription {
        semantic::SourceSpanId source;
        semantic::ScopeId scope;
        std::string name;
        std::span<const semantic::DeclarationId> declarations;
        std::span<const semantic::StatementId> statements;
        std::vector<runtime::simir::Sensitivity> sensitivities;
        std::optional<semantic::ExpressionId> event_expression;
        runtime::simir::EdgeKind event_edge {
            runtime::simir::EdgeKind::any
        };
        std::optional<semantic::ExpressionId> input_actual;
        std::optional<SignalId> input_actual_destination;
        std::optional<semantic::vhdl::SubtypeIndication>
            input_actual_vhdl_context;
        std::optional<SignalId> output_actual_source;
        std::optional<semantic::ExpressionId> output_actual;
        std::optional<SignalId> procedural_assignment_active;
        std::optional<semantic::ExpressionId> procedural_assignment_target;
        std::optional<semantic::ExpressionId> procedural_assignment_value;
        std::optional<semantic::ExpressionId> vhdl_guard;
        std::optional<semantic::vhdl::Delay> vhdl_disconnection_delay;
        frontend::ProcessKind kind { frontend::ProcessKind::VerilogAlways };
        bool wildcard_sensitivity { };
        bool require_wildcard_dependency { true };
        bool postponed { };
        bool initial { };
        bool final { };
        bool body_timed_always { };
        bool always_comb_or_latch { };
        bool wait_before_first_execution { };
        bool concurrent_assertion { };
        bool vhdl_guarded_assignment { };
    };

    [[nodiscard]] std::optional<Process> lower_hir_process_body(
        const HirProcessDescription& description,
        frontend::Language language,
        std::string_view hierarchy);

    [[nodiscard]] std::vector<SignalId> hir_signal_dependencies(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] std::vector<SignalId> hir_statement_signal_dependencies(
        std::span<const semantic::StatementId> statements,
        semantic::ScopeId process_scope) const;

    [[nodiscard]] frontend::SourceSpan hir_source_span(
        semantic::SourceSpanId source) const;
    [[nodiscard]] bool hir_vhdl_active_package_constant(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<std::int64_t> hir_constant_integer(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<semantic::ExpressionId>
    hir_constant_initializer(
        semantic::DeclarationId declaration) const;
    struct HirPackedRange {
        std::int64_t left { };
        std::int64_t right { };
        bool descending { };
    };
    struct HirVhdlArrayIndex {
        semantic::ExpressionId expression;
        std::int64_t left { };
        std::int64_t right { };
        std::size_t stride { };
        bool descending { };
    };
    struct HirVhdlArraySelection {
        semantic::DeclarationId declaration;
        semantic::vhdl::SubtypeIndication subtype;
        std::size_t root_width { };
        std::size_t offset { };
        std::size_t width { };
        frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
        bool signed_value { };
        std::vector<HirVhdlArrayIndex> dynamic_indices;
    };
    [[nodiscard]] std::optional<HirPackedRange> hir_expression_range(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
    struct HirConstantSelection {
        std::size_t offset { };
        std::size_t width { };
    };
    [[nodiscard]] std::optional<HirConstantSelection>
    hir_constant_selection(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] std::optional<HirConstantSelection>
    hir_root_constant_selection(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] bool hir_expression_signed(
        semantic::ExpressionId expression) const;
    [[nodiscard]] bool hir_dynamic_index_supported(
        semantic::ExpressionId source,
        semantic::ExpressionId index,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] std::optional<std::size_t> hir_dynamic_part_width(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] std::optional<runtime::simir::DynamicIndex>
    lower_hir_dynamic_index(
        semantic::ExpressionId source,
        semantic::ExpressionId index,
        std::size_t source_width,
        std::uint32_t base_offset = 0U);
    [[nodiscard]] std::optional<runtime::simir::DynamicPartIndex>
    lower_hir_vhdl_dynamic_slice(
        semantic::ExpressionId expression,
        std::size_t source_width,
        std::size_t selected_width,
        std::uint32_t base_offset = 0U);
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_vhdl_array_offset(
        const HirVhdlArraySelection& selection);
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_vhdl_dynamic_element_offset(
        semantic::ExpressionId source,
        semantic::ExpressionId index,
        std::size_t element_width,
        std::uint32_t member_offset = 0U);
    [[nodiscard]] std::optional<semantic::DeclarationId>
    hir_target_declaration(semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<SignalId> hir_direct_signal(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<SignalId>
    hir_systemverilog_event_signal(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] std::optional<std::uint64_t>
    hir_systemverilog_interface_handle(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<std::vector<semantic::sv::Sensitivity>>
    hir_default_clocking_event() const;
    [[nodiscard]] std::optional<std::size_t> hir_loop_iteration_count(
        semantic::StatementId statement) const;
    [[nodiscard]] std::optional<std::size_t> hir_expression_width(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] std::optional<std::size_t>
    hir_vhdl_expression_runtime_width(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] std::optional<std::size_t>
    hir_systemverilog_type_width(
        const semantic::sv::TypeReference& type) const;
    [[nodiscard]] bool hir_systemverilog_type_four_state(
        const semantic::sv::TypeReference& type) const;
    [[nodiscard]] std::optional<semantic::sv::TypeReference>
    hir_systemverilog_type_parameter_binding(
        semantic::TypeId type) const;
    [[nodiscard]] std::optional<semantic::DeclarationId>
    hir_systemverilog_named_type_declaration(
        std::string_view spelling,
        semantic::ScopeId use_scope) const;
    struct HirSystemVerilogCastProfile {
        std::size_t width { };
        frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
        bool signed_value { };
        std::optional<semantic::TypeId> nominal_type;
    };
    [[nodiscard]] std::optional<HirSystemVerilogCastProfile>
    hir_systemverilog_cast_profile(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<semantic::ExpressionId>
    hir_systemverilog_packed_pattern_operand(
        semantic::ExpressionId expression,
        const semantic::sv::TypeReference& target_type) const;
    struct HirVhdlConversionProfile {
        std::size_t width { };
        frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
        bool signed_value { };
        std::optional<frontend::IntegerRange> integer_range;
    };
    [[nodiscard]] std::optional<HirVhdlConversionProfile>
    hir_vhdl_conversion_profile(
        semantic::ExpressionId expression,
        std::optional<std::size_t> contextual_width = std::nullopt) const;
    enum class HirVhdlStandardFunctionKind : std::uint8_t {
        bit_conversion,
        logic_conversion,
        logic_mapping,
        logic_unknown_predicate,
        reduction,
        numeric_to_integer,
        numeric_resize,
        numeric_shift,
        psl_query,
    };
    struct HirVhdlStandardFunctionProfile {
        HirVhdlStandardFunctionKind kind;
        std::string_view name;
        std::size_t width { };
        frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
        bool signed_value { };
        bool invalid_numeric_size { };
    };
    [[nodiscard]] std::optional<HirVhdlStandardFunctionProfile>
    hir_vhdl_standard_function_profile(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_vhdl_standard_function(
        semantic::ExpressionId expression,
        const HirVhdlStandardFunctionProfile& profile);
    struct HirVhdlIntrinsicProfile {
        std::size_t width { };
        frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
        bool signed_value { };
    };
    struct HirVhdlIntrinsicAttempt {
        bool handled { };
        std::optional<RegisterId> value;
    };
    [[nodiscard]] bool is_hir_vhdl_fixed_function(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<HirVhdlIntrinsicProfile>
    hir_vhdl_fixed_function_profile(
        semantic::ExpressionId expression) const;
    [[nodiscard]] HirVhdlIntrinsicAttempt
    lower_hir_vhdl_fixed_function(
        semantic::ExpressionId expression,
        std::size_t expected_width);
    [[nodiscard]] bool validate_hir_vhdl_fixed_context(
        semantic::ExpressionId expression,
        const semantic::vhdl::SubtypeIndication& context);
    [[nodiscard]] bool is_hir_vhdl_float_function(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<HirVhdlIntrinsicProfile>
    hir_vhdl_float_function_profile(
        semantic::ExpressionId expression) const;
    [[nodiscard]] HirVhdlIntrinsicAttempt
    lower_hir_vhdl_float_function(
        semantic::ExpressionId expression,
        std::size_t expected_width);
    [[nodiscard]] bool validate_hir_vhdl_float_context(
        semantic::ExpressionId expression,
        const semantic::vhdl::SubtypeIndication& context);
    struct HirVhdlSynopsysNumericContext {
        bool signed_visible { };
        bool unsigned_visible { };
    };
    [[nodiscard]] HirVhdlSynopsysNumericContext
    hir_vhdl_synopsys_numeric_context(semantic::ScopeId scope) const;
    struct HirPackedMemberSelection {
        struct TaggedMember {
            std::size_t offset { };
            std::size_t width { };
            std::size_t payload_width { };
            std::size_t tag_width { };
            std::size_t tag_value { };
        };
        semantic::DeclarationId declaration;
        std::size_t offset { };
        std::size_t width { };
        std::optional<HirPackedRange> range;
        frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
        bool signed_value { };
        std::optional<TaggedMember> tagged;
    };
    [[nodiscard]] std::optional<std::size_t>
    hir_systemverilog_member_offset(
        const semantic::sv::TypeDefinition& type,
        std::size_t member_index) const;
    [[nodiscard]] std::optional<HirPackedMemberSelection>
    hir_systemverilog_member_selection(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<HirVhdlArraySelection>
    hir_vhdl_array_selection(
        semantic::ExpressionId expression) const;
    struct HirVhdlMemberSelection {
        semantic::DeclarationId declaration;
        semantic::ExpressionId root;
        semantic::vhdl::SubtypeIndication subtype;
        std::size_t offset { };
        std::size_t width { };
        frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
        bool signed_value { };
        std::optional<semantic::ExpressionId> index;
        std::size_t element_width { };
    };
    [[nodiscard]] std::optional<semantic::vhdl::SubtypeIndication>
    hir_vhdl_expression_subtype(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<std::pair<semantic::vhdl::TypeForm,
        semantic::TypeId>>
    hir_vhdl_composite_root_type(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<HirVhdlMemberSelection>
    hir_vhdl_member_selection(
        semantic::ExpressionId expression) const;
    struct HirCasePatternBinding {
        std::string name;
        std::optional<runtime::simir::RegisterId> value;
        std::size_t width { };
        frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
        bool signed_value { };
    };
    [[nodiscard]] std::optional<HirCasePatternBinding>
    hir_case_pattern_binding(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<frontend::ValueDomain>
    hir_expression_domain(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] frontend::SystemVerilogScalarKind
    hir_systemverilog_scalar_kind(
        semantic::ExpressionId expression,
        frontend::SystemVerilogScalarKind contextual_kind
        = frontend::SystemVerilogScalarKind::None) const;
    enum class HirVhdlAttributeKind : std::uint8_t {
        length,
        position,
        value,
        successor,
        predecessor,
    };
    enum class HirVhdlAttributeFailure : std::uint8_t {
        none,
        array_prefix,
        array_dimension,
        array_result,
        scalar_profile,
        scalar_value,
        enumeration_profile,
        enumeration_value,
    };
    struct HirVhdlAttributeProfile {
        HirVhdlAttributeKind kind { HirVhdlAttributeKind::length };
        std::optional<semantic::ExpressionId> value;
        std::optional<std::int64_t> constant;
        std::optional<HirPackedRange> discrete_range;
        std::size_t width { };
        std::size_t value_width { };
        frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
        std::optional<std::int64_t> lower_bound;
        std::optional<std::int64_t> upper_bound;
        bool array_prefix { };
    };
    [[nodiscard]] std::optional<HirVhdlAttributeProfile>
    hir_vhdl_attribute_profile(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope,
        HirVhdlAttributeFailure* failure = nullptr) const;
    enum class HirVhdlSignalAttributeKind : std::uint8_t {
        event,
        last_value,
        last_event,
        last_active,
        driving,
        driving_value,
        stable,
        quiet,
        active,
        transaction,
        delayed,
    };
    struct HirVhdlSignalAttributeProfile {
        HirVhdlSignalAttributeKind kind {
            HirVhdlSignalAttributeKind::event
        };
        SignalId signal { };
        runtime::SimulationTick duration { };
        std::size_t width { };
        frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
    };
    [[nodiscard]] std::optional<HirVhdlSignalAttributeProfile>
    hir_vhdl_signal_attribute_profile(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] bool is_hir_vhdl_vital_mux2(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<HirVhdlIntrinsicProfile>
    hir_vhdl_vital_expression_profile(
        semantic::ExpressionId expression) const;
    [[nodiscard]] HirVhdlIntrinsicAttempt
    lower_hir_vhdl_vital_expression(
        semantic::ExpressionId expression,
        std::size_t expected_width);
    [[nodiscard]] std::optional<SignalId>
    vhdl_implicit_signal_attribute(
        SignalId source,
        std::string_view attribute,
        runtime::SimulationTick duration,
        frontend::SourceSpan span);
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_vhdl_signal_attribute(
        semantic::ExpressionId expression,
        std::size_t expected_width);
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_vhdl_vital_mux2(
        semantic::ExpressionId expression,
        std::size_t expected_width);
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_vhdl_attribute(
        semantic::ExpressionId expression,
        std::size_t expected_width);
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_vhdl_aggregate(
        semantic::ExpressionId expression,
        std::size_t expected_width,
        const semantic::vhdl::SubtypeIndication* contextual_subtype = nullptr);
    [[nodiscard]] bool can_lower_hir_expression(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope,
        std::unordered_set<std::uint32_t>& visiting) const;
    [[nodiscard]] bool can_lower_hir_statement(
        semantic::StatementId statement,
        semantic::ScopeId process_scope,
        std::unordered_set<std::uint32_t>& visiting) const;
    [[nodiscard]] std::optional<std::size_t>
    hir_statement_expansion_cost(
        semantic::StatementId statement,
        std::unordered_set<std::uint32_t>& visiting) const;
    [[nodiscard]] bool can_lower_hir_declaration(
        semantic::DeclarationId declaration,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] std::optional<RegisterId> lower_hir_expression(
        semantic::ExpressionId expression,
        std::size_t expected_width,
        frontend::SystemVerilogScalarKind scalar_context
        = frontend::SystemVerilogScalarKind::None);
    struct HirSynchronizationAttempt {
        bool handled { };
        bool succeeded { };
        std::optional<RegisterId> value;
    };
    struct HirSynchronizationTaskProfile {
        semantic::DeclarationId receiver;
        std::string_view method;
        bool mailbox { };
    };
    [[nodiscard]] std::optional<HirSynchronizationTaskProfile>
    hir_synchronization_task_profile(
        const semantic::sv::Statement& statement,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] bool lower_hir_synchronization_statement(
        const semantic::sv::Statement& statement);
    [[nodiscard]] HirSynchronizationAttempt
    lower_hir_synchronization_expression(
        semantic::ExpressionId expression,
        std::size_t expected_width,
        const semantic::sv::TypeReference* expected_type = nullptr);
    [[nodiscard]] std::optional<runtime::SystemVerilogMathFunction>
    hir_systemverilog_math_function(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_systemverilog_math_call(
        semantic::ExpressionId expression);
    [[nodiscard]] bool is_hir_systemverilog_file_call(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_systemverilog_file_call(
        semantic::ExpressionId expression,
        std::size_t expected_width);
    [[nodiscard]] bool is_hir_systemverilog_coverage_call(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_systemverilog_coverage_call(
        semantic::ExpressionId expression,
        std::size_t expected_width);
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_systemverilog_packed_pattern(
        semantic::ExpressionId expression,
        const semantic::sv::TypeReference& type,
        std::size_t expected_width);
    [[nodiscard]] bool can_lower_hir_systemverilog_packed_pattern(
        semantic::ExpressionId expression,
        const semantic::sv::TypeReference& type,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] bool lower_hir_systemverilog_file_statement(
        semantic::StatementId statement);
    [[nodiscard]] bool is_hir_vhdl_file_call(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<std::uint64_t>
    hir_vhdl_standard_enumeration_literal(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<RegisterId> lower_hir_vhdl_file_call(
        semantic::ExpressionId expression);
    [[nodiscard]] bool is_hir_vhdl_file_statement(
        semantic::StatementId statement) const;
    [[nodiscard]] bool lower_hir_vhdl_file_statement(
        semantic::StatementId statement);
    [[nodiscard]] bool is_hir_vhdl_vital_delay_call(
        semantic::StatementId statement) const;
    [[nodiscard]] bool lower_hir_vhdl_vital_delay_call(
        semantic::StatementId statement);
    [[nodiscard]] bool is_hir_vhdl_vital_state_table_call(
        semantic::StatementId statement) const;
    [[nodiscard]] bool lower_hir_vhdl_vital_state_table_call(
        semantic::StatementId statement);
    [[nodiscard]] bool is_hir_vhdl_vital_timing_call(
        semantic::StatementId statement) const;
    [[nodiscard]] bool lower_hir_vhdl_vital_timing_call(
        semantic::StatementId statement);
    [[nodiscard]] bool is_hir_vhdl_vital_memory_expression(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_vhdl_vital_memory_expression(
        semantic::ExpressionId expression,
        std::size_t expected_width);
    [[nodiscard]] bool is_hir_vhdl_protected_expression(
        semantic::ExpressionId expression) const;
    [[nodiscard]] bool is_hir_vhdl_protected_statement(
        semantic::StatementId statement) const;
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_vhdl_protected_expression(
        semantic::ExpressionId expression,
        std::size_t expected_width);
    [[nodiscard]] bool lower_hir_vhdl_protected_statement(
        semantic::StatementId statement);
    [[nodiscard]] bool is_hir_vhdl_environment_expression(
        semantic::ExpressionId expression) const;
    [[nodiscard]] bool is_hir_vhdl_environment_status_literal(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<std::uint64_t>
    hir_vhdl_environment_status_literal(
        semantic::ExpressionId expression,
        const semantic::vhdl::SubtypeIndication& context) const;
    [[nodiscard]] bool is_hir_vhdl_environment_directory(
        semantic::DeclarationId declaration) const;
    [[nodiscard]] runtime::simir::ContainerType
    hir_vhdl_environment_directory_container_type() const;
    [[nodiscard]] bool is_hir_vhdl_environment_call_path(
        semantic::DeclarationId declaration) const;
    [[nodiscard]] runtime::simir::ContainerType
    hir_vhdl_environment_call_path_container_type() const;
    [[nodiscard]] std::optional<ContainerRegisterId>
    lower_hir_vhdl_environment_call_path_container_expression(
        semantic::ExpressionId expression);
    struct HirVhdlCallPathMemberSelection {
        semantic::DeclarationId call_path;
        semantic::ExpressionId index;
        std::uint32_t member { };
    };
    [[nodiscard]] std::optional<HirVhdlCallPathMemberSelection>
    hir_vhdl_environment_call_path_member_selection(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<StringRegisterId>
    lower_hir_vhdl_environment_call_path_string_selection(
        semantic::ExpressionId expression);
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_vhdl_environment_call_path_scalar_selection(
        semantic::ExpressionId expression,
        std::size_t expected_width);
    [[nodiscard]] bool is_hir_vhdl_environment_call_path_assignment(
        semantic::StatementId statement) const;
    [[nodiscard]] bool lower_hir_vhdl_environment_call_path_assignment(
        semantic::StatementId statement);
    struct HirVhdlDirectoryStringSelection {
        semantic::DeclarationId directory;
        std::optional<semantic::ExpressionId> index;
    };
    [[nodiscard]] std::optional<HirVhdlDirectoryStringSelection>
    hir_vhdl_environment_directory_string_selection(
        semantic::ExpressionId expression) const;
    [[nodiscard]] bool is_hir_vhdl_environment_directory_statement(
        semantic::StatementId statement) const;
    [[nodiscard]] bool lower_hir_vhdl_environment_directory_statement(
        semantic::StatementId statement);
    [[nodiscard]] std::optional<frontend::VhdlSimulatorApi>
    hir_vhdl_assert_expression_api(
        semantic::ExpressionId expression) const;
    [[nodiscard]] bool is_hir_vhdl_assert_statement(
        semantic::StatementId statement) const;
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_vhdl_assert_level(semantic::ExpressionId expression);
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_vhdl_assert_expression(
        semantic::ExpressionId expression,
        std::size_t expected_width);
    [[nodiscard]] std::optional<StringRegisterId>
    lower_hir_vhdl_assert_string_expression(
        semantic::ExpressionId expression);
    [[nodiscard]] bool lower_hir_vhdl_assert_statement(
        semantic::StatementId statement);
    [[nodiscard]] bool hir_vhdl_time_record_expression(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_vhdl_environment_expression(
        semantic::ExpressionId expression,
        std::size_t expected_width);
    [[nodiscard]] std::optional<StringRegisterId>
    lower_hir_vhdl_environment_string_expression(
        semantic::ExpressionId expression);
    [[nodiscard]] bool is_hir_vhdl_reflection_expression(
        semantic::ExpressionId expression) const;
    [[nodiscard]] bool is_hir_vhdl_reflection_string_expression(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_vhdl_reflection_expression(
        semantic::ExpressionId expression,
        std::size_t expected_width);
    [[nodiscard]] std::optional<StringRegisterId>
    lower_hir_vhdl_reflection_string_expression(
        semantic::ExpressionId expression);
    [[nodiscard]] bool is_hir_vhdl_access_expression(
        semantic::ExpressionId expression) const;
    struct HirVhdlAccessHeap;
    [[nodiscard]] HirVhdlAccessHeap* hir_vhdl_access_heap(
        const semantic::vhdl::TypeDefinition& type,
        const frontend::SourceSpan& span);
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_vhdl_access_expression(
        semantic::ExpressionId expression,
        std::size_t expected_width);
    [[nodiscard]] bool is_hir_vhdl_access_deallocation(
        semantic::StatementId statement) const;
    [[nodiscard]] bool lower_hir_vhdl_access_deallocation(
        semantic::StatementId statement);
    [[nodiscard]] bool is_hir_vhdl_access_assignment(
        semantic::StatementId statement) const;
    [[nodiscard]] bool lower_hir_vhdl_access_assignment(
        semantic::StatementId statement);
    [[nodiscard]] bool reject_hir_vhdl_access_signal_escape(
        semantic::StatementId statement);
    void emit_hir_vhdl_access_reclamation(
        const semantic::vhdl::TypeDefinition& type,
        const HirVhdlAccessHeap& heap,
        RegisterId candidate);
    [[nodiscard]] bool is_hir_vhdl_physical_expression(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_vhdl_physical_expression(
        semantic::ExpressionId expression,
        std::size_t expected_width);
    struct HirLetBinding {
        std::string name;
        semantic::ExpressionId actual;
    };
    struct HirLetFrame {
        const semantic::sv::LetDeclaration* declaration { };
        std::vector<HirLetBinding> bindings;
    };
    [[nodiscard]] std::optional<semantic::ExpressionId>
    hir_let_actual(semantic::ExpressionId expression) const;
    [[nodiscard]] const semantic::sv::LetDeclaration*
    hir_let_declaration(semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<std::vector<HirLetBinding>>
    bind_hir_let_actuals(
        semantic::ExpressionId expression,
        const semantic::sv::LetDeclaration& declaration) const;
    [[nodiscard]] bool push_hir_let_frame(
        semantic::ExpressionId expression,
        const semantic::sv::LetDeclaration& declaration) const;
    [[nodiscard]] bool hir_expression_is_string(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] bool can_lower_hir_string_expression(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
    enum class HirStringFormatStatus : std::uint8_t {
        not_applicable,
        valid,
        invalid,
    };
    [[nodiscard]] HirStringFormatStatus
    hir_string_format_expression_status(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] HirStringFormatStatus hir_string_format_task_status(
        semantic::StatementId statement,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] std::optional<StringRegisterId>
    lower_hir_string_expression(semantic::ExpressionId expression);
    [[nodiscard]] std::optional<StringRegisterId>
    lower_hir_string_format_expression(
        semantic::ExpressionId expression);
    [[nodiscard]] std::optional<StringRegisterId>
    lower_hir_formatted_string(
        std::span<const semantic::ExpressionId> arguments,
        std::optional<frontend::OutputFormat> default_format);
    [[nodiscard]] bool lower_hir_string_format_task(
        semantic::StatementId statement);
    [[nodiscard]] bool lower_hir_statements(
        std::span<const semantic::StatementId> statements);
    [[nodiscard]] bool lower_hir_statement(
        semantic::StatementId statement);
    void emit_deferred_assertion_action_handoff();
    [[nodiscard]] std::optional<InstructionIndex>
    finish_deferred_assertion_action_handoff();
    [[nodiscard]] bool initialize_hir_declarations(
        std::span<const semantic::DeclarationId> declarations);
    [[nodiscard]] std::optional<std::string> hir_name(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<semantic::DeclarationId>
    hir_referenced_declaration(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<semantic::DeclarationId>
    hir_vhdl_type_declaration(
        semantic::ExpressionId expression) const;
    using HirGenericBinding = semantic::CompiledActualBinding;
    using HirCallableResolution =
        semantic::CompiledVhdlCallableResolution;
    [[nodiscard]] std::optional<semantic::DeclarationId>
    hir_actual_declaration(semantic::DeclarationId formal) const;
    [[nodiscard]] std::vector<semantic::DeclarationId>
    hir_vhdl_package_member_candidates(
        const semantic::vhdl::Name& name,
        semantic::ScopeId use_scope) const;
    [[nodiscard]] std::vector<semantic::DeclarationId>
    hir_vhdl_callable_candidates(
        const semantic::vhdl::Name& name,
        semantic::ScopeId use_scope) const;

    [[nodiscard]] std::vector<HirCallableResolution>
    hir_vhdl_callable_resolutions(
        const semantic::vhdl::Name& name,
        semantic::ScopeId use_scope) const;
    [[nodiscard]] std::optional<semantic::ExpressionId>
    hir_generic_actual(semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<semantic::vhdl::SubtypeIndication>
    hir_effective_vhdl_subtype(
        const semantic::vhdl::SubtypeIndication& subtype) const;
    [[nodiscard]] std::optional<semantic::vhdl::SubtypeIndication>
    hir_vhdl_type_actual(semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<std::vector<HirGenericBinding>>
    bind_hir_vhdl_generics(
        std::span<const semantic::DeclarationId> formals,
        std::span<const semantic::vhdl::Association> associations) const;
    [[nodiscard]] std::optional<HirCallableResolution>
    hir_callable_resolution(semantic::DeclarationId declaration) const;
    enum class HirRuntimeBindingKind : std::uint8_t {
        local,
        signal,
    };
    struct HirRuntimeBinding {
        semantic::DeclarationId declaration;
        HirRuntimeBindingKind kind { HirRuntimeBindingKind::signal };
        std::string name;
        std::size_t width { };
        frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
        bool signed_value { };
        std::optional<frontend::IntegerRange> integer_range;
        std::optional<RegisterId> local;
        std::optional<SignalId> signal;
    };
    [[nodiscard]] std::optional<HirRuntimeBinding>
    hir_direct_signal_binding(semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<HirRuntimeBinding> hir_runtime_binding(
        semantic::DeclarationId declaration,
        semantic::ScopeId process_scope,
        bool require_storage) const;
    struct HirPackedUpdateTarget {
        HirRuntimeBinding binding;
        std::optional<HirConstantSelection> constant_selection;
        std::optional<runtime::simir::DynamicIndex> dynamic_selection;
        std::optional<std::size_t> dynamic_part_width;
        std::string selection_operation;
        std::size_t width { };
        frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
        bool signed_value { };
        bool index { };
        bool slice { };
        RegisterId captured { };
    };
    [[nodiscard]] std::optional<HirPackedUpdateTarget>
    capture_hir_packed_update_target(
        semantic::ExpressionId target);
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_packed_update_value(
        const HirPackedUpdateTarget& target,
        std::string_view operation,
        std::optional<semantic::ExpressionId> rhs);
    [[nodiscard]] bool write_hir_packed_update_target(
        const HirPackedUpdateTarget& target,
        RegisterId source);
    struct HirContainerObjectBinding {
        semantic::DeclarationId declaration;
        std::string name;
        runtime::simir::ContainerObjectId object { };
        std::optional<runtime::simir::ContainerRegisterId> local;
        const runtime::simir::ContainerType* type { };
        bool read_only { };
    };
    [[nodiscard]] std::optional<HirContainerObjectBinding>
    hir_container_object_binding(
        semantic::ExpressionId expression) const;
    struct HirContainerElementBinding {
        semantic::DeclarationId declaration;
        std::string name;
        runtime::simir::ContainerObjectId object { };
        std::optional<runtime::simir::ContainerRegisterId> local;
        const runtime::simir::ContainerType* type { };
        const runtime::simir::ContainerType* selected_type { };
        std::vector<semantic::ExpressionId> indices;
        std::size_t width { };
        frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
        bool signed_value { };
        bool read_only { };
    };
    [[nodiscard]] std::optional<HirContainerElementBinding>
    hir_container_element_binding(
        semantic::ExpressionId expression) const;
    struct HirContainerAggregateSelection {
        HirContainerElementBinding element;
        std::vector<std::uint32_t> members;
        runtime::simir::ContainerType leaf;
    };
    [[nodiscard]] std::optional<HirContainerAggregateSelection>
    hir_container_aggregate_selection(
        semantic::ExpressionId expression) const;
    struct HirPackedContainerAggregateProfile {
        std::size_t width { };
        frontend::ValueDomain domain {
            frontend::ValueDomain::Unknown
        };
    };
    [[nodiscard]] std::optional<HirPackedContainerAggregateProfile>
    hir_packed_container_aggregate_profile(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<runtime::simir::RegisterId>
    lower_hir_packed_container_aggregate(
        semantic::ExpressionId expression,
        std::size_t expected_width);
    [[nodiscard]] std::optional<runtime::simir::ContainerType>
    hir_static_container_expression_type(
        semantic::ExpressionId expression) const;
    [[nodiscard]] bool hir_static_array_function_call(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<std::uint64_t>
    hir_systemverilog_container_bit_width(
        const runtime::simir::ContainerType& type) const;
    [[nodiscard]] std::optional<std::int64_t>
    hir_systemverilog_container_query(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<runtime::simir::RegisterId>
    lower_hir_container_element_index(
        const HirContainerElementBinding& element);
    struct HirLoweredContainerElement {
        struct Parent {
            runtime::simir::ContainerRegisterId container { };
            runtime::simir::RegisterId index { };
            bool signed_index { };
        };
        runtime::simir::ContainerRegisterId root { };
        runtime::simir::ContainerRegisterId container { };
        const runtime::simir::ContainerType* type { };
        std::vector<semantic::ExpressionId> indices;
        std::vector<Parent> parents;
    };
    [[nodiscard]] std::optional<HirLoweredContainerElement>
    lower_hir_container_element_path(
        const HirContainerElementBinding& element);
    void publish_hir_container_element_path(
        const HirContainerElementBinding& element,
        const HirLoweredContainerElement& path);
    [[nodiscard]] std::optional<runtime::simir::ContainerRegisterId>
    lower_hir_container_assignment_pattern(
        semantic::ExpressionId expression,
        const HirContainerObjectBinding& target);
    [[nodiscard]] std::optional<runtime::simir::ContainerType>
    hir_systemverilog_container_type(
        const semantic::sv::TypeReference& type) const;
    struct HirStaticContainerValue {
        runtime::simir::ContainerRegisterId value { };
        runtime::simir::ContainerType type;
    };
    struct HirStaticContainerSelection {
        HirContainerObjectBinding base;
        runtime::simir::ContainerType selected_type;
        std::vector<semantic::ExpressionId> prefix_indices;
        std::optional<std::int32_t> selected_left;
    };
    enum class HirStaticContainerSelectionFailure : std::uint8_t {
        none,
        invalid_base,
        unsupported_element_profile,
        nonconstant_bound,
        nonpositive_width,
        direction_or_range,
    };
    [[nodiscard]] std::optional<HirStaticContainerSelection>
    hir_static_container_selection_profile(
        semantic::ExpressionId expression,
        HirStaticContainerSelectionFailure* failure = nullptr) const;
    [[nodiscard]] std::optional<HirStaticContainerSelection>
    hir_static_container_selection(semantic::ExpressionId expression);
    [[nodiscard]] std::optional<runtime::simir::RegisterId>
    lower_hir_static_container_selection_offset(
        const HirStaticContainerSelection& selection);
    [[nodiscard]] std::optional<HirStaticContainerValue>
    lower_hir_static_container_value(semantic::ExpressionId expression);
    enum class HirContainerExpressionPurpose : std::uint8_t {
        reduction_transformation,
        locator_predicate,
        locator_transformation,
    };
    [[nodiscard]] std::optional<std::vector<
        runtime::simir::ContainerPredicateNode>>
    lower_hir_container_expression_graph(
        semantic::ExpressionId expression,
        std::string_view iterator_name,
        const runtime::simir::ContainerType& source_type,
        HirContainerExpressionPurpose purpose);
    [[nodiscard]] std::optional<runtime::simir::RegisterId>
    lower_hir_systemverilog_container_expression(
        semantic::ExpressionId expression,
        std::size_t expected_width);
    [[nodiscard]] bool lower_hir_container_locator_assignment(
        semantic::ExpressionId target,
        semantic::ExpressionId value);
    [[nodiscard]] std::optional<runtime::simir::ContainerRegisterId>
    lower_hir_static_container_actual(
        semantic::ExpressionId expression,
        const runtime::simir::ContainerType& formal_type,
        semantic::DeclarationId contextual_declaration = { });
    [[nodiscard]] bool lower_hir_container_copy_out(
        semantic::ExpressionId target,
        runtime::simir::ContainerRegisterId source);
    void copy_hir_static_container_ordinals(
        runtime::simir::ContainerRegisterId destination,
        const runtime::simir::ContainerType& destination_range,
        runtime::simir::ContainerRegisterId source,
        const runtime::simir::ContainerType& source_range,
        std::optional<runtime::simir::RegisterId> destination_offset = { },
        std::optional<runtime::simir::RegisterId> source_offset = { });
    [[nodiscard]] std::optional<HirRuntimeBinding>
    hir_callable_formal_binding(
        semantic::DeclarationId formal,
        semantic::ScopeId callable_scope,
        semantic::ExpressionId actual,
        semantic::ScopeId actual_scope) const;
    enum class HirStringBindingKind : std::uint8_t {
        local,
        object,
    };
    struct HirStringBinding {
        semantic::DeclarationId declaration;
        HirStringBindingKind kind { HirStringBindingKind::object };
        std::string name;
        std::optional<StringRegisterId> local;
        std::optional<StringObjectId> object;
    };
    [[nodiscard]] std::optional<HirStringBinding> hir_string_binding(
        semantic::DeclarationId declaration,
        semantic::ScopeId process_scope,
        bool require_storage) const;
    [[nodiscard]] bool lower_hir_packed_copy_out(
        semantic::ExpressionId target,
        RegisterId source);
    [[nodiscard]] bool lower_hir_output_actual_write(
        semantic::ExpressionId target,
        RegisterId source);
    [[nodiscard]] bool lower_hir_string_copy_out(
        semantic::ExpressionId target,
        StringRegisterId source);
    [[nodiscard]] bool hir_expression_is_residual(
        semantic::ExpressionId expression) const;
    struct HirClassPropertyProfile {
        std::string identity;
        std::optional<semantic::ExpressionId> receiver;
        std::size_t width { };
        frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
        bool signed_value { };
        bool static_storage { };
        bool writable { };
    };
    [[nodiscard]] std::optional<HirClassPropertyProfile>
    hir_class_property_profile(
        semantic::ExpressionId expression) const;
    struct HirClassMethodActual {
        semantic::DeclarationId formal;
        semantic::ExpressionId expression;
        frontend::PortDirection direction { frontend::PortDirection::Unknown };
        std::string name;
        std::size_t width { };
        frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
        bool string { };
        bool container { };
        bool signed_value { };
    };
    struct HirClassMethodProfile {
        semantic::DeclarationId declaration;
        std::string identity;
        std::optional<semantic::ExpressionId> receiver;
        std::vector<HirClassMethodActual> actuals;
        std::size_t result_width { };
        frontend::ValueDomain result_domain { frontend::ValueDomain::Unknown };
        bool result_signed { };
        bool static_method { };
        bool virtual_dispatch { true };
    };
    [[nodiscard]] std::optional<std::vector<
        runtime::SystemVerilogConstraintTemplate>>
    hir_inline_constraints(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] std::optional<HirClassMethodProfile>
    hir_class_method_profile(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] bool can_lower_hir_class_method_call(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope,
        std::unordered_set<std::uint32_t>& visiting) const;
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_class_method_call(
        semantic::ExpressionId expression,
        std::size_t expected_width);
    struct HirClassTaskProfile {
        semantic::DeclarationId declaration;
        std::string identity;
        std::optional<semantic::ExpressionId> receiver;
        std::optional<std::string> interface_receiver;
        std::vector<HirClassMethodActual> actuals;
        bool static_method { };
        bool automatic { true };
    };
    [[nodiscard]] std::optional<HirClassTaskProfile>
    hir_class_task_profile(
        semantic::StatementId statement,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] bool can_lower_hir_class_task_call(
        semantic::StatementId statement,
        semantic::ScopeId process_scope,
        std::unordered_set<std::uint32_t>& visiting) const;
    [[nodiscard]] bool lower_hir_class_task_call(
        semantic::StatementId statement);
    struct HirInterfaceFunctionProfile {
        semantic::DeclarationId declaration;
        std::string receiver;
    };
    [[nodiscard]] std::optional<HirInterfaceFunctionProfile>
    hir_interface_function_profile(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
    struct HirCallableType {
        std::size_t width { };
        frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
        bool signed_value { };
        bool automatic { true };
        bool string { };
        std::optional<ContainerType> container;
    };
    [[nodiscard]] std::optional<HirCallableType> hir_callable_type(
        semantic::DeclarationId declaration,
        std::size_t contextual_width = 0U) const;
    [[nodiscard]] std::optional<HirCallableResolution>
    resolve_hir_vhdl_function_call(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope,
        std::size_t contextual_width) const;
    [[nodiscard]] std::optional<HirCallableResolution>
    resolve_hir_vhdl_procedure_call(
        semantic::StatementId statement,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] std::optional<std::vector<semantic::ExpressionId>>
    bind_hir_function_actuals(
        semantic::ExpressionId expression,
        semantic::DeclarationId declaration) const;
    [[nodiscard]] bool can_lower_hir_function_call(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope,
        std::unordered_set<std::uint32_t>& visiting,
        bool string_result = false) const;
    struct HirDpiFunctionActual {
        semantic::ExpressionId expression;
        std::string name;
        frontend::PortDirection direction {
            frontend::PortDirection::Unknown
        };
        std::size_t width { };
        frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
        bool signed_value { };
    };
    struct HirDpiFunctionProfile {
        std::string linkage_name;
        std::vector<HirDpiFunctionActual> actuals;
        std::size_t result_width { };
        frontend::ValueDomain result_domain {
            frontend::ValueDomain::Unknown
        };
        bool result_signed { };
    };
    [[nodiscard]] std::optional<HirDpiFunctionProfile>
    hir_dpi_function_profile(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] bool can_lower_hir_dpi_function_call(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope,
        std::unordered_set<std::uint32_t>& visiting) const;
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_dpi_function_call(
        semantic::ExpressionId expression,
        std::size_t expected_width);
    [[nodiscard]] std::optional<RegisterId> lower_hir_function_call(
        semantic::ExpressionId expression,
        std::size_t expected_width);
    struct HirFunctionCallValue {
        std::optional<RegisterId> packed;
        std::optional<HirStaticContainerValue> container;
    };
    [[nodiscard]] std::optional<HirFunctionCallValue>
    lower_hir_function_call_value(
        semantic::ExpressionId expression,
        std::size_t expected_width);
    [[nodiscard]] std::optional<HirStaticContainerValue>
    lower_hir_container_function_call(semantic::ExpressionId expression);
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_callable_actual(
        semantic::ExpressionId actual,
        semantic::DeclarationId formal,
        std::size_t expected_width);
    [[nodiscard]] std::optional<StringRegisterId>
    lower_hir_string_function_call(semantic::ExpressionId expression);
    [[nodiscard]] std::optional<std::vector<semantic::ExpressionId>>
    bind_hir_vhdl_procedure_actuals(
        semantic::StatementId statement,
        semantic::DeclarationId declaration) const;
    [[nodiscard]] bool can_lower_hir_vhdl_procedure_call(
        semantic::StatementId statement,
        semantic::ScopeId process_scope,
        std::unordered_set<std::uint32_t>& visiting) const;
    [[nodiscard]] bool lower_hir_vhdl_procedure_call(
        semantic::StatementId statement);
    [[nodiscard]] bool lower_pending_hir_callables();
    [[nodiscard]] bool lower_hir_callable_body(std::size_t callable);
    [[nodiscard]] bool lower_hir_callable_return(
        std::optional<semantic::ExpressionId> value);

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
    [[nodiscard]] SizedIntegralComparison size_integral_comparison(
        InsideIntegralOperand lhs,
        InsideIntegralOperand rhs);
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_membership_expression(semantic::ExpressionId expression);

    void prepare_hir_procedural_continuous_assignments(
        std::span<const semantic::StatementId> statements);
    [[nodiscard]] std::string hir_procedural_target_key(
        semantic::ExpressionId expression) const;
    [[nodiscard]] bool hir_procedural_target_is_visible(
        semantic::ExpressionId expression) const;
    [[nodiscard]] bool lower_hir_procedural_continuous_assignment(
        semantic::StatementId statement);
    [[nodiscard]] bool lower_hir_force_release(
        semantic::ExpressionId target,
        std::optional<semantic::ExpressionId> value,
        std::optional<RegisterId> lowered_value,
        bool force,
        semantic::SourceSpanId source,
        bool vhdl_driving_value = false);
    void materialize_hir_procedural_continuous_assignments();

    void validate_read_only_signal_writes(
        const frontend::SourceSpan& source,
        std::optional<SignalId> permitted_signal);
    void emit_debug_point(
        DebugPointKind kind,
        const frontend::SourceSpan& span);
    [[nodiscard]] std::string debug_scope_name() const;
    RegisterId allocate_register(
        std::size_t width,
        frontend::ValueDomain domain);
    StringRegisterId allocate_string_register();
    ContainerRegisterId allocate_container_register(
        const ContainerType& type);
    [[nodiscard]] std::size_t register_width(RegisterId id) const;
    [[nodiscard]] frontend::ValueDomain register_domain(
        RegisterId id) const;
    [[nodiscard]] RegisterId resize_register(
        RegisterId source,
        std::size_t width,
        bool sign_extend);
    [[nodiscard]] RegisterId convert_to_two_state(RegisterId source);
    [[nodiscard]] RegisterId widen_enumeration_ordinal(RegisterId source);
    void report(
        std::string code,
        std::string message,
        frontend::SourceSpan span);

    ElaboratedDesign& design_;
    const std::unordered_map<std::string, SignalId>& signals_;
    const std::unordered_set<SignalId>& read_only_signals_;
    const std::unordered_map<std::string, StringObjectId>& string_objects_;
    const std::unordered_set<StringObjectId>& read_only_string_objects_;
    const std::unordered_map<std::string, ContainerObjectId>&
        container_objects_;
    const std::unordered_set<std::string>& read_only_container_objects_;
    std::vector<Diagnostic>& diagnostics_;
    const semantic::SpecializedHirUnit* specialized_hir_unit_ { };
    const std::unordered_map<std::string, std::uint64_t>*
        systemverilog_interface_handles_ { };
    semantic::ScopeId hir_process_scope_;
    std::unordered_map<std::uint32_t, RegisterId> hir_local_registers_;
    std::unordered_map<std::uint32_t, StringRegisterId>
        hir_local_string_registers_;
    std::unordered_map<std::uint32_t, ContainerRegisterId>
        hir_local_container_registers_;
    std::unordered_map<std::uint32_t, ContainerType>
        hir_local_container_types_;
    struct HirCallableFrame {
        semantic::DeclarationId declaration;
        semantic::ScopeId scope;
        HirCallableType type;
        RegisterId result { };
        StringRegisterId string_result { };
        std::optional<ContainerRegisterId> container_result;
        std::optional<ContainerRegisterId> container_result_default;
        std::optional<RegisterId> class_receiver;
        std::optional<std::string> interface_receiver;
        std::vector<semantic::DeclarationId> formals;
        std::vector<semantic::DeclarationId> profile_formals;
        std::vector<RegisterId> arguments;
        std::vector<StringRegisterId> string_arguments;
        std::vector<ContainerRegisterId> container_arguments;
        std::vector<std::optional<ContainerRegisterId>>
            container_output_defaults;
        std::vector<bool> argument_is_string;
        std::vector<bool> argument_is_container;
        std::vector<frontend::PortDirection> directions;
        std::vector<HirGenericBinding> generic_bindings;
        std::string debug_name;
        std::unordered_map<std::uint32_t, RegisterId> static_variables;
        std::unordered_map<std::uint32_t, StringRegisterId>
            static_string_variables;
        std::unordered_map<std::uint32_t, ContainerRegisterId>
            static_container_variables;
        std::unordered_map<std::uint32_t, ContainerType>
            static_container_types;
        std::uint32_t invocation_identity { };
        std::vector<RegisterId> invocation_registers;
        std::vector<StringRegisterId> invocation_strings;
        std::vector<ContainerRegisterId> invocation_containers;
        std::vector<InstructionIndex> invocation_push_sites;
        std::optional<InstructionIndex> target;
        std::vector<InstructionIndex> call_sites;
        std::vector<InstructionIndex> return_jumps;
        bool allocated { };
        bool queued { };
        bool lowered { };
        bool snapshot_complete { };
        bool function { true };
    };
    [[nodiscard]] bool allocate_hir_static_callable_declarations(
        std::size_t frame_index);
    std::vector<HirCallableFrame> hir_callable_frames_;
    std::unordered_map<std::uint64_t, std::size_t> hir_callable_indices_;
    std::unordered_map<std::string, std::size_t>
        hir_interface_callable_indices_;
    std::deque<std::size_t> pending_hir_callables_;
    std::optional<std::size_t> active_hir_callable_;
    bool active_hir_vhdl_protected_method_ { };
    struct HirVhdlAccessHeap {
        ContainerRegisterId objects { };
        ContainerRegisterId issued_handles { };
        std::uint64_t maximum_live_objects { };
        semantic::vhdl::SubtypeIndication designated_subtype;
    };
    std::unordered_map<std::uint32_t, HirVhdlAccessHeap>
        hir_vhdl_access_heaps_;
    std::optional<RegisterId> hir_class_receiver_register_;
    mutable std::vector<HirLetFrame> hir_let_frames_;
    mutable std::vector<std::vector<HirCasePatternBinding>>
        hir_case_pattern_binding_frames_;
    mutable std::vector<semantic::CompiledBindingFrame>
        hir_generic_binding_frames_;
    Process process_;
    bool sample_concurrent_assertion_reads_ { };
    struct DeferredAssertionActionHandoff {
        InstructionIndex fork { };
        InstructionIndex continuation_jump { };
        InstructionIndex branch { };
    };
    std::optional<runtime::SchedulerPhase>
        deferred_assertion_action_phase_;
    std::optional<DeferredAssertionActionHandoff>
        deferred_assertion_action_handoff_;
    std::optional<std::uint32_t> systemverilog_program_owner_;
    std::vector<Process> generated_processes_;
    std::vector<SignalId> implicit_signal_dependencies_;
    RegisterId next_register_ { };
    StringRegisterId next_string_register_ { };
    ContainerRegisterId next_container_register_ { };
    std::vector<std::size_t> register_widths_;
    std::vector<frontend::ValueDomain> register_domains_;
    std::unordered_map<std::string, RegisterId> locals_;
    struct ProceduralContinuousAssignment {
        semantic::StatementId statement;
        semantic::ExpressionId target;
        semantic::ExpressionId value;
        semantic::SourceSpanId source;
        std::string statement_key;
        SignalId active { };
        std::string active_name;
        std::string target_key;
        bool valid { };
    };
    std::vector<ProceduralContinuousAssignment>
        procedural_continuous_assignments_;
    std::unordered_map<std::string, std::size_t>
        procedural_continuous_assignment_by_statement_;
    std::unordered_map<std::string, std::vector<std::size_t>>
        procedural_continuous_assignments_by_target_;
    std::unordered_map<std::string, StringRegisterId> string_locals_;
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
        InstructionIndex begin { };
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
    std::uint32_t next_callable_invocation_identity_ { 1 };
    frontend::ProcessKind process_kind_ {
        frontend::ProcessKind::VhdlProcess
    };
    frontend::Language language_ { frontend::Language::Vhdl2008 };
    frontend::VhdlStandard vhdl_standard_ {
        frontend::VhdlStandard::Vhdl2008
    };
    frontend::StandardRevision systemverilog_standard_ {
        frontend::StandardRevision::SystemVerilog2017
    };
    std::string hierarchy_;
};

} // namespace fsim::elaboration
