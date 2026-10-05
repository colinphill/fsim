// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir_region_activation.hpp"
#include "fsim/runtime/simir_region_frontier_v2.hpp"

#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace fsim::runtime::simir {

/// An optional persistent executor for one immutable region activation
/// kernel. It owns only private execution state. The interpreter remains the
/// authority for scheduling, signal publication, and member completion.
class RegionKernelBackend {
public:
    virtual ~RegionKernelBackend() = default;

    /// A failed attempt leaves the activation image and all
    /// interpreter-owned state unchanged. After false, callers must check
    /// RegionKernelFailureBackend when available;
    /// only an empty failure channel means recoverable decline and permits
    /// checked execution on the same image.
    [[nodiscard]] virtual bool execute(
        const RegionKernelActivationImage& image) noexcept = 0;

    /// Valid only after a successful execute() and until the next execute().
    [[nodiscard]] virtual std::span<const PackedLogic4>
    activation_registers() const noexcept = 0;
};

/// Optional one-shot channel for failures caught by a non-unwinding native
/// backend. After a false result, callers must poll this before any retry;
/// empty means ordinary recoverable decline. The next serialized attempt
/// clears prior state, and polling moves and clears the captured exception.
/// Callers must serialize execution and polling for one backend instance.
/// Implementations without this capability retain the legacy false-means-
/// decline behavior.
class RegionKernelFailureBackend {
public:
    virtual ~RegionKernelFailureBackend() = default;

    [[nodiscard]] virtual std::exception_ptr take_failure() noexcept = 0;
};

/// Optional second-stage native input path. A backend may implement this
/// interface when it can consume current Logic4 component planes directly.
/// False permits retry through execute(image) only when the optional failure
/// channel is empty.
/// The spans are borrowed for this call only and must not be retained.
class RegionKernelLogic4InputBackend {
public:
    virtual ~RegionKernelLogic4InputBackend() = default;

    [[nodiscard]] virtual bool execute_with_logic4_input_planes(
        const RegionKernelActivationImage& image,
        std::span<const RegionKernelLogic4InputPlane> planes) noexcept = 0;
};

/// Optional generalized native input path for prevalidated component planes.
/// A false return permits the checked `execute(image)` path only when the
/// optional failure channel is empty. The spans are borrowed for this call
/// only.
/// Implementations validate exact widths and plane counts, tails, Logic9
/// ordinals, and image equality before changing reusable execution state.
class RegionKernelInputPlaneBackend {
public:
    virtual ~RegionKernelInputPlaneBackend() = default;

    [[nodiscard]] virtual bool execute_with_input_planes(
        const RegionKernelActivationImage& image,
        std::span<const RegionKernelInputPlane> planes) noexcept = 0;
};

/// Optional native entry that writes a certified internal-output prefix into
/// caller-owned A4 replacement planes. The runtime retains those planes with
/// the original queued update tickets and seals each slot only when its
/// scheduler key is dispatched. After false, fallback is allowed only when
/// the optional failure channel is empty.
class RegionKernelPreparedOutputBackend {
public:
    virtual ~RegionKernelPreparedOutputBackend() = default;

    [[nodiscard]] virtual bool execute_internal_output_prefix_prepared(
        const RegionKernelActivationImage& image,
        std::span<const PackedLogic4> current_internal_values,
        std::span<const RegionConeOutputBinding> ordered_prefix,
        RegionPreparedOutputBatchV1& outputs) noexcept = 0;
};

