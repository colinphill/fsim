// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"

namespace fsim::tests::compiler {

namespace {

    std::size_t cached_object_count(const std::filesystem::path& root)
    {
        if (!std::filesystem::exists(root)) {
            return 0;
        }
        return std::ranges::count_if(
            std::filesystem::recursive_directory_iterator { root }, [](const auto& entry) {
                return entry.is_regular_file() && entry.path().extension() == ".fobj";
            });
    }

} // namespace

void test_vhdl_language_profile_cache_identity(
    const std::filesystem::path& cache_directory)
{
    constexpr std::string_view symbol = "cached_vhdl_language_profile";
    constexpr std::string_view compatibility = "fsim-synopsys-ieee-compat-v2";
    const std::array<std::uint32_t, 0> no_signals { };
    const LlvmJitOptions options {
        JitOptimizationLevel::o2, cache_directory
    };
    const auto make_process = [](
                                  std::string standard,
                                  std::string compatibility_profile) {
        Process process;
        process.id = 41;
        process.name = "cached_vhdl_language_profile";
        process.language_standard = std::move(standard);
        process.compatibility_profile = std::move(compatibility_profile);
        process.operations.emplace_back(Halt { });
        return process;
    };
    const auto materialize = [&](const Process& process,
                                 const std::uint64_t expected_hits,
                                 const std::uint64_t expected_misses) {
        LlvmJit jit { options };
        jit.add_process(symbol, process, no_signals);
        assert(jit.lookup(symbol));
        const auto statistics = jit.cache_statistics();
        assert(statistics.hits == expected_hits);
        assert(statistics.misses == expected_misses);
        assert(statistics.stores == expected_misses);
    };
    materialize(make_process("2008", std::string { compatibility }), 0, 1);
    materialize(make_process("2008", std::string { compatibility }), 1, 0);
    materialize(make_process("1993", std::string { compatibility }), 0, 1);
    materialize(make_process("2008", "changed-compatibility-profile"), 0, 1);
    assert(cached_object_count(cache_directory) == 3U);
}

} // namespace fsim::tests::compiler
