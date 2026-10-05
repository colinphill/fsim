// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/compiler/jit_runtime_v2.h"
#include "fsim/compiler/fused_masked_process.hpp"
#include "fsim/runtime/simir.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {
struct RegionConeActivationKernel;
struct RegionPreparedOutputBatchV1;
struct RegionPreparedOutputSuccessorMasksV1;
struct RegionDirectReadyWindowV1;
struct RegionFrontierLayoutV2;
}

namespace fsim::compiler {

namespace llvm_detail {
class RegionKernelActivationCertificate;
class RegionPreparedOutputCertificate;
class RegionDirectReadyOutputCertificate;
struct RegionDirectReadyLoweringBinding;
struct RegionKernelActivationBacking;
}

class LlvmRegionKernelExecutor;
class LlvmRegionFrontierExecutor;

enum class JitOptimizationLevel : std::uint8_t {
  o0,
  o1,
  o2,
};

[[nodiscard]] std::string_view
to_string(JitOptimizationLevel optimization) noexcept;

struct LlvmJitOptions {
  JitOptimizationLevel optimization = JitOptimizationLevel::o2;
  /// Empty disables persistent native-object caching.
  std::filesystem::path cache_directory;
  /// Automatic native-object pruning is best-effort and never blocks JIT
  /// construction. Null limits disable the corresponding policy.
  std::optional<std::uintmax_t> cache_maximum_bytes{
      std::uintmax_t{10} * 1024U * 1024U * 1024U};
  std::optional<std::size_t> cache_maximum_entries{10'000};
  std::optional<std::chrono::seconds> cache_maximum_age{
      std::chrono::hours{24 * 30}};
  /// Retain source-level DebugPoint handoff in generated code. Optimized
  /// simulation disables it; the LLVM debug engine enables it explicitly.
  bool debug_instrumentation = true;
  /// Generated update operations may assume that every advertised direct
  /// update slot is present. Application simulations enable this only when
  /// module-path routing cannot disable callback-free update staging.
  bool require_direct_update_slots = false;
  /// v3 code-coverage model/configuration digest. It is part of every native
  /// object cache identity even when instrumentation is disabled.
  std::string code_coverage_identity { "disabled" };
  /// Require every current ReadSignal to use the validated direct plane map.
  /// The direct_read_signals and wide-offset map pointers and entries, plus
  /// the plane addresses, must remain stable for one resume, and the planes
  /// must be authoritative for every listed current signal. Generated reads
  /// bypass the signal-read callbacks. Plane contents are loaded at each
  /// operation, so signal updates between resumes remain visible. Malformed
  /// backing is rejected before native entry, so callers can choose a checked
  /// fallback. The caller must provide the same signal widths and value kinds
  /// used to compile the process and allocations sized for the ABI counts.
  /// Each narrow slot requires dense aval/bval planes; Logic9 slots also
  /// require all four Logic9 planes.
  bool require_direct_read_signals = false;
};

/// Owning, not-yet-materialized compiler plan for one scheduler frontier.
/// The layout is borrowed only while this object remains alive. Materializing
/// consumes the plan and transfers its descriptors to the returned executor.
class LlvmPreparedRegionFrontier final {
public:
  ~LlvmPreparedRegionFrontier();
  LlvmPreparedRegionFrontier(LlvmPreparedRegionFrontier&&) noexcept;
  LlvmPreparedRegionFrontier& operator=(
      LlvmPreparedRegionFrontier&&) noexcept;
  LlvmPreparedRegionFrontier(const LlvmPreparedRegionFrontier&) = delete;
  LlvmPreparedRegionFrontier& operator=(
      const LlvmPreparedRegionFrontier&) = delete;

  [[nodiscard]] const runtime::simir::RegionFrontierLayoutV2& layout()
      const noexcept;

  /// Optional canonical structure identity for bounded census grouping only.
  /// The view is borrowed until this plan is moved, consumed, or destroyed.
  /// It is distinct from the exact physical JIT cache identity.
  [[nodiscard]] std::optional<std::string_view>
  structural_census_identity() const noexcept;

private:
  friend class LlvmRegionFrontierExecutor;

  struct Impl;
  explicit LlvmPreparedRegionFrontier(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> impl_;
};

struct LlvmJitCacheStatistics {
  std::uint64_t hits{};
  std::uint64_t misses{};
  std::uint64_t stores{};
  std::uint64_t rejected_entries{};
  std::uint64_t load_failures{};
  std::uint64_t store_failures{};
  std::uint64_t pruned_entries{};
  std::uintmax_t pruned_bytes{};
  std::uint64_t prune_failures{};

