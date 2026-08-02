// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_lowering_internal.hpp"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>

namespace fsim::compiler::llvm_detail {

using runtime::simir::Display;
using runtime::simir::TimeDisplay;
using runtime::simir::MonitorInstall;
using runtime::simir::MonitorControl;

void OutputOperationLowerer::lower(
    const Display& operation) {
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
                      llvm::ConstantInt::get(i32, process_id),
                      text,
                      constant_i64(context, operation.text.size()),
                      llvm::ConstantInt::get(
                          i32, operation.newline ? 1U : 0U),
                  });
              branch_to_next();
            
}

void OutputOperationLowerer::lower(
    const TimeDisplay&) {
              builder.CreateCall(
                  time_output_type,
                  time_output_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(i32, process_id),
                      llvm::ConstantInt::get(i32, instruction),
                  });
              branch_to_next();
            
}

void OutputOperationLowerer::lower(
    const MonitorInstall&) {
              builder.CreateCall(
                  time_output_type,
                  monitor_install_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(i32, process_id),
                      llvm::ConstantInt::get(i32, instruction),
                  });
              branch_to_next();
            
}

void OutputOperationLowerer::lower(
    const MonitorControl&) {
              builder.CreateCall(
                  time_output_type,
                  monitor_control_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(i32, process_id),
                      llvm::ConstantInt::get(i32, instruction),
                  });
              branch_to_next();
            
}



void OutputOperationLowerer::lower(
    const runtime::simir::Report& operation) {
              builder.CreateCall(
                  report_type,
                  report_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(i32, process_id),
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
            
}

void OutputOperationLowerer::lower(
    const runtime::simir::StringReport&) {
  builder.CreateCall(
      report_type,
      report_callback,
      {
          context_pointer,
          llvm::ConstantInt::get(i32, process_id),
          llvm::ConstantInt::get(i32, instruction),
      });
  branch_to_next();
}

void OutputOperationLowerer::lower(
    const runtime::simir::WaitFor& operation) {
  return_result(
      FSIM_JIT_RESUME_STATUS_WAIT_FOR,
      instruction,
      operation.delay,
      FSIM_JIT_FRAME_STATE_READY,
      instruction + 1U);
}

void OutputOperationLowerer::lower(
    const runtime::simir::WaitOn& operation) {
  return_result(
      FSIM_JIT_RESUME_STATUS_WAIT_ON,
      instruction,
      operation.timeout.value_or(0),
      FSIM_JIT_FRAME_STATE_READY,
      instruction + 1U);
}

}  // namespace fsim::compiler::llvm_detail
