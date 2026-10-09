// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "llvm_jit_internal.hpp"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/DataLayout.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <functional>
#include <string_view>
#include <optional>
#include <vector>

namespace fsim::compiler::llvm_detail {

enum class JitServiceField : unsigned {
#define FSIM_JIT_SERVICE_FIELD(name) name,
    FSIM_JIT_SERVICES_V2_CALLBACK_FIELDS(FSIM_JIT_SERVICE_FIELD)
#undef FSIM_JIT_SERVICE_FIELD
    count
};

enum class JitRuntimeInstanceField : unsigned {
    abi_version,
    struct_size,
    services,
    context,
    flags,
    reserved,
    direct_update_slots,
    direct_update_slot_count,
    direct_update_reserved,
    direct_signal_aval,
    direct_signal_bval,
    direct_read_signals,
    direct_read_signal_count,
    direct_signal_count,
    direct_signal_reserved,
    direct_wide_signal_aval,
    direct_wide_signal_bval,
    direct_wide_signal_offsets,
    direct_wide_signal_offset_count,
    direct_wide_word_count,
    direct_update_active_words,
    direct_update_active_word_count,
    direct_update_active_reserved,
    static_trigger_mask,
    direct_wide_signal_logic9_plane2,
    direct_wide_signal_logic9_plane3,
    direct_signal_logic9_plane0,
    direct_signal_logic9_plane1,
    direct_signal_logic9_plane2,
    direct_signal_logic9_plane3,
    code_coverage_hit_counters,
    code_coverage_counter_values,
    code_coverage_hit_count,
    code_coverage_counter_count,
    fused_activation_words,
    fused_activation_word_count,
    fused_activation_reserved,
    count
};

struct JitAbiV2Types {
    llvm::ArrayType* service_callbacks;
    llvm::StructType* services;
    llvm::StructType* runtime_instance;
    llvm::StructType* update_slot;
    llvm::StructType* frame;
    llvm::StructType* resume_result;
    llvm::StructType* projected_element;
    llvm::StructType* logic9_word;
    llvm::StructType* logic9_projected_element;
};

[[nodiscard]] JitAbiV2Types create_jit_abi_v2_types(
    llvm::LLVMContext& context);
void validate_jit_abi_v2_layout(
    const llvm::DataLayout& data_layout,
    const JitAbiV2Types& types);
[[nodiscard]] llvm::Value* runtime_instance_field_address(
    llvm::IRBuilder<>& builder,
    llvm::StructType* instance_type,
    llvm::Value* instance,
    JitRuntimeInstanceField field);
[[nodiscard]] llvm::Value* jit_service_callback_address(
    llvm::IRBuilder<>& builder,
    llvm::StructType* services_type,
    llvm::Value* services,
    JitServiceField field);
[[nodiscard]] llvm::Value* load_jit_service_callback(
    llvm::IRBuilder<>& builder,
    llvm::StructType* services_type,
    llvm::Value* services,
    JitServiceField field,
    std::string_view name);

void lower_masked_member_gates(llvm::Function& function,
    llvm::StructType* runtime_type,
    std::span<llvm::BasicBlock* const> instruction_blocks,
    std::span<const FusedMaskedMemberGate> gates);

struct EncodedValue {
    llvm::Value* aval { };
    llvm::Value* bval { };
    std::uint32_t width { };
    llvm::Value* logic9_plane2 { };
    llvm::Value* logic9_plane3 { };
    runtime::simir::ValueKind kind {
        runtime::simir::ValueKind::logic4
    };
};

struct ConstantPlaneForwarding {
    std::size_t definition { static_cast<std::size_t>(-1) };
    bool active { };
    unsigned stores { };
    std::array<llvm::ConstantInt*, 4> pending { };
    std::array<llvm::ConstantInt*, 4> published { };
    std::size_t published_planes { };
    std::size_t forwarded_loads { };
};

struct RegisterSlot {
    llvm::Value* aval_base { };
    llvm::Value* bval_base { };
    llvm::Value* initialized_base { };
    llvm::Value* logic9_plane2_base { };
    llvm::Value* logic9_plane3_base { };
    std::uint64_t word_offset { };
    std::uint32_t index { };
    std::uint32_t width { };
    runtime::simir::ValueKind kind {
        runtime::simir::ValueKind::logic4
    };
    ConstantPlaneForwarding* constant_planes { };
    bool known_logic4 { };
};

struct EncodedBit {
    llvm::Value* aval { };
    llvm::Value* bval { };
};

struct EncodedDynamicPartWrite {
    EncodedValue value;
    llvm::Value* offset { };
    llvm::Value* width { };
};

[[nodiscard]] EncodedDynamicPartWrite lower_dynamic_part_write(
    llvm::IRBuilder<>& builder,
    llvm::LLVMContext& context,
    llvm::Type* i32,
    llvm::Type* i64,
    const std::vector<RegisterSlot>& registers,
    runtime::simir::RegisterId source,
    const runtime::simir::DynamicPartIndex& selection,
    runtime::simir::ValueKind signal_kind);

[[nodiscard]] bool dynamic_part_offsets_increase(
    std::int64_t left,
    std::int64_t right,
    bool source_descending) noexcept;

[[nodiscard]] std::optional<EncodedValue>
lower_dynamic_part_select_interval(
    llvm::IRBuilder<>& builder,
    llvm::LLVMContext& context,
    llvm::Type* i32,
    llvm::Type* i64,
    const std::vector<RegisterSlot>& registers,
    const runtime::simir::DynamicPartSelect& operation);

[[nodiscard]] std::optional<EncodedValue>
lower_dynamic_part_insert_interval(
    llvm::IRBuilder<>& builder,
    llvm::LLVMContext& context,
    llvm::Type* i32,
    llvm::Type* i64,
    const std::vector<RegisterSlot>& registers,
    const runtime::simir::DynamicPartInsert& operation);

[[nodiscard]] EncodedValue load_wide_register_bit(
    llvm::IRBuilder<>& builder,
    llvm::LLVMContext& context,
    const std::vector<RegisterSlot>& registers,
    runtime::simir::RegisterId id,
    llvm::Value* safe_offset);

void store_wide_dynamic_insert_words(
    llvm::IRBuilder<>& builder,
    llvm::LLVMContext& context,
    const std::vector<RegisterSlot>& registers,
    runtime::simir::RegisterId destination,
    runtime::simir::RegisterId target,
    EncodedValue source,
    llvm::Value* valid,
    llvm::Value* safe_offset);

[[nodiscard]] runtime::simir::ShiftOperator reverse_shift(
    runtime::simir::ShiftOperator operation) noexcept;

[[nodiscard]] EncodedValue load_register(
    llvm::IRBuilder<>& builder,
    const std::vector<RegisterSlot>& registers,
    runtime::simir::RegisterId id);

[[nodiscard]] bool try_lower_wide_bitwise_binary(
    llvm::IRBuilder<>& builder,
    const std::vector<RegisterSlot>& registers,
    runtime::simir::BinaryOperator operation,
    runtime::simir::RegisterId destination,
    runtime::simir::RegisterId lhs,
    runtime::simir::RegisterId rhs);

[[nodiscard]] bool try_lower_wide_bitwise_not(
    llvm::IRBuilder<>& builder,
    const std::vector<RegisterSlot>& registers,
    runtime::simir::RegisterId destination,
    runtime::simir::RegisterId source);

[[nodiscard]] EncodedValue coerce_value_kind(
    llvm::IRBuilder<>& builder,
    EncodedValue value,
    runtime::simir::ValueKind destination_kind);

[[nodiscard]] EncodedValue canonicalize_logic9_value(
    llvm::IRBuilder<>& builder,
    EncodedValue value);

[[nodiscard]] llvm::Value* logic9_state_mask(
    llvm::IRBuilder<>& builder,
    const EncodedValue& value,
    std::uint8_t state);

[[nodiscard]] EncodedValue lower_logic9_not(
    llvm::IRBuilder<>& builder,
    const EncodedValue& value);

[[nodiscard]] EncodedValue lower_logic9_binary(
    llvm::IRBuilder<>& builder,
    const EncodedValue& lhs,
    const EncodedValue& rhs,
    runtime::simir::BinaryOperator operation);

void store_register(
    llvm::IRBuilder<>& builder,
    const std::vector<RegisterSlot>& registers,
    runtime::simir::RegisterId id,
    EncodedValue value);

[[nodiscard]] std::uint64_t width_mask(std::uint32_t width) noexcept;

[[nodiscard]] llvm::ConstantInt* constant_i64(
    llvm::LLVMContext& context,
    std::uint64_t value);

[[nodiscard]] llvm::IntegerType* packed_integer_type(
    llvm::LLVMContext& context,
    std::uint32_t width);

[[nodiscard]] llvm::ConstantInt* packed_constant(
    llvm::LLVMContext& context,
    std::uint32_t width,
    std::uint64_t value);

[[nodiscard]] llvm::ConstantInt* packed_mask(
    llvm::LLVMContext& context,
    std::uint32_t width);

[[nodiscard]] llvm::ConstantInt* packed_low_mask(
    llvm::LLVMContext& context,
    std::uint32_t storage_width,
    std::uint32_t active_width);

[[nodiscard]] EncodedBit bit_at(
    llvm::IRBuilder<>& builder,
    EncodedValue value,
    std::uint32_t index);

[[nodiscard]] EncodedBit truth_bit(
    llvm::IRBuilder<>& builder,
    EncodedValue value);

[[nodiscard]] EncodedBit bit_xor(
    llvm::IRBuilder<>& builder,
    EncodedBit lhs,
    EncodedBit rhs);

[[nodiscard]] EncodedBit bit_and(
    llvm::IRBuilder<>& builder,
    EncodedBit lhs,
    EncodedBit rhs);

[[nodiscard]] EncodedBit bit_or(
    llvm::IRBuilder<>& builder,
    EncodedBit lhs,
    EncodedBit rhs);

[[nodiscard]] EncodedValue lower_binary(
    llvm::IRBuilder<>& builder,
    runtime::simir::BinaryOperator operation,
    EncodedValue lhs,
    EncodedValue rhs);

struct ValueOperationLowerer {
    llvm::IRBuilder<>& builder;
    std::vector<RegisterSlot>& registers;
    llvm::LLVMContext& context;
    llvm::Type* i32;
    llvm::Type* i64;
    std::function<void()> branch_to_next;
    std::function<void(
        llvm::Value*,
        JitGeneratedRuntimeErrorReason,
        std::string_view)>
        runtime_error_if;
    std::function<llvm::Value*(
        const runtime::simir::DynamicIndex&)>
        dynamic_offset;
    /// Non-null only when control-flow analysis proves that this instruction
    /// immediately consumes the preceding immutable LoadConstant value.
    const runtime::PackedLogic4* constant_part_select_source { };
    std::optional<runtime::simir::SignalId> dynamic_part_signal_source;
    llvm::Value* context_pointer;
    std::uint32_t process;
    std::uint32_t instruction;
    llvm::Value* read_signal_dynamic_part_callback;
    llvm::FunctionType* read_signal_dynamic_part_type;
    llvm::Value* logic9_word_slot;
    llvm::function_ref<llvm::Value*(runtime::simir::SignalId)>
        callback_signal_id;

