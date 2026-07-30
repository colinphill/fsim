// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/compiler/llvm_jit.hpp"

#include <llvm/IR/DataLayout.h>
#include <llvm/TargetParser/Triple.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace llvm {
class Module;
}

namespace fsim::compiler::llvm_detail {

struct ValidatedProcess {
  std::vector<std::uint32_t> register_widths;
  bool uses_logic9{};
  bool requires_resume{};
  bool uses_write_update{};
  bool uses_write_after{};
  bool uses_write_inertial{};
  bool uses_write_projected{};
  bool uses_write_projected_waveform{};
  bool uses_write_blocking_slice{};
  bool uses_write_update_slice{};
  bool uses_write_after_slice{};
  bool uses_write_inertial_slice{};
  bool uses_write_projected_slice{};
  bool uses_write_projected_waveform_slice{};
  bool uses_debug_points{};
  bool uses_signal_event{};
  bool uses_signal_last_value{};
  bool uses_signal_last_event{};
  bool uses_signal_active{};
  bool uses_output{};
  bool uses_postponed_output{};
  bool uses_report{};
  bool uses_formatted_output{};
  bool uses_time_output{};
  bool uses_monitor_install{};
  bool uses_monitor_control{};
  bool uses_random_value{};
  bool uses_strings{};
  bool uses_files{};
  bool uses_containers{};
};

[[nodiscard]] bool valid_symbol(std::string_view symbol) noexcept;
[[nodiscard]] std::string generated_runtime_error_message(
    std::uint32_t instruction,
    JitGeneratedRuntimeErrorReason reason);
[[nodiscard]] std::optional<JitGeneratedRuntimeErrorReason>
decode_generated_runtime_error(std::uint64_t value) noexcept;

[[nodiscard]] ValidatedProcess validate_process(
    const runtime::simir::Process& process,
    std::span<const std::uint32_t> signal_widths,
    std::span<const runtime::simir::ValueKind> signal_value_kinds);

[[nodiscard]] std::string make_native_object_cache_key(
    std::string_view symbol,
    const runtime::simir::Process& process,
    std::span<const std::uint32_t> signal_widths,
    std::span<const runtime::simir::ValueKind> signal_value_kinds,
    JitOptimizationLevel optimization,
    const llvm::Triple& target_triple,
    const llvm::DataLayout& data_layout,
    std::string_view target_cpu,
    std::span<const std::string> target_features);

[[nodiscard]] std::string make_native_module_cache_key(
    std::string_view module_identity,
    std::span<const std::string> process_keys);

[[nodiscard]] JitProcessFrameLayout make_frame_layout(
    std::string_view cache_key,
    std::size_t register_count,
    std::size_t string_register_count,
    bool uses_logic9);

void lower_process(
    llvm::Module& module,
    const std::string& symbol,
    const runtime::simir::Process& process,
    std::span<const std::uint32_t> signal_widths,
    std::span<const runtime::simir::ValueKind> signal_value_kinds,
    const ValidatedProcess& validated,
    bool debug_instrumentation);

void optimize_module(
    llvm::Module& module,
    JitOptimizationLevel optimization);

[[nodiscard]] std::string verify_error(llvm::Module& module);

}  // namespace fsim::compiler::llvm_detail
