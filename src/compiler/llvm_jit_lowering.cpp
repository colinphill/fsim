// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_internal.hpp"
#include "llvm_jit_lowering_internal.hpp"
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/raw_ostream.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>
namespace fsim::compiler::llvm_detail {
using runtime::Logic9;
using runtime::simir::Assert;
using runtime::simir::Binary;
using runtime::simir::BinaryOperator;
using runtime::simir::Branch;
using runtime::simir::Call;
using runtime::simir::Concatenate;
using runtime::simir::ConditionalSelect;
using runtime::simir::CountOnes;
using runtime::simir::CountBits;
using runtime::simir::CopyRegister;
using runtime::simir::DebugPoint;
using runtime::simir::Display;
using runtime::simir::DynamicExtract;
using runtime::simir::DynamicPartSelect;
using runtime::simir::DynamicIndex;
using runtime::simir::DynamicInsert;
using runtime::simir::EdgeKind;
using runtime::simir::Extract;
using runtime::simir::FormatDisplay;
using runtime::simir::Halt;
using runtime::simir::InstructionIndex;
using runtime::simir::Insert;
using runtime::simir::IntegerBinary;
using runtime::simir::IntegerBinaryOperator;
using runtime::simir::IntegerCheck;
using runtime::simir::IntegerUnary;
using runtime::simir::IntegerUnaryOperator;
using runtime::simir::Jump;
using runtime::simir::LoadConstant;
using runtime::simir::LogicalBinary;
using runtime::simir::LogicalBinaryOperator;
using runtime::simir::LogicalNot;
using runtime::simir::MonitorControl;
using runtime::simir::MonitorInstall;
using runtime::simir::MonitorValueKind;
using runtime::simir::Operation;
using runtime::simir::Pause;
using runtime::simir::Process;
using runtime::simir::ReadSignal;
using runtime::simir::Reduction;
using runtime::simir::ReductionOperator;
using runtime::simir::RegisterId;
using runtime::simir::RandomValue;
using runtime::simir::Report;
using runtime::simir::Return;
using runtime::simir::Shift;
using runtime::simir::ShiftOperator;
using runtime::simir::SignalActive;
using runtime::simir::SignalEvent;
using runtime::simir::SignalLastEvent;
using runtime::simir::SignalLastValue;
using runtime::simir::Stop;
using runtime::simir::TimeDisplay;
using runtime::simir::UnaryNot;
using runtime::simir::UnknownBranchPolicy;
using runtime::simir::ValueKind;
using runtime::simir::WaitFor;
using runtime::simir::WaitOn;
using runtime::simir::WaitSensitivity;
using runtime::simir::WaitForever;
using runtime::simir::WriteAfter;
using runtime::simir::WriteAfterDynamicSlice;
using runtime::simir::WriteAfterSlice;
using runtime::simir::WriteBlocking;
using runtime::simir::WriteBlockingDynamicSlice;
using runtime::simir::WriteBlockingSlice;
using runtime::simir::WriteInertial;
using runtime::simir::WriteInertialDynamicSlice;
using runtime::simir::WriteInertialSlice;
using runtime::simir::WriteProjected;
using runtime::simir::WriteProjectedDynamicSlice;
using runtime::simir::WriteProjectedWaveform;
using runtime::simir::WriteProjectedWaveformDynamicSlice;
using runtime::simir::WriteProjectedSlice;
using runtime::simir::WriteProjectedWaveformSlice;
using runtime::simir::WriteUpdate;
using runtime::simir::WriteUpdateDynamicSlice;
using runtime::simir::WriteUpdateSlice;
using runtime::simir::Yield;
template <class... Ts> struct Overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;
void lower_process(llvm::Module &module, const std::string &symbol,
                   const Process &process,
                   const std::span<const std::uint32_t> signal_widths,
                   const std::span<const ValueKind> signal_value_kinds,
                   const ValidatedProcess &validated,
                   const bool debug_instrumentation) {
  auto &context = module.getContext();
  auto *i32 = llvm::Type::getInt32Ty(context);
  auto *i64 = llvm::Type::getInt64Ty(context);
  auto *pointer = llvm::PointerType::getUnqual(context);
  auto *runtime_type = llvm::StructType::create(
      context,
      {i32, i32, pointer, pointer, pointer, pointer, pointer, pointer,
       i32, i32, pointer, pointer, pointer, pointer, pointer, pointer,
       pointer, pointer, pointer, pointer, pointer, pointer, pointer,
       pointer, pointer, pointer, pointer, pointer, pointer, pointer,
       pointer, pointer, pointer, pointer, pointer, pointer, pointer,
       pointer, pointer, pointer, pointer, pointer, pointer, pointer,
       pointer, pointer, pointer, pointer, pointer, pointer, pointer,
       pointer, pointer, pointer, pointer, pointer, pointer, pointer,
       pointer, pointer, pointer, pointer, pointer, pointer},
      "fsim_jit_runtime_v1");
  auto *frame_type = llvm::StructType::create(
      context,
      {i32, i32, i64, i64, i32, i32, i32, i32, pointer, pointer,
       pointer, pointer, pointer},
      "fsim_jit_frame_v1");
  auto *result_type = llvm::StructType::create(
      context, {i32, i32, i32, i32, i64},
      "fsim_jit_resume_result_v1");
  auto *function_type =
      llvm::FunctionType::get(i32, {pointer, pointer, pointer}, false);
  auto *function = llvm::Function::Create(
      function_type, llvm::Function::ExternalLinkage, symbol, module);
  function->setCallingConv(llvm::CallingConv::C);
  function->getArg(0)->setName("runtime");
  function->getArg(1)->setName("frame");
  function->getArg(2)->setName("result");
  auto *entry = llvm::BasicBlock::Create(context, "entry", function);
  llvm::IRBuilder<> builder(entry);
  auto *runtime_argument = function->getArg(0);
  auto *frame_argument = function->getArg(1);
  auto *result_argument = function->getArg(2);
  auto *context_pointer = builder.CreateLoad(
      pointer, builder.CreateStructGEP(runtime_type, runtime_argument, 2),
      "context");
  auto *read_callback = builder.CreateLoad(
      pointer, builder.CreateStructGEP(runtime_type, runtime_argument, 3),
      "read_signal");
  auto *write_callback = builder.CreateLoad(
      pointer, builder.CreateStructGEP(runtime_type, runtime_argument, 4),
      "write_signal");
  auto *assert_callback = builder.CreateLoad(
      pointer, builder.CreateStructGEP(runtime_type, runtime_argument, 5),
      "assert_failed");
  llvm::Value *write_update_callback = nullptr;
  if (validated.uses_write_update) {
    write_update_callback = builder.CreateLoad(
        pointer, builder.CreateStructGEP(runtime_type, runtime_argument, 6),
        "write_update");
  }
  llvm::Value *write_after_callback = nullptr;
  if (validated.uses_write_after) {
    write_after_callback = builder.CreateLoad(
        pointer, builder.CreateStructGEP(runtime_type, runtime_argument, 7),
        "write_after");
  }
  llvm::Value* write_blocking_slice_callback = nullptr;
  if (validated.uses_write_blocking_slice) {
    write_blocking_slice_callback = builder.CreateLoad(
        pointer,
        builder.CreateStructGEP(
            runtime_type, runtime_argument, 10),
        "write_signal_slice");
  }
  llvm::Value* write_update_slice_callback = nullptr;
  if (validated.uses_write_update_slice) {
    write_update_slice_callback = builder.CreateLoad(
        pointer,
        builder.CreateStructGEP(
            runtime_type, runtime_argument, 11),
        "write_update_slice");
  }
  llvm::Value* write_after_slice_callback = nullptr;
  if (validated.uses_write_after_slice) {
    write_after_slice_callback = builder.CreateLoad(
        pointer,
        builder.CreateStructGEP(
            runtime_type, runtime_argument, 12),
        "write_after_slice");
  }
  llvm::Value* runtime_flags = nullptr;
  if (validated.uses_debug_points) {
    runtime_flags = builder.CreateLoad(
        i32, builder.CreateStructGEP(runtime_type, runtime_argument, 8),
        "runtime.flags");
  }
  llvm::Value* signal_event_callback = nullptr;
  if (validated.uses_signal_event) {
    signal_event_callback = builder.CreateLoad(
        pointer,
        builder.CreateStructGEP(
            runtime_type, runtime_argument, 13),
        "signal_event");
  }
  llvm::Value* signal_last_value_callback = nullptr;
  if (validated.uses_signal_last_value) {
    signal_last_value_callback = builder.CreateLoad(
        pointer,
        builder.CreateStructGEP(
            runtime_type, runtime_argument, 14),
        "signal_last_value");
  }
  llvm::Value* signal_last_event_callback = nullptr;
  if (validated.uses_signal_last_event) {
    signal_last_event_callback = builder.CreateLoad(
        pointer,
        builder.CreateStructGEP(
            runtime_type, runtime_argument, 15),
        "signal_last_event");
  }
  llvm::Value* signal_active_callback = nullptr;
  if (validated.uses_signal_active) {
    signal_active_callback = builder.CreateLoad(
        pointer,
        builder.CreateStructGEP(
            runtime_type, runtime_argument, 16),
        "signal_active");
  }
  llvm::Value* output_callback = nullptr;
  if (validated.uses_output) {
    output_callback = builder.CreateLoad(
        pointer,
        builder.CreateStructGEP(
            runtime_type, runtime_argument, 17),
        "write_output");
  }
  llvm::Value* postponed_output_callback = nullptr;
  if (validated.uses_postponed_output) {
    postponed_output_callback = builder.CreateLoad(
        pointer,
        builder.CreateStructGEP(
            runtime_type, runtime_argument, 18),
        "schedule_output");
  }
  llvm::Value* report_callback = nullptr;
  if (validated.uses_report) {
    report_callback = builder.CreateLoad(
        pointer,
        builder.CreateStructGEP(
            runtime_type, runtime_argument, 19),
        "write_report");
  }
  llvm::Value* formatted_output_callback = nullptr;
  if (validated.uses_formatted_output) {
    formatted_output_callback = builder.CreateLoad(
        pointer,
        builder.CreateStructGEP(
            runtime_type, runtime_argument, 20),
        "write_formatted");
  }
  llvm::Value* time_output_callback = nullptr;
  if (validated.uses_time_output) {
    time_output_callback = builder.CreateLoad(
        pointer,
        builder.CreateStructGEP(
            runtime_type, runtime_argument, 21),
        "write_time");
  }
  llvm::Value* monitor_install_callback = nullptr;
  if (validated.uses_monitor_install) {
    monitor_install_callback = builder.CreateLoad(
        pointer,
        builder.CreateStructGEP(
            runtime_type, runtime_argument, 22),
        "install_monitor");
  }
  llvm::Value* monitor_control_callback = nullptr;
  if (validated.uses_monitor_control) {
    monitor_control_callback = builder.CreateLoad(
        pointer,
        builder.CreateStructGEP(
            runtime_type, runtime_argument, 23),
        "control_monitor");
  }
  llvm::Value* random_value_callback = nullptr;
  if (validated.uses_random_value) {
    random_value_callback = builder.CreateLoad(
        pointer,
        builder.CreateStructGEP(
            runtime_type, runtime_argument, 24),
        "random_value");
  }
  llvm::Value* write_inertial_callback = nullptr;
  if (validated.uses_write_inertial) {
    write_inertial_callback = builder.CreateLoad(
        pointer,
        builder.CreateStructGEP(
            runtime_type, runtime_argument, 25),
        "write_inertial");
  }
  llvm::Value* write_inertial_slice_callback = nullptr;
  if (validated.uses_write_inertial_slice) {
    write_inertial_slice_callback = builder.CreateLoad(
        pointer,
        builder.CreateStructGEP(
            runtime_type, runtime_argument, 26),
        "write_inertial_slice");
  }
  llvm::Value* write_projected_callback = nullptr;
  if (validated.uses_write_projected) {
    write_projected_callback = builder.CreateLoad(
        pointer,
        builder.CreateStructGEP(
            runtime_type, runtime_argument, 27),
        "write_projected");
  }
  llvm::Value* write_projected_slice_callback = nullptr;
  if (validated.uses_write_projected_slice) {
    write_projected_slice_callback = builder.CreateLoad(
        pointer,
        builder.CreateStructGEP(
            runtime_type, runtime_argument, 28),
        "write_projected_slice");
  }
  llvm::Value* write_projected_waveform_callback = nullptr;
  if (validated.uses_write_projected_waveform) {
    write_projected_waveform_callback = builder.CreateLoad(
        pointer,
        builder.CreateStructGEP(
            runtime_type, runtime_argument, 29),
        "write_projected_waveform");
  }
  llvm::Value* write_projected_waveform_slice_callback = nullptr;
  if (validated.uses_write_projected_waveform_slice) {
    write_projected_waveform_slice_callback = builder.CreateLoad(
        pointer,
        builder.CreateStructGEP(
            runtime_type, runtime_argument, 30),
        "write_projected_waveform_slice");
  }
  llvm::Value* read_logic9_callback = nullptr;
  llvm::Value* write_logic9_callback = nullptr;
  llvm::Value* write_update_logic9_callback = nullptr;
  llvm::Value* write_after_logic9_callback = nullptr;
  llvm::Value* write_blocking_slice_logic9_callback = nullptr;
  llvm::Value* write_update_slice_logic9_callback = nullptr;
  llvm::Value* write_after_slice_logic9_callback = nullptr;
  llvm::Value* signal_last_value_logic9_callback = nullptr;
  llvm::Value* write_inertial_logic9_callback = nullptr;
  llvm::Value* write_inertial_slice_logic9_callback = nullptr;
  llvm::Value* write_projected_logic9_callback = nullptr;
  llvm::Value* write_projected_slice_logic9_callback = nullptr;
  llvm::Value* write_projected_waveform_logic9_callback = nullptr;
  llvm::Value* write_projected_waveform_slice_logic9_callback = nullptr;
  llvm::Value* write_formatted_logic9_callback = nullptr;
  if (validated.uses_logic9) {
    const auto load_callback =
        [&](const unsigned index,
            const llvm::Twine& name) -> llvm::Value* {
          return builder.CreateLoad(
              pointer,
              builder.CreateStructGEP(
                  runtime_type, runtime_argument, index),
              name);
        };
    read_logic9_callback =
        load_callback(31, "read_signal_logic9");
    write_logic9_callback =
        load_callback(32, "write_signal_logic9");
    write_update_logic9_callback =
        load_callback(33, "write_update_logic9");
    write_after_logic9_callback =
        load_callback(34, "write_after_logic9");
    write_blocking_slice_logic9_callback =
        load_callback(35, "write_signal_slice_logic9");
    write_update_slice_logic9_callback =
        load_callback(36, "write_update_slice_logic9");
    write_after_slice_logic9_callback =
        load_callback(37, "write_after_slice_logic9");
    signal_last_value_logic9_callback =
        load_callback(38, "signal_last_value_logic9");
    write_inertial_logic9_callback =
        load_callback(39, "write_inertial_logic9");
    write_inertial_slice_logic9_callback =
        load_callback(40, "write_inertial_slice_logic9");
    write_projected_logic9_callback =
        load_callback(41, "write_projected_logic9");
    write_projected_slice_logic9_callback =
        load_callback(42, "write_projected_slice_logic9");
    write_projected_waveform_logic9_callback =
        load_callback(43, "write_projected_waveform_logic9");
    write_projected_waveform_slice_logic9_callback =
        load_callback(44, "write_projected_waveform_slice_logic9");
    write_formatted_logic9_callback =
        load_callback(45, "write_formatted_logic9");
  }
  auto *read_type =
      llvm::FunctionType::get(i64, {pointer, i32, pointer}, false);
  auto *write_type =
      llvm::FunctionType::get(llvm::Type::getVoidTy(context),
                              {pointer, i32, i64, i64}, false);
  auto *assert_type =
      llvm::FunctionType::get(llvm::Type::getVoidTy(context),
                              {pointer, i32, i32, pointer, i64}, false);
  auto *write_after_type =
      llvm::FunctionType::get(llvm::Type::getVoidTy(context),
                              {pointer, i32, i64, i64, i64}, false);
  auto* write_slice_type =
      llvm::FunctionType::get(
          llvm::Type::getVoidTy(context),
          {pointer, i32, i32, i32, i64, i64},
          false);
  auto* write_after_slice_type =
      llvm::FunctionType::get(
          llvm::Type::getVoidTy(context),
          {pointer, i32, i32, i32, i64, i64, i64},
          false);
  auto* write_inertial_type =
      llvm::FunctionType::get(
          llvm::Type::getVoidTy(context),
          {pointer, i32, i64, i64, i64, i64, i64},
          false);
  auto* write_inertial_slice_type =
      llvm::FunctionType::get(
          llvm::Type::getVoidTy(context),
          {pointer, i32, i32, i32, i64, i64, i64, i64, i64},
          false);
  auto* write_projected_type =
      llvm::FunctionType::get(
          llvm::Type::getVoidTy(context),
          {pointer, i32, i64, i64, i64, i64, i32},
          false);
  auto* write_projected_slice_type =
      llvm::FunctionType::get(
          llvm::Type::getVoidTy(context),
          {pointer, i32, i32, i32, i64, i64, i64, i64, i32},
          false);
  auto* projected_element_type = llvm::StructType::create(
      context, {i64, i64, i64}, "fsim_jit_projected_element_v1");
  auto* write_projected_waveform_type =
      llvm::FunctionType::get(
          llvm::Type::getVoidTy(context),
          {pointer, i32, i32, pointer, i32, i64, i32},
          false);
  auto* write_projected_waveform_slice_type =
      llvm::FunctionType::get(
          llvm::Type::getVoidTy(context),
          {pointer, i32, i32, i32, pointer, i32, i64, i32},
          false);
  auto* signal_event_type =
      llvm::FunctionType::get(i32, {pointer, i32}, false);
  auto* signal_last_value_type =
      llvm::FunctionType::get(i64, {pointer, i32, pointer}, false);
  auto* signal_last_event_type =
      llvm::FunctionType::get(i64, {pointer, i32}, false);
  auto* signal_active_type =
      llvm::FunctionType::get(i32, {pointer, i32}, false);
  auto* output_type =
      llvm::FunctionType::get(
          llvm::Type::getVoidTy(context),
          {pointer, i32, pointer, i64, i32},
          false);
  auto* report_type =
      llvm::FunctionType::get(
          llvm::Type::getVoidTy(context),
          {pointer, i32, i32},
          false);
  auto* formatted_output_type =
      llvm::FunctionType::get(
          llvm::Type::getVoidTy(context),
          {pointer, i32, i32, i32, i64, i64},
          false);
  auto* time_output_type =
      llvm::FunctionType::get(
          llvm::Type::getVoidTy(context),
          {pointer, i32, i32},
          false);
  auto* random_value_type =
      llvm::FunctionType::get(
          i64,
          {pointer, i32, i32, i64, i64, i64, i64, pointer},
          false);
  auto* logic9_word_type = llvm::ArrayType::get(i64, 4);
  auto* read_logic9_type =
      llvm::FunctionType::get(
          llvm::Type::getVoidTy(context),
          {pointer, i32, pointer},
          false);
  auto* write_logic9_type =
      llvm::FunctionType::get(
          llvm::Type::getVoidTy(context),
          {pointer, i32, pointer},
          false);
  auto* write_after_logic9_type =
      llvm::FunctionType::get(
          llvm::Type::getVoidTy(context),
          {pointer, i32, pointer, i64},
          false);
  auto* write_slice_logic9_type =
      llvm::FunctionType::get(
          llvm::Type::getVoidTy(context),
          {pointer, i32, i32, i32, pointer},
          false);
  auto* write_after_slice_logic9_type =
      llvm::FunctionType::get(
          llvm::Type::getVoidTy(context),
          {pointer, i32, i32, i32, pointer, i64},
          false);
  auto* write_inertial_logic9_type =
      llvm::FunctionType::get(
          llvm::Type::getVoidTy(context),
          {pointer, i32, pointer, i64, i64, i64},
          false);
  auto* write_inertial_slice_logic9_type =
      llvm::FunctionType::get(
          llvm::Type::getVoidTy(context),
          {pointer, i32, i32, i32, pointer, i64, i64, i64},
          false);
  auto* write_projected_logic9_type =
      llvm::FunctionType::get(
          llvm::Type::getVoidTy(context),
          {pointer, i32, pointer, i64, i64, i32},
          false);
  auto* write_projected_slice_logic9_type =
      llvm::FunctionType::get(
          llvm::Type::getVoidTy(context),
          {pointer, i32, i32, i32, pointer, i64, i64, i32},
          false);
  auto* logic9_projected_element_type = llvm::StructType::create(
      context,
      {logic9_word_type, i64},
      "fsim_jit_logic9_projected_element_v1");
  auto* write_projected_waveform_logic9_type =
      llvm::FunctionType::get(
          llvm::Type::getVoidTy(context),
          {pointer, i32, i32, pointer, i32, i64, i32},
          false);
  auto* write_projected_waveform_slice_logic9_type =
      llvm::FunctionType::get(
          llvm::Type::getVoidTy(context),
          {pointer, i32, i32, i32, pointer, i32, i64, i32},
          false);
  auto* formatted_output_logic9_type =
      llvm::FunctionType::get(
          llvm::Type::getVoidTy(context),
          {pointer, i32, i32, i32, pointer},
          false);
  auto *register_aval = builder.CreateLoad(
      pointer, builder.CreateStructGEP(frame_type, frame_argument, 8),
      "register.aval.base");
  auto *register_bval = builder.CreateLoad(
      pointer, builder.CreateStructGEP(frame_type, frame_argument, 9),
      "register.bval.base");
  auto *register_initialized = builder.CreateLoad(
      pointer, builder.CreateStructGEP(frame_type, frame_argument, 10),
      "register.initialized.base");
  auto* register_logic9_plane2 = builder.CreateLoad(
      pointer,
      builder.CreateStructGEP(frame_type, frame_argument, 11),
      "register.logic9.plane2.base");
  auto* register_logic9_plane3 = builder.CreateLoad(
      pointer,
      builder.CreateStructGEP(frame_type, frame_argument, 12),
      "register.logic9.plane3.base");
  auto *i8 = llvm::Type::getInt8Ty(context);
  std::vector<RegisterSlot> registers(process.register_count);
  for (std::size_t index = 0; index < process.register_count; ++index) {
    const auto width = validated.register_widths[index];
    if (width == 0) {
      continue;
    }
    registers[index] = {
        builder.CreateGEP(
            i64, register_aval, constant_i64(context, index),
            "register." + std::to_string(index) + ".aval"),
        builder.CreateGEP(
            i64, register_bval, constant_i64(context, index),
            "register." + std::to_string(index) + ".bval"),
        builder.CreateGEP(
            i8, register_initialized, constant_i64(context, index),
            "register." + std::to_string(index) + ".initialized"),
        width,
        builder.CreateGEP(
            i64,
            register_logic9_plane2,
            constant_i64(context, index),
            "register." + std::to_string(index)
                + ".logic9.plane2"),
        builder.CreateGEP(
            i64,
            register_logic9_plane3,
            constant_i64(context, index),
            "register." + std::to_string(index)
                + ".logic9.plane3"),
        process.register_value_kinds.empty()
            ? ValueKind::logic4
            : process.register_value_kinds[index],
    };
  }
  auto *read_bval_slot = builder.CreateAlloca(i64, nullptr, "read.bval");
  auto* logic9_word_slot =
      builder.CreateAlloca(logic9_word_type, nullptr, "logic9.word");
  const auto logic9_plane_pointer =
      [&](llvm::Value* storage,
          const std::uint32_t plane) -> llvm::Value* {
        return builder.CreateInBoundsGEP(
            logic9_word_type,
            storage,
            {
                llvm::ConstantInt::get(i32, 0),
                llvm::ConstantInt::get(i32, plane)});
      };
  const auto store_logic9_word =
      [&](llvm::Value* storage, EncodedValue value) {
        value = coerce_value_kind(
            builder, value, ValueKind::logic9);
        const std::array planes{
            value.aval,
            value.bval,
            value.logic9_plane2,
            value.logic9_plane3};
        for (std::uint32_t plane = 0; plane < 4; ++plane) {
          builder.CreateStore(
              planes[plane],
              logic9_plane_pointer(storage, plane));
        }
      };
  const auto load_logic9_word =
      [&](llvm::Value* storage,
          const std::uint32_t width) -> EncodedValue {
        return {
            builder.CreateLoad(
                i64, logic9_plane_pointer(storage, 0)),
            builder.CreateLoad(
                i64, logic9_plane_pointer(storage, 1)),
            width,
            builder.CreateLoad(
                i64, logic9_plane_pointer(storage, 2)),
            builder.CreateLoad(
                i64, logic9_plane_pointer(storage, 3)),
            ValueKind::logic9};
      };
  const auto return_result =
      [&](const std::uint32_t status, const std::uint32_t instruction,
          const std::uint64_t delay, const std::uint32_t frame_state,
          const std::uint32_t next_pc) {
        builder.CreateStore(
            llvm::ConstantInt::get(i32, next_pc),
            builder.CreateStructGEP(frame_type, frame_argument, 5));
        builder.CreateStore(
            llvm::ConstantInt::get(i32, frame_state),
            builder.CreateStructGEP(frame_type, frame_argument, 6));
        builder.CreateStore(
            llvm::ConstantInt::get(i32, instruction),
            builder.CreateStructGEP(frame_type, frame_argument, 7));
        builder.CreateStore(
            llvm::ConstantInt::get(i32, status),
            builder.CreateStructGEP(result_type, result_argument, 2));
        builder.CreateStore(
            llvm::ConstantInt::get(i32, instruction),
            builder.CreateStructGEP(result_type, result_argument, 3));
        builder.CreateStore(
            constant_i64(context, delay),
            builder.CreateStructGEP(result_type, result_argument, 4));
        builder.CreateRet(llvm::ConstantInt::get(i32, status));
      };
  std::vector<llvm::BasicBlock *> instruction_blocks;
  instruction_blocks.reserve(process.operations.size());
  for (std::size_t index = 0; index < process.operations.size(); ++index) {
    instruction_blocks.push_back(llvm::BasicBlock::Create(
        context, "instruction." + std::to_string(index), function));
  }
  auto *invalid_pc =
      llvm::BasicBlock::Create(context, "invalid.pc", function);
  auto *program_counter = builder.CreateLoad(
      i32, builder.CreateStructGEP(frame_type, frame_argument, 5),
      "program.counter");
  auto *dispatch = builder.CreateSwitch(
      program_counter, invalid_pc,
      static_cast<unsigned>(process.operations.size()));
  for (std::size_t index = 0; index < instruction_blocks.size(); ++index) {
    dispatch->addCase(
        llvm::ConstantInt::get(i32, static_cast<std::uint32_t>(index)),
        instruction_blocks[index]);
  }

  builder.SetInsertPoint(invalid_pc);
  return_result(
      FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR, FSIM_JIT_INVALID_INSTRUCTION, 0,
      FSIM_JIT_FRAME_STATE_RUNTIME_ERROR, FSIM_JIT_INVALID_INSTRUCTION);

  for (std::size_t index = 0; index < process.operations.size(); ++index) {
    const auto instruction = static_cast<InstructionIndex>(index);
    const auto next_instruction =
        static_cast<InstructionIndex>(index + 1U);
    builder.SetInsertPoint(instruction_blocks[index]);
    const auto branch_to_next = [&] {
      builder.CreateBr(instruction_blocks[index + 1]);
    };
    const auto runtime_error_if =
        [&](llvm::Value* condition,
            const JitGeneratedRuntimeErrorReason reason,
            const std::string_view label) {
          auto* error_block = llvm::BasicBlock::Create(
              context,
              std::string{label} + ".error."
                  + std::to_string(index),
              function);
          auto* continue_block = llvm::BasicBlock::Create(
              context,
              std::string{label} + ".continue."
                  + std::to_string(index),
              function);
          builder.CreateCondBr(
              condition, error_block, continue_block);
          builder.SetInsertPoint(error_block);
          return_result(
              FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR,
              instruction,
              static_cast<std::uint64_t>(reason),
              FSIM_JIT_FRAME_STATE_RUNTIME_ERROR,
              static_cast<std::uint32_t>(reason));
          builder.SetInsertPoint(continue_block);
        };
    const auto dynamic_offset =
        [&](const DynamicIndex& selection) -> llvm::Value* {
          const auto selected = coerce_value_kind(
              builder,
              load_register(
                  builder, registers, selection.index),
              ValueKind::logic4);
          runtime_error_if(
              builder.CreateICmpNE(
                  builder.CreateAnd(
                      selected.bval,
                      constant_i64(
                          context,
                          std::numeric_limits<std::uint32_t>::max())),
                  constant_i64(context, 0)),
              JitGeneratedRuntimeErrorReason::dynamic_index_unknown,
              "dynamic.index.unknown");
          auto* signed_index = builder.CreateSExt(
              builder.CreateTrunc(selected.aval, i32), i64);
          auto* left = llvm::ConstantInt::getSigned(
              i64, selection.left);
          auto* right = llvm::ConstantInt::getSigned(
              i64, selection.right);
          auto* lower =
              selection.left <= selection.right ? left : right;
          auto* upper =
              selection.left <= selection.right ? right : left;
          runtime_error_if(
              builder.CreateOr(
                  builder.CreateICmpSLT(signed_index, lower),
                  builder.CreateICmpSGT(signed_index, upper)),
              JitGeneratedRuntimeErrorReason::dynamic_index_range,
              "dynamic.index.range");
          auto* distance = builder.CreateSelect(
              builder.CreateICmpSGE(signed_index, right),
              builder.CreateSub(signed_index, right),
              builder.CreateSub(right, signed_index));
          return builder.CreateAdd(
              distance,
              constant_i64(context, selection.base_offset),
              "dynamic.index.offset");
        };
    const auto dynamic_offset_i32 =
        [&](const DynamicIndex& selection) {
          return builder.CreateTrunc(
              dynamic_offset(selection), i32);
        };
    const auto emit_dynamic_slice =
        [&](const std::uint32_t signal,
            const RegisterId source_register,
            llvm::Value* offset,
            llvm::Value* logic4_callback,
            llvm::Value* logic9_callback) {
          const auto signal_kind =
              signal_value_kinds.empty()
                  ? ValueKind::logic4
                  : signal_value_kinds[signal];
          const auto source = coerce_value_kind(
              builder,
              load_register(
                  builder, registers, source_register),
              signal_kind);
          if (signal_kind == ValueKind::logic9) {
            store_logic9_word(logic9_word_slot, source);
            builder.CreateCall(
                write_slice_logic9_type,
                logic9_callback,
                {
                    context_pointer,
                    llvm::ConstantInt::get(i32, signal),
                    offset,
                    llvm::ConstantInt::get(i32, source.width),
                    logic9_word_slot});
          } else {
            builder.CreateCall(
                write_slice_type,
                logic4_callback,
                {
                    context_pointer,
                    llvm::ConstantInt::get(i32, signal),
                    offset,
                    llvm::ConstantInt::get(i32, source.width),
                    source.aval,
                    source.bval});
          }
          branch_to_next();
        };
    const auto emit_dynamic_after_slice =
        [&](const WriteAfterDynamicSlice& operation,
            llvm::Value* offset) {
          const auto signal_kind =
              signal_value_kinds.empty()
                  ? ValueKind::logic4
                  : signal_value_kinds[operation.signal];
          const auto source = coerce_value_kind(
              builder,
              load_register(
                  builder, registers, operation.source),
              signal_kind);
          if (signal_kind == ValueKind::logic9) {
            store_logic9_word(logic9_word_slot, source);
            builder.CreateCall(
                write_after_slice_logic9_type,
                write_after_slice_logic9_callback,
                {
                    context_pointer,
                    llvm::ConstantInt::get(
                        i32, operation.signal),
                    offset,
                    llvm::ConstantInt::get(i32, source.width),
                    logic9_word_slot,
                    constant_i64(context, operation.delay)});
          } else {
            builder.CreateCall(
                write_after_slice_type,
                write_after_slice_callback,
                {
                    context_pointer,
                    llvm::ConstantInt::get(
                        i32, operation.signal),
                    offset,
                    llvm::ConstantInt::get(i32, source.width),
                    source.aval,
                    source.bval,
                    constant_i64(context, operation.delay)});
          }
          branch_to_next();
        };
    const auto emit_dynamic_inertial_slice =
        [&](const WriteInertialDynamicSlice& operation,
            llvm::Value* offset) {
          const auto signal_kind =
              signal_value_kinds.empty()
                  ? ValueKind::logic4
                  : signal_value_kinds[operation.signal];
          const auto source = coerce_value_kind(
              builder,
              load_register(
                  builder, registers, operation.source),
              signal_kind);
          if (signal_kind == ValueKind::logic9) {
            store_logic9_word(logic9_word_slot, source);
            builder.CreateCall(
                write_inertial_slice_logic9_type,
                write_inertial_slice_logic9_callback,
                {
                    context_pointer,
                    llvm::ConstantInt::get(
                        i32, operation.signal),
                    offset,
                    llvm::ConstantInt::get(i32, source.width),
                    logic9_word_slot,
                    constant_i64(
                        context, operation.delays.rise),
                    constant_i64(
                        context, operation.delays.fall),
                    constant_i64(
                        context, operation.delays.turnoff)});
          } else {
            builder.CreateCall(
                write_inertial_slice_type,
                write_inertial_slice_callback,
                {
                    context_pointer,
                    llvm::ConstantInt::get(
                        i32, operation.signal),
                    offset,
                    llvm::ConstantInt::get(i32, source.width),
                    source.aval,
                    source.bval,
                    constant_i64(
                        context, operation.delays.rise),
                    constant_i64(
                        context, operation.delays.fall),
                    constant_i64(
                        context, operation.delays.turnoff)});
          }
          branch_to_next();
        };
    const auto emit_dynamic_projected_slice =
        [&](const WriteProjectedDynamicSlice& operation,
            llvm::Value* offset) {
          const auto signal_kind =
              signal_value_kinds.empty()
                  ? ValueKind::logic4
                  : signal_value_kinds[operation.signal];
          const auto source = coerce_value_kind(
              builder,
              load_register(
                  builder, registers, operation.source),
              signal_kind);
          if (signal_kind == ValueKind::logic9) {
            store_logic9_word(logic9_word_slot, source);
            builder.CreateCall(
                write_projected_slice_logic9_type,
                write_projected_slice_logic9_callback,
                {
                    context_pointer,
                    llvm::ConstantInt::get(
                        i32, operation.signal),
                    offset,
                    llvm::ConstantInt::get(i32, source.width),
                    logic9_word_slot,
                    constant_i64(context, operation.delay),
                    constant_i64(
                        context, operation.rejection),
                    llvm::ConstantInt::get(
                        i32,
                        static_cast<std::uint32_t>(
                            operation.mode))});
          } else {
            builder.CreateCall(
                write_projected_slice_type,
                write_projected_slice_callback,
                {
                    context_pointer,
                    llvm::ConstantInt::get(
                        i32, operation.signal),
                    offset,
                    llvm::ConstantInt::get(i32, source.width),
                    source.aval,
                    source.bval,
                    constant_i64(context, operation.delay),
                    constant_i64(
                        context, operation.rejection),
                    llvm::ConstantInt::get(
                        i32,
                        static_cast<std::uint32_t>(
                            operation.mode))});
          }
          branch_to_next();
        };
    ValueOperationLowerer value_lowerer{
        builder,
        registers,
        context,
        i32,
        i64,
        branch_to_next,
        runtime_error_if,
        dynamic_offset};
    StringOperationLowerer string_lowerer{
        module, builder, registers, context, i32, i64,
        context_pointer, process.id, instruction,
        runtime_type, runtime_argument,
        runtime_error_if, branch_to_next};
    FileOperationLowerer file_lowerer{
        builder, registers, context, i32, i64, context_pointer, process.id,
        instruction, runtime_type, runtime_argument,
        runtime_error_if, branch_to_next};
    ContainerOperationLowerer container_lowerer{
        builder, registers, context, i32, i64, context_pointer,
        process.id, instruction, runtime_type, runtime_argument,
        runtime_error_if, branch_to_next};
    SignalOperationLowerer signal_lowerer{
        builder,
        registers,
        signal_widths,
        signal_value_kinds,
        context,
        i32,
        i64,
        context_pointer,
        read_callback,
        read_logic9_callback,
        write_projected_waveform_callback,
        write_projected_waveform_logic9_callback,
        write_projected_callback,
        write_projected_logic9_callback,
        write_inertial_callback,
        write_inertial_logic9_callback,
        signal_event_callback,
        signal_last_value_callback,
        signal_last_value_logic9_callback,
        signal_last_event_callback,
        signal_active_callback,
        read_type,
        read_logic9_type,
        write_projected_waveform_type,
        write_projected_waveform_logic9_type,
        write_projected_type,
        write_projected_logic9_type,
        write_inertial_type,
        write_inertial_logic9_type,
        signal_event_type,
        signal_last_value_type,
        signal_last_event_type,
        signal_active_type,
        projected_element_type,
        logic9_projected_element_type,
        read_bval_slot,
        logic9_word_slot,
        branch_to_next,
        store_logic9_word,
        load_logic9_word,
        runtime_error_if};
    OutputOperationLowerer output_lowerer{
        builder,
        context,
        i32,
        context_pointer,
        process.id,
        instruction,
        index,
        symbol,
        output_type,
        time_output_type,
        report_type,
        output_callback,
        postponed_output_callback,
        time_output_callback,
        monitor_install_callback,
        monitor_control_callback,
        report_callback,
        branch_to_next,
        return_result};
    ControlFlowOperationLowerer control_lowerer{
        builder,
        registers,
        context,
        i8,
        i32,
        i64,
        register_aval,
        register_bval,
        register_initialized,
        instruction_blocks,
        invalid_pc,
        function,
        instruction,
        index,
        runtime_error_if,
        return_result};
    std::visit(
        Overloaded{
            [&](const LoadConstant& operation) {
              signal_lowerer.lower(operation);
            },
            [&](const WriteProjectedWaveform& operation) {
              signal_lowerer.lower(operation);
            },
            [&](const WriteProjected& operation) {
              signal_lowerer.lower(operation);
            },
            [&](const WriteInertial& operation) {
              signal_lowerer.lower(operation);
            },
            [&](const ReadSignal& operation) {
              signal_lowerer.lower(operation);
            },
            [&](const SignalEvent& operation) {
              signal_lowerer.lower(operation);
            },
            [&](const SignalLastValue& operation) {
              signal_lowerer.lower(operation);
            },
            [&](const SignalLastEvent& operation) {
              signal_lowerer.lower(operation);
            },
            [&](const SignalActive& operation) {
              signal_lowerer.lower(operation);
            },
            [&](const CopyRegister& operation) {
              value_lowerer.lower(operation);
            },
            [&](const UnaryNot& operation) {
              value_lowerer.lower(operation);
            },
            [&](const LogicalNot& operation) {
              value_lowerer.lower(operation);
            },
            [&](const LogicalBinary& operation) {
              value_lowerer.lower(operation);
            },
            [&](const Reduction& operation) {
              value_lowerer.lower(operation);
            },
            [&](const CountOnes& operation) {
              value_lowerer.lower(operation);
            },
            [&](const CountBits& operation) {
              value_lowerer.lower(operation);
            },
            [&](const Shift& operation) {
              value_lowerer.lower(operation);
            },
            [&](const Extract& operation) {
              value_lowerer.lower(operation);
            },
            [&](const DynamicExtract& operation) {
              value_lowerer.lower(operation);
            },
            [&](const DynamicPartSelect& operation) {
              value_lowerer.lower(operation); },
            [&](const Insert& operation) {
              value_lowerer.lower(operation);
            },
            [&](const DynamicInsert& operation) {
              value_lowerer.lower(operation);
            },
            [&](const Concatenate& operation) {
              value_lowerer.lower(operation);
            },
            [&](const Binary& operation) {
              value_lowerer.lower(operation);
            },
            [&](const IntegerUnary& operation) {
              value_lowerer.lower(operation);
            },
            [&](const IntegerBinary& operation) {
              value_lowerer.lower(operation);
            },
            [&](const IntegerCheck& operation) {
              value_lowerer.lower(operation);
            },
            [&](const ConditionalSelect& operation) {
              value_lowerer.lower(operation);
            },
            [&](const WriteBlocking &operation) {
              const auto signal_kind =
                  signal_value_kinds.empty()
                      ? ValueKind::logic4
                      : signal_value_kinds[operation.signal];
              const auto source = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.source),
                  signal_kind);
              if (signal_kind == ValueKind::logic9) {
                store_logic9_word(logic9_word_slot, source);
                builder.CreateCall(
                    write_logic9_type,
                    write_logic9_callback,
                    {
                        context_pointer,
                        llvm::ConstantInt::get(
                            i32, operation.signal),
                        logic9_word_slot});
                branch_to_next();
                return;
              }
              builder.CreateCall(
                  write_type, write_callback,
                  {context_pointer,
                   llvm::ConstantInt::get(i32, operation.signal), source.aval,
                   source.bval});
              branch_to_next();
            },
            [&](const WriteUpdate &operation) {
              const auto signal_kind =
                  signal_value_kinds.empty()
                      ? ValueKind::logic4
                      : signal_value_kinds[operation.signal];
              const auto source = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.source),
                  signal_kind);
              if (signal_kind == ValueKind::logic9) {
                store_logic9_word(logic9_word_slot, source);
                builder.CreateCall(
                    write_logic9_type,
                    write_update_logic9_callback,
                    {
                        context_pointer,
                        llvm::ConstantInt::get(
                            i32, operation.signal),
                        logic9_word_slot});
                branch_to_next();
                return;
              }
              builder.CreateCall(
                  write_type, write_update_callback,
                  {context_pointer,
                   llvm::ConstantInt::get(i32, operation.signal), source.aval,
                   source.bval});
              branch_to_next();
            },
            [&](const WriteAfter &operation) {
              const auto signal_kind =
                  signal_value_kinds.empty()
                      ? ValueKind::logic4
                      : signal_value_kinds[operation.signal];
              const auto source = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.source),
                  signal_kind);
              if (signal_kind == ValueKind::logic9) {
                store_logic9_word(logic9_word_slot, source);
                builder.CreateCall(
                    write_after_logic9_type,
                    write_after_logic9_callback,
                    {
                        context_pointer,
                        llvm::ConstantInt::get(
                            i32, operation.signal),
                        logic9_word_slot,
                        constant_i64(
                            context, operation.delay)});
                branch_to_next();
                return;
              }
              builder.CreateCall(
                  write_after_type, write_after_callback,
                  {context_pointer,
                   llvm::ConstantInt::get(i32, operation.signal), source.aval,
                   source.bval, constant_i64(context, operation.delay)});
              branch_to_next();
            },
            [&](const WriteBlockingSlice& operation) {
              const auto signal_kind =
                  signal_value_kinds.empty()
                      ? ValueKind::logic4
                      : signal_value_kinds[operation.signal];
              const auto source = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.source),
                  signal_kind);
              if (signal_kind == ValueKind::logic9) {
                store_logic9_word(logic9_word_slot, source);
                builder.CreateCall(
                    write_slice_logic9_type,
                    write_blocking_slice_logic9_callback,
                    {
                        context_pointer,
                        llvm::ConstantInt::get(
                            i32, operation.signal),
                        llvm::ConstantInt::get(
                            i32, operation.offset),
                        llvm::ConstantInt::get(i32, source.width),
                        logic9_word_slot});
                branch_to_next();
                return;
              }
              builder.CreateCall(
                  write_slice_type,
                  write_blocking_slice_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(
                          i32, operation.signal),
                      llvm::ConstantInt::get(
                          i32, operation.offset),
                      llvm::ConstantInt::get(
                          i32, source.width),
                      source.aval,
                      source.bval});
              branch_to_next();
            },
            [&](const WriteUpdateSlice& operation) {
              const auto signal_kind =
                  signal_value_kinds.empty()
                      ? ValueKind::logic4
                      : signal_value_kinds[operation.signal];
              const auto source = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.source),
                  signal_kind);
              if (signal_kind == ValueKind::logic9) {
                store_logic9_word(logic9_word_slot, source);
                builder.CreateCall(
                    write_slice_logic9_type,
                    write_update_slice_logic9_callback,
                    {
                        context_pointer,
                        llvm::ConstantInt::get(
                            i32, operation.signal),
                        llvm::ConstantInt::get(
                            i32, operation.offset),
                        llvm::ConstantInt::get(i32, source.width),
                        logic9_word_slot});
                branch_to_next();
                return;
              }
              builder.CreateCall(
                  write_slice_type,
                  write_update_slice_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(
                          i32, operation.signal),
                      llvm::ConstantInt::get(
                          i32, operation.offset),
                      llvm::ConstantInt::get(
                          i32, source.width),
                      source.aval,
                      source.bval});
              branch_to_next();
            },
            [&](const WriteAfterSlice& operation) {
              const auto signal_kind =
                  signal_value_kinds.empty()
                      ? ValueKind::logic4
                      : signal_value_kinds[operation.signal];
              const auto source = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.source),
                  signal_kind);
              if (signal_kind == ValueKind::logic9) {
                store_logic9_word(logic9_word_slot, source);
                builder.CreateCall(
                    write_after_slice_logic9_type,
                    write_after_slice_logic9_callback,
                    {
                        context_pointer,
                        llvm::ConstantInt::get(
                            i32, operation.signal),
                        llvm::ConstantInt::get(
                            i32, operation.offset),
                        llvm::ConstantInt::get(i32, source.width),
                        logic9_word_slot,
                        constant_i64(
                            context, operation.delay)});
                branch_to_next();
                return;
              }
              builder.CreateCall(
                  write_after_slice_type,
                  write_after_slice_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(
                          i32, operation.signal),
                      llvm::ConstantInt::get(
                          i32, operation.offset),
                      llvm::ConstantInt::get(
                          i32, source.width),
                      source.aval,
                      source.bval,
                      constant_i64(
                          context, operation.delay)});
              branch_to_next();
            },
            [&](const WriteInertialSlice& operation) {
              const auto signal_kind =
                  signal_value_kinds.empty()
                      ? ValueKind::logic4
                      : signal_value_kinds[operation.signal];
              const auto source = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.source),
                  signal_kind);
              if (signal_kind == ValueKind::logic9) {
                store_logic9_word(logic9_word_slot, source);
                builder.CreateCall(
                    write_inertial_slice_logic9_type,
                    write_inertial_slice_logic9_callback,
                    {
                        context_pointer,
                        llvm::ConstantInt::get(
                            i32, operation.signal),
                        llvm::ConstantInt::get(
                            i32, operation.offset),
                        llvm::ConstantInt::get(i32, source.width),
                        logic9_word_slot,
                        constant_i64(
                            context, operation.delays.rise),
                        constant_i64(
                            context, operation.delays.fall),
                        constant_i64(
                            context, operation.delays.turnoff)});
                branch_to_next();
                return;
              }
              builder.CreateCall(
                  write_inertial_slice_type,
                  write_inertial_slice_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(
                          i32, operation.signal),
                      llvm::ConstantInt::get(
                          i32, operation.offset),
                      llvm::ConstantInt::get(
                          i32, source.width),
                      source.aval,
                      source.bval,
                      constant_i64(context, operation.delays.rise),
                      constant_i64(context, operation.delays.fall),
                      constant_i64(
                          context, operation.delays.turnoff)});
              branch_to_next();
            },
            [&](const WriteProjectedSlice& operation) {
              const auto signal_kind =
                  signal_value_kinds.empty()
                      ? ValueKind::logic4
                      : signal_value_kinds[operation.signal];
              const auto source = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.source),
                  signal_kind);
              if (signal_kind == ValueKind::logic9) {
                store_logic9_word(logic9_word_slot, source);
                builder.CreateCall(
                    write_projected_slice_logic9_type,
                    write_projected_slice_logic9_callback,
                    {
                        context_pointer,
                        llvm::ConstantInt::get(
                            i32, operation.signal),
                        llvm::ConstantInt::get(
                            i32, operation.offset),
                        llvm::ConstantInt::get(i32, source.width),
                        logic9_word_slot,
                        constant_i64(
                            context, operation.delay),
                        constant_i64(
                            context, operation.rejection),
                        llvm::ConstantInt::get(
                            i32,
                            static_cast<std::uint32_t>(
                                operation.mode))});
                branch_to_next();
                return;
              }
              builder.CreateCall(
                  write_projected_slice_type,
                  write_projected_slice_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(
                          i32, operation.signal),
                      llvm::ConstantInt::get(
                          i32, operation.offset),
                      llvm::ConstantInt::get(
                          i32, source.width),
                      source.aval,
                      source.bval,
                      constant_i64(context, operation.delay),
                      constant_i64(context, operation.rejection),
                      llvm::ConstantInt::get(
                          i32,
                          static_cast<std::uint32_t>(
                              operation.mode))});
              branch_to_next();
            },
            [&](const WriteProjectedWaveformSlice& operation) {
              const auto signal_kind =
                  signal_value_kinds.empty()
                      ? ValueKind::logic4
                      : signal_value_kinds[operation.signal];
              if (signal_kind == ValueKind::logic9) {
                auto* array_type = llvm::ArrayType::get(
                    logic9_projected_element_type,
                    operation.elements.size());
                auto* storage = builder.CreateAlloca(
                    array_type,
                    nullptr,
                    "projected.logic9.slice.waveform");
                for (std::size_t element_index = 0;
                     element_index < operation.elements.size();
                     ++element_index) {
                  const auto& element =
                      operation.elements[element_index];
                  auto source = coerce_value_kind(
                      builder,
                      load_register(
                          builder,
                          registers,
                          element.source),
                      ValueKind::logic9);
                  auto* slot = builder.CreateInBoundsGEP(
                      array_type,
                      storage,
                      {
                          llvm::ConstantInt::get(i32, 0),
                          llvm::ConstantInt::get(
                              i32,
                              static_cast<std::uint32_t>(
                                  element_index))});
                  store_logic9_word(
                      builder.CreateStructGEP(
                          logic9_projected_element_type,
                          slot,
                          0),
                      source);
                  builder.CreateStore(
                      constant_i64(context, element.delay),
                      builder.CreateStructGEP(
                          logic9_projected_element_type,
                          slot,
                          1));
                }
                const auto first = load_register(
                    builder,
                    registers,
                    operation.elements.front().source);
                builder.CreateCall(
                    write_projected_waveform_slice_logic9_type,
                    write_projected_waveform_slice_logic9_callback,
                    {
                        context_pointer,
                        llvm::ConstantInt::get(
                            i32, operation.signal),
                        llvm::ConstantInt::get(
                            i32, operation.offset),
                        llvm::ConstantInt::get(i32, first.width),
                        storage,
                        llvm::ConstantInt::get(
                            i32,
                            static_cast<std::uint32_t>(
                                operation.elements.size())),
                        constant_i64(
                            context, operation.rejection),
                        llvm::ConstantInt::get(
                            i32,
                            static_cast<std::uint32_t>(
                                operation.mode))});
                branch_to_next();
                return;
              }
              auto* array_type = llvm::ArrayType::get(
                  projected_element_type, operation.elements.size());
              auto* storage = builder.CreateAlloca(
                  array_type, nullptr, "projected.slice.waveform");
              for (std::size_t element_index = 0;
                   element_index < operation.elements.size();
                   ++element_index) {
                const auto& element =
                    operation.elements[element_index];
                const auto source =
                    load_register(builder, registers, element.source);
                auto* slot = builder.CreateInBoundsGEP(
                    array_type,
                    storage,
                    {llvm::ConstantInt::get(i32, 0),
                     llvm::ConstantInt::get(
                         i32,
                         static_cast<std::uint32_t>(element_index))});
                builder.CreateStore(
                    source.aval,
                    builder.CreateStructGEP(
                        projected_element_type, slot, 0));
                builder.CreateStore(
                    source.bval,
                    builder.CreateStructGEP(
                        projected_element_type, slot, 1));
                builder.CreateStore(
                    constant_i64(context, element.delay),
                    builder.CreateStructGEP(
                        projected_element_type, slot, 2));
              }
              const auto first = load_register(
                  builder, registers, operation.elements.front().source);
              builder.CreateCall(
                  write_projected_waveform_slice_type,
                  write_projected_waveform_slice_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(i32, operation.signal),
                      llvm::ConstantInt::get(i32, operation.offset),
                      llvm::ConstantInt::get(i32, first.width),
                      storage,
                      llvm::ConstantInt::get(
                          i32,
                          static_cast<std::uint32_t>(
                              operation.elements.size())),
                      constant_i64(context, operation.rejection),
                      llvm::ConstantInt::get(
                          i32,
                          static_cast<std::uint32_t>(
                              operation.mode))});
              branch_to_next();
            },
            [&](const WriteBlockingDynamicSlice& operation) {
              emit_dynamic_slice(
                  operation.signal,
                  operation.source,
                  dynamic_offset_i32(operation.selection),
                  write_blocking_slice_callback,
                  write_blocking_slice_logic9_callback);
            },
            [&](const WriteUpdateDynamicSlice& operation) {
              emit_dynamic_slice(
                  operation.signal,
                  operation.source,
                  dynamic_offset_i32(operation.selection),
                  write_update_slice_callback,
                  write_update_slice_logic9_callback);
            },
            [&](const WriteAfterDynamicSlice& operation) {
              emit_dynamic_after_slice(
                  operation,
                  dynamic_offset_i32(operation.selection));
            },
            [&](const WriteInertialDynamicSlice& operation) {
              emit_dynamic_inertial_slice(
                  operation,
                  dynamic_offset_i32(operation.selection));
            },
            [&](const WriteProjectedDynamicSlice& operation) {
              emit_dynamic_projected_slice(
                  operation,
                  dynamic_offset_i32(operation.selection));
            },
            [&](const WriteProjectedWaveformDynamicSlice& operation) {
              const auto signal_kind =
                  signal_value_kinds.empty()
                      ? ValueKind::logic4
                      : signal_value_kinds[operation.signal];
              auto* offset =
                  dynamic_offset_i32(operation.selection);
              if (signal_kind == ValueKind::logic9) {
                auto* array_type = llvm::ArrayType::get(
                    logic9_projected_element_type,
                    operation.elements.size());
                auto* storage = builder.CreateAlloca(
                    array_type,
                    nullptr,
                    "projected.logic9.dynamic.slice.waveform");
                for (std::size_t element_index = 0;
                     element_index < operation.elements.size();
                     ++element_index) {
                  const auto& element =
                      operation.elements[element_index];
                  auto source = coerce_value_kind(
                      builder,
                      load_register(
                          builder,
                          registers,
                          element.source),
                      ValueKind::logic9);
                  auto* slot = builder.CreateInBoundsGEP(
                      array_type,
                      storage,
                      {
                          llvm::ConstantInt::get(i32, 0),
                          llvm::ConstantInt::get(
                              i32,
                              static_cast<std::uint32_t>(
                                  element_index))});
                  store_logic9_word(
                      builder.CreateStructGEP(
                          logic9_projected_element_type,
                          slot,
                          0),
                      source);
                  builder.CreateStore(
                      constant_i64(context, element.delay),
                      builder.CreateStructGEP(
                          logic9_projected_element_type,
                          slot,
                          1));
                }
                const auto first = load_register(
                    builder,
                    registers,
                    operation.elements.front().source);
                builder.CreateCall(
                    write_projected_waveform_slice_logic9_type,
                    write_projected_waveform_slice_logic9_callback,
                    {
                        context_pointer,
                        llvm::ConstantInt::get(
                            i32, operation.signal),
                        offset,
                        llvm::ConstantInt::get(
                            i32, first.width),
                        storage,
                        llvm::ConstantInt::get(
                            i32,
                            static_cast<std::uint32_t>(
                                operation.elements.size())),
                        constant_i64(
                            context, operation.rejection),
                        llvm::ConstantInt::get(
                            i32,
                            static_cast<std::uint32_t>(
                                operation.mode))});
                branch_to_next();
                return;
              }
              auto* array_type = llvm::ArrayType::get(
                  projected_element_type,
                  operation.elements.size());
              auto* storage = builder.CreateAlloca(
                  array_type,
                  nullptr,
                  "projected.dynamic.slice.waveform");
              for (std::size_t element_index = 0;
                   element_index < operation.elements.size();
                   ++element_index) {
                const auto& element =
                    operation.elements[element_index];
                const auto source = load_register(
                    builder, registers, element.source);
                auto* slot = builder.CreateInBoundsGEP(
                    array_type,
                    storage,
                    {
                        llvm::ConstantInt::get(i32, 0),
                        llvm::ConstantInt::get(
                            i32,
                            static_cast<std::uint32_t>(
                                element_index))});
                builder.CreateStore(
                    source.aval,
                    builder.CreateStructGEP(
                        projected_element_type, slot, 0));
                builder.CreateStore(
                    source.bval,
                    builder.CreateStructGEP(
                        projected_element_type, slot, 1));
                builder.CreateStore(
                    constant_i64(context, element.delay),
                    builder.CreateStructGEP(
                        projected_element_type, slot, 2));
              }
              const auto first = load_register(
                  builder,
                  registers,
                  operation.elements.front().source);
              builder.CreateCall(
                  write_projected_waveform_slice_type,
                  write_projected_waveform_slice_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(
                          i32, operation.signal),
                      offset,
                      llvm::ConstantInt::get(i32, first.width),
                      storage,
                      llvm::ConstantInt::get(
                          i32,
                          static_cast<std::uint32_t>(
                              operation.elements.size())),
                      constant_i64(
                          context, operation.rejection),
                      llvm::ConstantInt::get(
                          i32,
                          static_cast<std::uint32_t>(
                              operation.mode))});
              branch_to_next();
            },
            [&](const Assert &operation) {
              const auto condition =
                  load_register(builder, registers, operation.condition);
              auto *known = builder.CreateICmpEQ(
                  condition.bval, constant_i64(context, 0));
              auto *one = builder.CreateICmpEQ(condition.aval,
                                               constant_i64(context, 1));
              auto *passed = builder.CreateAnd(known, one);
              auto *failed_block =
                  llvm::BasicBlock::Create(
                      context, "assert.failed." + std::to_string(index),
                      function);
              builder.CreateCondBr(
                  passed, instruction_blocks[index + 1], failed_block);

              builder.SetInsertPoint(failed_block);
              if (operation.severity
                  == runtime::simir::AssertionSeverity::failure) {
                const auto &message =
                    operation.message.empty()
                        ? std::string{"assertion failed"}
                        : operation.message;
                auto *message_pointer = builder.CreateGlobalString(
                    message,
                    symbol + ".assert." + std::to_string(index));
                builder.CreateCall(
                    assert_type,
                    assert_callback,
                    {
                        context_pointer,
                        llvm::ConstantInt::get(i32, process.id),
                        llvm::ConstantInt::get(i32, instruction),
                        message_pointer,
                        constant_i64(context, message.size()),
                    });
                return_result(
                    FSIM_JIT_RESUME_STATUS_ASSERTION_FAILED,
                    instruction,
                    0,
                    FSIM_JIT_FRAME_STATE_ASSERTION_FAILED,
                    instruction);
              } else {
                builder.CreateCall(
                    report_type,
                    report_callback,
                    {
                        context_pointer,
                        llvm::ConstantInt::get(i32, process.id),
                        llvm::ConstantInt::get(i32, instruction),
                    });
                builder.CreateBr(
                    instruction_blocks[index + 1]);
              }
            },
            [&](const DebugPoint&) {
              if (debug_instrumentation) {
                return_result(
                    FSIM_JIT_RESUME_STATUS_DEBUG_POINT, instruction, 0,
                    FSIM_JIT_FRAME_STATE_READY, next_instruction);
              } else {
                auto* enabled = builder.CreateICmpNE(
                    builder.CreateAnd(
                        runtime_flags,
                        llvm::ConstantInt::get(
                            i32, FSIM_JIT_RUNTIME_FLAG_DEBUG_POINTS)),
                    llvm::ConstantInt::get(i32, 0));
                auto* enabled_block = llvm::BasicBlock::Create(
                    context,
                    "debug.enabled." + std::to_string(index),
                    function);
                builder.CreateCondBr(
                    enabled, enabled_block,
                    instruction_blocks[index + 1]);
                builder.SetInsertPoint(enabled_block);
                return_result(
                    FSIM_JIT_RESUME_STATUS_DEBUG_POINT, instruction, 0,
                    FSIM_JIT_FRAME_STATE_READY, next_instruction);
              }
            },
            [&](const Display& operation) {
              output_lowerer.lower(operation);
            },
            [&](const FormatDisplay& operation) {
              const auto value =
                  load_register(builder, registers, operation.source);
              if (value.kind == ValueKind::logic9) {
                store_logic9_word(logic9_word_slot, value);
                builder.CreateCall(
                    formatted_output_logic9_type,
                    write_formatted_logic9_callback,
                    {
                        context_pointer,
                        llvm::ConstantInt::get(i32, process.id),
                        llvm::ConstantInt::get(i32, instruction),
                        llvm::ConstantInt::get(i32, value.width),
                        logic9_word_slot});
                branch_to_next();
                return;
              }
              builder.CreateCall(
                  formatted_output_type,
                  formatted_output_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(i32, process.id),
                      llvm::ConstantInt::get(i32, instruction),
                      llvm::ConstantInt::get(i32, value.width),
                      value.aval,
                      value.bval,
                  });
              branch_to_next();
            },
            [&](const TimeDisplay& operation) {
              output_lowerer.lower(operation);
            },
            [&](const MonitorInstall& operation) {
              output_lowerer.lower(operation);
            },
            [&](const MonitorControl& operation) {
              output_lowerer.lower(operation);
            },
            [&](const RandomValue& operation) {
              const auto zero = constant_i64(context, 0);
              const auto maximum =
                  operation.maximum
                      ? load_register(
                            builder, registers, *operation.maximum)
                      : EncodedValue{zero, zero, 32};
              const auto minimum =
                  operation.minimum
                      ? load_register(
                            builder, registers, *operation.minimum)
                      : EncodedValue{zero, zero, 32};
              builder.CreateStore(zero, read_bval_slot);
              auto* aval = builder.CreateCall(
                  random_value_type,
                  random_value_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(i32, process.id),
                      llvm::ConstantInt::get(i32, instruction),
                      maximum.aval,
                      maximum.bval,
                      minimum.aval,
                      minimum.bval,
                      read_bval_slot,
                  });
              auto* bval = builder.CreateLoad(
                  i64, read_bval_slot, "random.bval");
              store_register(
                  builder,
                  registers,
                  operation.destination,
                  EncodedValue{aval, bval, 32});
              branch_to_next();
            },
            [&](const Report& operation) {
              output_lowerer.lower(operation);
            },
            [&](const Jump &operation) {
              control_lowerer.lower(operation);
            },
            [&](const Call& operation) {
              control_lowerer.lower(operation);
            },
            [&](const Return& operation) {
              control_lowerer.lower(operation);
            },
            [&](const Branch &operation) {
              control_lowerer.lower(operation);
            },
            [&](const WaitFor &operation) {
              output_lowerer.lower(operation);
            },
            [&](const WaitOn &operation) {
              output_lowerer.lower(operation);
            },
            [&](const WaitSensitivity &) {
              return_result(
                  FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY, instruction, 0,
                  FSIM_JIT_FRAME_STATE_READY, next_instruction);
            },
            [&](const WaitForever &) {
              return_result(
                  FSIM_JIT_RESUME_STATUS_WAIT_FOREVER, instruction, 0,
                  FSIM_JIT_FRAME_STATE_READY, next_instruction);
            },
            [&](const Yield &) {
              return_result(
                  FSIM_JIT_RESUME_STATUS_YIELDED, instruction, 0,
                  FSIM_JIT_FRAME_STATE_READY, next_instruction);
            },
            [&](const Pause &) {
              return_result(
                  FSIM_JIT_RESUME_STATUS_PAUSED, instruction, 0,
                  FSIM_JIT_FRAME_STATE_READY, next_instruction);
            },
            [&](const Stop &) {
              return_result(
                  FSIM_JIT_RESUME_STATUS_STOPPED, instruction, 0,
                  FSIM_JIT_FRAME_STATE_STOPPED, next_instruction);
            },
            [&](const Halt &) {
              return_result(
                  FSIM_JIT_RESUME_STATUS_COMPLETED, instruction, 0,
                  FSIM_JIT_FRAME_STATE_COMPLETED, next_instruction);
            },
            [&](const auto& operation) {
              if constexpr (
                  requires { file_lowerer.lower(operation); }) {
                file_lowerer.lower(operation);
              } else if constexpr (
                  requires { string_lowerer.lower(operation); }) {
                string_lowerer.lower(operation);
              } else if constexpr (
                  requires { container_lowerer.lower(operation); }) {
                container_lowerer.lower(operation);
              } else {
                llvm_unreachable(
                    "unsupported operations were rejected before lowering");
              }
            }},
        process.operations[index]);
  }
}
}  // namespace fsim::compiler::llvm_detail
