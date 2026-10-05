// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir_region_graph.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace fsim::runtime::simir {

/// Private compiler/runtime handoff for one preflighted internal-output
/// publication. This is not part of jit_runtime_v2.h and is only accepted by
/// the region executor's typed four-argument entry. The caller keeps every
/// replacement plane private and uniquely writable until that entry returns;
/// A4 publishes the completed role block only after a successful return.
inline constexpr std::uint32_t kRegionPreparedOutputBatchAbiVersionV1 = 1U;

enum class RegionPreparedOutputValueKindV1 : std::uint32_t {
    logic4 = 0U,
};

struct RegionPreparedOutputSlotV1 {
    std::uint32_t struct_size { };
    std::uint32_t signal_id { };
    std::uint32_t owner_id { };
    std::uint32_t width { };
    std::uint32_t word_count { };
    RegionPreparedOutputValueKindV1 value_kind {
        RegionPreparedOutputValueKindV1::logic4
    };
    std::uint32_t selected { };
    std::uint32_t reserved { };
    const std::uint64_t* owner_mask { };
    const std::uint64_t* old_current_aval { };
    const std::uint64_t* old_current_bval { };
    const std::uint64_t* old_owner_aval { };
    const std::uint64_t* old_owner_bval { };
    std::uint64_t* next_current_aval { };
    std::uint64_t* next_current_bval { };
    std::uint64_t* next_last_aval { };
    std::uint64_t* next_last_bval { };
    std::uint64_t* next_stored_aval { };
    std::uint64_t* next_stored_bval { };
    std::uint64_t* next_owner_aval { };
    std::uint64_t* next_owner_bval { };
    std::uint8_t* changed { };
    std::uint8_t* value_ready { };
    std::uint8_t* transaction_ready { };
};

struct RegionPreparedOutputBatchV1 {
    std::uint32_t abi_version { };
    std::uint32_t struct_size { };
    std::uint32_t slot_count { };
    std::uint32_t reserved { };
    const RegionPreparedOutputSlotV1* slots { };
};

/// Optional compiler-to-runtime result plane for static whole-signal
/// SystemVerilog any-change successors. Each prepared-output slot contains a
/// 64-bit local-member mask; bit positions are bound by the immutable kernel
/// mapping for that slot. The runtime validates that mapping against the
/// snapshot's complete grouped fanout before using this sidecar. V1 output
/// descriptors stay unchanged when this optional contract is unavailable.
inline constexpr std::uint32_t
    kRegionPreparedOutputSuccessorMasksAbiVersionV1 = 1U;

struct RegionPreparedOutputSuccessorMasksV1 {
    std::uint32_t abi_version { };
    std::uint32_t struct_size { };
    std::uint32_t slot_count { };
    std::uint32_t reserved { };
    std::uint64_t* member_masks { };
};

static_assert(std::is_standard_layout_v<RegionPreparedOutputSlotV1>);
static_assert(std::is_trivially_copyable_v<RegionPreparedOutputSlotV1>);
static_assert(std::is_standard_layout_v<RegionPreparedOutputBatchV1>);
static_assert(std::is_trivially_copyable_v<RegionPreparedOutputBatchV1>);
static_assert(std::is_standard_layout_v<
    RegionPreparedOutputSuccessorMasksV1>);
static_assert(std::is_trivially_copyable_v<
    RegionPreparedOutputSuccessorMasksV1>);
static_assert(offsetof(RegionPreparedOutputSuccessorMasksV1, member_masks)
    == sizeof(std::uint32_t) * 4U);
static_assert(sizeof(RegionPreparedOutputSuccessorMasksV1)
    == (sizeof(void*) == 4U ? 20U : 24U));

/// Call-scoped direct input image for the private ready-window entry. Only
/// narrow Logic4 values are admitted by this first ABI. `signal_id` and
/// `register_id` bind every pair of words to the immutable compiled mapping;
/// the generated wrapper copies these words into reusable direct-signal
/// storage before entering the process body, whose ordinary ReadSignal
/// operations fill registers. This is a plane-to-JIT-storage copy, not a
/// zero-copy body load.
inline constexpr std::uint32_t kRegionDirectReadyWindowAbiVersionV1 = 1U;

