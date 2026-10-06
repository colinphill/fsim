// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fsim::runtime::simir {

namespace region_graph_detail {
class RegionGraphProgramBuilder;
struct RegionGraphStorageCensusAccess;
}

/// Capabilities requested by an observer, not an assertion that storage may
/// disappear. Execution must materialize the requested state before exposing it.
enum class RegionObservation : std::uint32_t {
    none = 0U,
    current = 1U << 0U,
    previous = 1U << 1U,
    drivers = 1U << 2U,
    pending = 1U << 3U,
    events = 1U << 4U,
    mutation = 1U << 5U,
    coverage = 1U << 6U,
    unknown = 1U << 7U,
};

constexpr RegionObservation operator|(RegionObservation left,
    RegionObservation right) noexcept
{
    return static_cast<RegionObservation>(static_cast<std::uint32_t>(left)
        | static_cast<std::uint32_t>(right));
}

enum class RegionDriverClass : std::uint8_t {
    undriven,
    single_whole,
    single_partial,
    disjoint_partial,
    resolved,
    unknown,
};

/// Unlike a scheduler phase, this describes the publication contract. In
/// particular VHDL projected transactions can never be folded into SV Active.
enum class RegionUpdateKind : std::uint8_t {
    generic,
    systemverilog_active,
    systemverilog_nba,
    vhdl_projected,
    systemc,
    mixed_or_unknown,
};

/// Distinguish output publication effects that share an update domain but
/// have different source-language visibility. This is part of the immutable
/// kernel mapping and cache identity.
enum class RegionOutputPublicationKind : std::uint8_t {
    update,
    blocking_immediate,
};

struct RegionSignalDescriptor {
    /// Zero is a valid empty VHDL array. Retain its identity and dependencies,
    /// but exclude its readers and writers from compiled region admission.
    std::uint32_t width { };
    ResolutionKind resolution { ResolutionKind::none };
    ValueKind value_kind { ValueKind::logic4 };
    bool implicit_driver { };
    bool external_driver { };
    bool event_variable { };
    RegionObservation observations { RegionObservation::none };
};

/// One signal that a direct container-object operation may access. `writable`
/// describes an effect of the object operation, including publication of a
/// derived aggregate view. Object element indexes are runtime registers, so
/// graph accesses conservatively cover the complete signal rather than
/// guessing a selected element.
struct RegionContainerSignalBinding {
    SignalId signal { };
    bool readable { };
    bool writable { };
    /// The object operation changes a derived signal view, but process driver
    /// ownership is represented by another binding (for example, aggregate
    /// array storage projected from its leaf signals).
    bool indirect_write { };
};

/// Static signal bindings for one ContainerObjectId. `complete` means every
/// signal effect of direct object reads/writes is represented by `bindings`;
/// a complete descriptor with no bindings is owning storage with no signal
/// bindings.
/// Cascading views and other unresolved bindings must remain incomplete.
struct RegionContainerDescriptor {
    ContainerObjectId object { };
    std::vector<RegionContainerSignalBinding> bindings;
    bool complete { };
};

/// Verified physical projection for one aggregate array signal. Offsets are
/// normalized to the least-significant bit of the proxy; each member covers
/// one complete declared-order element. This relationship adds access and
/// observation edges only. It is not a scheduler dependency edge by itself.
struct RegionSignalAliasLeaf {
    SignalId signal { };
    std::uint32_t ordinal { };
    std::uint32_t offset { };
    std::uint32_t width { };

    bool operator==(const RegionSignalAliasLeaf&) const = default;
};

struct RegionSignalAliasRange {
    SignalId signal { };
    std::uint32_t offset { };
    std::uint32_t width { };

    bool operator==(const RegionSignalAliasRange&) const = default;
};

struct RegionSignalAliasFamilyDescriptor {
    ContainerObjectId object { };
    SignalId proxy { };
    std::uint32_t width { };
    std::vector<RegionSignalAliasLeaf> leaves;
    bool complete { };
    bool proxy_readable { };
    bool proxy_writable { };

