// SPDX-License-Identifier: Apache-2.0
#pragma once

// The SimIR reference interpreter. The data model it runs (operations,
// processes, signals, errors) is in simir.hpp, which declares the class.
#include "fsim/runtime/simir.hpp"

namespace fsim::runtime::simir {

/// Small reference interpreter for differential testing of generated code.
struct NativeRegionAllocationTestAccess;
struct SystemCBridgeTestAccess;
/// The application test that saves and restores a whole simulation reaches
/// its interpreter through this friend (tests/app/run_state_application_test.cpp).
struct RunStateTestAccess;

class Interpreter {
public:
    using SignalChangeHook = std::function<void(SignalId, const PackedLogic4&, SimulationTick)>;
    /// Returns true when an installed application bridge currently has a real
    /// observer for this signal. This lets dormant VPI/trace bridge hooks stay
    /// installed without forcing native updates through packed publication.
    using NativeSignalObservationRequiredHook
        = std::function<bool(SignalId)>;
    /// Returns true when any installed application bridge currently observes
    /// native signal publication. The update phase uses this scheduler-safe
    /// aggregate query to avoid repeating the per-signal callback when no
    /// observer exists anywhere.
    using NativeSignalObservationAnyHook = std::function<bool()>;
    /// Observes a change to the stored, unforced driver state. Unlike
    /// SignalChangeHook this also fires when a force masks the effective value.
    using StoredSignalChangeHook
        = std::function<void(SignalId, SimulationTick)>;
    /// Observes one resolved signal driver's underlying contribution even when
    /// the aggregate resolved value does not change.
    using DriverChangeHook
        = std::function<void(ProcessId, SignalId, SimulationTick)>;
    /// Observes an immediate, delta, or timed named-event trigger at delivery.
    using EventTriggerHook = std::function<void(SignalId, SimulationTick)>;
    using ForceStatementHook = simir::ForceStatementHook;
    using SignalTransactionHook = simir::SignalTransactionHook;
    using DriverTransactionHook = simir::DriverTransactionHook;
    using DebugLocalChangeHook = simir::DebugLocalChangeHook;
    /// Observes an actual write to a base container object after its value and
    /// any signal alias have been committed.
    using ContainerObjectChangeHook
        = std::function<void(ContainerObjectId, SimulationTick)>;
    using ContainerElementChangeHook = std::function<void(
        ContainerObjectId, std::size_t, const PackedLogic4&, SimulationTick)>;
    using ExecutionPointHook = simir::ExecutionPointHook;
    using OutputHook = simir::OutputHook;
    using ReportHook = simir::ReportHook;
    /// A covergroup sample (IEEE 1800-2023 19.8) at the time and delta of
    /// the sampling process: a closed static kernel running its own time
    /// steps passes its own time, ahead of the scheduler's.
    using CoverageSampleHook = std::function<void(
        std::string_view,
        std::span<const PackedLogic4>,
        std::span<const std::uint8_t>,
        std::span<const frontend::SystemVerilogScalarKind>,
        CoverageSampleTrigger,
        SimulationTick,
        std::uint64_t)>;
    using CoverageQueryHook = std::function<PackedLogic4(
        CoverageQueryKind, const std::string&)>;
    using VhdlPslApiHook = std::function<bool(
        VhdlPslApiKind, std::optional<bool>)>;
    using CoverageControlHook
        = std::function<std::int32_t(const CoverageControlEvent&)>;
    using CoverageAccessHook
        = std::function<std::int32_t(const CoverageAccessEvent&)>;
    using CodeCoverageOverflowHook
        = std::function<void(::fsim::runtime::CodeCoverageCounterId)>;
    using SystemCommandHook = simir::SystemCommandHook;
    using VcdControlHook = simir::VcdControlHook;
    using CoverageDatabaseControlHook
        = std::function<void(const CoverageDatabaseControlEvent&)>;
    /// Decide whether one dynamic fork operation may create its children.
    /// The identity is the static design process that owns the fork, including
    /// when a dynamic child creates a nested fork.
    using ForkSpawnFilter = std::function<bool(ProcessId)>;
    /// A concurrent assertion's monitor started an attempt at the given
    /// simulation tick (IEEE 1800-2023 39.4.2 cbAssertionStart); the process
    /// is the static design process that owns the assertion.
    using SvaAttemptHook = std::function<void(ProcessId, std::int64_t)>;
    /// A concurrent assertion's disable condition became true and ended the
    /// attempt that started at the given simulation tick in the disabled
    /// state (IEEE 1800-2023 16.12, 39.4.2 cbAssertionDisabledEvaluation);
    /// the process is the static design process that owns the assertion.
    /// Called at once, from the commit of the directive's disable net, once
    /// per attempt in the order the attempts started.
    using SvaDisabledHook = std::function<void(ProcessId, std::int64_t)>;
    /// A step of an attempt of the process's assertion (IEEE 1800-2023 39.4.2
    /// cbAssertionStepSuccess / cbAssertionStepFailure); produced only for
    /// attempts stepping was enabled for (39.5.2).
    using SvaStepHook = std::function<void(ProcessId, const sva::StepRecord&)>;
    /// The action controls of a design process's concurrent assertions and
    /// expect statements (IEEE 1800-2023 20.11, 39.5.2): bit n set means the
    /// action block of outcome n (1 success, 2 failure, 3 vacuous success)
    /// does not run. It reads no signal.
    using SvaActionFilter = std::function<std::uint32_t(ProcessId)>;
    /// One control site (IEEE 1800-2023 20.11): an assertion, expect
    /// statement or concurrent directive whose SvaKind::control reads of
    /// `process` carry `site` and the description `type|directive|name`
    /// (semantic::sva::control_on and the others).
    struct AssertionControlSite {
        ProcessId process { };
        std::uint32_t site { };
        std::string description;
    };
    // Class callbacks receive the currently executing process. The next
    // identity is the owning static design process; runtime fork children
    // retain it even though their ProcessProgram id is dynamic.
    using ClassAllocateContextHook = std::function<std::uint64_t(
        ProcessId, ProcessId, std::string_view, std::string_view,
        std::string_view,
        std::span<const PackedLogic4>,
        std::span<const std::string>,
        std::span<const std::string>)>;
    using ClassPropertyReadHook = std::function<PackedLogic4(
        std::uint64_t, std::string_view)>;
    using ClassPropertyWriteHook = std::function<void(
        std::uint64_t, std::string_view, const PackedLogic4&)>;
    // Read (write == false) or store a value-container class property;
    // a read of a property never written leaves `value` unchanged.
    using ClassPropertyContainerHook = std::function<void(
        std::uint64_t, std::string_view, ContainerValue&, bool)>;
    // Whether a value-change callback watches the elements of an object's
    // array property (object handle, property identity).
    using ClassContainerWatchedHook
        = std::function<bool(std::uint64_t, std::string_view)>;
    using ClassMethodCallContextHook = std::function<PackedLogic4(
        ProcessId,
        std::uint64_t,
        std::string_view,
        std::vector<PackedLogic4>&,
        std::vector<std::string>&,
        std::span<const std::string>,
        std::span<const std::uint8_t>,
        std::span<const SystemVerilogConstraintTemplate>,
        bool)>;
    using ClassStaticPropertyReadHook = std::function<PackedLogic4(std::string_view)>;
    using ClassStaticPropertyWriteHook = std::function<void(
        std::string_view, const PackedLogic4&)>;
    // The last argument is the call's class specialization
    // (ClassStaticMethodCall::class_specialization).
    using ClassStaticMethodCallContextHook = std::function<PackedLogic4(
        ProcessId,
        std::string_view,
        std::vector<PackedLogic4>&,
        std::vector<std::string>&,
        std::span<const std::string>,
        std::span<const std::uint8_t>,
        std::string_view)>;
    using DpiFunctionCallHook = simir::DpiFunctionCallHook;

