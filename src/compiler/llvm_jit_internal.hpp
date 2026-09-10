// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/compiler/llvm_jit.hpp"

#include <llvm/ExecutionEngine/ObjectCache.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Triple.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace llvm {
class Module;
}

namespace fsim::compiler::llvm_detail {

inline constexpr auto kJitRuntimeV1PrefixSize
    = static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, write_update));
inline constexpr auto kJitFrameV1PrefixSize
    = static_cast<std::uint32_t>(
        offsetof(fsim_jit_frame_v1, register_logic9_plane2));
inline constexpr auto kJitRuntimeLogic9Size
    = static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, load_string));
inline constexpr auto kJitRuntimeStringSize
    = static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, file_open));
inline constexpr auto kJitRuntimeFileSize
    = static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, container_operation));
inline constexpr auto kJitRuntimeForceSize
    = static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, read_simulation_time));
inline constexpr auto kJitRuntimeDriverForceSize
    = static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, execute_signal_operation));
inline constexpr auto kJitRuntimeContainerWordSize
    = static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, container_read_packed));
inline constexpr auto kJitRuntimeExactSignalSize
    = static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, read_signal_packed));
inline constexpr auto kJitRuntimeWideSignalReadSize
    = static_cast<std::uint32_t>(
        offsetof(fsim_jit_runtime_v1, write_signal_packed));

class LlvmObjectCache : public llvm::ObjectCache {
public:
    ~LlvmObjectCache() override = default;

    [[nodiscard]] virtual std::unique_ptr<llvm::MemoryBuffer> preflight(
        std::string_view key,
        std::vector<std::byte>* metadata = nullptr) = 0;
    virtual void stage_metadata(
        std::string key,
        std::vector<std::byte> metadata) = 0;
    virtual void discard_staged_metadata(std::string_view key) = 0;
    [[nodiscard]] virtual LlvmJitCacheStatistics statistics() const noexcept = 0;
};

struct IrShape {
    std::size_t functions { };
    std::size_t blocks { };
    std::size_t instructions { };
    std::size_t allocas { };
    std::size_t loads { };
    std::size_t stores { };
    std::size_t calls { };
    std::size_t branches { };
    std::size_t switches { };
    std::size_t phis { };
};

[[nodiscard]] std::vector<runtime::simir::SignalId> direct_update_signals(
    const runtime::simir::Process& process,
    std::span<const std::uint32_t> signal_widths,
    std::span<const runtime::simir::ValueKind> signal_value_kinds);
[[nodiscard]] std::vector<runtime::simir::SignalId> direct_read_signals(
    const runtime::simir::Process& process,
    std::span<const std::uint32_t> signal_widths,
    std::span<const runtime::simir::ValueKind> signal_value_kinds);
[[nodiscard]] IrShape ir_shape(const llvm::Module& module);
void dump_ir(const llvm::Module& module, const char* path);
void print_ir_shape(
    llvm::raw_ostream& stream,
    const char* phase,
    const IrShape& shape);
[[nodiscard]] std::string llvm_error(llvm::Error error);

template <typename T>
[[nodiscard]] T unwrap(
    llvm::Expected<T> expected,
    const std::string_view action)
{
    if (!expected) {
        throw LlvmJitError(
            std::string { action } + ": "
            + llvm_error(expected.takeError()));
    }
    return std::move(*expected);
}

struct ValidatedProcess {
    std::vector<std::uint32_t> register_widths;
    /// Exact packed-register dataflow retained for lowering-time fusion.
    std::vector<std::vector<runtime::simir::RegisterId>> instruction_uses;
    std::vector<std::vector<runtime::simir::RegisterId>>
        instruction_definitions;
    bool uses_logic9 { };
    bool requires_resume { };
    bool uses_write_update { };
    bool uses_write_after { };
    bool uses_write_inertial { };
    bool uses_write_projected { };
    bool uses_write_projected_waveform { };
    bool uses_write_blocking_slice { };
    bool uses_write_update_slice { };
    bool uses_write_after_slice { };
    bool uses_write_inertial_slice { };
    bool uses_write_projected_slice { };
    bool uses_write_projected_waveform_slice { };
    bool uses_force_signal_slice { };
    bool uses_release_signal_slice { };
    bool uses_force_driver_signal_slice { };
    bool uses_release_driver_signal_slice { };
    bool uses_debug_points { };
    bool uses_signal_event { };
    bool uses_signal_last_value { };
    bool uses_signal_last_event { };
    bool uses_simulation_time { };
    bool uses_vital_timing { };
    bool uses_vital_delay { };
    bool uses_signal_active { };
    bool uses_signal_last_active { };
    bool uses_signal_driving { };
    bool uses_signal_driving_value { };
    bool uses_output { };
    bool uses_postponed_output { };
    bool uses_report { };
    bool uses_formatted_output { };
    bool uses_time_output { };
    bool uses_monitor_install { };
    bool uses_monitor_control { };
    bool uses_random_value { };
    bool uses_strings { };
    bool uses_files { };
    bool uses_containers { };
    bool uses_wide_container_operation { };
    bool uses_exact_signal_operation { };
    bool uses_wide_signal_read { };
    bool uses_wide_signal_write { };
    bool uses_code_coverage { };
};