    void lower(const runtime::simir::CopyRegister& operation);
    void lower(const runtime::simir::ConvertToTwoState& operation);
    void lower(const runtime::simir::UnaryNot& operation);
    void lower(const runtime::simir::LogicalNot& operation);
    void lower(const runtime::simir::LogicalBinary& operation);
    void lower(const runtime::simir::Reduction& operation);
    void lower(const runtime::simir::CountOnes& operation);
    void lower(const runtime::simir::CountBits& operation);
    void lower(const runtime::simir::Shift& operation);
    void lower(const runtime::simir::Extract& operation);
    void lower(const runtime::simir::DynamicExtract& operation);
    void lower(const runtime::simir::DynamicPartSelect& operation);
    void lower(const runtime::simir::Insert& operation);
    void lower(const runtime::simir::DynamicInsert& operation);
    void lower(const runtime::simir::DynamicPartInsert& operation);
    void lower(const runtime::simir::Concatenate& operation);
    void lower(const runtime::simir::Binary& operation);
    void lower(const runtime::simir::IntegerUnary& operation);
    void lower(const runtime::simir::IntegerBinary& operation);
    void lower(const runtime::simir::IntegerCheck& operation);
    void lower(const runtime::simir::ConditionalSelect& operation);
};

struct StringOperationLowerer {
    llvm::Module& module;
    llvm::IRBuilder<>& builder;
    std::vector<RegisterSlot>& registers;
    llvm::LLVMContext& context;
    llvm::Type* i32;
    llvm::Type* i64;
    llvm::Value* context_pointer;
    std::uint32_t process;
    std::uint32_t instruction;
    std::array<llvm::Value*, 10> callbacks;
    std::array<llvm::FunctionType*, 10> callback_types;
    std::function<void(
        llvm::Value*,
        JitGeneratedRuntimeErrorReason,
        std::string_view)>
        runtime_error_if;
    std::function<void()> branch_to_next;