    using DpiSiteHooks = simir::DpiSiteHooks;

    explicit Interpreter(
        SchedulerOptions options = { },
        std::uint64_t seed = 1);
    ~Interpreter();
    Interpreter(Interpreter&&) noexcept;
    /// Marks the interpreter that lives until its command ends: when the
    /// executable exits without teardown (support::exit_without_teardown),
    /// its destructor skips freeing the design state. Other interpreters,
    /// such as elaboration-time evaluators, free theirs.
    void retain_state_until_exit() noexcept;
    Interpreter& operator=(Interpreter&&) noexcept;
    Interpreter(const Interpreter&) = delete;
    Interpreter& operator=(const Interpreter&) = delete;

    [[nodiscard]] SignalId add_signal(Signal signal);
    [[nodiscard]] StringObjectId add_string_object(StringObject object);
    [[nodiscard]] ContainerObjectId add_container_object(
        ContainerObject object);
    void add_container_signal_alias(ContainerSignalAlias alias);
    void add_container_element_signal_alias(
        ContainerElementSignalAlias alias);
    void add_container_aggregate_signal_alias(
        ContainerAggregateSignalAlias alias);
    /// Reserve contiguous hot records before the first process registration.
    /// Additional processes use stable overflow storage; existing records are
    /// never relocated. This is an optional setup hint, not a process limit.
    void reserve_process_capacity(std::size_t capacity);
    [[nodiscard]] ProcessId add_process(Process process);
    /// Register and validate one process topology without retaining its
    /// program or allocating its execution frame. The interpreter becomes a
    /// validation-only builder and cannot subsequently start.
    [[nodiscard]] ProcessId validate_process(const Process& process);
    [[nodiscard]] std::uint32_t add_module_path(ModulePath path);
    [[nodiscard]] std::uint32_t add_module_timing_check(
        ModuleTimingCheck check);
    /// Atomically replace topology-compatible module-path and timing-check
    /// timing. After start this is legal only from a scheduler safe-point hook.
    /// Pending path writes and timing-check history retain their old state;
    /// future events use the replacement timing.
    void reannotate_module_timing(
        std::span<const ModulePath> paths,
        std::span<const ModuleTimingCheck> checks);

    /// Live delay table of one module path (IEEE 1800-2023 38.11).
    [[nodiscard]] std::vector<SimulationTick> module_path_delays(
        std::uint32_t path) const;
    /// Replace one module path's 1/2/3/6/12-value delay table, as
    /// vpi_put_delays does (IEEE 1800-2023 38.34). Returns false, changing
    /// nothing, when the scheduler is started and not at a safe point.
    [[nodiscard]] bool set_module_path_delays(
        std::uint32_t path, std::span<const SimulationTick> delays);

    /// Replace one module path's delay table and, when `reject` and `error`
    /// are not empty, its pulse reject and error tables (the scalar limits
    /// then no longer apply), as vpi_put_delays does (IEEE 1800-2023 38.32).
    /// Empty pulse tables keep the path's limits. Safe-point rule and
    /// exception as for set_module_path_delays.
    [[nodiscard]] bool set_module_path_timing(std::uint32_t path,
        std::span<const SimulationTick> delays,
        std::span<const SimulationTick> reject,
        std::span<const SimulationTick> error);
    /// Replace the transition delays of one driver process: its slotted
    /// delayed writes (WriteDelayed::slotted) use them from the next write
    /// on, as vpi_put_delays on a primitive does (IEEE 1800-2023 38.32). A
    /// pending write keeps its timing. Legal at any time; false, changing
    /// nothing, when the driver has no slotted write.
    [[nodiscard]] bool set_driver_delays(ProcessId process, TransitionDelays delays);
    /// The delays set_driver_delays gave a driver, if any.
    [[nodiscard]] std::optional<TransitionDelays> driver_delays(
        ProcessId process) const;
    /// Replace the twelve transition delays and pulse limits of one driver
    /// process's WriteNetDelay operations from the next write on, as
    /// vpi_put_delays on an intermodule path or delay device does (IEEE
    /// 1800-2023 38.32); a pending change keeps its timing. Legal at any
    /// time; false, changing nothing, when the driver has no such write.
    [[nodiscard]] bool set_net_delay_table(
        ProcessId process, const NetDelayTable& table);
    /// The table set_net_delay_table gave a driver, if any.
    [[nodiscard]] std::optional<NetDelayTable> net_delay_table(
        ProcessId process) const;
    /// Declares `copy` a group-mode load copy of `net` (SDF INTERCONNECT
    /// delays per source: the copy's drivers read shadows of the net that the
    /// sources write, not the net), with the copy drivers `groups`. A foreign
    /// write of `net` (deposit, force or release by VPI, VHPI, ACC, TF, Tcl
    /// or the API: the Interpreter's deposit_signal, force_signal,
    /// release_signal, their slice forms and deposit_kernel_signal) is a
    /// change no source made, so no shadow carries it; it reaches the copy
    /// after the smallest delay of the groups for each bit's transition
    /// (IEEE 1076.4-2000 6.4.1 treats a change no source made the same way),
    /// as a deposit, force or release of the copy. A write that matures at
    /// or after a pending one's time replaces it (transport), and pulse
    /// limits do not apply. Legal before the simulation starts.
    void add_foreign_net_delay_load(SignalId net, SignalId copy,
        std::vector<ForeignNetDelayGroup> groups);
    /// Splits a load port of the running simulation (NetDelayGraft): the
    /// copy starts at the net's current value, the load's processes read it
    /// from their next resume on and are sensitive to it, and the copy's
    /// drivers write it with their tables from the net's next change; changes
    /// pending anywhere keep their time. Legal only at a scheduler safe point
    /// after start. The static kernel adds the copy's slot and the drivers'
    /// members and rebinds the load's reads without the evaluator running
    /// (a re-plan of the affected units, as respecialize_silent does for
    /// slot flags). The added processes take the ids that follow the run's
    /// own (fork children and foreign drivers included), written back into
    /// `graft.processes`. False, changing nothing and with `reason` set, when
    /// the engine cannot take the graft.
    [[nodiscard]] bool graft_net_delay(
        NetDelayGraft& graft, std::string& reason);
    /// Gives a gate's driver a delay slot (IEEE 1800-2023 38.32, 28.16): its
    /// delay-free writes of the output, WriteUpdate or WriteUpdateSlice, run
    /// as the WriteNetDelay operations of `rewritten` (the driver's program
    /// with those operations replaced in place, the same count and
    /// positions) from the next write on. Legal only at a scheduler safe
    /// point after start. The static kernel rewrites the member and takes it
    /// out of its partition's pass; the evaluator replaces the program.
    /// Afterwards set_net_delay_table retimes the driver. False, changing
    /// nothing and with `reason` set, when the engine cannot take it.
    [[nodiscard]] bool split_gate_delay(
        ProcessId process, Process rewritten, std::string& reason,
        bool dry_run = false);
    /// Replace the limits of one module timing check, as vpi_put_delays does
    /// (IEEE 1800-2023 38.32). Returns false, changing nothing, when the
    /// scheduler is started and not at a safe point; throws
    /// std::invalid_argument when the limits do not suit the check.
    [[nodiscard]] bool set_module_timing_check_limits(
        std::uint32_t check, std::span<const std::int64_t> limits);

