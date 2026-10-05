// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_compilation_contexts.hpp"

#include <llvm/Target/TargetMachine.h>

#include <cassert>
#include <utility>

namespace fsim::compiler::llvm_detail {

LlvmCompilationContexts::LlvmCompilationContexts(
    llvm::orc::JITTargetMachineBuilder machine_builder,
    llvm::ObjectCache* const object_cache)
    : machine_builder_ { std::move(machine_builder) }
    , object_cache_ { object_cache }
{
}

llvm::orc::ThreadSafeContext LlvmCompilationContexts::acquire()
{
    const auto worker = std::this_thread::get_id();
    const std::scoped_lock lock { mutex_ };
    if (const auto found = workers_.find(worker); found != workers_.end()) {
        return found->second->context;
    }
    auto state = std::make_shared<State>();
    auto* const context = state->context.withContextDo(
        [](llvm::LLVMContext* value) { return value; });
    // Publish both indexes transactionally. A failed registration must not
    // leave a context that can be acquired but cannot find its compiler.
    const auto [position, inserted] = contexts_.emplace(context, state);
    assert(inserted);
    try {
        workers_.emplace(worker, state);
    } catch (...) {
        contexts_.erase(position);
        throw;
    }
    return state->context;
}

llvm::Expected<std::unique_ptr<llvm::MemoryBuffer>>
LlvmCompilationContexts::compile(llvm::Module& module,
    const llvm::CodeGenOptLevel optimization,
    llvm::orc::IRCompileLayer::IRCompiler& fallback)
{
    std::shared_ptr<State> state;
    {
        const std::scoped_lock lock { mutex_ };
        const auto found = contexts_.find(&module.getContext());
        if (found != contexts_.end()) {
            state = found->second;
        }
    }
    if (!state || (optimization != llvm::CodeGenOptLevel::None
                      && optimization != llvm::CodeGenOptLevel::Less)) {
        fallback_compilations_.fetch_add(1U, std::memory_order_relaxed);
        return fallback(module);
    }
    // Never hold the registry mutex while waiting for a context: ORC enters
    // this function with the module's context lock already held.
    return state->context.withContextDo(
        [&]([[maybe_unused]] llvm::LLVMContext* context)
            -> llvm::Expected<std::unique_ptr<llvm::MemoryBuffer>> {
            assert(context == &module.getContext());
            const auto index = optimization == llvm::CodeGenOptLevel::Less
                ? 1U : 0U;
            auto& compiler = state->compilers[index];
            if (!compiler) {
                auto builder = machine_builder_;
                builder.setCodeGenOptLevel(optimization);
                auto machine = builder.createTargetMachine();
                if (!machine) {
                    return machine.takeError();
                }
                compiler = std::make_unique<llvm::orc::TMOwningSimpleCompiler>(
                    std::move(*machine), object_cache_);
                target_machines_.fetch_add(1U, std::memory_order_relaxed);
            } else {
                reused_compilations_.fetch_add(1U, std::memory_order_relaxed);
            }
            return (*compiler)(module);
        });
}

LlvmCompilationContexts::Statistics LlvmCompilationContexts::statistics() const
{
    const std::scoped_lock lock { mutex_ };
    return { static_cast<std::uint64_t>(contexts_.size()),
        target_machines_.load(std::memory_order_relaxed),
        reused_compilations_.load(std::memory_order_relaxed),
        fallback_compilations_.load(std::memory_order_relaxed) };
}

} // namespace fsim::compiler::llvm_detail
