// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/compiler/jit_runtime.h"
#include "fsim/compiler/fused_masked_process.hpp"
#include "fsim/runtime/simir.hpp"

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

namespace fsim::compiler {

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

/// Per-process immutable SimIR classification used to assemble ordered waves.
/// It is valid only for the LlvmJit instance that created it.
class JitPureWaveMemberBinding final {
public:
  constexpr JitPureWaveMemberBinding() noexcept = default;
  [[nodiscard]] explicit operator bool() const noexcept {
    return owner_ != nullptr && entry_ != nullptr;
  }
  friend bool operator==(
      JitPureWaveMemberBinding, JitPureWaveMemberBinding) = default;

private:
  friend class LlvmJit;

  constexpr JitPureWaveMemberBinding(
      const void* owner, const void* entry,
      std::uint64_t generation) noexcept
      : owner_(owner), entry_(entry), generation_(generation) {}

  const void* owner_{};
  const void* entry_{};
  std::uint64_t generation_{};
};

/// Owning view of one prepared pure-wave member. Keep one with its executor;
/// spans passed to the trusted wave entry point may then contain pointers to
/// these stable views without copying shared ownership on each activation.
class JitPureWaveMemberLease final {
public:
  JitPureWaveMemberLease() noexcept = default;
  [[nodiscard]] explicit operator bool() const noexcept {
    return owner_ != nullptr && entry_ != nullptr && view_ != nullptr
        && storage_ != nullptr;
  }
  /// Return the stable compiler-owned view pinned by this lease. The pointer
  /// is valid only while this lease remains alive and its binding is unreleased.
  [[nodiscard]] const void* prepared_view() const noexcept {
    return storage_ != nullptr ? view_ : nullptr;
  }
  /// Generation of the compiler binding represented by prepared_view().
  [[nodiscard]] std::uint64_t prepared_generation() const noexcept {
    return storage_ != nullptr ? generation_ : 0U;
  }
  /// Compiler/JIT identity that owns this prepared view.
  [[nodiscard]] const void* prepared_domain() const noexcept {
    return storage_ != nullptr ? owner_ : nullptr;
  }

private:
  friend class LlvmJit;

  JitPureWaveMemberLease(
      const void* owner, const void* entry, const void* view,
      std::uint64_t generation,
      std::shared_ptr<const void> storage) noexcept
      : owner_(owner), entry_(entry), view_(view), generation_(generation),
        storage_(std::move(storage)) {}

  const void* owner_ {};
  const void* entry_ {};
  const void* view_ {};
  std::uint64_t generation_ {};
  std::shared_ptr<const void> storage_;
};

/// Caller-owned activation records for one ordered native process cohort.
struct JitProcessCohortResumeEntry {
  JitProcessCohortResumeEntry() noexcept = default;
  JitProcessCohortResumeEntry(
      JitProcessBinding process_value,
      const fsim_jit_runtime_v1& runtime_value,
      fsim_jit_frame_v1& frame_value,
      fsim_jit_resume_result_v1& result_value,
      std::uint8_t* queued_value = nullptr,
      std::uint8_t* waiting_on_static_value = nullptr,
      std::uint8_t* process_status_value = nullptr,
      std::uint8_t* active_value = nullptr) noexcept
      : process(process_value), runtime(&runtime_value), frame(&frame_value),
        result(&result_value), queued(queued_value),
        waiting_on_static(waiting_on_static_value),
        process_status(process_status_value), active(active_value) {}

  JitProcessBinding process;
  const fsim_jit_runtime_v1* runtime {};
  fsim_jit_frame_v1* frame {};
  fsim_jit_resume_result_v1* result {};
  std::uint8_t* queued {};
  std::uint8_t* waiting_on_static {};
  std::uint8_t* process_status {};
  std::uint8_t* active {};
  std::uint32_t status {};
  std::exception_ptr failure;
};

/// One process in a caller-certified callback-free pure wave. The native
/// activation stores the scheduler/runtime addresses; process identifies the
/// immutable SimIR body checked when the wave binding is created.
struct JitPureWaveMember {
  JitProcessCohortResumeEntry native;
  const runtime::simir::Process* process {};
  std::span<const runtime::simir::SignalId> direct_update_signals;
};

/// One native register slot used by a supported Logic4 bit-and cohort member.
/// The logical ID addresses the initialization bitmap; the word offset and
/// width describe the physical register planes in the member frame. A
/// nonresident slot uses activation-local scratch and leaves frame planes and
/// initialization bytes untouched.
struct JitProcessCohortLogic4BitAndRegisterSlot {
  runtime::simir::RegisterId register_id {};
  std::uint32_t word_offset {};
  std::uint32_t width {};
  bool frame_resident {};