    /// Live limits of one module timing check, in scheduler ticks (IEEE
    /// 1800-2023 38.10).
    [[nodiscard]] std::vector<std::int64_t> module_timing_check_limits(
        std::uint32_t check) const;
    /// Install (or with an empty function remove) the callback that runs when
    /// a module timing check is violated, before its notifier toggles. It
    /// receives the check's index (IEEE 1800-2023 38.36.3).
    void set_module_timing_violation_hook(
        std::function<void(std::uint32_t)> hook);

    struct VitalTimingReannotation {
        ProcessId process { };
        InstructionIndex instruction { };
        bool timing_check { };
        std::array<SimulationTick, 6> values { };
        std::size_t value_count { };
    };

    /// Atomically replace topology-compatible VITAL delay/check timing. After
    /// start this is legal only from a scheduler safe-point hook. Pending
    /// projected writes retain their scheduled time; timing-check state is
    /// preserved unless reset_timing_state is requested.
    void reannotate_vital_timing(
        std::span<const VitalTimingReannotation> annotations,
        bool reset_timing_state = false);

    /// Restrict all HDL file operations to paths below this root. Must be set
    /// before start; an empty root leaves file operations disabled.
    void set_file_root(std::filesystem::path root);

    /// The VPI multichannel descriptor routines (IEEE 1800-2023 38.24-38.29)
    /// on the table $fopen uses. Channel 1 and the preopened descriptors
    /// belong to the caller.
    [[nodiscard]] FileHandle mcd_open(std::string_view name);
    /// Zero, or the descriptor bits left open.
    [[nodiscard]] FileHandle mcd_close(FileHandle descriptor);
    [[nodiscard]] std::optional<std::string> mcd_name(
        FileHandle descriptor) const;
    /// Write to the files named by the channel bits; false when one failed.
    [[nodiscard]] bool mcd_write(FileHandle descriptor, std::string_view text);
    [[nodiscard]] bool mcd_flush(FileHandle descriptor);

    /// Bound the operations interpreted from now on; past the bound the
    /// running process fails. Elaboration-time evaluation uses it so that a
    /// computation too large to elaborate is rejected rather than run.
    void set_operation_budget(std::uint64_t operations);

    /// Replace the ordered, simulation-owned Verilog/SystemVerilog plusargs.
    /// Arguments may include their conventional leading '+'. Must be set
    /// before start.
    void set_plusargs(std::span<const std::string> plusargs);
    /// What $reset_count and $reset_value read (IEEE 1800-2023 D.8). The
    /// embedder sets them after a reset or a restart; a run that never
    /// resets reads zero.
    void set_reset_values(std::int32_t count, std::int32_t value) noexcept;

    /// Set the duration represented by one scheduler tick. Must be set before
    /// start and is used by SystemVerilog $timeformat/%t services.
    void set_time_resolution_femtoseconds(std::uint64_t femtoseconds);

    /// Replace one process's reference evaluator with an alternate executor.
    ///
    /// The interpreter remains the sole scheduler and signal store. Installation
    /// is allowed only before start and at most once per process.
    void set_process_executor(
        ProcessId process, std::unique_ptr<ProcessExecutor> executor);

    /// Prepare an alternate executor without delaying simulation startup.
    /// The interpreter remains active until ready() reports completion, then
    /// migrates the lexical frame and installs the prepared executor at the
    /// next scheduler-owned process boundary. Both callbacks must be safe to
    /// destroy without first being invoked.
    void set_deferred_process_executor(
        ProcessId process,
        std::function<bool()> ready,
        std::function<std::unique_ptr<ProcessExecutor>()> take);
    /// The explicit contract enables only the capabilities the caller
    /// promises. The default overload keeps arbitrary factories conservative.
    void set_deferred_process_executor(
        ProcessId process,
        std::function<bool()> ready,
        std::function<std::unique_ptr<ProcessExecutor>()> take,
        DeferredProcessExecutorContract contract);

    /// Discard an installed or deferred alternate executor before start so the
    /// reference interpreter resumes ownership of the process. Returns false
    /// after start, when executor replacement can no longer be made safely.
    [[nodiscard]] bool clear_process_executor(ProcessId process);

    /// Install every ready deferred executor before simulation starts. This
    /// preserves eager debugger and fork semantics for callers that explicitly
    /// wait for native compilation while allowing other callers to overlap
    /// materialization with scheduler execution.
    void materialize_ready_process_executors();

    void start();
    /// Starts like start() but leaves the time-zero processes unstarted
    /// until the next start() (a no-op on a started interpreter): the
    /// kernel initializes and the processes that wait on a signal are
    /// armed, so a host can wake a generated service process by a deposit
    /// and run the scheduler for it alone before the design's own
    /// processes begin.
    void start_holding_time_zero();
    /// Runs the processes a host woke at the current time (a generated
    /// service process it deposited to) and returns when none is ready.
    /// After the end of simulation (finals ran) this is a final activation
    /// at the end time, after the final blocks like them (IEEE 1800-2023
    /// 9.2.3): only the processes the host woke run, and the run stays
    /// finished.
    RunResult run_host_activation();
    /// Starts for a restore (IEEE 1800-2023 D.9): builds the tables that
    /// restore_run_state() fills in but runs no user code, neither package
    /// static initializers nor the first run of any process, and queues no
    /// process at time zero; the saved state then supplies all of that. A
    /// no-op on a started interpreter.
    void start_for_restore();
    /// The embedder's start-of-simulation step (IEEE 1800-2023 38.36.3
    /// cbStartOfSimulation): start() calls it once static initialization,
    /// package variable initializers included, is complete (6.21, 10.5) and
    /// before the time-zero cycle begins, so work it schedules runs in that
    /// cycle. Not called by start_for_restore().
    void set_start_of_simulation_hook(std::function<void()> hook);
    /// Installs the factory reset_run_state() takes the time-zero state from:
    /// it returns a fresh interpreter of the same design, configured exactly
    /// as this one was before start (hooks excluded). Without one the run
    /// cannot reset.
    void set_reset_source(std::function<std::unique_ptr<Interpreter>()> source);
    /// The twin a reset builds runs the package and class static initializers
    /// of the design (start); with this set it runs them through this
    /// interpreter's class hooks (allocation, properties, methods), so the
    /// objects they make are the application's own, which the embedder cleared
    /// just before the reset (IEEE 1800-2023 D.8, 6.21).
    void set_reset_twin_class_hooks(bool share) noexcept;
    /// The embedder's share of the scheduler events of a run state (#128): the
    /// owners of the event kinds the embedder registered ("vpi_value_control",
    /// "uvm_objection", ...), bound into every save and restore beside the
    /// engine's own, and the takers of a restored cancelable event's handle
    /// (the same contract as Interpreter::Impl::bind_restored_event_handle).
    /// Without a host an event of an embedder kind refuses the save
    /// (FSIM-SAVE-0005), because its payload points outside the engine.
    struct RunStateEventHost {
        std::function<void(SchedulerEventOwners&)> bind_owners;
        std::function<bool(std::string_view kind, std::uint64_t key,
            const ScheduledTaskHandle& handle)>
            bind_handle;
    };
    void set_run_state_event_host(RunStateEventHost host);
    /// True only while the kernel executes ordered package-static startup
    /// actions, including tagged SimIR processes and registered callbacks.
    [[nodiscard]] bool package_static_initialization_active() const noexcept;
    [[nodiscard]] RunResult
    run(std::optional<SimulationTick> until = std::nullopt);
    /// Discard ordinary pending work and execute final processes exactly once.
    /// This is used by an external finish request after the scheduler reaches
    /// its safe stopped boundary.
    [[nodiscard]] RunResult finish();