    /// Project a nonempty normalized proxy range into ordered leaf-local
    /// ranges. Width zero means from the offset through the proxy's end.
    /// Returns false for an incomplete family or a range outside its coverage.
    [[nodiscard]] bool project_range(std::uint32_t offset,
        std::uint32_t width,
        std::vector<RegionSignalAliasRange>& ranges) const;

    bool operator==(const RegionSignalAliasFamilyDescriptor&) const = default;
};

struct RegionAccess {
    ProcessId process { };
    std::uint32_t offset { };
    /// Zero width is the entire signal, including unknown dynamic selections.
    std::uint32_t width { };
    EdgeKind edge { EdgeKind::any };

    bool operator==(const RegionAccess&) const = default;
};

struct RegionSignalNode {
    RegionSignalDescriptor descriptor;
    RegionDriverClass drivers { RegionDriverClass::undriven };
    RegionObservation observations { RegionObservation::none };
    /// Current writer accesses or declared ownership could not be proven.
    bool writers_unknown { };
    /// Fork children may introduce new driver identities for these accesses.
    /// Structural regions treat this as unknown ownership. Existing guarded
    /// cohorts can use proven current owners until spawn invalidates them.
    bool dynamic_fork_writers { };
    bool partial_projected_transactions { };
    std::vector<RegionAccess> readers;
    std::vector<RegionAccess> writers;
    /// Process dependencies invalidated by observation, mutation, or rebinding.
    /// This includes both readers and original driver owners.
    std::vector<ProcessId> invalidation_dependencies;
};

struct RegionProcessNode {
    ProcessId process { };
    ProcessSchedulingDomain scheduling_domain { ProcessSchedulingDomain::generic };
    RegionUpdateKind update_kind { RegionUpdateKind::generic };
    std::vector<Sensitivity> sensitivities;
    std::vector<Sensitivity> reads;
    std::vector<Process::DriverRegion> writes;
    bool pure { };
    /// True only when every classified direct write matches one declared
    /// driver range at the value width inferred by the graph walk.
    bool operation_write_ranges_exact { };
    /// True only when at least one instruction has an unclassified or opaque
    /// signal-access effect. This is independent of `pure`.
    bool dependencies_unknown { };
    /// Cycle classification within the pure scheduling/publication domain.
    /// Cross-domain or non-pure dependencies remain in the signal inventory.
    bool cyclic_or_dependent_on_cycle { };
};

enum class RegionProcessExclusionReason : std::uint8_t {
    not_pure,
    wrong_scheduling_domain,
    wrong_update_kind,
    cyclic_or_dependent_on_cycle,
    unknown_dependencies,
    edge_sensitivity,
    count,
};

enum class RegionBoundaryReason : std::uint8_t {
    no_internal_writer,
    writer_outside_component,
    reader_outside_component,
    unknown_driver_ownership,
    partial_driver,
    resolved_driver_class,
    unsupported_resolution_mode,
    implicit_driver,
    external_driver,
    event_signal,
    unsupported_width,
    unsupported_value_kind,
    partial_projected_transactions,
    access_inventory_incomplete,
    observed_current,
    observed_previous,
    observed_drivers,
    observed_pending,
    observed_events,
    observed_mutation,
    observed_coverage,
    observed_unknown,
    count,
};

struct RegionCertificateEpoch {
    ProcessId process { };
    std::uint64_t epoch { };

    bool operator==(const RegionCertificateEpoch&) const = default;
};

/// One operation kind that the bounded access visitor leaves opaque. The name
/// is an implementation-defined RTTI spelling intended only for local
/// profiling; it is neither serialized nor a stable cross-toolchain key.
struct RegionOpaqueOperationCount {
    std::string type_name;
    std::size_t incidences { };

    bool operator==(const RegionOpaqueOperationCount&) const = default;
};

enum class RegionComponentCertificateStatus : std::uint8_t {
    structural_candidate,
    no_internal_state,
    incomplete_access_inventory,
};