  friend bool operator==(
      JitProcessCohortLogic4BitAndRegisterSlot,
      JitProcessCohortLogic4BitAndRegisterSlot) = default;
};

/// Shape-only description of one caller-certified Logic4 bit-and cohort
/// member. Signal identities are represented by runtime table indices, so
/// generated code can be reused across instances with the same shape.
struct JitProcessCohortLogic4BitAndMember {
  JitProcessCohortLogic4BitAndRegisterSlot read_lhs;
  JitProcessCohortLogic4BitAndRegisterSlot read_rhs;
  JitProcessCohortLogic4BitAndRegisterSlot extract_lhs;
  JitProcessCohortLogic4BitAndRegisterSlot extract_rhs;
  JitProcessCohortLogic4BitAndRegisterSlot result;
  std::uint32_t direct_read_lhs_slot {};
  std::uint32_t direct_read_rhs_slot {};
  std::uint32_t extract_lhs_offset {};
  std::uint32_t extract_rhs_offset {};
  std::uint32_t direct_update_slot {};
  std::uint32_t update_offset {};
  bool tracks_register_initialization {};

  friend bool operator==(
      const JitProcessCohortLogic4BitAndMember&,
      const JitProcessCohortLogic4BitAndMember&) = default;
};

enum class JitExecutionStatus : std::uint32_t {
  completed = FSIM_JIT_RESUME_STATUS_COMPLETED,
  assertion_failed = FSIM_JIT_RESUME_STATUS_ASSERTION_FAILED,
  stopped = FSIM_JIT_RESUME_STATUS_STOPPED,
};

enum class JitResumeStatus : std::uint32_t {
  completed = FSIM_JIT_RESUME_STATUS_COMPLETED,
  assertion_failed = FSIM_JIT_RESUME_STATUS_ASSERTION_FAILED,
  wait_for = FSIM_JIT_RESUME_STATUS_WAIT_FOR,
  yielded = FSIM_JIT_RESUME_STATUS_YIELDED,
  stopped = FSIM_JIT_RESUME_STATUS_STOPPED,
  wait_on = FSIM_JIT_RESUME_STATUS_WAIT_ON,
  wait_sensitivity = FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY,
  debug_point = FSIM_JIT_RESUME_STATUS_DEBUG_POINT,
  wait_forever = FSIM_JIT_RESUME_STATUS_WAIT_FOREVER,
  paused = FSIM_JIT_RESUME_STATUS_PAUSED,
  fork = FSIM_JIT_RESUME_STATUS_FORK,
  fork_end = FSIM_JIT_RESUME_STATUS_FORK_END,
  wait_fork = FSIM_JIT_RESUME_STATUS_WAIT_FORK,
  disable_fork = FSIM_JIT_RESUME_STATUS_DISABLE_FORK,
  simir_boundary = FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
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

  friend bool operator==(const JitProcessFrameLayout&,
      const JitProcessFrameLayout&) = default;
};

/// One externally named process function within a compiled LLVM module.
///
/// The pointed-to SimIR process is consumed synchronously by
/// add_process_module() and need not outlive that call.
struct JitProcessModuleEntry {
  std::string_view symbol;
  const runtime::simir::Process* process{};
  /// Sorted instruction indexes whose known narrow literals are loaded from
  /// the embedding's instance-local container_operation callback.
  std::vector<runtime::simir::InstructionIndex> bound_literal_sites { };
  std::vector<FusedMaskedMemberGate> masked_member_gates { };
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
                       signal_value_kinds = {});

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

  /// Initialize a caller-owned v1 frame, zero its value storage, and mark
  /// every register unavailable until generated code first writes it.
  ///
  /// Each span must contain at least frame_layout().register_count elements
  /// and remain alive for every resume() using the frame.
  void initialize_frame(JitProcessHandle process, fsim_jit_frame_v1 &frame,
                        std::span<std::uint64_t> register_aval,
                        std::span<std::uint64_t> register_bval,
                        std::span<std::uint8_t> register_initialized,
                        std::span<std::uint64_t> register_logic9_plane2 = {},
                        std::span<std::uint64_t> register_logic9_plane3 = {})
      const;
  void initialize_frame(JitProcessBinding process, fsim_jit_frame_v1 &frame,
                        std::span<std::uint64_t> register_aval,
                        std::span<std::uint64_t> register_bval,
                        std::span<std::uint8_t> register_initialized,
                        std::span<std::uint64_t> register_logic9_plane2 = {},
                        std::span<std::uint64_t> register_logic9_plane3 = {})
      const;

