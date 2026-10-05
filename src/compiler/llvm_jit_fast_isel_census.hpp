// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>

namespace llvm {
class LLVMContext;
class Module;
class raw_ostream;
}

namespace fsim::compiler::llvm_detail {

struct FastIselCensusSummary {
    bool enabled { };
    std::size_t fallback_functions { };
    std::size_t argument_fallbacks { };
    std::size_t call_fallbacks { };
    std::size_t terminator_fallbacks { };
    std::size_t instruction_fallbacks { };
};

/// Scope around one native module compilation. The caller owns the module's
/// context exclusively. Disabled scopes do not install diagnostic handlers.
class FastIselCensus {
public:
    FastIselCensus(llvm::Module&, bool enabled);
    ~FastIselCensus();
    FastIselCensus(const FastIselCensus&) = delete;
    FastIselCensus& operator=(const FastIselCensus&) = delete;

    [[nodiscard]] FastIselCensusSummary summary() const;
    void report(bool compilation_succeeded, llvm::raw_ostream& output) const;

private:
    class Impl;
    Impl* impl_ { }; // Owned by the LLVMContext for the duration of this scope.
};

} // namespace fsim::compiler::llvm_detail
