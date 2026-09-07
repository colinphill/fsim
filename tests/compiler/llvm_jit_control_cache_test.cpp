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
    const auto test_coverage_control_cache_identity =
        [&](const std::filesystem::path& cache_directory) {
            constexpr std::string_view symbol = "cached_coverage_control";
            const std::array<std::uint32_t, 0> no_signals { };
            const auto options = LlvmJitOptions {
                JitOptimizationLevel::o2, cache_directory
            };
            const auto materialize = [&](const RegisterId command,
                                         const RegisterId coverage_type,
                                         const RegisterId scope,
                                         const StringRegisterId selector,
                                         const std::string_view context,
                                         const bool selector_is_instance,
                                         const std::uint64_t hits,
                                         const std::uint64_t misses) {
                Process process;
                process.id = 132;
                process.name = symbol;
                process.register_count = 6;
                process.string_register_count = 2;
                process.operations = {
                    LoadConstant {
                        0, PackedLogic4::from_aval_bval(32, 0, 0) },
                    LoadConstant {
                        1, PackedLogic4::from_aval_bval(32, 22, 0) },
                    LoadConstant {
                        2, PackedLogic4::from_aval_bval(32, 10, 0) },
                    LoadConstant {
                        3, PackedLogic4::from_aval_bval(32, 11, 0) },
                    LoadStringConstant { 0, "top" },
                    LoadStringConstant { 1, "other" },
                    CoverageControl { 5, command, coverage_type, scope,
                        selector, std::string { context },
                        selector_is_instance },
                    Halt { },
                };
                LlvmJit jit { options };
                jit.add_process(symbol, process, no_signals);
                assert(jit.lookup(symbol));
                const auto statistics = jit.cache_statistics();
                assert(statistics.hits == hits);
                assert(statistics.misses == misses);
                assert(statistics.stores == misses);
            };
            materialize(0, 1, 2, 0, "top", true, 0, 1);
            materialize(0, 1, 2, 0, "top", true, 1, 0);
            materialize(3, 1, 2, 0, "top", true, 0, 1);
            materialize(0, 2, 2, 0, "top", true, 0, 1);
            materialize(0, 1, 3, 0, "top", true, 0, 1);
            materialize(0, 1, 2, 1, "top", true, 0, 1);
            materialize(0, 1, 2, 0, "other", true, 0, 1);
            materialize(0, 1, 2, 0, "top", false, 0, 1);
        };
    test_coverage_control_cache_identity(root / "coverage-control");
    const auto test_coverage_access_cache_identity =
        [&](const std::filesystem::path& cache_directory) {
            constexpr std::string_view symbol = "cached_coverage_access";
            const std::array<std::uint32_t, 0> no_signals { };
            const auto options = LlvmJitOptions {
                JitOptimizationLevel::o2, cache_directory };
            const auto materialize = [&](
                                         const SystemVerilogCoverageAccessKind kind,
                                         const std::optional<StringRegisterId> filename,
                                         const StringRegisterId selector,
                                         const std::string_view context,
                                         const bool selector_is_instance,
                                         const std::uint64_t hits,
                                         const std::uint64_t misses) {
                Process process;
                process.id = 139;
                process.name = symbol;
                process.register_count = 3;
                process.string_register_count = 2;
                const auto selected = kind
                        == SystemVerilogCoverageAccessKind::get
                    || kind == SystemVerilogCoverageAccessKind::get_max;
                process.operations = {
                    LoadConstant {
                        0, PackedLogic4::from_aval_bval(32, 22, 0) },
                    LoadConstant {
                        1, PackedLogic4::from_aval_bval(32, 10, 0) },
                    LoadStringConstant { 0, "coverage.fsimcov" },
                    LoadStringConstant { 1, "top" },
                    CoverageAccess { 2, kind, 0,
                        selected ? std::optional<RegisterId> { 1 }
                                 : std::nullopt,
                        selected ? std::optional<StringRegisterId> { selector }
                                 : std::nullopt,
                        selected ? std::string { context } : std::string { },
                        selected && selector_is_instance, filename },
                    Halt { },
                };
                LlvmJit jit { options };
                jit.add_process(symbol, process, no_signals);
                assert(jit.lookup(symbol));
                const auto statistics = jit.cache_statistics();
                assert(statistics.hits == hits);
                assert(statistics.misses == misses);
                assert(statistics.stores == misses);
            };
            materialize(SystemVerilogCoverageAccessKind::save, 0, 1,
                "top", true, 0, 1);
            materialize(SystemVerilogCoverageAccessKind::save, 0, 1,
                "top", true, 1, 0);
            materialize(SystemVerilogCoverageAccessKind::merge, 0, 1,
                "top", true, 0, 1);
            materialize(SystemVerilogCoverageAccessKind::get, std::nullopt,
                1, "top", true, 0, 1);
            materialize(SystemVerilogCoverageAccessKind::get, std::nullopt,
                1, "top", true, 1, 0);
            materialize(SystemVerilogCoverageAccessKind::get, std::nullopt,
                0, "top", true, 0, 1);
            materialize(SystemVerilogCoverageAccessKind::get, std::nullopt,
                1, "other", true, 0, 1);
            materialize(SystemVerilogCoverageAccessKind::get, std::nullopt,
                1, "top", false, 0, 1);
        };
    test_coverage_access_cache_identity(root / "coverage-access");
    const std::array<std::uint32_t, 2> widths { 1, 1 };
    const std::array<std::uint32_t, 1> sampled_widths { 1 };
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

        const auto exit_directory = directory / "program-exit";
        const auto exit_options = LlvmJitOptions {
            optimization, exit_directory
        };
        const auto make_halt_process = [](const bool program_exit) {
            Process process;
            process.id = 0;
            process.name = "program_exit_cache";
            process.operations = { Halt { program_exit } };
            return process;
        };
        {
            LlvmJit cold { exit_options };
            cold.add_process(
                "program_exit_cache", make_halt_process(false), { });
            assert(cold.lookup("program_exit_cache"));
            const auto statistics = cold.cache_statistics();
            assert(statistics.misses == 1 && statistics.stores == 1);
        }
        {
            LlvmJit warm { exit_options };
            warm.add_process(
                "program_exit_cache", make_halt_process(false), { });
            assert(warm.lookup("program_exit_cache"));
            const auto statistics = warm.cache_statistics();
            assert(statistics.hits == 1 && statistics.misses == 0);
        }
        {
            LlvmJit changed { exit_options };
            changed.add_process(
                "program_exit_cache", make_halt_process(true), { });
            assert(changed.lookup("program_exit_cache"));
            const auto statistics = changed.cache_statistics();
            assert(statistics.misses == 1 && statistics.stores == 1);
        }

        const auto sampled_directory = directory / "sampled-read";
        const auto sampled_options = LlvmJitOptions {
            optimization, sampled_directory
        };
        const auto make_sampled_process = [](
                                              const SignalReadKind kind,
                                              const std::uint32_t ticks,
                                              const std::optional<SignalId> clock
                                              = std::nullopt,
                                              const SampledClockEdge edge
                                              = SampledClockEdge::any,
                                              const std::optional<SignalId> gate
                                              = std::nullopt) {
            Process process;
            process.id = 0;
            process.name = "sampled_read_cache";
            process.register_count = 1;
            process.register_value_kinds = { ValueKind::logic4 };
            process.operations = {
                ReadSignal { 0, 0, kind, ticks, clock, edge, gate },
                Halt { }
            };
            return process;
        };
        {
            LlvmJit cold { sampled_options };
            cold.add_process(
                "sampled_read_cache",
                make_sampled_process(SignalReadKind::sampled, 1U),
                sampled_widths);
            assert(cold.lookup("sampled_read_cache"));
            const auto statistics = cold.cache_statistics();
            assert(statistics.misses == 1 && statistics.stores == 1);
        }
        {
            LlvmJit warm { sampled_options };
            warm.add_process(
                "sampled_read_cache",
                make_sampled_process(SignalReadKind::sampled, 1U),
                sampled_widths);
            assert(warm.lookup("sampled_read_cache"));
            const auto statistics = warm.cache_statistics();
            assert(statistics.hits == 1 && statistics.misses == 0);
        }
        {
            LlvmJit changed_kind { sampled_options };
            changed_kind.add_process(
                "sampled_read_cache",
                make_sampled_process(SignalReadKind::past, 1U),
                sampled_widths);
            assert(changed_kind.lookup("sampled_read_cache"));
            const auto statistics = changed_kind.cache_statistics();
            assert(statistics.misses == 1 && statistics.stores == 1);
        }
        {
            LlvmJit changed_ticks { sampled_options };
            changed_ticks.add_process(
                "sampled_read_cache",
                make_sampled_process(SignalReadKind::past, 2U),
                sampled_widths);
            assert(changed_ticks.lookup("sampled_read_cache"));
            const auto statistics = changed_ticks.cache_statistics();
            assert(statistics.misses == 1 && statistics.stores == 1);
        }
        {
            LlvmJit changed_clock { sampled_options };
            changed_clock.add_process(
                "sampled_read_cache",
                make_sampled_process(
                    SignalReadKind::past, 2U, 0U,
                    SampledClockEdge::positive),
                sampled_widths);
            assert(changed_clock.lookup("sampled_read_cache"));
            const auto statistics = changed_clock.cache_statistics();
            assert(statistics.misses == 1 && statistics.stores == 1);
        }
        {
            LlvmJit changed_edge { sampled_options };
            changed_edge.add_process(
                "sampled_read_cache",
                make_sampled_process(
                    SignalReadKind::past, 2U, 0U,
                    SampledClockEdge::negative),
                sampled_widths);
            assert(changed_edge.lookup("sampled_read_cache"));
            const auto statistics = changed_edge.cache_statistics();
            assert(statistics.misses == 1 && statistics.stores == 1);
        }
        {
            LlvmJit changed_gate { sampled_options };
            changed_gate.add_process(
                "sampled_read_cache",
                make_sampled_process(
                    SignalReadKind::past, 2U, 0U,
                    SampledClockEdge::negative, 0U),
                sampled_widths);
            assert(changed_gate.lookup("sampled_read_cache"));
            const auto statistics = changed_gate.cache_statistics();
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