  /// Run from the frame PC until completion, failure, Stop, or suspension.
  ///
  /// result must advertise the v1 result ABI and structure size. WaitFor
  /// reports its delay without scheduling it; WaitOn and WaitSensitivity
  /// report the immutable SimIR instruction containing their operands; Yield
  /// reports a next-delta suspension. The caller decides when to invoke
  /// resume() again.
  [[nodiscard]] JitResumeStatus
  resume(JitProcessHandle process, const fsim_jit_runtime_v1 &runtime,
         fsim_jit_frame_v1 &frame,
         fsim_jit_resume_result_v1 &result) const;
  [[nodiscard]] JitResumeStatus
  resume(JitProcessBinding process, const fsim_jit_runtime_v1 &runtime,
         fsim_jit_frame_v1 &frame,
         fsim_jit_resume_result_v1 &result) const;

  /// Resume a stable binding whose runtime and frame were already validated by
  /// the owning scheduler. This skips the public ABI/capability checks on the
  /// activation hot path; callers must keep the JIT, binding, runtime callback
  /// table, and initialized frame alive and mutually consistent.
  [[nodiscard]] JitResumeStatus resume_prevalidated(
      JitProcessBinding process, const fsim_jit_runtime_v1 &runtime,
      fsim_jit_frame_v1 &frame, fsim_jit_resume_result_v1 &result) const;

  /// Resume an exact ordered cohort through one generated native wrapper.
  /// The wrapper stops after the first status other than WaitSensitivity so
  /// the scheduler can preserve canonical boundary handling.
  [[nodiscard]] std::size_t resume_cohort_prevalidated(
      std::span<JitProcessCohortResumeEntry> entries) const;

  /// Bind the exact cohort supplied to resume_cohort_prevalidated(). The
  /// caller must keep every referenced runtime, frame, result, and scheduler
  /// state object at the same address for the lifetime of the binding.
  [[nodiscard]] JitProcessCohortBinding bind_cohort_prevalidated(
      std::span<JitProcessCohortResumeEntry> entries) const;

  /// Bind an exact cohort for the caller-certified strict-Logic4 bit-and
  /// shape (DebugPoint 0, DebugPoint 1, Read lhs 2, Extract lhs 3, Read rhs
  /// 4, Extract rhs 5, BitAnd 6, WriteUpdateSlice 7, WaitSensitivity 8, and
  /// Jump 9 to 0).
  /// The caller is responsible for proving each descriptor matches its
  /// immutable SimIR body and for applying simulation-context, revision, hook,
  /// and pending-update guards. Member descriptors contain register layout
  /// and runtime-table indices, including each register's frame residency,
  /// but no instance signal identities. The JIT validates descriptor/layout
  /// compatibility and runtime, frame, and result ABI/table shape before
  /// creating the binding.
  /// Instrumented-debug and region cohorts are unsupported and return
  /// nullopt.
  /// The caller keeps this binding beside the generic cohort binding and
  /// releases it with release_cohort_binding().
  [[nodiscard]] std::optional<JitProcessCohortBinding>
  bind_logic4_bit_and_cohort_prevalidated(
      std::span<JitProcessCohortResumeEntry> entries,
      std::span<const JitProcessCohortLogic4BitAndMember> members) const;

  /// Bind a compact Logic4 bit-and cohort using the same caller-certified
  /// member shape. Binding succeeds only when all five register slots for
  /// every member are nonresident, each runtime resolves both reads to the
  /// same actual input pair on shared signal planes, and each deferred output
  /// slot and active-bitmap word is valid. The caller must provide the exact
  /// warm cohort order and keep its runtime owners, tables, output slots, and
  /// scheduler-state addresses stable for the binding lifetime. A failed
  /// shape or runtime preflight returns nullopt without mutation.
  ///
  /// The compact path updates only deferred output slots, active bitmap bits,
  /// and terminal queued/waiting/process-status bytes. It does not write
  /// frames, registers, resume results, or per-entry status/failure fields.
  /// The caller remains responsible for proving the exact immutable SimIR
  /// body, warm process state, absence of pending updates/hooks, and private
  /// disjoint storage needed by the specialized operation. This includes
  /// proving that runtime debug-point callbacks are disabled for the binding.
  [[nodiscard]] std::optional<JitProcessCohortBinding>
  bind_compact_logic4_bit_and_cohort_prevalidated(
      std::span<JitProcessCohortResumeEntry> entries,
      std::span<const JitProcessCohortLogic4BitAndMember> shapes) const;