  friend bool operator==(LlvmJitCacheStatistics,
                         LlvmJitCacheStatistics) = default;
};

struct LlvmNativeHostIdentity {
  std::string fingerprint;
  std::string llvm_version;
  std::string target;
  std::string data_layout;
  std::string cpu;
  std::vector<std::string> features;

  friend bool operator==(
      const LlvmNativeHostIdentity&,
      const LlvmNativeHostIdentity&) = default;
};

/// Opaque process token. It is meaningful only to the LlvmJit that created it.
struct JitProcessHandle {
  std::uint64_t value{};

  [[nodiscard]] explicit operator bool() const noexcept { return value != 0; }
  friend bool operator==(JitProcessHandle, JitProcessHandle) = default;
};

/// Stable native-process binding resolved from a JIT-owned handle.
///
/// Binding performs the synchronized handle lookup once. It remains valid for
/// the lifetime of the LlvmJit that created it and lets scheduler hot paths
/// resume generated code without consulting a concurrently growing handle
/// table.
class JitProcessBinding final {
public:
  constexpr JitProcessBinding() noexcept = default;
  [[nodiscard]] explicit operator bool() const noexcept {
    return owner_ != nullptr && entry_ != nullptr;
  }
  friend bool operator==(JitProcessBinding, JitProcessBinding) = default;

private:
  friend class LlvmJit;

  constexpr JitProcessBinding(const void* owner, const void* entry) noexcept
      : owner_(owner), entry_(entry) {}

  const void* owner_{};
  const void* entry_{};
};

/// Elaborated instance binding for one compiled direct-read slot. The caller
/// certifies the actual signal's width and value kind. Slots retain compiled
/// order; signal IDs may differ from the canonical compilation namespace.
struct JitDirectReadInstanceBinding {
  runtime::simir::SignalId signal { };
  std::uint32_t width { };
  runtime::simir::ValueKind kind { runtime::simir::ValueKind::logic4 };
};

/// Bind-once validation lease for an immutable required-direct-read backing.
/// The caller must keep the runtime, frame, result, services descriptor,
/// context, direct-read map, and backing storage at the same addresses, and
/// keep services contents, direct-read map contents, and direct-update slot
/// descriptors (including wide-plane pointers and extents) unchanged for the
/// lease lifetime. Signal values, update-slot values, masks, and activity
/// bits remain mutable.
class JitRequiredDirectReadLease final {
public:
  constexpr JitRequiredDirectReadLease() noexcept = default;
  [[nodiscard]] explicit operator bool() const noexcept {
    return owner_ != nullptr && entry_ != nullptr;
  }

private:
  friend class LlvmJit;

  void capture_backing(
      const fsim_jit_runtime_instance_v2& runtime,
      const fsim_jit_frame_v2& frame,
      const fsim_jit_resume_result_v2& result) noexcept;
  [[nodiscard]] bool matches_backing(
      const void* owner,
      const void* entry,
      JitProcessBinding process,
      const fsim_jit_runtime_instance_v2& runtime,
      const fsim_jit_frame_v2& frame,
      const fsim_jit_resume_result_v2& result) const noexcept;

  const void* owner_ { };
  const void* entry_ { };
  JitProcessBinding process_ { };
  std::array<const void*, 32U> pointers_ { };
  std::array<std::uint64_t, 32U> shape_ { };
};

/// Stable binding for one exact ordered native process cohort.
///
/// The binding retains the already-resolved process functions and the stable
/// runtime, frame, result, and scheduler-state addresses supplied by the
/// owning scheduler. It is meaningful only to the LlvmJit that created it.
class JitProcessCohortBinding final {
public:
  constexpr JitProcessCohortBinding() noexcept = default;
  [[nodiscard]] explicit operator bool() const noexcept {
    return owner_ != nullptr && entry_ != nullptr;
  }
  friend bool operator==(
      JitProcessCohortBinding, JitProcessCohortBinding) = default;

private:
  friend class LlvmJit;

  constexpr JitProcessCohortBinding(
      const void* owner, const void* entry,
      std::uint64_t generation) noexcept
      : owner_(owner), entry_(entry), generation_(generation) {}

  const void* owner_{};
  const void* entry_{};
  std::uint64_t generation_{};
};

/// Caller-owned activation records for one ordered native process cohort.
struct JitProcessCohortResumeEntry {
  JitProcessCohortResumeEntry() noexcept = default;
  JitProcessCohortResumeEntry(
      JitProcessBinding process_value,
      const fsim_jit_runtime_instance_v2& runtime_value,
      fsim_jit_frame_v2& frame_value,
      fsim_jit_resume_result_v2& result_value,
      std::uint8_t* queued_value = nullptr,
      std::uint8_t* waiting_on_static_value = nullptr,
      std::uint8_t* process_status_value = nullptr,
      std::uint8_t* active_value = nullptr) noexcept
      : process(process_value), runtime(&runtime_value), frame(&frame_value),
        result(&result_value), queued(queued_value),
        waiting_on_static(waiting_on_static_value),
        process_status(process_status_value), active(active_value) {}

