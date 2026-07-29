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
using runtime::simir::Concatenate;
using runtime::simir::ConditionalSelect;
using runtime::simir::CountOnes;
using runtime::simir::CountBits;
using runtime::simir::CopyRegister;
using runtime::simir::DebugPoint;
using runtime::simir::Display;
using runtime::simir::DynamicExtract;
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
       pointer, pointer},
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
    std::visit(
        Overloaded{
            [&](const LoadConstant &operation) {
              EncodedValue value{};
              if (operation.value.is_logic9()) {
                const auto word = operation.value.logic9_low_word();
                value = {
                    constant_i64(context, word.planes[0]),
                    constant_i64(context, word.planes[1]),
                    static_cast<std::uint32_t>(word.width),
                    constant_i64(context, word.planes[2]),
                    constant_i64(context, word.planes[3]),
                    ValueKind::logic9};
              } else {
                const auto word = operation.value.low_word();
                value = {
                    constant_i64(context, word.aval),
                    constant_i64(context, word.bval),
                    static_cast<std::uint32_t>(word.width)};
              }
              store_register(
                  builder, registers, operation.destination,
                  value);
              branch_to_next();
            },
            [&](const WriteProjectedWaveform& operation) {
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
                    "projected.logic9.waveform");
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
                    write_projected_waveform_logic9_type,
                    write_projected_waveform_logic9_callback,
                    {
                        context_pointer,
                        llvm::ConstantInt::get(
                            i32, operation.signal),
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
                  array_type, nullptr, "projected.waveform");
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
                  write_projected_waveform_type,
                  write_projected_waveform_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(i32, operation.signal),
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
            [&](const WriteProjected& operation) {
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
                    write_projected_logic9_type,
                    write_projected_logic9_callback,
                    {
                        context_pointer,
                        llvm::ConstantInt::get(
                            i32, operation.signal),
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
                  write_projected_type,
                  write_projected_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(i32, operation.signal),
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
            [&](const WriteInertial& operation) {
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
                    write_inertial_logic9_type,
                    write_inertial_logic9_callback,
                    {
                        context_pointer,
                        llvm::ConstantInt::get(
                            i32, operation.signal),
                        logic9_word_slot,
                        constant_i64(
                            context, operation.delays.rise),
                        constant_i64(
                            context, operation.delays.fall),
                        constant_i64(
                            context,
                            operation.delays.turnoff)});
                branch_to_next();
                return;
              }
              builder.CreateCall(
                  write_inertial_type,
                  write_inertial_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(i32, operation.signal),
                      source.aval,
                      source.bval,
                      constant_i64(context, operation.delays.rise),
                      constant_i64(context, operation.delays.fall),
                      constant_i64(
                          context, operation.delays.turnoff)});
              branch_to_next();
            },
            [&](const ReadSignal &operation) {
              const auto width = signal_widths[operation.signal];
              const auto signal_kind =
                  signal_value_kinds.empty()
                      ? ValueKind::logic4
                      : signal_value_kinds[operation.signal];
              if (signal_kind == ValueKind::logic9) {
                builder.CreateCall(
                    read_logic9_type,
                    read_logic9_callback,
                    {
                        context_pointer,
                        llvm::ConstantInt::get(
                            i32, operation.signal),
                        logic9_word_slot});
                auto value =
                    load_logic9_word(logic9_word_slot, width);
                auto* mask =
                    constant_i64(context, width_mask(width));
                value.aval = builder.CreateAnd(value.aval, mask);
                value.bval = builder.CreateAnd(value.bval, mask);
                value.logic9_plane2 =
                    builder.CreateAnd(value.logic9_plane2, mask);
                value.logic9_plane3 =
                    builder.CreateAnd(value.logic9_plane3, mask);
                store_register(
                    builder,
                    registers,
                    operation.destination,
                    value);
                branch_to_next();
                return;
              }
              builder.CreateStore(constant_i64(context, 0), read_bval_slot);
              auto *aval = builder.CreateCall(
                  read_type, read_callback,
                  {context_pointer, llvm::ConstantInt::get(i32, operation.signal),
                   read_bval_slot},
                  "aval");
              auto *bval =
                  builder.CreateLoad(i64, read_bval_slot, "bval.value");
              auto *mask = constant_i64(context, width_mask(width));
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateAnd(aval, mask),
                      builder.CreateAnd(bval, mask),
                      width});
              branch_to_next();
            },
            [&](const SignalEvent& operation) {
              auto* active = builder.CreateCall(
                  signal_event_type,
                  signal_event_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(
                          i32, operation.signal)},
                  "signal.event");
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateZExt(active, i64),
                      constant_i64(context, 0),
                      1});
              branch_to_next();
            },
            [&](const SignalLastValue& operation) {
              const auto width = signal_widths[operation.signal];
              const auto signal_kind =
                  signal_value_kinds.empty()
                      ? ValueKind::logic4
                      : signal_value_kinds[operation.signal];
              if (signal_kind == ValueKind::logic9) {
                builder.CreateCall(
                    read_logic9_type,
                    signal_last_value_logic9_callback,
                    {
                        context_pointer,
                        llvm::ConstantInt::get(
                            i32, operation.signal),
                        logic9_word_slot});
                store_register(
                    builder,
                    registers,
                    operation.destination,
                    load_logic9_word(logic9_word_slot, width));
                branch_to_next();
                return;
              }
              builder.CreateStore(
                  constant_i64(context, 0), read_bval_slot);
              auto* aval = builder.CreateCall(
                  signal_last_value_type,
                  signal_last_value_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(i32, operation.signal),
                      read_bval_slot},
                  "signal.last_value.aval");
              auto* bval = builder.CreateLoad(
                  i64, read_bval_slot, "signal.last_value.bval");
              auto* mask = constant_i64(context, width_mask(width));
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateAnd(aval, mask),
                      builder.CreateAnd(bval, mask),
                      width});
              branch_to_next();
            },
            [&](const SignalLastEvent& operation) {
              auto* elapsed = builder.CreateCall(
                  signal_last_event_type,
                  signal_last_event_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(i32, operation.signal)},
                  "signal.last_event");
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      elapsed,
                      constant_i64(context, 0),
                      64});
              branch_to_next();
            },
            [&](const SignalActive& operation) {
              auto* active = builder.CreateCall(
                  signal_active_type,
                  signal_active_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(i32, operation.signal)},
                  "signal.active");
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateZExt(active, i64),
                      constant_i64(context, 0),
                      1});
              branch_to_next();
            },
            [&](const CopyRegister& operation) {
              store_register(
                  builder, registers, operation.destination,
                  load_register(
                      builder, registers, operation.source));
              branch_to_next();
            },
            [&](const UnaryNot &operation) {
              const auto source =
                  load_register(builder, registers, operation.source);
              if (source.kind == ValueKind::logic9) {
                constexpr auto table = [] {
                  std::array<Logic9, 9> values{};
                  for (std::size_t state = 0;
                       state < values.size();
                       ++state) {
                    values[state] = runtime::logic_not(
                        static_cast<Logic9>(state));
                  }
                  return values;
                }();
                store_register(
                    builder,
                    registers,
                    operation.destination,
                    map_logic9_unary(builder, source, table));
                branch_to_next();
                return;
              }
              auto *mask = constant_i64(context, width_mask(source.width));
              auto *aval = builder.CreateAnd(
                  builder.CreateOr(builder.CreateNot(source.aval),
                                   source.bval),
                  mask);
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{aval, source.bval, source.width});
              branch_to_next();
            },
            [&](const LogicalNot& operation) {
              const auto source = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.source),
                  ValueKind::logic4);
              auto *mask =
                  constant_i64(context, width_mask(source.width));
              auto *known_ones = builder.CreateAnd(
                  builder.CreateAnd(source.aval, mask),
                  builder.CreateNot(source.bval));
              auto *has_one = builder.CreateICmpNE(
                  known_ones, constant_i64(context, 0));
              auto *has_unknown = builder.CreateICmpNE(
                  builder.CreateAnd(source.bval, mask),
                  constant_i64(context, 0));
              auto *not_true = builder.CreateNot(has_one);
              auto *unknown =
                  builder.CreateAnd(not_true, has_unknown);
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateZExt(
                          not_true,
                          llvm::Type::getInt64Ty(context)),
                      builder.CreateZExt(
                          unknown,
                          llvm::Type::getInt64Ty(context)),
                      1});
              branch_to_next();
            },
            [&](const LogicalBinary& operation) {
              const auto left = truth_bit(
                  builder,
                  coerce_value_kind(
                      builder,
                      load_register(
                          builder, registers, operation.lhs),
                      ValueKind::logic4));
              const auto right = truth_bit(
                  builder,
                  coerce_value_kind(
                      builder,
                      load_register(
                          builder, registers, operation.rhs),
                      ValueKind::logic4));
              const auto result =
                  operation.operation
                          == LogicalBinaryOperator::logical_and
                      ? bit_and(builder, left, right)
                      : bit_or(builder, left, right);
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateZExt(
                          result.aval,
                          llvm::Type::getInt64Ty(context)),
                      builder.CreateZExt(
                          result.bval,
                          llvm::Type::getInt64Ty(context)),
                      1});
              branch_to_next();
            },
            [&](const Reduction& operation) {
              const auto source = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.source),
                  ValueKind::logic4);
              if (operation.operation
                      == ReductionOperator::one_hot
                  || operation.operation
                      == ReductionOperator::one_hot_or_zero) {
                llvm::Value* seen_one =
                    llvm::ConstantInt::getFalse(context);
                llvm::Value* multiple_ones =
                    llvm::ConstantInt::getFalse(context);
                for (std::uint32_t bit = 0;
                     bit < source.width;
                     ++bit) {
                  const auto value =
                      bit_at(builder, source, bit);
                  auto* exact_one = builder.CreateAnd(
                      value.aval,
                      builder.CreateNot(value.bval));
                  multiple_ones = builder.CreateOr(
                      multiple_ones,
                      builder.CreateAnd(seen_one, exact_one));
                  seen_one =
                      builder.CreateOr(seen_one, exact_one);
                }
                auto* matched =
                    operation.operation
                            == ReductionOperator::one_hot
                        ? builder.CreateAnd(
                              seen_one,
                              builder.CreateNot(multiple_ones))
                        : builder.CreateNot(multiple_ones);
                store_register(
                    builder,
                    registers,
                    operation.destination,
                    EncodedValue{
                        builder.CreateZExt(
                            matched,
                            llvm::Type::getInt64Ty(context)),
                        llvm::ConstantInt::get(
                            llvm::Type::getInt64Ty(context), 0),
                        1});
                branch_to_next();
                return;
              }
              EncodedBit result{
                  operation.operation == ReductionOperator::bit_and
                      ? llvm::ConstantInt::getTrue(context)
                      : llvm::ConstantInt::getFalse(context),
                  llvm::ConstantInt::getFalse(context)};
              for (std::uint32_t bit = 0; bit < source.width; ++bit) {
                const auto value = bit_at(builder, source, bit);
                if (operation.operation
                    == ReductionOperator::bit_and) {
                  result = bit_and(builder, result, value);
                } else if (
                    operation.operation
                    == ReductionOperator::bit_or) {
                  result = bit_or(builder, result, value);
                } else {
                  result = bit_xor(builder, result, value);
                }
              }
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateZExt(
                          result.aval,
                          llvm::Type::getInt64Ty(context)),
                      builder.CreateZExt(
                          result.bval,
                          llvm::Type::getInt64Ty(context)),
                      1});
              branch_to_next();
            },
            [&](const CountOnes& operation) {
              const auto source = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.source),
                  ValueKind::logic4);
              llvm::Value* count = llvm::ConstantInt::get(
                  llvm::Type::getInt64Ty(context), 0);
              for (std::uint32_t bit = 0;
                   bit < source.width;
                   ++bit) {
                const auto value =
                    bit_at(builder, source, bit);
                auto* exact_one = builder.CreateAnd(
                    value.aval,
                    builder.CreateNot(value.bval));
                count = builder.CreateAdd(
                    count,
                    builder.CreateZExt(
                        exact_one,
                        llvm::Type::getInt64Ty(context)));
              }
              store_register(
                  builder,
                  registers,
                  operation.destination,
                  EncodedValue{
                      count,
                      llvm::ConstantInt::get(
                          llvm::Type::getInt64Ty(context), 0),
                      32});
              branch_to_next();
            },
            [&](const CountBits& operation) {
              const auto source = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.source),
                  ValueKind::logic4);
              llvm::Value* count = llvm::ConstantInt::get(
                  llvm::Type::getInt64Ty(context), 0);
              for (std::uint32_t bit = 0;
                   bit < source.width;
                   ++bit) {
                const auto value =
                    bit_at(builder, source, bit);
                auto* not_aval = builder.CreateNot(value.aval);
                auto* not_bval = builder.CreateNot(value.bval);
                llvm::Value* selected =
                    llvm::ConstantInt::getFalse(context);
                if ((operation.state_mask & 0x1U) != 0) {
                  selected = builder.CreateOr(
                      selected,
                      builder.CreateAnd(not_aval, not_bval));
                }
                if ((operation.state_mask & 0x2U) != 0) {
                  selected = builder.CreateOr(
                      selected,
                      builder.CreateAnd(value.aval, not_bval));
                }
                if ((operation.state_mask & 0x4U) != 0) {
                  selected = builder.CreateOr(
                      selected,
                      builder.CreateAnd(value.aval, value.bval));
                }
                if ((operation.state_mask & 0x8U) != 0) {
                  selected = builder.CreateOr(
                      selected,
                      builder.CreateAnd(not_aval, value.bval));
                }
                count = builder.CreateAdd(
                    count,
                    builder.CreateZExt(
                        selected,
                        llvm::Type::getInt64Ty(context)));
              }
              store_register(
                  builder,
                  registers,
                  operation.destination,
                  EncodedValue{
                      count,
                      llvm::ConstantInt::get(
                          llvm::Type::getInt64Ty(context), 0),
                      32});
              branch_to_next();
            },
            [&](const Shift& operation) {
              const auto value =
                  load_register(
                      builder, registers, operation.value);
              const auto amount = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.amount),
                  ValueKind::logic4);
              auto* value_mask =
                  constant_i64(context, width_mask(value.width));
              auto* amount_mask =
                  constant_i64(context, width_mask(amount.width));
              auto* amount_unknown = builder.CreateICmpNE(
                  builder.CreateAnd(amount.bval, amount_mask),
                  constant_i64(context, 0));
              auto* raw_amount_bits =
                  builder.CreateAnd(amount.aval, amount_mask);
              llvm::Value* amount_negative =
                  llvm::ConstantInt::getFalse(context);
              llvm::Value* amount_bits = raw_amount_bits;
              if (operation.signed_amount) {
                auto* sign_mask = constant_i64(
                    context,
                    std::uint64_t{1}
                        << (amount.width - 1U));
                amount_negative = builder.CreateICmpNE(
                    builder.CreateAnd(
                        raw_amount_bits, sign_mask),
                    constant_i64(context, 0));
                auto* magnitude = builder.CreateAnd(
                    builder.CreateSub(
                        constant_i64(context, 0),
                        raw_amount_bits),
                    amount_mask);
                amount_bits = builder.CreateSelect(
                    amount_negative,
                    magnitude,
                    raw_amount_bits);
              }
              auto* amount_too_large = builder.CreateICmpUGE(
                  amount_bits, constant_i64(context, value.width));
              const auto rotating =
                  operation.operation == ShiftOperator::rotate_left
                  || operation.operation == ShiftOperator::rotate_right;
              auto* safe_amount =
                  rotating
                      ? builder.CreateURem(
                            amount_bits,
                            constant_i64(context, value.width))
                      : builder.CreateSelect(
                            amount_too_large,
                            constant_i64(context, 0),
                            amount_bits);
              const auto shift_component =
                  [&](llvm::Value* component,
                      const ShiftOperator selected_operation,
                      const bool zero_plane)
                      -> llvm::Value* {
                    if (selected_operation
                            == ShiftOperator::rotate_left
                        || selected_operation
                            == ShiftOperator::rotate_right) {
                      auto* inverse_amount = builder.CreateURem(
                          builder.CreateSub(
                              constant_i64(context, value.width),
                              safe_amount),
                          constant_i64(context, value.width));
                      auto* left_amount =
                          selected_operation
                                  == ShiftOperator::rotate_left
                              ? safe_amount
                              : inverse_amount;
                      auto* right_amount =
                          selected_operation
                                  == ShiftOperator::rotate_left
                              ? inverse_amount
                              : safe_amount;
                      return builder.CreateOr(
                          builder.CreateShl(component, left_amount),
                          builder.CreateLShr(component, right_amount));
                    }
                    if (selected_operation
                            == ShiftOperator::logical_left
                        || selected_operation
                            == ShiftOperator::arithmetic_left) {
                      auto* shifted = builder.CreateShl(
                          component, safe_amount);
                      if (selected_operation
                          == ShiftOperator::arithmetic_left) {
                        auto* fill_mask = builder.CreateSub(
                            builder.CreateShl(
                                constant_i64(context, 1),
                                safe_amount),
                            constant_i64(context, 1));
                        auto* rightmost = builder.CreateAnd(
                            component, constant_i64(context, 1));
                        auto* fill = builder.CreateSelect(
                            builder.CreateICmpNE(
                                rightmost,
                                constant_i64(context, 0)),
                            fill_mask,
                            constant_i64(context, 0));
                        return builder.CreateOr(shifted, fill);
                      }
                      if (zero_plane) {
                        auto* fill_mask = builder.CreateSub(
                            builder.CreateShl(
                                constant_i64(context, 1),
                                safe_amount),
                            constant_i64(context, 1));
                        return builder.CreateOr(
                            shifted, fill_mask);
                      }
                      return shifted;
                    }
                    if (selected_operation
                        == ShiftOperator::logical_right) {
                      auto* shifted = builder.CreateLShr(
                          component, safe_amount);
                      if (zero_plane) {
                        auto* fill_mask = builder.CreateXor(
                            value_mask,
                            builder.CreateLShr(
                                value_mask, safe_amount));
                        return builder.CreateOr(
                            shifted, fill_mask);
                      }
                      return shifted;
                    }
                    const auto extension_shift =
                        64U - value.width;
                    auto* sign_extended = builder.CreateAShr(
                        builder.CreateShl(
                            component,
                            constant_i64(
                                context, extension_shift)),
                        constant_i64(
                            context, extension_shift));
                    return builder.CreateAShr(
                        sign_extended, safe_amount);
                  };
              const auto selected_shift_component =
                  [&](llvm::Value* component,
                      const bool zero_plane) -> llvm::Value* {
                    auto* positive = shift_component(
                        component, operation.operation, zero_plane);
                    if (!operation.signed_amount) {
                      return positive;
                    }
                    auto* negative = shift_component(
                        component,
                        reverse_shift(operation.operation),
                        zero_plane);
                    return builder.CreateSelect(
                        amount_negative, negative, positive);
                  };
              auto* shifted_aval =
                  selected_shift_component(value.aval, false);
              auto* shifted_bval =
                  selected_shift_component(
                      value.bval,
                      value.kind == ValueKind::logic9);
              auto* shifted_plane2 =
                  selected_shift_component(
                      value.logic9_plane2, false);
              auto* shifted_plane3 =
                  selected_shift_component(
                      value.logic9_plane3, false);
              const auto oversized_component =
                  [&](llvm::Value* component,
                      const ShiftOperator selected_operation,
                      const bool zero_plane)
                      -> llvm::Value* {
                    if (selected_operation
                        == ShiftOperator::arithmetic_right) {
                      const auto sign_offset =
                          value.width - 1U;
                      auto* sign = builder.CreateAnd(
                          builder.CreateLShr(
                              component,
                              constant_i64(
                                  context, sign_offset)),
                          constant_i64(context, 1));
                      return builder.CreateSelect(
                          builder.CreateICmpNE(
                              sign, constant_i64(context, 0)),
                          value_mask,
                          constant_i64(context, 0));
                    }
                    if (selected_operation
                        == ShiftOperator::arithmetic_left) {
                      auto* rightmost = builder.CreateAnd(
                          component, constant_i64(context, 1));
                      return builder.CreateSelect(
                          builder.CreateICmpNE(
                              rightmost,
                              constant_i64(context, 0)),
                          value_mask,
                          constant_i64(context, 0));
                    }
                    return constant_i64(
                        context, zero_plane ? width_mask(value.width) : 0);
                  };
              const auto selected_oversized_component =
                  [&](llvm::Value* component,
                      const bool zero_plane) -> llvm::Value* {
                    auto* positive = oversized_component(
                        component, operation.operation, zero_plane);
                    if (!operation.signed_amount) {
                      return positive;
                    }
                    auto* negative = oversized_component(
                        component,
                        reverse_shift(operation.operation),
                        zero_plane);
                    return builder.CreateSelect(
                        amount_negative, negative, positive);
                  };
              auto* oversized_aval =
                  selected_oversized_component(value.aval, false);
              auto* oversized_bval =
                  selected_oversized_component(
                      value.bval,
                      value.kind == ValueKind::logic9);
              auto* oversized_plane2 =
                  selected_oversized_component(
                      value.logic9_plane2, false);
              auto* oversized_plane3 =
                  selected_oversized_component(
                      value.logic9_plane3, false);
              auto* known_aval = builder.CreateSelect(
                  rotating
                      ? llvm::ConstantInt::getFalse(context)
                      : amount_too_large,
                  oversized_aval,
                  builder.CreateAnd(shifted_aval, value_mask));
              auto* known_bval = builder.CreateSelect(
                  rotating
                      ? llvm::ConstantInt::getFalse(context)
                      : amount_too_large,
                  oversized_bval,
                  builder.CreateAnd(shifted_bval, value_mask));
              auto* known_plane2 = builder.CreateSelect(
                  rotating
                      ? llvm::ConstantInt::getFalse(context)
                      : amount_too_large,
                  oversized_plane2,
                  builder.CreateAnd(shifted_plane2, value_mask));
              auto* known_plane3 = builder.CreateSelect(
                  rotating
                      ? llvm::ConstantInt::getFalse(context)
                      : amount_too_large,
                  oversized_plane3,
                  builder.CreateAnd(shifted_plane3, value_mask));
              if (value.kind == ValueKind::logic9) {
                store_register(
                    builder,
                    registers,
                    operation.destination,
                    EncodedValue{
                        builder.CreateSelect(
                            amount_unknown,
                            value_mask,
                            known_aval),
                        builder.CreateSelect(
                            amount_unknown,
                            constant_i64(context, 0),
                            known_bval),
                        value.width,
                        builder.CreateSelect(
                            amount_unknown,
                            constant_i64(context, 0),
                            known_plane2),
                        builder.CreateSelect(
                            amount_unknown,
                            constant_i64(context, 0),
                            known_plane3),
                        ValueKind::logic9});
                branch_to_next();
                return;
              }
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateSelect(
                          amount_unknown, value_mask, known_aval),
                      builder.CreateSelect(
                          amount_unknown, value_mask, known_bval),
                      value.width});
              branch_to_next();
            },
            [&](const Extract& operation) {
              const auto source =
                  load_register(
                      builder, registers, operation.source);
              auto* shift =
                  constant_i64(context, operation.offset);
              auto* mask =
                  constant_i64(context, width_mask(operation.width));
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateAnd(
                          builder.CreateLShr(source.aval, shift),
                          mask),
                      builder.CreateAnd(
                          builder.CreateLShr(source.bval, shift),
                          mask),
                      operation.width,
                      builder.CreateAnd(
                          builder.CreateLShr(
                              source.logic9_plane2, shift),
                          mask),
                      builder.CreateAnd(
                          builder.CreateLShr(
                              source.logic9_plane3, shift),
                          mask),
                      source.kind});
              branch_to_next();
            },
            [&](const DynamicExtract& operation) {
              const auto source = load_register(
                  builder, registers, operation.source);
              auto* shift = dynamic_offset(operation.selection);
              store_register(
                  builder,
                  registers,
                  operation.destination,
                  EncodedValue{
                      builder.CreateAnd(
                          builder.CreateLShr(source.aval, shift),
                          constant_i64(context, 1)),
                      builder.CreateAnd(
                          builder.CreateLShr(source.bval, shift),
                          constant_i64(context, 1)),
                      1,
                      builder.CreateAnd(
                          builder.CreateLShr(
                              source.logic9_plane2, shift),
                          constant_i64(context, 1)),
                      builder.CreateAnd(
                          builder.CreateLShr(
                              source.logic9_plane3, shift),
                          constant_i64(context, 1)),
                      source.kind});
              branch_to_next();
            },
            [&](const Insert& operation) {
              const auto destination_kind =
                  registers[operation.destination].kind;
              const auto target = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.target),
                  destination_kind);
              const auto source = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.source),
                  destination_kind);
              auto* source_mask =
                  constant_i64(context, width_mask(source.width));
              auto* shifted_mask =
                  builder.CreateShl(
                      source_mask,
                      constant_i64(context, operation.offset));
              auto* keep_mask =
                  builder.CreateAnd(
                      builder.CreateNot(shifted_mask),
                      constant_i64(
                          context, width_mask(target.width)));
              auto* shift =
                  constant_i64(context, operation.offset);
              auto* aval = builder.CreateOr(
                  builder.CreateAnd(target.aval, keep_mask),
                  builder.CreateShl(
                      builder.CreateAnd(source.aval, source_mask),
                      shift));
              auto* bval = builder.CreateOr(
                  builder.CreateAnd(target.bval, keep_mask),
                  builder.CreateShl(
                      builder.CreateAnd(source.bval, source_mask),
                      shift));
              auto* plane2 = builder.CreateOr(
                  builder.CreateAnd(
                      target.logic9_plane2, keep_mask),
                  builder.CreateShl(
                      builder.CreateAnd(
                          source.logic9_plane2, source_mask),
                      shift));
              auto* plane3 = builder.CreateOr(
                  builder.CreateAnd(
                      target.logic9_plane3, keep_mask),
                  builder.CreateShl(
                      builder.CreateAnd(
                          source.logic9_plane3, source_mask),
                      shift));
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      aval,
                      bval,
                      target.width,
                      plane2,
                      plane3,
                      destination_kind});
              branch_to_next();
            },
            [&](const DynamicInsert& operation) {
              const auto destination_kind =
                  registers[operation.destination].kind;
              const auto target = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.target),
                  destination_kind);
              const auto source = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.source),
                  destination_kind);
              auto* shift = dynamic_offset(operation.selection);
              auto* shifted_mask = builder.CreateShl(
                  constant_i64(context, 1), shift);
              auto* keep_mask = builder.CreateAnd(
                  builder.CreateNot(shifted_mask),
                  constant_i64(
                      context, width_mask(target.width)));
              const auto insert_plane =
                  [&](llvm::Value* target_plane,
                      llvm::Value* source_plane) {
                    return builder.CreateOr(
                        builder.CreateAnd(
                            target_plane, keep_mask),
                        builder.CreateShl(
                            builder.CreateAnd(
                                source_plane,
                                constant_i64(context, 1)),
                            shift));
                  };
              store_register(
                  builder,
                  registers,
                  operation.destination,
                  EncodedValue{
                      insert_plane(target.aval, source.aval),
                      insert_plane(target.bval, source.bval),
                      target.width,
                      insert_plane(
                          target.logic9_plane2,
                          source.logic9_plane2),
                      insert_plane(
                          target.logic9_plane3,
                          source.logic9_plane3),
                      destination_kind});
              branch_to_next();
            },
            [&](const Concatenate& operation) {
              llvm::Value* aval = constant_i64(context, 0);
              llvm::Value* bval = constant_i64(context, 0);
              llvm::Value* plane2 = constant_i64(context, 0);
              llvm::Value* plane3 = constant_i64(context, 0);
              const auto destination_kind =
                  registers[operation.destination].kind;
              std::uint32_t offset = 0;
              for (auto operand = operation.operands.rbegin();
                   operand != operation.operands.rend(); ++operand) {
                const auto source = coerce_value_kind(
                    builder,
                    load_register(builder, registers, *operand),
                    destination_kind);
                auto* source_mask =
                    constant_i64(context, width_mask(source.width));
                auto* source_aval =
                    builder.CreateAnd(source.aval, source_mask);
                auto* source_bval =
                    builder.CreateAnd(source.bval, source_mask);
                auto* source_plane2 = builder.CreateAnd(
                    source.logic9_plane2, source_mask);
                auto* source_plane3 = builder.CreateAnd(
                    source.logic9_plane3, source_mask);
                if (offset != 0) {
                  auto* shift = constant_i64(context, offset);
                  source_aval =
                      builder.CreateShl(source_aval, shift);
                  source_bval =
                      builder.CreateShl(source_bval, shift);
                  source_plane2 =
                      builder.CreateShl(source_plane2, shift);
                  source_plane3 =
                      builder.CreateShl(source_plane3, shift);
                }
                aval = builder.CreateOr(aval, source_aval);
                bval = builder.CreateOr(bval, source_bval);
                plane2 = builder.CreateOr(plane2, source_plane2);
                plane3 = builder.CreateOr(plane3, source_plane3);
                offset += source.width;
              }
              auto* mask =
                  constant_i64(context, width_mask(operation.width));
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateAnd(aval, mask),
                      builder.CreateAnd(bval, mask),
                      operation.width,
                      builder.CreateAnd(plane2, mask),
                      builder.CreateAnd(plane3, mask),
                      destination_kind});
              branch_to_next();
            },
            [&](const Binary &operation) {
              auto lhs =
                  load_register(builder, registers, operation.lhs);
              auto rhs =
                  load_register(builder, registers, operation.rhs);
              EncodedValue value{};
              if (lhs.kind == ValueKind::logic9
                  || rhs.kind == ValueKind::logic9) {
                lhs = coerce_value_kind(
                    builder, lhs, ValueKind::logic9);
                rhs = coerce_value_kind(
                    builder, rhs, ValueKind::logic9);
                if (operation.operation
                        == BinaryOperator::bit_and
                    || operation.operation
                        == BinaryOperator::bit_or
                    || operation.operation
                        == BinaryOperator::bit_xor) {
                  const auto make_table =
                      [&](const BinaryOperator selected) {
                        std::array<
                            std::array<Logic9, 9>, 9> table{};
                        for (std::size_t left = 0;
                             left < table.size();
                             ++left) {
                          for (std::size_t right = 0;
                               right < table[left].size();
                               ++right) {
                            const auto left_state =
                                static_cast<Logic9>(left);
                            const auto right_state =
                                static_cast<Logic9>(right);
                            table[left][right] =
                                selected
                                        == BinaryOperator::bit_and
                                    ? runtime::logic_and(
                                          left_state,
                                          right_state)
                                    : selected
                                              == BinaryOperator::bit_or
                                          ? runtime::logic_or(
                                                left_state,
                                                right_state)
                                          : runtime::logic_xor(
                                                left_state,
                                                right_state);
                          }
                        }
                        return table;
                      };
                  value = map_logic9_binary(
                      builder,
                      lhs,
                      rhs,
                      make_table(operation.operation));
                } else if (
                    operation.operation
                    == BinaryOperator::case_equal) {
                  auto* mask = constant_i64(
                      context, width_mask(lhs.width));
                  auto* mismatch = builder.CreateAnd(
                      builder.CreateOr(
                          builder.CreateOr(
                              builder.CreateXor(
                                  lhs.aval, rhs.aval),
                              builder.CreateXor(
                                  lhs.bval, rhs.bval)),
                          builder.CreateOr(
                              builder.CreateXor(
                                  lhs.logic9_plane2,
                                  rhs.logic9_plane2),
                              builder.CreateXor(
                                  lhs.logic9_plane3,
                                  rhs.logic9_plane3))),
                      mask);
                  auto* equal = builder.CreateICmpEQ(
                      mismatch, constant_i64(context, 0));
                  value = {
                      builder.CreateZExt(equal, i64),
                      constant_i64(context, 0),
                      1};
                } else {
                  value = lower_binary(
                      builder,
                      operation.operation,
                      coerce_value_kind(
                          builder, lhs, ValueKind::logic4),
                      coerce_value_kind(
                          builder, rhs, ValueKind::logic4));
                }
              } else {
                value = lower_binary(
                    builder, operation.operation, lhs, rhs);
              }
              store_register(
                  builder, registers, operation.destination, value);
              branch_to_next();
            },
            [&](const IntegerUnary& operation) {
              const auto source =
                  load_register(
                      builder, registers, operation.source);
              runtime_error_if(
                  builder.CreateICmpNE(
                      builder.CreateAnd(
                          source.bval,
                          constant_i64(
                              context,
                              std::numeric_limits<std::uint32_t>::max())),
                      constant_i64(context, 0)),
                  JitGeneratedRuntimeErrorReason::
                      integer_operand_unknown,
                  "integer.unary.unknown");
              auto* signed_source = builder.CreateSExt(
                  builder.CreateTrunc(source.aval, i32),
                  llvm::Type::getInt64Ty(context));
              auto* minimum = llvm::ConstantInt::getSigned(
                  llvm::Type::getInt64Ty(context),
                  std::numeric_limits<std::int32_t>::min());
              runtime_error_if(
                  builder.CreateICmpEQ(signed_source, minimum),
                  JitGeneratedRuntimeErrorReason::integer_overflow,
                  "integer.unary.overflow");
              llvm::Value* result = nullptr;
              if (operation.operation
                  == IntegerUnaryOperator::negate) {
                result = builder.CreateNeg(signed_source);
              } else {
                result = builder.CreateSelect(
                    builder.CreateICmpSLT(
                        signed_source,
                        constant_i64(context, 0)),
                    builder.CreateNeg(signed_source),
                    signed_source);
              }
              store_register(
                  builder,
                  registers,
                  operation.destination,
                  EncodedValue{
                      builder.CreateAnd(
                          result,
                          constant_i64(
                              context,
                              std::numeric_limits<std::uint32_t>::max())),
                      constant_i64(context, 0),
                      32});
              branch_to_next();
            },
            [&](const IntegerBinary& operation) {
              const auto lhs = load_register(
                  builder, registers, operation.lhs);
              const auto rhs = load_register(
                  builder, registers, operation.rhs);
              runtime_error_if(
                  builder.CreateICmpNE(
                      builder.CreateAnd(
                          builder.CreateOr(lhs.bval, rhs.bval),
                          constant_i64(
                              context,
                              std::numeric_limits<std::uint32_t>::max())),
                      constant_i64(context, 0)),
                  JitGeneratedRuntimeErrorReason::
                      integer_operand_unknown,
                  "integer.binary.unknown");
              auto* left = builder.CreateSExt(
                  builder.CreateTrunc(lhs.aval, i32), i64);
              auto* right = builder.CreateSExt(
                  builder.CreateTrunc(rhs.aval, i32), i64);
              auto* minimum = llvm::ConstantInt::getSigned(
                  i64, std::numeric_limits<std::int32_t>::min());
              auto* maximum = llvm::ConstantInt::getSigned(
                  i64, std::numeric_limits<std::int32_t>::max());
              const auto overflow_if =
                  [&](llvm::Value* value,
                      const std::string_view label) {
                    runtime_error_if(
                        builder.CreateOr(
                            builder.CreateICmpSLT(value, minimum),
                            builder.CreateICmpSGT(value, maximum)),
                        JitGeneratedRuntimeErrorReason::
                            integer_overflow,
                        label);
                  };
              llvm::Value* result = nullptr;
              switch (operation.operation) {
              case IntegerBinaryOperator::add:
                result = builder.CreateAdd(left, right);
                overflow_if(result, "integer.add.overflow");
                break;
              case IntegerBinaryOperator::subtract:
                result = builder.CreateSub(left, right);
                overflow_if(result, "integer.subtract.overflow");
                break;
              case IntegerBinaryOperator::multiply:
                result = builder.CreateMul(left, right);
                overflow_if(result, "integer.multiply.overflow");
                break;
              case IntegerBinaryOperator::power: {
                runtime_error_if(
                    builder.CreateICmpSLT(
                        right, constant_i64(context, 0)),
                    JitGeneratedRuntimeErrorReason::
                        integer_negative_exponent,
                    "integer.power.exponent");
                llvm::Value* powered = constant_i64(context, 1);
                llvm::Value* factor = left;
                for (std::uint32_t bit = 0; bit < 31; ++bit) {
                  auto* selected = builder.CreateICmpNE(
                      builder.CreateAnd(
                          builder.CreateLShr(
                              right, constant_i64(context, bit)),
                          constant_i64(context, 1)),
                      constant_i64(context, 0));
                  auto* product =
                      builder.CreateMul(powered, factor);
                  runtime_error_if(
                      builder.CreateAnd(
                          selected,
                          builder.CreateOr(
                              builder.CreateICmpSLT(
                                  product, minimum),
                              builder.CreateICmpSGT(
                                  product, maximum))),
                      JitGeneratedRuntimeErrorReason::
                          integer_overflow,
                      "integer.power.product");
                  powered = builder.CreateSelect(
                      selected, product, powered);
                  if (bit + 1U < 31U) {
                    auto* remaining = builder.CreateLShr(
                        right, constant_i64(context, bit + 1U));
                    auto* needed = builder.CreateICmpNE(
                        remaining, constant_i64(context, 0));
                    auto* squared =
                        builder.CreateMul(factor, factor);
                    runtime_error_if(
                        builder.CreateAnd(
                            needed,
                            builder.CreateOr(
                                builder.CreateICmpSLT(
                                    squared, minimum),
                                builder.CreateICmpSGT(
                                    squared, maximum))),
                        JitGeneratedRuntimeErrorReason::
                            integer_overflow,
                        "integer.power.factor");
                    factor = builder.CreateSelect(
                        needed, squared, factor);
                  }
                }
                result = powered;
                break;
              }
              case IntegerBinaryOperator::divide:
              case IntegerBinaryOperator::remainder:
              case IntegerBinaryOperator::modulo: {
                runtime_error_if(
                    builder.CreateICmpEQ(
                        right, constant_i64(context, 0)),
                    JitGeneratedRuntimeErrorReason::
                        integer_division_by_zero,
                    "integer.division.zero");
                runtime_error_if(
                    builder.CreateAnd(
                        builder.CreateICmpEQ(left, minimum),
                        builder.CreateICmpEQ(
                            right,
                            llvm::ConstantInt::getSigned(i64, -1))),
                    JitGeneratedRuntimeErrorReason::
                        integer_overflow,
                    "integer.division.overflow");
                if (operation.operation
                    == IntegerBinaryOperator::divide) {
                  result = builder.CreateSDiv(left, right);
                } else {
                  result = builder.CreateSRem(left, right);
                  if (operation.operation
                      == IntegerBinaryOperator::modulo) {
                    auto* nonzero = builder.CreateICmpNE(
                        result, constant_i64(context, 0));
                    auto* signs_differ = builder.CreateICmpNE(
                        builder.CreateICmpSLT(
                            result, constant_i64(context, 0)),
                        builder.CreateICmpSLT(
                            right, constant_i64(context, 0)));
                    result = builder.CreateSelect(
                        builder.CreateAnd(nonzero, signs_differ),
                        builder.CreateAdd(result, right),
                        result);
                  }
                }
                break;
              }
              }
              store_register(
                  builder,
                  registers,
                  operation.destination,
                  EncodedValue{
                      builder.CreateAnd(
                          result,
                          constant_i64(
                              context,
                              std::numeric_limits<std::uint32_t>::max())),
                      constant_i64(context, 0),
                      32});
              branch_to_next();
            },
            [&](const IntegerCheck& operation) {
              const auto source = load_register(
                  builder, registers, operation.source);
              runtime_error_if(
                  builder.CreateICmpNE(
                      builder.CreateAnd(
                          source.bval,
                          constant_i64(
                              context,
                              std::numeric_limits<std::uint32_t>::max())),
                      constant_i64(context, 0)),
                  JitGeneratedRuntimeErrorReason::
                      integer_operand_unknown,
                  "integer.check.unknown");
              auto* value = builder.CreateSExt(
                  builder.CreateTrunc(
                      source.aval,
                      llvm::Type::getInt32Ty(context)),
                  llvm::Type::getInt64Ty(context));
              auto* lower = llvm::ConstantInt::getSigned(
                  llvm::Type::getInt64Ty(context),
                  operation.lower);
              auto* upper = llvm::ConstantInt::getSigned(
                  llvm::Type::getInt64Ty(context),
                  operation.upper);
              runtime_error_if(
                  builder.CreateOr(
                      builder.CreateICmpSLT(value, lower),
                      builder.CreateICmpSGT(value, upper)),
                  JitGeneratedRuntimeErrorReason::
                      integer_subtype_range,
                  "integer.check.range");
              branch_to_next();
            },
            [&](const ConditionalSelect& operation) {
              const auto condition = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.condition),
                  ValueKind::logic4);
              const auto destination_kind =
                  registers[operation.destination].kind;
              const auto when_true = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.when_true),
                  destination_kind);
              const auto when_false = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.when_false),
                  destination_kind);
              auto *mask =
                  constant_i64(context, width_mask(when_true.width));
              auto *different = builder.CreateAnd(
                  builder.CreateOr(
                      builder.CreateOr(
                          builder.CreateXor(
                              when_true.aval, when_false.aval),
                          builder.CreateXor(
                              when_true.bval,
                              when_false.bval)),
                      builder.CreateOr(
                          builder.CreateXor(
                              when_true.logic9_plane2,
                              when_false.logic9_plane2),
                          builder.CreateXor(
                              when_true.logic9_plane3,
                              when_false.logic9_plane3))),
                  mask);
              auto *same = builder.CreateXor(different, mask);
              auto *merged_aval = builder.CreateOr(
                  builder.CreateAnd(when_true.aval, same),
                  different);
              auto *merged_bval = builder.CreateOr(
                  builder.CreateAnd(when_true.bval, same),
                  destination_kind == ValueKind::logic9
                      ? constant_i64(context, 0)
                      : different);
              auto* merged_plane2 = builder.CreateAnd(
                  when_true.logic9_plane2, same);
              auto* merged_plane3 = builder.CreateAnd(
                  when_true.logic9_plane3, same);
              auto *unknown = builder.CreateICmpNE(
                  builder.CreateAnd(
                      condition.bval, constant_i64(context, 1)),
                  constant_i64(context, 0));
              auto *select_true = builder.CreateICmpNE(
                  builder.CreateAnd(
                      condition.aval, constant_i64(context, 1)),
                  constant_i64(context, 0));
              auto *known_aval = builder.CreateSelect(
                  select_true, when_true.aval, when_false.aval);
              auto *known_bval = builder.CreateSelect(
                  select_true, when_true.bval, when_false.bval);
              auto* known_plane2 = builder.CreateSelect(
                  select_true,
                  when_true.logic9_plane2,
                  when_false.logic9_plane2);
              auto* known_plane3 = builder.CreateSelect(
                  select_true,
                  when_true.logic9_plane3,
                  when_false.logic9_plane3);
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateSelect(
                          unknown, merged_aval, known_aval),
                      builder.CreateSelect(
                          unknown, merged_bval, known_bval),
                      when_true.width,
                      builder.CreateSelect(
                          unknown, merged_plane2, known_plane2),
                      builder.CreateSelect(
                          unknown, merged_plane3, known_plane3),
                      destination_kind});
              branch_to_next();
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
              auto* text = builder.CreateGlobalString(
                  operation.text,
                  symbol + ".display." + std::to_string(index));
              builder.CreateCall(
                  output_type,
                  operation.postponed
                      ? postponed_output_callback
                      : output_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(i32, process.id),
                      text,
                      constant_i64(context, operation.text.size()),
                      llvm::ConstantInt::get(
                          i32, operation.newline ? 1U : 0U),
                  });
              branch_to_next();
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
            [&](const TimeDisplay&) {
              builder.CreateCall(
                  time_output_type,
                  time_output_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(i32, process.id),
                      llvm::ConstantInt::get(i32, instruction),
                  });
              branch_to_next();
            },
            [&](const MonitorInstall&) {
              builder.CreateCall(
                  time_output_type,
                  monitor_install_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(i32, process.id),
                      llvm::ConstantInt::get(i32, instruction),
                  });
              branch_to_next();
            },
            [&](const MonitorControl&) {
              builder.CreateCall(
                  time_output_type,
                  monitor_control_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(i32, process.id),
                      llvm::ConstantInt::get(i32, instruction),
                  });
              branch_to_next();
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
              builder.CreateCall(
                  report_type,
                  report_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(i32, process.id),
                      llvm::ConstantInt::get(i32, instruction),
                  });
              if (operation.severity
                  == runtime::simir::AssertionSeverity::failure) {
                return_result(
                    FSIM_JIT_RESUME_STATUS_ASSERTION_FAILED,
                    instruction,
                    0,
                    FSIM_JIT_FRAME_STATE_ASSERTION_FAILED,
                    instruction);
              } else {
                branch_to_next();
              }
            },
            [&](const Jump &operation) {
              builder.CreateBr(instruction_blocks[operation.target]);
            },
            [&](const Branch &operation) {
              const auto condition =
                  load_register(builder, registers, operation.condition);
              auto *known = builder.CreateICmpEQ(
                  condition.bval, constant_i64(context, 0));
              auto *one = builder.CreateICmpEQ(
                  condition.aval, constant_i64(context, 1));
              if (operation.unknown_policy ==
                  UnknownBranchPolicy::when_false) {
                builder.CreateCondBr(
                    builder.CreateAnd(known, one),
                    instruction_blocks[operation.when_true],
                    instruction_blocks[operation.when_false]);
                return;
              }

              auto *known_block = llvm::BasicBlock::Create(
                  context, "branch.known." + std::to_string(index), function);
              auto *unknown_block = llvm::BasicBlock::Create(
                  context, "branch.unknown." + std::to_string(index),
                  function);
              builder.CreateCondBr(known, known_block, unknown_block);

              builder.SetInsertPoint(known_block);
              builder.CreateCondBr(
                  one, instruction_blocks[operation.when_true],
                  instruction_blocks[operation.when_false]);

              builder.SetInsertPoint(unknown_block);
              constexpr auto reason =
                  JitGeneratedRuntimeErrorReason::
                      unknown_branch_condition;
              return_result(
                  FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR,
                  instruction,
                  static_cast<std::uint64_t>(reason),
                  FSIM_JIT_FRAME_STATE_RUNTIME_ERROR,
                  static_cast<std::uint32_t>(reason));
            },
            [&](const WaitFor &operation) {
              return_result(
                  FSIM_JIT_RESUME_STATUS_WAIT_FOR, instruction,
                  operation.delay, FSIM_JIT_FRAME_STATE_READY,
                  next_instruction);
            },
            [&](const WaitOn &operation) {
              return_result(
                  FSIM_JIT_RESUME_STATUS_WAIT_ON,
                  instruction,
                  operation.timeout.value_or(0),
                  FSIM_JIT_FRAME_STATE_READY, next_instruction);
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
            [&](const auto &) {
              llvm_unreachable(
                  "unsupported operations were rejected before lowering");
            }},
        process.operations[index]);
  }

}

}  // namespace fsim::compiler::llvm_detail