    StringOperationLowerer(
        llvm::Module& module,
        llvm::IRBuilder<>& builder,
        std::vector<RegisterSlot>& registers,
        llvm::LLVMContext& context,
        llvm::Type* i32,
        llvm::Type* i64,
        llvm::Value* context_pointer,
        std::uint32_t process,
        std::uint32_t instruction,
        llvm::StructType* services_type,
        llvm::Value* services_argument,
        std::function<void(
            llvm::Value*,
            JitGeneratedRuntimeErrorReason,
            std::string_view)>
            runtime_error_if,
        std::function<void()> branch_to_next);

    void lower(const runtime::simir::LoadStringConstant& operation);
    void lower(const runtime::simir::CopyStringRegister& operation);
    void lower(const runtime::simir::ReadStringObject& operation);
    void lower(const runtime::simir::WriteStringObject& operation);
    void lower(const runtime::simir::ConcatenateStrings& operation);
    void lower(const runtime::simir::CompareStrings& operation);
    void lower(const runtime::simir::StringLength& operation);
    void lower(const runtime::simir::StringIndex& operation);
    void lower(const runtime::simir::StringReplaceByte& operation);
    void lower(const runtime::simir::StringDisplay& operation);

private:
    void check(llvm::Value* status, std::string_view label);
};

struct FileOperationLowerer {
    llvm::IRBuilder<>& builder;
    std::vector<RegisterSlot>& registers;
    std::vector<RegisterSlot>& frame_registers;
    llvm::LLVMContext& context;
    llvm::Type* i32;
    llvm::Type* i64;
    llvm::Value* context_pointer;
    std::uint32_t process;
    std::uint32_t instruction;
    std::array<llvm::Value*, 6> callbacks;
    std::array<llvm::FunctionType*, 6> callback_types;
    llvm::Value* generic_callback { };
    llvm::FunctionType* generic_callback_type { };
    std::function<void(
        llvm::Value*,
        JitGeneratedRuntimeErrorReason,
        std::string_view)>
        runtime_error_if;
    std::function<void()> branch_to_next;