  JitProcessBinding process;
  const fsim_jit_runtime_instance_v2* runtime {};
  fsim_jit_frame_v2* frame {};
  fsim_jit_resume_result_v2* result {};
  std::uint8_t* queued {};
  std::uint8_t* waiting_on_static {};
  std::uint8_t* process_status {};
  std::uint8_t* active {};
  std::uint32_t status {};
  std::exception_ptr failure;
};

enum class JitExecutionStatus : std::uint32_t {
  completed = FSIM_JIT_RESUME_STATUS_COMPLETED_V2,
  assertion_failed = FSIM_JIT_RESUME_STATUS_ASSERTION_FAILED_V2,
  stopped = FSIM_JIT_RESUME_STATUS_STOPPED_V2,
};

enum class JitResumeStatus : std::uint32_t {
  completed = FSIM_JIT_RESUME_STATUS_COMPLETED_V2,
  assertion_failed = FSIM_JIT_RESUME_STATUS_ASSERTION_FAILED_V2,
  wait_for = FSIM_JIT_RESUME_STATUS_WAIT_FOR_V2,
  yielded = FSIM_JIT_RESUME_STATUS_YIELDED_V2,
  stopped = FSIM_JIT_RESUME_STATUS_STOPPED_V2,
  wait_on = FSIM_JIT_RESUME_STATUS_WAIT_ON_V2,
  wait_sensitivity = FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY_V2,
  debug_point = FSIM_JIT_RESUME_STATUS_DEBUG_POINT_V2,
  wait_forever = FSIM_JIT_RESUME_STATUS_WAIT_FOREVER_V2,
  paused = FSIM_JIT_RESUME_STATUS_PAUSED_V2,
  fork = FSIM_JIT_RESUME_STATUS_FORK_V2,
  fork_end = FSIM_JIT_RESUME_STATUS_FORK_END_V2,
  wait_fork = FSIM_JIT_RESUME_STATUS_WAIT_FORK_V2,
  disable_fork = FSIM_JIT_RESUME_STATUS_DISABLE_FORK_V2,
  simir_boundary = FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY_V2,
};

struct JitProcessFrameLayout {
  std::uint64_t layout_id_low{};
  std::uint64_t layout_id_high{};
  std::uint32_t register_count{};
  std::uint32_t register_word_count { };
  std::uint32_t string_register_count{};
  bool uses_logic9{};
  bool tracks_register_initialization{true};
  std::vector<std::uint32_t> register_widths;
  std::vector<std::uint32_t> register_word_offsets;
  std::vector<runtime::simir::SignalId> direct_read_signals;
  std::vector<runtime::simir::SignalId> direct_update_signals;
  /// Canonical SimIR signal operands whose callback IDs are loaded from the
  /// caller-owned frame tail after application binding. These remain source
  /// IDs in compiled metadata; each executor binds its own actual IDs.
  std::vector<runtime::simir::SignalId> signal_callback_operands;
  /// Word offset immediately after the packed source-register planes.
  /// The callback operand tail contains one uint32 ID per word.
  std::uint32_t signal_callback_operand_word_base { };
  /// True only for entries whose signal callback arguments are read from the
  /// per-instance frame tail and therefore already name actual signals.
  bool signal_callback_ids_are_actual { };
  /// One byte per source register. A set byte means the lowered executor
  /// stores that register's value in its caller-owned frame at suspension.
  std::vector<std::uint8_t> register_values_persistent;

