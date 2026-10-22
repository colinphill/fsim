// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "elaborator_internal.hpp"
#include "hierarchy_sv_constant_evaluator.hpp"
#include "scoped_bindings.hpp"
#include "systemverilog_template_bindings.hpp"
#include "fsim/elaboration/coverage_hir_points.hpp"
#include "fsim/frontend/parser.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace fsim::semantic::sv {
class ClassSpecializationIndex;
}

namespace fsim::elaboration {

// Restores a container to its size at construction.
template <typename Container>
class SizeRestoreGuard final {
public:
    explicit SizeRestoreGuard(Container& container)
        : container_ { container }
        , size_ { container.size() }
    {
    }

    SizeRestoreGuard(const SizeRestoreGuard&) = delete;
    SizeRestoreGuard& operator=(const SizeRestoreGuard&) = delete;

    ~SizeRestoreGuard() { container_.resize(size_); }

private:
    Container& container_;
    std::size_t size_;
};

template <typename Container>
SizeRestoreGuard(Container&) -> SizeRestoreGuard<Container>;

// A `timescale or timeunit spelling such as "10ns" in femtoseconds.
[[nodiscard]] std::optional<std::uint64_t>
systemverilog_unit_time_scale_femtoseconds(std::string_view spelling) noexcept;

using frontend::ProcessKind;
using runtime::Logic4;
using runtime::PackedLogic4;
using namespace runtime::simir;
using namespace elaboration_detail;

// The runtime report location of a source span: its file and first line
// and column.
// A SystemVerilog type after following typedefs and parameter bindings to
// a fixed point (at most eight steps).
[[nodiscard]] inline semantic::sv::TypeReference
fully_effective_systemverilog_type(
    const semantic::CompiledDesignResolver& resolver,
    semantic::sv::TypeReference type, const semantic::ScopeId scope)
{
    for (std::size_t depth { }; depth < 8U; ++depth) {
        auto effective = resolver.effective_systemverilog_type(type, scope);
        if (!effective || *effective == type) {
            break;
        }
        type = std::move(*effective);
    }
    return type;
}

[[nodiscard]] inline SourceLocation runtime_source_location(
    const frontend::SourceSpan& span)
{
    return SourceLocation {
        span.source_name.str(),
        static_cast<std::uint32_t>(span.begin.line),
        static_cast<std::uint32_t>(span.begin.column),
    };
}

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
[[nodiscard]] bool hir_vhdl_unshadowed_integer_vector(
    const semantic::SpecializedHirUnit&,
    const semantic::vhdl::SubtypeIndication&,
    semantic::ScopeId use_scope,
    std::span<const semantic::CompiledBindingFrame> binding_frames = {});
[[nodiscard]] semantic::vhdl::BuiltinTypeIdentity
hir_vhdl_standard_array_builtin_type(
    const semantic::SpecializedHirUnit&,
    const semantic::vhdl::SubtypeIndication&,
    semantic::ScopeId use_scope,
    std::span<const semantic::CompiledBindingFrame> binding_frames = {});

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

/// An internal `@sv-container-method:<method>:<container>` call: `size()`
/// (a 32-bit int) or `pop_front()` (a 64-bit two-state element handle) of
/// a container held by an object (IEEE 1800-2017 7.5.1, 7.10.2).
struct SvContainerMethodCall {
    bool pop_front { };
    std::string_view container;
    // `sum`, `product`, `and`, `or` or `xor` for an array reduction (7.12.3),
    // whose result has the element type.
    std::string_view reduction;
    // `pop_back()` rather than `pop_front()` (7.10.2.7).
    bool pop_back { };

    [[nodiscard]] std::size_t result_width() const noexcept
    {
        return pop_front ? 64U : 32U;
    }
    [[nodiscard]] frontend::ValueDomain result_domain() const noexcept
    {
        return pop_front ? frontend::ValueDomain::Bit2
                         : frontend::ValueDomain::Integer;
    }
};

[[nodiscard]] inline std::optional<SvContainerMethodCall>
sv_container_method_call(const std::string_view text) noexcept
{
    constexpr auto prefix = std::string_view { "@sv-container-method:" };
    constexpr auto pop_front = std::string_view { "pop_front:" };
    constexpr auto pop_back = std::string_view { "pop_back:" };
    constexpr auto size = std::string_view { "size:" };
    if (!text.starts_with(prefix)) {
        return std::nullopt;
    }
    const auto operation = text.substr(prefix.size());
    if (operation.starts_with(pop_front)) {
        return SvContainerMethodCall { true,
            operation.substr(pop_front.size()), { } };
    }
    if (operation.starts_with(pop_back)) {
        return SvContainerMethodCall { true,
            operation.substr(pop_back.size()), { }, true };
    }
    if (operation.starts_with(size)) {
        return SvContainerMethodCall { false, operation.substr(size.size()),
            { } };
    }
    for (const auto reduction : { std::string_view { "sum" },
             std::string_view { "product" }, std::string_view { "and" },
             std::string_view { "or" }, std::string_view { "xor" } }) {
        if (operation.size() > reduction.size()
            && operation.starts_with(reduction)
            && operation[reduction.size()] == ':') {
            return SvContainerMethodCall { false,
                operation.substr(reduction.size() + 1U), reduction };
        }
    }
    return std::nullopt;
}

// The width of an unsized decimal literal whose value needs more than 64
// bits: its significant bits plus a sign bit, as an unsized decimal number
// is signed and at least 32 bits (IEEE 1800-2017 5.7.1). Empty for other
// text.
[[nodiscard]] std::optional<std::size_t> wide_unsized_decimal_width(
    std::string_view text);

// HIR kind names for lowering diagnostics.
[[nodiscard]] std::string_view hir_statement_kind_name(
    semantic::sv::StatementKind kind) noexcept;
[[nodiscard]] std::string_view hir_statement_kind_name(
    semantic::vhdl::StatementKind kind) noexcept;
[[nodiscard]] std::string_view hir_expression_kind_name(
    semantic::sv::ExpressionKind kind) noexcept;
[[nodiscard]] std::string_view hir_expression_kind_name(
    semantic::vhdl::ExpressionKind kind) noexcept;

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
    void set_ancestor_systemverilog_specializations(
        const std::vector<const semantic::SpecializedHirUnit*>* contexts)
        noexcept;
    void set_hir_container_declaration_bindings(
        const ContainerDeclarationBindings* bindings) noexcept;
    void set_hir_code_coverage_context(
        const CoverageHirContext* coverage, bool module) noexcept;
    void set_hir_code_coverage_active(bool active) noexcept;
    using HirInstanceUnits = std::unordered_map<std::string,
        std::shared_ptr<const semantic::SpecializedHirUnit>>;
    void set_systemverilog_interface_handles(
        const std::unordered_map<std::string, std::uint64_t>* handles,
        const std::unordered_map<std::string, std::string>* types = nullptr,
        const HirInstanceUnits* instance_units = nullptr)
        noexcept;
    // Design-wide disable target labels (named blocks and tasks that a
    // disable statement may name from another process) and the labels a
    // disable may legally name.
    void set_disable_labels(
        const std::unordered_set<std::string>* targets,
        const std::unordered_set<std::string>* declared) noexcept;
    // The interface type bound to a generic `interface` port of this
    // instance, or empty.
    [[nodiscard]] std::string hir_generic_interface_type(
        std::string_view receiver) const;
    void diagnose_hir_systemverilog_file_process(
        semantic::ProcessId process, std::string_view hierarchy = { });
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
    // The construct or lowering step that last rejected the process being
    // lowered, for the caller's FSIM-ELAB-HIR-001 report.
    [[nodiscard]] const std::optional<std::string>&
    hir_lowering_rejection() const noexcept
    {
        return hir_lowering_rejection_;
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
            vhdl_context = std::nullopt,
        std::optional<std::pair<std::uint32_t, std::uint32_t>>
            actual_slice = std::nullopt);
    /// Lower one exact package variable declaration initializer as a
    /// synchronous, one-shot SystemVerilog initial process. The declaration
    /// supplies both the retained initializer expression and its lexical
    /// scope; destination must be the runtime binding for that declaration.
    [[nodiscard]] std::optional<Process>
    lower_hir_package_initializer(
        semantic::DeclarationId declaration,
        SignalId destination,
        frontend::Language language,
        std::string_view hierarchy);
    // The nets and constant bit ranges that an inout port actual selects
    // (`bus[0]`, `bus[3:2]`, `{a, b[1]}`), each with its offset in the
    // formal, for bidirectional switch connections.
    struct HirInoutActual {
        SignalId signal { };
        std::size_t offset { };
        std::size_t width { };
        std::size_t formal_offset { };
    };
    [[nodiscard]] std::optional<std::vector<HirInoutActual>>
    hir_inout_actual(semantic::ExpressionId expression) const;
    // The self-determined width of a port actual.
    [[nodiscard]] std::optional<std::size_t> hir_actual_width(
        const semantic::ExpressionId expression) const
    {
        return hir_expression_width(expression, semantic::ScopeId { });
    }
    [[nodiscard]] std::optional<Process> lower_hir_output_actual(
        SignalId source,
        semantic::ExpressionId expression,
        frontend::Language language,
        std::string_view hierarchy,
        std::size_t order,
        std::optional<std::pair<std::uint32_t, std::uint32_t>>
            actual_slice = std::nullopt);

    // A VHDL resolver program (IEEE 1076-2008 14.7.3.2): the runtime writes
    // the driving values of `signal`'s `driver_count` drivers, leftmost
    // first, to `input` and reads the resolved value from `output` after one
    // pass of the program.
    struct HirVhdlResolverProgram {
        Process process;
        RegisterId input { };
        RegisterId output { };
    };
    [[nodiscard]] std::optional<HirVhdlResolverProgram>
    lower_hir_vhdl_resolver_process(SignalId signal,
        semantic::DeclarationId function, std::size_t driver_count,
        std::string_view hierarchy);
    // A VHDL conversion of a port association (IEEE 1076-2008 6.5.7.1,
    // 14.7.3): the program computes `destination`'s effective value from
    // the signals it reads, in the same simulation cycle they change.
    struct HirVhdlConversionProgram {
        HirVhdlResolverProgram program;
        std::vector<SignalId> sources;
    };
    [[nodiscard]] std::optional<HirVhdlConversionProgram>
    lower_hir_vhdl_conversion_program(semantic::ExpressionId expression,
        SignalId destination, std::string_view hierarchy,
        std::optional<semantic::vhdl::SubtypeIndication> vhdl_context);
    // The concurrent assignment the parser derives from an output port's
    // formal conversion `f(formal) => actual`, as a conversion program of
    // the actual signal.
    struct HirVhdlFormalConversion {
        SignalId destination { };
        HirVhdlConversionProgram conversion;
    };
    [[nodiscard]] std::optional<HirVhdlFormalConversion>
    lower_hir_vhdl_formal_conversion(semantic::StatementId statement,
        std::string_view hierarchy);

    [[nodiscard]] std::vector<Process> take_generated_processes();

