// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "application_class_execution.hpp"
#include "application_class_hir_execution.hpp"
#include "application_internal.hpp"
#include "application_uvm_registry.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/artifact/coverage_database_codec.hpp"
#include "fsim/artifact/coverage_database_merge.hpp"
#include "fsim/runtime/run_state_sections.hpp"
#include "fsim/runtime/scheduler_events.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <limits>
#include <map>
#include <deque>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace fsim::app {
using namespace application_detail;

// The specialization `id` names exactly, or null.
[[nodiscard]] inline const semantic::design::Specialization*
exact_specialization(const semantic::design::DesignIr& design,
    const semantic::design::SpecializationId id) noexcept
{
    const auto& specializations = design.specializations();
    return id.valid() && id.value() < specializations.size()
            && specializations[id.value()].id == id
        ? &specializations[id.value()]
        : nullptr;
}

struct Simulation::Impl {
    // The hook last given to set_systemverilog_dpi_function_hook.
    SystemVerilogDpiFunctionHook dpi_function_hook;

    struct ObserverDispatchGeneration {
        std::uint64_t high { };
        std::uint64_t low { };

        [[nodiscard]] ObserverDispatchGeneration next() const
        {
            auto result = *this;
            if (result.low == std::numeric_limits<std::uint64_t>::max()) {
                if (result.high == std::numeric_limits<std::uint64_t>::max()) {
                    throw std::overflow_error {
                        "observer dispatch generation exhausted"
                    };
                }
                ++result.high;
                result.low = 0;
            } else {
                ++result.low;
            }
            return result;
        }

        friend bool operator==(
            const ObserverDispatchGeneration&,
            const ObserverDispatchGeneration&) noexcept = default;

        friend bool operator<(
            const ObserverDispatchGeneration left,
            const ObserverDispatchGeneration right) noexcept
        {
            return left.high < right.high
                || (left.high == right.high && left.low < right.low);
        }
    };

    template <typename Callback>
    struct ObserverEntry {
        std::uint64_t token { };
        Callback callback;
        ObserverDispatchGeneration visible_from;
        std::optional<ObserverDispatchGeneration> visible_through;

        [[nodiscard]] bool visible_at(
            const ObserverDispatchGeneration generation) const noexcept
        {
            return !(generation < visible_from)
                && (!visible_through || !(generation < *visible_through));
        }
    };

    template <typename Callback>
    using ObserverSnapshot
        = std::vector<std::shared_ptr<ObserverEntry<Callback>>>;

    enum class Lifecycle {
        ready,
        finished,
        poisoned,
    };

    struct PendingConcurrentAssertionAction {
        bool report { };
        bool newline { };
        runtime::simir::AssertionSeverity severity {
            runtime::simir::AssertionSeverity::error
        };
        std::string text;
        runtime::simir::SourceLocation source;
    };

    struct PendingConcurrentAssertion {
        runtime::simir::ProcessId process { };
        runtime::simir::ProcessId design_process { };
        std::uint32_t slot { };
        std::string kind;
        std::string name;
        bool vacuous { };
        std::vector<PendingConcurrentAssertionAction> actions;
    };

    [[nodiscard]] std::filesystem::path coverage_database_file(
        const std::string_view filename) const;

    void load_coverage_database(const std::filesystem::path& path);

    void save_coverage_database();

    void control_coverage_database(
        const runtime::simir::CoverageDatabaseControlEvent& event);

    [[nodiscard]] static artifact::CoverageDatabaseIdentity
    coverage_identity_from_digest(
        const support::Sha256::Digest& digest) noexcept;

    static void coverage_hash_u64(
        support::Sha256& hasher, const std::uint64_t value) noexcept;

    [[nodiscard]] static artifact::CoverageDatabaseDigest
    coverage_digest(const support::Sha256::Digest& digest) noexcept;

    [[nodiscard]] artifact::CoverageDatabaseIdentity
    standard_run_identity();

    [[nodiscard]] std::optional<artifact::CoverageDatabaseContents>
    fallback_standard_coverage_database();

    [[nodiscard]] std::optional<artifact::CoverageDatabaseContents>
    current_standard_coverage_database();

    [[nodiscard]] std::optional<artifact::CoverageDatabaseContents>
    aggregate_standard_coverage_database();

    struct StandardCoverageSelection {
        std::vector<std::string> instances;
        std::vector<runtime::CodeCoverageCounterId> counters;
        std::set<artifact::CoverageDatabaseIdentity> identities;
        std::size_t available_instances { };
    };

    [[nodiscard]] static bool standard_module_name_matches(
        const std::string_view target,
        const std::string_view selector) noexcept;

    [[nodiscard]] std::optional<StandardCoverageSelection>
    select_standard_coverage(
        const std::int32_t scope_value,
        std::string selector,
        const std::string_view instance_context,
        const bool selector_is_instance,
        const runtime::CodeCoverageMetric metric) const;

    [[nodiscard]] static std::int32_t query_standard_coverage(
        const artifact::CoverageDatabaseContents& contents,
        const bool maximum,
        const std::set<artifact::CoverageDatabaseIdentity>* identities,
        const artifact::CoverageDatabaseMetricFamily family) noexcept;

    [[nodiscard]] std::int32_t access_standard_coverage(
        const runtime::simir::CoverageAccessEvent& event) noexcept;

    [[nodiscard]] std::int32_t control_standard_coverage(
        const runtime::simir::CoverageControlEvent& event) noexcept;

    [[nodiscard]] std::optional<
        runtime::SystemVerilogVpiCoverageStatistics>
    standard_vpi_coverage_statistics(
        const fsim_vpi_handle_v1 handle) const noexcept;

    [[nodiscard]] std::int32_t control_standard_vpi_coverage(
        const runtime::SystemVerilogVpiCoverageControlRequest& request) noexcept;

    void configure_standard_vpi_coverage();

    Impl(
        BuiltProject project,
        const std::uint64_t max_deltas,
        const SimulationEngine engine,
        const SystemVerilogVpiRuntimeUpdates vpi_runtime_updates);

    void setup_execution(SimulationEngine engine);

    [[nodiscard]] std::size_t ensure_concurrent_assertion_coverage(
        const runtime::simir::ProcessId process,
        const std::uint32_t slot,
        const std::string_view kind,
        const std::string_view name);

    void finish_concurrent_assertions(
        const SimulationTick time,
        const std::uint64_t delta);

