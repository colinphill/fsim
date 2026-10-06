// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/simir_driver_inventory.hpp"
#include "fsim/support/atomic_shared_ptr.hpp"

#include <atomic>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace fsim::runtime::simir {

class ProcessProgramView;
struct StaticKernelRuntimeSpec;

/// Immutable register/layout metadata shared by process instances with an
/// exact match. Process-local identity, sensitivities, operation overrides,
/// and output ownership remain in ProcessInstanceProgram.
struct ProcessProgramTemplate {
    std::string language_standard;
    std::string compatibility_profile;
    std::size_t register_count { };
    std::size_t string_register_count { };
    std::size_t container_register_count { };
    std::vector<DebugLocal> debug_locals;
    std::vector<DebugStringLocal> debug_string_locals;
    std::vector<DebugContainerLocal> debug_container_locals;
    CopyOnWriteVector<ContainerType> container_register_types;
    CopyOnWriteVector<Process::StaticTriggerRegion> static_trigger_regions;
    CopyOnWriteVector<ValueKind> register_value_kinds;
    ExpressionProfileList expression_profiles;
    ProcessSchedulingDomain scheduling_domain {
        ProcessSchedulingDomain::generic
    };

    ProcessProgramTemplate() = default;
    explicit ProcessProgramTemplate(const Process& process);
    explicit ProcessProgramTemplate(const ProcessProgramView& process);
    [[nodiscard]] bool matches(const Process& process) const;
    [[nodiscard]] bool matches(const ProcessProgramView& process) const;
};

/// Dynamic/instance-owned half of a SimIR Process. This intentionally excludes
/// the potentially large, identical register/debug/type/profile vectors above.
struct ProcessInstanceProgram {
    ProcessId id { };
    std::string name;
    std::vector<Sensitivity> static_sensitivity;
    OperationList operations;
    std::vector<Process::DriverRegion> driver_regions;
    DriveStrength drive_strength;
    std::optional<SignalId> switch_source;
    std::optional<SignalId> switch_target;
    std::optional<SignalId> switch_control;
    std::uint64_t switch_source_offset { };
    std::uint64_t switch_target_offset { };
    std::uint64_t switch_width { };
    bool switch_active_high { true };
    bool switch_bidirectional { };
    bool switch_resistive { };
    bool initialize { true };
    bool observed { };
    bool reactive { };
    std::optional<std::uint32_t> program_owner;
    bool postponed { };
    bool final { };

    ProcessInstanceProgram() = default;
    explicit ProcessInstanceProgram(Process process);
    explicit ProcessInstanceProgram(const ProcessProgramView& process);
};

/// Stable heap-owned program identity shared by compact and full process
/// state. ProcessProgramView borrows the template and instance inside this
/// record, so promoting execution state never relocates either object.
struct ProcessProgramStorage {
    std::shared_ptr<const ProcessProgramTemplate> program_template;
    ProcessInstanceProgram instance_program;
    mutable std::unique_ptr<Process> public_program_facade;
};

/// Lazy shared operation-body cache for processes that originally shared one
/// immutable operation body. The source view is retained only while processes
/// are registered, then released before execution starts.
class ProcessStartupWriteBodyCache {
public:
    explicit ProcessStartupWriteBodyCache(const OperationList& source);

    [[nodiscard]] std::shared_ptr<const OperationList>
    cached_operations() const noexcept;
    [[nodiscard]] std::shared_ptr<const OperationList> publish_or_get(
        std::shared_ptr<const OperationList> candidate);
    void release_registration_identity() noexcept;

private:
    mutable fsim::support::AtomicSharedPtr<const OperationList>
        materialized_operations_;
    std::optional<OperationList> registration_identity_;
};

/// Data-only form of an exact time-zero SystemVerilog constant whole write.
/// The interpreter executes these fields directly; the original tiny body is
/// reconstructed only for callers that request the exact program view.
struct ProcessStartupWriteBank {
    PackedLogic4 value;
    DebugPoint entry_point;
    std::optional<DebugPoint> statement_point;
    std::optional<CopyRegister> constant_copy;
    SignalId signal { };
    std::uint32_t offset { };
    bool slice { };
    SignalUpdateDomain update_domain {
        SignalUpdateDomain::systemverilog_active
    };
    std::size_t operation_count { };
    std::shared_ptr<ProcessStartupWriteBodyCache> body_cache;

    [[nodiscard]] OperationList materialize_operations() const;
    [[nodiscard]] std::shared_ptr<const OperationList>
    cached_operations() const noexcept;
    [[nodiscard]] const OperationList& operations() const;

private:
    mutable fsim::support::AtomicSharedPtr<const OperationList>
        materialized_operations;
};