    /// True when reset_run_state() can restore this run: a reset source is
    /// installed (set_reset_source) and every live
    /// process's executor is state-free (ProcessExecutor::run_state_free).
    /// Both engines reset: the reference interpreter and the v4 static kernel
    /// take the whole mutable state (the scheduler, signal and driver values,
    /// process and thread states, containers, strings and files, the kernel's
    /// slots, waiters and timers) from the time-zero image of the twin.
    [[nodiscard]] bool can_reset_run_state() const noexcept;
    /// Puts the run back at time zero, so that the next run() initializes the
    /// design again (IEEE 1800-2023 D.8, IEEE 1076-2019 20.9): builds the twin
    /// from the reset source, starts it with start_for_restore(), and restores
    /// its state into this run through restore_run_state(). $reset_count and
    /// $reset_value are kept. False when can_reset_run_state() is false or the
    /// restore fails; after a failed restore the run must be discarded.
    [[nodiscard]] bool reset_run_state();
    /// Why the last reset_run_state() failed (an FSIM-SAVE line), else empty.
    [[nodiscard]] const std::string& reset_failure() const noexcept;
    /// Applies to `target` the requests this run's foreign interfaces made
    /// that change its plan or time-zero state (watch_signal_changes,
    /// watch_signal_transactions, open_external_observation,
    /// note_external_signal_write, graft_net_delay), those made before start
    /// (`after_start` false) or after it (true), in the order they were
    /// made. A reset replays them onto its twin, so the twin has this run's
    /// plan. False, with `reason`, when a graft cannot take the ids it had.
    [[nodiscard]] bool replay_plan_onto(
        Interpreter& target, bool after_start, std::string& reason) const;
    /// While set, a split or graft made on this run is a replay of an earlier
    /// run's (a restart rebuilds the run-time plan of the saved run before
    /// the saved state replaces the tables, IEEE 1800-2023 D.9): the
    /// safe-point test is waived because nothing has run, and the request is
    /// not journaled a second time. The embedder sets it around the replay.
    void set_plan_replay(bool replaying) noexcept;

    /// Saves the run state at a save point (#128; IEEE 1800-2023 D.9, IEEE
    /// 1076-2019 20.7): adds the engine's sections to `image` (run_state_image.hpp).
    /// Sections: "sched.state" (time, delta, every pending event as a registered
    /// kind), "host.run", "host.signals", "host.objects", "host.pending",
    /// "host.processes.<n>" (reference processes, chunks of 4096) and, for a
    /// compiled run, "kernel.values" and "kernel.state" (registers, thread
    /// slots, timers, pending writes, owned signals). Each has its own version;
    /// see run_state_sections.hpp for the registry the application and foreign
    /// interfaces use for theirs. Returns false, with `image` unchanged in
    /// its engine sections, when the run cannot be saved now. `report` lists
    /// each reason with its FSIM-SAVE code; `report.deferrable` is true when
    /// every reason is work of the current time slot that drains before the
    /// slot ends, so the request waits instead of failing (owner decision 2).
    bool record_run_state(RunStateImage& image) const;
    bool record_run_state(RunStateImage& image, RunStateReport& report) const;
    /// Restores the sections record_run_state() wrote into this interpreter,
    /// which must be built from the same design and not yet run, and brings it
    /// to the saved point: pending events are re-created, kernel doorbells are
    /// re-armed, and run() continues from the saved time. False, with `report`
    /// listing each reason (FSIM-SAVE-0008..0010), when a section is missing,
    /// has another version or does not fit the design. After a false return
    /// the interpreter may be half restored and must be discarded.
    [[nodiscard]] bool restore_run_state(const RunStateImage& image);
    [[nodiscard]] bool restore_run_state(
        const RunStateImage& image, RunStateReport& report);
    /// The xxh64 over the sections record_run_state() would write now: equal
    /// states have equal hashes, and saving twice from one point gives the
    /// same hash. Zero when the run cannot be saved.
    [[nodiscard]] std::uint64_t run_state_hash() const;
    /// True for the section names the engine owns ("sched.", "host.",
    /// "kernel."), so a driver can tell its own sections from the engine's.
    [[nodiscard]] static bool owns_run_state_section(std::string_view name) noexcept;

    /// Deposit immediately and activate sensitive processes in the next delta.
    /// A systemverilog origin domain (a VPI write from a calltf) wakes
    /// SystemVerilog processes in the current Active region instead
    /// (IEEE 1800-2023 38.34).
    void deposit_signal(SignalId signal, PackedLogic4 value,
        ProcessSchedulingDomain origin_domain = ProcessSchedulingDomain::generic);