    FileOperationLowerer(
        llvm::IRBuilder<>& builder,
        std::vector<RegisterSlot>& registers,
        std::vector<RegisterSlot>& frame_registers,
        llvm::LLVMContext& context,
        llvm::Type* i32,
        llvm::Type* i64,
        llvm::Value* context_pointer,
        std::uint32_t process,
        std::uint32_t instruction,
        llvm::StructType* services_type,
        llvm::Value* services_argument,
        std::function<void(
            llvm::Value*,
            JitGeneratedRuntimeErrorReason,
            std::string_view)>
            runtime_error_if,
        std::function<void()> branch_to_next);

    void lower(const runtime::simir::FileOpen& operation);
    void lower(const runtime::simir::FileClose& operation);
    void lower(const runtime::simir::FileWriteLiteral& operation);
    void lower(const runtime::simir::FileWriteFormatted& operation);
    void lower(const runtime::simir::FileWriteString& operation);
    void lower(const runtime::simir::FileReadLine& operation);
    void lower(const runtime::simir::FileEndOfFile& operation);
    void lower(const runtime::simir::FileErrorStatus& operation);
    void lower(const runtime::simir::FileScan& operation);
    void lower(const runtime::simir::FileBinaryRead& operation);
    void lower(const runtime::simir::FilePosition& operation);
    void lower(const runtime::simir::FileFlush& operation);

private:
    void lower_handle_only(
        runtime::simir::RegisterId handle,
        std::size_t callback,
        std::string_view label);
    void lower_result(
        runtime::simir::RegisterId destination,
        runtime::simir::RegisterId handle,
        std::size_t callback,
        std::string_view label);
    void check(llvm::Value* status, std::string_view label);
};

struct ContainerOperationLowerer {
    llvm::IRBuilder<>& builder;
    std::vector<RegisterSlot>& registers;
    std::vector<RegisterSlot>& frame_registers;
    std::span<const runtime::simir::RegisterId> instruction_uses;
    llvm::LLVMContext& context;
    llvm::Type* i32;
    llvm::Type* i64;
    llvm::Value* context_pointer;
    std::uint32_t process;
    std::uint32_t instruction;
    llvm::Value* callback;
    llvm::FunctionType* callback_type;
    llvm::Value* read_word_callback;
    llvm::Value* write_word_callback;
    llvm::StructType* services_type_value;
    llvm::Value* services_argument_value;
    llvm::FunctionType* read_word_callback_type;
    llvm::FunctionType* write_word_callback_type;
    llvm::FunctionType* read_packed_callback_type;
    llvm::FunctionType* write_packed_callback_type;
    std::span<const runtime::simir::ContainerType> container_types;
    llvm::Value* result_aval;
    llvm::Value* result_bval;
    llvm::Value* wide_callback_scratch;
    std::uint32_t wide_callback_scratch_word_stride;
    std::uint32_t fused_container_object_read_distance;
    bool fused_container_object_read_single_use;
    std::function<void(
        llvm::Value*,
        JitGeneratedRuntimeErrorReason,
        std::string_view)>
        runtime_error_if;
    std::function<void()> branch_to_next;