struct RegionDirectReadyInputSlotV1 {
    std::uint32_t struct_size { };
    std::uint32_t signal_id { };
    std::uint32_t register_id { };
    std::uint32_t width { };
    std::uint32_t word_count { };
    std::uint32_t reserved { };
    const std::uint64_t* aval { };
    const std::uint64_t* bval { };
};

struct RegionDirectReadyWindowV1 {
    std::uint32_t abi_version { };
    std::uint32_t struct_size { };
    /// Exact activation and scheduler cut whose borrowed planes were checked.
    std::uint64_t activation_generation { };
    std::uint64_t frontier_generation { };
    std::uint32_t member_count { };
    std::uint32_t readiness_word_count { };
    std::uint32_t input_slot_count { };
    std::uint32_t reserved { };
    const std::uint64_t* readiness_mask { };
    const RegionDirectReadyInputSlotV1* input_slots { };
};

static_assert(std::is_standard_layout_v<RegionDirectReadyInputSlotV1>);
static_assert(std::is_trivially_copyable_v<RegionDirectReadyInputSlotV1>);
static_assert(std::is_standard_layout_v<RegionDirectReadyWindowV1>);
static_assert(std::is_trivially_copyable_v<RegionDirectReadyWindowV1>);

/// Scheduling provenance captured when one original member becomes ready.
/// The order fields are copied from the scheduler entry and are retained for
/// every output publication produced by that activation.
struct RegionKernelActivationOrigin {
    ProcessSchedulingDomain process_domain {
        ProcessSchedulingDomain::generic
    };
    SchedulerPhase phase { SchedulerPhase::active };
    SimulationTick time { };
    std::uint64_t delta { };
    StableOrder stable_order { };
    std::uint64_t sequence { };
    std::uint64_t systemverilog_round { };

    bool operator==(const RegionKernelActivationOrigin&) const = default;
};

struct RegionKernelReadyMember {
    ProcessId process { };
    std::uint64_t trigger_mask { Process::full_static_trigger_mask };
    RegionKernelActivationOrigin origin;

    bool operator==(const RegionKernelReadyMember&) const = default;
};

struct RegionKernelSchedulerPrefixTask {
    /// Ordinal in the scheduler's frozen domain-specific frontier. This is
    /// not a process-local index or a stable order key.
    std::size_t task_ordinal { };
    RegionKernelReadyMember member;

    bool operator==(const RegionKernelSchedulerPrefixTask&) const = default;
};

/// Concrete scheduler snapshot for one contiguous prefix of a frozen
/// scheduler frontier. The scheduler dispatcher must construct this
/// from its live running queue and validate that its generation/cursor still
/// identify the current prefix immediately before calling begin_wave(). The
/// activation state validates the record's internal ordering and bounds; it
/// cannot authenticate a caller-created record or grant execution permission.
/// Entries after `tasks` remain in the frozen batch and are not consumed by
/// this activation, so an ordinary task naturally terminates the prefix.
struct RegionKernelSchedulerPrefix {
    std::uint64_t frontier_generation { };
    std::size_t frontier_cursor { };
    std::size_t frontier_end { };
    SimulationTick time { };
    std::uint64_t delta { };
    SchedulerPhase phase { SchedulerPhase::active };
    std::uint64_t systemverilog_round { };
    std::vector<RegionKernelSchedulerPrefixTask> tasks;
    /// Historical callers use this for SystemVerilog Active batches. Generic
    /// frontier callers set the domain explicitly and keep the SV round zero.
    ProcessSchedulingDomain process_domain {
        ProcessSchedulingDomain::systemverilog
    };

    bool operator==(const RegionKernelSchedulerPrefix&) const = default;
};

struct RegionKernelRegisterInput {
    RegisterId register_id { };
    PackedLogic4 value;

    bool operator==(const RegionKernelRegisterInput&) const = default;
};