    std::vector<runtime::SystemVerilogUvmCommandReportApplication>
    apply_uvm_report_settings(
        const std::string_view phase,
        const SimulationTick time);

    void schedule_uvm_report_settings();

    /// Scheduler event of a UVM report setting with a time (#128): kind
    /// "sim.uvm_report_setting", plain data (this simulation and the time
    /// offset, which is also the cancel key). The settings are rebuilt from
    /// the command line, so a restart re-creates their events from the saved
    /// ones and `bind_restored_event_handle` gives each handle back.
    struct UvmReportSettingEvent {
        struct Payload {
            Impl* owner { };
            SimulationTick offset { };
        };
        struct Domain {
            static constexpr std::string_view name { "simulation" };
        };
        struct Codec : runtime::OwnedFieldsSchedulerEventCodec<Payload, Impl,
                           Domain, &Payload::owner, &Payload::offset> {
            static std::uint64_t cancel_key(const Payload& payload)
            {
                return payload.offset;
            }
        };
        static void dispatch(runtime::Scheduler&, const Payload&);
    };
    /// Registers this simulation's event kinds (idempotent).
    static void register_event_kinds();
    [[nodiscard]] bool bind_restored_event_handle(std::string_view kind,
        std::uint64_t key, const runtime::ScheduledTaskHandle& handle);

    void validate_external_value(
        const SignalId signal,
        const PackedLogic4& value,
        const std::string_view operation) const;

    [[nodiscard]] const semantic::sv::ClassSpecialization&
    class_specialization(const std::string_view identity) const;

    [[nodiscard]] runtime::SystemVerilogUvmRootHandle component_root(
        const std::string_view allocation_scope);

    // `names` selects the randomized properties; `@inline-value:N` entries
    // instead carry calling-scope values for the inline constraints.
    [[nodiscard]] runtime::PackedLogic4 invoke_source_randomize(
        const runtime::SystemVerilogClassHandle handle,
        const std::span<const std::string> names,
        const std::span<const runtime::PackedLogic4> actuals,
        const std::span<const runtime::SystemVerilogConstraintTemplate>
            inline_constraints);

    [[nodiscard]] runtime::PackedLogic4 invoke_source_randomization_mode(
        const runtime::SystemVerilogClassHandle handle,
        const std::string_view method,
        const std::span<const runtime::PackedLogic4> actuals);

    [[nodiscard]] static runtime::PackedLogic4 resize_packed(
        const runtime::PackedLogic4& value,
        const std::size_t width);

    [[nodiscard]] static runtime::PackedLogic4 packed_property_value(
        const runtime::SystemVerilogClassPropertyValue& value);

    // `object_specialization` names the specialization of the object that
    // holds the property; a static property has none.
    void assign_property_value(
        runtime::SystemVerilogClassPropertyValue& destination,
        const std::string_view canonical_identity,
        const runtime::PackedLogic4& value,
        std::string_view object_specialization = { });

    [[nodiscard]] runtime::PackedLogic4 invoke_class_container(
        const runtime::SystemVerilogClassHandle receiver,
        const std::string_view operation,
        const std::span<const runtime::PackedLogic4> actuals);

    [[nodiscard]] runtime::PackedLogic4 invoke_checked_class_cast(
        const runtime::SystemVerilogClassHandle handle,
        const std::string_view operation) const;

    [[nodiscard]] static std::pair<std::string_view, std::string_view>
    static_property_parts(const std::string_view identity);
    // The static storage a method of `handle`'s object uses for a static
    // property declared by `owner`: that object's specialization of the
    // owner (IEEE 1800-2017 8.25), or `owner` itself.
    [[nodiscard]] std::string static_property_specialization(
        std::string_view owner,
        runtime::SystemVerilogClassHandle handle) const;

    [[nodiscard]] runtime::PackedLogic4 invoke_source_function(
        const runtime::SystemVerilogClassHandle handle,
        const std::string_view canonical_identity,
        std::vector<runtime::PackedLogic4>& actuals,
        std::vector<std::string>& string_actuals,
        const std::span<const std::string> actual_names,
        const std::span<const std::uint8_t> actual_directions,
        const bool virtual_dispatch,
        application_detail::SystemVerilogClassHirExecution::ActualReferences
            actual_references = {},
        std::optional<runtime::simir::ProcessId> report_process
            = std::nullopt);

    [[nodiscard]] runtime::PackedLogic4 invoke_source_static_function(
        const std::string_view canonical_identity,
        std::vector<runtime::PackedLogic4>& actuals,
        std::vector<std::string>& string_actuals,
        const std::span<const std::string> actual_names,
        const std::span<const std::uint8_t> actual_directions,
        application_detail::SystemVerilogClassHirExecution::ActualReferences
            actual_references = {},
        std::optional<runtime::simir::ProcessId> report_process
            = std::nullopt);

    void invoke_source_constructor(
        const semantic::sv::ClassSpecialization& specialization,
        const runtime::SystemVerilogClassHandle handle,
        const std::span<const runtime::PackedLogic4> actuals,
        const std::span<const std::string> string_actuals,
        const std::span<const std::string> actual_names,
        std::optional<runtime::simir::ProcessId> report_process
            = std::nullopt);

    [[nodiscard]] runtime::SystemVerilogClassHandle allocate_class(
        const std::string_view specialization_identity,
        const std::string_view declared_type,
        const std::string_view allocation_scope = "$api");

    [[nodiscard]] runtime::SystemVerilogClassHandle construct_class(
        const std::string_view specialization_identity,
        const std::string_view declared_type,
        const std::span<const runtime::PackedLogic4> actuals,
        const std::span<const std::string> string_actuals,
        const std::span<const std::string> actual_names,
        const std::string_view allocation_scope,
        const runtime::SystemVerilogUvmRootHandle requested_root = 0,
        const std::optional<runtime::simir::ProcessId> owner_design_process
            = std::nullopt,
        const runtime::SystemVerilogClassHandle owner_context_handle = 0U,
        std::optional<runtime::simir::ProcessId> report_process
            = std::nullopt);

    using PackedSnapshot = std::map<
        std::pair<runtime::SystemVerilogClassHandle, std::string>,
        runtime::PackedLogic4>;

    using StaticPackedSnapshot = std::map<
        std::pair<std::string, std::string>, runtime::PackedLogic4>;

    [[nodiscard]] PackedSnapshot packed_class_snapshot() const;