    /// A foreign program's driver (VHPI 22.5.4, 22.6, 23.34): the driver
    /// `process` has for `signal`, the process being the placeholder of a
    /// foreign architecture. A projected waveform edits the driver's
    /// projected output waveform (IEEE 1076-2019 10.5.2.2); `offset` selects
    /// the bits of a basic signal's driver inside the signal.
    void schedule_host_driver_waveform(ProcessId process, SignalId signal,
        const std::vector<ProjectedWaveformValue>& elements,
        std::optional<std::size_t> offset, SimulationTick rejection,
        ProjectedDelayMode mode);
    /// Schedule the null transaction of a host driver `delay` ticks from now
    /// (IEEE 1076-2019 10.5.2.2, 23.34): the driver stops contributing to the
    /// guarded signal once it matures and its later transactions are
    /// cancelled. Returns false, scheduling nothing, for a signal that has no
    /// resolution (an unguarded signal cannot be disconnected).
    [[nodiscard]] bool disconnect_host_driver(
        ProcessId process, SignalId signal, SimulationTick delay);
    /// Force a host driver at once, or in the next update phase when
    /// `propagate` is set (IEEE 1076-2019 22.5.4).
    void force_host_driver(ProcessId process, SignalId signal,
        PackedLogic4 value, std::size_t offset, bool propagate);
    /// Deposit into a host driver at once; the bits of a forced driver keep
    /// their value (IEEE 1076-2019 22.5.4 vhpiDeposit).
    void deposit_host_driver(ProcessId process, SignalId signal,
        PackedLogic4 value, std::size_t offset);
    /// Release a host driver (IEEE 1076-2019 22.5.4).
    void release_host_driver(ProcessId process, SignalId signal,
        std::size_t offset, std::size_t width);
    /// A new process with the program of `model` that never runs: the
    /// owner of the drivers of one more process (or of a driver with no
    /// process) that a foreign program creates in the instance whose
    /// placeholder process is `model` (IEEE 1076-2019 23.6). It can be
    /// added before or after the simulation starts.
    [[nodiscard]] ProcessId add_foreign_driver_owner(ProcessId model);
    /// Gives `process` a driver of `signal` at the driver's initial value
    /// (IEEE 1076-2019 14.7.2), unless it has one: a driver vhpi_create
    /// makes is a source of the signal from its creation (23.6).
    void add_host_driver(ProcessId process, SignalId signal);

    /// Override the visible value while preserving subsequently driven values.
    /// Releasing the force publishes the most recent underlying driven value.
    void force_signal(SignalId signal, PackedLogic4 value,
        ProcessSchedulingDomain origin_domain = ProcessSchedulingDomain::generic);
    void release_signal(SignalId signal,
        ProcessSchedulingDomain origin_domain = ProcessSchedulingDomain::generic);
    /// Procedural continuous assignment of a register from a host (IEEE
    /// 1364-2005 9.3.1): the value overrides the register's procedural writes
    /// until the deassign, which leaves it as the stored value; a force wins
    /// over it, and releasing that force reveals it again.
    void assign_signal(SignalId signal, PackedLogic4 value,
        ProcessSchedulingDomain origin_domain = ProcessSchedulingDomain::generic);
    void deassign_signal(SignalId signal,
        ProcessSchedulingDomain origin_domain = ProcessSchedulingDomain::generic);
    void force_signal_slice(
        SignalId signal, PackedLogic4 value, std::size_t offset);
    void release_signal_slice(
        SignalId signal, std::size_t offset, std::size_t width);
    /// True when a force (not a procedural continuous assign) holds a bit.
    [[nodiscard]] bool signal_is_forced(SignalId signal) const;
    /// True when a procedural continuous assign (acc_set_value accAssignFlag,
    /// IEEE 1364-2005 9.3.1) holds a bit.
    [[nodiscard]] bool signal_is_assigned(SignalId signal) const;
    /// True when a force or an assign overrides the stored value, so reads
    /// return the effective value.
    [[nodiscard]] bool signal_has_override(SignalId signal) const;

    /// Schedule an external drive in the update phase of an absolute timestamp.
    void schedule_signal_at(SignalId signal, PackedLogic4 value,
        SimulationTick time, StableOrder order = 0);

    /// Schedule an external drive relative to the scheduler's current time.
    void schedule_signal_after(SignalId signal, PackedLogic4 value,
        SimulationTick delay, StableOrder order = 0);

    /// After start(), materialize a signal's current/previous/original-driver
    /// and pending state before installing a late observer. Only dependent
    /// graph kernels are demoted. This does not publish pending transactions
    /// early or advance simulation. May throw on allocation failure; demoted
    /// certificates remain invalid and the operation may be retried. Before
    /// start, initial state is already public; the caller must expose its new
    /// registration to the startup observation inventory before calling start.
    void prepare_signal_observation(SignalId signal);
    /// Every later change of these signals reaches the scheduler's signal
    /// state in its own region (value-change observers such as VPI and VHPI
    /// callbacks and traces see it). A signal the v4 kernel owns is published
    /// from now on (docs/engine-v4-semantics.md S19); other signals already
    /// are. A watch lasts for the rest of the run.
    void watch_signal_changes(std::span<const SignalId> signals);
    /// watch_signal_changes for the signals that hold a container object's
    /// elements (its packed bridge, element or aggregate signals, through
    /// slice views): every later change of an element written in the v4
    /// kernel, by a member or a foreign deposit, reaches the host object
    /// and the container change hook, as in the reference engine (a VPI
    /// value change callback on a memory word, IEEE 1800-2023 38.36.1).
    void watch_container_object_changes(ContainerObjectId object);
    /// The packed bridge signal that holds a container object's elements
    /// (element 0 in its most significant bits), when the object has one.
    [[nodiscard]] std::optional<SignalId> container_bridge_signal(
        ContainerObjectId object) const;
    /// watch_signal_changes, and the v4 kernel also reports every assignment
    /// to these signals that leaves the value as it is, a transaction
    /// without an event (IEEE 1076-2019 14.7.3.1), to the signal transaction
    /// hook. A watch lasts for the rest of the run.
    void watch_signal_transactions(std::span<const SignalId> signals);
    /// A client outside the processes (a foreign library) observes delta
    /// cycles or time steps from now on: the v4 kernel runs every round as a
    /// scheduler delta (docs/engine-v4-semantics.md S19).
    void open_external_observation();
    /// The v4 kernel owns the signal: its scheduler copy is current only
    /// while watched or after an observation barrier
    /// (prepare_signal_observation).
    [[nodiscard]] bool signal_kernel_owned(SignalId signal) const noexcept;
    /// A client outside the processes is about to write, force or release
    /// the signal through the scheduler (VPI and VHPI puts): the v4 kernel
    /// takes the committed value into a signal it owns and rescans a signal
    /// it reads.
    void note_external_signal_write(SignalId signal);
    /// The net's value before a foreign write, when the net has group-mode
    /// load copies (add_foreign_net_delay_load); nothing otherwise (the
    /// usual case: one map test). foreign_net_delay_after follows the write
    /// with the delayed write of the copies. `offset` and `width` are the
    /// bits the write covered.
    [[nodiscard]] std::optional<PackedLogic4> foreign_net_delay_before(
        SignalId signal) const;
    void foreign_net_delay_after(SignalId signal, ForeignNetDelayKind kind,
        const std::optional<PackedLogic4>& before, std::size_t offset,
        std::size_t width);
    /// The delayed write of the copies once the net holds its value after
    /// the write: at once, or for a release of a net the v4 kernel owns, once
    /// its drivers have run again (rerun_kernel_drivers).
    void foreign_net_delay_settle(SignalId signal, ForeignNetDelayKind kind,
        const PackedLogic4& before, std::size_t offset, std::size_t width);
    void foreign_net_delay_mature(SignalId signal, std::size_t index,
        ForeignNetDelayKind kind,
        const std::vector<std::pair<std::uint32_t, Logic4>>& bits);
    /// A foreign deposit straight into the slot of a signal the v4 kernel
    /// owns, as an update event of the current time step: false when the
    /// kernel does not hold the signal's value alone (not owned, forced,
    /// published to the scheduler, or computed on read), and the caller
    /// deposits through the scheduler.
    [[nodiscard]] bool deposit_kernel_signal(SignalId signal,
        const PackedLogic4& value);
    /// The value of a signal the v4 kernel owns, read from its slot without
    /// an observation barrier; nullopt under the same conditions.
    [[nodiscard]] std::optional<PackedLogic4> kernel_signal_value(
        SignalId signal) const;
    /// The value of a design container object the v4 kernel owns (through
    /// its packed bridge signal, its element signals or its own element
    /// storage), read from the kernel without an observation barrier: no
    /// host copy is refreshed and no change is published, so a foreign
    /// interface may call it while it holds its own locks. nullopt when the
    /// kernel does not hold the whole value (as kernel_signal_value), and
    /// the caller takes container_object_value_snapshot().
    [[nodiscard]] std::optional<ContainerValue> kernel_container_object_value(
        ContainerObjectId object) const;
    /// A mailbox or semaphore by the handle a variable holds; nullopt for a
    /// value that names neither.
    [[nodiscard]] std::optional<SynchronizationObject> synchronization_object(
        std::uint64_t handle) const;
    /// After a foreign release of a net the kernel owns: the members that
    /// drive it run again, so it returns to the drivers' value (IEEE
    /// 1800-2023 10.6.2). No effect on any other signal.
    void rerun_kernel_drivers(SignalId signal);

