// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "llvm_jit_internal.hpp"

#include <llvm/IR/IRBuilder.h>

#include <array>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace fsim::compiler::llvm_detail {

struct EncodedValue {
  llvm::Value* aval{};
  llvm::Value* bval{};
  std::uint32_t width{};
  llvm::Value* logic9_plane2{};
  llvm::Value* logic9_plane3{};
  runtime::simir::ValueKind kind{
      runtime::simir::ValueKind::logic4};
};

struct RegisterSlot {
  llvm::Value* aval{};
  llvm::Value* bval{};
  llvm::Value* initialized{};
  std::uint32_t width{};
  llvm::Value* logic9_plane2{};
  llvm::Value* logic9_plane3{};
  runtime::simir::ValueKind kind{
      runtime::simir::ValueKind::logic4};
};

struct EncodedBit {
  llvm::Value* aval{};
  llvm::Value* bval{};
};

[[nodiscard]] runtime::simir::ShiftOperator reverse_shift(
    runtime::simir::ShiftOperator operation) noexcept;

[[nodiscard]] EncodedValue load_register(
    llvm::IRBuilder<>& builder,
    const std::vector<RegisterSlot>& registers,
    runtime::simir::RegisterId id);

[[nodiscard]] EncodedValue coerce_value_kind(
    llvm::IRBuilder<>& builder,
    EncodedValue value,
    runtime::simir::ValueKind destination_kind);

[[nodiscard]] llvm::Value* logic9_state_mask(
    llvm::IRBuilder<>& builder,
    const EncodedValue& value,
    std::uint8_t state);

[[nodiscard]] EncodedValue map_logic9_unary(
    llvm::IRBuilder<>& builder,
    const EncodedValue& value,
    const std::array<runtime::Logic9, 9>& table);

[[nodiscard]] EncodedValue map_logic9_binary(
    llvm::IRBuilder<>& builder,
    const EncodedValue& lhs,
    const EncodedValue& rhs,
    const std::array<std::array<runtime::Logic9, 9>, 9>& table);

void store_register(
    llvm::IRBuilder<>& builder,
    const std::vector<RegisterSlot>& registers,
    runtime::simir::RegisterId id,
    EncodedValue value);

[[nodiscard]] std::uint64_t width_mask(std::uint32_t width) noexcept;

[[nodiscard]] llvm::ConstantInt* constant_i64(
    llvm::LLVMContext& context,
    std::uint64_t value);

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
      std::string_view)> runtime_error_if;
  std::function<llvm::Value*(
      const runtime::simir::DynamicIndex&)> dynamic_offset;

  void lower(const runtime::simir::CopyRegister& operation);
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
      std::string_view)> runtime_error_if;
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
      llvm::StructType* runtime_type,
      llvm::Value* runtime_argument,
      std::function<void(
          llvm::Value*,
          JitGeneratedRuntimeErrorReason,
          std::string_view)> runtime_error_if,
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
  llvm::LLVMContext& context;
  llvm::Type* i32;
  llvm::Type* i64;
  llvm::Value* context_pointer;
  std::uint32_t process;
  std::uint32_t instruction;
  std::array<llvm::Value*, 6> callbacks;
  std::array<llvm::FunctionType*, 6> callback_types;
  std::function<void(
      llvm::Value*,
      JitGeneratedRuntimeErrorReason,
      std::string_view)> runtime_error_if;
  std::function<void()> branch_to_next;

  FileOperationLowerer(
      llvm::IRBuilder<>& builder,
      std::vector<RegisterSlot>& registers,
      llvm::LLVMContext& context,
      llvm::Type* i32,
      llvm::Type* i64,
      llvm::Value* context_pointer,
      std::uint32_t process,
      std::uint32_t instruction,
      llvm::StructType* runtime_type,
      llvm::Value* runtime_argument,
      std::function<void(
          llvm::Value*,
          JitGeneratedRuntimeErrorReason,
          std::string_view)> runtime_error_if,
      std::function<void()> branch_to_next);

  void lower(const runtime::simir::FileOpen& operation);
  void lower(const runtime::simir::FileClose& operation);
  void lower(const runtime::simir::FileWriteLiteral& operation);
  void lower(const runtime::simir::FileWriteFormatted& operation);
  void lower(const runtime::simir::FileWriteString& operation);
  void lower(const runtime::simir::FileReadLine& operation);
  void lower(const runtime::simir::FileEndOfFile& operation);
  void lower(const runtime::simir::FileErrorStatus& operation);

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
  llvm::LLVMContext& context;
  llvm::Type* i32;
  llvm::Type* i64;
  llvm::Value* context_pointer;
  std::uint32_t process;
  std::uint32_t instruction;
  llvm::Value* callback;
  llvm::FunctionType* callback_type;
  std::function<void(
      llvm::Value*,
      JitGeneratedRuntimeErrorReason,
      std::string_view)> runtime_error_if;
  std::function<void()> branch_to_next;

  ContainerOperationLowerer(
      llvm::IRBuilder<>&,
      std::vector<RegisterSlot>&,
      llvm::LLVMContext&,
      llvm::Type*,
      llvm::Type*,
      llvm::Value*,
      std::uint32_t,
      std::uint32_t,
      llvm::StructType*,
      llvm::Value*,
      std::function<void(
          llvm::Value*,
          JitGeneratedRuntimeErrorReason,
          std::string_view)>,
      std::function<void()>);

  void lower(const runtime::simir::ResizeContainer&);
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
  void lower(const runtime::simir::DeleteContainer&);
  void lower(const runtime::simir::ContainerExists&);
  void lower(const runtime::simir::TraverseContainer&);
  void lower(const runtime::simir::LoadMemory&);
  void lower(const runtime::simir::PushContainer&);
  void lower(const runtime::simir::PopContainer&);

