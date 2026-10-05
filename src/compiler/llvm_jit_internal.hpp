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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace llvm {
class Module;
}

namespace fsim::compiler::llvm_detail {

inline constexpr auto kJitNoUnwindCallbackMetadata
    = "fsim.jit.abi.nounwind.callback";
inline constexpr auto kTieredDirectReadLoadMetadata
    = "fsim.jit.tiered-direct-read-load";
inline constexpr auto kTieredSafeFrameStoreMetadata
    = "fsim.jit.tiered-safe-frame-store";

enum class LlvmBackendTier : std::uint32_t {
    none = 0U,
    less = 1U,
};

enum class ProcessLoweringMode : std::uint8_t {
    four_state,
    region_known_logic4,
};

inline constexpr std::uint64_t kLessBackendTierInstructionLimit = 16'384U;
inline constexpr std::string_view kBackendTierPolicy
    = "none-less-ir16384-marked-direct-read-cse-v1";

[[nodiscard]] constexpr bool backend_tier_eligible(
    const JitProcessModuleEntry& entry) noexcept
{
    return jit_backend_tier_hint_eligible(
        entry.backend_tier_hint, entry.bound_instance_count);
}

[[nodiscard]] inline bool module_backend_tier_eligible(
    const std::span<const JitProcessModuleEntry> entries) noexcept
{
    if (entries.empty()) {
        return false;
    }
    for (const auto& entry : entries) {
        if (!backend_tier_eligible(entry)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool tiered_read_dedup_eligible(
    const JitProcessModuleEntry& entry,
    const bool global_require_direct_read_signals) noexcept
{
    const bool fused_hint
        = entry.backend_tier_hint == JitBackendTierHint::fused_static_cohort
        || entry.backend_tier_hint == JitBackendTierHint::fused_masked_region;
    return entry.tiered_read_dedup_safe
        && (global_require_direct_read_signals
            || entry.require_direct_read_signals)
        && fused_hint && backend_tier_eligible(entry);
}

[[nodiscard]] inline bool module_tiered_read_dedup_eligible(
    const std::span<const JitProcessModuleEntry> entries,
    const bool global_require_direct_read_signals) noexcept
{
    return !entries.empty()
        && std::ranges::all_of(entries, [&](const auto& entry) {
               return tiered_read_dedup_eligible(
                   entry, global_require_direct_read_signals);
           });
}

[[nodiscard]] constexpr LlvmBackendTier select_backend_tier(
    const bool eligible,
    const std::uint64_t optimized_instruction_count) noexcept
{
    return eligible
            && optimized_instruction_count
                <= kLessBackendTierInstructionLimit
        ? LlvmBackendTier::less
        : LlvmBackendTier::none;
}

[[nodiscard]] constexpr bool valid_backend_tier_proof(
    const LlvmBackendTier tier,
    const bool eligible,
    const std::uint64_t optimized_instruction_count) noexcept
{
    return tier == select_backend_tier(
        eligible, optimized_instruction_count);
}

inline constexpr auto kJitRuntimeV2Size
    = static_cast<std::uint32_t>(sizeof(fsim_jit_runtime_instance_v2));
inline constexpr auto kJitServicesV2Size
    = static_cast<std::uint32_t>(sizeof(fsim_jit_services_v2));
inline constexpr auto kJitFrameV2Size
    = static_cast<std::uint32_t>(sizeof(fsim_jit_frame_v2));

class LlvmObjectCache : public llvm::ObjectCache {
public:
    ~LlvmObjectCache() override = default;

    [[nodiscard]] virtual std::unique_ptr<llvm::MemoryBuffer> preflight(
        std::string_view key,
        std::vector<std::byte>* metadata = nullptr) = 0;
    virtual void accept_preflight_hit(std::string_view key) = 0;
    virtual void discard_preflight(std::string_view key) = 0;
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
    std::size_t returns { };
    std::size_t max_block_instructions { };
    std::size_t max_integer_width { };
    std::map<std::string, std::size_t> wide_integer_opcodes;
    std::map<std::string, std::size_t> wide_vector_opcodes;
};

struct TieredReadDedupStatistics {
    std::uint64_t marked_loads { };
    std::uint64_t eliminated_loads { };
    std::uint64_t marked_value_loads { };
    std::uint64_t eliminated_value_loads { };
};

[[nodiscard]] TieredReadDedupStatistics run_tiered_direct_read_dedup(
    llvm::Module& module,
    LlvmBackendTier tier,
    bool safe_entry);

[[nodiscard]] std::vector<runtime::simir::SignalId> direct_update_signals(
    const runtime::simir::Process& process,
    std::span<const std::uint32_t> signal_widths,
    std::span<const runtime::simir::ValueKind> signal_value_kinds);
[[nodiscard]] std::vector<runtime::simir::SignalId> direct_read_signals(
    const runtime::simir::Process& process,
    std::span<const std::uint32_t> signal_widths,
    std::span<const runtime::simir::ValueKind> signal_value_kinds);
[[nodiscard]] std::vector<runtime::simir::SignalId>
signal_callback_operands(
    const runtime::simir::Process& process,
    std::span<const std::uint32_t> signal_widths);
[[nodiscard]] IrShape ir_shape(const llvm::Module& module);
[[nodiscard]] std::uint64_t ir_instruction_count(
    const llvm::Module& module) noexcept;
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
    /// Count operation and debugger-visible references in the separate
    /// container-register namespace.
    std::vector<std::size_t> container_register_reference_counts;
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
    bool uses_container_read_index64 { };
    bool uses_exact_signal_operation { };
    bool uses_wide_signal_read { };
    bool uses_wide_signal_write { };
    bool uses_wide_projected_write { };
    bool uses_code_coverage { };
    bool uses_coverage_sample { };
    bool uses_class_property_operation { };
    bool uses_event_triggered { };
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
    const runtime::simir::Operation& operation);
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
[[nodiscard]] bool is_resume_boundary(
    const runtime::simir::Operation& operation,
    std::span<const std::uint32_t> register_widths) noexcept;

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
    bool require_direct_read_signals,
    bool tiered_read_dedup_safe,
    bool signal_callback_ids_are_actual,
    ProcessLoweringMode mode,
    std::string_view code_coverage_identity,
    const llvm::Triple& target_triple,
    const llvm::DataLayout& data_layout,
    std::string_view target_cpu,
    std::span<const std::string> target_features,
    std::span<const runtime::simir::InstructionIndex> bound_literal_sites = { });

[[nodiscard]] std::string make_immutable_design_object_cache_key(
    std::string_view design_identity,
    std::string_view module_identity,
    std::string_view symbol,
    JitOptimizationLevel optimization,
    bool debug_instrumentation,
    bool require_direct_update_slots,
    bool require_direct_read_signals,
    bool tiered_read_dedup_safe,
    bool signal_callback_ids_are_actual,
    ProcessLoweringMode mode,
    std::string_view code_coverage_identity,
    const llvm::Triple& target_triple,
    const llvm::DataLayout& data_layout,
    std::string_view target_cpu,
    std::span<const std::string> target_features,
    std::span<const runtime::simir::InstructionIndex> bound_literal_sites = { });

[[nodiscard]] std::string make_native_module_cache_key(
    std::string_view module_identity,
    std::span<const std::string> process_keys,
    LlvmBackendTier backend_tier,
    bool tier_eligible);

[[nodiscard]] JitProcessFrameLayout make_frame_layout(
    std::string_view cache_key,
    std::span<const std::uint32_t> register_widths,
    std::size_t string_register_count,
    bool uses_logic9,
    bool tracks_register_initialization,
    std::span<const runtime::simir::SignalId> direct_read_signals,
    std::span<const runtime::simir::SignalId> direct_update_signals,
    bool signal_callback_ids_are_actual,
    std::span<const runtime::simir::SignalId> signal_callback_operands);

struct ProcessLoweringPlan {
    std::vector<bool> operations;
    std::vector<runtime::simir::InstructionIndex> entry_points;
    bool partial { };
};

struct RegionPreparedOutputLoweringBinding {
    std::uint32_t direct_update_slot { };
    std::uint32_t descriptor_slot { };
    std::uint32_t width { };
    std::uint64_t successor_member_mask { };
};

enum class RegionDirectReadyBindingKind : std::uint32_t {
    input_slot = 0U,
    member_ready = 1U,
    prefix_current = 2U,
};

/// Compiler-owned mapping from the private direct-ready-window descriptor
/// into reusable direct-signal storage. `source_index` names an input slot or
/// member mask bit according to `kind`; prefix-current values are appended to
/// the same input-slot table after ordinary signal inputs.
struct RegionDirectReadyLoweringBinding {
    RegionDirectReadyBindingKind kind { };
    std::uint32_t source_index { };
    std::uint32_t signal_id { };
    std::uint32_t register_id { };
    std::uint32_t synthetic_signal_id { };
    std::uint32_t width { };
};

[[nodiscard]] ProcessLoweringPlan make_process_lowering_plan(
    const runtime::simir::Process& process,
    std::span<const std::uint32_t> register_widths,
    bool debug_instrumentation);

void lower_process(
    llvm::Module& module,
    const std::string& symbol,
    const runtime::simir::Process& process,
    std::span<const std::uint32_t> signal_widths,
    std::span<const runtime::simir::ValueKind> signal_value_kinds,
    std::span<const runtime::simir::SignalId> direct_read_signals,
    std::span<const runtime::simir::SignalId> direct_update_signals,
    bool signal_callback_ids_are_actual,
    std::span<const runtime::simir::SignalId> signal_callback_operands,
    std::uint32_t signal_callback_operand_word_base,
    const ValidatedProcess& validated,
    const ProcessLoweringPlan& lowering_plan,
    JitOptimizationLevel optimization,
    bool debug_instrumentation,
    bool require_direct_update_slots,
    bool require_direct_read_signals,
    ProcessLoweringMode mode,
    std::span<const runtime::simir::InstructionIndex> bound_literal_sites,
    std::span<const FusedMaskedMemberGate> masked_member_gates,
    std::vector<std::uint8_t>& register_values_persistent,
    std::string_view prepared_output_entry_symbol = { },
    std::span<const RegionPreparedOutputLoweringBinding>
        prepared_output_bindings = { },
    std::string_view prepared_output_successor_entry_symbol = { },
    std::string_view direct_ready_entry_symbol = { },
    std::string_view direct_ready_successor_entry_symbol = { },
    std::span<const RegionDirectReadyLoweringBinding>
        direct_ready_bindings = { });

void optimize_module(
    llvm::Module& module,
    JitOptimizationLevel optimization,
    std::string_view profile_identity = { },
    std::size_t process_count = 0);

[[nodiscard]] std::string verify_error(llvm::Module& module);
void apply_jit_module_no_unwind_contract(llvm::Module& module);
void require_jit_module_no_unwind_contract(const llvm::Module& module);
[[nodiscard]] std::string make_native_host_identity_fingerprint(
    const LlvmNativeHostIdentity& identity,
    JitOptimizationLevel optimization,
    std::string_view native_object_schema);

} // namespace fsim::compiler::llvm_detail