    void notify_class_changes(const PackedSnapshot& before);

    [[nodiscard]] StaticPackedSnapshot packed_static_snapshot() const;

    void notify_static_changes(const StaticPackedSnapshot& before);

    [[nodiscard]] runtime::SystemVerilogClassInvocationResult
    invoke_class_method(
        const std::string_view canonical_method,
        const runtime::SystemVerilogClassHandle this_handle,
        std::vector<runtime::SystemVerilogClassMethodValue>& actuals,
        const std::optional<std::uint32_t> virtual_slot);

    [[nodiscard]] runtime::SystemVerilogUvmPhaseExecutionResult
    execute_uvm_function_phase(
        const runtime::SystemVerilogUvmPhaseHandle phase_handle);

    [[nodiscard]] runtime::SystemVerilogUvmPhaseExecutionResult
    execute_uvm_task_phase(const runtime::SystemVerilogUvmPhaseHandle phase_handle,
        const UvmTaskPhaseContinuation& continuation);

    // The native UVM `run_test` (IEEE 1800.2-2020 F.3.1.2). The lowered
    // caller runs `@uvm-run-test-begin` (the phases before run), forks the
    // run_phase of each `@uvm-run-test-component`, waits until
    // `@uvm-run-test-ended`, and finishes after `@uvm-run-test-end` (the
    // phases after run).
    [[nodiscard]] std::optional<runtime::PackedLogic4> invoke_uvm_run_test(
        std::string_view identity,
        std::span<const runtime::PackedLogic4> actuals,
        std::span<const std::string> string_actuals);
    // A method called on a phase object of the native run.
    [[nodiscard]] std::optional<runtime::PackedLogic4> invoke_uvm_phase_method(
        runtime::SystemVerilogClassHandle phase_object,
        std::string_view method,
        std::span<const runtime::PackedLogic4> actuals,
        std::span<const std::string> string_actuals);
    // The object a phase callback receives for a phase: a tagged handle
    // that only the phase methods above accept.
    [[nodiscard]] runtime::SystemVerilogClassHandle uvm_phase_object(
        const runtime::SystemVerilogUvmPhaseHandle& phase);
    static constexpr runtime::SystemVerilogClassHandle uvm_phase_object_tag {
        UINT64_C(0x7f00000000000000)
    };
    std::vector<runtime::SystemVerilogUvmPhaseHandle> uvm_phase_objects;
    struct UvmRunTestState {
        runtime::SystemVerilogUvmRootHandle root { };
        // Every root holding components: the test's, and the automatic root
        // of each scope that constructed components itself.
        std::vector<runtime::SystemVerilogUvmRootHandle> roots;
        std::vector<runtime::SystemVerilogClassHandle> components;
        // The run-phase processes, one per component, that the forked
        // run_phase tasks stand for.
        std::vector<runtime::SystemVerilogUvmPhaseProcessHandle> processes;
        // The function phase in progress and the components it has handed
        // out; build visits components its parents create on the way.
        std::optional<runtime::SystemVerilogUvmPhaseKind> phase;
        std::vector<runtime::SystemVerilogClassHandle> order;
        std::size_t next { };
        std::vector<runtime::SystemVerilogClassHandle> visited;
        bool raised { };
        bool ended { };
    };
    UvmRunTestState uvm_run_test;
    // The object each TLM port, export or imp was constructed for, and the
    // connections made from each analysis port or export (IEEE 1800.2-2020
    // 12.2.10).
    std::map<runtime::SystemVerilogClassHandle, runtime::SystemVerilogClassHandle>
        uvm_tlm_owners;
    std::map<runtime::SystemVerilogClassHandle,
        std::vector<runtime::SystemVerilogClassHandle>>
        uvm_analysis_connections;
    // The sequence-item handshake (IEEE 1800.2-2020 15.3, 14.2.6): the
    // sequencer each started sequence runs on, the items each sequencer
    // holds for its driver, the item a driver took with get_next_item, and
    // the items whose finish_item still waits for a driver.
    std::map<runtime::SystemVerilogClassHandle,
        runtime::SystemVerilogClassHandle>
        uvm_sequence_sequencers;
    std::map<runtime::SystemVerilogClassHandle,
        std::vector<runtime::SystemVerilogClassHandle>>
        uvm_sequencer_items;
    std::map<runtime::SystemVerilogClassHandle,
        runtime::SystemVerilogClassHandle>
        uvm_sequencer_current;
    std::set<runtime::SystemVerilogClassHandle> uvm_pending_items;
    [[nodiscard]] runtime::SystemVerilogClassHandle uvm_pull_sequencer(
        runtime::SystemVerilogClassHandle port) const;
    void construct_uvm_component_ports(
        const semantic::sv::ClassSpecialization& specialization,
        runtime::SystemVerilogClassHandle handle,
        std::string_view allocation_scope);
    // The report object of the root, for reports outside any component.
    runtime::SystemVerilogClassHandle uvm_root_report_object { };

    [[nodiscard]] std::optional<runtime::simir::SignalId>
    backdoor_signal(const std::string_view path) const noexcept;


    ~Impl();

    [[nodiscard]] runtime::SystemVerilogVpiStoredValue vpi_value(
        const SignalId signal,
        PackedLogic4 value) const;

    [[nodiscard]] runtime::SystemVerilogVpiStoredValue vpi_driver_value(
        const SignalId signal,
        const SystemVerilogVpiDriverBinding& binding) const;
    void announce_vpi_driver(
        const SignalId signal,
        const SystemVerilogVpiDriverBinding& binding);

    static void require_vpi_value(
        const runtime::SystemVerilogVpiValueError error,
        const std::string_view operation);

    [[nodiscard]] runtime::SystemVerilogVpiValueError apply_vpi_value_state(
        const runtime::SystemVerilogVpiValueStateUpdate& update);

    void publish_vpi_stored_signal(const SignalId signal);


    /// `restoring` builds the engine's tables without running user code, for
    /// a restore to fill in (Interpreter::start_for_restore).
    void start_interpreter(bool restoring = false);

    void publish_vpi_driver(
        const runtime::simir::ProcessId process,
        const SignalId signal);