struct RegionComponentCertificate {
    std::vector<ProcessId> members;
    /// Structural internal-signal candidates only; this does not mean runtime
    /// storage has been hidden, materialized, or made executable.
    std::vector<SignalId> structural_internal_signal_candidates;
    /// Signals with ordinary boundary semantics stay public and use the
    /// scheduler's existing reads and publications.
    std::vector<SignalId> boundary_signals;
    std::vector<RegionCertificateEpoch> captured_epochs;
    /// RegionGraph has no scheduler/executor view. Every census record still
    /// requires a separate proof of epochs, wait state, settled boundaries,
    /// pending publications, callback/observer/mutation hooks, aliases and any
    /// custom process-executor behavior before a future runtime may use it.
    bool requires_runtime_execution_proof { true };
    /// For these candidate signals, a future executor must separately match
    /// the active direct-single-driver route owner to the sole graph writer,
    /// require its record and `can_publish_native_word_prevalidated` check, and
    /// honor force/charge/alias/wait/observer/dependency and epoch guards.
    std::vector<SignalId> runtime_single_driver_proof_signals;
    bool requires_runtime_single_driver_proof { };
    RegionComponentCertificateStatus status {
        RegionComponentCertificateStatus::no_internal_state
    };

    bool operator==(const RegionComponentCertificate&) const = default;
};

struct RegionCertificateInventory {
    std::vector<RegionComponentCertificate> components;
    /// Build-snapshot operation incidences left opaque by access analysis,
    /// sorted by implementation-defined type name for deterministic output.
    std::vector<RegionOpaqueOperationCount> opaque_operation_counts;
    std::array<std::size_t,
        static_cast<std::size_t>(RegionProcessExclusionReason::count)>
        process_exclusion_counts { };
    std::array<std::size_t,
        static_cast<std::size_t>(RegionBoundaryReason::count)>
        boundary_reason_counts { };
    /// False means an opaque process may access arbitrary signals. No signal
    /// can be hidden while this whole-graph dependency inventory is incomplete.
    bool access_inventory_complete { true };

    bool operator==(const RegionCertificateInventory&) const = default;
};

/// One captured whole-signal value associated with its original process
/// driver. `value_register` is a dedicated snapshot register in the compute
/// program, not a reused source register. `source_instruction` preserves the
/// original operation order; `compute_instruction` locates its snapshot copy.
struct RegionConeOutputBinding {
    ProcessId owner { };
    SignalId signal { };
    std::uint32_t offset { };
    std::uint32_t width { };
    ValueKind value_kind { ValueKind::logic4 };
    SignalUpdateDomain domain { SignalUpdateDomain::systemverilog_active };
    RegisterId value_register { };
    InstructionIndex source_instruction { };
    InstructionIndex compute_instruction { };
    /// Instruction location in the readiness-masked activation kernel.
    /// `compute_instruction` remains the corresponding settled-oracle slot.
    InstructionIndex kernel_instruction { };
    /// Retain the original language publication contract through the
    /// readiness-masked kernel. VHDL projected assignments remain projected
    /// writes when their captured values are published by the owner.
    RegionUpdateKind update_kind {
        RegionUpdateKind::systemverilog_active
    };
    ProjectedDelayMode projected_mode { ProjectedDelayMode::inertial };
    SimulationTick projected_delay { };
    SimulationTick projected_rejection { };
    RegionOutputPublicationKind publication_kind {
        RegionOutputPublicationKind::update
    };

    /// Full physical target width. `width` above remains the captured RHS
    /// width, which may be a slice for a boundary WriteUpdateSlice. Zero is
    /// retained for legacy whole-output aggregate initializers; consumers may
    /// infer it as `width` only when `offset` is zero.
    std::uint32_t signal_width { };

    bool operator==(const RegionConeOutputBinding&) const = default;
};

/// A contiguous instruction range in the synthetic compute program that was
/// derived from one original process. Its sensitivities are retained for a
/// future activation-frontier proof; the range itself grants no permission to
/// execute that member or publish its outputs.
struct RegionConeMemberSpan {
    ProcessId process { };
    InstructionIndex begin { };
    InstructionIndex end { };
    bool initialize { true };
    std::vector<Sensitivity> sensitivities;

    bool operator==(const RegionConeMemberSpan&) const = default;
};

/// One signal value sampled into a region kernel's read-current register bank
/// before any selected member executes. Internal inputs are read from the
/// committed region plane; boundary inputs are captured from public current
/// signal storage at the same activation cut.
struct RegionConeKernelInput {
    SignalId signal { };
    RegisterId value_register { };
    std::uint32_t width { };
    ValueKind value_kind { ValueKind::logic4 };
    bool internal { };