    ContainerOperationLowerer(
        llvm::IRBuilder<>&,
        std::vector<RegisterSlot>&,
        std::vector<RegisterSlot>&,
        std::span<const runtime::simir::RegisterId>,
        llvm::LLVMContext&,
        llvm::Type*,
        llvm::Type*,
        llvm::Value*,
        std::uint32_t,
        std::uint32_t,
        llvm::StructType* services_type,
        llvm::Value* services_argument,
        std::span<const runtime::simir::ContainerType>,
        llvm::Value*,
        llvm::Value*,
        llvm::Value*,
        std::uint32_t,
        std::uint32_t,
        bool,
        std::function<void(
            llvm::Value*,
            JitGeneratedRuntimeErrorReason,
            std::string_view)>,
        std::function<void()>);

    void lower(const runtime::simir::ResizeContainer&);
    void lower(const runtime::simir::SystemVerilogScalarBinary&);
    void lower(const runtime::simir::SystemVerilogMath&);
    void lower(const runtime::simir::CopyContainerRegister&);
    void lower(const runtime::simir::ConditionalContainerSelect&);
    void lower(const runtime::simir::CompareContainers&);
    void lower(const runtime::simir::ReadContainerObject&);
    void lower(const runtime::simir::WriteContainerObject&);
    void lower(const runtime::simir::ContainerSize&);
    void lower(const runtime::simir::ContainerReduction&);
    void lower(const runtime::simir::OrderContainer&);
    void lower(const runtime::simir::LocateContainer&);
    void lower(const runtime::simir::ContainerRead&);
    void lower(const runtime::simir::ContainerWrite&);
    void lower(const runtime::simir::WriteContainerObjectElement&);
    void lower_bound_literal(const runtime::simir::LoadConstant&);
    void lower(const runtime::simir::ContainerStringRead&);
    void lower(const runtime::simir::FormatContainerPattern&);
    void lower(const runtime::simir::AppendContainer&);
    void lower(const runtime::simir::ContainerStringWrite&);
    void lower(const runtime::simir::ContainerElementRead&);
    void lower(const runtime::simir::ContainerElementWrite&);
    void lower(const runtime::simir::ContainerAggregateRead&);
    void lower(const runtime::simir::ContainerAggregateWrite&);
    void lower(const runtime::simir::CopyContainerAggregateElement&);
    void lower(const runtime::simir::DeleteContainer&);
    void lower(const runtime::simir::ContainerExists&);
    void lower(const runtime::simir::TraverseContainer&);
    void lower(const runtime::simir::LoadMemory&);
    void lower(const runtime::simir::VitalMemoryDeclare&);
    void lower(const runtime::simir::PushContainer&);
    void lower(const runtime::simir::PopContainer&);
    void lower(const runtime::simir::StringMethod&);

private:
    void invoke(
        std::optional<runtime::simir::RegisterId>,
        std::optional<runtime::simir::RegisterId>,
        std::optional<runtime::simir::RegisterId>,
        std::string_view,
        bool reload_wide_destination_from_frame = false);
};

struct ControlFlowOperationLowerer {
    llvm::IRBuilder<>& builder;
    std::vector<RegisterSlot>& registers;
    llvm::LLVMContext& context;
    llvm::Type* i8;
    llvm::Type* i32;
    llvm::Type* i64;
    llvm::Value* register_aval;
    llvm::Value* register_bval;
    llvm::Value* register_initialized;
    const std::vector<llvm::BasicBlock*>& instruction_blocks;
    std::span<const runtime::simir::InstructionIndex> static_return_targets;
    std::span<const runtime::simir::InstructionIndex> native_return_targets;
    llvm::StructType* frame_type;
    llvm::Value* frame_argument;
    bool native_call;
    bool native_return;
    bool native_frame;
    llvm::Value* ssa_callable_return;
    llvm::BasicBlock* invalid_pc;
    llvm::Function* function;
    runtime::simir::InstructionIndex instruction;
    std::size_t index;
    std::function<void(
        llvm::Value*,
        JitGeneratedRuntimeErrorReason,
        std::string_view)>
        runtime_error_if;
    std::function<void(
        std::uint32_t,
        std::uint32_t,
        std::uint64_t,
        std::uint32_t,
        std::uint32_t)>
        return_result;