    // Creates and releases the words of a published dynamic array or queue
    // to match its current size (IEEE 1800-2023 37.17).
    void materialize_vpi_children(fsim_vpi_handle_v1 parent);
    [[nodiscard]] std::optional<runtime::SystemVerilogVpiStoredValue>
    vpi_live_value(fsim_vpi_handle_v1 handle, bool stored) const;
    // $scope, $showscopes and $showvars (IEEE 1800-2023 D.11-D.13): the
    // task `name` with its payload, run by the process that called it.
    void run_scope_task(runtime::simir::ProcessId process,
        std::string_view payload, SimulationTick time, std::uint64_t delta);
    // The design objects the scope tasks list: the VPI registry when the
    // run publishes one, else one built on the first scope task.
    struct ScopeCatalog {
        runtime::SystemVerilogVpiObjectRegistry* registry { };
        const std::map<fsim_vpi_handle_v1, SignalId>* signals { };
        const std::map<SignalId, runtime::SystemVerilogScalarKind>*
            scalar_kinds { };
        const std::map<fsim_vpi_handle_v1, runtime::simir::StringObjectId>*
            strings { };
    };
    [[nodiscard]] ScopeCatalog scope_catalog();
    [[nodiscard]] std::string current_interactive_scope() const;
    [[nodiscard]] bool vpi_signal_watched(SignalId signal) const noexcept;
    [[nodiscard]] bool vpi_any_signal_watched() const noexcept
    {
        return vpi_watch_all_values || vpi_some_signal_watched;
    }
    /// Start announcing the value changes of `object` (a value change
    /// callback was registered on it); an object without a signal, word or
    /// driver binding watches every value.
    void watch_vpi_object(std::optional<fsim_vpi_handle_v1> object);
    // A watched string variable's value change is found by comparing its
    // string object's text at each safe point (IEEE 1800-2023 38.36.1).
    [[nodiscard]] std::optional<std::string> vpi_string_text(
        fsim_vpi_handle_v1 handle,
        std::map<runtime::simir::ContainerObjectId,
            runtime::simir::ContainerValue>& snapshots) const;
    void watch_vpi_string(fsim_vpi_handle_v1 handle);
    void publish_vpi_strings();
    void sync_vpi_live_memory(runtime::simir::ContainerObjectId object,
        const runtime::simir::ContainerValue& value);
    void publish_vpi_container(
        const runtime::simir::ContainerObjectId object);

    void publish_vpi_event(const SignalId event);

    void publish_vpi_signal(
        const SignalId signal,
        const PackedLogic4& value);

    void refresh_observation_hooks();

    void rebuild_signal_observer_snapshot();

    void compact_signal_observer_snapshot() noexcept;

    void remove_signal_observer(std::uint64_t token) noexcept;

    void rebuild_scalar_signal_observer_snapshot();

    void compact_scalar_signal_observer_snapshot() noexcept;

    void publish_vpi_assertion(
        const ConcurrentAssertionEvent& event);

    void publish_assertion_attempt_start(
        runtime::simir::ProcessId design_process, std::int64_t start);
    void publish_assertion_step(runtime::simir::ProcessId design_process,
        const runtime::sva::StepRecord& step);

    void start_vpi(bool restoring = false);

    void end_vpi();

    void start_systemc();

    void end_systemc(
        const std::optional<runtime::SimulationTick> current_time = std::nullopt);

    /// Why the run cannot reset (empty when it can): Simulation::reset_refusal.
    [[nodiscard]] std::string reset_refusal() const;

    /// The design has a SystemC part (a native hierarchy registry).
    [[nodiscard]] bool has_systemc() const noexcept
    {
        return built.systemc_hierarchy != nullptr
            || !built.systemc_hierarchies.empty();
    }

    template <typename Callback>
    void for_each_systemc_registry(Callback&& callback)
    {
        if (built.systemc_hierarchies.empty()) {
            if (built.systemc_hierarchy) {
                callback(*built.systemc_hierarchy, built.systemc_roots);
            }
            return;
        }
        for (const auto& registry : built.systemc_hierarchies) {
            std::vector<std::uint64_t> roots;
            std::ranges::copy_if(
                built.systemc_roots,
                std::back_inserter(roots),
                [&](const auto handle) {
                    return registry->owns_handle(handle);
                });
            callback(*registry, roots);
        }
    }

    BuiltProject built;

    // The classes of loop-generate iterations a loaded design names: rebuilt
    // from their identities on first use (IEEE 1800-2017 27.4).
    mutable std::deque<semantic::sv::ClassSpecialization>
        generated_class_specializations;
    mutable std::unique_ptr<semantic::sv::ClassSpecializationIndex>
        generated_class_index;
    [[nodiscard]] const semantic::sv::ClassSpecialization*
    generated_class_specialization(std::string_view identity) const;
    // The parameters of the module, interface or program instance whose
    // class `specialization` is, by constraint binding identity
    // (`unit::name`) as decimal values; empty for any other class.
    [[nodiscard]] const std::map<std::string, std::string, std::less<>>&
    class_instance_parameters(
        const semantic::sv::ClassSpecialization& specialization) const;
    mutable std::map<std::string,
        std::map<std::string, std::string, std::less<>>, std::less<>>
        class_instance_parameter_cache;

    bool vpi_runtime_updates_enabled { true };

    // The interactive scope's hierarchical name, empty until the first
    // change (it is then the first top-level module). Kept by a lifecycle
    // hook, so vpi_control, the debugger and $scope all move it.
    std::string interactive_scope;
    // The VPI design built for the scope tasks of a run that publishes none.
    std::optional<SystemVerilogVpiPublishedDesign> scope_design;