/// A call-scoped borrowed Logic4 view for an internal signal that has a
/// current-generation component plane. The receiver must validate this view
/// against the captured register input before changing reusable native state;
/// it must not retain either span after the call returns.
struct RegionKernelLogic4InputPlane {
    SignalId signal { };
    RegisterId register_id { };
    std::uint32_t width { };
    std::span<const std::uint64_t> aval;
    std::span<const std::uint64_t> bval;
};

/// A call-scoped borrowed view of a current internal signal. Logic4 inputs
/// use planes 0 and 1; Logic9 inputs use all four ordinal planes. The receiver
/// must validate the kind, exact width, words, tail bits, and captured image
/// before changing reusable native state, and must not retain the spans.
struct RegionKernelInputPlane {
    SignalId signal { };
    RegisterId register_id { };
    std::uint32_t width { };
    ValueKind value_kind { ValueKind::logic4 };
    std::array<std::span<const std::uint64_t>, 4U> planes;
};

/// Complete immutable input image for one kernel call. Every member selected
/// at the scheduler cut reads the same committed internal values, regardless
/// of the order in which its guarded block appears in the kernel.
struct RegionKernelActivationImage {
    std::uint64_t generation { };
    RegionKernelSchedulerPrefix scheduler_prefix;
    std::vector<ProcessId> ready_processes;
    std::vector<RegionKernelReadyMember> requests;
    std::vector<std::size_t> active_member_indices;
    std::vector<RegionKernelRegisterInput> register_inputs;

    bool operator==(const RegionKernelActivationImage&) const = default;
};

struct RegionKernelPendingPublication {
    RegionConeOutputBinding binding;
    PackedLogic4 value;
    RegionKernelActivationOrigin origin;

    bool operator==(const RegionKernelPendingPublication&) const = default;
};

struct RegionKernelInternalSeed {
    SignalId signal { };
    ProcessId owner { };
    PackedLogic4 current;
    PackedLogic4 previous;
    PackedLogic4 raw_driver;
};

struct RegionKernelInternalState {
    SignalId signal { };
    ProcessId owner { };
    ValueKind value_kind { ValueKind::logic4 };
    PackedLogic4 current;
    PackedLogic4 previous;
    PackedLogic4 raw_driver;
};

/// State and staging boundary for the runtime activation kernel. The owner
/// still schedules and publishes every returned output through its original
/// process route; this object only captures one scheduler-ready mask, samples
/// committed inputs once, and retains typed previous/current/raw planes until
/// those owner publications are acknowledged in order. The referenced kernel
/// must remain immutable and outlive this state object.
class RegionKernelActivationState {
public:
    explicit RegionKernelActivationState(
        const RegionConeActivationKernel& kernel);

    RegionKernelActivationState(
        const RegionConeActivationKernel& kernel,
        std::span<const RegionKernelInternalSeed> internal_seed);

    /// Replace only the committed internal seed values while retaining all
    /// activation buffers. The state must not have an open wave.
    void reset_internal_state(
        std::span<const RegionKernelInternalSeed> internal_seed);

    /// Preflight and commit one narrow private owner publication without
    /// rebuilding the activation bank. The caller prepares both packed values
    /// before changing interpreter-visible state; this install is nonthrowing.
    [[nodiscard]] bool can_publish_internal_update(
        SignalId signal, ProcessId owner, std::uint32_t width) const noexcept;
    void publish_internal_update(
        SignalId signal,
        ProcessId owner,
        PackedLogic4&& current,
        PackedLogic4&& raw_driver,
        bool changed) noexcept;

    [[nodiscard]] RegionKernelActivationImage begin_wave(
        const RegionKernelSchedulerPrefix& scheduler_prefix,
        std::span<const PackedLogic4> current_boundary_inputs,
        std::span<const std::uint64_t> active_member_mask = { });