  /// Validate and retain one process's immutable pure-wave shape and resolved
  /// runtime slots. The actual Process body and mapped update IDs are checked
  /// once here; the returned handle can be reused in many ordered waves.
  [[nodiscard]] std::optional<JitPureWaveMemberBinding>
  bind_pure_wave_member_prevalidated(const JitPureWaveMember& member) const;

  /// Retain a prepared member once for direct trusted wave dispatch. Releasing
  /// the binding marks this view stale, while the lease keeps its immutable
  /// descriptor storage alive until the owning executor drops the lease.
  [[nodiscard]] std::optional<JitPureWaveMemberLease>
  acquire_pure_wave_member_lease(JitPureWaveMemberBinding member) const;

  /// Release a per-process pure-wave member handle after its executor and
  /// runtime storage are no longer offered to wave bindings.
  [[nodiscard]] bool release_pure_wave_member_binding(
      JitPureWaveMemberBinding member) const;

  /// Bind one ordered wave from previously classified members. task_ends
  /// contains cumulative member counts for complete original scheduler tasks
  /// and must end at the member count. The wave reuses four fixed shape kernels
  /// and stores the runtime addresses/slots used by the caller's task order.
  /// The caller must keep all member handles, runtimes, scheduler state, and
  /// deferred-update storage alive and unchanged until release. Unsupported
  /// task shapes return nullopt without mutation. Application context,
  /// hook-lifetime, and task-order guards remain the caller's duty.
  [[nodiscard]] std::optional<JitProcessCohortBinding>
  bind_pure_wave_prevalidated(
      std::span<const JitPureWaveMemberBinding> members,
      std::span<const std::size_t> task_ends) const;

  /// Execute every bound task and write its deferred updates/terminal process
  /// bytes exactly once. The caller retains normal scheduler and update commit
  /// ownership. False means preflight declined before any member or slot
  /// changed; after execution starts, the whole bound wave completes. The
  /// compiler updates only deferred slots, their activity bitmap, and terminal
  /// queued/waiting/process-status bytes. It leaves frames, registers, native
  /// resume results, and per-entry status/failure fields untouched; the caller
  /// supplies the certified static-wait completion result.
  [[nodiscard]] bool try_resume_pure_wave_prevalidated(
      JitProcessCohortBinding wave) const;

  /// Execute an ordered wave directly from prepared per-executor member
  /// views. The caller owns bounded reusable pointer/task scratch and certifies
  /// the current owner, storage, writer epoch, warm state, unique queued
  /// processes, and exact original task boundaries, including shared input
  /// pairs for complete AND tasks. Every lease and its external runtime/frame
  /// storage must remain alive and unchanged, with no concurrent release, for
  /// the duration of this trusted call. A decline returns nullopt before any
  /// member, update slot, or scheduler byte changes; success returns the
  /// number of complete tasks dispatched. This path checks only the lease
  /// tokens and bounded task/shape structure, relying on the caller's
  /// certificate instead of repeating full runtime, frame-layout, and signal
  /// map validation. The checked wave API remains available independently.
  [[nodiscard]] std::optional<std::size_t>
  try_resume_pure_wave_members_prevalidated(
      std::span<const JitPureWaveMemberLease* const> members,
      std::span<const std::size_t> task_ends) const;

  /// Execute an ordered wave directly from runtime prepared-member records.
  /// Each record's compiler_view must be pinned by its still-live member lease,
  /// and compiler_generation must match that lease's binding generation. The
  /// caller also certifies the owner epoch, storage, warm state, unique queued
  /// processes, and exact original task boundaries. A decline occurs before
  /// any member, update slot, or scheduler byte changes; success returns the
  /// number of complete tasks dispatched. Runtime records and their external
  /// frame/update storage must remain alive and unchanged through this call,
  /// with no concurrent release. The checked wave API remains available for
  /// callers that cannot provide this certificate.
  [[nodiscard]] std::optional<std::size_t>
  try_resume_pure_wave_prepared_members_prevalidated(
      std::span<const runtime::simir::PureWavePreparedMember* const> members,
      std::span<const std::size_t> task_ends) const;

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