/// The fields changed by fixed-container normalization. All other program
/// metadata continues to belong to the original instance and template.
struct ProcessProgramOverlay {
    std::size_t register_count { };
    std::size_t container_register_count { };
    OperationList operations;
    CopyOnWriteVector<ValueKind> register_value_kinds;
    CopyOnWriteVector<ContainerType> container_register_types;
};

/// One fused combinational cone installed by the elaboration fusion planner.
/// The sink ProcessId executes the fused body; the other members remain
/// installed with their original programs but never run (no sensitivity, no
/// initialization). Internal nets are not published while the cone is fused;
/// observing one recomputes the cone's internal values from the committed
/// boundary inputs and commits them under each original member's identity.
struct FusedConeRuntimeSpec {
    /// Topological order; includes the sink.
    std::vector<ProcessId> members;
    ProcessId sink { };
    /// The sink's original program, used only for materialization.
    Process sink_original;
    std::vector<SignalId> internal_signals;
    std::vector<SignalId> boundary_inputs;
};

/// Non-owning read view used by runtime consumers. Runtime views borrow the
/// stable template and instance records; temporary facade views borrow a
/// caller-owned Process. Constructing a view does not allocate or copy data.
class ProcessProgramView {
public:
    ProcessProgramView() = default;
    ProcessProgramView(
        const ProcessProgramTemplate& common,
        const ProcessInstanceProgram& instance,
        const ProcessStartupWriteBank* startup_write_bank = nullptr) noexcept;
    explicit ProcessProgramView(const Process& facade) noexcept;
    ProcessProgramView(const ProcessProgramView& original,
        const ProcessProgramOverlay& overlay) noexcept;

    [[nodiscard]] const ProcessId& id() const noexcept;
    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] const std::string& language_standard() const noexcept;
    [[nodiscard]] const std::string& compatibility_profile() const noexcept;
    [[nodiscard]] const std::size_t& register_count() const noexcept;
    [[nodiscard]] const std::size_t& string_register_count() const noexcept;
    [[nodiscard]] const std::size_t& container_register_count() const noexcept;
    [[nodiscard]] const std::vector<DebugLocal>& debug_locals() const noexcept;
    [[nodiscard]] const std::vector<DebugStringLocal>&
        debug_string_locals() const noexcept;
    [[nodiscard]] const std::vector<DebugContainerLocal>&
        debug_container_locals() const noexcept;
    [[nodiscard]] const CopyOnWriteVector<ContainerType>&
        container_register_types() const noexcept;
    [[nodiscard]] const std::vector<Sensitivity>&
        static_sensitivity() const noexcept;
    [[nodiscard]] const CopyOnWriteVector<Process::StaticTriggerRegion>&
        static_trigger_regions() const noexcept;
    [[nodiscard]] const OperationList& operations() const;
    [[nodiscard]] const std::vector<Process::DriverRegion>&
        driver_regions() const noexcept;
    [[nodiscard]] const DriveStrength& drive_strength() const noexcept;
    [[nodiscard]] const std::optional<SignalId>& switch_source() const noexcept;
    [[nodiscard]] const std::optional<SignalId>& switch_target() const noexcept;
    [[nodiscard]] const std::optional<SignalId>& switch_control() const noexcept;
    [[nodiscard]] const std::uint64_t& switch_source_offset() const noexcept;
    [[nodiscard]] const std::uint64_t& switch_target_offset() const noexcept;
    [[nodiscard]] const std::uint64_t& switch_width() const noexcept;
    [[nodiscard]] const bool& switch_active_high() const noexcept;
    [[nodiscard]] const bool& switch_bidirectional() const noexcept;
    [[nodiscard]] const bool& switch_resistive() const noexcept;
    [[nodiscard]] const CopyOnWriteVector<ValueKind>&
        register_value_kinds() const noexcept;
    [[nodiscard]] const bool& initialize() const noexcept;
    [[nodiscard]] const bool& observed() const noexcept;
    [[nodiscard]] const bool& reactive() const noexcept;
    [[nodiscard]] const std::optional<std::uint32_t>&
        program_owner() const noexcept;
    [[nodiscard]] const bool& postponed() const noexcept;
    [[nodiscard]] const bool& final() const noexcept;
    [[nodiscard]] const ExpressionProfileList&
        expression_profiles() const noexcept;
    [[nodiscard]] const ProcessSchedulingDomain&
        scheduling_domain() const noexcept;

    [[nodiscard]] Process materialize() const;
    [[nodiscard]] Process materialize_transient() const;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] const void* common_identity() const noexcept;
    [[nodiscard]] bool matches_registered_binding(
        ProcessId process,
        const ProcessExecutorProgramBinding& binding) const noexcept;
    [[nodiscard]] bool matches_forked_binding(
        ProcessId process,
        const ProcessExecutorProgramBinding& binding) const noexcept;

