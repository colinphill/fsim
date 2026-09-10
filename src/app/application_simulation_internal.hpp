// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "application_class_execution.hpp"
#include "application_internal.hpp"
#include "application_uvm_registry.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/artifact/coverage_database_codec.hpp"
#include "fsim/artifact/coverage_database_merge.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <set>
#include <tuple>

namespace fsim::app {
using namespace application_detail;

struct Simulation::Impl {

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
        const bool selector_is_instance) const;

    [[nodiscard]] static std::int32_t query_standard_coverage(
        const artifact::CoverageDatabaseContents& contents,
        const bool maximum,
        const std::set<artifact::CoverageDatabaseIdentity>* identities
        = nullptr) noexcept;

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

    void validate_external_value(
        const SignalId signal,
        const PackedLogic4& value,
        const std::string_view operation) const;

    using ConstructorEnvironment = application_detail::SystemVerilogClassExecution::ConstructorEnvironment;

    using SourceStringEnvironment = std::map<std::string, std::string, std::less<>>;

    [[nodiscard]] const frontend::SystemVerilogClassSpecialization&
    class_specialization(const std::string_view identity) const;

    [[nodiscard]] runtime::SystemVerilogUvmRootHandle component_root(
        const std::string_view allocation_scope);

    [[nodiscard]] runtime::PackedLogic4 invoke_source_randomize(
        const runtime::SystemVerilogClassHandle handle,
        const std::span<const std::string> selected_names,
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

    void assign_property_value(
        runtime::SystemVerilogClassPropertyValue& destination,
        const std::string_view canonical_identity,
        const runtime::PackedLogic4& value);

    [[nodiscard]] runtime::PackedLogic4 invoke_class_container(
        const runtime::SystemVerilogClassHandle receiver,
        const std::string_view operation,
        const std::span<const runtime::PackedLogic4> actuals);

    [[nodiscard]] runtime::PackedLogic4 invoke_checked_class_cast(
        const runtime::SystemVerilogClassHandle handle,
        const std::string_view operation) const;

    [[nodiscard]] static std::pair<std::string_view, std::string_view>
    static_property_parts(const std::string_view identity);

    [[nodiscard]] std::optional<runtime::PackedLogic4>
    evaluate_constructor_expression(
        const frontend::Expression& expression,
        const runtime::SystemVerilogClassHandle handle,
        ConstructorEnvironment& environment);

    [[nodiscard]] ConstructorEnvironment bind_constructor_actuals(
        const frontend::SystemVerilogClassMethodProfile& constructor,
        const runtime::SystemVerilogClassHandle handle,
        const std::span<const runtime::PackedLogic4> actuals,
        const std::span<const std::string> actual_names);

    void execute_constructor_statements(
        const std::span<const frontend::Statement> statements,
        const runtime::SystemVerilogClassHandle handle,
        ConstructorEnvironment& environment,
        const bool native_uvm_library);

    [[nodiscard]] const frontend::SystemVerilogClassMethodProfile&
    source_method(
        const runtime::SystemVerilogClassHandle handle,
        const std::string_view canonical_identity,
        const bool virtual_dispatch) const;

    [[nodiscard]] const frontend::SystemVerilogClassMethodProfile*
    source_method_named(
        const runtime::SystemVerilogClassHandle handle,
        const std::string_view name) const;

    struct SourceFunctionResult {
        bool returned { };
        runtime::PackedLogic4 value;
    };

    [[nodiscard]] static std::optional<std::string>
    evaluate_source_string_expression(
        const frontend::Expression& expression,
        const SourceStringEnvironment& environment);

    [[nodiscard]] SourceFunctionResult execute_source_function_statements(
        const std::span<const frontend::Statement> statements,
        const runtime::SystemVerilogClassHandle handle,
        ConstructorEnvironment& environment,
        SourceStringEnvironment& string_environment);

    [[nodiscard]] runtime::PackedLogic4 invoke_source_profile(
        const frontend::SystemVerilogClassMethodProfile& method,
        const runtime::SystemVerilogClassHandle handle,
        std::vector<runtime::PackedLogic4>& actuals,
        std::vector<std::string>& string_actuals,
        const std::span<const std::string> actual_names,
        const std::span<const std::uint8_t> actual_directions);

    [[nodiscard]] runtime::PackedLogic4 invoke_source_profile(
        const frontend::SystemVerilogClassMethodProfile& method,
        const runtime::SystemVerilogClassHandle handle,
        std::vector<runtime::PackedLogic4>& actuals,
        const std::span<const std::string> actual_names,
        const std::span<const std::uint8_t> actual_directions);

    void invoke_source_task_profile(
        const frontend::SystemVerilogClassMethodProfile& method,
        const runtime::SystemVerilogClassHandle handle,
        std::vector<runtime::PackedLogic4>& actuals);

    [[nodiscard]] runtime::PackedLogic4 invoke_source_function(
        const runtime::SystemVerilogClassHandle handle,
        const std::string_view canonical_identity,
        std::vector<runtime::PackedLogic4>& actuals,
        std::vector<std::string>& string_actuals,
        const std::span<const std::string> actual_names,
        const std::span<const std::uint8_t> actual_directions,
        const bool virtual_dispatch);

    [[nodiscard]] const frontend::SystemVerilogClassMethodProfile&
    source_static_method(const std::string_view canonical_identity) const;

    [[nodiscard]] runtime::PackedLogic4 invoke_source_static_function(
        const std::string_view canonical_identity,
        std::vector<runtime::PackedLogic4>& actuals,
        std::vector<std::string>& string_actuals,
        const std::span<const std::string> actual_names,
        const std::span<const std::uint8_t> actual_directions);

    void invoke_source_constructor(
        const frontend::SystemVerilogClassSpecialization& specialization,
        const runtime::SystemVerilogClassHandle handle,
        const std::span<const runtime::PackedLogic4> actuals,
        const std::span<const std::string> actual_names);

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
        const runtime::SystemVerilogUvmRootHandle requested_root = 0);

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