[[nodiscard]] bool valid_symbol(std::string_view symbol) noexcept;
[[nodiscard]] std::string generated_runtime_error_message(
    std::uint32_t instruction,
    JitGeneratedRuntimeErrorReason reason);
[[nodiscard]] std::optional<JitGeneratedRuntimeErrorReason>
decode_generated_runtime_error(std::uint64_t value) noexcept;

[[nodiscard]] std::optional<std::string>
validate_container_locator_metadata(
    const runtime::simir::LocateContainer& operation,
    const runtime::simir::ContainerType& destination,
    const runtime::simir::ContainerType& source);
[[nodiscard]] std::optional<std::string>
validate_container_locator_transformation_metadata(
    const runtime::simir::LocateContainer& operation,
    const runtime::simir::ContainerType& source);
[[nodiscard]] std::optional<std::string>
validate_container_reduction_metadata(
    const runtime::simir::ContainerReduction& operation,
    const runtime::simir::ContainerType& source);
[[nodiscard]] std::optional<std::string>
validate_container_ordering_metadata(
    const runtime::simir::OrderContainer& operation,
    const runtime::simir::ContainerType& target);
[[nodiscard]] std::optional<std::string>
validate_dynamic_index_metadata(
    const runtime::simir::DynamicIndex& selection);
[[nodiscard]] std::optional<std::string>
validate_dynamic_index_bounds(
    const runtime::simir::DynamicIndex& selection,
    std::uint64_t target_width);
[[nodiscard]] std::optional<std::string>
validate_dynamic_part_select_metadata(
    const runtime::simir::DynamicPartSelect& operation);
[[nodiscard]] std::optional<std::string>
validate_dynamic_part_select_source_width(
    const runtime::simir::DynamicPartSelect& operation,
    std::uint32_t source_width);
[[nodiscard]] std::optional<std::string>
validate_dynamic_part_index_metadata(
    const runtime::simir::DynamicPartIndex& selection);
[[nodiscard]] std::optional<std::string>
validate_dynamic_part_index_bounds(
    const runtime::simir::DynamicPartIndex& selection,
    std::uint64_t target_width);
[[nodiscard]] std::optional<std::string>
validate_expression_profile_metadata(
    std::span<const runtime::simir::ExpressionProfile> profiles);
void validate_process_shape(
    const runtime::simir::Process& process,
    std::span<const std::uint32_t> signal_widths,
    std::span<const runtime::simir::ValueKind> signal_value_kinds);
[[nodiscard]] bool supports_wide_register_operation(
    const runtime::simir::Operation& operation,
    std::span<const std::uint32_t> register_widths);
[[nodiscard]] std::optional<std::string>
validate_extract_bounds(
    const runtime::simir::Extract& operation,
    std::uint32_t source_width);
[[nodiscard]] std::optional<std::string>
validate_insert_bounds(
    const runtime::simir::Insert& operation,
    std::uint32_t target_width,
    std::uint32_t source_width);

struct OperationValidationError {
    std::size_t instruction { };
    std::string message;
};

struct PackedRegisterValidation {
    runtime::simir::RegisterId id { };
    std::uint32_t width { };
    bool definition { };
};
[[nodiscard]] std::optional<std::string> validate_string_method_metadata(
    const runtime::simir::StringMethod& operation,
    const runtime::simir::Process& process,
    std::vector<PackedRegisterValidation>& registers);
[[nodiscard]] std::optional<std::string> validate_scalar_binary_metadata(
    const runtime::simir::SystemVerilogScalarBinary& operation,
    std::vector<PackedRegisterValidation>& registers);
