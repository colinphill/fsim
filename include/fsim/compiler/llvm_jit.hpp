// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/compiler/jit_runtime.h"
#include "fsim/runtime/simir.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::compiler {

enum class JitOptimizationLevel : std::uint8_t {
  o0,
  o2,
};

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

/// Opaque process token. It is meaningful only to the LlvmJit that created it.
struct JitProcessHandle {
  std::uint64_t value{};

  [[nodiscard]] explicit operator bool() const noexcept { return value != 0; }
  friend bool operator==(JitProcessHandle, JitProcessHandle) = default;
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
};

struct JitProcessFrameLayout {
  std::uint64_t layout_id_low{};
  std::uint64_t layout_id_high{};
  std::uint32_t register_count{};
  std::uint32_t string_register_count{};
  bool uses_logic9{};

  friend bool operator==(JitProcessFrameLayout,
                         JitProcessFrameLayout) = default;
};

/// One externally named process function within a compiled LLVM module.
///
/// The pointed-to SimIR process is consumed synchronously by
/// add_process_module() and need not outlive that call.
struct JitProcessModuleEntry {
  std::string_view symbol;
  const runtime::simir::Process* process{};
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
/// to LLVM basic blocks backed by a versioned caller-owned frame. O0 always
/// returns DebugPoint boundaries; O2 returns them only when the runtime enables
/// debug points. Values crossing the native ABI must be between 1 and 64 bits;
/// sensitivity-only signals may be wider. Control-flow cycles without a
/// suspension safe point are rejected during module addition.
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

  /// Compile/materialize a symbol through ORC and return an opaque handle.
  [[nodiscard]] JitProcessHandle lookup(std::string_view symbol);

  /// Return the caller-owned frame layout required by a compiled process.
  [[nodiscard]] JitProcessFrameLayout
  frame_layout(JitProcessHandle process) const;

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

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace fsim::compiler