    [[nodiscard]] std::optional<runtime::simir::SignalId>
    backdoor_signal(const std::string_view path) const noexcept;

#if defined(FSIM_HAS_LLVM)
    void request_background_jit_compilation(
        const bool force_adaptive = false);

    void cancel_background_jit_compilation() noexcept;

#endif

    ~Impl();

    [[nodiscard]] runtime::SystemVerilogVpiStoredValue vpi_value(
        const SignalId signal,
        PackedLogic4 value) const;

    [[nodiscard]] runtime::SystemVerilogVpiStoredValue vpi_driver_value(
        const SignalId signal,
        const SystemVerilogVpiDriverBinding& binding) const;

    static void require_vpi_value(
        const runtime::SystemVerilogVpiValueError error,
        const std::string_view operation);

    [[nodiscard]] runtime::SystemVerilogVpiValueError apply_vpi_value_state(
        const runtime::SystemVerilogVpiValueStateUpdate& update);

    void publish_vpi_stored_signal(const SignalId signal);

    void publish_vpi_driver(
        const runtime::simir::ProcessId process,
        const SignalId signal);

    void publish_vpi_container(
        const runtime::simir::ContainerObjectId object);

    void publish_vpi_event(const SignalId event);

    void publish_vpi_signal(
        const SignalId signal,
        const PackedLogic4& value);

    void publish_vpi_assertion(
        const ConcurrentAssertionEvent& event);

    void start_vpi();

    void end_vpi();

    void start_systemc();

    void end_systemc(
        const std::optional<runtime::SimulationTick> current_time = std::nullopt);

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

    bool vpi_runtime_updates_enabled { true };

    runtime::SystemVerilogClassHeap class_heap;

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

#if defined(FSIM_HAS_LLVM)
    // Shared by every compiled executor. It is fully populated before executor
    // installation and outlives the interpreter that owns those executors.
    std::vector<std::uint32_t> signal_widths;

    std::vector<runtime::simir::ValueKind>
        signal_value_kinds;

    std::vector<runtime::simir::ResolutionKind>
        signal_resolutions;

    // Optimized executors may use elaboration-proven constant signal values.
    // Keep their process programs alive alongside the JIT and interpreter.
    std::vector<std::unique_ptr<runtime::simir::Process>>
        jit_specialized_processes;

    // Ordinary compiled execution deliberately omits source restart points.
    // Retain exactly the LLVM-owned process IDs so a pre-start execution-point
    // observer can return those processes to the reference interpreter without
    // disturbing SystemC or other alternate executors.
    std::vector<runtime::simir::ProcessId> jit_processes;

    bool jit_debug_instrumentation { };

    // The interpreter owns executors referring to this JIT. Member destruction
    // is reversed, so declaring the JIT first destroys the interpreter first.
    std::unique_ptr<compiler::LlvmJit> jit;

    // Retain each bounded background compilation so cache observers can request
    // a stable completed snapshot without making normal simulation startup
    // synchronous again.
    std::vector<std::shared_future<
        std::vector<compiler::JitProcessHandle>>>
        jit_compilations;

    // Only bounded, cold-start-effective modules participate in the startup
    // barrier. Larger recurring kernels remain in jit_compilations for cache
    // synchronization and lifetime management, but promote asynchronously.
    std::vector<std::shared_future<
        std::vector<compiler::JitProcessHandle>>>
        jit_startup_compilations;

    std::mutex jit_background_mutex;

    std::condition_variable jit_background_condition;

    bool jit_background_requested { };

    bool jit_background_forced { };