    runtime::SystemVerilogClassHeap class_heap;
    // The types class constraints give the properties they name, by
    // canonical property identity; built at the first class randomize.
    std::optional<std::map<std::string, semantic::sv::TypeReference,
        std::less<>>>
        class_constraint_property_types;
    // A unit-scope variable a class constraint reads as a state variable
    // (IEEE 1800-2017 18.5): the solver sees its value at the randomize
    // call as a fixed variable, so the value is read per solve.
    struct ConstraintStateVariable {
        std::string identity;
        semantic::DeclarationId declaration;
        semantic::sv::ConstraintBinding binding;
    };
    // The state variables of a class's composed constraints, by class
    // declaration identity; built at the first solve of the class.
    std::map<std::string, std::vector<ConstraintStateVariable>, std::less<>>
        class_constraint_state_variables;
    [[nodiscard]] const std::vector<ConstraintStateVariable>&
    constraint_state_variables(
        const semantic::sv::ClassSpecialization& specialization);
    void add_constraint_state_variables(
        runtime::SystemVerilogConstraintSolver& solver,
        runtime::SystemVerilogClassRandomizeVariables& variables,
        const semantic::sv::ClassSpecialization& specialization);
    // A static constraint's mode is shared by every object of its class
    // (IEEE 1800-2017 18.9), by canonical constraint identity.
    std::map<std::string, bool, std::less<>> static_constraint_modes;
    std::optional<std::set<std::string, std::less<>>>
        static_constraint_identities;
    [[nodiscard]] bool static_constraint(std::string_view identity);
    // Exact enclosing module/interface parameter values and owner process
    // captured by class objects. Declaration identity distinguishes same-name
    // parameters; the process recovers the exact unit and instance path.
    // UVM teardown removes entries when it releases the corresponding handle.
    using ClassOwnerParameterValues = std::map<
        semantic::DeclarationId,
        std::optional<runtime::PackedLogic4>>;
    struct ClassOwnerParameterContext {
        ClassOwnerParameterValues parameters;
        std::optional<runtime::simir::ProcessId> owner_design_process;
    };
    std::map<runtime::SystemVerilogClassHandle, ClassOwnerParameterContext>
        class_owner_parameter_values;

    runtime::SystemVerilogChandleRegistry chandle_registry;

    runtime::SystemVerilogClassStaticStore class_static_store;

    runtime::SystemVerilogClassMethodRuntime class_methods;

    runtime::SystemVerilogUvmObjectService uvm_objects;

    runtime::SystemVerilogUvmComponentService uvm_components;

    runtime::SystemVerilogUvmActivityService uvm_activity;

    runtime::SystemVerilogUvmPhaseService uvm_phases;

    runtime::SystemVerilogUvmObjectionService uvm_objections;

    runtime::SystemVerilogUvmTlm1Service uvm_tlm1;

    runtime::SystemVerilogUvmTlm2Service uvm_tlm2;

    runtime::SystemVerilogUvmSequenceService uvm_sequences;

    runtime::SystemVerilogUvmCallbackService uvm_callbacks;

    runtime::SystemVerilogUvmTransactionRecorderService uvm_transactions;

    runtime::SystemVerilogUvmRegisterModelService uvm_register_model;

    runtime::SystemVerilogUvmForeignService uvm_foreign;

    runtime::SystemVerilogUvmRegistryService uvm_registry;

    runtime::SystemVerilogUvmFactoryService uvm_factory;

    runtime::SystemVerilogUvmResourcePoolService uvm_resources;

    runtime::SystemVerilogUvmSynchronizationService uvm_synchronization;

    runtime::SystemVerilogUvmConfigDbService uvm_config_db;

    runtime::SystemVerilogUvmCommandLineService uvm_command_line;

    runtime::SystemVerilogUvmTestRunnerService uvm_test_runner;

    runtime::SystemVerilogUvmReportService uvm_reports;

    std::vector<runtime::ScheduledTaskHandle> uvm_report_setting_tasks;

    std::set<std::tuple<runtime::simir::ProcessId, std::string,
        std::uint32_t, std::uint32_t>>
        numeric_metavalue_reports;

    std::map<std::string, runtime::SystemVerilogUvmRootHandle, std::less<>>
        uvm_roots_by_scope;

    application_detail::SystemVerilogClassExecution class_execution;

    /// Parser-independent executable class-body path. The adjacent
    /// class_execution object is the explicitly bounded frontend adapter for
    /// heap layout, UVM services, constraints, and unsupported residuals.
    application_detail::SystemVerilogClassHirExecution class_hir_execution;

    /// The elaborated design's process count, taken before the interpreter
    /// takes its processes (a run-time split plans over them).
    std::size_t design_process_count {};
    /// A process runs $save, $incsave, $restart or $reset (ElaboratedDesign::
    /// uses_simulation_control, read before the interpreter takes the
    /// processes).
    bool simulation_control_tasks {};
    std::unique_ptr<runtime::simir::Interpreter> interpreter;

    std::unique_ptr<runtime::SystemVerilogVpiObjectRegistry> vpi_registry;

    std::unique_ptr<runtime::SystemVerilogVpiTimeService> vpi_time;
    std::unique_ptr<runtime::SystemVerilogVpiDataReadService> vpi_data_read;

    std::unique_ptr<runtime::SystemVerilogVpiCallbackManager> vpi_callbacks;

    std::unique_ptr<runtime::SystemVerilogVpiValueControl> vpi_values;

    std::unique_ptr<runtime::SystemVerilogVpiControlService> vpi_control;

    std::unique_ptr<runtime::SystemVerilogVpiAssertionApi> vpi_assertions;

    std::unique_ptr<runtime::SystemVerilogVpiCoverageService> vpi_coverage;

    std::unique_ptr<runtime::SystemVerilogVpiSystemRegistry> vpi_systems;

    std::map<SignalId, std::vector<fsim_vpi_handle_v1>> vpi_signal_handles;

    std::vector<runtime::SystemVerilogVpiPort> vpi_ports;

    std::map<fsim_vpi_handle_v1, SignalId> vpi_handle_signals;

    std::map<fsim_vpi_handle_v1,
        std::vector<runtime::CodeCoverageCounterId>>
        vpi_statement_counters;

    std::map<fsim_vpi_handle_v1, std::vector<std::string>>
        vpi_assertion_coverage_keys;

    std::map<SignalId, std::vector<SystemVerilogVpiDriverBinding>>
        vpi_driver_bindings;

    std::map<SignalId, std::vector<fsim_vpi_handle_v1>> vpi_event_handles;
    // The event a published named-event handle triggers (IEEE 1800-2023
    // 38.35).
    std::map<fsim_vpi_handle_v1, SignalId> vpi_event_signals;

    std::map<std::string, fsim_vpi_handle_v1, std::less<>>
        vpi_assertion_handles;

    std::map<fsim_vpi_handle_v1, std::set<runtime::simir::ProcessId>>
        vpi_assertion_processes;

    std::map<SignalId, runtime::SystemVerilogScalarKind> vpi_scalar_kinds;

    std::map<SignalId, runtime::SystemVerilogVpiValueCategory> vpi_categories;

    std::map<runtime::simir::ContainerObjectId,
        std::vector<std::pair<fsim_vpi_handle_v1, std::size_t>>>
        vpi_container_words;

    std::set<fsim_vpi_handle_v1> vpi_read_only_container_words;

