// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"

#include <optional>

namespace fsim::tests::compiler {

void test_process_control_cache_identity()
{
    const auto serial = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path()
        / ("fsim-control-cache-" + std::to_string(serial));
    std::error_code error;
    std::filesystem::remove_all(root, error);
    assert(!error);
    const std::array<std::uint32_t, 2> widths { 1, 1 };
    const auto make_process = [](const InstructionIndex equal_target) {
        Process process;
        process.id = 0;
        process.name = "control_cache";
        process.register_count = 3;
        process.register_value_kinds = {
            ValueKind::logic4, ValueKind::logic4, ValueKind::logic4
        };
        process.operations = {
            LoadConstant { 0, PackedLogic4::from_msb_string("0") },
            WaitOn { { 0, 1 }, { EdgeKind::any, EdgeKind::any } },
            ReadSignal { 1, 0 },
            Binary { BinaryOperator::case_equal, 2, 0, 1 },
            CopyRegister { 0, 1 },
            Branch {
                2, equal_target, 6,
                UnknownBranchPolicy::when_false },
            Halt { }
        };
        return process;
    };
    for (const auto optimization : {
             JitOptimizationLevel::o0,
             JitOptimizationLevel::o2 }) {
        const auto directory = root
            / (optimization == JitOptimizationLevel::o0 ? "o0" : "o2");
        const auto options = LlvmJitOptions { optimization, directory };
        {
            LlvmJit cold { options };
            cold.add_process("control_cache", make_process(1), widths);
            assert(cold.lookup("control_cache"));
            const auto statistics = cold.cache_statistics();
            assert(statistics.misses == 1 && statistics.stores == 1);
        }
        {
            LlvmJit warm { options };
            warm.add_process("control_cache", make_process(1), widths);
            assert(warm.lookup("control_cache"));
            const auto statistics = warm.cache_statistics();
            assert(statistics.hits == 1 && statistics.misses == 0);
        }
        {
            LlvmJit changed { options };
            changed.add_process("control_cache", make_process(0), widths);
            assert(changed.lookup("control_cache"));
            const auto statistics = changed.cache_statistics();
            assert(statistics.misses == 1 && statistics.stores == 1);
        }

        const auto fork_directory = directory / "fork";
        const auto fork_options = LlvmJitOptions { optimization, fork_directory };
        const auto make_fork_process = [](
                                           const InstructionIndex branch,
                                           const ForkJoinKind join,
                                           const std::optional<InstructionIndex> disable_site) {
            Process process;
            process.id = 0;
            process.name = "fork_cache";
            process.register_count = 1;
            process.operations = {
                Fork { { branch }, join },
                Jump { 5 },
                LoadConstant { 0, PackedLogic4::from_msb_string("1") },
                ForkEnd { },
                DisableFork { disable_site },
                Halt { }
            };
            return process;
        };
        {
            LlvmJit cold { fork_options };
            cold.add_process(
                "fork_cache",
                make_fork_process(2, ForkJoinKind::all, std::nullopt), { });
            assert(cold.lookup("fork_cache"));
            const auto statistics = cold.cache_statistics();
            assert(statistics.misses == 1 && statistics.stores == 1);
        }
        {
            LlvmJit warm { fork_options };
            warm.add_process(
                "fork_cache",
                make_fork_process(2, ForkJoinKind::all, std::nullopt), { });
            assert(warm.lookup("fork_cache"));
            const auto statistics = warm.cache_statistics();
            assert(statistics.hits == 1 && statistics.misses == 0);
        }
        {
            LlvmJit changed_branch { fork_options };
            changed_branch.add_process(
                "fork_cache",
                make_fork_process(3, ForkJoinKind::all, std::nullopt), { });
            assert(changed_branch.lookup("fork_cache"));
            const auto statistics = changed_branch.cache_statistics();
            assert(statistics.misses == 1 && statistics.stores == 1);
        }
        {
            LlvmJit changed_join { fork_options };
            changed_join.add_process(
                "fork_cache",
                make_fork_process(2, ForkJoinKind::any, std::nullopt), { });
            assert(changed_join.lookup("fork_cache"));
            const auto statistics = changed_join.cache_statistics();
            assert(statistics.misses == 1 && statistics.stores == 1);
        }
        {
            LlvmJit changed_disable_site { fork_options };
            changed_disable_site.add_process(
                "fork_cache",
                make_fork_process(2, ForkJoinKind::all, 0), { });
            assert(changed_disable_site.lookup("fork_cache"));
            const auto statistics = changed_disable_site.cache_statistics();
            assert(statistics.misses == 1 && statistics.stores == 1);
        }
        {
            LlvmJit invalid_disable_site {
                LlvmJitOptions {
                    optimization, fork_directory / "invalid-disable-site" }
            };
            expect_error(
                [&] {
                    invalid_disable_site.add_process(
                        "fork_cache_invalid",
                        make_fork_process(2, ForkJoinKind::all, 1), { });
                },
                "does not reference Fork");
        }

        const auto block_directory = directory / "named-block";
        const auto block_options = LlvmJitOptions { optimization, block_directory };
        const auto make_block_process = [](const InstructionIndex end) {
            Process process;
            process.id = 0;
            process.name = "named_block_cache";
            process.operations = {
                DisableBlock { 0, end },
                Halt { },
                Halt { }
            };
            return process;
        };
        {
            LlvmJit cold { block_options };
            cold.add_process(
                "named_block_cache", make_block_process(1), { });
            assert(cold.lookup("named_block_cache"));
            const auto statistics = cold.cache_statistics();
            assert(statistics.misses == 1 && statistics.stores == 1);
        }
        {
            LlvmJit warm { block_options };
            warm.add_process(
                "named_block_cache", make_block_process(1), { });
            assert(warm.lookup("named_block_cache"));
            const auto statistics = warm.cache_statistics();
            assert(statistics.hits == 1 && statistics.misses == 0);
        }
        {
            LlvmJit changed { block_options };
            changed.add_process(
                "named_block_cache", make_block_process(2), { });
            assert(changed.lookup("named_block_cache"));
            const auto statistics = changed.cache_statistics();
            assert(statistics.misses == 1 && statistics.stores == 1);
        }
        {
            LlvmJit invalid {
                LlvmJitOptions {
                    optimization, block_directory / "invalid-interval" }
            };
            auto invalid_process = make_block_process(1);
            invalid_process.operations.front() = DisableBlock { 1, 1 };
            expect_error(
                [&] {
                    invalid.add_process(
                        "named_block_invalid", invalid_process, { });
                },
                "invalid lexical interval");
        }
    }
    std::filesystem::remove_all(root, error);
    assert(!error);
}

} // namespace fsim::tests::compiler
