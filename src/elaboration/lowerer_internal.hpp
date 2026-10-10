// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "elaborator_internal.hpp"
#include "scoped_bindings.hpp"
#include "systemverilog_template_bindings.hpp"
#include "fsim/elaboration/coverage_hir_points.hpp"
#include "fsim/frontend/parser.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"

#include <functional>
#include <map>
#include <vector>

namespace fsim::elaboration {

// A `timescale or timeunit spelling such as "10ns" in femtoseconds.
[[nodiscard]] std::optional<std::uint64_t>
systemverilog_unit_time_scale_femtoseconds(std::string_view spelling) noexcept;

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
        const SignalBindings& signals,
        const ReadOnlySignalBindings& read_only_signals,
        const StringObjectBindings& string_objects,
        const ReadOnlyStringBindings& read_only_string_objects,
        const ContainerObjectBindings& container_objects,
        const ReadOnlyContainerBindings& read_only_container_objects,
        std::vector<Diagnostic>& diagnostics);

    /// Select the parser-independent working HIR for subsequent ID-based
    /// lowering. The caller retains ownership for the lifetime of this
    /// Lowerer.
    void set_specialized_hir_unit(
        const semantic::SpecializedHirUnit* unit) noexcept;
    void set_hir_container_declaration_bindings(
        const ContainerDeclarationBindings* bindings) noexcept;
    void set_hir_code_coverage_context(
        const CoverageHirContext* coverage, bool module) noexcept;
    void set_hir_code_coverage_active(bool active) noexcept;
    void set_systemverilog_interface_handles(
        const std::unordered_map<std::string, std::uint64_t>* handles,
        const std::unordered_map<std::string, std::string>* types = nullptr)
        noexcept;
    // The interface type bound to a generic `interface` port of this
    // instance, or empty.
    [[nodiscard]] std::string hir_generic_interface_type(
        std::string_view receiver) const;
    void diagnose_hir_systemverilog_file_process(
        semantic::ProcessId process);
    [[nodiscard]] std::optional<semantic::SourceSpanId>
    hir_vhdl_unspecified_inference_failure(
        semantic::ProcessId process) const;
    [[nodiscard]] std::optional<Process> lower_hir_process(
        semantic::ProcessId process,
        frontend::Language language,
        std::string_view hierarchy);
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
    [[nodiscard]] std::optional<SignalId>
    hir_concurrent_port_signal(semantic::DeclarationId declaration) const;
    using HirConcurrentContainerElementSignal
        = SystemVerilogTemplateElementSignalBinding;
    /// Return a complete, validated per-element signal map for one exact
    /// SystemVerilog container declaration. An empty result keeps the cache
    /// on ordinary lowering when the declaration has no eligible alias.
    [[nodiscard]] std::vector<HirConcurrentContainerElementSignal>
    hir_concurrent_container_element_signals(
        semantic::DeclarationId declaration) const;
    [[nodiscard]] bool hir_concurrent_signal_read_only(
        SignalId signal) const noexcept;
    [[nodiscard]] bool has_generated_processes() const noexcept;
    // SystemVerilog hierarchical references (IEEE 1800-2017 23.6-23.8)
    // resolved, or missed, since the last reset. A process that missed one
    // may be lowered again once more of the hierarchy exists; a process
    // that used one must not become a shared process template.
    void reset_hierarchical_reference_state() noexcept
    {
        hierarchical_reference_used_ = false;
        hierarchical_reference_missed_ = false;
    }
    [[nodiscard]] bool hierarchical_reference_used() const noexcept
    {
        return hierarchical_reference_used_;
    }
    [[nodiscard]] bool hierarchical_reference_missed() const noexcept
    {
        return hierarchical_reference_missed_;
    }
    // Whether the caller retries a process that misses a hierarchical
    // reference once the unit's instances exist.
    void set_hierarchical_reference_retry(const bool retry) noexcept
    {
        hierarchical_reference_retry_ = retry;
    }
    [[nodiscard]] std::uint32_t next_hir_callable_invocation_identity()
        const noexcept;
    [[nodiscard]] bool advance_hir_callable_invocation_identity(
        std::uint32_t expected_before,
        std::uint32_t after) noexcept;
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

    // Materializes a composite VHDL package constant as a read-only design
    // signal on its first use and returns it; nullopt keeps constant folding.
    using PackageConstantSignal
        = std::function<std::optional<SignalId>(semantic::DeclarationId)>;
    void set_package_constant_signal(PackageConstantSignal materialize)
    {
        package_constant_signal_ = std::move(materialize);
    }