    bool operator==(const RegionConeKernelInput&) const = default;
};

/// A guarded startup-constant candidate for one boundary input. Runtime still
/// captures the live value each activation and may use a specialized native
/// body only while this exact value matches. The original owner and complete
/// binding shape remain part of the immutable specialization identity.
struct RegionConeConstantInput {
    ProcessId owner { };
    SignalId signal { };
    std::uint32_t offset { };
    std::uint32_t width { };
    ValueKind value_kind { ValueKind::logic4 };
    SignalUpdateDomain domain { SignalUpdateDomain::systemverilog_active };
    PackedLogic4 value;
    // Retain the original publication contract for non-Active constant
    // owners. These fields are authenticated by both guarded-code identities
    // even though native execution still checks the captured value per entry.
    RegionUpdateKind update_kind {
        RegionUpdateKind::systemverilog_active
    };
    ProjectedDelayMode projected_mode { ProjectedDelayMode::inertial };
    SimulationTick projected_delay { };
    SimulationTick projected_rejection { };

    bool operator==(const RegionConeConstantInput&) const = default;
};

/// Maps a source Process register to its activation-kernel register.
/// The defined flag is false when ordinary execution leaves this source
/// register uninitialized for every activation.
using RegionConeKernelRegisterBinding
    = ProcessExecutor::RegionRegisterBinding;

/// The last inert source marker executed by one member. Replacement execution
/// has no execution-point observer, but it must leave the same diagnostic
/// source/scope on the suspended ProcessState.
struct RegionConeFinalDebugState {
    SourceLocation source;
    std::string scope;

    bool operator==(const RegionConeFinalDebugState&) const = default;
};

/// One member's guarded portion of a stable region kernel. Readiness is an
/// explicit one-bit input, so the native kernel can skip every member outside
/// the scheduler's current active mask.
struct RegionConeKernelMember {
    ProcessId process { };
    RegisterId readiness_register { };
    InstructionIndex branch_instruction { };
    InstructionIndex begin { };
    InstructionIndex end { };
    bool initialize { true };
    std::vector<Sensitivity> sensitivities;
    std::optional<RegionConeFinalDebugState> final_debug_state;
    std::vector<RegionConeKernelRegisterBinding> register_bindings;
    /// True only for the accepted straight-line body when every source
    /// register has a width-known definition before any use. The builder
    /// rejects branches and unsupported operations before setting this bit.
    bool all_registers_definitely_defined { };

    bool operator==(const RegionConeKernelMember&) const = default;
};

/// The single compiled activation kernel. Unlike the diagnostic settled
/// compute program, every internal read uses a runtime-populated committed
/// input register and every internal write is captured in an owner-tagged
/// output register. This representation does not itself grant scheduler or
/// publication permission.
struct RegionConeActivationKernel {
    Process program;
    std::vector<RegionConeKernelInput> inputs;
    std::vector<RegionConeKernelMember> members;
    /// Optional execution order of member indices. Empty means the member
    /// vector's existing ProcessId order. A forwarding kernel uses this to
    /// keep graph metadata ProcessId-indexed while laying bodies out in the
    /// certified topological order.
    std::vector<std::size_t> member_execution_order;
    std::vector<RegionConeOutputBinding> outputs;
    std::vector<SignalId> internal_signals;
    std::vector<RegionConeConstantInput> constant_inputs;
};

/// One member in a certified private forwarding program. Records are stored
/// in ascending ProcessId order; each member index addresses this array.
/// Output and dependency spans address the containing kernel's immutable
/// arrays.
struct RegionConeForwardingDependency {
    SignalId signal { };
    /// Member whose unique full-signal output produces this dependency.
    std::size_t writer_member_index {
        std::numeric_limits<std::size_t>::max()
    };
    EdgeKind edge { EdgeKind::any };
    /// LSB-normalized sensitivity interval. A zero width denotes the complete
    /// signal and requires offset zero.
    std::uint32_t offset { };
    std::uint32_t width { };

    bool operator==(const RegionConeForwardingDependency&) const = default;
};

