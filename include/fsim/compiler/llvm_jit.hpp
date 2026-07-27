// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/compiler/jit_runtime.h"
#include "fsim/runtime/simir.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
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
};

struct LlvmJitCacheStatistics {
  std::uint64_t hits{};
  std::uint64_t misses{};
  std::uint64_t stores{};
  std::uint64_t rejected_entries{};
  std::uint64_t load_failures{};
  std::uint64_t store_failures{};

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
};

struct JitProcessFrameLayout {
  std::uint64_t layout_id_low{};
  std::uint64_t layout_id_high{};
  std::uint32_t register_count{};

  friend bool operator==(JitProcessFrameLayout,
                         JitProcessFrameLayout) = default;
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
/// Supported processes may use LoadConstant, ReadSignal, UnaryNot, Binary,
/// WriteBlocking, WriteUpdate, WriteAfter, Assert, Jump, Branch, WaitFor,
/// WaitOn, WaitSensitivity, Yield, Stop, and Halt. Control flow is lowered to
/// LLVM basic blocks backed by a versioned caller-owned frame. Values crossing
/// the native ABI must be between 1 and 64 bits; sensitivity-only signals may
/// be wider. Control-flow cycles without a suspension safe point are rejected
/// during add_process().
///
/// Persistent caching is opt-in through LlvmJitOptions::cache_directory.
/// Cached native objects are checksummed and keyed to the complete supported
/// SimIR module plus the native compilation environment. The adapter does not
/// currently evict entries by age or total cache size.
class LlvmJit final {
public:
  explicit LlvmJit(LlvmJitOptions options = {});
  ~LlvmJit();
  LlvmJit(LlvmJit &&) noexcept;
  LlvmJit &operator=(LlvmJit &&) noexcept;
  LlvmJit(const LlvmJit &) = delete;
  LlvmJit &operator=(const LlvmJit &) = delete;

  /// Validate, lower, optimize, and add a process under an ORC symbol name.
  ///
  /// signal_widths is indexed by SimIR SignalId. All referenced widths must be
  /// in [1, 64]. Adding a duplicate symbol is an error.
  void add_process(std::string_view symbol,
                   const runtime::simir::Process &process,
                   std::span<const std::uint32_t> signal_widths);

  /// Compile/materialize a symbol through ORC and return an opaque handle.
  [[nodiscard]] JitProcessHandle lookup(std::string_view symbol);

  /// Return the caller-owned frame layout required by a compiled process.
  [[nodiscard]] JitProcessFrameLayout
  frame_layout(JitProcessHandle process) const;

  /// Initialize a caller-owned v1 frame and zero its register storage.
  ///
  /// Each span must contain at least frame_layout().register_count elements and
  /// remain alive for every resume() using the frame.
  void initialize_frame(JitProcessHandle process, fsim_jit_frame_v1 &frame,
                        std::span<std::uint64_t> register_aval,
                        std::span<std::uint64_t> register_bval) const;

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