    std::map<runtime::simir::ContainerObjectId, SystemVerilogVpiLiveMemory>
        vpi_live_memories;
    // A watcher of dynamic array and queue sizes (cbSizeChange), and of the
    // end of the simulation for the live class objects (cbReclaimObj); both
    // empty unless a foreign library registered such a callback.
    std::function<void(fsim_vpi_handle_v1, std::size_t, std::size_t)>
        vpi_size_change_hook;
    std::function<void()> class_reclaim_hook;
    // cbCreateObj under the execution probes: the objects of lowered
    // constructors that have not completed (innermost last), and the
    // listener told of each completed object (IEEE 1800-2023 38.36.1).
    std::function<void(runtime::SystemVerilogClassHandle)> class_created_hook;
    std::vector<runtime::SystemVerilogClassHandle> constructed_classes;

    std::unordered_map<fsim_vpi_handle_v1, SystemVerilogVpiDeferredChildren>
        vpi_deferred_children;

    std::unordered_map<fsim_vpi_handle_v1,
        std::pair<runtime::simir::ProcessId, SignalId>>
        vpi_driver_handles;
    // The value each watched driver object last announced, from its watch
    // on: a driver's callback hears of its own changes alone, whichever
    // hook reports them (IEEE 1364-2005 Table 88, IEEE 1800-2023 38.36.1).
    std::unordered_map<fsim_vpi_handle_v1,
        runtime::SystemVerilogVpiStoredValue>
        vpi_driver_announced;

    // The signals and memories whose value changes reach VPI observers; a
    // watched memory keeps its last announced value to find changed words.
    std::vector<std::uint8_t> vpi_watched_signals;
    std::map<runtime::simir::ContainerObjectId,
        std::optional<runtime::simir::ContainerValue>>
        vpi_watched_containers;
    bool vpi_watch_all_values { };
    bool vpi_some_signal_watched { };
    std::map<SignalId, std::vector<fsim_vpi_handle_v1>> vpi_watched_handles;
    [[nodiscard]] bool vpi_handle_watched(
        SignalId signal, fsim_vpi_handle_v1 handle) const;

    std::map<fsim_vpi_handle_v1,
        std::pair<runtime::simir::ContainerObjectId, std::size_t>>
        vpi_word_handles;

    // The string variables and string ports VPI reads and writes through the
    // interpreter's string objects (IEEE 1800-2023 37.17, 38.15).
    std::map<fsim_vpi_handle_v1, runtime::simir::StringObjectId>
        vpi_string_handles;
    // The watched string variables with the text last announced, and the
    // safe-point observer that compares them (0 until one is watched).
    std::map<fsim_vpi_handle_v1, std::string> vpi_watched_strings;
    std::uint64_t vpi_string_watch_observer { };

    std::map<runtime::simir::ContainerObjectId,
        runtime::SystemVerilogScalarKind>
        vpi_container_scalar_kinds;

    std::map<runtime::simir::ContainerObjectId,
        runtime::SystemVerilogVpiValueCategory>
        vpi_container_categories;

    std::optional<std::uint64_t> vpi_value_state_observer;

    std::set<SignalId> vpi_forced_signals;

    std::recursive_mutex vpi_bridge_mutex;

    bool vpi_started { };
    /// Orders the signal updates foreign interfaces schedule for the next
    /// update phase (VHPI vhpiDepositPropagate, vhpiForcePropagate).
    std::uint64_t foreign_update_serial { };

    bool vpi_ended { };

    /// The foreign interfaces' save, restart and reset hooks, and the
    /// resets requested but not yet performed (IEEE 1800-2023 D.8).
    std::vector<Simulation::LifecycleHook> lifecycle_hooks;
    std::vector<Simulation::ResetRequest> reset_requests;
    /// What the first foreign-created structure of the run is (a driver, a
    /// driver owner or a process a library made): a reset rebuilds the design
    /// from the artifact and cannot recreate it, so it is refused while this
    /// is set (FSIM-SAVE-0018). Saved in app.control.
    std::string foreign_structure;
    std::uint32_t reset_count { };
    std::int32_t reset_value { };
    /// Run the oldest requested reset: start-of-reset before time changes,
    /// the engine's return to time zero, end-of-reset before the first
    /// statements run. False when the request enters interactive mode
    /// afterwards.
    [[nodiscard]] bool perform_reset();
    /// The application's share of a reset (class objects and static members,
    /// delayed VPI writes), after the engine's.
    void reset_application_state();
    /// UVM component, phase, objection and sequence state lives in the
    /// host services above; the run-state file does not carry it yet, so a
    /// run that holds any refuses to save and to reset.
    [[nodiscard]] bool uvm_state_live() const;

    // ---- Run-state files (application_simulation_impl_save.cpp) ----
    /// The application's sections of a run-state image; the engine's are
    /// written by Interpreter::record_run_state.
    runtime::RunStateSectionRegistry section_registry;
    /// True while read_state_file() sets the run up for a restore: the
    /// foreign interfaces hear no start-of-simulation phase then (IEEE
    /// 1800-2023 D.9, IEEE 1076-2019 20.8; owner decision 2026-11-07).
    bool restoring_state { };
    /// "foreign.vpi" / "foreign.vhpi" (set_foreign_run_state_sections): saved
    /// after the end-of-save callbacks, restored before start-of-restart.
    runtime::RunStateSectionRegistry foreign_section_registry;
    /// The command line and libraries a restart must repeat.
    std::string state_file_identity;
    /// The arguments $reset has sent for the calling process, by position,
    /// until its request marker arrives.
    std::map<runtime::simir::ProcessId, std::array<std::int32_t, 3>>
        reset_arguments;
    struct SaveRequest {
        std::filesystem::path file;
        bool incremental { };
    };
    /// Saves waiting for the end of the time slot, oldest first.
    std::deque<SaveRequest> save_requests;
    /// A $restart waiting for the safe point after the call.
    std::optional<std::filesystem::path> restart_request;
    /// The last full save of this run: the base of a following $incsave.
    std::optional<runtime::RunStateImage> last_full_save;
    std::filesystem::path last_full_save_file;
    /// A save that failed at the quiet point, reported when run() returns.
    std::optional<std::string> save_failure;
    /// Called for the output text of a `simulation_control_marker` message.
    void run_simulation_control(runtime::simir::ProcessId process,
        std::string_view payload);
    void register_save_sections();
    [[nodiscard]] std::string state_identity() const;
    [[nodiscard]] Simulation::StateFileResult write_state_file(
        const std::filesystem::path& file, bool incremental);
    [[nodiscard]] Simulation::StateFileResult read_state_file(
        const std::filesystem::path& file);
    /// Queue a save for the end of the current time slot (the scheduler's
    /// slot-end quiet point).
    void queue_save(SaveRequest request);
    static void save_quiet_point(void* context, runtime::Scheduler& scheduler);
    /// Run the queued $restart at a safe point of run(); false when it
    /// failed (the error is in save_failure).
    [[nodiscard]] bool perform_restart();

