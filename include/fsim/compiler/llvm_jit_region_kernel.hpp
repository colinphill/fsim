// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/compiler/llvm_jit.hpp"
#include "fsim/runtime/simir_region_activation.hpp"

#include <cstdint>
#include <exception>
#include <memory>
#include <span>
#include <string_view>

namespace fsim::compiler {

namespace llvm_detail {
struct RegionKernelTestAccess;
}

enum class RegionKernelSpecialization : std::uint8_t {
  dynamic_inputs,
  guarded_constant_inputs,
};

/// One persistent LLVM execution object for a checked region activation
/// kernel. It owns the compiled entry and its V2 frame. It does not schedule
/// members or publish their signals; callers retain those duties through the
/// original member executors and output bindings. The native wrapper accepts
/// Logic4 values of any representable nonzero width and Logic9 values when
/// the persistent frame layout supports complete register export. Unsupported
/// layouts decline and leave the checked C++ evaluator available. The entry
/// is value-only and does not invoke per-instruction observers: callers must
/// apply the region dispatcher’s observer/debug admission before using it.
/// DebugPoint operations are retained as each member’s final source/scope
/// snapshot, not emitted as callbacks.
class LlvmRegionKernelExecutor final {
public:
  /// Build a persistent entry and frame for a supported kernel. An invalid or
  /// unsupported kernel returns nullptr so the caller can use a checked
  /// fallback. Compiler failures throw.
  [[nodiscard]] static std::unique_ptr<LlvmRegionKernelExecutor> try_create(
      const runtime::simir::RegionConeActivationKernel& kernel,
      LlvmJitOptions options = {},
      std::string_view immutable_design_identity = {},
      RegionKernelSpecialization specialization
          = RegionKernelSpecialization::dynamic_inputs);

  ~LlvmRegionKernelExecutor();
  LlvmRegionKernelExecutor(LlvmRegionKernelExecutor&&) noexcept;
  LlvmRegionKernelExecutor& operator=(LlvmRegionKernelExecutor&&) noexcept;
  LlvmRegionKernelExecutor(const LlvmRegionKernelExecutor&) = delete;
  LlvmRegionKernelExecutor& operator=(const LlvmRegionKernelExecutor&) = delete;

  /// Run one already captured activation image. The persistent ABI frame,
  /// native entry, and result vector are reused. False means the frame did not
  /// complete and no output may be consumed; no signal or process state was
  /// changed by this object. After false, callers must inspect take_failure()
  /// before fallback; an empty result means ordinary decline. Callers
  /// serialize each call with its failure poll. Concurrent or reentrant calls
  /// return false before modifying reusable activation storage.
  [[nodiscard]] bool execute(
      const runtime::simir::RegionKernelActivationImage& image) noexcept;

  /// Consume the exception captured by the immediately preceding false
  /// execution attempt. Empty means that attempt was an ordinary decline.
  /// Polling moves and clears the value; a second poll returns empty. Callers
  /// must serialize execution and failure consumption for this object.
  [[nodiscard]] std::exception_ptr take_failure() noexcept;

  /// Run from exact borrowed current Logic4 planes for internal inputs.
  /// Inputs are compared with the captured image before reusable frame state
  /// is changed. False permits the checked execute(image) entry only when
  /// take_failure() is empty.
  [[nodiscard]] bool execute_with_logic4_input_planes(
      const runtime::simir::RegionKernelActivationImage& image,
      std::span<const runtime::simir::RegionKernelLogic4InputPlane> planes)
      noexcept;
  /// General plane form accepts Logic4 and Logic9 inputs at any supported
  /// width. It checks plane shape, unused tail bits, reserved Logic9 codes,
  /// and equality with the captured image before resetting the native frame.
  /// A rejected borrowed call leaves the checked image entry available when
  /// take_failure() is empty.
  [[nodiscard]] bool execute_with_input_planes(
      const runtime::simir::RegionKernelActivationImage& image,
      std::span<const runtime::simir::RegionKernelInputPlane> planes)
      noexcept;

  /// Execute a certified ordered prefix of unique SystemVerilog internal
  /// outputs. The current values are aligned with kernel.internal_signals;
  /// generated case-equality operations compare the produced values with
  /// this committed cut and spill one changed bit per output. After false,
  /// the caller must poll take_failure(); only an empty result permits retry.
  /// This is a compiler-level prefix primitive: the caller still owns atomic
  /// sidecar installation, scheduler-key reservation, and readiness
  /// publication.
  [[nodiscard]] bool execute_internal_output_prefix(
      const runtime::simir::RegionKernelActivationImage& image,
      std::span<const runtime::PackedLogic4> current_internal_values,
      std::span<const runtime::simir::RegionConeOutputBinding>
          ordered_prefix) noexcept;

  /// Execute the same certified narrow Logic4 prefix, then write its result
  /// into caller-owned, unpublished A4 replacement planes through the
  /// private typed entry. The caller must keep every destination uniquely
  /// writable and unpublished until this call succeeds. After false, poll
  /// take_failure(); only an empty result permits fallback. No role block was
  /// published by this executor.
  [[nodiscard]] bool execute_internal_output_prefix_prepared(
      const runtime::simir::RegionKernelActivationImage& image,
      std::span<const runtime::PackedLogic4> current_internal_values,
      std::span<const runtime::simir::RegionConeOutputBinding>
          ordered_prefix,
      runtime::simir::RegionPreparedOutputBatchV1& outputs,
      runtime::simir::RegionPreparedOutputSuccessorMasksV1* successors
          = nullptr) noexcept;

  /// Execute the narrow Logic4 prepared prefix from call-scoped direct
  /// planes and the frozen member-ready mask. The metadata-only activation
  /// image must match both generations in `input_window`; false permits retry
  /// only when take_failure() is empty, and declines before changing reusable
  /// frame or output storage.
  [[nodiscard]] bool execute_direct_ready_window_prepared(
      const runtime::simir::RegionKernelActivationImage& image,
      const runtime::simir::RegionDirectReadyWindowV1& input_window,
      std::span<const runtime::PackedLogic4> current_internal_values,
      std::span<const runtime::simir::RegionConeOutputBinding>
          ordered_prefix,
      runtime::simir::RegionPreparedOutputBatchV1& outputs,
      runtime::simir::RegionPreparedOutputSuccessorMasksV1* successors
          = nullptr) noexcept;

  [[nodiscard]] bool supports_direct_ready_window() const noexcept;
  [[nodiscard]] bool supports_direct_ready_window_successor_masks()
      const noexcept;

  /// One generated change bit per binding in the last successfully executed
  /// internal-output prefix. Empty until that entry succeeds.
  [[nodiscard]] std::span<const std::uint8_t>
  internal_output_prefix_changed() const noexcept;

  [[nodiscard]] std::span<const runtime::PackedLogic4>
  activation_registers() const noexcept;

  [[nodiscard]] std::span<const runtime::simir::RegionConeKernelMember>
  members() const noexcept;

  [[nodiscard]] std::span<const runtime::simir::RegionConeOutputBinding>
  outputs() const noexcept;

  /// Canonical region mapping identity supplied to the native object cache.
  [[nodiscard]] std::string_view cache_identity() const noexcept;

private:
  friend struct llvm_detail::RegionKernelTestAccess;

  struct Impl;

  explicit LlvmRegionKernelExecutor(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> impl_;
};

} // namespace fsim::compiler