    void lower(const runtime::simir::Jump& operation);
    void lower(const runtime::simir::Call& operation);
    void lower(const runtime::simir::Return& operation);
    void lower(const runtime::simir::CallableFramePush& operation);
    void lower(const runtime::simir::CallableFramePop& operation);
    void lower(const runtime::simir::Branch& operation);
};

struct SignalOperationLowerer {
    llvm::IRBuilder<>& builder;
    std::vector<RegisterSlot>& registers;
    std::span<const std::uint32_t> signal_widths;
    std::span<const runtime::simir::ValueKind> signal_value_kinds;
    std::span<const runtime::simir::SignalId> direct_read_signals;
    std::span<const runtime::simir::SignalId> direct_update_signals;
    llvm::StructType* direct_update_slot_type;
    llvm::Value* direct_update_slots;
    llvm::Value* direct_update_active_words;
    bool require_direct_update_slots;
    bool require_direct_read_signals;
    llvm::LLVMContext& context;
    llvm::Type* i32;
    llvm::Type* i64;
    llvm::Value* context_pointer;
    llvm::Value* direct_signal_aval;
    llvm::Value* direct_signal_bval;
    llvm::Value* direct_signal_logic9_plane0;
    llvm::Value* direct_signal_logic9_plane1;
    llvm::Value* direct_signal_logic9_plane2;
    llvm::Value* direct_signal_logic9_plane3;
    llvm::Value* direct_read_signal_map;
    llvm::Value* direct_read_signal_count;
    llvm::Value* direct_signal_count;
    std::uint32_t process_id;
    runtime::simir::InstructionIndex instruction;
    llvm::Value* read_callback;
    llvm::Value* read_logic9_callback;
    llvm::Value* write_callback;
    llvm::Value* write_update_callback;
    llvm::Value* write_after_callback;
    llvm::Value* write_logic9_callback;
    llvm::Value* write_update_logic9_callback;
    llvm::Value* write_after_logic9_callback;
    llvm::Value* write_blocking_slice_callback;
    llvm::Value* write_update_slice_callback;
    llvm::Value* write_after_slice_callback;
    llvm::Value* write_blocking_slice_logic9_callback;
    llvm::Value* write_update_slice_logic9_callback;
    llvm::Value* write_after_slice_logic9_callback;
    llvm::Value* force_signal_slice_callback;
    llvm::Value* force_signal_slice_logic9_callback;
    llvm::Value* release_signal_slice_callback;
    llvm::Value* force_driver_signal_slice_callback;
    llvm::Value* force_driver_signal_slice_logic9_callback;
    llvm::Value* release_driver_signal_slice_callback;
    llvm::Value* write_projected_waveform_callback;
    llvm::Value* write_projected_waveform_logic9_callback;
    llvm::Value* write_projected_callback;
    llvm::Value* write_projected_logic9_callback;
    llvm::Value* write_inertial_callback;
    llvm::Value* write_inertial_logic9_callback;
    llvm::Value* signal_event_callback;
    llvm::Value* signal_last_value_callback;
    llvm::Value* signal_last_value_logic9_callback;
    llvm::Value* signal_last_event_callback;
    llvm::Value* signal_active_callback;
    llvm::Value* signal_last_active_callback;
    llvm::Value* signal_driving_callback;
    llvm::Value* signal_driving_value_callback;
    llvm::Value* signal_driving_value_logic9_callback;
    llvm::Value* read_simulation_time_callback;
    llvm::Value* vital_timing_check_callback;
    llvm::Value* vital_delay_callback;
    llvm::FunctionType* read_type;
    llvm::FunctionType* read_logic9_type;
    llvm::FunctionType* write_type;
    llvm::FunctionType* write_update_type;
    llvm::FunctionType* write_after_type;
    llvm::FunctionType* write_logic9_type;
    llvm::FunctionType* write_update_logic9_type;
    llvm::FunctionType* write_after_logic9_type;
    llvm::FunctionType* write_slice_type;
    llvm::FunctionType* write_update_slice_type;
    llvm::FunctionType* write_after_slice_type;
    llvm::FunctionType* write_slice_logic9_type;
    llvm::FunctionType* write_update_slice_logic9_type;
    llvm::FunctionType* write_after_slice_logic9_type;
    llvm::FunctionType* release_slice_type;
    llvm::FunctionType* write_projected_waveform_type;
    llvm::FunctionType* write_projected_waveform_logic9_type;
    llvm::FunctionType* write_projected_type;
    llvm::FunctionType* write_projected_logic9_type;
    llvm::FunctionType* write_inertial_type;
    llvm::FunctionType* write_inertial_logic9_type;
    llvm::FunctionType* signal_event_type;
    llvm::FunctionType* signal_last_value_type;
    llvm::FunctionType* signal_last_event_type;
    llvm::FunctionType* signal_active_type;
    llvm::FunctionType* signal_last_active_type;
    llvm::FunctionType* signal_driving_type;
    llvm::FunctionType* signal_driving_value_type;
    llvm::FunctionType* read_simulation_time_type;
    llvm::FunctionType* vital_timing_check_type;
    llvm::FunctionType* vital_delay_type;
    llvm::StructType* projected_element_type;
    llvm::StructType* logic9_projected_element_type;
    llvm::Value* read_bval_slot;
    llvm::Value* logic9_word_slot;
    std::function<void()> branch_to_next;
    std::function<void(llvm::Value*, EncodedValue)> store_logic9_word;
    std::function<EncodedValue(llvm::Value*, std::uint32_t)>
        load_logic9_word;
    std::function<void(
        llvm::Value*,
        JitGeneratedRuntimeErrorReason,
        std::string_view)>
        runtime_error_if;
    std::function<llvm::Value*(const runtime::simir::DynamicIndex&)>
        dynamic_offset;
    llvm::function_ref<llvm::Value*(runtime::simir::SignalId)>
        callback_signal_id;