/// An internal value read by one body. Runtime validates the complete signal
/// value against this writer's predicted output at the body's original key.
struct RegionConeForwardingRead {
    SignalId signal { };
    std::size_t writer_member_index {
        std::numeric_limits<std::size_t>::max()
    };

    bool operator==(const RegionConeForwardingRead&) const = default;
};

struct RegionConeForwardingMember {
    ProcessId process { };
    std::uint32_t depth { };
    std::size_t output_begin { };
    std::size_t output_count { };
    std::size_t dependency_begin { };
    std::size_t dependency_count { };
    std::size_t read_begin { };
    std::size_t read_count { };

    bool operator==(const RegionConeForwardingMember&) const = default;
};

/// An optional stateless SV execution form derived from the same certified
/// member bodies as the checked activation kernel. Its program forwards
/// internal reads through private registers in topological order; it never
/// publishes signals or grants scheduler permission. Runtime must separately
/// authenticate the actual contiguous ready-member prefix and revalidate
/// boundary values before each original member publication.
struct RegionConeForwardingKernel {
    RegionConeActivationKernel execution_kernel;
    /// Member indices in the program's topological body order.
    std::vector<std::size_t> topological_member_indices;
    /// Member records in ascending ProcessId order.
    std::vector<RegionConeForwardingMember> members;
    /// Internal sensitivities and their unique producing members.
    /// Initial admission is restricted to EdgeKind::any; retaining the edge
    /// makes the certificate explicit and lets runtime fail closed if it
    /// encounters a different trigger kind. Each edge is independent, so
    /// children may join values from multiple predecessor members.
    std::vector<RegionConeForwardingDependency> dependencies;
    /// Every internal ReadSignal and its unique producer. These values are
    /// checked at the original member callback before consuming a cached join
    /// result, preserving intermediate cuts when predecessors interleave.
    std::vector<RegionConeForwardingRead> internal_reads;
    /// Internal signal set used to compare a parent's private outputs with
    /// the captured committed cut before activating its children.
    std::vector<SignalId> internal_signals;
};

/// Program construction for one structural component. The settled-value
/// construction oracle is temporary and is not retained in this runtime
/// object. Runtime execution normally uses the readiness-masked activation
/// kernel so internal reads remain on the committed-current side of
/// publication. A separately certified forwarding kernel may evaluate a
/// narrow closed SV subgraph into a private result bank; it does not publish.
struct RegionConeProgram {
    std::size_t component_index { };
    RegionConeActivationKernel activation_kernel;
    std::optional<RegionConeForwardingKernel> forwarding_kernel;
    std::vector<ProcessId> members;
    std::vector<RegionConeMemberSpan> member_spans;
    std::vector<Sensitivity> boundary_sensitivities;
    std::vector<RegionConeOutputBinding> boundary_outputs;
    std::vector<RegionConeOutputBinding> internal_materializations;
};

/// Optional, allocation-free profile detail for a failed compute-program
/// build. The string fields point to static literals and are valid for the
/// duration of the build result inspection.
struct RegionComputeProgramRejection {
    const char* reason { };
    std::size_t component { };
    SignalId signal { std::numeric_limits<SignalId>::max() };
    SignalId family_proxy { std::numeric_limits<SignalId>::max() };
    ProcessId writer_process { std::numeric_limits<ProcessId>::max() };
    ProcessId process { std::numeric_limits<ProcessId>::max() };
    std::size_t operation_index {
        std::numeric_limits<std::size_t>::max()
    };
    const char* operation_type { };
};

/// One deterministic elaborated dependency graph. It does not grant execution
/// permission: a future hidden-storage path additionally requires runtime
/// proofs of epoch validity, settled inputs, a scheduler-domain-specific
/// execution contract, and the absence or safe routing of runtime modifiers.
class RegionGraph {
public:
    /// process_access_complete, when supplied, is aligned with processes.
    /// False entries identify alternate executors whose signal effects are
    /// not bound to the supplied SimIR program; any such process makes the
    /// whole-graph access inventory incomplete.
    [[nodiscard]] static RegionGraph build(
        std::span<const Process* const> processes,
        std::span<const RegionSignalDescriptor> signals,
        bool collect_opaque_operation_counts = false,
        std::span<const RegionContainerDescriptor> containers = { },
        std::span<const std::uint8_t> process_access_complete = { },
        std::span<const RegionSignalAliasFamilyDescriptor>
            signal_alias_families = { });

