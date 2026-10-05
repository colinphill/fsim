// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_fast_isel_census.hpp"

#include <llvm/IR/DiagnosticHandler.h>
#include <llvm/IR/DiagnosticInfo.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>

namespace fsim::compiler::llvm_detail {

class FastIselCensus::Impl final : public llvm::DiagnosticHandler {
public:
    explicit Impl(llvm::Module& module)
        : context_(module.getContext())
        , module_(module.getModuleIdentifier())
    {
    }

    bool isMissedOptRemarkEnabled(llvm::StringRef pass) const override
    {
        return pass == "sdagisel" || previous_->isMissedOptRemarkEnabled(pass);
    }

    bool isAnalysisRemarkEnabled(llvm::StringRef pass) const override
    {
        return previous_->isAnalysisRemarkEnabled(pass);
    }

    bool isPassedOptRemarkEnabled(llvm::StringRef pass) const override
    {
        return previous_->isPassedOptRemarkEnabled(pass);
    }

    bool isAnyRemarkEnabled() const override { return true; }

    bool handleDiagnostics(const llvm::DiagnosticInfo& diagnostic) override
    {
        const auto* remark
            = llvm::dyn_cast<llvm::DiagnosticInfoOptimizationBase>(&diagnostic);
        if (remark == nullptr || !remark->isMissed()
            || remark->getPassName() != "sdagisel"
            || remark->getRemarkName() != "FastISelFailure") {
            return previous_->handleDiagnostics(diagnostic);
        }
        const auto message = remark->getMsg();
        const auto reason = message.starts_with("FastISel missed call") ? "call"
            : message.starts_with("FastISel missed terminator") ? "terminator"
            : message.starts_with("FastISel didn't lower all arguments") ? "arguments"
            : "instruction";
        ++reasons_[reason];
        functions_.insert(remark->getFunction().getName().str());
        // Keep the original LLVM remark when explicitly requested by the user.
        if (previous_->isMissedOptRemarkEnabled("sdagisel")) {
            return previous_->handleDiagnostics(diagnostic);
        }
        return true;
    }

    [[nodiscard]] FastIselCensusSummary summary() const
    {
        const auto count = [&](const char* const reason) {
            const auto found = reasons_.find(reason);
            return found == reasons_.end() ? std::size_t { 0U }
                                           : found->second;
        };
        return {
            true,
            functions_.size(),
            count("arguments"),
            count("call"),
            count("terminator"),
            count("instruction"),
        };
    }

    void report(bool succeeded, llvm::raw_ostream& output) const
    {
        std::string text;
        llvm::raw_string_ostream stream(text);
        stream << "fsim-profile: llvm-fast-isel module='";
        stream.write_escaped(module_);
        stream << "' compiled=" << succeeded
               << " fallback_functions=" << functions_.size();
        for (const auto* reason : { "arguments", "call", "terminator", "instruction" }) {
            const auto found = reasons_.find(reason);
            stream << " " << reason << '='
                   << (found == reasons_.end() ? 0U : found->second);
        }
        stream << '\n';
        static std::mutex output_mutex;
        const std::lock_guard lock(output_mutex);
        output << text;
    }

    llvm::LLVMContext& context_;
    std::string module_;
    std::unique_ptr<llvm::DiagnosticHandler> previous_;
    std::map<std::string, std::size_t> reasons_;
    std::set<std::string> functions_;
};

FastIselCensus::FastIselCensus(llvm::Module& module, bool enabled)
{
    if (!enabled) {
        return;
    }
    // Allocate before transferring the context's handler so allocation failure
    // leaves its diagnostic behavior intact.
    auto handler = std::make_unique<Impl>(module);
    auto fallback = std::make_unique<llvm::DiagnosticHandler>();
    handler->previous_ = module.getContext().getDiagnosticHandler();
    if (!handler->previous_) {
        handler->previous_ = std::move(fallback);
    }
    impl_ = handler.get();
    module.getContext().setDiagnosticHandler(std::move(handler));
}

FastIselCensus::~FastIselCensus()
{
    if (impl_) {
        // The context owns this handler while it is installed. Release the
        // non-owning scoped reference before replacement destroys the handler.
        auto* handler = std::exchange(impl_, nullptr);
        auto previous = std::move(handler->previous_);
        previous->HasErrors |= handler->HasErrors;
        handler->context_.setDiagnosticHandler(std::move(previous));
    }
}

FastIselCensusSummary FastIselCensus::summary() const
{
    return impl_ != nullptr ? impl_->summary() : FastIselCensusSummary { };
}

void FastIselCensus::report(
    bool succeeded, llvm::raw_ostream& output) const
{
    if (impl_) {
        impl_->report(succeeded, output);
    }
}

} // namespace fsim::compiler::llvm_detail
