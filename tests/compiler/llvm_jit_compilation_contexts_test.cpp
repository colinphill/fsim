// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_compilation_contexts.hpp"
#include "llvm_jit_test_support.hpp"

#include <llvm/ExecutionEngine/ObjectCache.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/MemoryBuffer.h>

#include <array>
#include <cassert>
#include <future>
#include <latch>
#include <map>
#include <mutex>
#include <string>

namespace fsim::tests::compiler {
namespace {

class ContextObjectCache final : public llvm::ObjectCache {
public:
    void notifyObjectCompiled(const llvm::Module* module,
        llvm::MemoryBufferRef object) override
    {
        const std::scoped_lock lock { mutex_ };
        objects_[module->getModuleIdentifier()] = object.getBuffer().str();
    }

    std::unique_ptr<llvm::MemoryBuffer> getObject(
        const llvm::Module* module) override
    {
        const std::scoped_lock lock { mutex_ };
        const auto found = objects_.find(module->getModuleIdentifier());
        if (found == objects_.end()) {
            return nullptr;
        }
        ++hits_;
        return llvm::MemoryBuffer::getMemBufferCopy(found->second);
    }

    [[nodiscard]] std::size_t hits() const
    {
        const std::scoped_lock lock { mutex_ };
        return hits_;
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, std::string> objects_;
    std::size_t hits_ { };
};

void check_context_bound_compilers(const bool cached)
{
    using fsim::compiler::llvm_detail::LlvmCompilationContexts;
    auto detected = llvm::orc::JITTargetMachineBuilder::detectHost();
    assert(detected);
    auto builder = std::move(*detected);
    auto layout = builder.getDefaultDataLayoutForTarget();
    assert(layout);
    ContextObjectCache cache;
    LlvmCompilationContexts pool { builder, cached ? &cache : nullptr };
    llvm::orc::ConcurrentIRCompiler fallback { builder, cached ? &cache : nullptr };
    auto context = pool.acquire();
    auto same_context = pool.acquire();
    context.withContextDo([&](llvm::LLVMContext* first) {
        same_context.withContextDo([&](llvm::LLVMContext* second) {
            assert(first == second);
        });
    });
    const auto compile_one = [&](llvm::orc::ThreadSafeContext& selected,
                                 const std::string& name,
                                 const llvm::CodeGenOptLevel optimization) {
        return selected.withContextDo([&](llvm::LLVMContext* raw_context) {
            llvm::Module module { name, *raw_context };
            module.setTargetTriple(builder.getTargetTriple());
            module.setDataLayout(*layout);
            llvm::IRBuilder<> ir { *raw_context };
            auto* type = llvm::FunctionType::get(ir.getInt32Ty(),
                { ir.getInt32Ty() }, false);
            auto* function = llvm::Function::Create(type,
                llvm::Function::ExternalLinkage, name, module);
            ir.SetInsertPoint(llvm::BasicBlock::Create(
                *raw_context, "entry", function));
            ir.CreateRet(ir.CreateAdd(function->getArg(0U), ir.getInt32(17U)));
            auto object = pool.compile(module, optimization, fallback);
            assert(object && !(*object)->getBuffer().empty());
            return (*object)->getBuffer().str();
        });
    };

    for (const auto tier : { llvm::CodeGenOptLevel::None,
             llvm::CodeGenOptLevel::Less }) {
        const auto name = tier == llvm::CodeGenOptLevel::None
            ? "context_none" : "context_less";
        const auto first = compile_one(context, name, tier);
        const auto second = compile_one(context, name, tier);
        assert(first == second);
    }
    auto counts = pool.statistics();
    assert(counts.contexts == 1U && counts.target_machines == 2U
        && counts.reused_compilations == 2U);

    constexpr std::size_t worker_count = 4U;
    std::latch ready { worker_count };
    std::latch start { 1U };
    std::array<std::future<void>, worker_count> workers;
    for (std::size_t worker = 0U; worker < worker_count; ++worker) {
        workers[worker] = std::async(std::launch::async, [&, worker] {
            auto worker_context = pool.acquire();
            ready.count_down();
            start.wait();
            const auto name = "worker_context_" + std::to_string(worker);
            const auto first = compile_one(
                worker_context, name, llvm::CodeGenOptLevel::None);
            const auto second = compile_one(
                worker_context, name, llvm::CodeGenOptLevel::None);
            assert(first == second);
        });
    }
    ready.wait();
    start.count_down();
    for (auto& worker : workers) {
        worker.get();
    }
    counts = pool.statistics();
    assert(counts.contexts == worker_count + 1U
        && counts.target_machines == worker_count + 2U
        && counts.reused_compilations == worker_count + 2U);

    // An unrelated LLVMContext must never borrow any of the reused machines.
    llvm::orc::ThreadSafeContext foreign {
        std::make_unique<llvm::LLVMContext>()
    };
    compile_one(foreign, "foreign_context", llvm::CodeGenOptLevel::None);
    counts = pool.statistics();
    assert(counts.fallback_compilations == 1U
        && counts.target_machines == worker_count + 2U);
    assert(cache.hits() == (cached ? worker_count + 2U : 0U));
}

} // namespace

void test_compilation_context_reuse()
{
    // Constructing the public JIT initializes the native target through the
    // same once-only path used by application materialization.
    fsim::compiler::LlvmJit initialize;
    check_context_bound_compilers(false);
    check_context_bound_compilers(true);
}

} // namespace fsim::tests::compiler
