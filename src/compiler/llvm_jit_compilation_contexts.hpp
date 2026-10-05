// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <llvm/ExecutionEngine/Orc/CompileUtils.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace fsim::compiler::llvm_detail {

/// Reuses target machines only with the LLVMContext they were created for.
/// Each lowering worker receives a shared context; its recursive context lock
/// serializes lowering, code generation and teardown, including materialization
/// on another application worker. Other module producers keep fresh machines.
/// The containing JIT must keep its object cache alive until this pool dies.
class LlvmCompilationContexts final {
public:
    struct Statistics {
        std::uint64_t contexts { };
        std::uint64_t target_machines { };
        std::uint64_t reused_compilations { };
        std::uint64_t fallback_compilations { };
    };

    LlvmCompilationContexts(llvm::orc::JITTargetMachineBuilder,
        llvm::ObjectCache*);

    [[nodiscard]] llvm::orc::ThreadSafeContext acquire();

    [[nodiscard]] llvm::Expected<std::unique_ptr<llvm::MemoryBuffer>> compile(
        llvm::Module&, llvm::CodeGenOptLevel,
        llvm::orc::IRCompileLayer::IRCompiler& fallback);

    [[nodiscard]] Statistics statistics() const;

private:
    struct State {
        // Reverse destruction order destroys both machines before the context.
        llvm::orc::ThreadSafeContext context {
            std::make_unique<llvm::LLVMContext>()
        };
        std::array<std::unique_ptr<llvm::orc::TMOwningSimpleCompiler>, 2U>
            compilers;
    };

    llvm::orc::JITTargetMachineBuilder machine_builder_;
    llvm::ObjectCache* object_cache_ { };
    mutable std::mutex mutex_;
    std::map<std::thread::id, std::shared_ptr<State>> workers_;
    std::unordered_map<const llvm::LLVMContext*, std::shared_ptr<State>> contexts_;
    std::atomic<std::uint64_t> target_machines_ { };
    std::atomic<std::uint64_t> reused_compilations_ { };
    std::atomic<std::uint64_t> fallback_compilations_ { };
};

} // namespace fsim::compiler::llvm_detail