  friend bool operator==(const JitProcessFrameLayout&,
      const JitProcessFrameLayout&) = default;
};

/// One externally named process function within a compiled LLVM module.
///
/// The pointed-to SimIR process is consumed synchronously by
/// add_process_module() and need not outlive that call.
enum class JitBackendTierHint : std::uint8_t {
  none,
  shared_process_template,
  fused_static_cohort,
  fused_masked_region,
};

[[nodiscard]] constexpr bool jit_backend_tier_hint_eligible(
    const JitBackendTierHint hint,
    const std::size_t bound_instance_count) noexcept
{
  switch (hint) {
  case JitBackendTierHint::shared_process_template:
    return bound_instance_count >= 64U;
  case JitBackendTierHint::fused_static_cohort:
  case JitBackendTierHint::fused_masked_region:
    return bound_instance_count != 0U;
  case JitBackendTierHint::none:
    return false;
  }
  return false;
}

struct JitProcessModuleEntry {
  std::string_view symbol;
  const runtime::simir::Process* process{};
  /// Sorted instruction indexes whose known narrow literals are loaded from
  /// the embedding's instance-local container_operation callback.
  std::vector<runtime::simir::InstructionIndex> bound_literal_sites { };
  std::vector<FusedMaskedMemberGate> masked_member_gates { };
  /// Explicit cold backend-tier eligibility; ordinary entries default to
  /// None. Shared templates require at least 64 bound instances.
  JitBackendTierHint backend_tier_hint { JitBackendTierHint::none };
  /// Exact count of executors bound to this compiled entry when hinted.
  std::size_t bound_instance_count { 1U };
  /// Require callback-free reads from the validated direct signal planes for
  /// this entry, even when the LlvmJit-wide default remains guarded.
  bool require_direct_read_signals { };
  /// Permit the bounded Less-tier marked-load CSE pass for this entry. This
  /// requires required direct reads and a fused static/masked tier hint.
  bool tiered_read_dedup_safe { };
  /// Opt into a compact frame-tail table for per-instance actual signal IDs.
  /// Ordinary JIT entries keep the legacy canonical-ID callback contract.
  bool signal_callback_ids_are_actual { };
};

class LlvmJitError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

/// A valid SimIR process that is outside the current LLVM adapter subset.
///
/// Callers may catch this narrower type to select the reference interpreter.
/// Malformed SimIR, ABI mismatches, and materialization failures continue to
/// report LlvmJitError directly. Generated-code language failures use
/// LlvmJitGeneratedRuntimeError below.
class LlvmJitUnsupportedError : public LlvmJitError {
public:
  using LlvmJitError::LlvmJitError;
};

enum class JitGeneratedRuntimeErrorReason : std::uint8_t {
    unknown_branch_condition,
    integer_operand_unknown,
    integer_overflow,
    integer_division_by_zero,
    integer_negative_exponent,
    integer_subtype_range,
    dynamic_index_unknown,
    dynamic_index_range,
    call_stack_unknown,
    call_stack_overflow,
    call_stack_underflow,
    call_stack_target,
    string_callback_failure,
    file_callback_failure,
    container_callback_failure,
    signal_callback_failure,
    coverage_callback_failure,
    native_service_callback_failure,
    fused_activation_invalid,
};

/// A failure deliberately reported by generated SimIR code.
///
/// This is distinct from adapter, ABI, cache, and ORC failures so a scheduler
/// can translate the generated-language failure without treating compiler
/// infrastructure errors as an interpreter fallback.
class LlvmJitGeneratedRuntimeError : public LlvmJitError {
public:
  LlvmJitGeneratedRuntimeError(
      std::uint32_t instruction,
      JitGeneratedRuntimeErrorReason reason);

  [[nodiscard]] std::uint32_t instruction() const noexcept {
    return instruction_;
  }
  [[nodiscard]] JitGeneratedRuntimeErrorReason reason() const noexcept {
    return reason_;
  }

private:
  std::uint32_t instruction_;
  JitGeneratedRuntimeErrorReason reason_;
};

/// Narrow LLVM ORC adapter for the first compiled SimIR subset.
///
/// Supported processes may use constant/read/copy/extract/insert/concatenate
/// value operations, typed unary/binary operations, whole or partial
/// blocking/update/delayed writes, assertions, control flow, waits, yields,
/// stop/halt, and source-bearing DebugPoint operations. Control flow is lowered
/// to LLVM basic blocks backed by a versioned caller-owned frame. DebugPoint
/// boundaries are returned only when the runtime enables them. Scalar runtime
/// callbacks accept values up to 64 bits; wider values use the process frame
/// and packed or exact-width callbacks. Control-flow cycles are supported;
/// the backend does not insert scheduler safe points on backedges, so an
/// unbounded loop without an existing boundary can remain in one resume call.
///
/// Persistent caching is opt-in through LlvmJitOptions::cache_directory.
/// Cached native objects are checksummed and keyed to the complete supported
/// SimIR module plus the native compilation environment. Construction performs
/// best-effort deterministic LRU pruning using configurable age, entry-count,
/// and encoded-byte limits.
class LlvmJit final {
public:
  explicit LlvmJit(LlvmJitOptions options = {});
  ~LlvmJit();
  LlvmJit(LlvmJit &&) noexcept;
  LlvmJit &operator=(LlvmJit &&) noexcept;
  LlvmJit(const LlvmJit &) = delete;
  LlvmJit &operator=(const LlvmJit &) = delete;

  /// Select a previously verified immutable-design content identity before
  /// adding modules. This permits compact native cache keys without trusting
  /// caller-controlled paths or mutable project state.
  void set_immutable_design_identity(std::string identity);