    bool jit_background_cancelled { };

    // Large designs may profitably execute their first events while the
    // bounded startup tier materializes. Small designs retain an eager native
    // set so debugger/trace observations and short-run behavior stay stable.
    bool jit_overlap_startup { };

    // The deferred executor promises are fulfilled by this bounded worker
    // batch while early simulation work proceeds through the interpreter.
    // It is declared before the interpreter so destruction stops execution
    // before waiting for compilation, while still outliving the JIT itself.
    std::shared_future<void> jit_materialization;

#endif
    std::unique_ptr<runtime::simir::Interpreter> interpreter;

    std::unique_ptr<runtime::SystemVerilogVpiObjectRegistry> vpi_registry;

    std::unique_ptr<runtime::SystemVerilogVpiTimeService> vpi_time;

    std::unique_ptr<runtime::SystemVerilogVpiCallbackManager> vpi_callbacks;

    std::unique_ptr<runtime::SystemVerilogVpiValueControl> vpi_values;

    std::unique_ptr<runtime::SystemVerilogVpiControlService> vpi_control;

    std::unique_ptr<runtime::SystemVerilogVpiCoverageService> vpi_coverage;

    std::unique_ptr<runtime::SystemVerilogVpiSystemRegistry> vpi_systems;

    std::map<SignalId, std::vector<fsim_vpi_handle_v1>> vpi_signal_handles;

    std::map<fsim_vpi_handle_v1, SignalId> vpi_handle_signals;

    std::map<fsim_vpi_handle_v1,
        std::vector<runtime::CodeCoverageCounterId>>
        vpi_statement_counters;

    std::map<fsim_vpi_handle_v1, std::vector<std::string>>
        vpi_assertion_coverage_keys;

    std::map<SignalId, std::vector<SystemVerilogVpiDriverBinding>>
        vpi_driver_bindings;

    std::map<SignalId, std::vector<fsim_vpi_handle_v1>> vpi_event_handles;

    std::map<std::string, fsim_vpi_handle_v1, std::less<>>
        vpi_assertion_handles;

    std::map<SignalId, runtime::SystemVerilogScalarKind> vpi_scalar_kinds;

    std::map<SignalId, runtime::SystemVerilogVpiValueCategory> vpi_categories;

    std::map<runtime::simir::ContainerObjectId,
        std::vector<std::pair<fsim_vpi_handle_v1, std::size_t>>>
        vpi_container_words;

    std::map<fsim_vpi_handle_v1,
        std::pair<runtime::simir::ContainerObjectId, std::size_t>>
        vpi_word_handles;

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

    bool vpi_ended { };

    mutable std::unique_ptr<runtime::VhdlVhpiObjectRegistry>
        vhdl_vhpi_registry;

    std::unique_ptr<VhdlPslExecution> vhdl_psl;

    std::size_t compiled_processes { };

    std::size_t compiled_modules { };

    SignalChangeHook signal_change_hook;

    std::map<std::uint64_t, SignalChangeHook> signal_observers;

    std::uint64_t next_signal_observer { 1 };

    ScalarSignalChangeHook scalar_signal_change_hook;

    std::map<std::uint64_t, ScalarSignalChangeHook> scalar_signal_observers;

    std::uint64_t next_scalar_signal_observer { 1 };

    SafePointHook safe_point_hook;

    std::map<std::uint64_t, SafePointHook> safe_point_observers;

    std::uint64_t next_safe_point_observer { 1 };

    OutputHook output_hook;

    ReportHook report_hook;

    application_detail::HdlVcdState hdl_vcd;

    std::vector<ConcurrentAssertionCoverage>
        concurrent_assertion_coverage;

    std::vector<ConcurrentAssertionEvent>
        concurrent_assertion_events;

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

    VhdlPslAttemptHook vhdl_psl_attempt_hook;

    std::map<std::uint32_t, std::size_t>
        concurrent_assertion_indices;

    std::set<runtime::simir::ProcessId>
        concurrent_assertion_design_processes;

    std::map<std::uint32_t, bool>
        concurrent_assertion_actions_suppressed;

    std::map<runtime::simir::ProcessId, PendingConcurrentAssertion>
        pending_concurrent_assertions;

    bool concurrent_assertions_enabled { true };

    bool concurrent_assertion_pass_actions_enabled { true };

    bool concurrent_assertion_vacuous_actions_enabled { true };

    bool concurrent_assertion_failure_actions_enabled { true };

    ClassPropertyChangeHook class_property_change_hook;

    ClassStaticPropertyChangeHook class_static_property_change_hook;

    std::size_t source_method_depth_ { };

    Lifecycle lifecycle { Lifecycle::ready };

    bool systemc_start_attempted { };

    bool systemc_ended { };
};

} // namespace fsim::app