/// Private ready-window input entry paired with prepared output publication.
/// It consumes call-scoped Logic4 plane descriptors and a member-ready mask;
/// an executor must reject malformed descriptors before changing its frame or
/// the unpublished output batch. A false result leaves both available for
/// the ordinary image-based fallback only when the optional failure channel
/// is empty.
class RegionKernelDirectReadyWindowBackend
    : public RegionKernelPreparedOutputBackend {
public:
    [[nodiscard]] virtual bool supports_direct_ready_window() const noexcept
    {
        return false;
    }

    /// Optional combined entry. Implementations must reject a malformed
    /// successor sidecar before changing the direct input planes or output
    /// batch. A false result permits a caller retry only when the optional
    /// failure channel is empty. The V1 direct-ready entry remains available
    /// when this optional capability is absent.
    [[nodiscard]] virtual bool
    supports_direct_ready_window_successor_masks() const noexcept
    {
        return false;
    }

    [[nodiscard]] virtual bool
    execute_direct_ready_window_prepared_with_successor_masks(
        const RegionKernelActivationImage&,
        const RegionDirectReadyWindowV1&,
        std::span<const PackedLogic4>,
        std::span<const RegionConeOutputBinding>,
        RegionPreparedOutputBatchV1&,
        RegionPreparedOutputSuccessorMasksV1&) noexcept
    {
        return false;
    }

    [[nodiscard]] virtual bool execute_direct_ready_window_prepared(
        const RegionKernelActivationImage& image,
        const RegionDirectReadyWindowV1& input_window,
        std::span<const PackedLogic4> current_internal_values,
        std::span<const RegionConeOutputBinding> ordered_prefix,
        RegionPreparedOutputBatchV1& outputs) noexcept = 0;
};

/// Optional native successor-mask output path. The sidecar carries one
/// compiler-generated local-member mask per prepared output. Runtime binds
/// those local bits to a complete, generation-matched whole-any fanout before
/// execution, then consumes them only at the original owner publication key.
/// A false return leaves the activation image and both private output planes
/// available for the existing checked fallback when the failure channel is
/// empty.
class RegionKernelPreparedOutputSuccessorMaskBackend {
public:
    virtual ~RegionKernelPreparedOutputSuccessorMaskBackend() = default;

    [[nodiscard]] virtual bool
    execute_internal_output_prefix_prepared_with_successor_masks(
        const RegionKernelActivationImage& image,
        std::span<const PackedLogic4> current_internal_values,
        std::span<const RegionConeOutputBinding> ordered_prefix,
        RegionPreparedOutputBatchV1& outputs,
        RegionPreparedOutputSuccessorMasksV1& successors) noexcept = 0;
};

/// Application-owned compiler boundary. `identity()` describes every
/// immutable compilation option that affects generated code. `create()` is
/// called only while the initial runtime snapshot is prepared; quiet refreshes
/// rebind exact matches from the runtime-owned pool and never call this
/// provider.
class RegionKernelBackendProvider {
public:
    virtual ~RegionKernelBackendProvider() = default;

    [[nodiscard]] virtual std::string_view identity() const noexcept = 0;

    /// Return null when this kernel is unsupported. Exceptions other than
    /// std::bad_alloc from optional native construction also decline the
    /// backend; std::bad_alloc propagates. The runtime keeps the checked
    /// activation program after a declined backend. Graph and certificate
    /// errors are reported independently before this provider is called.
    [[nodiscard]] virtual std::unique_ptr<RegionKernelBackend> create(
        const RegionConeActivationKernel& kernel) = 0;
};

/// Optional generated scheduler-frontier entry for a complete certified
/// SystemVerilog activation component. V2 carries typed Logic4/Logic9 plane
/// descriptors; it is a distinct ABI and cannot be called with a V1 frame.
/// The backend owns immutable entry/layout descriptors; the interpreter owns
/// and preallocates one mutable frame per component instance.
class RegionFrontierBackend {
public:
    virtual ~RegionFrontierBackend() = default;

    [[nodiscard]] virtual RegionFrontierStepEntryV2
    step_entry() const noexcept = 0;

    [[nodiscard]] virtual const RegionFrontierLayoutV2&
    layout() const noexcept = 0;
};

/// Separate optional capability discovered on the installed activation
/// provider only after the ordinary graph/certificate and frame shape pass.
class RegionFrontierBackendProvider {
public:
    virtual ~RegionFrontierBackendProvider() = default;