private:
  void invoke(
      std::optional<runtime::simir::RegisterId>,
      std::optional<runtime::simir::RegisterId>,
      std::optional<runtime::simir::RegisterId>,
      std::string_view);
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
  llvm::BasicBlock* invalid_pc;
  llvm::Function* function;
  runtime::simir::InstructionIndex instruction;
  std::size_t index;
  std::function<void(
      llvm::Value*,
      JitGeneratedRuntimeErrorReason,
      std::string_view)> runtime_error_if;
  std::function<void(
      std::uint32_t,
      std::uint32_t,
      std::uint64_t,
      std::uint32_t,
      std::uint32_t)> return_result;

  void lower(const runtime::simir::Jump& operation);
  void lower(const runtime::simir::Call& operation);
  void lower(const runtime::simir::Return& operation);
  void lower(const runtime::simir::Branch& operation);
};

struct SignalOperationLowerer {
  llvm::IRBuilder<>& builder;
  std::vector<RegisterSlot>& registers;
  std::span<const std::uint32_t> signal_widths;
  std::span<const runtime::simir::ValueKind> signal_value_kinds;
  llvm::LLVMContext& context;
  llvm::Type* i32;
  llvm::Type* i64;
  llvm::Value* context_pointer;
  llvm::Value* read_callback;
  llvm::Value* read_logic9_callback;
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
  llvm::FunctionType* read_type;
  llvm::FunctionType* read_logic9_type;
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
      std::string_view)> runtime_error_if;

  void lower(const runtime::simir::LoadConstant& operation);
  void lower(const runtime::simir::WriteProjectedWaveform& operation);
  void lower(const runtime::simir::WriteProjected& operation);
  void lower(const runtime::simir::WriteInertial& operation);
  void lower(const runtime::simir::ReadSignal& operation);
  void lower(const runtime::simir::SignalEvent& operation);
  void lower(const runtime::simir::SignalLastValue& operation);
  void lower(const runtime::simir::SignalLastEvent& operation);
  void lower(const runtime::simir::SignalActive& operation);
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
      std::uint32_t)> return_result;

  void lower(const runtime::simir::Display& operation);
  void lower(const runtime::simir::TimeDisplay& operation);
  void lower(const runtime::simir::MonitorInstall& operation);
  void lower(const runtime::simir::MonitorControl& operation);
  void lower(const runtime::simir::Report& operation);
  void lower(const runtime::simir::WaitFor& operation);
  void lower(const runtime::simir::WaitOn& operation);
};

}  // namespace fsim::compiler::llvm_detail