private:
    struct HirEffectiveVhdlSubtypeCacheEntry {
        semantic::vhdl::SubtypeIndication subtype;
        semantic::ScopeId scope;
        std::vector<semantic::CompiledBindingFrame> binding_frames;
        semantic::vhdl::SubtypeIndication result;
    };

    struct HirExpressionResolutionCacheEntry {
        std::vector<semantic::CompiledBindingFrame> binding_frames;
        semantic::CompiledDeclarationResolution resolution;
    };

    class HirEffectiveVhdlSubtypeCacheScope final {
    public:
        explicit HirEffectiveVhdlSubtypeCacheScope(
            const Lowerer& lowerer) noexcept
            : lowerer_ { &lowerer }
        {
            lowerer_->begin_hir_effective_vhdl_subtype_cache_scope();
        }

        HirEffectiveVhdlSubtypeCacheScope(
            const HirEffectiveVhdlSubtypeCacheScope&) = delete;
        HirEffectiveVhdlSubtypeCacheScope& operator=(
            const HirEffectiveVhdlSubtypeCacheScope&) = delete;

        ~HirEffectiveVhdlSubtypeCacheScope()
        {
            lowerer_->end_hir_effective_vhdl_subtype_cache_scope();
        }

    private:
        const Lowerer* lowerer_;
    };

    void begin_hir_effective_vhdl_subtype_cache_scope() const noexcept;
    void end_hir_effective_vhdl_subtype_cache_scope() const noexcept;
    void clear_hir_effective_vhdl_subtype_cache() const noexcept;
    [[nodiscard]] const semantic::CompiledDeclarationResolution*
    find_hir_expression_resolution_cache(
        semantic::ExpressionId expression) const noexcept;
    void cache_hir_expression_resolution(
        semantic::ExpressionId expression,
        const semantic::CompiledDeclarationResolution& resolution) const noexcept;
    void clear_hir_expression_resolution_cache() const noexcept;
    [[nodiscard]] const std::optional<semantic::TypeId>*
    find_hir_vhdl_subtype_name_cache(
        semantic::ScopeId scope,
        std::string_view spelling) const noexcept;
    void cache_hir_vhdl_subtype_name(
        semantic::ScopeId scope,
        std::string_view spelling,
        std::optional<semantic::TypeId> result) const noexcept;
    void clear_hir_vhdl_subtype_name_cache() const noexcept;
    [[nodiscard]] std::optional<std::int64_t>
    hir_pure_integral_attempt(semantic::ExpressionId expression) const;
    void clear_hir_pure_integral_attempt_cache() const noexcept;

    mutable std::size_t hir_effective_vhdl_subtype_cache_depth_ { };
    mutable std::size_t hir_effective_vhdl_subtype_cache_bytes_ { };
    mutable std::vector<HirEffectiveVhdlSubtypeCacheEntry>
        hir_effective_vhdl_subtype_cache_;
    mutable std::size_t hir_expression_resolution_cache_bytes_ { };
    mutable std::size_t hir_expression_resolution_cache_entries_ { };
    mutable std::size_t hir_expression_resolution_cache_hits_ { };
    mutable std::size_t hir_expression_resolution_cache_misses_ { };
    mutable std::size_t hir_expression_resolution_cache_cap_drops_ { };
    mutable std::map<semantic::ExpressionId,
        std::vector<HirExpressionResolutionCacheEntry>>
        hir_expression_resolution_cache_;
    mutable std::size_t hir_vhdl_subtype_name_cache_bytes_ { };
    mutable std::size_t hir_vhdl_subtype_name_cache_entries_ { };
    mutable std::size_t hir_vhdl_subtype_name_cache_hits_ { };
    mutable std::size_t hir_vhdl_subtype_name_cache_misses_ { };
    mutable std::size_t hir_vhdl_subtype_name_cache_cap_drops_ { };
    mutable std::map<std::uint32_t,
        std::map<std::string, std::optional<semantic::TypeId>, std::less<>>>
        hir_vhdl_subtype_name_cache_;
    mutable std::map<semantic::ExpressionId, std::optional<std::int64_t>>
        hir_pure_integral_attempt_cache_;
    mutable std::size_t hir_pure_integral_attempt_hits_ { };
    mutable std::size_t hir_pure_integral_attempt_misses_ { };
    mutable std::size_t hir_pure_integral_attempt_cap_drops_ { };

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
        bool event_expression_scalar_edge { };
        std::optional<semantic::ExpressionId> input_actual;
        std::optional<SignalId> input_actual_destination;
        std::optional<semantic::vhdl::SubtypeIndication>
            input_actual_vhdl_context;
        std::optional<SignalId> output_actual_source;
        std::optional<semantic::ExpressionId> output_actual;
        std::optional<SignalId> procedural_assignment_active;
        std::uint32_t procedural_assignment_owner { };
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
    [[nodiscard]] std::optional<std::optional<HirPackedRange>>
    hir_vhdl_callable_formal_range(
        semantic::DeclarationId declaration) const;
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
    // A VHDL index expression of an enumeration type (IEEE 1076-2008 5.3.2.1):
    // its runtime value is the ordinal, widened before indexing.
    [[nodiscard]] bool hir_vhdl_enumeration_index(
        semantic::ExpressionId index,
        semantic::ScopeId process_scope) const;
    // The array type an array subtype belongs to, for overload resolution:
    // STD_LOGIC_VECTOR and STD_ULOGIC_VECTOR, UNSIGNED, SIGNED and
    // BIT_VECTOR are distinct (IEEE 1076-2008 4.5.2). Nullopt when unknown.
    [[nodiscard]] std::optional<std::string> hir_vhdl_array_type_family(
        const semantic::vhdl::SubtypeIndication& subtype) const;
    // The enumeration type a VHDL for-loop parameter iterates over, when the
    // value names one; the parameter's runtime value is the ordinal.
    [[nodiscard]] std::optional<semantic::TypeId>
    hir_vhdl_loop_parameter_enumeration(
        semantic::ExpressionId value) const;
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
    // A member selected through a virtual interface handle (`vif.data`):
    // the handle's declaration and, for every elaborated instance of the
    // interface type, its handle and member signal.
    struct HirVirtualInterfaceMember {
        semantic::DeclarationId receiver;
        std::vector<std::pair<std::uint64_t, SignalId>> candidates;
        std::size_t width { };
        frontend::ValueDomain domain { frontend::ValueDomain::Logic4 };
    };
    [[nodiscard]] std::optional<HirVirtualInterfaceMember>
    hir_virtual_interface_member(semantic::ExpressionId expression_id) const;
    [[nodiscard]] std::optional<HirVirtualInterfaceMember>
    hir_virtual_interface_member(
        std::string_view name, semantic::ScopeId scope) const;
    [[nodiscard]] std::optional<RegisterId> lower_hir_virtual_interface_read(
        const HirVirtualInterfaceMember& member);
    [[nodiscard]] std::optional<RegisterId> lower_hir_virtual_interface_handle(
        semantic::DeclarationId receiver);
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
    // The index of the non-null operand of a VHDL `&` whose other operand
    // is a statically null array, if exactly one operand is null.
    // The CHARACTER code of a VHDL character literal lowered self-determined
    // or in an 8-bit context: a non-logic character, a CHARACTER control
    // name (NUL), or a logic character ('0') whose context is CHARACTER.
    [[nodiscard]] std::optional<std::uint64_t>
    hir_vhdl_character_literal_code(
        semantic::ExpressionId expression, std::size_t expected_width) const;
    // The field width of a SystemVerilog decimal conversion: without an
    // explicit width or %0d, a value is padded to the width of its largest
    // magnitude (IEEE 1800-2017 21.2.1.3).
    [[nodiscard]] std::uint32_t hir_systemverilog_decimal_width(
        semantic::ExpressionId expression,
        runtime::simir::OutputFormat format,
        bool suppress_leading_zero,
        std::uint32_t minimum_width) const;
    // The declared type of a SystemVerilog parameter that has an explicit
    // type or range, which its value converts to.
    [[nodiscard]] std::optional<semantic::sv::TypeReference>
    hir_systemverilog_parameter_type(semantic::ExpressionId expression) const;
    // The packed dimensions of a SystemVerilog multidimensional packed array
    // expression, outermost first: a name's declared dimensions, or those
    // left after an element select. Empty for an ordinary vector.
    [[nodiscard]] std::vector<HirPackedRange>
    hir_systemverilog_packed_shape(semantic::ExpressionId expression) const;
    // Whether a call actual sign-extends to its formal's width: a
    // SystemVerilog value extends by its own signedness (IEEE 1800-2017
    // 11.8.2, 13.5), other languages by the formal's.
    [[nodiscard]] bool hir_actual_extension_signed(
        semantic::ExpressionId actual, bool formal_signed) const;
    // The width an index target writes: an element of a multidimensional
    // packed array, otherwise one bit.
    [[nodiscard]] std::optional<std::size_t> hir_index_target_width(
        semantic::ExpressionId target) const;
    // The bit offset, in a flattened multidimensional packed array, of the
    // element an index selects in its outermost dimension: a signed 32-bit
    // register that is X for an unknown index.
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_systemverilog_element_offset(semantic::ExpressionId index,
        const HirPackedRange& dimension, std::size_t element_width);
    // Whether a VHDL expression has the predefined CHARACTER type.
    [[nodiscard]] bool hir_vhdl_character_typed(
        semantic::ExpressionId expression) const;
    // Collects the logic character literals of the current unit whose
    // context (assignment target, initialized object, comparison operand,
    // case selector, or attribute prefix) is CHARACTER.
    void collect_hir_vhdl_character_contexts() const;
    mutable const semantic::vhdl::Hir* hir_vhdl_character_context_hir_ { };
    mutable std::uint64_t hir_vhdl_character_context_revision_ { };
    mutable std::unordered_set<std::uint32_t>
        hir_vhdl_character_context_literals_;
    [[nodiscard]] std::optional<std::size_t>
    hir_vhdl_null_concatenation_operand(
        std::span<const semantic::ExpressionId> operands) const;
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
    // An enumeration method (IEEE 1800-2017 6.19.5): `e.first()`, or the
    // parenthesis-free `e.next` that the frontend keeps as a dotted name.
    struct HirEnumerationMethod {
        std::string method;
        std::optional<semantic::ExpressionId> receiver;
        semantic::DeclarationId receiver_declaration;
        std::optional<semantic::ExpressionId> count;
        std::vector<std::uint64_t> values;
        std::vector<std::string> names;
        std::size_t width { };
        bool four_state { };
        bool signed_value { };
    };
    [[nodiscard]] std::optional<HirEnumerationMethod>
    hir_systemverilog_enumeration_method(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<HirEnumerationMethod>
    hir_systemverilog_enumeration_profile(HirEnumerationMethod method,
        semantic::DeclarationId declaration) const;
    // The literal name of an enumeration variable's value, or "" (6.19.5.6).
    [[nodiscard]] std::optional<StringRegisterId>
    lower_hir_systemverilog_enumeration_value_name(
        semantic::ExpressionId expression);
    [[nodiscard]] std::optional<StringRegisterId>
    lower_hir_systemverilog_enumeration_name(
        const HirEnumerationMethod& method);
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_systemverilog_enumeration_receiver(
        const HirEnumerationMethod& method);
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_systemverilog_enumeration_method(
        semantic::ExpressionId expression, std::size_t expected_width);
    [[nodiscard]] std::optional<StringRegisterId>
    lower_hir_systemverilog_enumeration_name(
        semantic::ExpressionId expression);
    // An integral value converted to a real or shortreal payload; x and z
    // bits convert as zero (IEEE 1800-2017 6.12.2).
    [[nodiscard]] RegisterId convert_hir_integral_to_real(RegisterId value,
        bool signed_value, frontend::SystemVerilogScalarKind target);
    // A real or shortreal payload rounded to a 64-bit signed integral value
    // (IEEE 1800-2017 6.12.2: to nearest, ties away from zero).
    [[nodiscard]] RegisterId convert_hir_real_to_integral(RegisterId value,
        frontend::SystemVerilogScalarKind source);
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
    [[nodiscard]] std::optional<semantic::vhdl::SubtypeIndication>
    hir_vhdl_original_integer_generic_subtype(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<std::int64_t>
    hir_vhdl_generate_iterator_value(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
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
    // VHDL REAL and floating types share the IEEE double representation and
    // runtime scalar operations of SystemVerilog real.
    // A VHDL name that denotes a function called without an actual
    // parameter part (IEEE 1076-2008 9.3.4).
    [[nodiscard]] bool hir_vhdl_function_name(
        semantic::ExpressionId expression) const;
    // The predefined function STD.STANDARD.NOW (IEEE 1076-2008 16.3),
    // unless a user declaration named NOW hides it.
    [[nodiscard]] bool hir_vhdl_now_expression(
        semantic::ExpressionId expression) const;
    [[nodiscard]] bool hir_vhdl_subtype_is_real(
        const semantic::vhdl::SubtypeIndication& subtype) const;
    [[nodiscard]] bool hir_vhdl_expression_is_real(
        semantic::ExpressionId expression) const;
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
    [[nodiscard]] std::optional<RegisterId> lower_hir_expression(
        semantic::ExpressionId expression,
        std::size_t expected_width,
        frontend::SystemVerilogScalarKind scalar_context
        = frontend::SystemVerilogScalarKind::None);
    [[nodiscard]] std::optional<RegisterId> lower_hir_expression_untraced(
        semantic::ExpressionId expression,
        std::size_t expected_width,
        frontend::SystemVerilogScalarKind scalar_context);
    [[nodiscard]] std::optional<RegisterId> lower_hir_expression_impl(
        semantic::ExpressionId expression,
        std::size_t expected_width,
        frontend::SystemVerilogScalarKind scalar_context);
    // The innermost expression that failed to lower without its own
    // diagnostic while the current statement was lowered; named in the
    // statement's FSIM-ELAB-HIR-001 report.
    std::optional<std::string> hir_unlowered_expression_;
    // An unsigned SystemVerilog operation zero-extends its signed
    // context-determined operands (IEEE 1800-2017 11.8.2): the next lowered
    // operand extends without sign, and an operator propagates that to its
    // own operands.
    bool hir_unsigned_extension_ = false;
    bool hir_unsigned_operation_ = false;
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
        bool vhdl_port { };
        semantic::vhdl::Direction vhdl_direction {
            semantic::vhdl::Direction::unknown
        };
    };
    [[nodiscard]] bool signal_binding_can_emit_write(
        const HirRuntimeBinding& binding) const noexcept;
    [[nodiscard]] bool signal_binding_is_writable(
        const HirRuntimeBinding& binding) const noexcept;
    void record_readonly_signal_write(SignalId signal);
    void record_readonly_vhdl_output_write(
        const HirRuntimeBinding& binding);
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
    [[nodiscard]] std::optional<HirContainerObjectBinding>
    hir_container_object_binding_for_declaration(
        semantic::DeclarationId declaration) const;
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
        struct RuntimeReadProfile {
            frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
            bool default_x { };
        };
        std::optional<RuntimeReadProfile> runtime_read_profile;
    };
    [[nodiscard]] std::optional<
        HirContainerElementBinding::RuntimeReadProfile>
    hir_sv_runtime_container_read_profile(
        const HirContainerElementBinding& element) const;
    struct HirStaticContainerIndex {
        std::uint32_t ordinal { };
        std::uint32_t count { };
        std::string suffix;
    };
    [[nodiscard]] std::optional<HirStaticContainerIndex>
    hir_static_container_index(
        const HirContainerElementBinding& element) const;
    struct HirStaticContainerSignalExtract {
        runtime::simir::SignalId signal { };
        std::uint32_t source_width { };
        std::uint32_t offset { };
        std::uint32_t width { };
        frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
    };
    [[nodiscard]] std::optional<HirContainerElementBinding>
    hir_container_element_binding(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<HirStaticContainerSignalExtract>
    hir_static_element_signal_extract(
        const HirContainerElementBinding& element) const;
    [[nodiscard]] std::optional<HirStaticContainerSignalExtract>
    hir_static_container_signal_extract(
        const HirContainerElementBinding& element) const;
    struct HirContainerAggregateSelection {
        HirContainerElementBinding element;
        std::vector<std::uint32_t> members;
        runtime::simir::ContainerType leaf;
    };
    // `allow_string` also admits a string member leaf.
    [[nodiscard]] std::optional<HirContainerAggregateSelection>
    hir_container_aggregate_selection(
        semantic::ExpressionId expression, bool allow_string = false) const;
    [[nodiscard]] bool hir_sv_dynamic_aggregate_member_read_supported(
        const HirContainerAggregateSelection& selection) const;
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
    [[nodiscard]] bool hir_sv_runtime_container_read_supported(
        const HirContainerElementBinding& element) const;
    [[nodiscard]] std::optional<runtime::simir::RegisterId>
    lower_hir_sv_runtime_container_read_index(
        const HirContainerElementBinding& element);
    void lower_hir_sv_runtime_container_read(
        const HirContainerElementBinding& element,
        runtime::simir::ContainerRegisterId container,
        runtime::simir::RegisterId index,
        runtime::simir::RegisterId destination);
    [[nodiscard]] bool hir_checked_fixed_array_index(
        const HirContainerElementBinding& element) const;
    // Invalid fixed-array coordinates produce a disjoint sentinel. Reads
    // return the element default; writes guard publication after evaluation.
    [[nodiscard]] std::optional<runtime::simir::RegisterId>
    lower_hir_fixed_array_index(const HirContainerElementBinding& element);
    [[nodiscard]] std::optional<runtime::simir::InstructionIndex>
    begin_hir_fixed_array_write(
        const HirContainerElementBinding& element,
        runtime::simir::RegisterId index);
    void end_hir_fixed_array_write(
        std::optional<runtime::simir::InstructionIndex> branch);
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
    // Continuous SV concatenation copyout accepts static packed signal leaves
    // and stages them in source order; other callers retain blocking copyout.
    enum class HirPackedCopyOutMode : std::uint8_t {
        blocking,
        systemverilog_active_update,
    };
    [[nodiscard]] bool lower_hir_packed_copy_out(
        semantic::ExpressionId target,
        RegisterId source,
        HirPackedCopyOutMode mode = HirPackedCopyOutMode::blocking);
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
        semantic::ExpressionId expression,
        bool string_property = false) const;
    // A string class property read into a string register, or written from
    // one, through the class method boundary.
    [[nodiscard]] std::optional<StringRegisterId>
    lower_hir_class_string_property_read(semantic::ExpressionId expression);
    // The signal a plain event-control name denotes: a local signal, or an
    // imported package variable.
    [[nodiscard]] std::optional<SignalId> hir_named_signal(
        const std::string& name, semantic::ScopeId scope);
    [[nodiscard]] bool lower_hir_class_string_property_write(
        semantic::ExpressionId target, StringRegisterId value);
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
    // Runtime VHDL T'IMAGE(X) (quoted character literals) and TO_STRING,
    // TO_HSTRING and TO_OSTRING of a runtime value (IEEE 1076-2008 5.7,
    // 16.2). `type_prefix` names T for 'IMAGE; vector_format selects the
    // digit grouping for an array value.
    [[nodiscard]] std::optional<StringRegisterId> lower_hir_vhdl_runtime_image(
        semantic::ExpressionId value,
        std::optional<semantic::ExpressionId> type_prefix,
        runtime::simir::OutputFormat vector_format,
        bool image_quotes);
    [[nodiscard]] std::optional<std::vector<semantic::ExpressionId>>
    bind_hir_vhdl_procedure_actuals(
        semantic::StatementId statement,
        semantic::DeclarationId declaration) const;
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
    // $monitor and $strobe arguments other than plain signals are evaluated
    // into hidden signals by generated always_comb drivers.
    void materialize_hir_monitor_drivers();
    // A hierarchical reference to an instance parameter, such as `u1.WIDTH`
    // (IEEE 1800-2017 23.6), as its elaborated value.
    struct HirHierarchicalParameter {
        runtime::PackedLogic4 value;
        bool signed_value { };
        frontend::SystemVerilogScalarKind scalar_kind {
            frontend::SystemVerilogScalarKind::None
        };
    };
    [[nodiscard]] std::optional<HirHierarchicalParameter>
    hir_hierarchical_parameter(semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<SignalId> hir_monitor_expression_signal(
        semantic::ExpressionId expression);

    void validate_read_only_signal_writes(
        const frontend::SourceSpan& source);
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
    void initialize_delayed_net_driver(SignalId signal, std::size_t offset,
        std::size_t width, bool continuous_assignment);
    [[nodiscard]] RegisterId widen_enumeration_ordinal(RegisterId source);
    void report(
        std::string code,
        std::string message,
        frontend::SourceSpan span);

    ElaboratedDesign& design_;
    const SignalBindings& signals_;
    const ReadOnlySignalBindings& read_only_signals_;
    PackageConstantSignal package_constant_signal_;
    const StringObjectBindings& string_objects_;
    const ReadOnlyStringBindings& read_only_string_objects_;
    const ContainerObjectBindings& container_objects_;
    const ReadOnlyContainerBindings& read_only_container_objects_;
    std::vector<Diagnostic>& diagnostics_;
    const semantic::SpecializedHirUnit* specialized_hir_unit_ { };
    const CoverageHirContext* coverage_hir_context_ { };
    bool coverage_hir_process_active_ { };
    // Set for one function call lowered as a call statement, whose result is
    // discarded and which may name a void function.
    bool discarding_call_result_ { };
    const std::unordered_map<std::string, std::uint64_t>*
        systemverilog_interface_handles_ { };
    const std::unordered_map<std::string, std::string>*
        systemverilog_interface_types_ { };
    const ContainerDeclarationBindings*
        container_declaration_bindings_ { };
    semantic::ScopeId hir_process_scope_;
    std::map<std::size_t, SignalId>
        readonly_signal_write_operations_;
    std::unordered_map<std::uint32_t, RegisterId> hir_local_registers_;
    std::unordered_map<std::uint32_t, StringRegisterId>
        hir_local_string_registers_;
    std::unordered_map<std::uint32_t, ContainerRegisterId>
        hir_local_container_registers_;
    std::unordered_map<std::uint32_t, ContainerType>
        hir_local_container_types_;
    // Process-level locals while deferred callable bodies are lowered. A
    // subprogram declared in a process shares the process register file and
    // may name the process's variables (IEEE 1076-2008 4.3 and 10.6.2.1).
    std::unordered_map<std::uint32_t, RegisterId>
        hir_enclosing_local_registers_;
    std::unordered_map<std::uint32_t, StringRegisterId>
        hir_enclosing_local_string_registers_;
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
        std::vector<std::pair<semantic::DeclarationId, std::int64_t>>
            static_integer_bindings;
        // Shape facts only: a present empty value means the caller's packed
        // range was not proven and must not fall back to type-level bounds.
        std::unordered_map<std::uint32_t,
            std::optional<HirPackedRange>> vhdl_formal_ranges;
        std::string debug_name;
        // Signal-class procedure formals denote their actual signal for the
        // whole call. Frames are specialized per actual signal set.
        std::unordered_map<std::uint32_t, HirRuntimeBinding> signal_formals;
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
    std::unordered_map<std::string, std::size_t> hir_callable_indices_;
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
    mutable std::unordered_set<std::uint32_t>
        hir_vhdl_generate_iterator_profile_frames_;
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
    void record_implicit_signal_dependency(SignalId signal,
        std::uint32_t offset = 0U, std::uint32_t width = 0U);
    void record_container_object_dependency(ContainerObjectId object);
    [[nodiscard]] std::optional<runtime::simir::Sensitivity>
    hir_static_signal_sensitivity(semantic::ExpressionId expression,
        semantic::ScopeId process_scope);
    std::vector<runtime::simir::Sensitivity> implicit_signal_dependencies_;
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
        // The owner value that selects this assignment's driver.
        std::uint32_t owner { };
    };
    // The shared owner signal of a procedural continuous assignment target:
    // zero, or the owner value of the assignment that currently drives it
    // (IEEE 1800-2017 10.6.1). Every process finds it by the target's name.
    [[nodiscard]] std::optional<SignalId> hir_procedural_assign_owner(
        semantic::ExpressionId target);
    std::vector<ProceduralContinuousAssignment>
        procedural_continuous_assignments_;
    struct PendingMonitorDriver {
        semantic::ExpressionId expression;
        SignalId signal { };
        semantic::ScopeId scope;
    };
    std::vector<PendingMonitorDriver> pending_monitor_drivers_;
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
    mutable bool hierarchical_reference_used_ { };
    mutable bool hierarchical_reference_missed_ { };
    bool hierarchical_reference_retry_ { };
    // `g[1].h[2].x` as a hierarchical name when every index is constant.
    [[nodiscard]] std::optional<std::string> hir_systemverilog_constant_path(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<SignalId> hir_hierarchical_signal(
        std::string_view name) const;
};

} // namespace fsim::elaboration
