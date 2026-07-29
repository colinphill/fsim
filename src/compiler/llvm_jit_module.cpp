// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_internal.hpp"

#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/raw_ostream.h>

namespace fsim::compiler::llvm_detail {

void optimize_module(llvm::Module &module,
                     const JitOptimizationLevel optimization) {
  llvm::LoopAnalysisManager loop_analyses;
  llvm::FunctionAnalysisManager function_analyses;
  llvm::CGSCCAnalysisManager cgscc_analyses;
  llvm::ModuleAnalysisManager module_analyses;
  llvm::PassBuilder builder;

  builder.registerModuleAnalyses(module_analyses);
  builder.registerCGSCCAnalyses(cgscc_analyses);
  builder.registerFunctionAnalyses(function_analyses);
  builder.registerLoopAnalyses(loop_analyses);
  builder.crossRegisterProxies(loop_analyses, function_analyses,
                              cgscc_analyses, module_analyses);

  const auto level = optimization == JitOptimizationLevel::o0
                         ? llvm::OptimizationLevel::O0
                         : llvm::OptimizationLevel::O2;
  auto pipeline = builder.buildPerModuleDefaultPipeline(level);
  pipeline.run(module, module_analyses);
}

[[nodiscard]] std::string verify_error(llvm::Module &module) {
  std::string message;
  llvm::raw_string_ostream stream(message);
  if (!llvm::verifyModule(module, &stream)) {
    return {};
  }
  stream.flush();
  return message;
}

}  // namespace fsim::compiler::llvm_detail