  /// Attempt one fully checked Logic4 bit-and cohort activation. The JIT
  /// revalidates every member's runtime tables, frame/result ABI, and buffered
  /// update slots before changing any frame or scheduler state. Unsupported
  /// runtime state returns nullopt without mutation so the caller can use the
  /// generic bound cohort path. Writes remain in the existing deferred update
  /// slots for the host to commit at the normal suspension boundary. Every
  /// frame must be ready at the certified WaitSensitivity resume point.
  [[nodiscard]] std::optional<std::size_t>
  try_resume_logic4_bit_and_cohort_prevalidated(
      JitProcessCohortBinding cohort,
      std::span<JitProcessCohortResumeEntry> entries) const;

  /// Attempt one Logic4 bit-and activation using a caller-established
  /// same-owner runtime certificate. The caller must pass the exact ordered
  /// span with the same process/runtime/frame/result/scheduler-state addresses
  /// used at binding and with each entry's active pointer still null. Preserve
  /// each runtime's ABI, direct-read map/planes,
  /// update slot layout, active-word mapping, frame/result ABI layout, and
  /// backing register planes. This method validates the binding owner and
  /// generation, but trusts those per-entry identities and immutable layouts.
  /// It still checks the mutable runtime debug flag and frame
  /// PC/state/last-instruction/native-depth for every member before any member
  /// is changed. A failed check returns nullopt without mutation. Deferred
  /// update slots remain pending for the normal host commit.
  [[nodiscard]] std::optional<std::size_t>
  try_resume_logic4_bit_and_cohort_trusted_prevalidated(
      JitProcessCohortBinding cohort,
      std::span<JitProcessCohortResumeEntry> entries) const;

  /// Execute a compact binding created by
  /// bind_compact_logic4_bit_and_cohort_prevalidated(). False means the
  /// binding preflight was rejected before any member was changed; once
  /// execution starts this method returns true. It relies on the caller's
  /// same-owner, warm-state, no-debug-hook, and pointer-lifetime certificate.
  [[nodiscard]] bool try_resume_compact_logic4_bit_and_cohort_prevalidated(
      JitProcessCohortBinding cohort) const;

  /// Resume the active members of a stable ordered region through one cached
  /// native wrapper. Every entry must provide an active byte. Inactive members
  /// are untouched; active members are consumed in canonical order.
  [[nodiscard]] std::size_t resume_region_prevalidated(
      std::span<JitProcessCohortResumeEntry> entries,
      std::span<const std::size_t> active_indices) const;

  /// Execute a previously looked-up process.
  ///
  /// This compatibility helper creates a temporary frame. It rejects processes
  /// containing WaitFor or Yield before executing them; use resume() for those.
  /// Callback exceptions must not cross the generated plain-C ABI.
  [[nodiscard]] JitExecutionStatus
  execute(JitProcessHandle process,
          const fsim_jit_runtime_v1 &runtime) const;

  /// Snapshot persistent-cache activity for this JIT instance.
  ///
  /// Lookup/materialization, rather than add_process(), performs native cache
  /// reads and writes. A rejected entry is recompiled and replaced.
  [[nodiscard]] LlvmJitCacheStatistics cache_statistics() const noexcept;

  /// LLVM version used to compile this adapter (for diagnostics/cache keys).
  [[nodiscard]] static std::string_view llvm_version() noexcept;

  /// Complete source-independent native-code admission identity. This uses
  /// the same host detection, ABI constants, LLVM version, target, data
  /// layout, CPU, and sorted feature set as persistent object-cache keys.
  [[nodiscard]] static LlvmNativeHostIdentity native_host_identity(
      JitOptimizationLevel optimization);

private:
  struct Impl;
  [[nodiscard]] std::optional<std::size_t>
  try_resume_logic4_bit_and_cohort_prevalidated_impl(
      JitProcessCohortBinding cohort,
      std::span<JitProcessCohortResumeEntry> entries,
      bool validate_immutable_state) const;

  std::unique_ptr<Impl> impl_;
};

} // namespace fsim::compiler