    [[nodiscard]] virtual std::unique_ptr<RegionFrontierBackend>
    create_frontier(const RegionConeActivationKernel& kernel) = 0;
};

/// Owning, not-yet-materialized plan for a native scheduler-frontier entry.
/// Its layout and pointer-backed descriptors remain valid for every
/// synchronous preflight that occurs before the one-shot materialize call.
class RegionFrontierPreparedBackend {
public:
    virtual ~RegionFrontierPreparedBackend() = default;

    [[nodiscard]] virtual const RegionFrontierLayoutV2&
    layout() const noexcept = 0;

    /// Optional canonical shape identity for census grouping only. The view
    /// is borrowed from this prepared object and must be copied by the caller
    /// before it is destroyed or materialized. It never authorizes code reuse.
    [[nodiscard]] virtual std::optional<std::string_view>
    structural_census_identity() const noexcept
    {
        return std::nullopt;
    }

    /// Consume this plan once. A decline returns null; no placeholder entry
    /// is exposed to the runtime.
    [[nodiscard]] virtual std::unique_ptr<RegionFrontierBackend>
    materialize() && = 0;
};

/// Separate optional capability. Providers without it retain the existing
/// create_frontier path and virtual table unchanged.
class RegionFrontierPreparingProvider {
public:
    virtual ~RegionFrontierPreparingProvider() = default;

    [[nodiscard]] virtual std::unique_ptr<RegionFrontierPreparedBackend>
    prepare_frontier(const RegionConeActivationKernel& kernel) = 0;
};

/// Optional private evaluator for a certified topological forwarding kernel.
/// `boundary_inputs` is in the exact order of the kernel's non-internal
/// inputs. `output_values` has one slot per execution-kernel output binding,
/// in binding order; each slot is an ordinary owning, unpublished PackedLogic4
/// value with the binding's exact width and Logic4 kind. Runtime supplies
/// these pre-shaped result slots only from its private result bank; they are
/// never A4-bound public roles. The backend validates all slots, evaluates
/// into private owning scratch, and replaces caller slots only after the
/// complete result batch succeeds. It never commits, schedules, or invokes member
/// callbacks. Runtime retains active member outputs until each original
/// ProcessId task reaches its scheduler frontier, and validates the complete
/// internal-read cut before consuming a cached result. For a join whose
/// predecessors have not all reached the predicted cut, runtime declines to
/// checked execution at that same key without waiting for another parent,
/// but only when the forwarding failure channel is empty.
class RegionConeForwardingBackend {
public:
    virtual ~RegionConeForwardingBackend() = default;

    [[nodiscard]] virtual bool execute_forwarding(
        const RegionKernelSchedulerPrefix& origin,
        std::span<const PackedLogic4> boundary_inputs,
        std::span<PackedLogic4> output_values) noexcept = 0;
};

/// Optional one-shot failure channel for forwarding backends. Poll after each
/// false result before trying another backend or the checked path. An empty
/// pointer means ordinary decline. Implementations clear on take and before
/// each serialized attempt; callers serialize execution and polling.
class RegionConeForwardingFailureBackend {
public:
    virtual ~RegionConeForwardingFailureBackend() = default;

    [[nodiscard]] virtual std::exception_ptr take_failure() noexcept = 0;
};

/// Separate optional capability so existing custom activation providers do
/// not need to implement forwarding. Snapshot construction may dynamically
/// discover this interface on the existing provider and compile once.
class RegionConeForwardingBackendProvider {
public:
    virtual ~RegionConeForwardingBackendProvider() = default;

    [[nodiscard]] virtual std::unique_ptr<RegionConeForwardingBackend>
    create_forwarding(const RegionConeForwardingKernel& kernel) = 0;
};

/// Exact immutable comparison used to reuse a startup backend after graph
/// refresh. Unsupported operation alternatives fail closed.
[[nodiscard]] bool same_region_kernel_mapping(
    const RegionConeActivationKernel& left,
    const RegionConeActivationKernel& right);

} // namespace fsim::runtime::simir