    // The subtype of an object whose declared subtype leaves index ranges
    // open, completed from the shape of its initial value (IEEE 1076-2008
    // 5.3.2.2, 6.4.2.2); nullopt when that shape is not known.
    [[nodiscard]] std::optional<semantic::vhdl::SubtypeIndication>
    hir_vhdl_initialized_object_subtype(semantic::ExpressionId initializer,
        const semantic::vhdl::SubtypeIndication& declared,
        semantic::ScopeId scope) const;

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
    // Materializes a SystemVerilog package unpacked-array variable as a
    // design-global container object on its first use.
    using PackageContainerObject = std::function<
        std::optional<ContainerObjectId>(semantic::DeclarationId)>;
    void set_package_container_object(PackageContainerObject materialize)
    {
        package_container_object_ = std::move(materialize);
    }
    using PackageStringObject = std::function<
        std::optional<StringObjectId>(semantic::DeclarationId)>;
    void set_package_string_object(PackageStringObject materialize)
    {
        package_string_object_ = std::move(materialize);
    }

private:
    enum class HirSystemVerilogScanValueKind : std::uint8_t {
        unsupported,
        packed_integral,
        string,
        real,
    };
    [[nodiscard]] HirSystemVerilogScanValueKind
    hir_systemverilog_scan_value_kind(
        semantic::ExpressionId expression) const;

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
        bool input_actual_assignment_context { };
        // The input actual is a conversion program: its value goes to the
        // resolver output register instead of an update of the destination.
        bool input_actual_program { };
        std::optional<semantic::sv::TypeReference>
            input_actual_assignment_type;
        std::optional<semantic::vhdl::SubtypeIndication>
            input_actual_vhdl_context;
        std::optional<SignalId> output_actual_source;
        std::optional<semantic::ExpressionId> output_actual;
        // The bit offset and width within the actual that one instance of
        // an instance array connects to (IEEE 1800-2017 23.3.3.5).
        std::optional<std::pair<std::uint32_t, std::uint32_t>> actual_slice;
        std::optional<SignalId> procedural_assignment_active;
        std::uint32_t procedural_assignment_owner { };
        // The force owner of a procedural assign's target: a force
        // overrides the assignment while it is active (IEEE 1800-2017
        // 10.6.2).
        std::optional<SignalId> procedural_assignment_force_owner;
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
        // An event control whose terms have no static signals (`always
        // @(vif.d)`): the body begins by waiting on them (IEEE 1800-2017
        // 9.2.2.1, 9.4.2).
        std::vector<semantic::sv::Sensitivity> leading_wait;
        bool always_comb_or_latch { };
        // A source always_comb or always_latch first runs after every
        // initial and always procedure has started (IEEE 1800-2017
        // 9.2.2.2.2).
        bool start_after_procedures { };
        bool wait_before_first_execution { };
        bool concurrent_assertion { };
        bool vhdl_guarded_assignment { };
        struct VhdlResolver {
            SignalId signal { };
            semantic::DeclarationId function;
            std::size_t driver_count { };
        };
        std::optional<VhdlResolver> vhdl_resolver;
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
    // The time unit of the unit being lowered, for %t of integral values;
    // the maximum value when it counts in the project resolution.
    [[nodiscard]] std::uint64_t hir_time_unit_femtoseconds() const;
    // A range bound (a VHDL range constraint or a SystemVerilog packed
    // range), folding a bound that is still an expression.
    // Whether an expression names something a callable frame binds (a
    // class parameter of the running specialization), whose value the
    // unit's evaluator and the frontend's folded bounds do not see.
    [[nodiscard]] bool hir_frame_bound_expression(
        semantic::ExpressionId expression) const;
    // A bound folded before specialization yields to its expression when
    // that names a frame-bound parameter.
    [[nodiscard]] std::optional<std::int64_t> hir_range_bound(
        const std::optional<std::int64_t>& folded,
        const std::optional<semantic::ExpressionId>& expression) const
    {
        if (expression && !hir_generic_binding_frames_.empty()
            && hir_frame_bound_expression(*expression)) {
            if (const auto value = hir_constant_integer(*expression)) {
                return value;
            }
        }
        return folded ? folded
            : expression ? hir_constant_integer(*expression)
            : std::nullopt;
    }
    template <typename Range>
    [[nodiscard]] std::optional<std::int64_t> hir_range_left(
        const Range& range) const
    {
        return hir_range_bound(range.left, range.left_expression);
    }
    template <typename Range>
    [[nodiscard]] std::optional<std::int64_t> hir_range_right(
        const Range& range) const
    {
        return hir_range_bound(range.right, range.right_expression);
    }
    // Append a callable-cache key fragment for an actual's VHDL element
    // constraints: each constraint's path, folded bounds and direction.
    void append_vhdl_element_constraint_key(std::string& key,
        const std::vector<semantic::vhdl::ElementConstraint>& constraints)
        const;
    [[nodiscard]] std::optional<semantic::ExpressionId>
    hir_constant_initializer(
        semantic::DeclarationId declaration) const;
    struct HirPackedRange {
        std::int64_t left { };
        std::int64_t right { };
        bool descending { };
    };
    struct HirVhdlFunctionResultShape {
        std::size_t width { };
        frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
        HirPackedRange range;
        std::size_t element_width { };
        std::vector<semantic::vhdl::ElementConstraint>
            element_constraints { };
        bool null_range { };
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
        std::optional<semantic::ExpressionId> root_expression;
        bool access_object_root { };
    };
    struct HirVhdlConstantArrayShape {
        std::size_t root_width { };
        std::size_t element_width { };
        semantic::vhdl::RangeConstraint element_range;
        std::vector<semantic::SpecializedHirVhdlPackedValue> elements;
    };
    [[nodiscard]] std::optional<HirVhdlConstantArrayShape>
    hir_vhdl_constant_array_shape(
        semantic::DeclarationId declaration) const;
    // The bits of a positional array aggregate of `context`'s type whose
    // element count its associations fix (IEEE 1076-2008 9.3.3.3).
    [[nodiscard]] std::optional<std::size_t> hir_vhdl_positional_aggregate_bits(
        semantic::ExpressionId aggregate,
        const semantic::vhdl::SubtypeIndication& context,
        semantic::ScopeId scope) const;
    // Whether `expression` is a value with a known null index range: a
    // null array or "" (IEEE 1076-2008 5.3.2.2).
    [[nodiscard]] bool hir_vhdl_null_value(
        semantic::ExpressionId expression) const;
    // The unconstrained array subtype the predefined "&" of two array
    // operands of one type yields (IEEE 1076-2008 9.2.5), or nullopt.
    [[nodiscard]] std::optional<semantic::vhdl::SubtypeIndication>
    hir_vhdl_concatenation_subtype(semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<HirPackedRange>
    hir_vhdl_unconstrained_constant_range(
        const semantic::vhdl::Declaration& declaration, std::size_t width,
        semantic::ScopeId process_scope,
        std::size_t* proven_width = nullptr) const;
    [[nodiscard]] std::optional<HirPackedRange> hir_expression_range(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] std::optional<HirPackedRange> hir_expression_direct_range(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
    // The smallest and largest choices of a named array aggregate without
    // `others` (IEEE 1076-2008 9.3.3.3), as an ascending range; `index`
    // evaluates one index choice (integer constants by default).
    // An aggregate whose only choice is one non-static range (IEEE
    // 1076-2008 9.3.3.3): `(l to r => e)` or `(a'range => e)`.
    // A VHDL array object whose index ranges are known only when it is
    // elaborated at run time (IEEE 1076-2008 5.3.2.2, 14.4.2.5).
    struct HirVhdlShapedProfile {
        // The array subtype the objects are declared with.
        semantic::vhdl::SubtypeIndication subtype;
        std::vector<semantic::vhdl::RangeConstraint> constraints;
        semantic::vhdl::SubtypeIndication element_subtype;
        std::size_t element_width { };
        frontend::ValueDomain element_domain { frontend::ValueDomain::Unknown };
        bool element_signed { };
        PackedLogic4 element_default;
        // The element's own statically shaped index ranges, when it is an
        // array, and its leaf elements.
        std::vector<VhdlShapeRange> inner;
        std::size_t leaf_width { };
        frontend::ValueDomain leaf_domain { frontend::ValueDomain::Unknown };
        bool leaf_signed { };
        ContainerType type;
    };

    struct HirVhdlShapedLocal {
        ContainerRegisterId container { };
        HirVhdlShapedProfile profile;
        // An object designated by an access value (`p.all`, or `p` as a
        // prefix, IEEE 1076-2008 8.1): the handle expression and its access
        // type. Its container is valid only once bound.
        std::optional<semantic::ExpressionId> heap_handle;
        const semantic::vhdl::TypeDefinition* access_type { };
    };
    struct HirVhdlAccessHeap;
    // A shaped object ready for operations: a heap object is read into a
    // fresh container and written back after an update.
    struct HirVhdlShapedBound {
        HirVhdlShapedLocal local;
        std::optional<RegisterId> handle;
        ContainerRegisterId heap_objects { };
    };
    // An element (`v(i)`) or a leaf of an array element (`v(i)(j)`) of a
    // shaped object.
    struct HirVhdlShapedAccess {
        const HirVhdlShapedLocal* local { };
        std::vector<semantic::ExpressionId> selectors;
        bool leaf { };
    };
    struct HirVhdlShapedAttributeUse {
        const HirVhdlShapedLocal* local { };
        VhdlShapedAttribute attribute { VhdlShapedAttribute::length };
        std::uint32_t dimension { };
    };
    // `element` constrained by the element constraints along `owner`'s
    // subtype chain (`t(open)(7 downto 0)`, IEEE 1076-2008 5.3.2.2), the
    // outermost taking precedence.
    [[nodiscard]] semantic::vhdl::SubtypeIndication
    hir_vhdl_chain_element_subtype(
        const semantic::vhdl::SubtypeIndication& owner,
        semantic::vhdl::SubtypeIndication element) const;
    // The length of a VHDL slice whose bounds differ by a constant
    // (`v(i*8+7 downto i*8)`), or nullopt.
    [[nodiscard]] std::optional<std::size_t> hir_vhdl_symbolic_slice_length(
        semantic::ExpressionId slice) const;
    // The element width of a one-dimensional array subtype, or nullopt.
    [[nodiscard]] std::optional<std::size_t> hir_vhdl_vector_element_width(
        const semantic::vhdl::SubtypeIndication& subtype) const;
    // `open_array`: the profile of an unconstrained array type, whose
    // bounds every object supplies.
    [[nodiscard]] std::optional<HirVhdlShapedProfile> hir_vhdl_shaped_profile(
        const semantic::vhdl::SubtypeIndication& subtype,
        bool open_array = false) const;
    // The profile of the objects of an access type that designates an
    // unconstrained array (IEEE 1076-2008 5.4.1), or nullopt.
    [[nodiscard]] std::optional<HirVhdlShapedProfile>
    hir_vhdl_shaped_heap_profile(
        const semantic::vhdl::TypeDefinition& access_type) const;
    // A local constant computed when its declaration is elaborated: one with
    // run-time bounds, or an integer whose initializer does not fold.
    [[nodiscard]] bool hir_vhdl_elaborated_constant(
        const semantic::vhdl::Declaration& declaration) const;
    // The profile of an array value with run-time bounds: a shaped object,
    // a qualified one, or a concatenation with one (9.2.5).
    [[nodiscard]] std::optional<HirVhdlShapedProfile>
    hir_vhdl_shaped_value_profile(semantic::ExpressionId expression) const;
    // Such a value in a container register of its own shape.
    [[nodiscard]] std::optional<
        std::pair<ContainerRegisterId, HirVhdlShapedProfile>>
    lower_hir_vhdl_shaped_container(
        semantic::ExpressionId expression, const frontend::SourceSpan& span);
    // The index range of a packed array value of `profile`'s type whose own
    // bounds the context does not give (9.3.3.3, 9.3.2).
    [[nodiscard]] HirPackedRange hir_vhdl_shaped_packed_range(
        const HirVhdlShapedProfile& profile, semantic::ExpressionId value,
        std::size_t width) const;
    // `value` in the element representation `domain` of a shaped payload.
    [[nodiscard]] RegisterId conform_hir_vhdl_shaped_domain(
        RegisterId value, frontend::ValueDomain domain);
    // The run-time-shaped profile of the unconstrained array type of
    // `subtype`, or nullopt.
    [[nodiscard]] std::optional<HirVhdlShapedProfile>
    hir_vhdl_shaped_open_profile(
        const semantic::vhdl::SubtypeIndication& subtype) const;
    // Whether this unit has any shaped object or shaped access type.
    [[nodiscard]] bool hir_vhdl_shaped_active() const;
    // The heap object denoted by `p.all`, or by `p` when `prefix`.
    [[nodiscard]] const HirVhdlShapedLocal* hir_vhdl_shaped_heap_view(
        semantic::ExpressionId expression, bool prefix) const;
    // A shaped name used as the prefix of an index, slice or attribute.
    [[nodiscard]] const HirVhdlShapedLocal* hir_vhdl_shaped_prefix(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<HirVhdlShapedBound> bind_hir_vhdl_shaped(
        const HirVhdlShapedLocal& local, const frontend::SourceSpan& span);
    void store_hir_vhdl_shaped(const HirVhdlShapedBound& bound);
    // `new T(r)` or `new T'(v)` for a shaped heap (IEEE 1076-2008 9.3.7).
    [[nodiscard]] std::optional<RegisterId> lower_hir_vhdl_shaped_allocator(
        const HirVhdlAccessHeap& heap,
        std::span<const semantic::vhdl::RangeConstraint> constraints,
        std::optional<semantic::ExpressionId> initializer,
        const frontend::SourceSpan& span);
    [[nodiscard]] bool lower_hir_vhdl_shaped_declaration(
        semantic::DeclarationId declaration);
    [[nodiscard]] const HirVhdlShapedLocal* hir_vhdl_shaped_name(
        semantic::ExpressionId expression) const;
    [[nodiscard]] const HirVhdlShapedLocal* hir_vhdl_shaped_element(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::pair<const HirVhdlShapedLocal*,
        std::span<const semantic::ExpressionId>>
    hir_vhdl_shaped_element_selectors(semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<HirVhdlShapedAttributeUse>
    hir_vhdl_shaped_attribute(semantic::ExpressionId expression) const;
    struct HirVhdlShapedSlice {
        const HirVhdlShapedLocal* local { };
        semantic::ExpressionId first;
        semantic::ExpressionId last;
        bool descending { };
    };
    [[nodiscard]] std::optional<HirVhdlShapedSlice> hir_vhdl_shaped_slice(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<VhdlShaped> lower_hir_vhdl_shaped_slice(
        const HirVhdlShapedSlice& slice, VhdlShapedOperation operation);
    [[nodiscard]] std::optional<HirVhdlShapedAccess> hir_vhdl_shaped_access(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<VhdlShaped> lower_hir_vhdl_shaped_access(
        const HirVhdlShapedAccess& access, VhdlShapedOperation operation);
    [[nodiscard]] std::optional<std::size_t> hir_vhdl_shaped_width(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<frontend::ValueDomain> hir_vhdl_shaped_domain(
        semantic::ExpressionId expression) const;
    // Lowers an element, attribute or whole-object read of a shaped object;
    // nullopt when `expression` is none of these.
    [[nodiscard]] std::optional<std::optional<RegisterId>>
    lower_hir_vhdl_shaped_expression(
        semantic::ExpressionId expression, std::size_t expected_width);
    // A value assigned to (part of) a shaped object: `width` bits when
    // known, an aggregate in the object's type.
    [[nodiscard]] std::optional<RegisterId> lower_hir_vhdl_shaped_value(
        semantic::ExpressionId value, const HirVhdlShapedProfile& profile,
        std::optional<std::size_t> width);
    [[nodiscard]] bool lower_hir_vhdl_shaped_whole_assignment(
        ContainerRegisterId target, const HirVhdlShapedProfile& profile,
        semantic::ExpressionId value);
    // Lowers a variable assignment to a shaped object or one of its
    // elements; nullopt when the target is neither.
    [[nodiscard]] std::optional<bool> lower_hir_vhdl_shaped_assignment(
        semantic::ExpressionId target, semantic::ExpressionId value);
    // The prefix of `x(x'range)`, a slice whose discrete range is its own
    // prefix's index range and so denotes the whole object (IEEE 1076-2008
    // 8.5).
    [[nodiscard]] std::optional<semantic::ExpressionId>
    hir_vhdl_whole_range_slice(semantic::ExpressionId expression) const;
    [[nodiscard]] bool hir_vhdl_single_dynamic_range_aggregate(
        const semantic::vhdl::Expression& aggregate) const;
    [[nodiscard]] std::optional<HirPackedRange>
    hir_vhdl_named_aggregate_bounds(
        const semantic::vhdl::Expression& aggregate,
        const std::function<std::optional<std::int64_t>(
            semantic::ExpressionId)>& index = { }) const;
    [[nodiscard]] std::optional<HirVhdlFunctionResultShape>
    hir_vhdl_function_result_shape(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope,
        std::optional<semantic::DeclarationId> selected_body = std::nullopt) const;
    // The body patterns hir_vhdl_function_result_shape proves first.
    // An integer element of a constant elaborated into a read-only signal.
    [[nodiscard]] std::optional<std::int64_t>
    hir_vhdl_elaborated_constant_element(
        semantic::ExpressionId expression) const;
    // `c(l to r)(i)`: the element of a static slice of such a constant.
    [[nodiscard]] std::optional<std::int64_t>
    hir_vhdl_elaborated_slice_constant_element(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<HirVhdlFunctionResultShape>
    hir_vhdl_function_result_proof(semantic::ExpressionId expression,
        semantic::ScopeId process_scope,
        std::optional<semantic::DeclarationId> selected_body) const;
    // The one static shape every return value of the called function has.
    [[nodiscard]] std::optional<HirVhdlFunctionResultShape>
    hir_vhdl_returned_result_shape(semantic::ExpressionId expression,
        semantic::ScopeId process_scope,
        std::optional<semantic::DeclarationId> selected_body) const;
    [[nodiscard]] std::optional<bool> hir_vhdl_range_direction(
        const semantic::vhdl::RangeConstraint& range,
        semantic::ScopeId process_scope) const;
    // `range` with folded bounds, a resolved direction and its null flag.
    [[nodiscard]] std::optional<semantic::vhdl::RangeConstraint>
    hir_vhdl_concrete_range(const semantic::vhdl::RangeConstraint& range,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] std::optional<std::optional<HirPackedRange>>
    hir_vhdl_callable_formal_range(
        semantic::DeclarationId declaration,
        std::size_t dimension = 0U) const;
    [[nodiscard]] const std::vector<semantic::vhdl::ElementConstraint>*
    hir_vhdl_callable_formal_element_constraints(
        semantic::DeclarationId declaration) const;
    // The shape of a VHDL composite value (IEEE 1076-2008 5.3.2.2, 5.3.3):
    // the index range of each dimension, outermost first (empty for a
    // scalar or record; an unknown range is empty), and the static
    // constraints of the elements and record elements below.
    struct HirVhdlShape {
        std::vector<std::optional<HirPackedRange>> ranges;
        std::vector<semantic::vhdl::ElementConstraint> element_constraints;
        [[nodiscard]] bool complete() const noexcept;
        // A known null range in some dimension: the value has no elements
        // (IEEE 1076-2008 5.3.2.2).
        [[nodiscard]] bool null() const noexcept;
    };
    // The shape a subtype gives its objects.
    [[nodiscard]] std::optional<HirVhdlShape> hir_vhdl_subtype_shape(
        const semantic::vhdl::SubtypeIndication& subtype,
        semantic::ScopeId scope) const;
    // What an object supplies beyond its declared subtype: a formal's
    // actual, a signal's elaborated layout.
    [[nodiscard]] std::optional<HirVhdlShape> hir_vhdl_object_facts(
        semantic::DeclarationId declaration, std::size_t dimensions) const;
    // The shape of an object: its declared subtype completed by its facts.
    // The shape of `expression` where `context` is its expected type: a
    // literal or aggregate with an open range takes its bounds from itself
    // (IEEE 1076-2008 9.3.2, 9.3.3.3).
    // The subtype a fully constrained target gives a call of a `return
    // <identifier> of <type>` function as its value (IEEE 1076-2019 4.2.1
    // a, c, d).
    [[nodiscard]] std::optional<semantic::vhdl::SubtypeIndication>
    hir_vhdl_return_identifier_context(semantic::ExpressionId value,
        const semantic::vhdl::SubtypeIndication& target) const;
    // A local of a `return <identifier> of <type>` function declared with
    // the return identifier's subtype (IEEE 1076-2019 4.2.1).
    [[nodiscard]] bool hir_vhdl_contextual_result_local(
        semantic::DeclarationId declaration) const;
    [[nodiscard]] std::optional<HirVhdlShape> hir_vhdl_contextual_shape(
        semantic::ExpressionId expression,
        const semantic::vhdl::SubtypeIndication& context,
        semantic::ScopeId scope) const;
    [[nodiscard]] std::optional<HirVhdlShape> hir_vhdl_object_shape(
        semantic::DeclarationId declaration, semantic::ScopeId scope) const;
    // Two composite operands whose static shapes have a level of different
    // lengths (never equal, 9.2.3).
    [[nodiscard]] bool hir_vhdl_shapes_differ(semantic::ExpressionId left,
        semantic::ExpressionId right) const;
    // A record subtype with an element whose bounds are still open
    // (IEEE 1076-2008 5.3.3).
    [[nodiscard]] bool hir_vhdl_partially_constrained_record(
        const semantic::vhdl::SubtypeIndication& subtype) const;
    // Whether a function result takes its subtype from the call's context:
    // an unconstrained array or a partially constrained record (IEEE
    // 1076-2019 4.2.1).
    [[nodiscard]] bool hir_vhdl_context_sized(
        const semantic::vhdl::SubtypeIndication& result) const;
    // The subtype an aggregate target gives the value assigned to it (IEEE
    // 1076-2008 10.5.2.1, 10.6.2.1): `type` with each open record element
    // taken from the target element associated with it; nullopt while a
    // level stays open.
    [[nodiscard]] std::optional<semantic::vhdl::SubtypeIndication>
    hir_vhdl_aggregate_target_subtype(semantic::ExpressionId target,
        const semantic::vhdl::SubtypeIndication& type,
        semantic::ScopeId scope) const;
    // Fill the open ranges and element constraints of `subtype` from
    // `shape`.
    void apply_hir_vhdl_shape(semantic::vhdl::SubtypeIndication& subtype,
        const HirVhdlShape& shape, semantic::ScopeId scope) const;
    // The shape of the value an expression denotes: its subtype's, completed
    // from the object it names (the actual of a formal or port, an alias's
    // object) through its index, slice and member selections.
    [[nodiscard]] std::optional<HirVhdlShape> hir_vhdl_shape(
        semantic::ExpressionId expression) const;
    mutable std::size_t hir_vhdl_shape_depth_ { };
    // Expressions whose range is being taken from the shape service.
    mutable std::unordered_set<std::uint32_t> hir_vhdl_range_shapes_;
    // User attribute values whose open subtype is being completed.
    mutable std::unordered_set<std::uint32_t> hir_vhdl_attribute_values_;
    struct HirConstantSelection {
        std::size_t offset { };
        std::size_t width { };
    };
    // A static slice of a prefix value whose position is not static, as
    // bits of that value.
    [[nodiscard]] std::optional<HirConstantSelection> hir_vhdl_value_slice(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<HirConstantSelection>
    hir_constant_selection(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] std::optional<HirConstantSelection>
    hir_constant_element_selection(
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
    // The element subtype of a predefined array type named by its simple
    // name (bit_vector, string, boolean_vector, integer_vector,
    // time_vector, real_vector, std_logic_vector, std_ulogic_vector).
    [[nodiscard]] std::optional<semantic::vhdl::SubtypeIndication>
    hir_vhdl_predefined_array_element(std::string_view array_name) const;
    [[nodiscard]] std::optional<std::string> hir_vhdl_array_type_family(
        const semantic::vhdl::SubtypeIndication& subtype) const;
    [[nodiscard]] bool hir_vhdl_same_nominal_array_type(
        const semantic::vhdl::SubtypeIndication& left,
        const semantic::vhdl::SubtypeIndication& right,
        semantic::ScopeId scope) const;
    // The enumeration type a VHDL for-loop parameter iterates over, when the
    // value names one; the parameter's runtime value is the ordinal.
    [[nodiscard]] bool hir_vhdl_boolean_loop_parameter(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<semantic::TypeId>
    hir_vhdl_loop_parameter_enumeration(
        semantic::ExpressionId value) const;
    // The width of a VHDL slice whose bounds differ by a constant.
    [[nodiscard]] std::optional<std::size_t> hir_vhdl_static_slice_width(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] std::optional<std::size_t> hir_dynamic_part_width(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_systemverilog_indexed_member_offset(
        semantic::ExpressionId expression, std::size_t source_width);
    [[nodiscard]] std::optional<runtime::simir::DynamicIndex>
    lower_hir_dynamic_index(
        semantic::ExpressionId source,
        semantic::ExpressionId index,
        std::size_t source_width,
        std::uint32_t base_offset = 0U);
    /// The runtime base of an indexed part-select `v[b +: n]`/`v[b -: n]`
    /// as a bit index of `v`; a multidimensional packed `v` selects whole
    /// elements of its first dimension (IEEE 1800-2017 7.4.6, 11.5.1).
    [[nodiscard]] std::optional<runtime::simir::DynamicIndex>
    lower_hir_dynamic_part_index(
        semantic::ExpressionId slice,
        std::size_t source_width,
        std::uint32_t base_offset = 0U);
    [[nodiscard]] std::optional<runtime::simir::DynamicPartIndex>
    lower_hir_vhdl_dynamic_slice(
        semantic::ExpressionId expression,
        std::size_t source_width,
        std::size_t selected_width,
        std::uint32_t base_offset = 0U);
    /// A one-dimensional slice whose length is known only at run time: the
    /// bit offset of its right element and its element count, zero for a
    /// null slice (IEEE 1076-2008 8.5).
    struct HirVhdlRuntimeSlice {
        RegisterId base { };
        RegisterId length { };
        std::size_t element_width { };
    };
    [[nodiscard]] std::optional<HirVhdlRuntimeSlice>
    lower_hir_vhdl_runtime_slice(
        semantic::ExpressionId expression,
        std::size_t source_width);
    /// `target_slice := value_slice` of two run-time-length slices, one
    /// element at a time after a length check (10.6.2.1). `write` stores
    /// one element given its bit offset within the target object.
    [[nodiscard]] bool lower_hir_vhdl_runtime_slice_copy(
        const HirVhdlRuntimeSlice& target,
        const HirVhdlRuntimeSlice& value,
        RegisterId value_source,
        std::size_t value_source_width,
        const frontend::SourceSpan& span,
        const std::function<void(RegisterId offset, RegisterId element)>&
            write);
    /// Predefined `=` of two one-dimensional run-time-length slices
    /// (9.2.3); the result is one bit.
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_vhdl_runtime_slice_equality(
        semantic::ExpressionId left,
        semantic::ExpressionId right);
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
        // A class object whose property `receiver` holds the handle
        // (`obj.vif.m`).
        std::optional<semantic::DeclarationId> object;
        std::vector<std::pair<std::uint64_t, SignalId>> candidates;
        // A parameter of the interface (`vif.P`): each instance's value.
        std::vector<std::pair<std::uint64_t, PackedLogic4>> parameters;
        // A packed structure field of the member (`vif.data.x`): its
        // offset in each candidate signal; `width` is the field's.
        std::optional<std::uint32_t> select_offset;
        std::size_t width { };
        frontend::ValueDomain domain { frontend::ValueDomain::Logic4 };
    };
    // The virtual interface type of a declaration, through type aliases
    // (IEEE 1800-2017 25.9, 6.18).
    [[nodiscard]] std::optional<semantic::sv::TypeReference>
    hir_systemverilog_virtual_interface_type(
        const semantic::sv::Declaration& declaration) const;
    [[nodiscard]] std::optional<HirVirtualInterfaceMember>
    hir_virtual_interface_member(semantic::ExpressionId expression_id) const;
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_virtual_interface_member_handle(
        const HirVirtualInterfaceMember& member);
    [[nodiscard]] std::optional<HirVirtualInterfaceMember>
    hir_virtual_interface_member(
        std::string_view name, semantic::ScopeId scope) const;
    [[nodiscard]] std::optional<RegisterId> lower_hir_virtual_interface_read(
        const HirVirtualInterfaceMember& member);
    [[nodiscard]] std::optional<RegisterId> lower_hir_virtual_interface_handle(
        semantic::DeclarationId receiver);
    [[nodiscard]] std::optional<RegisterId> lower_hir_string_to_packed(
        semantic::ExpressionId operand, std::size_t width,
        const semantic::vhdl::SubtypeIndication* contextual_subtype = nullptr);
    // A VHDL function call whose result has a static shape assigned to a
    // target of another length fails (IEEE 1076-2008 10.6.2.1, 14.7.3.4):
    // emits the run-time failure and returns true.
    bool emit_hir_vhdl_call_length_check(semantic::ExpressionId value,
        std::size_t target_width, const frontend::SourceSpan& span);
    // The same check for each array-valued element of an initializer that
    // is not otherwise lowered (an unread constant): only the checks are
    // emitted, no value.
    void emit_hir_vhdl_initializer_length_checks(semantic::ExpressionId value,
        const semantic::vhdl::SubtypeIndication& subtype,
        const frontend::SourceSpan& span, std::size_t depth = 0U);
    void emit_hir_vhdl_string_length_check(
        StringRegisterId target, StringRegisterId value,
        const frontend::SourceSpan& span);
    [[nodiscard]] bool hir_process_handle_expression(
        semantic::ExpressionId expression) const;
    // `p.status()` (true) or `p.completed()` (false) of a process handle.
    [[nodiscard]] std::optional<bool> hir_process_status_query(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<std::int64_t> hir_vhdl_subtype_name_bound(
        semantic::ExpressionId expression, std::size_t depth) const;
    // The ascending (low, high) bounds of a discrete subtype name used as a
    // choice; low exceeds high for a null range.
    [[nodiscard]] std::optional<std::pair<std::int64_t, std::int64_t>>
    hir_vhdl_discrete_subtype_choice(semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<semantic::ExpressionId>
    hir_systemverilog_parameter_member(semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<std::pair<semantic::DeclarationId,
        std::vector<std::pair<std::uint64_t, std::string>>>>
    hir_virtual_interface_instances(std::string_view receiver,
        semantic::ScopeId scope = { }) const;
    // Tests a virtual interface `handle` against each instance of its
    // interface type (IEEE 1800-2017 25.9) and emits the matching
    // instance's call through `emit(path)`.
    [[nodiscard]] bool lower_hir_virtual_interface_dispatch(RegisterId handle,
        const std::vector<std::pair<std::uint64_t, std::string>>& instances,
        const std::function<bool(const std::string&)>& emit);
    // The specialization whose property initializers an implicit constructor
    // is lowering, and the callable that was active when they began.
    struct HirInitializerContext {
        std::optional<std::size_t> active_callable;
        std::uint32_t specialization { };
    };
    std::optional<HirInitializerContext> hir_initializer_context_;
    // The instance an interface function call runs on, while a call through
    // a virtual interface handle is lowered once per instance.
    std::optional<std::string> hir_interface_receiver_override_;
    [[nodiscard]] std::optional<semantic::ExpressionId>
    hir_parameter_type_expression(semantic::DeclarationId declaration) const;
    [[nodiscard]] std::optional<HirSystemVerilogConstant>
    hir_parameter_actual_constant(semantic::DeclarationId declaration) const;
    // An unpacked array concatenation built as a queue or dynamic array
    // value (IEEE 1800-2017 10.10).
    [[nodiscard]] std::optional<ContainerRegisterId>
    lower_hir_container_concatenation(const ContainerType& type,
        std::span<const semantic::ExpressionId> operands);
    [[nodiscard]] bool lower_hir_virtual_interface_select_write(
        semantic::ExpressionId target, semantic::ExpressionId value,
        bool nonblocking, SignalUpdateDomain update_domain);
    // Marks a clockvar's synchronous drive request pending (IEEE
    // 1800-2017 14.16); a signal without a pending flag is not a clockvar.
    void emit_clocking_drive_pending(SignalId request);
    // Writes `value` to the member in the instance the handle names.
    [[nodiscard]] bool lower_hir_virtual_interface_write(
        const HirVirtualInterfaceMember& member, RegisterId value,
        bool nonblocking, runtime::simir::SignalUpdateDomain update_domain);
    [[nodiscard]] std::optional<SignalId> hir_direct_signal(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<SignalId>
    hir_systemverilog_event_signal(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
    struct HirSystemVerilogEventArraySelection {
        semantic::ExpressionId index_expression;
        std::vector<std::pair<std::int32_t, SignalId>> elements;
    };
    [[nodiscard]] std::optional<HirSystemVerilogEventArraySelection>
    hir_systemverilog_event_array_selection(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
    // Selects one element of an event array, `@(e[i])` or `-> e[i]`, and
    // applies `emit` to it: directly for a constant index, otherwise through
    // a compare-and-branch chain over the declared elements. When no element
    // matches, a wait blocks forever and a trigger does nothing.
    [[nodiscard]] bool lower_hir_systemverilog_event_array_dispatch(
        const HirSystemVerilogEventArraySelection& event_array,
        const frontend::SourceSpan& span,
        const std::function<bool(SignalId)>& emit,
        bool wait_when_unmatched);
    [[nodiscard]] std::optional<PackedLogic4>
    hir_systemverilog_event_array_index_value(
        std::int32_t index,
        semantic::ExpressionId index_expression,
        std::size_t width) const;
    [[nodiscard]] std::optional<std::uint64_t>
    hir_systemverilog_interface_handle(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<std::vector<semantic::sv::Sensitivity>>
    hir_default_clocking_event() const;
    [[nodiscard]] std::optional<std::size_t> hir_loop_iteration_count(
        semantic::StatementId statement) const;
    [[nodiscard]] std::optional<std::size_t> hir_expression_width(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope,
        const semantic::vhdl::SubtypeIndication* vhdl_array_context = nullptr) const;
    // The target expression of a name that denotes an object alias of an
    // element or slice (`alias a : t is s(1 to 4)`).
    [[nodiscard]] std::optional<semantic::ExpressionId> hir_vhdl_alias_target_expression(
        semantic::ExpressionId expression) const;
    // Whether an indexed name's prefix is a STRING lowered as a runtime
    // string (a variable, a constant, or an alias of a slice).
    // `s'length`, `s'left`, `s'right`, `s'low` or `s'high` of a STRING
    // held as a runtime string: a signed 32-bit integer.
    [[nodiscard]] bool hir_vhdl_runtime_string_bound(
        semantic::ExpressionId expression) const;
    // Whether a VHDL STRING subtype resolves through aliases/subtypes to the
    // predefined STD.STANDARD.STRING declaration (or an unshadowed builtin).
    // The 1 to N range a predefined STRING gives a string literal.
    [[nodiscard]] std::optional<HirPackedRange> hir_vhdl_string_literal_range(
        std::string_view value);
    // The range of a subtype with exactly one array index constraint.
    [[nodiscard]] std::optional<HirPackedRange> hir_vhdl_single_index_range(
        const semantic::vhdl::SubtypeIndication& subtype,
        semantic::ScopeId use_scope);
    // A constant with an unconstrained predefined STRING subtype takes the
    // exact bounds of a direct string-literal initializer; shape is not
    // inferred through concatenations or function results.
    [[nodiscard]] std::optional<HirPackedRange>
    hir_vhdl_constant_string_literal_range(semantic::ExpressionId actual);
    [[nodiscard]] bool hir_vhdl_predefined_string_type(
        const semantic::vhdl::SubtypeIndication& subtype) const;
    [[nodiscard]] std::optional<semantic::vhdl::SubtypeIndication>
    hir_vhdl_qualified_integer_vector_subtype(
        semantic::ExpressionId expression) const;
    [[nodiscard]] bool hir_vhdl_runtime_string_prefix(
        semantic::ExpressionId prefix) const;
    // Whether a VHDL subprogram formal is of a STRING subtype.
    [[nodiscard]] bool vhdl_string_formal(semantic::DeclarationId formal) const;
    // The object a whole-object alias declaration denotes.
    [[nodiscard]] std::optional<semantic::DeclarationId> hir_vhdl_alias_object(
        semantic::DeclarationId alias) const;
    // The end of a VHDL subtype's chain of named subtypes and aliases: the
    // last base spelling followed, the last predefined array identity a
    // base names, the first type that is not a subtype or alias (or the
    // unresolved base it stopped at), and the scope of the last
    // declaration reached, in which each base was resolved.
    struct HirVhdlSubtypeChain {
        std::string_view spelling;
        semantic::vhdl::BuiltinTypeIdentity builtin {
            semantic::vhdl::BuiltinTypeIdentity::none
        };
        bool followed { };
        semantic::TypeId type;
        const semantic::vhdl::TypeDefinition* definition { };
        semantic::ScopeId scope;
    };
    [[nodiscard]] HirVhdlSubtypeChain hir_vhdl_subtype_chain(
        const semantic::vhdl::SubtypeIndication& subtype,
        semantic::ScopeId scope) const;
    // Whether an unresolved VITAL procedure name denotes the compiler's
    // intrinsic rather than a user declaration.
    [[nodiscard]] bool hir_vhdl_vital_intrinsic_visible(
        const semantic::vhdl::Name& procedure, semantic::ScopeId scope) const;
    // The named (lower-case formal) and positional actuals of a VITAL
    // intrinsic procedure call.
    struct HirVitalActuals {
        std::vector<semantic::ExpressionId> positional;
        std::map<std::string, semantic::ExpressionId, std::less<>> named;
        [[nodiscard]] std::optional<semantic::ExpressionId> find(
            std::string_view formal, std::size_t position) const;
    };
    // Reports a duplicate named actual, or a positional actual after a
    // named one, as `code` for the `label` procedure family.
    [[nodiscard]] HirVitalActuals collect_hir_vital_actuals(
        std::span<const semantic::vhdl::ProcedureAssociation> arguments,
        std::string_view code, std::string_view label, bool& valid);
    // An expression's source span, or `fallback` when it has none.
    [[nodiscard]] frontend::SourceSpan hir_expression_span(
        semantic::ExpressionId id, const frontend::SourceSpan& fallback) const;
    // A static Boolean or string-literal VITAL actual, or `fallback` when
    // the formal has no actual.
    [[nodiscard]] bool hir_vital_static_bool(
        std::optional<semantic::ExpressionId> expression,
        std::string_view formal, bool fallback, std::string_view code,
        std::string_view label, bool& valid);
    [[nodiscard]] std::string hir_vital_static_string(
        std::optional<semantic::ExpressionId> expression,
        std::string_view formal, std::string_view fallback,
        std::string_view code, std::string_view label, bool& valid);
    // Protected objects (IEEE 1076-2008 5.6, lowerer_hir_vhdl_special.cpp):
    // a method runs as an ordinary callable frame specialized per object.
    // The object's private variables are global storage, loaded into the
    // frame's locals on entry and stored on exit and around nested calls.
    struct HirVhdlProtectedMember {
        semantic::DeclarationId declaration;
        ContainerObjectId storage { };
    };
    struct HirVhdlProtectedCache {
        semantic::DeclarationId declaration;
        ContainerObjectId storage { };
        ContainerRegisterId container { };
        RegisterId value { };
    };
    // The protected type of the object a method call is selected from.
    [[nodiscard]] const semantic::vhdl::TypeDefinition*
    hir_vhdl_protected_type(
        semantic::ScopeId scope, std::string_view object) const;
    // The defined methods of the protected body that `name`
    // (`object.method`) selects when its prefix denotes a protected object.
    [[nodiscard]] std::optional<semantic::CompiledVhdlPackageMember>
    hir_vhdl_protected_instance(
        semantic::ScopeId scope, std::string_view object) const;
    [[nodiscard]] std::vector<semantic::CompiledVhdlCallableResolution>
    hir_vhdl_protected_method_resolutions(const semantic::vhdl::Name& name,
        semantic::ScopeId scope, bool function) const;
    // The private-variable storage of the object a call of `method` named
    // `call_name` runs on: the selected object, or the active method's own
    // object for an unprefixed call of a sibling method.
    [[nodiscard]] std::optional<std::vector<HirVhdlProtectedMember>>
    hir_vhdl_protected_call_members(std::string_view call_name,
        semantic::ScopeId scope, semantic::DeclarationId method) const;
    // A constant of an architecture, block or package whose composite value
    // the elaborator cannot fold is computed once at run time: its first
    // reader runs the initializer (IEEE 1076-2008 14.4.2.2) into global
    // storage that later readers load.
    struct HirVhdlGlobalFile {
        semantic::DeclarationId declaration;
        ContainerObjectId storage { };
        RegisterId handle { };
    };
    [[nodiscard]] bool hir_vhdl_design_level_declaration(
        const semantic::vhdl::Declaration& declaration) const;
    [[nodiscard]] std::vector<HirVhdlGlobalFile> bind_hir_vhdl_global_files(
        std::span<const semantic::ExpressionId> actuals);
    void release_hir_vhdl_global_files(
        std::span<const HirVhdlGlobalFile> files);
    // A design-level constant read through a name; a member of a package
    // instance carries that instance's generic actuals (IEEE 1076-2008 4.9).
    struct HirLazyGlobalConstant {
        semantic::DeclarationId declaration;
        std::optional<semantic::CompiledVhdlPackageMember> instance;
    };
    [[nodiscard]] std::optional<HirLazyGlobalConstant>
    hir_vhdl_lazy_global_constant(semantic::ExpressionId expression) const;
    [[nodiscard]] bool materialize_hir_vhdl_global_constant(
        const HirLazyGlobalConstant& constant);
    bool materializing_hir_vhdl_global_constant_ { };
    // std::randomize lvalue arguments and their solver identities; a
    // with-clause expression structurally equal to one names that target.
    std::vector<std::pair<semantic::ExpressionId, std::string>>
        hir_scope_randomize_arguments_;
    // The width of an instance property sized by class value parameters in
    // the specialization the receiver's declared type names.
    [[nodiscard]] std::optional<std::size_t>
    hir_class_property_specialized_width(
        const semantic::sv::ClassProperty& property,
        semantic::ExpressionId receiver) const;
    // Creates a protected object's anchor and registry entry.
    ContainerObjectId create_hir_vhdl_protected_anchor(const std::string& name,
        const std::string& type_name, const frontend::SourceSpan& source);
    // The anchor of the protected object `object` names in `scope`.
    [[nodiscard]] std::optional<ContainerObjectId> hir_vhdl_protected_anchor(
        std::string_view object, semantic::ScopeId scope) const;
    // The protected type a subtype denotes, if any.
    [[nodiscard]] const semantic::vhdl::TypeDefinition*
    hir_vhdl_subtype_protected_type(
        const semantic::vhdl::SubtypeIndication& subtype) const;
    // Reports a protected method that suspends (VHPROTECTED-020) and a pure
    // function calling an impure method (VHPROTECTED-022); true when legal.
    [[nodiscard]] bool check_hir_vhdl_protected_call(
        semantic::DeclarationId method, std::string_view call_name,
        semantic::SourceSpanId source);
    // A function call selected from a protected object (`p.get`).
    [[nodiscard]] bool is_hir_vhdl_protected_expression(
        semantic::ExpressionId expression) const;
    // Object members for the frame being created by the call in progress.
    std::optional<std::vector<HirVhdlProtectedMember>>
        pending_hir_vhdl_protected_members_;
    std::vector<HirVhdlProtectedCache> hir_vhdl_protected_caches_;
    // Storage of protected objects declared in processes and subprograms,
    // keyed by hierarchy path and declaration.
    std::unordered_map<std::string, ContainerObjectId>
        hir_vhdl_local_protected_anchors_;
    // The anchor first, then the private variables' storage, created on
    // first use.
    [[nodiscard]] std::optional<std::vector<HirVhdlProtectedMember>>
    hir_vhdl_protected_object_storage(ContainerObjectId anchor,
        const semantic::vhdl::TypeDefinition& body);
    // Creates the object a variable of protected type declares and
    // initializes its private variables; nullopt when not protected.
    [[nodiscard]] std::optional<bool> initialize_hir_vhdl_protected_object(
        semantic::DeclarationId declaration,
        const semantic::vhdl::Declaration& variable);
    [[nodiscard]] std::string hir_vhdl_local_protected_key(
        semantic::DeclarationId declaration) const;
    void load_hir_vhdl_protected_caches(
        std::span<const HirVhdlProtectedMember> members);
    void flush_hir_vhdl_protected_caches();
    void reload_hir_vhdl_protected_caches();
    // Clears the register, local and control state of one lowered process.
    void reset_hir_process_state();
    // A resolver over the specialized unit that sees the generic bindings
    // of the instance being lowered.
    [[nodiscard]] semantic::CompiledDesignResolver hir_resolver() const
    {
        return semantic::CompiledDesignResolver {
            *specialized_hir_unit_, hir_generic_binding_frames_
        };
    }
    // The type a VHDL type mark spelling denotes when looked up from
    // `scope`, for type marks the HIR left unresolved.
    [[nodiscard]] std::optional<semantic::TypeId> hir_vhdl_type_of_spelling(
        std::string_view spelling, semantic::ScopeId scope) const;
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
    hir_systemverilog_packed_shape(
        semantic::ExpressionId expression,
        bool element_shape = false) const;
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
    // Whether a VHDL subtype resolves to predefined CHARACTER.
    [[nodiscard]] bool hir_vhdl_predefined_character_subtype(
        const semantic::vhdl::SubtypeIndication& subtype,
        semantic::ScopeId use_scope) const;
    // Collects the logic character literals of the current unit whose
    // context (assignment target, initialized object, comparison operand,
    // case selector, or attribute prefix) is CHARACTER.
    void collect_hir_vhdl_character_contexts() const;
    mutable const semantic::vhdl::Hir* hir_vhdl_character_context_hir_ { };
    mutable std::uint64_t hir_vhdl_character_context_revision_ { };
    mutable std::unordered_set<std::uint32_t>
        hir_vhdl_character_context_literals_;
    // Character literals with a visible user enumeration literal whose known
    // context type (target, other operand, selector) is a different type:
    // they denote the predefined type's literal (IEEE 1076-2008 9.3.2).
    mutable std::unordered_set<std::uint32_t>
        hir_vhdl_foreign_context_literals_;
    [[nodiscard]] std::optional<std::size_t>
    hir_vhdl_null_concatenation_operand(
        std::span<const semantic::ExpressionId> operands) const;
    [[nodiscard]] std::optional<std::size_t>
    hir_vhdl_expression_runtime_width(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] std::optional<std::size_t>
    hir_systemverilog_type_width(
        const semantic::sv::TypeReference& type,
        std::optional<semantic::ScopeId> use_scope = std::nullopt) const;
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
    [[nodiscard]] bool hir_systemverilog_size_cast_operand_is_integral(
        semantic::ExpressionId expression) const;
    // The underlying type of the type name `name` visible in `scope`.
    [[nodiscard]] std::optional<semantic::sv::TypeReference>
    hir_systemverilog_named_type(
        std::string_view name, semantic::ScopeId scope) const;
    // The type of `T'(x)`; with `containers`, also an unpacked array or
    // queue type (a bit-stream cast, 6.24.3).
    [[nodiscard]] std::optional<semantic::sv::TypeReference>
    hir_systemverilog_cast_type(
        semantic::ExpressionId expression, bool containers = false) const;
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
        numeric_extremum,
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
        /// The dimensions of a multidimensional packed member.
        std::vector<HirPackedRange> shape;
        /// The member's declared type, where a type path selected it.
        std::optional<semantic::sv::TypeReference> member_type;
    };
    [[nodiscard]] std::optional<std::size_t>
    hir_systemverilog_member_offset(
        const semantic::sv::TypeDefinition& type,
        std::size_t member_index, bool unpacked_structures = false) const;
    [[nodiscard]] std::optional<HirPackedMemberSelection>
    hir_systemverilog_member_selection(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<HirPackedMemberSelection>
    hir_systemverilog_indexed_member(
        semantic::ExpressionId expression,
        bool element_relative = false) const;
    // Adds a packed member's offset to a constant selection (or makes the
    // member the selection of an unselected target); false on overflow.
    [[nodiscard]] static bool add_hir_member_selection(
        const std::optional<HirPackedMemberSelection>& member,
        std::optional<HirConstantSelection>& selection, bool selected);
    [[nodiscard]] std::optional<HirPackedMemberSelection>
    hir_systemverilog_member_path(
        semantic::DeclarationId declaration, std::string_view path) const;
    // A member path through a packed structure type, as a declaration's
    // members (`declaration` is recorded in the result). With
    // `unpacked_structures`, unpacked structures are laid out as packed ones
    // (class properties store them so).
    [[nodiscard]] std::optional<HirPackedMemberSelection>
    hir_systemverilog_type_member_path(const semantic::sv::TypeReference& type,
        semantic::ScopeId scope, semantic::DeclarationId declaration,
        std::string_view path, bool unpacked_structures = false) const;
    // An enumeration method (IEEE 1800-2017 6.19.5): `e.first()`, or the
    // parenthesis-free `e.next` that the frontend keeps as a dotted name.
    struct HirEnumerationMethod {
        std::string method;
        std::optional<semantic::ExpressionId> receiver;
        semantic::DeclarationId receiver_declaration;
        std::optional<semantic::ExpressionId> count;
        /// A packed structure member receiver (`s.m.name`).
        std::optional<HirPackedMemberSelection> member;
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
    hir_systemverilog_enumeration_type_profile(HirEnumerationMethod result,
        semantic::sv::TypeReference type, semantic::ScopeId scope) const;
    [[nodiscard]] std::optional<semantic::sv::TypeReference>
    hir_systemverilog_value_type(semantic::ExpressionId expression,
        std::size_t trailing_segments = 0U) const;
    [[nodiscard]] std::optional<HirEnumerationMethod>
    hir_systemverilog_enumeration_profile(HirEnumerationMethod method,
        semantic::DeclarationId declaration) const;
    // The text `%p` writes for a value (IEEE 1800-2017 21.2.1.7) when it is
    // not formatted like `%0d` or `%g`: an unpacked value as an assignment
    // pattern, a string (to be quoted) or an enumeration value's name.
    struct HirPatternText {
        bool failed { };
        std::optional<StringRegisterId> text;
        bool quoted { };
    };
    [[nodiscard]] HirPatternText lower_hir_pattern_text(
        semantic::ExpressionId value);
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
        frontend::SystemVerilogScalarKind source, std::size_t width = 64U);
    // A value lowered for an element or member of `target` kind (None for
    // integral), converted between real and integral (IEEE 1800-2017
    // 10.9: each pattern item is assigned as if by an assignment).
    [[nodiscard]] std::optional<RegisterId> lower_hir_scalar_converted_value(
        semantic::ExpressionId value, std::size_t width,
        frontend::SystemVerilogScalarKind target);
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
        bool access_object_root { };
    };
    struct HirVhdlAggregateTargetSegments {
        std::vector<semantic::ExpressionId> targets;
        std::vector<std::size_t> widths;
        std::vector<semantic::vhdl::SubtypeIndication> subtypes;
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
    [[nodiscard]] std::optional<semantic::vhdl::SubtypeIndication>
    hir_vhdl_constant_record_object_subtype(
        semantic::DeclarationId declaration, semantic::ScopeId use_scope) const;
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
    // The predefined MINIMUM and MAXIMUM of scalar values, and the array
    // reduction forms (IEEE 1076-2008 5.2.6, 5.3.2.4).
    [[nodiscard]] std::optional<StringRegisterId> lower_hir_vhdl_real_to_string(
        semantic::ExpressionId expression);
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_vhdl_static_string_concatenation(
        semantic::ExpressionId expression, std::size_t expected_width);
    [[nodiscard]] std::optional<std::size_t> hir_vhdl_string_element_width(
        semantic::ExpressionId expression) const;
    struct HirVhdlCharacterElements {
        std::vector<std::string> images;
        std::size_t element_width { };
    };
    [[nodiscard]] std::optional<HirVhdlCharacterElements>
    hir_vhdl_character_enumeration_elements(
        const semantic::vhdl::TypeDefinition& array) const;
    [[nodiscard]] std::optional<std::size_t>
    hir_vhdl_qualified_enumeration_string_width(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<RegisterId> lower_hir_vhdl_enumeration_string(
        semantic::ExpressionId expression, std::size_t expected_width);
    struct HirVhdlMinMaxShape {
        bool maximum { };
        bool real { };
        bool array { };
        std::size_t element_width { };
        std::size_t array_width { };
        std::optional<std::int64_t> null_value;
    };
    [[nodiscard]] std::optional<HirVhdlMinMaxShape> hir_vhdl_min_max_shape(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<RegisterId> lower_hir_vhdl_min_max(
        semantic::ExpressionId expression, std::size_t expected_width);
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
        std::optional<PackedLogic4> packed_constant { };
        std::optional<semantic::vhdl::SubtypeIndication> scalar_subtype { };
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
        std::size_t storage_width { };
        frontend::ValueDomain storage_domain {
            frontend::ValueDomain::Unknown
        };
        std::optional<HirConstantSelection> selection;
        // The declaration is the resolved actual behind any signal-formal
        // alias; the process scope is inherited by deferred callable bodies.
        std::optional<semantic::DeclarationId> signal_declaration;
        std::optional<semantic::ScopeId> driving_process_scope;
        // 'EVENT of an indexed element of the signal: the element read.
        std::optional<semantic::ExpressionId> element;
        // S'STABLE(T)'EVENT/'ACTIVE or S'QUIET(T)'EVENT/'ACTIVE: the kind of
        // the implicit signal prefix whose changes are tested.
        std::optional<HirVhdlSignalAttributeKind> implicit_prefix;
        // An attribute of S'DELAYED(T): T; the attribute reads the implicit
        // signal, which has S's layout.
        std::optional<runtime::SimulationTick> delayed_prefix;
    };
    [[nodiscard]] std::optional<HirVhdlSignalAttributeProfile>
    hir_vhdl_signal_attribute_profile(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] std::optional<semantic::vhdl::Direction>
    hir_vhdl_signal_formal_direction(
        semantic::ExpressionId attribute_expression) const;
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
        frontend::SourceSpan span,
        std::optional<HirConstantSelection> selection = std::nullopt);
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
        const semantic::vhdl::SubtypeIndication* contextual_subtype = nullptr,
        std::size_t array_dimension_offset = 0U);
    [[nodiscard]] std::optional<semantic::vhdl::SubtypeIndication>
    hir_vhdl_concat_aggregate_context(
        const semantic::vhdl::SubtypeIndication& result_context,
        semantic::ExpressionId aggregate,
        std::size_t expected_width) const;
    [[nodiscard]] std::optional<std::size_t>
    hir_vhdl_positional_aggregate_width(
        semantic::ExpressionId aggregate,
        const semantic::vhdl::SubtypeIndication& contextual_subtype,
        const semantic::vhdl::TypeDefinition* contextual_definition = nullptr) const;
    // Copies a composite value back through an aggregate of writable names,
    // the actual of individually associated out or inout subelements (IEEE
    // 1076-2008 6.5.7.1).
    [[nodiscard]] bool lower_hir_vhdl_aggregate_copy_out(
        semantic::ExpressionId aggregate,
        RegisterId source,
        const semantic::vhdl::SubtypeIndication& subtype);
    [[nodiscard]] std::optional<HirVhdlAggregateTargetSegments>
    hir_vhdl_mixed_aggregate_target_segments(
        semantic::ExpressionId aggregate,
        const semantic::vhdl::SubtypeIndication& source_subtype,
        std::size_t source_width) const;
    [[nodiscard]] bool hir_vhdl_aggregate_is_writable(
        semantic::ExpressionId aggregate) const;
    [[nodiscard]] bool can_lower_hir_expression(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope,
        std::unordered_set<std::uint32_t>& visiting) const;
    [[nodiscard]] std::optional<RegisterId> lower_hir_expression(
        semantic::ExpressionId expression,
        std::size_t expected_width,
        frontend::SystemVerilogScalarKind scalar_context
        = frontend::SystemVerilogScalarKind::None,
        const semantic::vhdl::SubtypeIndication* vhdl_array_context = nullptr);
    [[nodiscard]] std::optional<RegisterId> lower_hir_expression_untraced(
        semantic::ExpressionId expression,
        std::size_t expected_width,
        frontend::SystemVerilogScalarKind scalar_context,
        const semantic::vhdl::SubtypeIndication* vhdl_array_context);
    [[nodiscard]] std::optional<RegisterId> lower_hir_expression_impl(
        semantic::ExpressionId expression,
        std::size_t expected_width,
        frontend::SystemVerilogScalarKind scalar_context,
        const semantic::vhdl::SubtypeIndication* vhdl_array_context);
    // The innermost expression that failed to lower without its own
    // diagnostic while the current statement was lowered; named in the
    // statement's FSIM-ELAB-HIR-001 report.
    std::optional<std::string> hir_unlowered_expression_;
    // The first construct or step that rejected the current statement or
    // process without a diagnostic of its own; named in FSIM-ELAB-HIR-001.
    std::optional<std::string> hir_lowering_rejection_;
    // Records why lowering failed (the first reason wins) and returns false.
    bool reject_hir_lowering(std::string reason);
    // "<kind> expression '<text>' at <line>:<column>" for diagnostics.
    [[nodiscard]] std::optional<std::string> hir_expression_summary(
        semantic::ExpressionId expression) const;
    // An unsigned SystemVerilog operation zero-extends its signed
    // context-determined operands (IEEE 1800-2017 11.8.2): the next lowered
    // operand extends without sign, and an operator propagates that to its
    // own operands.
    bool hir_unsigned_extension_ = false;
    // Set while lowering an aggregate whose context is not one that fixes
    // its direction (a relational operand): a named aggregate then takes
    // its direction from the index subtype (IEEE 1076-2008 9.3.3.3).
    bool hir_vhdl_aggregate_free_direction_ { };
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
    // A typed mailbox's element: a string, or a packed type of `width`
    // bits (IEEE 1800-2017 15.4.9). Reports FSIM-ELAB-SVSYNC-002 with
    // `message` when the element has neither form.
    struct HirMailboxElement {
        bool string { };
        std::uint32_t width { };
    };
    [[nodiscard]] std::optional<HirMailboxElement> hir_mailbox_element(
        const semantic::sv::TypeReference* mailbox_type,
        std::string_view message, const frontend::SourceSpan& span);
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
    // The IEEE.MATH_REAL function (IEEE 1076.2) a VHDL call names, by its
    // lower-case simple name, when every operand is REAL.
    [[nodiscard]] std::optional<std::string> hir_vhdl_math_real_function(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<RegisterId> lower_hir_vhdl_math_real_call(
        semantic::ExpressionId expression);
    // `T(x)` for array types whose elements are integer and floating: each
    // element converts in place (IEEE 1076-2008 9.3.6).
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_vhdl_numeric_array_conversion(semantic::ExpressionId operand,
        const semantic::vhdl::SubtypeIndication& target_element,
        const semantic::vhdl::SubtypeIndication& operand_element,
        std::size_t target_width);
    // A value of a closely related type in the target type: record
    // elements match by name and convert one by one, recursively (IEEE
    // 1076-2019 9.3.6).
    [[nodiscard]] std::optional<RegisterId>
    convert_hir_vhdl_closely_related_value(RegisterId value,
        const semantic::vhdl::SubtypeIndication& operand,
        const semantic::vhdl::SubtypeIndication& target);
    // The value of the IEEE.MATH_REAL constant (MATH_PI, ...) a VHDL name
    // denotes.
    [[nodiscard]] std::optional<double> hir_vhdl_math_real_constant(
        semantic::ExpressionId expression) const;
    // IEEE.MATH_REAL.UNIFORM (IEEE 1076.2-1996): the procedure call's
    // outcome when it names that procedure, nullopt otherwise.
    [[nodiscard]] std::optional<bool> lower_hir_vhdl_math_real_uniform(
        semantic::StatementId statement);
    // The unit whose timescale governs a SystemVerilog scope: a package or
    // $unit subroutine keeps its declaring unit's time unit (IEEE 1800-2017
    // 3.14.2), anything else uses the unit being lowered.
    [[nodiscard]] const semantic::sv::Unit* hir_time_scope_unit(
        semantic::ScopeId scope) const;
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
    [[nodiscard]] bool hir_systemverilog_own_element_member(
        semantic::ExpressionId expression_id) const;
    [[nodiscard]] std::optional<bool> hir_systemverilog_packed_element_signed(
        semantic::ExpressionId expression_id) const;
    [[nodiscard]] bool hir_systemverilog_type_signed(
        const semantic::sv::TypeReference& type,
        std::optional<semantic::ScopeId> use_scope = std::nullopt) const;
    [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>>
    hir_systemverilog_parameter_member_slice(
        semantic::ExpressionId parameter, std::string_view path) const;
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_systemverilog_parameter_pattern(
        semantic::ExpressionId parameter, semantic::ExpressionId value);
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_systemverilog_packed_array_pattern(
        semantic::ExpressionId expression,
        const semantic::sv::TypeReference& type, std::size_t expected_width);
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_systemverilog_packed_pattern(
        semantic::ExpressionId expression,
        const semantic::sv::TypeReference& type,
        std::size_t expected_width);
    [[nodiscard]] bool lower_hir_systemverilog_file_statement(
        semantic::StatementId statement);
    [[nodiscard]] bool is_hir_vhdl_file_call(
        semantic::ExpressionId expression) const;
    // The width and domain of a file call's result: BOOLEAN for ENDFILE
    // and FILE_CANSEEK, an enumeration for FILE_OPEN, FILE_STATE and
    // FILE_MODE, INTEGER otherwise (IEEE 1076-2019 5.5.2).
    [[nodiscard]] std::pair<std::size_t, frontend::ValueDomain>
    hir_vhdl_file_call_type(semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<std::uint64_t>
    hir_vhdl_standard_enumeration_literal(
        semantic::ExpressionId expression) const;
    // The position of a STD.REFLECTION TYPE_CLASS literal (CLASS_ARRAY and
    // so on) that names no user declaration (IEEE 1076-2019 16.12.2).
    [[nodiscard]] std::optional<std::uint64_t>
    hir_vhdl_reflection_class_literal(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<RegisterId> lower_hir_vhdl_file_call(
        semantic::ExpressionId expression);
    // A FILE_SEEK, FILE_TRUNCATE, FILE_POSITION or other VHDL-2019 file
    // operation on the file object held in `file`, as a 32-bit result; a
    // missing amount is 0 and a missing origin FILE_ORIGIN_BEGIN (IEEE
    // 1076-2019 5.5.2).
    [[nodiscard]] std::optional<RegisterId> lower_hir_vhdl_file_position(
        runtime::simir::FilePositionKind kind, RegisterId file,
        semantic::ExpressionId file_expression,
        std::optional<semantic::ExpressionId> amount,
        std::optional<semantic::ExpressionId> origin);
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
    [[nodiscard]] bool is_hir_vhdl_environment_expression(
        semantic::ExpressionId expression) const;
    // A STD.ENV status literal compared with a status-returning STD.ENV
    // function call (`dir_createdir(p) = STATUS_ITEM_EXISTS`).
    [[nodiscard]] std::optional<std::uint64_t>
    hir_vhdl_environment_call_status_literal(
        semantic::ExpressionId literal,
        semantic::ExpressionId call) const;
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
    [[nodiscard]] HirVhdlAccessHeap* hir_vhdl_access_heap(
        const semantic::vhdl::TypeDefinition& type,
        const frontend::SourceSpan& span,
        const semantic::vhdl::SubtypeIndication* access_subtype = nullptr,
        std::optional<semantic::ExpressionId> initializer = std::nullopt,
        std::span<const semantic::vhdl::RangeConstraint>
            allocator_constraints = {});
    // Issues a fresh handle of `heap` within its live-object limit and
    // stores the new object's value through `store(handle)`.
    [[nodiscard]] RegisterId issue_hir_vhdl_access_handle(
        const HirVhdlAccessHeap& heap, const frontend::SourceSpan& span,
        const std::function<void(RegisterId)>& store);
    // Fails at run time unless `handle` names a live object of `heap`
    // (IEEE 1076-2008 8.3: a dereference of null is an error).
    void emit_hir_vhdl_access_live_check(const HirVhdlAccessHeap& heap,
        RegisterId handle, const frontend::SourceSpan& span);
    [[nodiscard]] const semantic::vhdl::TypeDefinition*
    hir_vhdl_access_type_definition(
        const semantic::vhdl::SubtypeIndication& subtype) const;
    // A whole-range slice `p(p'range)` of an access-to-STRING value `p`:
    // the access value, whose designated string the slice denotes.
    [[nodiscard]] std::optional<semantic::ExpressionId>
    hir_vhdl_access_string_whole_slice(
        semantic::ExpressionId expression) const;
    // The effective subtype an access subtype designates.
    [[nodiscard]] std::optional<semantic::vhdl::SubtypeIndication>
    hir_vhdl_designated_subtype(
        const semantic::vhdl::SubtypeIndication& access_subtype) const;
    // An access type's designated subtype with its heap's object shape.
    [[nodiscard]] std::optional<semantic::vhdl::SubtypeIndication>
    hir_vhdl_access_shape_subtype(
        const semantic::vhdl::TypeDefinition& type) const;
    // The STRING object an access value designates.
    [[nodiscard]] std::optional<StringRegisterId>
    lower_hir_vhdl_access_string_handle(
        semantic::ExpressionId prefix, const frontend::SourceSpan& span);
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_vhdl_access_expression(
        semantic::ExpressionId expression,
        std::size_t expected_width,
        const semantic::vhdl::SubtypeIndication* expected_access_subtype
        = nullptr,
        bool implicit_dereference = false);
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_vhdl_access_object(
        RegisterId handle,
        const semantic::vhdl::SubtypeIndication& access_subtype,
        const frontend::SourceSpan& span);
    [[nodiscard]] std::optional<StringRegisterId>
    lower_hir_vhdl_access_string_object(
        semantic::ExpressionId dereference);
    [[nodiscard]] bool is_hir_vhdl_access_deallocation(
        semantic::StatementId statement) const;
    [[nodiscard]] bool lower_hir_vhdl_access_deallocation(
        semantic::StatementId statement);
    [[nodiscard]] bool is_hir_vhdl_access_assignment(
        semantic::StatementId statement) const;
    // `p(i) := v` for an access value p: nullopt when the target is not
    // such an element.
    [[nodiscard]] std::optional<bool> lower_hir_vhdl_access_element_assignment(
        semantic::ExpressionId target, semantic::ExpressionId value);
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
    struct HirVhdlPhysicalUnit {
        std::size_t width { };
        std::int64_t scale { };
    };
    [[nodiscard]] std::optional<HirVhdlPhysicalUnit> hir_vhdl_physical_unit(
        std::string_view unit_name,
        std::size_t expected_width,
        semantic::ScopeId source_scope) const;
    [[nodiscard]] std::optional<std::string> hir_vhdl_physical_unit_name(
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
    [[nodiscard]] std::optional<std::size_t>
    hir_vhdl_predefined_string_concat_length(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope,
        bool* predefined_operator = nullptr,
        bool compute_length = true) const;
    /// Check operator eligibility without deriving a static result length.
    [[nodiscard]] bool hir_vhdl_is_predefined_string_concat(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] bool can_lower_hir_string_expression(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope,
        const semantic::vhdl::SubtypeIndication* contextual_subtype
            = nullptr) const;
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
    // The `width`-bit `selection` of `value`; fails when the selection is
    // not `width` bits or leaves the register.
    [[nodiscard]] std::optional<RegisterId> lower_hir_selection_extract(
        RegisterId value, const HirConstantSelection& selection,
        std::size_t width, frontend::ValueDomain domain);
    // Whether `value` is a slice of a named array whose constant bounds are
    // a null range inside the prefix's index range, in its direction; only
    // such bounds, never a missing width, prove a zero-element segment.
    // `prefix_matches` checks the prefix's type.
    [[nodiscard]] bool hir_vhdl_proven_null_slice(semantic::ExpressionId value,
        semantic::ScopeId fallback_scope,
        const std::function<bool(semantic::ExpressionId)>& prefix_matches)
        const;
    // A string holding the `width / 8` CHARACTER bytes of `packed`, most
    // significant first, with no display-format NUL substitution.
    [[nodiscard]] StringRegisterId lower_hir_packed_characters_to_string(
        RegisterId packed, std::size_t width);
    // The text of one element of a predefined STRING concatenation or
    // positional aggregate: a STRING operand, or one CHARACTER (IEEE
    // 1076-2008 9.2.5, 9.3.3.3).
    [[nodiscard]] std::optional<StringRegisterId>
    lower_hir_vhdl_string_piece(semantic::ExpressionId operand,
        const semantic::vhdl::SubtypeIndication* string_context);
    // Unpacks a bit queue into the operands of a stream target, reversing
    // `<<` slices first (IEEE 1800-2017 11.4.14.3).
    [[nodiscard]] bool lower_hir_sv_stream_target_bits(
        semantic::ExpressionId target, ContainerRegisterId bits);
    [[nodiscard]] std::optional<StringRegisterId>
    lower_hir_string_expression(
        semantic::ExpressionId expression,
        const semantic::vhdl::SubtypeIndication* contextual_subtype = nullptr);
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
        // A selected signal formal reads one subrange of the root signal.
        // Keep the formal view separate from the root signal storage.
        std::optional<HirConstantSelection> signal_selection;
        std::optional<semantic::ExpressionId> signal_selection_expression;
        std::optional<std::size_t> signal_storage_width;
        std::optional<frontend::ValueDomain> signal_storage_domain;
        bool vhdl_port { };
        semantic::vhdl::Direction vhdl_direction {
            semantic::vhdl::Direction::unknown
        };
    };
    [[nodiscard]] bool signal_binding_can_emit_write(
        const HirRuntimeBinding& binding) const noexcept;
    [[nodiscard]] bool signal_binding_is_writable(
        const HirRuntimeBinding& binding) const noexcept;
    // Permits the writes of read-only signals from operation `begin` on.
    void authorize_readonly_signal_writes(std::size_t begin);
    void record_readonly_signal_write(SignalId signal);
    void record_readonly_vhdl_output_write(
        const HirRuntimeBinding& binding);
    [[nodiscard]] std::optional<HirRuntimeBinding>
    hir_direct_signal_binding(semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<HirRuntimeBinding> hir_runtime_binding(
        semantic::DeclarationId declaration,
        semantic::ScopeId process_scope,
        bool require_storage) const;
    [[nodiscard]] std::optional<semantic::ScopeId>
    hir_vhdl_current_process_scope() const;
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
    // push_back, push_front or insert of `value` into the queue in
    // `target` (IEEE 1800-2017 7.10.2); false when the value cannot be
    // lowered.
    [[nodiscard]] bool lower_hir_queue_push(
        runtime::simir::ContainerRegisterId target,
        const runtime::simir::ContainerType& type,
        semantic::DeclarationId declaration,
        semantic::ExpressionId value,
        bool front,
        std::optional<semantic::ExpressionId> insertion_index);
    // A static value-container class property
    // (`@sv-static-container-property:`) is one container object per
    // specialization (IEEE 1800-2017 8.9, 8.25), created at its first use
    // and holding its constant initializer's value.
    [[nodiscard]] std::optional<HirContainerObjectBinding>
    hir_class_static_container_binding(semantic::ExpressionId access) const;
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
        semantic::ExpressionId expression,
        bool packed_member = false) const;
    // A member of a packed element of a container (`d[1].header`): the
    // element and the member's bits within it (IEEE 1800-2017 7.2.1).
    struct HirContainerElementMember {
        HirContainerElementBinding element;
        std::size_t offset { };
        std::size_t width { };
        bool signed_value { };
        frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
    };
    [[nodiscard]] std::optional<HirContainerElementMember>
    hir_container_element_member(semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<HirPackedRange>
    hir_container_element_member_range(semantic::ExpressionId expression) const;
    // The packed type of the elements a container element binding selects.
    [[nodiscard]] std::optional<semantic::sv::TypeReference>
    hir_container_element_packed_type(
        const HirContainerElementBinding& element) const;
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
    struct HirArrayQueryShape {
        std::vector<HirPackedRange> dimensions;
        // Keep variable-sized unpacked dimensions in the same slowest-first
        // vector as fixed and packed dimensions. A parallel marker preserves
        // the distinction needed by IEEE 1800-2017 20.7.1.
        std::vector<bool> variable_dimensions;
        std::size_t unpacked_dimensions { };
    };
    [[nodiscard]] std::optional<HirArrayQueryShape>
    hir_systemverilog_array_query_shape(
        semantic::ExpressionId expression,
        bool count_associative = false) const;
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
            // `index` is a string register (a string-keyed level).
            bool string_index { };
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
    struct HirAggregateElementTarget {
        runtime::simir::ContainerRegisterId output { };
        RegisterId index { };
        bool signed_index { };
    };
    [[nodiscard]] std::optional<runtime::simir::ContainerRegisterId>
    lower_hir_container_assignment_pattern(
        semantic::ExpressionId expression,
        const HirContainerObjectBinding& target,
        const std::optional<HirAggregateElementTarget>& element_target
        = std::nullopt);
    [[nodiscard]] std::optional<runtime::simir::ContainerType>
    hir_systemverilog_container_type(
        const semantic::sv::TypeReference& type) const;
    // The container type T of a typed assignment pattern `T'{...}` with
    // scalar or packed elements (IEEE 1800-2017 10.9).
    [[nodiscard]] std::optional<runtime::simir::ContainerType>
    hir_typed_pattern_container_type(
        const semantic::sv::Expression& source) const;
    struct HirStaticContainerValue {
        runtime::simir::ContainerRegisterId value { };
        runtime::simir::ContainerType type;
    };
    // A SystemVerilog unpacked value path (IEEE 1800-2017 7.2, 7.4): a
    // container object, then index and member steps. `type` is the selected
    // value's profile; a leaf is a packed or scalar element or member.
    struct HirSvValuePath {
        struct Step {
            std::optional<std::uint32_t> member;
            std::string member_name;
            std::vector<semantic::ExpressionId> indices;
            // The container an index step selects from.
            runtime::simir::ContainerType container;
        };
        HirContainerObjectBinding root;
        // A value-container property of an object (`obj.v[i].m`): the root
        // is read from, and written back to, the object (IEEE 1800-2017
        // 8.4). `root.type` then points at `class_root_type`.
        struct ClassRoot {
            // None: a property of `this` named by its simple name.
            std::optional<semantic::ExpressionId> receiver;
            std::string identity;
        };
        std::optional<ClassRoot> class_root;
        std::shared_ptr<const runtime::simir::ContainerType> class_root_type;
        std::vector<Step> steps;
        runtime::simir::ContainerType type;
        bool leaf { };
        bool string_leaf { };
        // A constant slice of the selected one-dimensional fixed array
        // (`s.arr[3:1]`, `a[1:0]`, IEEE 1800-2017 7.4.6) ends the path:
        // `type` is the slice and `base` the array it selects from.
        struct Slice {
            std::int64_t left { };
            std::int64_t right { };
            runtime::simir::ContainerType base;
        };
        std::optional<Slice> slice;
        // A select within a packed leaf whose position is known only at run
        // time (`m[i][j]`, `m[i][b +: 4]`, IEEE 1800-2017 7.4.5, 11.5.1).
        struct PackedSelect {
            enum class Kind : std::uint8_t { index, up, down };
            Kind kind { Kind::index };
            // The index, or the base of an indexed part-select.
            semantic::ExpressionId index;
            HirPackedRange dimension;
            // Bits per element of the selected dimension.
            std::size_t stride { };
            // Elements an indexed part-select spans.
            std::size_t count { 1U };
        };
        // Bits of the leaf value: a member of a packed leaf (`s.pt.b`), or
        // a bit-, part- or element-select of it. The selected bits start at
        // `offset` plus the run-time offsets of `selects`.
        struct PackedTail {
            std::size_t offset { };
            std::size_t width { };
            bool signed_value { };
            std::vector<PackedSelect> selects;
        };
        std::optional<PackedTail> packed_tail;
    };
    struct HirSvValuePathCursor {
        struct Level {
            runtime::simir::ContainerRegisterId container { };
            RegisterId index { };
            bool signed_index { true };
            // A position in a multidimensional fixed array.
            bool linear_index { };
            std::vector<std::uint32_t> members;
            runtime::simir::ContainerType type;
            // `index` is a string register (a string-keyed level).
            bool string_index { };
        };
        runtime::simir::ContainerRegisterId root { };
        runtime::simir::ContainerRegisterId container { };
        std::vector<Level> levels;
        // The object a class-property root is written back to.
        std::optional<RegisterId> class_receiver;
    };
    // A packed element of an object's multidimensional array reached by
    // indexing a subarray (`obj.m[1][2]`, IEEE 1800-2017 7.4.5).
    [[nodiscard]] std::optional<HirSvValuePath> hir_class_subarray_element(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<HirSvValuePath> hir_sv_value_path(
        semantic::ExpressionId expression) const;
    // The bits of a fixed container's packed signal bridge that a place with
    // constant selects occupies, leftmost element and first member most
    // significant (IEEE 1800-2017 10.6.2: force targets on that image).
    struct HirPlaceSignalImage {
        SignalId signal { };
        HirConstantSelection selection;
        // The variable the place belongs to.
        semantic::DeclarationId declaration;
    };
    [[nodiscard]] std::optional<HirPlaceSignalImage> hir_place_signal_image(
        semantic::ExpressionId expression) const;
    // The packed dimensions of a value path's packed leaf, outermost first;
    // empty when they are not known.
    [[nodiscard]] std::vector<HirPackedRange> hir_sv_value_path_leaf_shape(
        const HirSvValuePath& path, semantic::ScopeId scope) const;
    // The run-time low bit of a packed tail with run-time selects.
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_sv_value_path_tail_offset(const HirSvValuePath& path);
    // `container` holds the value the step selects from (for `$`).
    [[nodiscard]] std::optional<RegisterId> lower_hir_sv_value_path_index(
        const HirSvValuePath& path, const HirSvValuePath::Step& step,
        std::optional<runtime::simir::ContainerRegisterId> container);
    [[nodiscard]] std::optional<HirSvValuePathCursor>
    lower_hir_sv_value_path_descend(const HirSvValuePath& path);
    void publish_hir_sv_value_path(
        const HirSvValuePath& path, const HirSvValuePathCursor& cursor);
    [[nodiscard]] std::optional<RegisterId> lower_hir_sv_value_path_read(
        const HirSvValuePath& path);
    // The string member a string-leaf path selects (`o.q[i].name`).
    [[nodiscard]] std::optional<StringRegisterId>
    lower_hir_sv_value_path_string_read(const HirSvValuePath& path);
    // The selected value, including a packed member tail.
    [[nodiscard]] std::optional<RegisterId> lower_hir_sv_value_path_value(
        const HirSvValuePath& path);
    [[nodiscard]] std::optional<HirStaticContainerValue>
    lower_hir_sv_value_path_container_read(const HirSvValuePath& path);
    // Copy elements `left`..`right` (declared indices) into a fixed array
    // that has those indices, from the same indices of `source` or, with
    // `source_positional`, from its elements in order.
    [[nodiscard]] bool lower_hir_container_element_copy(
        runtime::simir::ContainerRegisterId source,
        runtime::simir::ContainerRegisterId target,
        const runtime::simir::ContainerType& type, std::int64_t left,
        std::int64_t right, bool source_positional = false);
    struct HirAggregateLeaf {
        std::vector<std::uint32_t> members;
        std::size_t width { };
        bool two_state { };
    };
    [[nodiscard]] std::optional<std::vector<HirAggregateLeaf>>
    hir_aggregate_packed_leaves(
        const runtime::simir::ContainerType& type) const;
    [[nodiscard]] std::optional<RegisterId> pack_hir_aggregate_value(
        runtime::simir::ContainerRegisterId value,
        const runtime::simir::ContainerType& type);
    [[nodiscard]] std::optional<runtime::simir::ContainerRegisterId>
    unpack_hir_aggregate_value(
        RegisterId packed, const runtime::simir::ContainerType& type);
    [[nodiscard]] bool lower_hir_sv_value_path_write(
        const HirSvValuePath& path,
        std::optional<RegisterId> packed_value,
        std::optional<runtime::simir::ContainerRegisterId> container_value);
    [[nodiscard]] bool hir_sv_container_stream(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<runtime::simir::ContainerRegisterId>
    lower_hir_sv_bitstream(semantic::ExpressionId expression);
    [[nodiscard]] std::optional<RegisterId> lower_hir_sv_bitstream_value(
        semantic::ExpressionId expression, std::size_t width,
        frontend::ValueDomain domain);
    [[nodiscard]] std::optional<runtime::simir::ContainerRegisterId>
    lower_hir_sv_bitstream_container(semantic::ExpressionId expression,
        const runtime::simir::ContainerType& type);
    // `before_write` runs once the indices and value are captured, just
    // before the update is scheduled (an intra-assignment control).
    [[nodiscard]] bool lower_hir_sv_value_path_nonblocking_write(
        const HirSvValuePath& path, RegisterId value,
        const std::function<bool()>& before_write = { });
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
        const HirStaticContainerSelection& selection,
        std::vector<runtime::simir::InstructionIndex>* skip_branches
        = nullptr);
    [[nodiscard]] std::optional<runtime::simir::RegisterId>
    lower_hir_class_handle_array_index(
        std::string_view identity,
        const std::vector<semantic::ExpressionId>& indices,
        runtime::simir::RegisterId& validity,
        bool& applies);
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
    // With `declared_target`, the result initializes that container (a
    // local's declaration initializer) instead of the target expression.
    [[nodiscard]] bool lower_hir_container_locator_assignment(
        semantic::ExpressionId target,
        semantic::ExpressionId value,
        const HirContainerObjectBinding* declared_target = nullptr);
    [[nodiscard]] std::optional<runtime::simir::ContainerRegisterId>
    lower_hir_static_container_actual(
        semantic::ExpressionId expression,
        const runtime::simir::ContainerType& formal_type,
        semantic::DeclarationId contextual_declaration = { });
    // With `nonblocking`, a selection of a module array publishes each
    // selected element in the NBA region (IEEE 1800-2017 10.4.2).
    [[nodiscard]] bool lower_hir_container_copy_out(
        semantic::ExpressionId target,
        runtime::simir::ContainerRegisterId source,
        bool nonblocking = false);
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
    // Emit the scalar subtype check that belongs to an input/inout VHDL
    // callable formal. Dynamic bounds are lowered in the callable body after
    // all formal registers have been bound.
    [[nodiscard]] std::optional<std::uint32_t>
    hir_vhdl_lexical_enclosing_callable(
        semantic::ScopeId declaration_scope) const;
    [[nodiscard]] bool emit_hir_callable_scalar_range_check(
        semantic::DeclarationId formal,
        semantic::ScopeId callable_scope,
        RegisterId value);
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
    // Points a scan target at a writable string or packed binding; false
    // when the binding is read-only.
    [[nodiscard]] bool set_hir_string_scan_target(
        InputScanTarget& target, const HirStringBinding& binding) const;
    [[nodiscard]] bool set_hir_packed_scan_target(
        InputScanTarget& target, const HirRuntimeBinding& binding) const;
    [[nodiscard]] std::optional<HirStringBinding> hir_string_binding(
        semantic::DeclarationId declaration,
        semantic::ScopeId process_scope,
        bool require_storage) const;
    [[nodiscard]] std::optional<HirStringBinding> hir_hierarchical_string_binding(
        semantic::ExpressionId expression) const;
    // Continuous SV concatenation copyout accepts static packed signal leaves
    // and stages them in source order; other callers retain blocking copyout.
    enum class HirPackedCopyOutMode : std::uint8_t {
        blocking,
        systemverilog_active_update,
        // A nonblocking assignment to a concatenation (10.4.2).
        systemverilog_nonblocking_update,
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
        bool event { };
        // The property's type in the accessed specialization.
        semantic::sv::TypeReference type;
        // A class parameter of a parameterized specialization (a localparam
        // or enum literal) folds from its initializer there (8.25).
        std::optional<semantic::ExpressionId> constant;
        std::optional<std::uint32_t> constant_specialization;
    };
    [[nodiscard]] std::optional<HirClassPropertyProfile>
    hir_class_property_profile(
        semantic::ExpressionId expression,
        bool string_property = false) const;
    // A member of a packed structure class property (`c.p.a`, IEEE
    // 1800-2017 7.2, 8.5): the property access and the member's position.
    struct HirClassPropertyMember {
        semantic::ExpressionId property;
        HirClassPropertyProfile profile;
        HirPackedMemberSelection member;
    };
    [[nodiscard]] std::optional<HirClassPropertyMember>
    hir_class_property_member(semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<RegisterId> lower_hir_class_property_member(
        const HirClassPropertyMember& member);
    [[nodiscard]] bool lower_hir_class_property_member_write(
        const HirClassPropertyMember& member, RegisterId value);
    // A string class property read into a string register, or written from
    // one, through the class method boundary.
    [[nodiscard]] std::optional<StringRegisterId>
    lower_hir_class_string_property_read(semantic::ExpressionId expression);
    // The signal a plain event-control name denotes: a local signal, or an
    // imported package variable.
    [[nodiscard]] std::optional<SignalId> hir_named_signal(
        const std::string& name, semantic::ScopeId scope,
        std::uint64_t source_order = 0U);
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
        // The method returns a string, carried in a hidden string actual.
        bool string_result { };
        // The method returns an unpacked array or queue (IEEE 1800-2017
        // 13.4.1); only a lowered subroutine carries it.
        bool container_result { };
        // The parameterized class specialization the call runs in.
        std::optional<std::uint32_t> specialization;
    };
    [[nodiscard]] std::optional<std::vector<
        runtime::SystemVerilogConstraintTemplate>>
    // `runtime_values`, when given, receives each calling-scope name read
    // at the call; its template carries a placeholder constant operand
    // that the runtime fills from the matching `@inline-value:` actual
    // (IEEE 1800-2017 18.7).
    hir_inline_constraints(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope,
        std::vector<semantic::ExpressionId>* runtime_values
        = nullptr,
        std::vector<semantic::ExpressionId>* runtime_containers
        = nullptr) const;
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
        std::size_t expected_width,
        std::optional<StringRegisterId>* string_result = nullptr);
    struct HirClassTaskProfile {
        semantic::DeclarationId declaration;
        std::string identity;
        std::optional<semantic::ExpressionId> receiver;
        std::optional<std::string> interface_receiver;
        std::vector<HirClassMethodActual> actuals;
        bool static_method { };
        bool automatic { true };
        // The parameterized class specialization the call runs in.
        std::optional<std::uint32_t> specialization;
    };
    // The actuals of a native UVM database call (`@uvm-config-db-get:T`,
    // `@uvm-resource-db-read_by_name:T`), each of its own type; a read
    // copies its value actual out.
    [[nodiscard]] std::optional<std::vector<HirClassMethodActual>>
    hir_uvm_database_actuals(std::string_view identity,
        std::span<const std::optional<semantic::ExpressionId>> expressions,
        semantic::ScopeId process_scope) const;
    // Call actuals bound to a class method's formals by position or name,
    // with each unbound formal's default (13.5.3).
    [[nodiscard]] std::optional<std::vector<HirClassMethodActual>>
    bind_hir_class_actuals(semantic::DeclarationId callable,
        std::span<const semantic::sv::TaskAssociation> associations,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] std::optional<HirClassTaskProfile>
    hir_class_task_profile(
        semantic::StatementId statement,
        semantic::ScopeId process_scope) const;
    [[nodiscard]] bool lower_hir_class_task_call(
        semantic::StatementId statement);
    [[nodiscard]] bool lower_hir_class_task_invocation(
        semantic::StatementId statement,
        const HirClassTaskProfile& profile,
        std::optional<RegisterId> receiver);
    // The value of a lowered class function call (none for a task).
    struct HirClassCallResult {
        std::optional<RegisterId> packed;
        std::optional<StringRegisterId> string;
        std::optional<HirStaticContainerValue> container;
    };
    // A class function call whose result is a container value.
    [[nodiscard]] std::optional<HirStaticContainerValue>
    lower_hir_class_container_call(semantic::ExpressionId expression);
    // Invoke a class task or function body as a deferred SimIR subroutine
    // with `receiver` as `this`.
    [[nodiscard]] std::optional<HirClassCallResult>
    lower_hir_class_callable_invocation(
        semantic::SourceSpanId source,
        const HirClassTaskProfile& profile,
        std::optional<RegisterId> receiver,
        bool function);
    // The overrides of a virtual method in classes derived from its owner,
    // most derived first (IEEE 1800-2017 8.20); empty for a non-virtual
    // method.
    struct HirClassOverride {
        std::string owner_identity;
        semantic::DeclarationId declaration;
        std::string identity;
    };
    [[nodiscard]] std::vector<HirClassOverride> hir_class_method_overrides(
        semantic::DeclarationId declaration) const;
    // A non-static class function call lowered as a subroutine, dispatched
    // on the receiver's class; none when the call needs the runtime path.
    [[nodiscard]] std::optional<HirClassCallResult>
    lower_hir_class_function_call(
        semantic::ExpressionId expression,
        const HirClassMethodProfile& profile,
        std::optional<RegisterId> receiver);
    // Undo a partly lowered call: drop its operations and the call and
    // frame-push sites recorded past `mark`, and unqueue frames created
    // since `frames`.
    void rollback_hir_lowering(std::size_t mark, std::size_t frames);
    // A class outside the UVM library whose construction can be lowered
    // (8.7): its bases are too, and a parameterized class or base is
    // constructed in `specialization` (8.25).
    [[nodiscard]] const semantic::sv::ClassDeclaration*
    hir_lowerable_constructor_class(std::string_view identity,
        std::optional<std::uint32_t> specialization = std::nullopt) const;
    [[nodiscard]] bool hir_class_derives_from_uvm(
        std::string_view identity) const;
    // Run the constructor of `declaration` on `receiver` with the call
    // actuals: the explicit constructor body, or the implicit base
    // construction and property initializers.
    [[nodiscard]] bool lower_hir_class_construction(
        const semantic::sv::ClassDeclaration& declaration,
        RegisterId receiver,
        std::span<const semantic::sv::TaskAssociation> actuals,
        semantic::SourceSpanId source,
        std::optional<std::uint32_t> specialization = std::nullopt);
    // The specialization of the base of the class whose constructor the
    // active frame runs.
    [[nodiscard]] std::optional<std::uint32_t>
    hir_constructor_base_specialization() const;
    // The property initializers of `declaration` other than literals, which
    // the object's descriptor holds (8.7).
    [[nodiscard]] bool lower_hir_class_property_initializers(
        const semantic::sv::ClassDeclaration& declaration,
        RegisterId receiver);
    // Before a constructor body without `super.new(...)`: the base class's
    // implicit construction and this class's property initializers.
    [[nodiscard]] bool lower_hir_constructor_prologue(
        semantic::DeclarationId callable,
        std::span<const semantic::StatementId> statements);
    // `super.new(...)` in a lowered constructor body.
    [[nodiscard]] bool lower_hir_base_constructor_call(
        semantic::StatementId statement);
    // Value-container properties of `this` cached in container registers
    // while a class function or constructor body is lowered (none for a
    // task, which may suspend).
    struct HirClassContainerCache {
        ContainerRegisterId value { };
        std::string identity;
        semantic::DeclarationId declaration;
    };
    std::vector<HirClassContainerCache> hir_class_container_caches_;
    // Cache the container properties of the class owning `callable` for a
    // body that runs on `receiver`.
    void cache_hir_class_container_properties(
        semantic::DeclarationId callable, RegisterId receiver);
    // A value-container property (`obj.v`) materialized in a container
    // register: the cache of `this`, or a copy read from `receiver` that
    // store_hir_class_value_container writes back after a change.
    struct HirClassContainerAccess {
        HirStaticContainerValue container;
        std::optional<RegisterId> receiver;
        std::string identity;
        semantic::DeclarationId declaration;
    };
    [[nodiscard]] std::optional<ContainerType> hir_class_value_container_type(
        std::string_view identity) const;
    // A value-container property of an object: `@sv-container-property:<id>`
    // of a handle, or a structure property held as a value container
    // (`obj.s`, IEEE 1800-2017 7.2, 8.4).
    struct HirClassContainerPlace {
        semantic::ExpressionId receiver;
        std::string identity;
    };
    [[nodiscard]] std::optional<HirClassContainerPlace>
    hir_class_container_place(semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<HirClassContainerAccess>
    lower_hir_class_value_container(
        semantic::ExpressionId receiver, std::string_view identity);
    // The value-container type of an instance property that is a queue,
    // dynamic, associative or one-dimensional fixed array of class handles;
    // its handles stay in the class heap and each access copies them.
    [[nodiscard]] std::optional<ContainerType> hir_class_handle_container_type(
        std::string_view identity) const;
    [[nodiscard]] std::optional<HirClassContainerAccess>
    lower_hir_class_handle_container(
        semantic::ExpressionId receiver, std::string_view identity);
    void store_hir_class_value_container(const HirClassContainerAccess& access);
    // lower_hir_statement without the cache store and reload around a
    // statement that can suspend.
    [[nodiscard]] bool lower_hir_statement_unsuspended(
        semantic::StatementId statement);
    // Store the cached properties back to the object, and reload them.
    void flush_hir_class_container_caches();
    void reload_hir_class_container_caches();
    // Class function calls are lowered as subroutines unless a retry of the
    // process turned this off; the flag below records that one was.
    bool lower_class_functions_ { true };
    bool class_functions_lowered_ { };
    // Class functions whose bodies failed to lower in this process; a
    // retry leaves only calls to them (or to a dispatch reaching them) to
    // the runtime.
    std::unordered_set<std::uint32_t> hir_failed_class_functions_;
    std::unordered_set<std::uint32_t> hir_interpreted_class_functions_;
    [[nodiscard]] bool hir_class_function_interpreted(
        semantic::DeclarationId declaration) const;
    // Whether `declaration` is a class function other than a constructor.
    [[nodiscard]] bool hir_class_function_declaration(
        semantic::DeclarationId declaration) const;
    [[nodiscard]] std::optional<Process> lower_hir_process_body_once(
        const HirProcessDescription& description,
        frontend::Language language,
        std::string_view hierarchy);
    struct HirInterfaceFunctionProfile {
        semantic::DeclarationId declaration;
        std::string receiver;
        // The lexical scope the receiver name is written in.
        semantic::ScopeId scope;
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
    // The default value of a VHDL record, array or REAL subtype (IEEE
    // 1076-2008 6.4.2.3); other scalars return nullopt unless `scalar`.
    [[nodiscard]] std::optional<PackedLogic4> hir_vhdl_default_value(
        const semantic::vhdl::SubtypeIndication& subtype, std::size_t width,
        std::size_t depth = 0U, bool scalar = false) const;
    // The bounds of a REAL subtype (IEEE 1076-2008 5.2.5), lowest first:
    // its range constraint when the bounds fold, otherwise REAL's range.
    // Not a REAL subtype or unfoldable bounds return nullopt.
    [[nodiscard]] std::optional<std::pair<double, double>>
    hir_vhdl_real_range(const semantic::vhdl::SubtypeIndication& subtype) const;
    [[nodiscard]] std::optional<double> hir_vhdl_constant_real(
        semantic::ExpressionId expression, std::size_t depth = 0U) const;
    // VHDL ordering of one-dimensional discrete arrays of equal length
    // (IEEE 1076-2008 9.2.3), lexicographic from the left element.
    [[nodiscard]] std::optional<RegisterId> lower_hir_vhdl_array_ordering(
        const semantic::vhdl::Expression& source);
    [[nodiscard]] std::optional<HirCallableType> hir_callable_type(
        semantic::DeclarationId declaration,
        std::size_t contextual_width = 0U,
        bool allow_null_vhdl_array = false) const;
    [[nodiscard]] std::optional<HirCallableResolution>
    resolve_hir_vhdl_function_call(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope,
        std::size_t contextual_width) const;
    [[nodiscard]] std::optional<HirCallableResolution>
    resolve_hir_vhdl_procedure_call(
        semantic::StatementId statement,
        semantic::ScopeId process_scope) const;
    // The unspecified formal types (VHDL-2019 6.5.2) an overload candidate
    // infers from its actuals; one identity per inference name.
    struct HirVhdlOverloadInference {
        bool uses_unspecified_type { };
        std::unordered_map<std::string, std::string> actual_types;
    };
    // Whether `actual` may be associated with `formal` of the overload
    // candidate `resolution` (IEEE 1076-2008 4.5.3, 12.5).
    [[nodiscard]] bool hir_vhdl_actual_matches_formal(
        const HirCallableResolution& resolution,
        semantic::DeclarationId formal,
        semantic::ScopeId callable_scope,
        semantic::ExpressionId actual,
        semantic::ScopeId process_scope,
        HirVhdlOverloadInference& inference) const;
    // Whether the call's actuals match the parameter profile of `callable`
    // (IEEE 1076-2008 4.5.3); an implicitly declared operation of the same
    // designator remains visible beside a user overload (12.3).
    [[nodiscard]] bool hir_vhdl_overload_profile_applies(
        semantic::ExpressionId expression,
        semantic::DeclarationId callable) const;
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
        frontend::SystemVerilogScalarKind scalar {
            frontend::SystemVerilogScalarKind::None
        };
        // An unpacked open-array formal (IEEE 1800-2023 35.5.6.1): the
        // actual's fixed array type; `width` is all its elements' bits,
        // leftmost element most significant.
        std::optional<runtime::simir::ContainerType> open_array;
    };
    struct HirDpiFunctionProfile {
        std::string linkage_name;
        // The scope of the import's declaration.
        semantic::ScopeId owner_scope;
        std::vector<HirDpiFunctionActual> actuals;
        std::size_t result_width { };
        frontend::ValueDomain result_domain {
            frontend::ValueDomain::Unknown
        };
        bool result_signed { };
        frontend::SystemVerilogScalarKind result_scalar {
            frontend::SystemVerilogScalarKind::None
        };
        // A packed bit-array result of at most 32 bits returns as a C
        // svBitVecVal, 32 bits wide (IEEE 1800-2023 H.7.4).
        std::optional<std::size_t> result_c_width;
        // A string result (`result_domain` String): it returns in a
        // string register, and the destination is a placeholder.
        bool result_string { };
        // A task or void function: the destination is a placeholder.
        bool result_void { };
        bool task { };
        // The C signature the foreign service calls through: argument
        // codes, `:`, the result code, and `@c` for a context import.
        std::string signature;
    };
    // The DPI import a call names, in the unit or its compilation unit.
    [[nodiscard]] const semantic::sv::DpiDeclaration* hir_dpi_import(
        std::string_view name,
        std::optional<semantic::ScopeId> scope = std::nullopt) const;
    [[nodiscard]] std::optional<HirDpiFunctionProfile> hir_dpi_profile(
        const semantic::sv::DpiDeclaration& declaration,
        std::span<const semantic::ExpressionId> actuals) const;
    [[nodiscard]] std::optional<HirDpiFunctionProfile>
    hir_dpi_function_profile(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope) const;
    // Emit the call boundary of a DPI import; the destination register.
    // A string result is written to `string_result`.
    // The hidden task that serves a context import's DPI export calls in
    // this unit, if it has exports.
    [[nodiscard]] const semantic::sv::Declaration* hir_dpi_export_hook(
        semantic::ScopeId scope) const;
    bool lowering_dpi_export_hook_ { };
    [[nodiscard]] std::optional<RegisterId> emit_hir_dpi_call(
        const HirDpiFunctionProfile& profile,
        const frontend::SourceSpan& span,
        std::optional<StringRegisterId> string_result = std::nullopt);
    // A call of a DPI import whose result is a string (IEEE 1800-2023
    // 35.5.5, H.7.4).
    [[nodiscard]] std::optional<StringRegisterId> lower_hir_dpi_string_call(
        semantic::ExpressionId expression);
    // A task-call statement naming a DPI task or function; nullopt when
    // the statement names no DPI import.
    [[nodiscard]] std::optional<bool> lower_hir_dpi_call_statement(
        std::string_view name,
        std::span<const semantic::sv::TaskAssociation> arguments,
        const frontend::SourceSpan& span,
        semantic::ScopeId scope);
    [[nodiscard]] bool can_lower_hir_dpi_function_call(
        semantic::ExpressionId expression,
        semantic::ScopeId process_scope,
        std::unordered_set<std::uint32_t>& visiting) const;
    [[nodiscard]] std::optional<RegisterId>
    lower_hir_dpi_function_call(
        semantic::ExpressionId expression,
        std::size_t expected_width);
    // A call of a system task or function registered by a foreign (VPI)
    // library. The call boundary names its site and arguments; a task's
    // result bit requests that the simulation finish.
    [[nodiscard]] const UserSystemTask* hir_user_system_task(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<RegisterId> lower_hir_user_system_call(
        const UserSystemTask& task,
        std::span<const std::optional<semantic::ExpressionId>> arguments,
        const frontend::SourceSpan& span);
    // The generated concurrent-property monitor calls of an assertion
    // process (runtime::simir::SvaOperation).
    [[nodiscard]] std::optional<RegisterId> lower_hir_sva_call(
        const semantic::sv::Expression& call, std::size_t expected_width);
    [[nodiscard]] std::optional<RegisterId> lower_hir_function_call(
        semantic::ExpressionId expression,
        std::size_t expected_width);
    struct HirFunctionCallValue {
        std::optional<RegisterId> packed;
        std::optional<HirStaticContainerValue> container;
    };
    struct HirVhdlFunctionActualShapes {
        std::vector<std::pair<std::size_t, std::int64_t>>
            static_integer_actuals;
        std::vector<std::pair<std::size_t, semantic::ExpressionId>>
            static_integer_actual_expressions;
        std::vector<std::pair<std::size_t,
            std::vector<std::optional<HirPackedRange>>>>
            ranges;
        std::vector<std::pair<std::size_t,
            std::vector<semantic::vhdl::ElementConstraint>>>
            element_constraints;
    };
    [[nodiscard]] HirVhdlFunctionActualShapes hir_vhdl_function_actual_shapes(
        semantic::DeclarationId body,
        const std::vector<semantic::DeclarationId>& formals,
        const std::vector<semantic::ExpressionId>& actuals);
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
    // 'EVENT of a statically indexed element of `signal`, given the
    // signal's own 'EVENT register.
    // Typed VHDL file READ/WRITE of one element through its text image.
    enum class HirVhdlFileImageResult : std::uint8_t {
        lowered,
        unsupported,
        failed,
    };
    [[nodiscard]] HirVhdlFileImageResult lower_hir_vhdl_file_image(
        bool write, RegisterId handle,
        const semantic::vhdl::SubtypeIndication& element,
        semantic::ExpressionId value,
        std::optional<semantic::ExpressionId> length,
        const frontend::SourceSpan& span);
    [[nodiscard]] std::optional<RegisterId> lower_hir_vhdl_element_event(
        semantic::ExpressionId element, SignalId signal,
        RegisterId signal_event, const frontend::SourceSpan& span,
        bool report_failure = true);
    // A local VHDL constant of a multi-bit logic vector subtype, held in
    // process-local storage like a variable so it can be indexed.
    [[nodiscard]] bool hir_vhdl_local_vector_constant(
        const semantic::vhdl::Declaration& declaration) const;
    // The static text of E'SIMPLE_NAME, E'PATH_NAME or E'INSTANCE_NAME for
    // a named entity E in the current instance (IEEE 1076-2008 16.2.5).
    [[nodiscard]] std::optional<std::string> hir_vhdl_path_attribute(
        semantic::ExpressionId expression) const;
    // Whether a VHDL expression is a CHARACTER value (a character literal or
    // an object of type CHARACTER).
    [[nodiscard]] bool hir_vhdl_character_expression(
        semantic::ExpressionId value) const;
    // A non-empty aggregate of positional CHARACTER elements.
    [[nodiscard]] bool hir_vhdl_positional_character_aggregate(
        const semantic::vhdl::Expression& aggregate) const;
    // Whether a VHDL expression is of type TIME (or DELAY_LENGTH), including
    // folded time literals and NOW.
    [[nodiscard]] bool hir_vhdl_time_expression(
        semantic::ExpressionId value) const;
    // Append the runtime text of a TIME value written in the unit whose
    // index in fs..hr is unit_index (fs for 'IMAGE) to `target`.
    [[nodiscard]] bool append_hir_vhdl_time_text(StringRegisterId target,
        semantic::ExpressionId value, std::uint32_t unit_index);
    [[nodiscard]] std::optional<StringRegisterId> lower_hir_vhdl_runtime_image(
        semantic::ExpressionId value,
        std::optional<semantic::ExpressionId> type_prefix,
        runtime::simir::OutputFormat vector_format,
        bool image_quotes);
    // The image of a lowered scalar or bit/character vector value.
    [[nodiscard]] std::optional<StringRegisterId> lower_hir_vhdl_scalar_image(
        RegisterId value, std::size_t width,
        const semantic::vhdl::TypeDefinition* definition, bool builtin_array,
        std::string_view simple_type, bool real_value,
        runtime::simir::OutputFormat vector_format, bool image_quotes,
        const frontend::SourceSpan& span);
    // The string representation of a lowered value of `subtype`, including
    // a composite one: `(e1,e2,...)` (IEEE 1076-2019 5.7).
    [[nodiscard]] std::optional<StringRegisterId> lower_hir_vhdl_value_image(
        RegisterId value, const semantic::vhdl::SubtypeIndication& subtype,
        runtime::simir::OutputFormat vector_format,
        const frontend::SourceSpan& span);
    // Whether values of the type have the parenthesized composite string
    // representation rather than a character string (IEEE 1076-2019 5.7).
    [[nodiscard]] bool hir_vhdl_parenthesized_image_type(
        const semantic::vhdl::TypeDefinition& definition) const;
    [[nodiscard]] std::optional<std::vector<semantic::ExpressionId>>
    bind_hir_vhdl_procedure_actuals(
        semantic::StatementId statement,
        semantic::DeclarationId declaration) const;
    // Whether no actual of known width differs from the width of its
    // constrained array formal (IEEE 1076-2008 4.2.2.1, 14.6).
    [[nodiscard]] bool hir_vhdl_actual_lengths_fit(
        semantic::DeclarationId callable,
        std::span<const semantic::ExpressionId> actuals,
        semantic::ScopeId process_scope) const;
    /// The STD.ENV spelling (`std.env.stop`) of a simple procedure name
    /// that a use clause imports from STD.ENV and no declaration overloads.
    // The STD.ENV operation an expression names, including a simple name
    // made visible by `use std.env.all` (IEEE 1076-2019 16.5).
    // Both operands of a VHDL `=` or `/=` are STRING; FILE_LINE takes its
    // STRING overload beside a STRING operand (IEEE 1076-2019 16.5).
    [[nodiscard]] bool hir_vhdl_string_comparison_operands(
        semantic::ExpressionId left, semantic::ExpressionId right) const;
    [[nodiscard]] frontend::VhdlSimulatorApi hir_vhdl_simulator_api(
        const semantic::vhdl::Expression& source) const;
    [[nodiscard]] std::string hir_vhdl_std_env_procedure_spelling(
        const semantic::vhdl::Name& name,
        semantic::ScopeId use_scope) const;
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
        semantic::ExpressionId expression,
        bool allow_array_elements = false) const;
    [[nodiscard]] bool lower_hir_procedural_continuous_assignment(
        semantic::StatementId statement);
    [[nodiscard]] bool lower_hir_procedural_force(
        semantic::StatementId statement);
    // Whether an assignment update (`a[i] += v`, `a[i]++`), whose value
    // already reads its target, would evaluate a side effect of the target
    // twice (IEEE 1800-2017 11.4.1).
    [[nodiscard]] bool hir_update_reevaluates_target(
        const semantic::sv::Statement& statement) const;
    // hir_update_reevaluates_target's last answer: assignment lowering
    // asks it repeatedly for one statement.
    struct HirReevaluationCache {
        const semantic::sv::Statement* statement { };
        const semantic::SpecializedHirUnit* unit { };
        bool reevaluates { };
    };
    mutable HirReevaluationCache hir_reevaluation_cache_;
    // Index expressions of an update target with side effects
    // (`a[i++] += 2`), evaluated once before the update; lowering reads
    // these registers instead of evaluating them again (IEEE 1800-2017
    // 11.4.1).
    std::unordered_map<std::uint32_t, RegisterId> hir_captured_update_indices_;
    // Bound `with`-clause iterators over signed elements: such an iterator
    // has its element's signedness (IEEE 1800-2017 7.12).
    std::unordered_set<std::uint32_t> hir_captured_signed_;
    // Bound `with`-clause iterators over string elements (IEEE 1800-2017
    // 7.12.1).
    std::unordered_map<std::uint32_t, StringRegisterId> hir_captured_strings_;
    // Bound `with`-clause iterators over the rows of a multidimensional
    // array: each iterator is a row (IEEE 1800-2023 7.12.3).
    std::unordered_map<std::uint32_t, HirStaticContainerValue>
        hir_captured_containers_;
    // Whether every side effect of the current update target is captured.
    bool hir_update_indices_captured_ { };
    // The declaration an assignment or initializer writes, so that `new`
    // allocates that declaration's class specialization (IEEE 1800-2017
    // 8.25, 8.7).
    std::optional<semantic::DeclarationId> hir_allocation_target_;
    [[nodiscard]] std::optional<std::string>
    hir_allocation_target_specialization(
        std::string_view class_identity);
    // The design's class specializations, computed on first use.
    [[nodiscard]] semantic::sv::ClassSpecializationIndex&
    class_specializations() const;
    // The specialization of class `owner_identity` that code lowered in
    // the active callable sees: the frame's own specialization or one of
    // its bases (IEEE 1800-2017 8.25.1).
    [[nodiscard]] std::optional<std::uint32_t>
    hir_context_class_specialization(
        std::string_view owner_identity) const;
    // The parameterized specialization a method call runs in: the class a
    // static selection names, the receiver's declared class type, or the
    // calling method's own specialization; none when no class parameter
    // is bound along the owner's chain.
    // The class specialization a declaration's class-handle type selects:
    // its constant actuals, or actuals bound in the active specialization.
    [[nodiscard]] std::optional<std::uint32_t>
    hir_declaration_class_specialization(
        semantic::DeclarationId declaration) const;
    // The same for an access to class member `member`. `selected`, when
    // given, receives the specialization the access selects before the
    // member's declaring class is sought in its base chain.
    [[nodiscard]] std::optional<std::uint32_t>
    hir_class_member_specialization(semantic::ExpressionId access,
        semantic::DeclarationId member,
        std::optional<semantic::ExpressionId> receiver,
        std::optional<std::uint32_t>* selected = nullptr) const;
    [[nodiscard]] std::optional<std::uint32_t>
    hir_class_call_specialization(semantic::ExpressionId call,
        const semantic::sv::ClassMethod& method,
        std::optional<semantic::ExpressionId> receiver) const;
    // `T::f(...)` and `T::Alias::f(...)`: a static method selected through
    // a class type parameter or typedef (IEEE 1800-2017 8.25.1, 8.23).
    struct HirScopedClassMethod {
        const semantic::sv::ClassMethod* method { };
        std::optional<std::uint32_t> specialization;
    };
    [[nodiscard]] std::optional<HirScopedClassMethod>
    hir_scoped_class_method(const semantic::sv::Expression& call) const;
    // The class specialization `T::...::m` selects member `m` of, with the
    // leading name a class type parameter or typedef; `leaf` is `m`.
    [[nodiscard]] std::optional<std::uint32_t> hir_scoped_class_owner(
        std::string_view text, semantic::ScopeId scope,
        semantic::SourceSpanId source, std::string_view& leaf) const;
    [[nodiscard]] bool hir_update_expression_pure(
        semantic::ExpressionId expression, std::size_t depth = 0U) const;
    // Evaluate the impure indices of an update target once; false when a
    // side effect is outside an index.
    [[nodiscard]] bool capture_hir_update_target_indices(
        const semantic::sv::Statement& statement);
    // `%v` (IEEE 1800-2017 21.2.1.5): the strength text of a named net or
    // variable.
    [[nodiscard]] std::optional<StringRegisterId> lower_hir_strength_text(
        semantic::ExpressionId expression);
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
    // The packed member `path` of a structure parameter value (7.2.1).
    [[nodiscard]] std::optional<HirHierarchicalParameter>
    hir_parameter_member_slice(const HirHierarchicalParameter& whole,
        std::string_view owner_instance, semantic::UnitId unit,
        std::string_view formal, std::string_view path) const;
    // The signal an external name denotes (IEEE 1076-2008 8.7), checked
    // against the name's subtype; a mismatch is reported.
    [[nodiscard]] std::optional<SignalId> hir_vhdl_external_signal(
        semantic::ExpressionId expression);
    // The signal an external name's path reaches, unchecked.
    [[nodiscard]] std::optional<SignalId> hir_vhdl_external_signal_lookup(
        semantic::ExpressionId expression) const;
    // The subtype of the object an external name denotes: its declared
    // subtype, completed from the object (IEEE 1076-2008 8.7).
    [[nodiscard]] std::optional<semantic::vhdl::SubtypeIndication>
    hir_vhdl_external_subtype(semantic::ExpressionId expression) const;
    // A member of a packed structure signal named hierarchically.
    struct HirHierarchicalSignalMember {
        SignalId signal { };
        std::uint64_t offset { };
        std::uint64_t width { };
        bool signed_value { };
    };
    [[nodiscard]] bool hir_is_interface_hierarchical_path(
        std::string_view name) const;
    [[nodiscard]] bool hir_interface_path_has_lexical_root_binding(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<HirHierarchicalSignalMember>
    hir_hierarchical_signal_member(semantic::ExpressionId expression) const;
    // A parameter or local parameter of a selected, non-loop generate block
    // of this unit named by its block labels (`blk.P`, IEEE 1800-2017 27.6).
    [[nodiscard]] std::optional<HirHierarchicalParameter>
    hir_generate_block_parameter(std::string_view name) const;
    [[nodiscard]] std::optional<HirHierarchicalParameter>
    hir_hierarchical_parameter(semantic::ExpressionId expression,
        std::string_view published_prefix = { }) const;
    [[nodiscard]] std::optional<SignalId> hir_monitor_expression_signal(
        semantic::ExpressionId expression);
    // Automatic variables, strings and containers that an event control or
    // wait statement waits on (IEEE 1800-2017 9.4.2, 9.4.3) are not
    // signals, so each such object has a notification signal that every
    // change toggles and every such wait includes.
    [[nodiscard]] bool hir_local_event_waited(
        semantic::DeclarationId declaration) const;
    [[nodiscard]] bool hir_change_notified_storage(
        semantic::DeclarationId declaration) const;
    [[nodiscard]] std::optional<std::pair<semantic::DeclarationId,
        StringRegisterId>>
    hir_local_string_event(std::string_view name, semantic::ScopeId scope,
        std::uint64_t source_order) const;
    [[nodiscard]] std::optional<SignalId> hir_change_notifier(
        std::string_view name, semantic::ScopeId scope,
        std::uint64_t source_order);
    struct HirChangeWatch {
        SignalId notifier { };
        semantic::DeclarationId declaration;
        std::optional<RegisterId> old_packed;
        std::optional<StringRegisterId> old_string;
    };
    [[nodiscard]] std::optional<HirChangeWatch>
    prepare_hir_change_notification(semantic::ExpressionId target);
    void finish_hir_change_notification(const HirChangeWatch& watch);
    // The clocking event a lone `@cb` or `@vif.cb` names (IEEE 1800-2017
    // 14.3, 25.9), or nullopt.
    [[nodiscard]] std::optional<std::vector<semantic::sv::Sensitivity>>
    hir_named_clocking_event(
        std::span<const semantic::sv::Sensitivity> sensitivities,
        semantic::ScopeId scope) const;
    // Suspends until one of `sensitivities` occurs (IEEE 1800-2017 9.4.2):
    // the lowering of an event control.
    [[nodiscard]] bool lower_hir_sensitivity_wait(
        std::span<const semantic::sv::Sensitivity> sensitivities,
        std::optional<semantic::ExpressionId> wildcard_expression,
        std::span<const semantic::StatementId> wildcard_statements,
        const frontend::SourceSpan& span);
    // The handle register of an automatic or callable event named `name`
    // (or by `expression`), whose event object is created per activation
    // (IEEE 1800-2017 15.5).
    [[nodiscard]] std::optional<RegisterId> hir_systemverilog_event_handle(
        std::string_view name, semantic::ScopeId scope,
        std::optional<semantic::ExpressionId> expression = std::nullopt);
    [[nodiscard]] std::optional<SignalId> hir_local_event_notifier(
        semantic::DeclarationId declaration);
    [[nodiscard]] std::optional<std::pair<semantic::DeclarationId, RegisterId>>
    hir_local_event_variable(std::string_view name, semantic::ScopeId scope,
        std::uint64_t source_order) const;
    void hir_local_event_notifiers(semantic::ExpressionId expression,
        std::vector<SignalId>& dependencies);
    // The design-wide signal the runtime toggles whenever a class property
    // changes; waits on expressions that read class properties include it.
    [[nodiscard]] std::optional<SignalId> hir_class_heap_notifier();
    // `f(i)` read as `f()(i)` when every formal of `f` has a default and the
    // context takes one element of its array result (IEEE 1076-2008 8.4).
    [[nodiscard]] std::optional<RegisterId> lower_hir_vhdl_defaulted_call_index(
        semantic::ExpressionId expression, std::size_t expected_width);
    // Calls lowered with only their default actuals.
    std::unordered_set<std::uint32_t> hir_vhdl_defaulted_calls_;
    // A predicate locator (`find... with (p)`) evaluated element by element
    // in SimIR, the iterator bound to each element and its index (IEEE
    // 1800-2017 7.12.1); used when the bounded predicate graph cannot
    // express the predicate.
    [[nodiscard]] bool lower_hir_container_locator_loop(
        std::string_view method, runtime::simir::ContainerRegisterId source,
        const runtime::simir::ContainerType& source_type,
        runtime::simir::ContainerRegisterId destination,
        std::string_view iterator_name, semantic::ExpressionId predicate);
    // `sort`/`rsort` with a key expression (IEEE 1800-2017 7.12.2) beyond
    // the bounded key graph: an insertion sort in SimIR whose comparisons
    // evaluate the key with the iterator bound to each element.
    [[nodiscard]] bool lower_hir_container_keyed_order_loop(
        runtime::simir::ContainerRegisterId target,
        const runtime::simir::ContainerType& type, bool descending,
        std::string_view iterator_name, semantic::ExpressionId key);
    // A reduction (`sum with (e)`) accumulated element by element; the
    // result has the with expression's width (IEEE 1800-2017 7.12.3).
    [[nodiscard]] std::optional<RegisterId> lower_hir_container_reduction_loop(
        std::string_view method, runtime::simir::ContainerRegisterId source,
        const runtime::simir::ContainerType& source_type,
        std::string_view iterator_name, semantic::ExpressionId transformation);
    // The with-clause expressions naming the iterator (`true` for
    // `iterator.index`).
    [[nodiscard]] std::vector<std::pair<std::uint32_t, bool>>
    hir_container_iterator_references(semantic::ExpressionId expression,
        std::string_view iterator_name) const;
    // Whether a VHDL expression has an array subtype.
    [[nodiscard]] bool hir_vhdl_array_valued(
        semantic::ExpressionId expression) const;
    // Whether a VHDL value is signed: SIGNED, a slice or concatenation of
    // SIGNED values, or an integer (IEEE 1076-2008 8.5, 9.2.5).
    [[nodiscard]] bool hir_vhdl_signed_array(
        semantic::ExpressionId expression, std::size_t depth = 0U) const;
    // The units of a physical type with their scales in primary units, in
    // declaration order; nullopt when a scale is not static.
    [[nodiscard]] std::optional<std::vector<std::pair<std::string, std::int64_t>>>
    hir_vhdl_physical_unit_scales(
        const semantic::vhdl::TypeDefinition& type) const;
    // A class property named in an event control: `p` in a method of its
    // class, or `obj.p` through a class handle variable (IEEE 1800-2017
    // 9.4.2, 8.4).
    struct HirNamedClassProperty {
        std::string identity;
        std::optional<semantic::DeclarationId> object;
        bool static_storage { };
        std::size_t width { };
        frontend::ValueDomain domain { frontend::ValueDomain::Logic4 };
        // An `event` property: a trigger toggles its one-bit value.
        bool event { };
    };
    [[nodiscard]] std::optional<HirNamedClassProperty>
    hir_named_class_property(std::string_view name,
        semantic::ScopeId scope, std::uint64_t source_order) const;
    [[nodiscard]] std::optional<RegisterId> lower_hir_named_class_property(
        const HirNamedClassProperty& property);
    [[nodiscard]] bool lower_hir_named_class_property_write(
        const HirNamedClassProperty& property, RegisterId value);

    void validate_read_only_signal_writes(
        const frontend::SourceSpan& source);
    void emit_debug_point(
        DebugPointKind kind,
        const frontend::SourceSpan& span);
    [[nodiscard]] std::string debug_scope_name() const;
    RegisterId allocate_register(
        std::size_t width,
        frontend::ValueDomain domain);
    // A format_packed string method appending `value`, `width` bits wide,
    // to `target` in `format`; the caller sets any other options.
    // A file name operand: a string, or an integral value holding its
    // characters (IEEE 1800-2017 21.3.1, 21.4).
    [[nodiscard]] bool hir_filename_operand(
        semantic::ExpressionId expression, semantic::ScopeId scope) const;
    [[nodiscard]] std::optional<StringRegisterId> lower_hir_filename(
        semantic::ExpressionId expression);
    [[nodiscard]] StringMethod hir_format_packed(StringRegisterId target,
        RegisterId value, std::size_t width,
        runtime::simir::OutputFormat format);
    // A fresh register loaded with a constant.
    RegisterId load_constant(PackedLogic4 value, frontend::ValueDomain domain);
    RegisterId load_integer(std::int64_t value, std::size_t width,
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
    // Writes of the current process through virtual interface handles.
    std::unordered_map<SignalId, std::size_t> hir_virtual_interface_writes_;
    // VHDL objects with run-time index ranges, by declaration.
    std::unordered_map<std::uint32_t, HirVhdlShapedLocal> hir_vhdl_shaped_locals_;
    mutable std::unordered_map<std::uint32_t, HirVhdlShapedLocal>
        hir_vhdl_shaped_heap_views_;
    // The array type an aggregate with a sole non-static range choice takes
    // from its context while a concatenation is lowered (9.3.3.3).
    std::optional<HirVhdlShapedProfile> hir_vhdl_shaped_context_profile_;
    mutable std::unordered_map<std::uint32_t,
        std::optional<HirVhdlShapedProfile>> hir_vhdl_shaped_heap_profiles_;
    mutable const semantic::SpecializedHirUnit* hir_vhdl_shaped_active_unit_ { };
    mutable bool hir_vhdl_shaped_active_types_ { };
    // The subtype a VHDL function call takes from its context while it is
    // lowered, by call expression: a target that completes an open result
    // type (IEEE 1076-2019 4.2.1). An aggregate target never gives a return
    // identifier its subtype (4.2.1 b).
    struct HirVhdlCallContext {
        semantic::vhdl::SubtypeIndication subtype;
        bool aggregate_target { };
    };
    mutable std::unordered_map<std::uint32_t, HirVhdlCallContext>
        hir_vhdl_call_contexts_;
    class HirVhdlCallContextScope final {
    public:
        HirVhdlCallContextScope(const Lowerer& lowerer,
            const semantic::ExpressionId call,
            semantic::vhdl::SubtypeIndication subtype,
            const bool aggregate_target = false)
            : lowerer_ { &lowerer }
            , call_ { call.value() }
        {
            auto& contexts = lowerer_->hir_vhdl_call_contexts_;
            if (const auto found = contexts.find(call_);
                found != contexts.end()) {
                previous_ = std::move(found->second);
            }
            contexts.insert_or_assign(call_,
                HirVhdlCallContext { std::move(subtype), aggregate_target });
        }

        HirVhdlCallContextScope(const HirVhdlCallContextScope&) = delete;
        HirVhdlCallContextScope& operator=(
            const HirVhdlCallContextScope&) = delete;

        ~HirVhdlCallContextScope()
        {
            auto& contexts = lowerer_->hir_vhdl_call_contexts_;
            if (previous_) {
                contexts.insert_or_assign(call_, std::move(*previous_));
            } else {
                contexts.erase(call_);
            }
        }

    private:
        const Lowerer* lowerer_;
        std::uint32_t call_;
        std::optional<HirVhdlCallContext> previous_;
    };
    // When set, a single dynamic-range aggregate records whether its length
    // matches its context here instead of asserting it.
    std::optional<RegisterId>* hir_vhdl_aggregate_length_sink_ { };
    const SignalBindings& signals_;
    const ReadOnlySignalBindings& read_only_signals_;
    PackageConstantSignal package_constant_signal_;
    PackageContainerObject package_container_object_;
    PackageStringObject package_string_object_;
    const StringObjectBindings& string_objects_;
    const ReadOnlyStringBindings& read_only_string_objects_;
    const ContainerObjectBindings& container_objects_;
    const ReadOnlyContainerBindings& read_only_container_objects_;
    std::vector<Diagnostic>& diagnostics_;
    const semantic::SpecializedHirUnit* specialized_hir_unit_ { };
    const std::vector<const semantic::SpecializedHirUnit*>*
        ancestor_systemverilog_specializations_ { };
    const CoverageHirContext* coverage_hir_context_ { };
    bool coverage_hir_process_active_ { };
    // Set for one function call lowered as a call statement, whose result is
    // discarded and which may name a void function.
    bool discarding_call_result_ { };
    const std::unordered_map<std::string, std::uint64_t>*
        systemverilog_interface_handles_ { };
    const std::unordered_map<std::string, std::string>*
        systemverilog_interface_types_ { };
    // Each interface instance's specialization, by instance path: a
    // function or task called through the instance has its types
    // (IEEE 1800-2017 25.7).
    const HirInstanceUnits* systemverilog_instance_units_ { };
    [[nodiscard]] const semantic::SpecializedHirUnit* hir_callee_unit(
        std::string_view receiver) const;
    // Lowers in another specialization for its lifetime; a null unit keeps
    // the current one.
    class HirUnitSwap {
    public:
        HirUnitSwap(Lowerer& lowerer, const semantic::SpecializedHirUnit* unit);
        ~HirUnitSwap();
        HirUnitSwap(const HirUnitSwap&) = delete;
        HirUnitSwap& operator=(const HirUnitSwap&) = delete;

    private:
        Lowerer& lowerer_;
        const semantic::SpecializedHirUnit* saved_ { };
        bool active_ { };
    };
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
    struct HirVhdlCapturedScalarRange {
        semantic::SourceSpanId source;
        RegisterId left;
        RegisterId right;
        bool descending { };
    };
    struct HirCallableFrame {
        semantic::DeclarationId declaration;
        semantic::ScopeId scope;
        // The specialization of the interface instance whose function or
        // task this frame runs, when it is not the caller's.
        const semantic::SpecializedHirUnit* callee_unit { };
        // A function or procedure called from a process evaluates
        // 'DRIVING_VALUE in that caller process, even while its deferred body
        // is lowered in the callable's nested scope.
        std::optional<semantic::ScopeId> vhdl_driver_process_scope;
        HirCallableType type;
        RegisterId result { };
        StringRegisterId string_result { };
        std::optional<ContainerRegisterId> container_result;
        std::optional<ContainerRegisterId> container_result_default;
        std::optional<RegisterId> class_receiver;
        // The parameterized class specialization a class method body runs
        // in; its parameters are bound in generic_bindings.
        std::optional<std::uint32_t> class_specialization;
        std::optional<std::string> interface_receiver;
        // The private variables of the protected object a method frame
        // runs on; part of the frame key.
        std::vector<HirVhdlProtectedMember> vhdl_protected_members;
        // Protected-type formals and the anchors of their actual objects.
        std::unordered_map<std::uint32_t, ContainerObjectId>
            vhdl_protected_formals;
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
        // Type parameters this frame binds to class handles (8.25), by
        // name.
        std::vector<std::string> handle_type_parameters;
        // Per-dimension shape facts: a present empty range means the caller
        // did not prove that dimension and it must not use type-level bounds.
        std::unordered_map<std::uint32_t,
            std::vector<std::optional<HirPackedRange>>> vhdl_formal_ranges;
        // Element constraints an unconstrained array formal takes from its
        // actual (IEEE 1076-2008 4.2.2.2), keyed like vhdl_formal_ranges.
        std::unordered_map<std::uint32_t,
            std::vector<semantic::vhdl::ElementConstraint>>
            vhdl_formal_element_constraints;
        // The result subtype an open VHDL result type takes from the call's
        // context; part of the frame key.
        std::optional<semantic::vhdl::SubtypeIndication> vhdl_result_subtype;
        std::string debug_name;
        // Signal-class procedure formals denote their actual signal for the
        // whole call. Frames are specialized per actual signal set.
        std::unordered_map<std::uint32_t, HirRuntimeBinding> signal_formals;
        std::unordered_map<std::uint32_t, RegisterId> static_variables;
        // The registers of the body's formals and locals once lowered,
        // visible to subprograms declared inside it.
        std::unordered_map<std::uint32_t, RegisterId> body_registers;
        std::unordered_map<std::uint32_t, StringRegisterId>
            body_string_registers;
        // Scalar subtype bounds evaluated while this callable activation's
        // declarative part is elaborated, keyed by the source range.
        std::vector<HirVhdlCapturedScalarRange> body_vhdl_scalar_ranges;
        // The lexical callable frame whose locals and captured subtype ranges
        // this frame closes over. This identity is also part of the frame key.
        std::optional<std::uint32_t>
            enclosing_callable_invocation_identity;
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
        // The design-level name prefix of this class method's static
        // locals; empty for any other callable (IEEE 1800-2017 6.21, 8.25).
        std::string class_static_scope;
    };
    void inherit_enclosing_hir_frame_facts(HirCallableFrame& frame) const;
    [[nodiscard]] bool allocate_hir_static_callable_declarations(
        std::size_t frame_index);
    // The name prefix shared by the static locals of one class method in
    // one class specialization: each specialization of a parameterized
    // class has its own (IEEE 1800-2017 8.25). Empty for a callable that
    // is not a class method.
    // An unpacked structure whose value has no packed image: a class
    // property of this type is a value container (IEEE 1800-2017 7.2).
    [[nodiscard]] bool hir_systemverilog_boxed_aggregate(
        const semantic::sv::TypeReference& type) const;
    [[nodiscard]] std::string hir_class_static_scope(
        const HirCallableFrame& frame) const;
    // Creates, once per design, the signal, string or container object
    // holding the static local `declaration` of the class method running
    // in `frame_index`, so every call, loop iteration, process and
    // nonblocking assignment sees one object (IEEE 1800-2017 6.21, 13.3.2).
    // False when the local's type or initializer needs the register path.
    [[nodiscard]] bool ensure_hir_class_static_local(
        std::size_t frame_index, semantic::DeclarationId declaration);
    [[nodiscard]] std::optional<RegisterId> lower_hir_vhdl_resolution_call(
        semantic::DeclarationId function, RegisterId driving_values,
        std::size_t driver_count, std::size_t element_width);
    std::optional<std::pair<RegisterId, RegisterId>> hir_vhdl_resolver_registers_;
    std::vector<HirCallableFrame> hir_callable_frames_;
    std::vector<HirVhdlCapturedScalarRange>
        hir_vhdl_captured_scalar_ranges_;
    struct HirStaticStorage {
        std::optional<SignalId> signal;
        std::optional<StringObjectId> string;
        std::optional<ContainerObjectId> container;
    };
    // The index of the frame of the callable lexically enclosing `frame`.
    [[nodiscard]] std::optional<std::size_t> hir_enclosing_callable_frame(
        const HirCallableFrame& frame) const;
    // A static local of a static callable keeps its value between calls;
    // only its first activation runs the initializer (IEEE 1800-2017 6.21).
    [[nodiscard]] bool hir_retained_static_local(
        const semantic::CompiledDeclarationView& declaration,
        bool first_initialization) const;
    [[nodiscard]] std::optional<HirStaticStorage> hir_static_storage(
        semantic::DeclarationId declaration,
        const HirCallableFrame* frame = nullptr) const;
    [[nodiscard]] std::optional<SignalId> hir_static_variable_signal(
        semantic::DeclarationId declaration,
        const HirCallableFrame* frame = nullptr) const;
    void transfer_hir_static_formals(const HirCallableFrame& frame, bool copy_in);
    // A callable of another module instance (`u.f()`) reads that
    // instance's parameter values.
    void bind_hir_instance_parameters(HirCallableFrame& frame) const;
    std::unordered_map<std::string, std::size_t> hir_callable_indices_;
    // Distinct frames per callable body; bounds a specialization that never
    // repeats (a recursion whose actuals keep changing).
    std::unordered_map<std::uint32_t, std::size_t> hir_callable_body_frames_;
    std::unordered_map<std::string, std::size_t>
        hir_interface_callable_indices_;
    std::deque<std::size_t> pending_hir_callables_;
    std::optional<std::size_t> active_hir_callable_;
    struct HirVhdlAccessHeap {
        ContainerRegisterId objects { };
        ContainerRegisterId issued_handles { };
        std::uint64_t maximum_live_objects { };
        semantic::vhdl::SubtypeIndication designated_subtype;
        // Objects of an unconstrained array type each keep their own
        // bounds (IEEE 1076-2008 5.4.1, 9.3.7).
        std::optional<HirVhdlShapedProfile> shaped;
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
    mutable std::unordered_set<std::uint32_t>
        hir_vhdl_function_shape_frames_;
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
    // Static storage of called tasks and functions. Their bodies' reads of
    // it are not operands of the calling expression, so they add no
    // implicit sensitivity (IEEE 1800-2017 9.4.2.2, 10.3).
    mutable std::unordered_set<SignalId> hir_static_storage_signals_;
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
        // A procedural force, whose value is also continuously evaluated
        // (IEEE 1800-2017 10.6.2) under its own owner signal.
        bool force { };
    };
    // The shared owner signal of a procedural continuous assignment target:
    // zero, or the owner value of the assignment that currently drives it
    // (IEEE 1800-2017 10.6.1). Every process finds it by the target's name.
    [[nodiscard]] std::optional<SignalId> hir_procedural_assign_owner(
        semantic::ExpressionId target, bool force = false);
    std::vector<ProceduralContinuousAssignment>
        procedural_continuous_assignments_;
    struct PendingMonitorDriver {
        semantic::ExpressionId expression;
        SignalId signal { };
        semantic::ScopeId scope;
    };
    std::vector<PendingMonitorDriver> pending_monitor_drivers_;
    mutable std::optional<std::unordered_set<std::uint32_t>>
        waited_local_declarations_;
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
    // Lowers a loop body in a new loop-control context; the caller patches
    // its continue and break jumps.
    [[nodiscard]] std::optional<LoopControlContext> lower_hir_loop_body(
        std::string_view label,
        std::span<const semantic::StatementId> statements);
    // Points pending jump instructions at `target`.
    void patch_hir_jumps(
        std::span<const InstructionIndex> jumps, InstructionIndex target);
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
    const std::unordered_set<std::string>* disable_target_labels_ { };
    // The register holding a queue's last index while one of its element
    // indices is lowered; `$` reads it (IEEE 1800-2017 7.10.1).
    std::optional<runtime::simir::RegisterId> hir_queue_last_index_;
    [[nodiscard]] bool hir_index_mentions_last(
        semantic::ExpressionId expression) const;
    // Set hir_queue_last_index_ for an element index of a queue that uses
    // `$`; the caller restores the previous value.
    void bind_hir_queue_last_index(
        const HirContainerElementBinding& element);
    struct HirQueueLastIndexRestore {
        std::optional<runtime::simir::RegisterId>& slot;
        std::optional<runtime::simir::RegisterId> saved;
        ~HirQueueLastIndexRestore() { slot = saved; }
    };
    const std::unordered_set<std::string>* disable_declared_labels_ { };
    // The hierarchical name of the current lexical scope: the instance path
    // followed by the enclosing named blocks.
    [[nodiscard]] std::vector<std::string> hir_named_scope_path() const;
    // A task is named in its instance; a block within its enclosing scopes.
    void emit_disable_scope_marker(
        runtime::simir::DebugPointKind kind, std::string_view label,
        const frontend::SourceSpan& span, bool task = false);
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
    // Labels of the named blocks enclosing the statement being lowered.
    std::vector<std::string> hir_named_blocks_;
    // `library.cell` of the unit being lowered, for %l (IEEE 1800-2017
    // 21.2.1.1).
    [[nodiscard]] std::string hir_library_cell() const;
    // `%m` (IEEE 1800-2017 21.2.1.6): the instance and its enclosing named
    // blocks.
    [[nodiscard]] std::string hir_scope_hierarchy() const;
    // The instance whose code is being lowered: the callee's instance while
    // a task or function of another instance (`a.t()`) is lowered inline.
    [[nodiscard]] std::string hir_instance_path() const;
    mutable bool hierarchical_reference_used_ { };
    mutable bool hierarchical_reference_missed_ { };
    bool hierarchical_reference_retry_ { };
    // `g[1].h[2].x` as a hierarchical name when every index is constant.
    // A task or function of a module instance named by a hierarchical
    // receiver (`u.t`): its declaration and the instance path.
    [[nodiscard]] std::optional<std::pair<semantic::DeclarationId, std::string>>
    hir_instance_callable(std::string_view receiver, std::string_view member,
        bool function) const;
    [[nodiscard]] std::optional<std::string> hir_systemverilog_constant_path(
        semantic::ExpressionId expression) const;
    [[nodiscard]] std::optional<SignalId> hir_hierarchical_signal(
        std::string_view name) const;
    [[nodiscard]] bool hir_genvar_hides_signal(
        std::string_view name, semantic::ScopeId scope) const;
};

// "; rejected <construct>" when the lowerer recorded why its last process
// failed without a diagnostic of its own.
[[nodiscard]] inline std::string hir_lowering_rejection_detail(
    const Lowerer& lowerer)
{
    const auto& rejection = lowerer.hir_lowering_rejection();
    return rejection ? "; rejected " + *rejection : std::string { };
}

} // namespace fsim::elaboration