    mutable std::unique_ptr<runtime::VhdlVhpiObjectRegistry>
        vhdl_vhpi_registry;

    std::unique_ptr<VhdlPslExecution> vhdl_psl;

    SignalChangeHook signal_change_hook;

    std::map<std::uint64_t,
        std::shared_ptr<ObserverEntry<SignalChangeHook>>> signal_observers;

    std::shared_ptr<ObserverSnapshot<SignalChangeHook>>
        signal_observer_snapshot {
            std::make_shared<ObserverSnapshot<SignalChangeHook>>()
        };

    ObserverDispatchGeneration signal_observer_generation;

    bool signal_observer_snapshot_dirty { };

    std::size_t signal_observer_hook_depth { };

    bool signal_observation_bridge_installed { };

    std::uint64_t next_signal_observer { 1 };

    ScalarSignalChangeHook scalar_signal_change_hook;

    std::map<std::uint64_t,
        std::shared_ptr<ObserverEntry<ScalarSignalChangeHook>>>
        scalar_signal_observers;

    std::shared_ptr<ObserverSnapshot<ScalarSignalChangeHook>>
        scalar_signal_observer_snapshot {
            std::make_shared<ObserverSnapshot<ScalarSignalChangeHook>>()
        };

    ObserverDispatchGeneration scalar_signal_observer_generation;

    bool scalar_signal_observer_snapshot_dirty { };

    std::size_t scalar_signal_observer_hook_depth { };

    bool scalar_signal_observation_bridge_installed { };

    bool observation_hooks_refresh_pending { };

    bool native_signal_observation_hooks_installed { };

    bool vpi_observation_hooks_installed { };
    bool vpi_container_hook_installed { };

    std::uint64_t next_scalar_signal_observer { 1 };

    SafePointHook safe_point_hook;

    std::map<std::uint64_t, SafePointHook> safe_point_observers;

    /// Splits of load ports asked for by a foreign module input port delay
    /// put (Simulation::request_net_delay_split), the safe-point observer
    /// that makes them (0 when none is waiting), why requests were refused,
    /// and the ports refused, which later puts do not ask for again.
    std::vector<elaboration::SdfNetDelayInfo> net_delay_split_requests;
    std::uint64_t net_delay_split_observer { 0U };
    /// Bumped by each live SDF reannotation that replaces the net delay
    /// records (Simulation::sdf_net_delay_epoch).
    std::uint64_t sdf_net_delay_epoch { 0U };
    std::vector<std::string> net_delay_split_refusals;
    /// The interpreter ids of the copy drivers a run-time split added, by
    /// their ids in `built.design` (the run's fork children and foreign
    /// drivers come between).
    std::unordered_map<runtime::simir::ProcessId, runtime::simir::ProcessId>
        grafted_process_ids;
    [[nodiscard]] runtime::simir::ProcessId running_process(
        runtime::simir::ProcessId design_process) const
    {
        const auto found = grafted_process_ids.find(design_process);
        return found == grafted_process_ids.end() ? design_process
                                                  : found->second;
    }
    /// Gate drivers asked to take a delay slot (Simulation::
    /// put_gate_driver_delays) with the delays of the latest put, the
    /// safe-point observer that splits them, the ones split, and why a split
    /// was refused.
    std::map<runtime::simir::ProcessId, runtime::simir::TransitionDelays>
        gate_delay_requests;
    std::uint64_t gate_delay_observer { 0U };
    std::set<runtime::simir::ProcessId> gate_delay_split;
    std::set<runtime::simir::ProcessId> gate_delay_refused;
    std::vector<std::string> gate_delay_split_refusals;
    /// The deferred splits (gate, switch control, load port) that failed at
    /// their safe point after the put had returned success, as FSIM-VPI-SPLIT-001
    /// messages, and how many vpi_chk_error has handed out.
    std::vector<std::string> deferred_split_errors;
    std::size_t deferred_split_errors_taken { };
    void report_deferred_split_failure(
        const std::string& what, const std::string& reason);
    void commit_gate_delay_splits();
    /// The run-time splits that vpi_put_delays made, in order: the plan
    /// journal of the application (the engine journals the programs for a
    /// reset; a restart rebuilds them from these requests, since a fresh
    /// process has not made the puts). A gate split keeps its driver and
    /// delays, a switch-control split its switch name, table and the ids the
    /// run gave the copy drivers. Saved in "app.late_splits" and replayed by
    /// replay_late_splits before the engine restores its state.
    struct LateSplit {
        // 0 gate delay slot, 1 tranif control copy, 2 port net-delay copy
        // (an SDF or vpi_put_delays split of a load port)
        std::uint8_t kind { };
        runtime::simir::ProcessId driver { };
        std::string name;
        runtime::simir::TransitionDelays delays { };
        runtime::simir::NetDelayTable table { };
        std::vector<runtime::simir::ProcessId> run_ids;
        elaboration::SdfNetDelayInfo net_delay;
    };
    std::vector<LateSplit> late_splits;
    /// The splits of a restart's file, applied to the restoring run; false,
    /// with `reason`, when one cannot be rebuilt as it was.
    [[nodiscard]] bool replay_late_splits(
        const std::vector<LateSplit>& splits, std::string& reason);
    /// The gate split of one driver now (commit_gate_delay_splits'
    /// per-driver work); false, with `reason`, when the engine refuses.
    [[nodiscard]] bool split_gate_now(runtime::simir::ProcessId driver,
        runtime::simir::TransitionDelays delays, std::string& reason);
    /// The control copy of one switch now (commit_tranif_control_splits'
    /// per-switch work); `expected_ids` are the ids a replay requires.
    [[nodiscard]] bool split_net_delay_now(elaboration::SdfNetDelayInfo record,
        std::string& reason,
        const std::vector<runtime::simir::ProcessId>* expected_ids);
    [[nodiscard]] bool split_tranif_now(const std::string& name,
        const runtime::simir::NetDelayTable& table, std::string& reason,
        const std::vector<runtime::simir::ProcessId>* expected_ids);
    /// Switches declared without a delay whose control a put asked to delay
    /// (Simulation::put_tranif_control_delays), the observer that grafts the
    /// control's delayed copy, and the copy drivers made (by switch name).
    std::map<std::string, runtime::simir::NetDelayTable> tranif_requests;
    std::uint64_t tranif_observer { 0U };
    std::map<std::string, runtime::simir::ProcessId> tranif_controls;
    void commit_tranif_control_splits();
    /// The driver's program with its delay-free output writes replaced by
    /// delayed ones (WriteNetDelay) for `delays`; nullopt, with `reason`,
    /// when it writes its output in no way the split rewrites.
    [[nodiscard]] std::optional<runtime::simir::Process> gate_delay_program(
        runtime::simir::ProcessId driver, runtime::simir::TransitionDelays delays,
        std::string& reason);
    std::string gate_delay_error;
    std::set<std::string> net_delay_split_refused;
    /// `failures`, when given, receives each split that failed (what and
    /// why) besides the run's deferred split errors.
    void commit_net_delay_splits(std::vector<std::string>* failures = nullptr);
    /// Declares the group-mode SDF load copies of the design to the
    /// interpreter (Interpreter::add_foreign_net_delay_load), from the copy
    /// drivers the split built: a foreign write of the net reaches such a
    /// copy after the smallest delay of its groups.
    /// Declares the delayed-load groups of the SDF net delays on `target`
    /// (the running interpreter, or the twin a reset builds).
    void register_foreign_net_delays(runtime::simir::Interpreter& target);
    /// The fresh interpreter a reset takes the time-zero state from (IEEE
    /// 1800-2023 D.8): `fresh` is the design reloaded from its artifact,
    /// planned and configured as the running one was before start.
    [[nodiscard]] std::unique_ptr<runtime::simir::Interpreter> make_reset_twin(
        BuiltProject fresh, std::uint64_t max_deltas, SimulationEngine engine);