  /// Return whether a well-formed process is in the compiled subset.
  ///
  /// Malformed SimIR still throws LlvmJitError. A valid capability miss
  /// returns false so a hybrid caller can omit only that process from a
  /// specialization module and retain interpreter fallback.
  [[nodiscard]] bool supports_process(
      const runtime::simir::Process& process,
      std::span<const std::uint32_t> signal_widths,
      std::span<const runtime::simir::ValueKind>
          signal_value_kinds = {}) const;

  /// Release a cached preflight summary when a process reuses another
  /// process's native body through an external signal remap. Returns whether
  /// a summary was present; the process remains eligible for later validation.
  [[nodiscard]] bool discard_prevalidated_process(
      const runtime::simir::Process& process) const;

  /// Validate, lower, optimize, and add several process functions as one
  /// native compilation/cache unit.
  ///
  /// module_identity must be non-empty. Entries and symbols must be non-empty
  /// and unique, and every process must be supported. The operation is atomic:
  /// no symbol is registered when validation or lowering fails.
  void add_process_module(
      std::string_view module_identity,
      std::span<const JitProcessModuleEntry> entries,
      std::span<const std::uint32_t> signal_widths,
      std::span<const runtime::simir::ValueKind>
          signal_value_kinds = {});

  /// Add one process as a one-function module.
  ///
  /// This compatibility wrapper uses symbol as the module identity.
  void add_process(std::string_view symbol,
                   const runtime::simir::Process &process,
                   std::span<const std::uint32_t> signal_widths,
                   std::span<const runtime::simir::ValueKind>
                       signal_value_kinds,
                   JitBackendTierHint backend_tier_hint,
                   std::size_t bound_instance_count,
                   bool require_direct_read_signals = false,
                   bool tiered_read_dedup_safe = false);
  void add_process(std::string_view symbol,
                   const runtime::simir::Process &process,
                   std::span<const std::uint32_t> signal_widths,
                   std::span<const runtime::simir::ValueKind>
                       signal_value_kinds = {});

  void add_masked_process(std::string_view symbol,
      const FusedMaskedProcess& process,
      std::span<const std::uint32_t> signal_widths,
      std::span<const runtime::simir::ValueKind> signal_value_kinds,
      std::size_t bound_instance_count,
      bool require_direct_read_signals = false,
      bool tiered_read_dedup_safe = false);
  void add_masked_process(std::string_view symbol,
      const FusedMaskedProcess& process,
      std::span<const std::uint32_t> signal_widths,
      std::span<const runtime::simir::ValueKind> signal_value_kinds = {});

  /// Compile/materialize a symbol through ORC and return an opaque handle.
  [[nodiscard]] JitProcessHandle lookup(std::string_view symbol);

  /// Resolve a process handle once into stable native storage.
  [[nodiscard]] JitProcessBinding bind(JitProcessHandle process) const;

  /// True when a compiled process contains native code for this entry PC.
  [[nodiscard]] bool supports_entry(
      JitProcessHandle process,
      runtime::simir::InstructionIndex instruction) const;
  [[nodiscard]] bool supports_entry(
      JitProcessBinding process,
      runtime::simir::InstructionIndex instruction) const;

  /// Return the caller-owned frame layout required by a compiled process.
  [[nodiscard]] JitProcessFrameLayout
  frame_layout(JitProcessHandle process) const;
  [[nodiscard]] JitProcessFrameLayout
  frame_layout(JitProcessBinding process) const;

  /// Operation-stream bound validated when this native entry was compiled.
  [[nodiscard]] std::uint32_t
  operation_count(JitProcessBinding process) const;

  /// Initialize a caller-owned v2 frame, zero its value storage, and mark
  /// every register unavailable until generated code first writes it.
  ///
  /// Each span must contain at least frame_layout().register_count elements
  /// and remain alive for every resume() using the frame.
  void initialize_frame(JitProcessHandle process, fsim_jit_frame_v2 &frame,
                        std::span<std::uint64_t> register_aval,
                        std::span<std::uint64_t> register_bval,
                        std::span<std::uint8_t> register_initialized,
                        std::span<std::uint64_t> register_logic9_plane2 = {},
                        std::span<std::uint64_t> register_logic9_plane3 = {})
      const;
  void initialize_frame(JitProcessBinding process, fsim_jit_frame_v2 &frame,
                        std::span<std::uint64_t> register_aval,
                        std::span<std::uint64_t> register_bval,
                        std::span<std::uint8_t> register_initialized,
                        std::span<std::uint64_t> register_logic9_plane2 = {},
                        std::span<std::uint64_t> register_logic9_plane3 = {})
      const;

