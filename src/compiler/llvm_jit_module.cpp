// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_internal.hpp"
#include "../diagnostic/thread_cpu_clock.hpp"

#include <cstdlib>
#include <map>
#include <vector>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/PassInstrumentation.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/raw_ostream.h>

namespace fsim::compiler::llvm_detail {

void optimize_module(llvm::Module &module,
                     const JitOptimizationLevel optimization,
                     const std::string_view profile_identity,
                     const std::size_t process_count) {
  const bool profile_passes
      = std::getenv("FSIM_PROFILE_LLVM_MODULES") != nullptr;
  struct PassFrame {
    std::string name;
    std::optional<diagnostic::ThreadCpuTime> begin;
    diagnostic::ThreadCpuTime child_cpu { };
    bool complete { true };
  };
  struct PassTotals {
    std::size_t calls { };
    std::size_t unavailable { };
    diagnostic::ThreadCpuTime inclusive { };
    diagnostic::ThreadCpuTime exclusive { };
  };
  std::vector<PassFrame> active_passes;
  std::map<std::string, PassTotals> pass_totals;
  std::size_t invalid_nesting { };
  llvm::PassInstrumentationCallbacks callbacks;
  if (profile_passes) {
    callbacks.registerBeforeNonSkippedPassCallback(
        [&](const llvm::StringRef name, llvm::Any) {
          active_passes.push_back(
              { name.str(), diagnostic::thread_cpu_now(), { }, true });
        });
    const auto finish_pass = [&](const llvm::StringRef name) {
      if (active_passes.empty()) {
        ++invalid_nesting;
        return;
      }
      auto frame = std::move(active_passes.back());
      active_passes.pop_back();
      if (llvm::StringRef { frame.name } != name) {
        ++invalid_nesting;
      }
      auto& totals = pass_totals[frame.name];
      ++totals.calls;
      const auto elapsed = diagnostic::thread_cpu_elapsed(
          frame.begin, diagnostic::thread_cpu_now());
      if (!elapsed || !frame.complete
          || frame.child_cpu > *elapsed) {
        ++totals.unavailable;
        if (!active_passes.empty()) {
          active_passes.back().complete = false;
        }
        return;
      }
      totals.inclusive += *elapsed;
      totals.exclusive += *elapsed - frame.child_cpu;
      if (!active_passes.empty()) {
        active_passes.back().child_cpu += *elapsed;
      }
    };
    callbacks.registerAfterPassCallback(
        [finish_pass](const llvm::StringRef name, llvm::Any,
            const llvm::PreservedAnalyses&) {
          finish_pass(name);
        });
    callbacks.registerAfterPassInvalidatedCallback(
        [finish_pass](const llvm::StringRef name,
            const llvm::PreservedAnalyses&) {
          finish_pass(name);
        });
  }
  // Analyses may retain callbacks through their proxy results. Destroy them
  // before the instrumentation and its captured pass totals.
  llvm::LoopAnalysisManager loop_analyses;
  llvm::FunctionAnalysisManager function_analyses;
  llvm::CGSCCAnalysisManager cgscc_analyses;
  llvm::ModuleAnalysisManager module_analyses;
  llvm::PassBuilder builder { nullptr, llvm::PipelineTuningOptions { },
      std::nullopt, profile_passes ? &callbacks : nullptr };

  builder.registerModuleAnalyses(module_analyses);
  builder.registerCGSCCAnalyses(cgscc_analyses);
  builder.registerFunctionAnalyses(function_analyses);
  builder.registerLoopAnalyses(loop_analyses);
  builder.crossRegisterProxies(loop_analyses, function_analyses,
                              cgscc_analyses, module_analyses);

  llvm::ModulePassManager pipeline;
  if (optimization == JitOptimizationLevel::o0) {
    pipeline = builder.buildPerModuleDefaultPipeline(
        llvm::OptimizationLevel::O0);
  } else if (optimization == JitOptimizationLevel::o1) {
    llvm::cantFail(builder.parsePassPipeline(
        pipeline,
        "function(sroa,early-cse,simplifycfg,instcombine<no-verify-fixpoint>,"
        "reassociate,gvn,dse,"
        "simplifycfg)"));
  } else {
    // Generated SimIR functions benefit from scalar propagation and dead-code
    // cleanup, but the stock O2 pipeline and generic jump-threading passes
    // spend substantial cold-start time repeatedly analyzing the generated
    // process dispatch CFG. Keep the profitable scalar subset explicit and
    // bounded; SimIR lowering has already coalesced its straight-line blocks.
    llvm::cantFail(builder.parsePassPipeline(
        pipeline,
        "function(sroa,early-cse,simplifycfg,"
        "instcombine<no-verify-fixpoint>,simplifycfg)"));
  }
  pipeline.run(module, module_analyses);
  // Large generated process dispatch functions made the stock pipeline too
  // expensive. Small functions with a real backedge can still benefit from
  // its loop and range-check simplifications.
  constexpr std::size_t loop_instruction_limit = 2048;
  constexpr std::size_t loop_block_limit = 256;
  std::size_t instruction_count { };
  std::size_t block_count { };
  bool size_eligible = optimization == JitOptimizationLevel::o2
      && process_count == 1;
  if (size_eligible) {
    for (const auto& function : module) {
      if (function.isDeclaration()) {
        continue;
      }
      if (function.size() > loop_block_limit - block_count) {
        size_eligible = false;
        break;
      }
      block_count += function.size();
      for (const auto& block : function) {
        if (block.size() > loop_instruction_limit - instruction_count) {
          size_eligible = false;
          break;
        }
        instruction_count += block.size();
      }
      if (!size_eligible) {
        break;
      }
    }
  }
  bool has_natural_loop { };
  if (size_eligible) {
    for (auto& function : module) {
      if (!function.isDeclaration()
          && !function_analyses.getResult<llvm::LoopAnalysis>(function)
                  .empty()) {
        has_natural_loop = true;
        break;
      }
    }
  }
  if (has_natural_loop) {
    auto loop_pipeline = builder.buildPerModuleDefaultPipeline(
        llvm::OptimizationLevel::O1);
    loop_pipeline.run(module, module_analyses);
  }
  if (profile_passes) {
    const auto milliseconds = [](const auto duration) {
      return std::chrono::duration<double, std::milli> { duration }.count();
    };
    std::string profile_line;
    llvm::raw_string_ostream profile(profile_line);
    const auto identity = profile_identity.empty()
        ? std::string_view { module.getModuleIdentifier() }
        : profile_identity;
    profile << "fsim-profile: llvm-loop-o1 identity='" << identity
            << "' process_count=" << process_count
            << " size_eligible=" << size_eligible
            << " natural_loop=" << has_natural_loop
            << " counted_instructions=" << instruction_count
            << " counted_blocks=" << block_count << '\n';
    for (const auto& [name, totals] : pass_totals) {
      profile << "fsim-profile: llvm-pass-cpu identity='" << identity
              << "' pass='" << name << "' calls=" << totals.calls
              << " unavailable=" << totals.unavailable
              << " inclusive_cpu_ms=" << milliseconds(totals.inclusive)
              << " exclusive_cpu_ms=" << milliseconds(totals.exclusive)
              << '\n';
    }
    profile << "fsim-profile: llvm-pass-cpu-summary identity='"
            << identity << "' invalid_nesting=" << invalid_nesting
            << " remaining_frames=" << active_passes.size() << '\n';
    profile.flush();
    llvm::errs() << profile_line;
  }
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
