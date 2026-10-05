// SPDX-License-Identifier: Apache-2.0
#include "fsim/compiler/llvm_jit.hpp"
#include "llvm_jit_llvm_args.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void set_arguments(const char* value)
{
#if defined(_WIN32)
    const auto status = _putenv_s("FSIM_LLVM_ARGS", value);
#else
    const auto status = setenv("FSIM_LLVM_ARGS", value, 1);
#endif
    assert(status == 0);
}

} // namespace

int main(int argc, char** argv)
{
    using fsim::compiler::LlvmJitError;
    using fsim::compiler::llvm_detail::initialize_llvm_arguments;
    assert(argc >= 2);
    const auto mode = std::string_view { argv[1] };
    if (mode == "cache" || mode == "immutable-cache") {
        assert(argc == 3);
        fsim::compiler::LlvmJit jit { fsim::compiler::LlvmJitOptions {
            fsim::compiler::JitOptimizationLevel::o2,
            std::filesystem::path { argv[2] } } };
        if (mode == "immutable-cache") {
            jit.set_immutable_design_identity("llvm-argument-fixture");
        }
        fsim::runtime::simir::Process process;
        process.id = 1U;
        process.name = "llvm_argument_fixture";
        process.operations = { fsim::runtime::simir::Halt { } };
        jit.add_process(process.name, process, { });
        assert(jit.lookup(process.name));
        const auto statistics = jit.cache_statistics();
        std::cout << statistics.hits << ' ' << statistics.misses << '\n';
        return 0;
    }
    assert(argc == 2);
    if (mode == "freeze") {
        set_arguments("");
        assert(initialize_llvm_arguments().empty());
        set_arguments("-fsim-invalid-option-after-initialization");
        try {
            static_cast<void>(initialize_llvm_arguments());
            assert(false);
        } catch (const LlvmJitError& error) {
            assert(std::string_view { error.what() }.find("changed after")
                != std::string_view::npos);
        }
        set_arguments("");
        assert(initialize_llvm_arguments().empty());
        return 0;
    }
    if (mode == "invalid" || mode == "help" || mode == "response") {
        set_arguments(mode == "invalid" ? "-fsim-invalid-llvm-option"
            : mode == "help" ? "--help" : "@missing-response-file");
        try {
            static_cast<void>(initialize_llvm_arguments());
            assert(false);
        } catch (const LlvmJitError& error) {
            assert(!std::string_view { error.what() }.empty());
        }
        // A failed parse may have partially changed LLVM globals, so it is
        // permanently rejected instead of silently retrying in this process.
        set_arguments("");
        try {
            static_cast<void>(initialize_llvm_arguments());
            assert(false);
        } catch (const LlvmJitError&) {
        }
        return 0;
    }
    assert(mode == "identity");
    const auto identity = fsim::compiler::LlvmJit::native_host_identity(
        fsim::compiler::JitOptimizationLevel::o2);
    std::cout << identity.fingerprint << '\n';
}