    std::uint64_t next_safe_point_observer { 1 };

    std::map<std::uint64_t, ForceStatementHook> force_statement_observers;

    ExecutionPointHook execution_point_primary;

    std::map<std::uint64_t, ExecutionPointHook> execution_point_observers;

    bool foreign_probes { };
    bool vhpi_end_of_step_timers { };

    std::map<std::uint64_t, SignalTransactionHook>
        signal_transaction_observers;

    std::map<std::uint64_t, DriverTransactionHook>
        driver_transaction_observers;

    std::uint64_t next_activity_observer { 1 };

    OutputHook output_hook;

    runtime::simir::Interpreter::OutputHook interpreter_output_hook;

    bool trusted_builtin_stdout_output { };

    ReportHook report_hook;
    // The installed report_hook only formats text and never observes state.
    bool trusted_text_report_hook { };

    application_detail::HdlVcdState hdl_vcd;

    std::vector<ConcurrentAssertionCoverage>
        concurrent_assertion_coverage;

    frontend::SystemVerilogCoverageExecutionState coverage_execution_state;

    frontend::SystemVerilogCoverageExecutionMode coverage_execution_mode {
        frontend::SystemVerilogCoverageExecutionMode::Interpreter
    };

    std::unordered_set<std::string> stopped_covergroups;

    std::vector<frontend::Diagnostic> coverage_diagnostics;

    std::optional<std::filesystem::path> coverage_database_path;

    std::optional<artifact::CoverageDatabaseContents>
        standard_coverage_history;

    std::optional<artifact::CoverageDatabaseIdentity>
        standard_coverage_run_identity;

    ConcurrentAssertionHook concurrent_assertion_hook;

    AssertionAttemptHook assertion_attempt_hook;
    AssertionStepHook assertion_step_hook;

    // The VPI assertion object of each design process that owns one.
    std::map<runtime::simir::ProcessId, fsim_vpi_handle_v1>
        vpi_process_assertions;

    VhdlPslAttemptHook vhdl_psl_attempt_hook;

    std::map<std::uint32_t, std::size_t>
        concurrent_assertion_indices;

    std::set<runtime::simir::ProcessId>
        concurrent_assertion_design_processes;

    std::set<runtime::simir::ProcessId>
        disabled_vpi_assertion_processes;

    std::set<runtime::simir::ProcessId>
        enabled_vpi_assertion_processes;

    std::map<std::uint32_t, bool>
        concurrent_assertion_actions_suppressed;

    std::map<runtime::simir::ProcessId, PendingConcurrentAssertion>
        pending_concurrent_assertions;
    // The end-of-simulation failure actions of each concurrent-property
    // monitor process, keyed by design process.
    std::map<runtime::simir::ProcessId, PendingConcurrentAssertion>
        sva_obligations;
    std::map<runtime::simir::ProcessId, PendingConcurrentAssertion>
        sva_vacuous_obligations;

    bool concurrent_assertions_enabled { true };

    bool concurrent_assertion_pass_actions_enabled { true };

    bool concurrent_assertion_vacuous_actions_enabled { true };

    bool concurrent_assertion_failure_actions_enabled { true };

    // Per assertion, the vpi_control action settings that override the
    // global ones above: pass, vacuous pass, fail (IEEE 1800-2023 39.5.2).
    std::map<runtime::simir::ProcessId, std::array<std::optional<bool>, 3U>>
        concurrent_assertion_action_overrides;

    // Whether the action of an outcome ("pass", "vacuous", else fail) of a
    // design process's assertion runs.
    [[nodiscard]] bool concurrent_assertion_action_enabled(
        runtime::simir::ProcessId design_process,
        std::string_view outcome) const;

    ClassPropertyChangeHook class_property_change_hook;

    // The elaborated notifier that waits on class properties depend on; it
    // toggles whenever a class property changes.
    std::optional<runtime::simir::SignalId> class_heap_change_signal;
    void toggle_class_heap_change();

    std::size_t source_function_dispatch_depth { };

    ClassStaticPropertyChangeHook class_static_property_change_hook;

    Lifecycle lifecycle { Lifecycle::ready };

    bool systemc_start_attempted { };

    bool systemc_ended { };
};

} // namespace fsim::app
