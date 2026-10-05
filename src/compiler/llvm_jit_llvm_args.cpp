// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_llvm_args.hpp"

#include "fsim/compiler/llvm_jit.hpp"

#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/Allocator.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/StringSaver.h>
#include <llvm/Support/raw_ostream.h>

#include <cstdlib>
#include <exception>
#include <limits>
#include <mutex>
#include <string>

namespace fsim::compiler::llvm_detail {
namespace {

struct LlvmArguments {
    std::string source;
    std::string error;
};

[[nodiscard]] LlvmArguments parse_arguments(const std::string_view source)
{
    LlvmArguments result { std::string { source }, { } };
    if (source.empty()) {
        return result;
    }

    llvm::BumpPtrAllocator allocator;
    llvm::StringSaver saver { allocator };
    llvm::SmallVector<const char*, 16> arguments { "fsim-llvm" };
    // Use the same quoting on every host; no shell expansion is performed.
    llvm::cl::TokenizeGNUCommandLine(source, saver, arguments);
    for (const auto* argument : arguments) {
        auto option = std::string_view { argument };
        if (option.starts_with('@')) {
            result.error = "FSIM_LLVM_ARGS does not accept response files";
            return result;
        }
        while (option.starts_with('-')) {
            option.remove_prefix(1U);
        }
        option = option.substr(0U, option.find('='));
        if (option == "help" || option == "help-hidden"
            || option == "help-list" || option == "help-list-hidden"
            || option == "version") {
            result.error = "FSIM_LLVM_ARGS cannot request process-exiting "
                           "LLVM help or version options";
            return result;
        }
    }
    if (arguments.size() > static_cast<std::size_t>(
            std::numeric_limits<int>::max())) {
        result.error = "FSIM_LLVM_ARGS contains too many arguments";
        return result;
    }
    llvm::raw_string_ostream errors { result.error };
    if (!llvm::cl::ParseCommandLineOptions(
            static_cast<int>(arguments.size()), arguments.data(), {}, &errors)) {
        if (result.error.empty()) {
            result.error = "LLVM rejected FSIM_LLVM_ARGS";
        }
    }
    return result;
}

} // namespace

std::string_view initialize_llvm_arguments()
{
    const auto* environment = std::getenv("FSIM_LLVM_ARGS");
    const auto source = environment != nullptr
        ? std::string_view { environment } : std::string_view { };
    static std::once_flag once;
    static LlvmArguments arguments;
    static std::exception_ptr failure;
    std::call_once(once, [source] {
        try {
            arguments = parse_arguments(source);
        } catch (...) {
            // Parsing can mutate LLVM globals before an allocation fails.
            // Do not let call_once retry a partially applied configuration.
            failure = std::current_exception();
        }
    });
    if (failure) {
        std::rethrow_exception(failure);
    }
    if (!arguments.error.empty()) {
        throw LlvmJitError(arguments.error);
    }
    if (source != arguments.source) {
        throw LlvmJitError(
            "FSIM_LLVM_ARGS changed after LLVM initialization; "
            "use a new process to change LLVM options");
    }
    return arguments.source;
}

} // namespace fsim::compiler::llvm_detail