    /// Reuse the state-owned image and its reserved vectors. This is the
    /// native runtime path; the returned image remains valid until the wave is
    /// completed or abandoned.
    [[nodiscard]] const RegionKernelActivationImage& begin_wave_reusable(
        const RegionKernelSchedulerPrefix& scheduler_prefix,
        std::span<const PackedLogic4> current_boundary_inputs,
        std::span<const std::uint64_t> active_member_mask = { });

    /// Build a metadata-only image for the private direct-ready entry. Input
    /// values are borrowed separately for this wave; a caller that declines
    /// native execution must call materialize_wave_inputs() before using the
    /// ordinary checked-image backend.
    [[nodiscard]] const RegionKernelActivationImage&
    begin_direct_wave_reusable(
        const RegionKernelSchedulerPrefix& scheduler_prefix,
        std::span<const PackedLogic4> current_boundary_inputs,
        std::span<const std::uint64_t> active_member_mask = { });

    [[nodiscard]] bool direct_wave_active() const noexcept
    {
        return wave_active_ && !input_values_materialized_;
    }

    /// Restore the full value image after direct-entry decline. This may
    /// allocate for wide values and is called only before checked fallback.
    void materialize_wave_inputs(
        std::span<const PackedLogic4> current_boundary_inputs);

    void stage_kernel_outputs(
        const RegionKernelActivationImage& image,
        std::span<const PackedLogic4> registers);

    [[nodiscard]] std::span<const RegionKernelPendingPublication>
    pending_publications() const noexcept
    {
        return pending_publications_;
    }

    [[nodiscard]] std::size_t expected_publication_count() const noexcept
    {
        return expected_publication_count_;
    }

    [[nodiscard]] std::span<const RegionConeOutputBinding* const>
    expected_publication_bindings() const noexcept
    {
        return active_publication_bindings_;
    }

    /// Begin the original owner's raw-driver publication stage. This is
    /// separate from current-value publication because existing driver hooks
    /// observe the new raw record before resolution/current hooks run.
    void begin_raw_publication(std::size_t index);

    /// Finish the original owner's current-value stage. For internal signals,
    /// previous/current are advanced only here, after the real runtime commit.
    void publish_current(std::size_t index, const PackedLogic4& resolved_value);

    void complete_wave();
    /// Abandon reusable per-wave staging after the caller has committed the
    /// original publication route (or declined before semantic mutation).
    void discard_wave() noexcept;

    [[nodiscard]] const RegionKernelInternalState& internal_state(
        SignalId signal) const;

private:
    struct ReadyActivation {
        RegionKernelReadyMember request;
        std::size_t member_index { };
    };

    [[nodiscard]] std::size_t member_index(ProcessId process) const;
    [[nodiscard]] std::size_t internal_index(SignalId signal) const;
    void require_value_shape(
        SignalId signal, ValueKind kind, const PackedLogic4& value) const;

    void validate_kernel_shape();
    void reserve_kernel_storage();
    [[nodiscard]] const RegionKernelActivationImage& begin_wave_reusable_impl(
        const RegionKernelSchedulerPrefix& scheduler_prefix,
        std::span<const PackedLogic4> current_boundary_inputs,
        std::span<const std::uint64_t> active_member_mask,
        bool include_input_values);

    const RegionConeActivationKernel* kernel_ { };
    std::optional<RegionKernelActivationImage> active_image_;
    std::vector<RegionKernelInternalState> internal_;
    std::vector<ReadyActivation> active_members_;
    std::vector<RegionKernelPendingPublication> pending_publications_;
    std::vector<std::size_t> member_publication_offsets_;
    std::vector<std::size_t> member_publication_indices_;
    std::vector<const RegionConeOutputBinding*> active_publication_bindings_;
    std::uint64_t generation_ { };
    std::optional<std::uint64_t> last_frontier_generation_;
    std::size_t last_frontier_extent_ { };
    std::size_t last_prefix_end_ { };
    std::size_t next_publication_ { };
    std::size_t expected_publication_count_ { };
    bool wave_active_ { };
    bool publication_raw_started_ { };
    bool outputs_staged_ { };
    bool input_values_materialized_ { };
};

} // namespace fsim::runtime::simir