    [[nodiscard]] std::span<const RegionProcessNode> processes() const noexcept
    {
        return processes_;
    }
    [[nodiscard]] std::span<const RegionSignalNode> signals() const noexcept
    {
        return signals_;
    }
    [[nodiscard]] std::span<const RegionSignalAliasFamilyDescriptor>
    signal_alias_families() const noexcept
    {
        return signal_alias_families_;
    }
    /// Invalidate the signal's readers and original owners before exposing
    /// additional state. Epoch zero permanently disables a trusted binding
    /// after overflow; callers must never treat zero as a valid certificate.
    /// The successful path uses a precomputed stable dependency span and does
    /// not allocate.
    [[nodiscard]] std::span<const ProcessId> observe_signal(
        SignalId signal, RegionObservation capabilities);
    [[nodiscard]] std::uint64_t capability_epoch(ProcessId process) const;

    /// A structural census only. Boundary-only components are reported but do
    /// not qualify as internal-state candidates. Captured epochs become stale
    /// after observation or rebinding; an equal epoch proves only that no such
    /// invalidation occurred, not that a process is currently runnable. A
    /// consumer must separately prove members are waiting, boundary inputs are
    /// settled, pending publications are safe, and runtime modifiers are
    /// compatible before using any internal-signal candidate.
    [[nodiscard]] const RegionCertificateInventory& certificate_inventory() const noexcept
    {
        return certificate_inventory_;
    }
    [[nodiscard]] bool component_epochs_current(std::size_t component) const;

    /// Build a deterministic readiness-masked activation program for one
    /// current structural component. Unsupported or stale source programs
    /// fail closed; success does not itself grant runtime admission.
    [[nodiscard]] std::optional<RegionConeProgram> build_compute_program(
        std::size_t component,
        std::span<const Process* const> programs) const;
    /// Same builder with an optional fixed-size diagnostic record. The
    /// ordinary overload above retains its original API and does not collect
    /// diagnostic details.
    [[nodiscard]] std::optional<RegionConeProgram> build_compute_program(
        std::size_t component,
        std::span<const Process* const> programs,
        RegionComputeProgramRejection* rejection) const;

    /// Pure acyclic processes in deterministic dependency order within each
    /// scheduling/publication domain. Sequential and cross-domain edges are
    /// boundaries, not permission to run with unsettled inputs. VHDL nodes
    /// remain cycle-exact; this order never authorizes collapsing their cycles.
    [[nodiscard]] std::span<const ProcessId> topological_order() const noexcept
    {
        return topological_order_;
    }

private:
    friend class region_graph_detail::RegionGraphProgramBuilder;
    friend struct region_graph_detail::RegionGraphStorageCensusAccess;

    std::vector<RegionProcessNode> processes_;
    std::vector<RegionSignalNode> signals_;
    std::vector<RegionSignalAliasFamilyDescriptor> signal_alias_families_;
    /// Family index for each proxy/leaf signal, or max() when unaliased.
    std::vector<std::size_t> signal_alias_family_by_signal_;
    /// True only for the aggregate proxy signal in the corresponding family.
    std::vector<std::uint8_t> signal_alias_is_proxy_;
    std::vector<std::vector<ProcessId>>
        signal_alias_invalidation_dependencies_;
    std::vector<ProcessId> topological_order_;
    /// Rank in topological_order_, or size_t max for absent processes.
    std::vector<std::size_t> topological_rank_by_process_;
    std::vector<std::uint64_t> capability_epochs_;
    /// Reverse membership for the disjoint captured component member sets.
    /// Processes outside a certificate component use size_t max().
    std::vector<std::size_t> certificate_component_by_process_;
    /// Captured epochs never change after graph construction. Once any
    /// observation invalidates a captured process, its component can never
    /// become current again in this graph instance.
    std::vector<std::uint8_t> component_epochs_stale_;
    RegionCertificateInventory certificate_inventory_;
};

} // namespace fsim::runtime::simir