    /// Return a live reference; its backing remains on public storage because
    /// callers may retain it beyond this call.
    [[nodiscard]] const PackedLogic4& signal_value(SignalId signal) const;
    /// Return a value snapshot without retaining an observation reference.
    [[nodiscard]] PackedLogic4 signal_value_snapshot(SignalId signal) const;
    /// Return the underlying effective driven value without applying a force.
    /// This keeps public driver state distinct from the visible signal value.
    /// The returned reference pins its backing on public storage.
    [[nodiscard]] const PackedLogic4& stored_signal_value(
        SignalId signal) const;
    /// Return an underlying-value snapshot without retaining a reference.
    [[nodiscard]] PackedLogic4 stored_signal_value_snapshot(
        SignalId signal) const;
    [[nodiscard]] const std::string&
    string_object_value(StringObjectId object) const;
    void deposit_string_object(
        StringObjectId object, std::string_view value);
    /// Return the current stored container value by reference. Its backing and
    /// element storage remain stable, and aliased signal values refresh before
    /// current-change observers run. Use the snapshot getter when a detached
    /// value copy is preferable.
    [[nodiscard]] const ContainerValue&
    container_object_value(ContainerObjectId object) const;
    /// Return a container snapshot without pinning aliased signals.
    [[nodiscard]] ContainerValue container_object_value_snapshot(
        ContainerObjectId object) const;
    void deposit_container_object(
        ContainerObjectId object, ContainerValue value);
    void deposit_container_object_element(
        ContainerObjectId object, std::size_t ordinal,
        PackedLogic4 value);
    /// Return one process-owned driver slot. For an unresolved signal this is
    /// the single underlying driven value.
    [[nodiscard]] PackedLogic4 driver_value(
        ProcessId process, SignalId signal) const;
    /// Whether null transactions turned off `process`'s driver of every bit
    /// of [offset, offset + width) of `signal` (IEEE 1076-2019 10.5.2.2,
    /// 14.7.2): the driver has no value then.
    [[nodiscard]] bool driver_disconnected(ProcessId process, SignalId signal,
        std::uint32_t offset, std::uint32_t width) const;
    /// The transactions the projected output waveform of `process`'s driver
    /// of the bits [offset, offset + width) of `signal` holds beyond the one
    /// that determines its current value, in time order (IEEE 1076-2019
    /// 14.7.2, VHPI 19.12.1 vhpiTransactions). Every scalar of the range is
    /// scheduled together, so a transaction exists at each time any of its
    /// bits has one; a bit with none at that time keeps the value of its
    /// latest earlier transaction, else its current driver value. A
    /// transaction that applies in the update phase of the cycle it was
    /// scheduled in is not pending any more and is not listed.
    [[nodiscard]] std::vector<DriverTransaction> driver_transactions(
        ProcessId process, SignalId signal, std::size_t offset,
        std::size_t width) const;
    /// Return the strongest currently active zero and one contributions used
    /// by resolved-port observers such as extended VCD.
    [[nodiscard]] DriveStrength signal_strength(SignalId signal) const;
    /// Return the static design process that owns a dynamic fork child. Static
    /// processes map to themselves.
    [[nodiscard]] ProcessId design_process(ProcessId process) const;
    /// Return the immutable SimIR program owned by a static design process.
    /// The reference remains valid for the lifetime of this interpreter.
    [[nodiscard]] const Process& process_program(ProcessId process) const;
    /// The net whose user-defined nettype's resolution function `process`
    /// runs (IEEE 1800-2023 6.6.7; a frame's vpiOrigin, 37.43 detail 6).
    [[nodiscard]] std::optional<SignalId> nettype_resolution_signal(
        ProcessId process) const;
    /// process_program with the static sensitivity a kernel member's program
    /// gave up to the kernel's tables restored, for a lazy MIPD graft plan
    /// (graft_net_delay). Built on first request and kept until a graft.
    [[nodiscard]] const Process& sensitive_process_program(ProcessId process);
    /// Return the next SimIR instruction for a scheduler-owned process.
    [[nodiscard]] InstructionIndex process_instruction(
        ProcessId process) const;
    /// Enable and inspect lightweight interpreter-operation accounting for a
    /// process awaiting adaptive native promotion. Accounting is local to the
    /// scheduler thread and is sampled only from deferred-executor callbacks.
    void track_process_interpreter_operations(ProcessId process);
    [[nodiscard]] std::uint64_t process_interpreter_operations(
        ProcessId process) const;
    /// Return the top-level dynamic child that owns an execution descendant.
    /// Static processes map to themselves. This is the stable identity of one
    /// overlapping temporal attempt across nested lowering forks.
    [[nodiscard]] ProcessId dynamic_process_root(ProcessId process) const;
    /// Kill every live dynamic fork attempt owned by one of the selected
    /// static design processes. Static processes remain available for later
    /// assertion re-enablement.
    void kill_dynamic_processes(
        std::span<const ProcessId> design_processes);
    /// A concurrent assertion whose running attempts fail at the end of
    /// simulation (IEEE 1800-2017 16.12.2).
    struct SvaObligation {
        ProcessId process { };
        std::size_t failures { };
        std::size_t holds { };
        /// The start tick of each failing and each holding attempt.
        std::vector<std::int64_t> failure_starts;
        std::vector<std::int64_t> hold_starts;
    };
    /// End every concurrent-property monitor and report, per process, the
    /// running attempts that fail and those that hold on the truncated
    /// trace.
    [[nodiscard]] std::vector<SvaObligation> finish_sva_monitors();
    /// Stop the running attempts of the selected processes' monitors
    /// ($assertkill, IEEE 1800-2017 20.12).
    void kill_sva_monitors(std::span<const ProcessId> design_processes);
    /// Install the action controls (SvaActionFilter); every monitor takes
    /// them now and at its creation.
    void set_sva_action_filter(SvaActionFilter filter);
    /// The action controls changed: every monitor takes them again, so the
    /// next outcome each pops runs or skips its action block accordingly.
    void refresh_sva_actions();
    /// The control sites of the design's processes, numbered: element i
    /// is the site whose control word set_assertion_control(i, ...) sets.
    /// The first call numbers them.
    [[nodiscard]] const std::vector<AssertionControlSite>&
        assertion_control_sites();
    /// Set the control word of control site `index` (20.11): the reads
    /// that follow, in generated code too, see it.
    void set_assertion_control(std::size_t index, std::int64_t word);
    /// Stop only the running attempts that started at `start_time` in the
    /// selected processes' monitors (vpiAssertionKill with an attempt start
    /// time, IEEE 1800-2023 39.5.2); true when one was running.
    bool kill_sva_attempt(
        std::span<const ProcessId> design_processes, std::int64_t start_time);
    /// The start tick of the attempt behind the outcome the process's monitor
    /// reported last (attemptStartTime, 39.4.2).
    [[nodiscard]] std::optional<std::int64_t> sva_outcome_start(
        ProcessId design_process) const;
    /// The source position (1-based line and column) of the boolean
    /// expression that rejected the attempt behind the failure the process's
    /// monitor reported last (failExpr, 39.4.2); none when unknown.
    [[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>>
    sva_outcome_fail(ProcessId design_process) const;
    void set_sva_attempt_hook(SvaAttemptHook hook);
    void set_sva_disabled_hook(SvaDisabledHook hook);
    /// Turn the inert attempt-start reports of the concurrent assertion
    /// evaluators (Display::inert) into reports; effective for every attempt
    /// that starts after the call, at any point of the run.
    void bind_assertion_start_reports() noexcept;
    void set_sva_step_hook(SvaStepHook hook);
    /// vpiAssertionEnableStep / vpiAssertionDisableStep for the attempt that
    /// starts at `start_time` in the selected processes' monitors (39.5.2).
    /// A request made before a monitor exists, or before the attempt
    /// starts, waits for it. False when the attempt has already started:
    /// its stepping mode cannot change any more (39.5.2), and nothing
    /// changes.
    [[nodiscard]] bool set_sva_step(std::span<const ProcessId> design_processes,
        std::int64_t start_time, bool enable);
    /// `newer_activations` selects an outer activation of a recursive
    /// callable: the number of activations of the same callable entered
    /// after the one read (VHPI stack frames, IEEE 1076-2019 19.10).
    [[nodiscard]] PackedLogic4 read_debug_local(ProcessId process,
        std::size_t local_index, std::size_t newer_activations = 0U) const;
    /// The register of slot `slot` of the process program's call-result
    /// table (`Process::call_results`): the value the callee's result
    /// register holds at its frame exit.
    [[nodiscard]] PackedLogic4 read_call_result(
        ProcessId process, std::size_t slot) const;
    /// The register of slot `local_index` of the process program's
    /// frame-local table (`Process::frame_locals`): a formal or local of a
    /// VHDL function, or the parameter of a `for` loop; `newer_activations`
    /// as for read_debug_local.
    [[nodiscard]] PackedLogic4 read_frame_local(ProcessId process,
        std::size_t local_index, std::size_t newer_activations = 0U) const;
    /// The table of a process program that read_local_register reads.
    enum class LocalTable : std::uint8_t {
        debug,
        call_result,
        frame,
    };
    /// The common body of read_debug_local, read_call_result and
    /// read_frame_local.
    [[nodiscard]] PackedLogic4 read_local_register(ProcessId process,
        std::size_t local_index, std::size_t newer_activations,
        LocalTable table) const;
    /// Deposit a value into a debug local (a variable of a process): the
    /// value has the local's width (VHPI vhpi_put_value, IEEE 1076-2019
    /// 22.5.2).
    void write_debug_local(ProcessId process, std::size_t local_index,
        const PackedLogic4& value, std::size_t newer_activations = 0U);
    /// Forces bits [offset, offset + width) of a debug local to those of
    /// `value` (the local's width) and writes them: variable assignments
    /// and the association of out or inout formals leave forced bits as
    /// they are, and so does a deposit, until they are released (IEEE
    /// 1076-2019 22.5.2).
    void force_debug_local(ProcessId process, std::size_t local_index,
        const PackedLogic4& value, std::size_t offset, std::size_t width);
    /// Releases bits [offset, offset + width) of a debug local, which keeps
    /// its value (22.5.2).
    void release_debug_local(ProcessId process, std::size_t local_index,
        std::size_t offset, std::size_t width);
    /// Whether any of bits [offset, offset + width) of a debug local is
    /// forced.
    [[nodiscard]] bool debug_local_forced(ProcessId process,
        std::size_t local_index, std::size_t offset, std::size_t width) const;
    /// Starts or stops reporting the changes of a debug local's value to
    /// the DebugLocalChangeHook (IEEE 1076-2019 21.3.2.2).
    void watch_debug_local(
        ProcessId process, std::size_t local_index, bool watched);
    /// `newer_activations` selects an outer activation of a recursive
    /// callable, whose strings the newer ones saved on entry.
    [[nodiscard]] std::string read_debug_string_local(ProcessId process,
        std::size_t local_index, std::size_t newer_activations = 0U) const;
    /// Deposit a value into a string debug local (a frame's automatic
    /// string, vpi_put_value on it, IEEE 1800-2023 37.43, 38.34).
    void write_debug_string_local(ProcessId process, std::size_t local_index,
        std::string_view value, std::size_t newer_activations = 0U);
    /// Triggers the event object a handle register holds (an automatic or
    /// callable event, IEEE 1800-2023 15.5.1, 37.43): its waiters wake as for
    /// `-> e`. False for a null or foreign handle, which names no event
    /// (15.5.5).
    bool trigger_dynamic_event(std::uint64_t handle);
    /// A new event object, as `DynamicEventOperation::create` makes one: the
    /// handle a register or an element of an event array holds (IEEE
    /// 1800-2023 15.5, 7.4).
    [[nodiscard]] std::uint64_t create_dynamic_event();
    /// Replace a container debug local of the same type (vpi_put_value_array
    /// on a frame's automatic container, IEEE 1800-2023 38.34).
    void write_debug_container_local(ProcessId process,
        std::size_t local_index, const ContainerValue& value);
    /// The string or container a call-result slot of a function returning
    /// one holds (IEEE 1800-2023 37.42, 38.15), read at the frame's end.
    [[nodiscard]] std::string read_call_result_string(
        ProcessId process, std::size_t slot) const;
    [[nodiscard]] ContainerValue read_call_result_container(
        ProcessId process, std::size_t slot) const;
    [[nodiscard]] ContainerValue read_debug_container_local(
        ProcessId process, std::size_t local_index) const;
    /// True once a language-level Stop operation (`$finish` or equivalent) has
    /// executed. External scheduler stop requests do not set this flag.
    [[nodiscard]] bool stopped_by_design() const noexcept;
    /// True once a final procedure has executed `$stop`.
    [[nodiscard]] bool paused_in_final() const noexcept;
    /// The times of the future time steps that hold work, host and kernel
    /// timers together, in increasing order without repeats (IEEE 1800-2023
    /// 37.81). A debug query.
    [[nodiscard]] std::vector<SimulationTick> pending_times() const;
    /// The time and delta of the running time step, as the design and the
    /// host services its processes call observe them: the scheduler's, or,
    /// while a closed static kernel runs a time step of its own ahead of the
    /// scheduler, that step's time at its first delta, which the scheduler
    /// would have reported (docs/engine-v4-semantics.md S10).
    [[nodiscard]] SimulationTick current_time() const noexcept;
    [[nodiscard]] std::uint64_t current_delta() const noexcept;
    [[nodiscard]] Scheduler& scheduler() noexcept;
    [[nodiscard]] const Scheduler& scheduler() const noexcept;
    void set_signal_change_hook(SignalChangeHook hook);
    /// While a SignalChangeHook runs: the process whose write made the change
    /// (its blocking or nonblocking assignment, continuous assignment or
    /// force statement; on a resolved net, the driver whose contribution
    /// changed), empty when no process wrote it (a foreign write, a
    /// resolution of no driver's). The changes of one process in one region
    /// come in the order its statements executed (IEEE 1800-2023 4.6 a),
    /// b)); those of different processes in either engine's order (4.7).
    [[nodiscard]] std::optional<ProcessId> signal_change_writer() const noexcept;
    /// Publish the static kernel's changed outputs at once (none without a
    /// kernel): what its running process wrote reaches the host's signals
    /// and their observers before a foreign routine the process called
    /// continues.
    void publish_static_kernel_outputs();
    void set_native_signal_observation_required_hook(
        NativeSignalObservationRequiredHook hook);
    void set_native_signal_observation_any_hook(
        NativeSignalObservationAnyHook hook);
    void set_stored_signal_change_hook(StoredSignalChangeHook hook);
    void set_driver_change_hook(DriverChangeHook hook);
    void set_event_trigger_hook(EventTriggerHook hook);
    void set_force_statement_hook(ForceStatementHook hook);
    void set_signal_transaction_hook(SignalTransactionHook hook);
    void set_driver_transaction_hook(DriverTransactionHook hook);
    void set_debug_local_change_hook(DebugLocalChangeHook hook);
    void set_container_object_change_hook(ContainerObjectChangeHook hook);
    void set_container_element_change_hook(ContainerElementChangeHook hook);
#include "fsim/runtime/simir_scalar_interpreter.hpp"
    void set_execution_point_hook(ExecutionPointHook hook);
    void set_output_hook(OutputHook hook);
    /// Installs a sink for already-formatted text. A trusted sink may consume
    /// the supplied text and metadata without observing signal state. Before
    /// inspecting or mutating interpreter state, it must call
    /// prepare_output_callback_observation(). Unlike set_output_hook(), this
    /// skips the implicit signal-observation barrier before each callback.
    void set_trusted_text_output_hook(OutputHook hook);
    /// Prepare the complete signal-observation barrier before code that may
    /// inspect interpreter state, including code reached from a trusted text
    /// sink.
    void prepare_output_callback_observation();
    void set_report_hook(ReportHook hook);
    void set_coverage_sample_hook(CoverageSampleHook hook);
    void set_coverage_query_hook(CoverageQueryHook hook);
    void set_vhdl_psl_api_hook(VhdlPslApiHook hook);
    void set_coverage_control_hook(CoverageControlHook hook);
    void set_coverage_access_hook(CoverageAccessHook hook);
    /// Install or restore the dense simulation-owned code counters before
    /// start. An empty vector deliberately configures an empty table.
    void set_code_coverage_counters(std::vector<std::uint64_t> counters);
    [[nodiscard]] std::span<const std::uint64_t>
    code_coverage_counters() const noexcept;
    [[nodiscard]] bool code_coverage_counter_overflowed(
        ::fsim::runtime::CodeCoverageCounterId counter) const noexcept;
    [[nodiscard]] std::size_t code_coverage_overflow_count() const noexcept;
    [[nodiscard]] bool code_coverage_counters_configured() const noexcept;
    [[nodiscard]] bool code_coverage_collection_enabled() const noexcept;
    void set_code_coverage_collection_enabled(bool enabled) noexcept;
    [[nodiscard]] bool set_code_coverage_collection_enabled(
        std::span<const ::fsim::runtime::CodeCoverageCounterId> counters,
        bool enabled) noexcept;
    void reset_code_coverage_counters() noexcept;
    [[nodiscard]] bool reset_code_coverage_counters(
        std::span<const ::fsim::runtime::CodeCoverageCounterId> counters)
        noexcept;
    [[nodiscard]] std::optional<std::size_t>
    code_coverage_overflow_count(
        std::span<const ::fsim::runtime::CodeCoverageCounterId> counters)
        const noexcept;
    void set_code_coverage_overflow_hook(CodeCoverageOverflowHook hook);
    void set_system_command_hook(SystemCommandHook hook);
    void set_vcd_control_hook(VcdControlHook hook);
    void set_coverage_database_control_hook(
        CoverageDatabaseControlHook hook);
    /// A filter that reads no signal (observes_signals false) runs without
    /// the callback observation barrier that publishes every signal first.
    void set_fork_spawn_filter(ForkSpawnFilter filter, bool observes_signals = true);
    void set_class_allocate_context_hook(ClassAllocateContextHook hook);
    // The seed a new object's random number generator takes from the
    // allocating process's generator (IEEE 1800-2017 18.14.1), available
    // while the class allocation hook runs.
    [[nodiscard]] std::optional<std::uint64_t> take_class_allocation_seed();
    void set_class_property_read_hook(ClassPropertyReadHook hook);
    void set_class_property_write_hook(ClassPropertyWriteHook hook);
    void set_class_property_container_hook(ClassPropertyContainerHook hook);
    // The write-backs marked when_watched run only for an array property
    // the hook reports watched; with no hook (nothing watched) none runs.
    void set_class_container_watched_hook(ClassContainerWatchedHook hook);
    void set_class_method_call_context_hook(
        ClassMethodCallContextHook hook);
    void set_class_static_property_read_hook(
        ClassStaticPropertyReadHook hook);
    void set_class_static_property_write_hook(
        ClassStaticPropertyWriteHook hook);
    void set_class_static_method_call_context_hook(
        ClassStaticMethodCallContextHook hook);
    void set_dpi_function_call_hook(DpiFunctionCallHook hook);
    void set_dpi_site_hooks(DpiSiteHooks hooks);

private:
    // The time-zero processes' queueing, the tail of start().
    void queue_time_zero_processes(const std::vector<bool>& prearmed_static_waits);
    /// graft_net_delay without the journal entry.
    [[nodiscard]] bool graft_net_delay_planned(
        NetDelayGraft& graft, std::string& reason);
    [[nodiscard]] ProcessId
    add_process_impl(const Process& process, Process* owned_process);
    friend struct OwnedDriverDemotionTestAccess;
    friend struct NativeRegionAllocationTestAccess;
    friend struct PackageStaticStartupRuntimeTestAccess;
    friend struct SystemCBridgeTestAccess;
    friend class InterpreterProgramAccess;
    friend struct InterpreterEventAccess;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fsim::runtime::simir
