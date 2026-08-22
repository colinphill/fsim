// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"

namespace fsim::tests::compiler {

void test_code_coverage_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    LlvmJit jit { LlvmJitOptions { optimization, { } } };
    Process process;
    process.id = 47U;
    process.name = std::string { symbol };
    process.operations = {
        CodeCoverageHit {
            { 0x1020304050607080ULL, 0x0102030405060708ULL },
            ::fsim::runtime::CodeCoverageMetric::Statement,
            { 0U } },
        CodeCoverageHit {
            { 0x8877665544332211ULL, 0x8070605040302010ULL },
            ::fsim::runtime::CodeCoverageMetric::Branch,
            { 1U } },
        Halt { },
    };
    jit.add_process(symbol, process, { });
    const auto handle = jit.lookup(symbol);

    TestRuntime runtime;
    auto descriptor = abi(runtime);
    const std::array<std::uint32_t, 2> hit_counters { 5U, 2U };
    std::array<std::uint64_t, 6> counters { 0U, 0U, 7U, 0U, 0U, 41U };
    descriptor.code_coverage_hit_counters = hit_counters.data();
    descriptor.code_coverage_hit_count = hit_counters.size();
    descriptor.code_coverage_counter_values = counters.data();
    descriptor.code_coverage_counter_count = counters.size();

    assert(jit.execute(handle, descriptor) == JitExecutionStatus::completed);
    assert(counters[5] == 42U);
    assert(counters[2] == 8U);
    assert(runtime.code_coverage_checked_calls == 0U);

    counters[5] = std::numeric_limits<std::uint64_t>::max();
    assert(jit.execute(handle, descriptor) == JitExecutionStatus::completed);
    assert(counters[5] == std::numeric_limits<std::uint64_t>::max());
    assert(counters[2] == 9U);
    assert(runtime.code_coverage_checked_calls == 1U);
    assert(runtime.code_coverage_checked_counters
        == std::vector<std::uint32_t> { 5U });

    descriptor.code_coverage_counter_values = nullptr;
    descriptor.code_coverage_counter_count = 0U;
    runtime.code_coverage_callback_status = 1U;
    expect_generated_runtime_error(
        [&] { (void)jit.execute(handle, descriptor); },
        0U,
        JitGeneratedRuntimeErrorReason::coverage_callback_failure,
        "code coverage counter runtime callback failed");
}

void test_code_coverage_cache_identity()
{
    const auto serial
        = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path()
        / ("fsim-code-coverage-cache-" + std::to_string(serial));
    std::error_code error;
    std::filesystem::remove_all(root, error);
    assert(!error);

    Process process;
    process.id = 91U;
    process.name = "code_coverage_cache_identity";
    process.operations = { Halt { } };
    const auto materialize = [&](const std::string_view identity) {
        LlvmJitOptions options { JitOptimizationLevel::o2, root };
        options.code_coverage_identity = identity;
        LlvmJit jit { std::move(options) };
        jit.add_process(process.name, process, { });
        assert(jit.lookup(process.name));
        return jit.cache_statistics();
    };

    const auto cold_disabled = materialize("coverage-disabled");
    assert(cold_disabled.hits == 0U && cold_disabled.misses == 1U
        && cold_disabled.stores == 1U);
    const auto warm_disabled = materialize("coverage-disabled");
    assert(warm_disabled.hits == 1U && warm_disabled.misses == 0U
        && warm_disabled.stores == 0U);
    const auto cold_enabled = materialize("coverage-enabled");
    assert(cold_enabled.hits == 0U && cold_enabled.misses == 1U
        && cold_enabled.stores == 1U);

    std::size_t object_count = 0U;
    for (const auto& entry : std::filesystem::recursive_directory_iterator {
             root }) {
        object_count += entry.is_regular_file()
            && entry.path().extension() == ".fobj";
    }
    assert(object_count == 2U);
    std::filesystem::remove_all(root, error);
    assert(!error);
}

} // namespace fsim::tests::compiler