  /// Run from the frame PC until completion, failure, Stop, or suspension.
  ///
  /// result must advertise the v2 result ABI and structure size. WaitFor
  /// reports its delay without scheduling it; WaitOn and WaitSensitivity
  /// report the immutable SimIR instruction containing their operands; Yield
  /// reports a next-delta suspension. The caller decides when to invoke
  /// resume() again.
  [[nodiscard]] JitResumeStatus
  resume(JitProcessHandle process, const fsim_jit_runtime_instance_v2 &runtime,
         fsim_jit_frame_v2 &frame,
         fsim_jit_resume_result_v2 &result) const;
  [[nodiscard]] JitResumeStatus
  resume(JitProcessBinding process, const fsim_jit_runtime_instance_v2 &runtime,
         fsim_jit_frame_v2 &frame,
         fsim_jit_resume_result_v2 &result) const;

  /// Resume a stable binding through the checked public activation path. The
  /// binding skips only the synchronized handle lookup; runtime, frame, result,
  /// and required-service checks still run before generated code is entered.
  [[nodiscard]] JitResumeStatus resume_prevalidated(
      JitProcessBinding process, const fsim_jit_runtime_instance_v2 &runtime,
      fsim_jit_frame_v2 &frame, fsim_jit_resume_result_v2 &result) const;

  /// Validate a callback-free required-direct-read entry and its caller-owned
  /// backing once. An empty optional means the entry or backing is unsupported.
  [[nodiscard]] std::optional<JitRequiredDirectReadLease>
  bind_required_direct_read_prevalidated(
      JitProcessBinding process,
      const fsim_jit_runtime_instance_v2& runtime,
      fsim_jit_frame_v2& frame,
      fsim_jit_resume_result_v2& result) const;

  /// Validate an explicit instance mapping against the compiled slot types and
  /// runtime map. The binding span is consumed during this call and need not
  /// persist. The runtime map and backing obey the same immutable lease
  /// contract as the identity-mapped overload above. A malformed capability
  /// declines without changing frame, result, or signal values.
  [[nodiscard]] std::optional<JitRequiredDirectReadLease>
  bind_mapped_required_direct_read_prevalidated(
      JitProcessBinding process,
      const fsim_jit_runtime_instance_v2& runtime,
      fsim_jit_frame_v2& frame,
      fsim_jit_resume_result_v2& result,
      std::span<const JitDirectReadInstanceBinding> bindings) const;

  /// Resume only when the previously validated entry and backing identities
  /// still match. The optional is empty on a pre-entry mismatch; errors after
  /// entering generated code propagate and are never eligible for fallback.
  [[nodiscard]] std::optional<JitResumeStatus>
  resume_required_direct_read_prevalidated(
      const JitRequiredDirectReadLease& lease,
      JitProcessBinding process,
      const fsim_jit_runtime_instance_v2& runtime,
      fsim_jit_frame_v2& frame,
      fsim_jit_resume_result_v2& result) const;

  /// Resume an exact ordered cohort through one generated native wrapper.
  /// The wrapper stops after the first status other than WaitSensitivity so
  /// the scheduler can preserve canonical boundary handling.
  [[nodiscard]] std::size_t resume_cohort_prevalidated(
      std::span<JitProcessCohortResumeEntry> entries) const;

  /// Checked ordered-wave wrapper, bounded to 64 members and at most 16 new
  /// wrapper materialization attempts per JIT. Reuse of existing shapes does
  /// not consume this budget. Zero declines before native execution, allowing
  /// ordinary checked activation; generated code and state remain unchanged.
  [[nodiscard]] std::size_t resume_ordered_cohort_prevalidated(
      std::span<JitProcessCohortResumeEntry> entries) const;

  /// Bind the exact cohort supplied to resume_cohort_prevalidated(). The
  /// caller must keep every referenced runtime, frame, result, and scheduler
  /// state object at the same address for the lifetime of the binding.
  [[nodiscard]] JitProcessCohortBinding bind_cohort_prevalidated(
      std::span<JitProcessCohortResumeEntry> entries) const;

  /// Release one bound cohort after its caller invalidates the saved member
  /// and scheduler-state addresses. A stale binding cannot release a newer one.
  [[nodiscard]] bool release_cohort_binding(
      JitProcessCohortBinding cohort) const;
  [[nodiscard]] std::size_t active_cohort_binding_count() const;

  /// Resume a previously bound exact cohort without rebuilding member hashes
  /// and pointer arrays. Entries provide only per-activation status/failure
  /// destinations and must retain the order used when binding.
  [[nodiscard]] std::size_t resume_cohort_prevalidated(
      JitProcessCohortBinding cohort,
      std::span<JitProcessCohortResumeEntry> entries) const;