private:
    friend class InterpreterProgramAccess;

    [[nodiscard]] static ProcessExecutorProgramBinding executor_binding(
        const ProcessProgramView& registered,
        const ProcessProgramView& generated,
        ProcessId generated_process,
        std::shared_ptr<const ProcessSignalRemap> signal_remap);
    [[nodiscard]] static ProcessExecutorProgramBinding
    fork_access_attestation(
        const ProcessExecutorProgramBinding& binding);
    [[nodiscard]] Process materialize_impl(bool cache_startup_body) const;

    const ProcessProgramTemplate* common_ { };
    const ProcessInstanceProgram* instance_ { };
    const Process* facade_ { };
    const ProcessProgramOverlay* overlay_ { };
    const ProcessStartupWriteBank* startup_write_bank_ { };

};

namespace process_program_detail {

[[nodiscard]] bool operation_list_shareable(
    const OperationList& operations);
[[nodiscard]] bool share_operations(
    const ProcessProgramView& representative,
    Process& candidate,
    std::span<const Signal> signals,
    OperationList::Storage* recycled_operations = nullptr);
[[nodiscard]] bool share_operations(
    const ProcessProgramView& representative,
    const ProcessProgramTemplate& candidate_common,
    ProcessInstanceProgram& candidate,
    std::span<const Signal> signals,
    OperationList::Storage* recycled_operations = nullptr);

} // namespace process_program_detail

/// Internal bridge for app/runtime code that needs a stable view without
/// materializing the public Process facade.
class InterpreterProgramAccess {
public:
    [[nodiscard]] static ProcessExecutorProgramBinding executor_binding(
        const ProcessProgramView& registered,
        const ProcessProgramView& generated,
        ProcessId generated_process,
        std::shared_ptr<const ProcessSignalRemap> signal_remap = { });
    [[nodiscard]] static ProcessExecutorProgramBinding
    fork_access_attestation(
        const ProcessExecutorProgramBinding& binding);
    [[nodiscard]] static ProcessProgramView view(
        const Interpreter& interpreter, ProcessId process);
    [[nodiscard]] static bool data_only_startup_write(
        const Interpreter& interpreter, ProcessId process) noexcept;
    [[nodiscard]] static std::size_t operation_count(
        const Interpreter& interpreter, ProcessId process);
    [[nodiscard]] static ProcessId add_program(
        Interpreter& interpreter,
        std::shared_ptr<const ProcessProgramTemplate> common,
        ProcessInstanceProgram instance);
    [[nodiscard]] static ProcessId validate_program(
        Interpreter& interpreter,
        std::shared_ptr<const ProcessProgramTemplate> common,
        ProcessInstanceProgram instance);
    /// Install proof built by trusted elaboration or rebuilt by validated
    /// from_state ingress. This source-private bridge is not an authenticity
    /// check; mutable/serialized inventories must be validated before calling.
    static void set_trusted_signal_driver_inventory(
        Interpreter& interpreter,
        std::shared_ptr<const SignalDriverInventory> inventory) noexcept;
    /// Install a report sink that consumes only the supplied message and
    /// metadata. It skips the implicit signal-observation barrier; before it
    /// inspects or mutates interpreter state it must call
    /// Interpreter::prepare_output_callback_observation().
    static void set_trusted_text_report_hook(
        Interpreter& interpreter, Interpreter::ReportHook hook);
    /// Install fused-cone metadata before start. `dormant` lists members that
    /// never execute while fused.
    static void install_fused_cones(Interpreter& interpreter,
        std::vector<FusedConeRuntimeSpec> cones,
        const std::vector<ProcessId>& dormant);
    [[nodiscard]] static bool fusion_dormant(
        const Interpreter& interpreter, ProcessId process) noexcept;
    /// Install the engine v4 static kernel before start. The host must
    /// already run its stub program; every other member must be dormant.
    static void install_static_kernel(Interpreter& interpreter,
        StaticKernelRuntimeSpec spec);
};

/// Startup-only interner. Operation-body identity narrows likely matches;
/// sharing still requires exact equality of every common field. Signal
/// remaps and operation overrides remain instance-owned.
class ProcessProgramTemplatePool {
public:
    [[nodiscard]] std::shared_ptr<const ProcessProgramTemplate> intern(
        const Process& process);
    [[nodiscard]] std::shared_ptr<const ProcessProgramTemplate> intern(
        const ProcessProgramView& process);
    void clear() noexcept;

private:
    std::unordered_map<const void*,
        std::vector<std::shared_ptr<const ProcessProgramTemplate>>> buckets_;
};

} // namespace fsim::runtime::simir