[[nodiscard]] std::optional<std::string> validate_scalar_math_metadata(
    const runtime::simir::SystemVerilogMath& operation,
    std::vector<PackedRegisterValidation>& registers);
[[nodiscard]] std::optional<std::uint32_t> formatted_value_width(
    runtime::simir::OutputFormat format,
    runtime::SystemVerilogScalarKind scalar_kind) noexcept;
[[nodiscard]] std::optional<std::string> validate_file_write_metadata(
    const runtime::simir::FileWriteFormatted& operation,
    std::vector<PackedRegisterValidation>& registers);
[[nodiscard]] std::optional<std::string> validate_file_scan_metadata(
    const runtime::simir::FileScan& operation,
    const runtime::simir::Process& process,
    std::span<const std::uint32_t> signal_widths,
    std::span<const runtime::simir::ValueKind> signal_value_kinds,
    std::vector<PackedRegisterValidation>& registers);
[[nodiscard]] std::optional<std::string> validate_file_binary_metadata(
    const runtime::simir::FileBinaryRead& operation,
    const runtime::simir::Process& process,
    std::span<const std::uint32_t> signal_widths,
    std::vector<PackedRegisterValidation>& registers);
[[nodiscard]] std::optional<std::string> validate_file_position_metadata(
    const runtime::simir::FilePosition& operation,
    std::vector<PackedRegisterValidation>& registers);

[[nodiscard]] std::optional<OperationValidationError>
validate_selection_operation_bounds(
    const runtime::simir::Process& process,
    std::span<const std::uint32_t> register_widths,
    std::span<const std::uint32_t> signal_widths);

[[noreturn]] void reject(
    const runtime::simir::Process& process,
    std::size_t instruction,
    std::string_view message);
[[noreturn]] void reject_unsupported(
    const runtime::simir::Process& process,
    std::size_t instruction,
    std::string_view message);
void validate_fork_operation(
    const runtime::simir::Process& process,
    std::size_t instruction,
    const runtime::simir::Fork& operation);
[[nodiscard]] bool is_resume_boundary(
    const runtime::simir::Operation& operation) noexcept;

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
    bool debug_instrumentation,
    bool require_direct_update_slots,
    std::string_view code_coverage_identity,
    const llvm::Triple& target_triple,
    const llvm::DataLayout& data_layout,
    std::string_view target_cpu,
    std::span<const std::string> target_features);

[[nodiscard]] std::string make_immutable_design_object_cache_key(
    std::string_view design_identity,
    std::string_view module_identity,
    std::string_view symbol,
    JitOptimizationLevel optimization,
    bool debug_instrumentation,
    bool require_direct_update_slots,
    std::string_view code_coverage_identity,
    const llvm::Triple& target_triple,
    const llvm::DataLayout& data_layout,
    std::string_view target_cpu,
    std::span<const std::string> target_features);

[[nodiscard]] std::string make_native_module_cache_key(
    std::string_view module_identity,
    std::span<const std::string> process_keys);

[[nodiscard]] JitProcessFrameLayout make_frame_layout(
    std::string_view cache_key,
    std::span<const std::uint32_t> register_widths,
    std::size_t string_register_count,
    bool uses_logic9,
    bool tracks_register_initialization,
    std::span<const runtime::simir::SignalId> direct_read_signals,
    std::span<const runtime::simir::SignalId> direct_update_signals);

struct ProcessLoweringPlan {
    std::vector<bool> operations;
    std::vector<runtime::simir::InstructionIndex> entry_points;
    bool partial { };
};

[[nodiscard]] ProcessLoweringPlan make_process_lowering_plan(
    const runtime::simir::Process& process,
    bool debug_instrumentation);

void lower_process(
    llvm::Module& module,
    const std::string& symbol,
    const runtime::simir::Process& process,
    std::span<const std::uint32_t> signal_widths,
    std::span<const runtime::simir::ValueKind> signal_value_kinds,
    std::span<const runtime::simir::SignalId> direct_read_signals,
    std::span<const runtime::simir::SignalId> direct_update_signals,
    const ValidatedProcess& validated,
    JitOptimizationLevel optimization,
    bool debug_instrumentation,
    bool require_direct_update_slots);

void optimize_module(
    llvm::Module& module,
    JitOptimizationLevel optimization);

[[nodiscard]] std::string verify_error(llvm::Module& module);

} // namespace fsim::compiler::llvm_detail