 /// It still checks the mutable runtime debug flag and frame
  /// Execute a previously looked-up process.
  ///
  /// This compatibility helper creates a temporary frame. It rejects processes
  /// containing WaitFor or Yield before executing them; use resume() for those.
  /// Callback exceptions must not cross the generated plain-C ABI.
  [[nodiscard]] JitExecutionStatus
  execute(JitProcessHandle process,
          const fsim_jit_runtime_instance_v2 &runtime) const;

  /// Snapshot persistent-cache activity for this JIT instance.
  ///
  /// Lookup/materialization, rather than add_process(), performs native cache
  /// reads and writes. A rejected entry is recompiled and replaced.
  [[nodiscard]] LlvmJitCacheStatistics cache_statistics() const noexcept;

  /// LLVM version used to compile this adapter (for diagnostics/cache keys).
  [[nodiscard]] static std::string_view llvm_version() noexcept;

  /// Complete source-independent native-code admission identity. This uses
  /// the same host detection, ABI constants, LLVM version, target, data
  /// layout, CPU, sorted feature set, and native object-cache schema as
  /// persistent object-cache keys.
  [[nodiscard]] static LlvmNativeHostIdentity native_host_identity(
      JitOptimizationLevel optimization);

private:
  friend class LlvmRegionKernelExecutor;
  friend class LlvmRegionFrontierExecutor;

  struct Impl;
  [[nodiscard]] std::optional<JitRequiredDirectReadLease>
  bind_required_direct_read_impl(
      JitProcessBinding process,
      const fsim_jit_runtime_instance_v2& runtime,
      fsim_jit_frame_v2& frame,
      fsim_jit_resume_result_v2& result,
      std::optional<std::span<const JitDirectReadInstanceBinding>> bindings) const;

  [[nodiscard]] JitResumeStatus resume_impl(
      JitProcessBinding process,
      const fsim_jit_runtime_instance_v2& runtime,
      fsim_jit_frame_v2& frame,
      fsim_jit_resume_result_v2& result,
      bool direct_backing_prevalidated,
      bool* native_entered) const;
  [[nodiscard]] llvm_detail::RegionKernelActivationCertificate
  bind_region_activation(
      JitProcessBinding process,
      const fsim_jit_runtime_instance_v2& runtime,
      fsim_jit_frame_v2& frame,
      fsim_jit_resume_result_v2& result,
      const JitProcessFrameLayout& layout,
      const llvm_detail::RegionKernelActivationBacking& backing);
  [[nodiscard]] bool matches_region_activation(
      const llvm_detail::RegionKernelActivationCertificate& certificate,
      JitProcessBinding process,
      const fsim_jit_runtime_instance_v2& runtime,
      const fsim_jit_frame_v2& frame,
      const fsim_jit_resume_result_v2& result) const noexcept;
  [[nodiscard]] bool resume_region_activation(
      const llvm_detail::RegionKernelActivationCertificate& certificate,
      JitProcessBinding process,
      const fsim_jit_runtime_instance_v2& runtime,
      fsim_jit_frame_v2& frame,
      fsim_jit_resume_result_v2& result,
      std::uint32_t& raw_status) const noexcept;
  [[nodiscard]] bool resume_region_activation(
      const llvm_detail::RegionKernelActivationCertificate& certificate,
      JitProcessBinding process,
      const fsim_jit_runtime_instance_v2& runtime,
      fsim_jit_frame_v2& frame,
      fsim_jit_resume_result_v2& result,
      std::uint32_t& raw_status,
      std::exception_ptr& failure) const noexcept;
  [[nodiscard]] llvm_detail::RegionPreparedOutputCertificate
  bind_region_prepared_output(
      JitProcessBinding process,
      const fsim_jit_runtime_instance_v2& runtime,
      fsim_jit_frame_v2& frame,
      fsim_jit_resume_result_v2& result,
      const JitProcessFrameLayout& layout,
      const llvm_detail::RegionKernelActivationBacking& backing);
  [[nodiscard]] bool matches_region_prepared_output(
      const llvm_detail::RegionPreparedOutputCertificate& certificate,
      JitProcessBinding process,
      const fsim_jit_runtime_instance_v2& runtime,
      const fsim_jit_frame_v2& frame,
      const fsim_jit_resume_result_v2& result) const noexcept;
  [[nodiscard]] bool resume_region_prepared_output(
      const llvm_detail::RegionPreparedOutputCertificate& certificate,
      JitProcessBinding process,
      const fsim_jit_runtime_instance_v2& runtime,
      fsim_jit_frame_v2& frame,
      fsim_jit_resume_result_v2& result,
      const runtime::simir::RegionPreparedOutputBatchV1& outputs,
      std::uint32_t& raw_status,
      runtime::simir::RegionPreparedOutputSuccessorMasksV1* successors
          = nullptr) const noexcept;
  [[nodiscard]] bool resume_region_prepared_output(
      const llvm_detail::RegionPreparedOutputCertificate& certificate,
      JitProcessBinding process,
      const fsim_jit_runtime_instance_v2& runtime,
      fsim_jit_frame_v2& frame,
      fsim_jit_resume_result_v2& result,
      const runtime::simir::RegionPreparedOutputBatchV1& outputs,
      std::uint32_t& raw_status,
      runtime::simir::RegionPreparedOutputSuccessorMasksV1* successors,
      std::exception_ptr& failure) const noexcept;
  [[nodiscard]] llvm_detail::RegionDirectReadyOutputCertificate
  bind_region_direct_ready_output(
      JitProcessBinding process,
      const fsim_jit_runtime_instance_v2& runtime,
      fsim_jit_frame_v2& frame,
      fsim_jit_resume_result_v2& result,
      const JitProcessFrameLayout& layout,
      const llvm_detail::RegionKernelActivationBacking& backing);
  [[nodiscard]] bool matches_region_direct_ready_output(
      const llvm_detail::RegionDirectReadyOutputCertificate& certificate,
      JitProcessBinding process,
      const fsim_jit_runtime_instance_v2& runtime,
      const fsim_jit_frame_v2& frame,
      const fsim_jit_resume_result_v2& result) const noexcept;
  [[nodiscard]] bool resume_region_direct_ready_output(
      const llvm_detail::RegionDirectReadyOutputCertificate& certificate,
      JitProcessBinding process,
      const fsim_jit_runtime_instance_v2& runtime,
      fsim_jit_frame_v2& frame,
      fsim_jit_resume_result_v2& result,
      const runtime::simir::RegionDirectReadyWindowV1& input_window,
      const runtime::simir::RegionPreparedOutputBatchV1& outputs,
      runtime::simir::RegionPreparedOutputSuccessorMasksV1* successors,
      std::uint32_t& raw_status) const noexcept;
  [[nodiscard]] bool resume_region_direct_ready_output(
      const llvm_detail::RegionDirectReadyOutputCertificate& certificate,
      JitProcessBinding process,
      const fsim_jit_runtime_instance_v2& runtime,
      fsim_jit_frame_v2& frame,
      fsim_jit_resume_result_v2& result,
      const runtime::simir::RegionDirectReadyWindowV1& input_window,
      const runtime::simir::RegionPreparedOutputBatchV1& outputs,
      runtime::simir::RegionPreparedOutputSuccessorMasksV1* successors,
      std::uint32_t& raw_status,
      std::exception_ptr& failure) const noexcept;