    [[nodiscard]] bool begin_direct_update(
        runtime::simir::SignalId signal,
        std::uint32_t offset,
        EncodedValue source);
    void mark_direct_update_active(
        llvm::Value* slot,
        std::uint32_t slot_index,
        llvm::Value* enabled = nullptr);
    [[nodiscard]] bool begin_direct_update(
        runtime::simir::SignalId signal,
        llvm::Value* offset,
        llvm::Value* width,
        EncodedValue source);
    [[nodiscard]] bool begin_direct_wide_update(
        runtime::simir::SignalId signal,
        std::uint32_t offset,
        EncodedValue source);
    [[nodiscard]] bool begin_direct_wide_update(
        runtime::simir::SignalId signal,
        llvm::Value* offset,
        llvm::Value* width,
        EncodedValue source);
    void lower(const runtime::simir::LoadConstant& operation);
    void lower(const runtime::simir::WriteBlocking& operation);
    void lower(const runtime::simir::WriteUpdate& operation);
    void lower(const runtime::simir::WriteAfter& operation);
    void lower(const runtime::simir::WriteBlockingSlice& operation);
    void lower(const runtime::simir::WriteUpdateSlice& operation);
    void lower(const runtime::simir::WriteAfterSlice& operation);
    void lower(const runtime::simir::ForceSignalSlice& operation);
    void lower(const runtime::simir::ReleaseSignalSlice& operation);
    void lower(const runtime::simir::WriteProjectedWaveform& operation);
    void lower(const runtime::simir::WriteProjected& operation);
    void lower(const runtime::simir::WriteInertial& operation);
    void lower(const runtime::simir::ReadSignal& operation);
    void lower(const runtime::simir::SignalEvent& operation);
    void lower(const runtime::simir::SignalLastValue& operation);
    void lower(const runtime::simir::SignalLastEvent& operation);
    void lower(const runtime::simir::ReadSimulationTime& operation);
    void lower(const runtime::simir::VitalTimingCheck& operation);
    void lower(const runtime::simir::VitalDelay& operation);
    void lower(const runtime::simir::SignalActive& operation);
    void lower(const runtime::simir::SignalLastActive& operation);
    void lower(const runtime::simir::SignalDriving& operation);
    void lower(const runtime::simir::SignalDrivingValue& operation);
};

struct OutputOperationLowerer {
    llvm::IRBuilder<>& builder;
    llvm::LLVMContext& context;
    llvm::Type* i32;
    llvm::Value* context_pointer;
    std::uint32_t process_id;
    runtime::simir::InstructionIndex instruction;
    std::size_t index;
    const std::string& symbol;
    llvm::FunctionType* output_type;
    llvm::FunctionType* time_output_type;
    llvm::FunctionType* report_type;
    llvm::Value* output_callback;
    llvm::Value* postponed_output_callback;
    llvm::Value* time_output_callback;
    llvm::Value* monitor_install_callback;
    llvm::Value* monitor_control_callback;
    llvm::Value* report_callback;
    std::function<void()> branch_to_next;
    std::function<void(
        std::uint32_t,
        std::uint32_t,
        std::uint64_t,
        std::uint32_t,
        std::uint32_t)>
        return_result;

    void lower(const runtime::simir::Display& operation);
    void lower(const runtime::simir::TimeDisplay& operation);
    void lower(const runtime::simir::MonitorInstall& operation);
    void lower(const runtime::simir::MonitorControl& operation);
    void lower(const runtime::simir::Report& operation);
    void lower(const runtime::simir::StringReport& operation);
    void lower(const runtime::simir::WaitFor& operation);
    void lower(const runtime::simir::WaitOn& operation);
};

} // namespace fsim::compiler::llvm_detail