  /// Compile the region wrapper's private known-Logic4 body. This entry is
  /// inaccessible to general JIT clients; the region executor performs the
  /// full input-plane preflight before selecting it.
  void add_region_known_logic4_process(
      std::string_view symbol,
      const runtime::simir::Process& process,
      std::span<const std::uint32_t> signal_widths,
      std::span<const runtime::simir::ValueKind> signal_value_kinds,
      JitBackendTierHint backend_tier_hint,
      std::size_t bound_instance_count);
  /// Compile the region wrapper's private prepared-output prefix entry beside
  /// its ordinary three-argument activation body. The extra typed entry is
  /// never registered as a general JIT process symbol.
  void add_region_prepared_output_process(
      std::string_view symbol,
      const runtime::simir::Process& process,
      std::span<const std::uint32_t> signal_widths,
      std::span<const runtime::simir::ValueKind> signal_value_kinds,
      std::span<const std::uint32_t> output_signals,
      JitBackendTierHint backend_tier_hint,
      std::size_t bound_instance_count,
      std::span<const llvm_detail::RegionDirectReadyLoweringBinding>
          direct_ready_bindings = { },
      std::span<const std::uint64_t> successor_member_masks = { });
  void add_process_module_impl(
      std::string_view module_identity,
      std::span<const JitProcessModuleEntry> entries,
      std::span<const std::uint32_t> signal_widths,
      std::span<const runtime::simir::ValueKind> signal_value_kinds,
      bool region_known_logic4,
      std::span<const std::uint32_t> prepared_output_signals = { },
      std::span<const llvm_detail::RegionDirectReadyLoweringBinding>
          direct_ready_bindings = { },
      std::span<const std::uint64_t> successor_member_masks = { });

  [[nodiscard]] std::size_t resume_cohort_prevalidated_impl(
      std::span<JitProcessCohortResumeEntry> entries,
      bool ordered_bounded) const;
  std::unique_ptr<Impl> impl_;
};

} // namespace fsim::compiler
