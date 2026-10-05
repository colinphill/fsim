// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"
#include "fsim/compiler/fused_masked_process.hpp"

namespace fsim::tests::compiler {

using fsim::compiler::JitBackendTierHint;

void test_debug_point_instrumentation()
{
    const std::array<std::uint32_t, 0> no_signals { };
    const auto check_kind =
        [&](const DebugPointKind kind,
            const std::string_view kind_name) {
            Process process;
            process.id = 0;
            process.name = "debug_point";
            process.operations = {
                DebugPoint {
                    kind,
                    SourceLocation { "debug_point.sv", 7, 3 } },
                Halt { },
            };
            const auto run =
                [&](const JitOptimizationLevel optimization,
                    const std::string& symbol) {
                    LlvmJit jit { LlvmJitOptions { optimization, { } } };
                    jit.add_process(symbol, process, no_signals);
                    const auto handle = jit.lookup(symbol);
                    std::array<std::uint64_t, 0> aval { };
                    std::array<std::uint64_t, 0> bval { };
                    std::array<std::uint8_t, 0> initialized { };
                    fsim_jit_frame_v2 frame { };
                    jit.initialize_frame(
                        handle, frame, aval, bval, initialized);
                    fsim_jit_resume_result_v2 result {
                        FSIM_JIT_RESUME_RESULT_ABI_VERSION_V2,
                        static_cast<std::uint32_t>(
                            sizeof(fsim_jit_resume_result_v2)),
                        0,
                        FSIM_JIT_INVALID_INSTRUCTION_V2,
                        0,
                    };
                    TestRuntime runtime;
                    auto descriptor = abi(runtime);
                    auto short_descriptor = descriptor;
                    short_descriptor.struct_size = static_cast<std::uint32_t>(
                        offsetof(fsim_jit_runtime_instance_v2, flags));
                    expect_error(
                        [&] {
                            (void)jit.resume(
                                handle, short_descriptor, frame, result);
                        },
                        "runtime-instance ABI structure is too small");
                    assert(
                        jit.resume(handle, descriptor, frame, result)
                        == JitResumeStatus::completed);
                    assert(result.instruction == 1);
                    jit.initialize_frame(
                        handle, frame, aval, bval, initialized);
                    descriptor.flags = FSIM_JIT_RUNTIME_FLAG_DEBUG_POINTS_V2;
                    assert(
                        jit.resume(handle, descriptor, frame, result)
                        == JitResumeStatus::debug_point);
                    assert(result.instruction == 0);
                    assert(frame.program_counter == 1);
                    assert(
                        jit.resume(handle, descriptor, frame, result)
                        == JitResumeStatus::completed);
                };
            run(
                JitOptimizationLevel::o0,
                "debug_point_" + std::string { kind_name } + "_o0");
            run(
                JitOptimizationLevel::o2,
                "debug_point_" + std::string { kind_name } + "_o2");
        };
    check_kind(DebugPointKind::statement, "statement");
    check_kind(DebugPointKind::call, "call");
}

[[nodiscard]] Process make_cached_process(const std::string_view value)
{
    Process process;
    process.id = 11;
    process.name = "cached_process";
    process.register_count = 1;
    process.operations = {
        LoadConstant { 0, PackedLogic4::from_msb_string(value) },
        WriteBlocking { 0, 0 },
        Halt { },
    };
    return process;
}

[[nodiscard]] Process
make_cached_signal_process(const SignalId signal)
{
    Process process;
    process.id = 12;
    process.name = "cached_signal_process";
    process.register_count = 1;
    process.operations = {
        ReadSignal { 0, signal },
        Halt { },
    };
    return process;
}

[[nodiscard]] Process make_cached_required_read_process()
{
    Process process;
    process.id = 1201U;
    process.name = "cached_required_read_process";
    process.register_count = 4U;
    process.operations = {
        ReadSignal { 0U, 0U },
        CopyRegister { 1U, 0U },
        ReadSignal { 2U, 0U },
        Binary { BinaryOperator::bit_and, 3U, 1U, 2U },
        Halt { },
    };
    return process;
}

[[nodiscard]] fsim::compiler::FusedMaskedProcess
make_cached_fused_required_read_process()
{
    constexpr std::uint32_t width = 8U;
    std::array<Process, 2> members;
    members[0].id = 1701U;
    members[0].name = "tiered_fused_member_a";
    members[0].initialize = true;
    members[0].register_count = 9U;
    members[0].static_sensitivity = {
        { 0U, EdgeKind::any }, { 1U, EdgeKind::any }
    };
    members[0].driver_regions = { { 2U, 0U, width - 1U, false } };
    members[0].operations = {
        ReadSignal { 0U, 0U },
        CopyRegister { 6U, 0U },
        ReadSignal { 7U, 0U },
        Binary { BinaryOperator::bit_and, 8U, 6U, 7U },
        ReadSignal { 1U, 1U },
        Binary { BinaryOperator::bit_xor, 2U, 8U, 1U },
        Extract { 3U, 2U, 0U, width - 1U },
        WriteUpdateSlice { 2U, 3U, 0U },
        Binary { BinaryOperator::bit_or, 4U, 8U, 1U },
        Extract { 5U, 4U, 0U, width - 1U },
        WriteUpdateSlice { 2U, 5U, 0U },
        WaitSensitivity { },
        Jump { 0U },
    };
    members[1].id = 1702U;
    members[1].name = "tiered_fused_member_b";
    members[1].initialize = true;
    members[1].register_count = 8U;
    members[1].static_sensitivity = {
        { 0U, EdgeKind::any }, { 1U, EdgeKind::any }
    };
    members[1].driver_regions = {
        { 2U, width - 1U, 1U, false }
    };
    members[1].operations = {
        ReadSignal { 0U, 0U },
        ReadSignal { 1U, 1U },
        Binary { BinaryOperator::bit_and, 2U, 0U, 1U },
        Reduction { ReductionOperator::bit_xor, 3U, 2U },
        CopyRegister { 4U, 3U },
        LoadConstant { 5U, PackedLogic4(1U, Logic4::z) },
        Concatenate { 6U, { 4U, 5U }, 2U },
        Extract { 7U, 6U, 1U, 1U },
        WriteUpdateSlice { 2U, 7U, width - 1U },
        WaitSensitivity { },
        Jump { 0U },
    };
    const std::array<const Process*, 2> pointers {
        &members[0], &members[1]
    };
    constexpr std::array<std::uint32_t, 3> widths {
        width, width, width
    };
    constexpr std::array<ValueKind, 3> kinds {
        ValueKind::logic4, ValueKind::logic4, ValueKind::logic4
    };
    auto fused = fsim::compiler::fuse_masked_processes(
        pointers, widths, kinds, 99U);
    assert(fused);
    return std::move(*fused);
}

void run_cached_required_read_process(
    LlvmJit& jit, const std::string_view symbol)
{
    const auto handle = jit.lookup(symbol);
    const auto layout = jit.frame_layout(handle);
    std::vector<std::uint64_t> register_aval(layout.register_word_count);
    std::vector<std::uint64_t> register_bval(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v2 frame { };
    jit.initialize_frame(handle, frame, register_aval, register_bval,
        initialized);

    TestRuntime callbacks;
    auto runtime = abi(callbacks);
    std::array<std::uint64_t, 1> signal_aval { UINT64_C(0xa5) };
    std::array<std::uint64_t, 1> signal_bval { 0U };
    runtime.direct_signal_aval = signal_aval.data();
    runtime.direct_signal_bval = signal_bval.data();
    runtime.direct_signal_count = 1U;
    runtime.direct_read_signals = layout.direct_read_signals.data();
    runtime.direct_read_signal_count = static_cast<std::uint32_t>(
        layout.direct_read_signals.size());
    auto result = new_resume_result();
    assert(jit.resume(handle, runtime, frame, result)
        == JitResumeStatus::completed);
    assert(register_aval[0U] == UINT64_C(0xa5));
    assert(register_bval[0U] == 0U);
    assert(register_aval[layout.register_word_offsets[3U]]
        == UINT64_C(0xa5));
    assert(register_bval[layout.register_word_offsets[3U]] == 0U);
}

[[nodiscard]] Process
make_cached_scheduled_process(const bool delayed,
    const std::uint64_t delay)
{
    Process process;
    process.id = 13;
    process.name = "cached_scheduled_process";
    process.register_count = 1;
    process.operations = {
        LoadConstant { 0, PackedLogic4::from_msb_string("10100101") },
        delayed ? Operation { WriteAfter { 0, 0, delay } }
                : Operation { WriteUpdate { 0, 0 } },
        Halt { },
    };
    return process;
}

[[nodiscard]] Process make_cached_inertial_process(
    const TransitionDelays delays)
{
    Process process;
    process.id = 16;
    process.name = "cached_inertial_process";
    process.register_count = 1;
    process.operations = {
        LoadConstant { 0, PackedLogic4::from_msb_string("10100101") },
        WriteInertial { 0, 0, delays },
        Halt { },
    };
    return process;
}

[[nodiscard]] Process make_cached_projected_process(
    const std::uint64_t delay,
    const std::uint64_t rejection,
    const ProjectedDelayMode mode)
{
    Process process;
    process.id = 17;
    process.name = "cached_projected_process";
    process.register_count = 1;
    process.operations = {
        LoadConstant { 0, PackedLogic4::from_msb_string("10100101") },
        WriteProjected { 0, 0, delay, rejection, mode },
        Halt { },
    };
    return process;
}

[[nodiscard]] Process make_cached_projected_waveform_process(
    const std::uint64_t second_delay,
    const std::uint64_t rejection,
    const ProjectedDelayMode mode)
{
    Process process;
    process.id = 18;
    process.name = "cached_projected_waveform_process";
    process.register_count = 2;
    process.operations = {
        LoadConstant { 0, PackedLogic4::from_msb_string("10100101") },
        LoadConstant { 1, PackedLogic4::from_msb_string("01011010") },
        WriteProjectedWaveform {
            0, { { 0, 5 }, { 1, second_delay } }, rejection, mode },
        Halt { },
    };
    return process;
}

[[nodiscard]] Process
make_cached_wait_process(const bool static_wait,
    std::vector<SignalId> signals,
    std::vector<EdgeKind> edges = { })
{
    Process process;
    process.id = 14;
    process.name = "cached_wait_process";
    for (const auto signal : signals) {
        process.static_sensitivity.push_back({ signal, EdgeKind::any });
    }
    if (static_wait) {
        process.operations = { WaitSensitivity { }, Halt { } };
    } else {
        process.operations = {
            WaitOn { std::move(signals), std::move(edges) }, Halt { }
        };
    }
    return process;
}

[[nodiscard]] Process make_cached_assertion_process(
    const AssertionSeverity severity,
    const std::uint32_t source_line)
{
    Process process;
    process.id = 15;
    process.name = "cached_assertion_process";
    process.register_count = 1;
    process.operations = {
        LoadConstant { 0, PackedLogic4::from_msb_string("1") },
        Assert {
            0,
            "cached assertion",
            severity,
            SourceLocation { "cache_assertion.sv", source_line, 3 } },
        Halt { },
    };
    return process;
}

[[nodiscard]] Process make_cached_debug_point_process(
    const DebugPointKind kind, const std::uint32_t source_line,
    const std::string_view scope = "cached_debug_point_process")
{
    Process process;
    process.id = 16;
    process.name = "cached_debug_point_process";
    process.operations = { DebugPoint {
                               kind, SourceLocation { "cache_debug_point.sv", source_line, 5 },
                               std::string { scope } },
        Halt { } };
    return process;
}

void expect_cache_statistics(const LlvmJit& jit, const std::uint64_t hits,
    const std::uint64_t misses,
    const std::uint64_t stores,
    const std::uint64_t rejected_entries = 0)
{
    const auto statistics = jit.cache_statistics();
    assert(statistics.hits == hits);
    assert(statistics.misses == misses);
    assert(statistics.stores == stores);
    assert(statistics.rejected_entries == rejected_entries);
    assert(statistics.load_failures == 0);
    assert(statistics.store_failures == 0);
    assert(statistics.prune_failures == 0);
}

void run_cached_process(LlvmJit& jit, const std::string_view symbol,
    const EncodedSignal expected)
{
    const auto handle = jit.lookup(symbol);
    TestRuntime runtime;
    auto descriptor = abi(runtime);
    assert(jit.execute(handle, descriptor) == JitExecutionStatus::completed);
    assert(runtime.signals[0] == expected);
}

void run_cached_signal_process(LlvmJit& jit,
    const std::string_view symbol)
{
    const auto handle = jit.lookup(symbol);
    TestRuntime runtime;
    runtime.signals[0] = { UINT64_C(0xa5), 0 };
    runtime.signals[1] = { UINT64_C(0x3c), 0 };
    auto descriptor = abi(runtime);
    assert(jit.execute(handle, descriptor) == JitExecutionStatus::completed);
}

void run_cached_assertion_process(
    LlvmJit& jit, const std::string_view symbol)
{
    TestRuntime runtime;
    auto descriptor = abi(runtime);
    assert(
        jit.execute(jit.lookup(symbol), descriptor)
        == JitExecutionStatus::completed);
    assert(runtime.assertion_count == 0);
}

void run_cached_scheduled_process(
    LlvmJit& jit, const std::string_view symbol,
    const ScheduledWriteKind expected_kind,
    const std::uint64_t expected_delay)
{
    const auto handle = jit.lookup(symbol);
    TestRuntime runtime;
    auto descriptor = abi(runtime);
    assert(jit.execute(handle, descriptor) == JitExecutionStatus::completed);
    assert(runtime.scheduled_writes.size() == 1);
    assert(runtime.scheduled_writes.front().kind == expected_kind);
    assert(runtime.scheduled_writes.front().delay == expected_delay);
}

void materialize_cached_wait_process(
    LlvmJit& jit, const std::string_view symbol)
{
    assert(jit.lookup(symbol));
}

[[nodiscard]] std::vector<std::filesystem::path>
cached_object_paths(const std::filesystem::path& root)
{
    std::vector<std::filesystem::path> result;
    if (!std::filesystem::exists(root)) {
        return result;
    }
    for (const auto& entry :
        std::filesystem::recursive_directory_iterator { root }) {
        if (entry.is_regular_file() && entry.path().extension() == ".fobj") {
            result.push_back(entry.path());
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

struct CachedWideProjectedWriteCapture {
    std::uint32_t calls { };
    std::uint32_t signal { UINT32_MAX };
    std::uint32_t width { };
    std::uint32_t callback_status { UINT32_MAX };
    bool logic9_planes_are_null { };
    std::vector<std::uint64_t> aval_words;
    std::vector<std::uint64_t> bval_words;
};

extern "C" std::uint32_t capture_cached_wide_projected_write(
    void* opaque, const std::uint32_t signal, const std::uint32_t width,
    const std::uint64_t* aval_words, const std::uint64_t* bval_words,
    const std::uint64_t* logic9_plane2_words,
    const std::uint64_t* logic9_plane3_words)
{
    auto* const capture
        = static_cast<CachedWideProjectedWriteCapture*>(opaque);
    if (capture == nullptr || width == 0U || aval_words == nullptr
        || bval_words == nullptr) {
        return 1U;
    }
    const auto word_count
        = (static_cast<std::size_t>(width) + 63U) / 64U;
    try {
        ++capture->calls;
        capture->signal = signal;
        capture->width = width;
        capture->callback_status = 0U;
        capture->logic9_planes_are_null = logic9_plane2_words == nullptr
            && logic9_plane3_words == nullptr;
        capture->aval_words.assign(aval_words, aval_words + word_count);
        capture->bval_words.assign(bval_words, bval_words + word_count);
    } catch (...) {
        capture->callback_status = 1U;
        return 1U;
    }
    return 0U;
}

[[nodiscard]] Process make_cached_wide_projected_process(
    const PackedLogic4& value)
{
    Process process;
    process.id = 2301U;
    process.name = "cached_wide_projected";
    process.register_count = 1U;
    process.register_value_kinds = { ValueKind::logic4 };
    process.operations = {
        LoadConstant { 0U, value },
        WriteProjected {
            0U, 0U, 0U, 0U, ProjectedDelayMode::inertial },
        Halt { },
    };
    return process;
}

void test_cached_wide_projected_write(
    const JitOptimizationLevel optimization,
    const std::filesystem::path& cache_directory)
{
    constexpr std::uint32_t width = 129U;
    constexpr std::array<std::uint64_t, 3> aval_words {
        UINT64_C(0x0123456789abcdef),
        UINT64_C(0xfedcba9876543210),
        UINT64_MAX,
    };
    constexpr std::array<std::uint64_t, 3> bval_words {
        UINT64_C(0x1010101010101010),
        UINT64_C(0x0101010101010101),
        UINT64_MAX,
    };
    const auto expected = PackedLogic4::from_word_planes(
        width, aval_words, bval_words);
    assert(expected.aval_words().back() == 1U);
    assert(expected.bval_words().back() == 1U);
    constexpr std::array<std::uint32_t, 1> signal_widths { width };
    constexpr std::string_view symbol = "cached_wide_projected_write";
    const auto process = make_cached_wide_projected_process(expected);
    const auto options = LlvmJitOptions { optimization, cache_directory };

    const auto execute_and_check = [&](LlvmJit& jit) {
        TestRuntime runtime;
        CachedWideProjectedWriteCapture capture;
        auto descriptor = abi(runtime);
        auto services = *descriptor.services;
        services.write_signal_packed = nullptr;
        services.write_projected_signal_packed
            = capture_cached_wide_projected_write;
        descriptor.services = &services;
        descriptor.context = &capture;
        assert(services.write_signal_packed == nullptr);
        assert(services.write_projected_signal_packed != nullptr);
        assert(jit.execute(jit.lookup(symbol), descriptor)
            == JitExecutionStatus::completed);
        assert(capture.calls == 1U);
        assert(capture.signal == 0U);
        assert(capture.width == width);
        assert(capture.callback_status == 0U);
        assert(capture.logic9_planes_are_null);
        assert(std::ranges::equal(capture.aval_words, expected.aval_words()));
        assert(std::ranges::equal(capture.bval_words, expected.bval_words()));
    };

    {
        LlvmJit cold { options };
        cold.add_process(symbol, process, signal_widths);
        execute_and_check(cold);
        expect_cache_statistics(cold, 0U, 1U, 1U);
    }
    assert(cached_object_paths(cache_directory).size() == 1U);

    {
        LlvmJit warm { options };
        warm.add_process(symbol, process, signal_widths);
        TestRuntime runtime;
        CachedWideProjectedWriteCapture capture;
        auto descriptor = abi(runtime);
        auto services = *descriptor.services;
        services.write_signal_packed = nullptr;
        services.write_projected_signal_packed = nullptr;
        descriptor.services = &services;
        descriptor.context = &capture;
        expect_error(
            [&] { (void)warm.execute(warm.lookup(symbol), descriptor); },
            "write_projected_signal_packed");
        assert(capture.calls == 0U);

        execute_and_check(warm);
        expect_cache_statistics(warm, 1U, 0U, 0U);
    }
}

struct BoundSignalCallbackCapture {
    std::array<std::uint32_t, 2U> signals { UINT32_MAX, UINT32_MAX };
    std::array<std::uint64_t, 2U> aval { };
    std::array<std::uint64_t, 2U> bval { };
    std::uint32_t read_signal { UINT32_MAX };
    std::uint32_t read_calls { };
    std::uint32_t calls { };
};

extern "C" std::uint64_t capture_bound_signal_read(
    void* opaque,
    const std::uint32_t signal,
    std::uint64_t* bval) noexcept
{
    auto* const capture = static_cast<BoundSignalCallbackCapture*>(opaque);
    if (capture == nullptr || capture->read_calls != 0U || bval == nullptr) {
        return 0U;
    }
    capture->read_signal = signal;
    ++capture->read_calls;
    *bval = 0U;
    return 1U;
}

extern "C" std::uint32_t capture_bound_signal_read_packed(
    void* opaque,
    const std::uint32_t signal,
    const std::uint32_t width,
    std::uint64_t* aval,
    std::uint64_t* bval,
    std::uint64_t* logic9_plane2,
    std::uint64_t* logic9_plane3) noexcept
{
    auto* const capture = static_cast<BoundSignalCallbackCapture*>(opaque);
    if (capture == nullptr || capture->read_calls != 0U || width != 1U
        || aval == nullptr || bval == nullptr) {
        return 1U;
    }
    capture->read_signal = signal;
    ++capture->read_calls;
    aval[0U] = 1U;
    bval[0U] = 0U;
    if (logic9_plane2 != nullptr) {
        logic9_plane2[0U] = 0U;
    }
    if (logic9_plane3 != nullptr) {
        logic9_plane3[0U] = 0U;
    }
    return 0U;
}

extern "C" void capture_bound_signal_write(
    void* opaque,
    const std::uint32_t signal,
    const std::uint64_t aval,
    const std::uint64_t bval) noexcept
{
    auto* const capture = static_cast<BoundSignalCallbackCapture*>(opaque);
    if (capture == nullptr || capture->calls >= capture->signals.size()) {
        return;
    }
    const auto index = capture->calls++;
    capture->signals[index] = signal;
    capture->aval[index] = aval;
    capture->bval[index] = bval;
}

extern "C" std::uint32_t capture_bound_signal_callback(
    void* opaque,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const std::uint32_t mode,
    const std::uint64_t delay,
    const std::uint64_t* aval,
    const std::uint64_t* bval,
    const std::uint64_t* logic9_plane2,
    const std::uint64_t* logic9_plane3,
    const std::uint32_t update_domain) noexcept
{
    static_cast<void>(mode);
    static_cast<void>(delay);
    static_cast<void>(update_domain);
    auto* const capture = static_cast<BoundSignalCallbackCapture*>(opaque);
    if (capture == nullptr || capture->calls >= capture->signals.size()
        || offset != 0U || width != 1U || aval == nullptr || bval == nullptr
        || logic9_plane2 != nullptr || logic9_plane3 != nullptr) {
        return 1U;
    }
    const auto index = capture->calls++;
    capture->signals[index] = signal;
    capture->aval[index] = aval[0U];
    capture->bval[index] = bval[0U];
    return 0U;
}

[[nodiscard]] Process make_bound_signal_callback_process()
{
    Process process;
    process.id = 2302U;
    process.name = "bound_signal_callback_operands";
    process.register_count = 4U;
    process.register_value_kinds = {
        ValueKind::logic4,
        ValueKind::logic4,
        ValueKind::logic4,
        ValueKind::logic4,
    };
    process.operations = {
        LoadConstant { 0U, PackedLogic4::from_msb_string("1") },
        WriteBlocking { 7U, 0U },
        ReadSignal { 1U, 65U },
        LoadConstant { 2U, PackedLogic4 { 32U, Logic4::zero } },
        DynamicExtract { 3U, 1U, DynamicIndex { 2U, 0, 0, 0U } },
        WriteBlocking { 65U, 3U },
        Halt { },
    };
    return process;
}

void test_cached_bound_signal_callback_operands(
    const JitOptimizationLevel optimization,
    const std::filesystem::path& cache_directory)
{
    constexpr std::string_view actual_symbol = "bound_signal_actual_ids";
    constexpr std::string_view canonical_symbol = "bound_signal_canonical_ids";
    auto process = make_bound_signal_callback_process();
    std::array<std::uint32_t, 1001U> signal_widths { };
    signal_widths.fill(1U);
    const auto options = LlvmJitOptions { optimization, cache_directory };

    const auto add_entries = [&](LlvmJit& jit) {
        JitProcessModuleEntry actual_entry;
        actual_entry.symbol = actual_symbol;
        actual_entry.process = &process;
        actual_entry.signal_callback_ids_are_actual = true;

        auto canonical_entry = actual_entry;
        canonical_entry.symbol = canonical_symbol;
        canonical_entry.signal_callback_ids_are_actual = false;
        const std::array entries { actual_entry, canonical_entry };
        jit.add_process_module(
            "bound-signal-callback-operands",
            entries,
            signal_widths);
    };

    const auto execute_and_check = [&](LlvmJit& jit) {
        const auto actual_handle = jit.lookup(actual_symbol);
        const auto canonical_handle = jit.lookup(canonical_symbol);
        const auto actual_layout = jit.frame_layout(actual_handle);
        const auto canonical_layout = jit.frame_layout(canonical_handle);
        assert(actual_layout.signal_callback_ids_are_actual);
        assert(!canonical_layout.signal_callback_ids_are_actual);
        assert((actual_layout.signal_callback_operands
            == std::vector<std::uint32_t> { 7U, 65U }));
        assert(canonical_layout.signal_callback_operands.empty());
        assert(actual_layout.signal_callback_operand_word_base == 4U);
        assert(canonical_layout.signal_callback_operand_word_base == 4U);
        assert(actual_layout.register_count == canonical_layout.register_count);
        assert(actual_layout.register_count == 4U);
        assert(actual_layout.register_word_count == 6U);
        assert(canonical_layout.register_word_count == 4U);
        assert(actual_layout.layout_id_low != canonical_layout.layout_id_low
            || actual_layout.layout_id_high
                != canonical_layout.layout_id_high);

        std::vector<std::uint64_t> actual_aval(
            actual_layout.register_word_count);
        std::vector<std::uint64_t> actual_bval(
            actual_layout.register_word_count);
        std::vector<std::uint8_t> actual_initialized(
            actual_layout.register_count);
        fsim_jit_frame_v2 actual_frame { };
        jit.initialize_frame(
            actual_handle,
            actual_frame,
            actual_aval,
            actual_bval,
            actual_initialized);
        assert(actual_aval[4U] == 7U && actual_aval[5U] == 65U);
        assert(actual_bval[4U] == 0U && actual_bval[5U] == 0U);

        std::vector<std::uint64_t> short_aval(
            actual_layout.register_word_count - 1U);
        std::vector<std::uint64_t> short_bval(
            actual_layout.register_word_count - 1U);
        fsim_jit_frame_v2 short_frame { };
        expect_error(
            [&] {
                jit.initialize_frame(
                    actual_handle,
                    short_frame,
                    short_aval,
                    short_bval,
                    actual_initialized);
            },
            "caller-owned JIT register storage is smaller than the frame layout");

        TestRuntime mismatch_runtime;
        auto mismatch_descriptor = abi(mismatch_runtime);
        BoundSignalCallbackCapture mismatch_capture;
        auto mismatch_services = *mismatch_descriptor.services;
        mismatch_services.write_signal = capture_bound_signal_write;
        mismatch_services.write_signal_packed
            = capture_bound_signal_callback;
        mismatch_descriptor.services = &mismatch_services;
        mismatch_descriptor.context = &mismatch_capture;
        auto mismatch_result = new_resume_result();
        expect_error(
            [&] {
                (void)jit.resume(
                    canonical_handle,
                    mismatch_descriptor,
                    actual_frame,
                    mismatch_result);
            },
            "JIT frame layout mismatch");
        assert(mismatch_capture.calls == 0U);

        const auto run_entry = [&](const JitProcessHandle handle,
                                   const fsim::compiler::JitProcessFrameLayout& layout,
                                   const bool bind_actual_ids) {
            std::vector<std::uint64_t> aval(layout.register_word_count);
            std::vector<std::uint64_t> bval(layout.register_word_count);
            std::vector<std::uint8_t> initialized(layout.register_count);
            fsim_jit_frame_v2 frame { };
            jit.initialize_frame(handle, frame, aval, bval, initialized);
            if (bind_actual_ids) {
                aval[layout.signal_callback_operand_word_base] = 65U;
                aval[layout.signal_callback_operand_word_base + 1U]
                    = 1000U;
            }

            TestRuntime runtime;
            auto descriptor = abi(runtime);
            BoundSignalCallbackCapture capture;
            auto services = *descriptor.services;
            services.read_signal = capture_bound_signal_read;
            services.read_signal_packed
                = capture_bound_signal_read_packed;
            services.write_signal = capture_bound_signal_write;
            services.write_signal_packed = capture_bound_signal_callback;
            descriptor.services = &services;
            descriptor.context = &capture;
            auto result = new_resume_result();
            assert(jit.resume(handle, descriptor, frame, result)
                == JitResumeStatus::completed);
            assert(capture.read_calls == 1U);
            assert(capture.read_signal
                == (bind_actual_ids ? 1000U : 65U));
            assert(capture.calls == 2U);
            assert((capture.signals == (bind_actual_ids
                ? std::array<std::uint32_t, 2U> { 65U, 1000U }
                : std::array<std::uint32_t, 2U> { 7U, 65U })));
            assert((capture.aval
                == std::array<std::uint64_t, 2U> { 1U, 1U }));
            assert((capture.bval
                == std::array<std::uint64_t, 2U> { 0U, 0U }));
        };
        run_entry(actual_handle, actual_layout, true);
        run_entry(canonical_handle, canonical_layout, false);
        return std::array { actual_layout, canonical_layout };
    };

    std::array<fsim::compiler::JitProcessFrameLayout, 2U> original_layouts;
    {
        LlvmJit cold { options };
        add_entries(cold);
        original_layouts = execute_and_check(cold);
        expect_cache_statistics(cold, 0U, 1U, 1U);
    }
    assert(cached_object_paths(cache_directory).size() == 1U);

    {
        LlvmJit warm { options };
        add_entries(warm);
        const auto warm_layouts = execute_and_check(warm);
        assert(warm_layouts == original_layouts);
        expect_cache_statistics(warm, 1U, 0U, 0U);
    }
    assert(cached_object_paths(cache_directory).size() == 1U);
}

void expect_native_record_header(const std::filesystem::path& cache_directory,
    const std::filesystem::path& object_path)
{
    fsim::compiler::ObjectCache storage {
        cache_directory / "llvm" / "objects"
    };
    std::error_code error;
    const auto record = storage.load(object_path.stem().string(), error);
    assert(record && !error && record->size() >= 44U);
    constexpr std::array record_magic {
        std::byte { 'F' }, std::byte { 'S' }, std::byte { 'I' },
        std::byte { 'M' }, std::byte { 'J' }, std::byte { 'O' },
        std::byte { '3' }, std::byte { 0 }
    };
    constexpr std::array metadata_magic {
        std::byte { 'F' }, std::byte { 'S' }, std::byte { 'I' },
        std::byte { 'M' }, std::byte { 'J' }, std::byte { 'M' },
        std::byte { '5' }, std::byte { 0 }
    };
    assert(std::equal(record_magic.begin(), record_magic.end(), record->begin()));
    assert(std::equal(metadata_magic.begin(), metadata_magic.end(),
        record->begin() + 24));

    const auto u32_le = [&](const std::size_t offset) {
        std::uint32_t result { };
        for (std::size_t byte = 0U; byte < 4U; ++byte) {
            result |= static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>((*record)[offset + byte]))
                << (byte * 8U);
        }
        return result;
    };
    assert(u32_le(8U) == 1U && u32_le(32U) == 9U);
    const auto metadata_size = static_cast<std::size_t>(u32_le(12U));
    assert(metadata_size >= 37U && metadata_size <= record->size() - 24U);
    assert(u32_le(36U) == metadata_size);
    const auto identity_size = static_cast<std::size_t>(u32_le(40U));
    assert(identity_size != 0U && identity_size <= metadata_size - 37U);
    const auto backend_tier_offset = 44U + identity_size;
    assert(u32_le(backend_tier_offset) == 0U);
    assert(std::to_integer<std::uint8_t>(
               (*record)[backend_tier_offset + 4U]) == 0U);
    const auto object_offset = (24U + metadata_size + 7U) & ~std::size_t { 7U };
    assert(object_offset < record->size());
    const auto object_size = static_cast<std::uint64_t>(u32_le(16U))
        | (static_cast<std::uint64_t>(u32_le(20U)) << 32U);
    assert(object_size == record->size() - object_offset);
    assert(std::all_of(record->begin() + static_cast<std::ptrdiff_t>(
        24U + metadata_size),
        record->begin() + static_cast<std::ptrdiff_t>(object_offset),
        [](const std::byte value) { return value == std::byte { 0 }; }));
}

struct CachedTieredReadDedupMetadata {
    std::uint32_t tier { };
    std::uint64_t selection_instructions { };
    std::uint64_t emitted_instructions { };
    std::uint64_t marked_loads { };
    std::uint64_t eliminated_loads { };
    std::uint64_t marked_value_loads { };
    std::uint64_t eliminated_value_loads { };
};

[[nodiscard]] CachedTieredReadDedupMetadata
read_tiered_read_dedup_metadata(
    const std::filesystem::path& cache_directory,
    const std::filesystem::path& selected_object = { })
{
    const auto paths = cached_object_paths(cache_directory);
    assert(!paths.empty());
    assert(paths.size() == 1U || !selected_object.empty());
    const auto object_path = selected_object.empty()
        ? paths.front() : selected_object;
    assert(std::ranges::find(paths, object_path) != paths.end());
    fsim::compiler::ObjectCache storage {
        cache_directory / "llvm" / "objects"
    };
    std::error_code error;
    const auto record = storage.load(object_path.stem().string(), error);
    assert(record && !error && record->size() >= 45U);
    const auto read_u32 = [&](const std::size_t offset) {
        assert(offset + 4U <= record->size());
        std::uint32_t value { };
        for (std::size_t byte = 0U; byte < 4U; ++byte) {
            value |= static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>((*record)[offset + byte]))
                << (byte * 8U);
        }
        return value;
    };
    const auto read_u64 = [&](const std::size_t offset) {
        assert(offset + 8U <= record->size());
        std::uint64_t value { };
        for (std::size_t byte = 0U; byte < 8U; ++byte) {
            value |= static_cast<std::uint64_t>(
                std::to_integer<std::uint8_t>((*record)[offset + byte]))
                << (byte * 8U);
        }
        return value;
    };
    constexpr std::size_t cache_identity_size_offset = 40U;
    const auto identity_size = static_cast<std::size_t>(
        read_u32(cache_identity_size_offset));
    assert(identity_size > 0U);
    const auto tier_offset = 44U + identity_size;
    CachedTieredReadDedupMetadata result;
    result.tier = read_u32(tier_offset);
    const auto selection_offset = tier_offset + 5U;
    result.selection_instructions = read_u64(selection_offset);
    result.emitted_instructions = read_u64(selection_offset + 8U);
    result.marked_loads = read_u64(selection_offset + 16U);
    result.eliminated_loads = read_u64(selection_offset + 24U);
    result.marked_value_loads = read_u64(selection_offset + 32U);
    result.eliminated_value_loads = read_u64(selection_offset + 40U);
    return result;
}

void test_cache_identity_mismatch_diagnostics(
    const std::filesystem::path& cache_directory)
{
    const auto corrupt_embedded_identity = [](
        const std::filesystem::path& directory) {
        const auto paths = cached_object_paths(directory);
        assert(paths.size() == 1U);
        fsim::compiler::ObjectCache storage {
            directory / "llvm" / "objects"
        };
        std::error_code error;
        auto record = storage.load(paths.front().stem().string(), error);
        assert(record && !error && record->size() >= 45U);
        const auto u32_le = [&](const std::size_t offset) {
            std::uint32_t result { };
            for (std::size_t byte = 0U; byte < 4U; ++byte) {
                result |= static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>((*record)[offset + byte]))
                    << (byte * 8U);
            }
            return result;
        };
        const auto metadata_size = static_cast<std::size_t>(u32_le(12U));
        const auto identity_size = static_cast<std::size_t>(u32_le(40U));
        assert(metadata_size >= 37U);
        assert(metadata_size <= record->size() - 24U);
        assert(identity_size != 0U && identity_size <= metadata_size - 37U);
        (*record)[44U] ^= std::byte { 1U };
        assert(storage.store(
            paths.front().stem().string(),
            std::span<const std::byte> { *record }, error));
        assert(!error);
    };

    const auto corrupt_first_register_width = [](
        const std::filesystem::path& directory) {
        const auto paths = cached_object_paths(directory);
        assert(paths.size() == 1U);
        fsim::compiler::ObjectCache storage {
            directory / "llvm" / "objects"
        };
        std::error_code error;
        auto record = storage.load(paths.front().stem().string(), error);
        assert(record && !error && record->size() >= 86U);
        const auto u32_le = [&](const std::size_t offset) {
            std::uint32_t result { };
            for (std::size_t byte = 0U; byte < 4U; ++byte) {
                result |= static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>((*record)[offset + byte]))
                    << (byte * 8U);
            }
            return result;
        };
        const auto metadata_size = static_cast<std::size_t>(u32_le(12U));
        const auto identity_size = static_cast<std::size_t>(u32_le(40U));
        assert(metadata_size >= 37U);
        assert(metadata_size <= record->size() - 24U);
        assert(identity_size <= metadata_size - 37U);
        // Schema 7 adds one tier-selection count and four deduplication
        // counters before the per-process layout (five 64-bit fields).
        const auto process_offset = 101U + identity_size;
        const auto register_widths_offset = process_offset + 30U;
        assert(register_widths_offset + 8U <= 24U + metadata_size);
        assert(u32_le(register_widths_offset) == 1U);
        const auto first_width_offset = register_widths_offset + 4U;
        assert(first_width_offset + 4U <= 24U + metadata_size);
        assert(u32_le(first_width_offset) == 4U);
        for (std::size_t byte = 0U; byte < 4U; ++byte) {
            (*record)[first_width_offset + byte] = std::byte { 0U };
        }
        assert(storage.store(
            paths.front().stem().string(),
            std::span<const std::byte> { *record }, error));
        assert(!error);
    };

    const auto corrupt_first_register_persistence = [](
        const std::filesystem::path& directory) {
        const auto paths = cached_object_paths(directory);
        assert(paths.size() == 1U);
        fsim::compiler::ObjectCache storage {
            directory / "llvm" / "objects"
        };
        std::error_code error;
        auto record = storage.load(paths.front().stem().string(), error);
        assert(record && !error && record->size() >= 45U);
        const auto u32_le = [&](const std::size_t offset) {
            std::uint32_t result { };
            for (std::size_t byte = 0U; byte < 4U; ++byte) {
                result |= static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>((*record)[offset + byte]))
                    << (byte * 8U);
            }
            return result;
        };
        const auto metadata_size = static_cast<std::size_t>(u32_le(12U));
        assert(metadata_size >= 37U);
        assert(metadata_size <= record->size() - 24U);
        const auto persistence_offset = 24U + metadata_size - 1U;
        assert((*record)[persistence_offset] == std::byte { 1U });
        (*record)[persistence_offset] = std::byte { 2U };
        assert(storage.store(
            paths.front().stem().string(),
            std::span<const std::byte> { *record }, error));
        assert(!error);
    };

    const auto verify = [&](const bool immutable_identity) {
        const auto directory = cache_directory
            / (immutable_identity ? "immutable" : "ordinary");
        Process process;
        process.id = immutable_identity ? 212U : 211U;
        process.name = immutable_identity
            ? "immutable_cache_identity_mismatch"
            : "ordinary_cache_identity_mismatch";
        process.operations = { Halt { } };
        const auto options = LlvmJitOptions {
            JitOptimizationLevel::o2, directory
        };
        const auto set_identity = [&](LlvmJit& jit) {
            if (immutable_identity) {
                jit.set_immutable_design_identity(
                    "cache-identity-mismatch-design");
            }
        };
        {
            LlvmJit warm { options };
            set_identity(warm);
            warm.add_process(process.name, process, { });
            assert(warm.lookup(process.name));
        }
        corrupt_embedded_identity(directory);
        LlvmJit incompatible { options };
        set_identity(incompatible);
        expect_error(
            [&] {
                incompatible.add_process(process.name, process, { });
            },
            "cached LLVM native object has incompatible ABI, semantics, "
            "optimization-tier, or target identity");
    };

    verify(false);
    verify(true);

    const auto layout_directory = cache_directory / "used-register-layout";
    Process process;
    process.id = 213U;
    process.name = "used_register_cache_layout";
    process.register_count = 1U;
    process.operations = {
        LoadConstant { 0U, PackedLogic4::from_msb_string("1010") },
        Halt { }
    };
    fsim::runtime::simir::DebugLocal persistent_register;
    persistent_register.name = "persistent_value";
    persistent_register.type_name = "logic [3:0]";
    persistent_register.register_id = 0U;
    persistent_register.width = 4U;
    process.debug_locals.push_back(std::move(persistent_register));
    const auto options = LlvmJitOptions {
        JitOptimizationLevel::o2, layout_directory
    };
    {
        LlvmJit warm { options };
        warm.add_process(process.name, process, { });
        const auto handle = warm.lookup(process.name);
        assert(handle);
        assert((warm.frame_layout(handle).register_values_persistent
            == std::vector<std::uint8_t> { 1U }));
    }
    corrupt_first_register_persistence(layout_directory);
    LlvmJit incompatible { options };
    expect_error(
        [&] { incompatible.add_process(process.name, process, { }); },
        "cached LLVM native object metadata does not match its compiled "
        "process module");

    const auto width_layout_directory
        = cache_directory / "used-register-width-layout";
    auto width_process = process;
    width_process.id = 214U;
    width_process.name = "used_register_width_cache_layout";
    const auto width_options = LlvmJitOptions {
        JitOptimizationLevel::o2, width_layout_directory
    };
    {
        LlvmJit warm { width_options };
        warm.add_process(width_process.name, width_process, { });
        assert(warm.lookup(width_process.name));
    }
    corrupt_first_register_width(width_layout_directory);
    LlvmJit bad_width_cache { width_options };
    expect_error(
        [&] {
            bad_width_cache.add_process(
                width_process.name, width_process, { });
        },
        "cached LLVM native object metadata does not match its compiled "
        "process module");
}

void test_process_module_grouping_at_level(
    const JitOptimizationLevel optimization,
    const std::filesystem::path& cache_directory)
{
    const auto make_process =
        [](const ProcessId id, const SignalId signal,
            const std::string_view value) {
            Process process;
            process.id = id;
            process.name = "grouped_process_" + std::to_string(id);
            process.register_count = 1;
            process.operations = {
                LoadConstant {
                    0, PackedLogic4::from_msb_string(value) },
                WriteBlocking { signal, 0 },
                Halt { },
            };
            return process;
        };
    const std::array<std::uint32_t, 2> widths { 8, 8 };
    auto first = make_process(20, 0, "10100101");
    fsim::runtime::simir::DebugLocal visible_register;
    visible_register.name = "visible_value";
    visible_register.type_name = "logic [7:0]";
    visible_register.register_id = 0U;
    visible_register.width = 8U;
    first.debug_locals.push_back(std::move(visible_register));
    const auto second = make_process(21, 1, "01011010");
    const auto add_group =
        [&](LlvmJit& jit, const Process& left,
            const Process& right) {
            const std::array entries {
                JitProcessModuleEntry { "grouped_first", &left },
                JitProcessModuleEntry { "grouped_second", &right },
            };
            jit.add_process_module(
                "work.grouped@top", entries, widths);
        };
    const auto execute_group =
        [](LlvmJit& jit) {
            TestRuntime runtime;
            auto descriptor = abi(runtime);
            const auto first_handle = jit.lookup("grouped_first");
            const auto second_handle = jit.lookup("grouped_second");
            assert(first_handle != second_handle);
            assert(
                jit.execute(first_handle, descriptor)
                == JitExecutionStatus::completed);
            assert(
                jit.execute(second_handle, descriptor)
                == JitExecutionStatus::completed);
            assert((
                runtime.signals[0]
                == EncodedSignal { UINT64_C(0xa5), 0 }));
            assert((
                runtime.signals[1]
                == EncodedSignal { UINT64_C(0x5a), 0 }));
            const auto first_layout = jit.frame_layout(first_handle);
            const auto second_layout = jit.frame_layout(second_handle);
            assert(first_layout.register_values_persistent.size()
                == first_layout.register_count);
            assert(second_layout.register_values_persistent.size()
                == second_layout.register_count);
            assert((first_layout.register_values_persistent
                == std::vector<std::uint8_t> { 1U }));
            assert((second_layout.register_values_persistent
                == std::vector<std::uint8_t> { 0U }));
            return std::array {
                first_layout,
                second_layout,
            };
        };

    std::array<fsim::compiler::JitProcessFrameLayout, 2>
        original_layouts;
    LlvmJitOptions cache_options { optimization, cache_directory };
    cache_options.debug_instrumentation = false;
    {
        LlvmJit cold { cache_options };
        assert(cold.supports_process(first, widths));
        assert(cold.supports_process(second, widths));
        add_group(cold, first, second);
        original_layouts = execute_group(cold);
        expect_cache_statistics(cold, 0, 1, 1);
    }
    assert(cached_object_paths(cache_directory).size() == 1);

    {
        LlvmJit warm { cache_options };
        add_group(warm, first, second);
        const auto warm_layouts = execute_group(warm);
        assert(warm_layouts == original_layouts);
        expect_cache_statistics(warm, 1, 0, 0);
    }
    assert(cached_object_paths(cache_directory).size() == 1);

    // Any changed member invalidates the specialization object, while an
    // unchanged member retains its process-local frame identity.
    {
        LlvmJit changed { cache_options };
        const auto changed_second = make_process(21, 1, "00111100");
        add_group(changed, first, changed_second);
        const auto first_handle = changed.lookup("grouped_first");
        const auto second_handle = changed.lookup("grouped_second");
        assert(
            changed.frame_layout(first_handle)
            == original_layouts[0]);
        assert(
            changed.frame_layout(second_handle)
            != original_layouts[1]);
        expect_cache_statistics(changed, 0, 1, 1);
    }
    assert(cached_object_paths(cache_directory).size() == 2);

    {
        LlvmJit rejected;
        const std::array<JitProcessModuleEntry, 0> empty { };
        expect_fatal_error(
            [&] {
                rejected.add_process_module(
                    "empty", empty, widths);
            },
            "module cannot be empty");
        const std::array null_entry {
            JitProcessModuleEntry { "missing", nullptr }
        };
        expect_fatal_error(
            [&] {
                rejected.add_process_module(
                    "null", null_entry, widths);
            },
            "has no SimIR process");
        const std::array duplicate_entries {
            JitProcessModuleEntry { "duplicate", &first },
            JitProcessModuleEntry { "duplicate", &second },
        };
        expect_fatal_error(
            [&] {
                rejected.add_process_module(
                    "duplicates", duplicate_entries, widths);
            },
            "duplicate LLVM process symbol");

        Process supported;
        supported.id = 22;
        supported.name = "supported";
        supported.operations = { Halt { } };
        Process too_wide;
        too_wide.id = 23;
        too_wide.name = "too_wide";
        too_wide.register_count = 1;
        too_wide.register_value_kinds = { ValueKind::logic9 };
        too_wide.operations = {
            LoadConstant {
                0,
                PackedLogic4::from_logic9_msb_string(
                    std::string(65, '0')) },
            Halt { }
        };
        const std::array<std::uint32_t, 1> wide_widths { 65 };
        assert(rejected.supports_process(supported, wide_widths));
        assert(rejected.supports_process(too_wide, wide_widths));
        Process exact_register;
        exact_register.id = 24;
        exact_register.name = "exact_register";
        exact_register.register_count = 1;
        exact_register.register_value_kinds = {
            ValueKind::logic9
        };
        exact_register.operations = {
            LoadConstant {
                0,
                PackedLogic4::from_logic9_msb_string("W") },
            Halt { }
        };
        assert(rejected.supports_process(
            exact_register, std::array<std::uint32_t, 0> { }));
        Process exact_signal_access;
        exact_signal_access.id = 25;
        exact_signal_access.name = "exact_signal_access";
        exact_signal_access.register_count = 1;
        exact_signal_access.operations = {
            ReadSignal { 0, 0 }, Halt { }
        };
        const std::array<std::uint32_t, 1> scalar_widths { 1 };
        const std::array<ValueKind, 1> exact_signal_kinds {
            ValueKind::logic9
        };
        assert(rejected.supports_process(
            exact_signal_access,
            scalar_widths,
            exact_signal_kinds));
        Process exact_wide_drivers;
        exact_wide_drivers.id = 26;
        exact_wide_drivers.name = "exact_wide_drivers";
        exact_wide_drivers.register_count = 4;
        const DynamicIndex bit_selection { 2, 256, 0, 0 };
        const DynamicPartIndex part_selection {
            2, 256, 0, 0, 129, true, true
        };
        exact_wide_drivers.operations = {
            LoadConstant { 0, PackedLogic4 { 257, Logic4::zero } },
            LoadConstant { 1, PackedLogic4 { 129, Logic4::one } },
            LoadConstant { 2, PackedLogic4::from_aval_bval(32, 72, 0) },
            LoadConstant { 3, PackedLogic4 { 1, Logic4::one } },
            WriteBlockingSlice { 0, 1, 72 },
            WriteBlockingDynamicSlice { 0, 3, bit_selection },
            WriteBlockingDynamicPartSlice { 0, 1, part_selection },
            WriteUpdateDynamicSlice { 0, 3, bit_selection },
            WriteAfter { 0, 0, 1 },
            WriteAfterSlice { 0, 1, 72, 1 },
            WriteAfterDynamicSlice { 0, 3, bit_selection, 1 },
            WriteAfterDynamicPartSlice { 0, 1, part_selection, 1 },
            WriteInertialDynamicSlice {
                0, 3, bit_selection, TransitionDelays { 1, 1, 1 } },
            ForceSignalSlice { 0, 0, 0, std::nullopt, false },
            ForceSignalSlice { 0, 1, 72, std::nullopt, false },
            ForceSignalSlice { 0, 3, 0, bit_selection, false },
            ReleaseSignalSlice { 0, 0, 257, std::nullopt, false },
            ReleaseSignalSlice { 0, 72, 129, std::nullopt, false },
            ReleaseSignalSlice { 0, 0, 1, bit_selection, false },
            Halt { }
        };
        const std::array<std::uint32_t, 1> exact_wide_widths { 257 };
        const std::array<ValueKind, 1> exact_wide_kinds { ValueKind::logic4 };
        assert(rejected.supports_process(
            exact_wide_drivers, exact_wide_widths, exact_wide_kinds));
        Process exact_wide_callable_frame;
        exact_wide_callable_frame.id = 27;
        exact_wide_callable_frame.name = "exact_wide_callable_frame";
        exact_wide_callable_frame.register_count = 2;
        exact_wide_callable_frame.operations = {
            LoadConstant { 0, PackedLogic4 { 137, Logic4::zero } },
            LoadConstant { 1, PackedLogic4 { 137, Logic4::one } },
            CallableFramePush { 1, { 0, 1 }, { }, { } },
            CallableFramePop { 1, { 1 }, { }, { } },
            Halt { }
        };
        assert(rejected.supports_process(
            exact_wide_callable_frame,
            std::array<std::uint32_t, 0> { }));
        auto duplicate_callable_frame = exact_wide_callable_frame;
        duplicate_callable_frame.id = 28;
        duplicate_callable_frame.operations[2] = CallableFramePush {
            1, { 0, 0 }, { }, { }
        };
        expect_fatal_error(
            [&] {
                (void)rejected.supports_process(
                    duplicate_callable_frame,
                    std::array<std::uint32_t, 0> { });
            },
            "automatic callable frame repeats a packed register");
        auto zero_identity_callable_frame = exact_wide_callable_frame;
        zero_identity_callable_frame.id = 29;
        zero_identity_callable_frame.operations[2] = CallableFramePush {
            0, { 0, 1 }, { }, { }
        };
        expect_fatal_error(
            [&] {
                (void)rejected.supports_process(
                    zero_identity_callable_frame,
                    std::array<std::uint32_t, 0> { });
            },
            "automatic callable frame identity must be nonzero");
        Process unsupported_register_abi;
        unsupported_register_abi.id = 30;
        unsupported_register_abi.name = "unsupported_register_abi";
        unsupported_register_abi.register_count
            = static_cast<std::size_t>(
                  std::numeric_limits<RegisterId>::max())
            + 1U;
        unsupported_register_abi.operations = {
            Halt { }
        };
        const std::array unsupported_entries {
            JitProcessModuleEntry { "eligible", &supported },
            JitProcessModuleEntry {
                "unsupported", &unsupported_register_abi },
        };
        expect_unsupported(
            [&] {
                rejected.add_process_module(
                    "unsupported-member", unsupported_entries,
                    wide_widths);
            },
            "too many registers for the JIT ABI");
        expect_error(
            [&] { (void)rejected.lookup("eligible"); },
            "was not added");

        const std::array first_entry {
            JitProcessModuleEntry { "registered", &first }
        };
        rejected.add_process_module(
            "same-module", first_entry, widths);
        const std::array second_entry {
            JitProcessModuleEntry { "new_symbol", &second }
        };
        expect_fatal_error(
            [&] {
                rejected.add_process_module(
                    "same-module", second_entry, widths);
            },
            "duplicate LLVM process module identity");
        expect_error(
            [&] { (void)rejected.lookup("new_symbol"); },
            "was not added");
    }
}

void test_canonical_operation_cache_identity(
    const std::filesystem::path& cache_directory)
{
    constexpr std::string_view symbol = "canonical_operation_cache";
    const auto options = LlvmJitOptions {
        JitOptimizationLevel::o0, cache_directory
    };
    const auto make_process = [] {
        Process process;
        process.id = 190;
        process.name = "canonical_operation_cache";
        process.static_sensitivity = { { 0, EdgeKind::any } };
        process.operations = { WaitSensitivity { }, Halt { } };
        return process;
    };
    const auto materialize = [&](const Process& process,
                                 const std::array<std::uint32_t, 2>& widths,
                                 const std::uint64_t hits,
                                 const std::uint64_t misses) {
        LlvmJit jit { options };
        jit.add_process(symbol, process, widths);
        assert(jit.lookup(symbol));
        expect_cache_statistics(jit, hits, misses, misses);
    };
    const std::array<std::uint32_t, 2> widths { 1, 1 };
    const auto process = make_process();
    materialize(process, widths, 0, 1);
    materialize(process, widths, 1, 0);

    // The static wait has no fields, but its sensitivity still depends on
    // the referenced signal profile. Unrelated signals remain out of the key.
    materialize(process, { 1, 2 }, 1, 0);
    materialize(process, { 2, 1 }, 0, 1);

    const std::array<std::uint32_t, 2> range_widths { 8U, 1U };
    materialize(process, range_widths, 0, 1);
    auto changed_range = make_process();
    changed_range.static_sensitivity.front().offset = 1U;
    changed_range.static_sensitivity.front().width = 2U;
    materialize(changed_range, range_widths, 0, 1);

    auto changed_field = make_process();
    fsim::runtime::simir::operation_get<Halt>(
        changed_field.operations.back()).program_exit = true;
    materialize(changed_field, widths, 0, 1);

    // Different empty operation alternatives require distinct stable tags.
    auto changed_tag = make_process();
    changed_tag.operations.front() = WaitForever { };
    materialize(changed_tag, widths, 0, 1);

    // A remapped executor has no native body of its own. Releasing its
    // preflight summary must leave future standalone compilation valid.
    LlvmJit remapped { options };
    remapped.set_immutable_design_identity("prevalidated-remap-test");
    assert(remapped.supports_process(process, widths));
    assert(remapped.discard_prevalidated_process(process));
    assert(!remapped.discard_prevalidated_process(process));
    remapped.add_process(symbol, process, widths);
    assert(remapped.lookup(symbol));
}

void test_cohort_binding_retention()
{
    LlvmJit jit { LlvmJitOptions {
        JitOptimizationLevel::o0, { } } };
    Process process;
    process.id = 191U;
    process.name = "cohort_binding_retention";
    process.register_count = 1U;
    process.static_sensitivity = { { 0U, EdgeKind::posedge } };
    process.operations = {
        LoadConstant { 0U, PackedLogic4::from_msb_string("1") },
        WriteBlocking { 0U, 0U },
        WaitSensitivity { },
        Jump { 0U }
    };
    const std::array<std::uint32_t, 1> widths { 1U };
    jit.add_process("cohort_retention_a", process, widths);
    jit.add_process("cohort_retention_b", process, widths);
    const std::array handles {
        jit.lookup("cohort_retention_a"),
        jit.lookup("cohort_retention_b")
    };
    std::array<fsim_jit_frame_v2, 2> frames { };
    std::array<std::array<std::uint64_t, 1>, 2> register_aval { };
    std::array<std::array<std::uint64_t, 1>, 2> register_bval { };
    std::array<std::array<std::uint8_t, 1>, 2> register_initialized { };
    std::array<fsim_jit_resume_result_v2, 2> results {
        new_resume_result(), new_resume_result()
    };
    std::array<TestRuntime, 2> runtimes;
    std::array<fsim_jit_runtime_instance_v2, 2> descriptors {
        abi(runtimes[0]), abi(runtimes[1])
    };
    std::array<fsim::compiler::JitProcessCohortResumeEntry, 2> entries {
        fsim::compiler::JitProcessCohortResumeEntry {
            jit.bind(handles[0]), descriptors[0], frames[0], results[0] },
        fsim::compiler::JitProcessCohortResumeEntry {
            jit.bind(handles[1]), descriptors[1], frames[1], results[1] }
    };
    const auto reset_frames = [&] {
        for (std::size_t index = 0; index < handles.size(); ++index) {
            jit.initialize_frame(handles[index], frames[index],
                register_aval[index], register_bval[index],
                register_initialized[index]);
            results[index] = new_resume_result();
        }
    };
    reset_frames();
    assert(jit.resume_cohort_prevalidated(entries) == entries.size());
    assert(std::ranges::all_of(entries, [](const auto& entry) {
        return entry.status
            == FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY_V2;
    }));
    fsim::compiler::JitProcessCohortBinding previous;
    for (std::size_t attempt = 0; attempt < 32U; ++attempt) {
        const auto binding = jit.bind_cohort_prevalidated(entries);
        assert(jit.active_cohort_binding_count() == 1U);
        if (previous) {
            assert(!jit.release_cohort_binding(previous));
        }
        reset_frames();
        assert(jit.resume_cohort_prevalidated(binding, entries)
            == entries.size());
        assert(std::ranges::all_of(entries, [](const auto& entry) {
            return entry.status
                == FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY_V2;
        }));
        assert(jit.release_cohort_binding(binding));
        assert(jit.active_cohort_binding_count() == 0U);
        expect_error([&] {
            (void)jit.resume_cohort_prevalidated(binding, entries);
        }, "stale prevalidated LLVM cohort binding");
        previous = binding;
    }
}

void test_object_cache_at_level(const JitOptimizationLevel optimization,
    const std::filesystem::path& cache_directory)
{
    const std::array<std::uint32_t, 2> widths { 8, 1 };
    constexpr std::string_view symbol = "persistent_cache_process";
    const auto options = LlvmJitOptions { optimization, cache_directory };

    {
        LlvmJit cold { options };
        cold.add_process(symbol, make_cached_process("10100101"), widths);
        expect_cache_statistics(cold, 0, 0, 0);
        run_cached_process(cold, symbol,
            EncodedSignal { UINT64_C(0xa5), 0 });
        expect_cache_statistics(cold, 0, 1, 1);
    }
    assert(cached_object_paths(cache_directory).size() == 1);
    expect_native_record_header(
        cache_directory, cached_object_paths(cache_directory).front());
    {
        const auto metadata = read_tiered_read_dedup_metadata(cache_directory);
        assert(metadata.tier == 0U);
        assert(metadata.selection_instructions
            == metadata.emitted_instructions);
        assert(metadata.marked_loads == 0U);
        assert(metadata.eliminated_loads == 0U);
        assert(metadata.marked_value_loads == 0U);
        assert(metadata.eliminated_value_loads == 0U);
    }

    {
        LlvmJit warm { options };
        warm.add_process(symbol, make_cached_process("10100101"), widths);
        run_cached_process(warm, symbol,
            EncodedSignal { UINT64_C(0xa5), 0 });
        expect_cache_statistics(warm, 1, 0, 0);
    }

    const auto cached_objects = cached_object_paths(cache_directory);
    assert(cached_objects.size() == 1);
    {
        std::ofstream stream { cached_objects.front(),
            std::ios::binary | std::ios::trunc };
        assert(stream);
        stream << "corrupt";
        stream.flush();
        assert(stream);
    }
    {
        LlvmJit recovered { options };
        recovered.add_process(symbol, make_cached_process("10100101"), widths);
        run_cached_process(recovered, symbol,
            EncodedSignal { UINT64_C(0xa5), 0 });
        expect_cache_statistics(recovered, 0, 1, 1, 1);
    }
    {
        LlvmJit healed { options };
        healed.add_process(symbol, make_cached_process("10100101"), widths);
        run_cached_process(healed, symbol,
            EncodedSignal { UINT64_C(0xa5), 0 });
        expect_cache_statistics(healed, 1, 0, 0);
    }

    // A checksum-valid payload can still be an incompatible native object.
    // The LLVM adapter validates its object structure before handing it to ORC.
    {
        const std::array incompatible {
            std::byte { 0xde }, std::byte { 0xad }, std::byte { 0xbe }, std::byte { 0xef }
        };
        fsim::compiler::ObjectCache storage {
            cache_directory / "llvm" / "objects"
        };
        std::error_code error;
        assert(storage.store(cached_objects.front().stem().string(),
            incompatible, error));
        assert(!error);
    }
    {
        LlvmJit recovered { options };
        recovered.add_process(symbol, make_cached_process("10100101"), widths);
        run_cached_process(recovered, symbol,
            EncodedSignal { UINT64_C(0xa5), 0 });
        expect_cache_statistics(recovered, 0, 1, 1, 1);
    }

    // Executable SimIR contents participate in the key.
    {
        LlvmJit changed_process { options };
        changed_process.add_process(
            symbol, make_cached_process("00111100"), widths);
        run_cached_process(changed_process, symbol,
            EncodedSignal { UINT64_C(0x3c), 0 });
        expect_cache_statistics(changed_process, 0, 1, 1);
    }
    assert(cached_object_paths(cache_directory).size() == 2);

    const auto debug_container_cache
        = cache_directory.parent_path()
            / (cache_directory.filename().string()
                + "-debug-container-local-observer");
    const auto make_debug_container_process =
        [](const ContainerRegisterId observed_register) {
            ContainerType type;
            type.element_width = 8U;
            type.fixed = false;
            type.index_left = 0;
            type.index_right = 1;
            Process process;
            process.id = 93U;
            process.name = "debug_container_observer_cache";
            process.register_count = 2U;
            process.container_register_count = 2U;
            process.container_register_types = { type, type };
            process.debug_container_locals.push_back(
                { "snapshot", observed_register, type, { } });
            process.operations = {
                LoadConstant {
                    0U, PackedLogic4::from_aval_bval(32U, 0U, 0U) },
                ReadContainerObject { 0U, 0U },
                ContainerRead { 1U, 0U, 0U, true, false, false },
                Halt { },
            };
            return process;
        };
    const auto materialize_debug_container_process =
        [&](const Process& process,
            const std::uint64_t hits,
            const std::uint64_t misses) {
            LlvmJitOptions debug_container_options {
                optimization, debug_container_cache };
            debug_container_options.debug_instrumentation = false;
            LlvmJit jit { std::move(debug_container_options) };
            jit.add_process(
                "debug_container_observer_cache", process, { });
            assert(jit.lookup("debug_container_observer_cache"));
            expect_cache_statistics(jit, hits, misses, misses);
        };
    // Only the debugger-visible container-register mapping changes, and it
    // changes whether the generated read may leave the register unmaterialized.
    materialize_debug_container_process(
        make_debug_container_process(1U), 0U, 1U);
    materialize_debug_container_process(
        make_debug_container_process(0U), 0U, 1U);
    materialize_debug_container_process(
        make_debug_container_process(0U), 1U, 0U);
    assert(cached_object_paths(debug_container_cache).size() == 2U);

    const auto tiered_cache_directory
        = cache_directory.parent_path()
            / (cache_directory.filename().string() + "-tiered-read-dedup");
    constexpr std::string_view tiered_symbol
        = "persistent_cache_tiered_required_read";
    constexpr std::array<std::uint32_t, 1> tiered_widths { 8U };
    constexpr std::array<ValueKind, 1> tiered_kinds {
        ValueKind::logic4
    };
    {
        LlvmJit cold { LlvmJitOptions {
            optimization, tiered_cache_directory } };
        cold.add_process(tiered_symbol, make_cached_required_read_process(),
            tiered_widths, tiered_kinds,
            JitBackendTierHint::fused_static_cohort, 1U, true, true);
        run_cached_required_read_process(cold, tiered_symbol);
        expect_cache_statistics(cold, 0U, 1U, 1U);
    }
    assert(cached_object_paths(tiered_cache_directory).size() == 1U);
    const auto cold_tiered_metadata
        = read_tiered_read_dedup_metadata(tiered_cache_directory);
    assert(cold_tiered_metadata.tier == 1U);
    assert(cold_tiered_metadata.marked_value_loads >= 2U);
    assert(cold_tiered_metadata.eliminated_value_loads >= 1U);
    assert(cold_tiered_metadata.marked_loads
        > cold_tiered_metadata.eliminated_loads);
    assert(cold_tiered_metadata.emitted_instructions
        < cold_tiered_metadata.selection_instructions);
    const auto original_tiered_object
        = cached_object_paths(tiered_cache_directory).front();
    {
        LlvmJit warm { LlvmJitOptions {
            optimization, tiered_cache_directory } };
        warm.add_process(tiered_symbol, make_cached_required_read_process(),
            tiered_widths, tiered_kinds,
            JitBackendTierHint::fused_static_cohort, 1U, true, true);
        run_cached_required_read_process(warm, tiered_symbol);
        // A hit proves both per-entry flags survived the native metadata
        // round-trip and matched the source module on reload.
        expect_cache_statistics(warm, 1U, 0U, 0U);
    }
    const auto warm_tiered_metadata
        = read_tiered_read_dedup_metadata(tiered_cache_directory);
    assert(warm_tiered_metadata.tier == cold_tiered_metadata.tier);
    assert(warm_tiered_metadata.selection_instructions
        == cold_tiered_metadata.selection_instructions);
    assert(warm_tiered_metadata.emitted_instructions
        == cold_tiered_metadata.emitted_instructions);
    assert(warm_tiered_metadata.marked_value_loads
        == cold_tiered_metadata.marked_value_loads);
    assert(warm_tiered_metadata.eliminated_value_loads
        == cold_tiered_metadata.eliminated_value_loads);
    {
        LlvmJit changed_policy { LlvmJitOptions {
            optimization, tiered_cache_directory } };
        changed_policy.add_process(
            tiered_symbol, make_cached_required_read_process(),
            tiered_widths, tiered_kinds,
            JitBackendTierHint::fused_static_cohort, 1U, true, false);
        run_cached_required_read_process(changed_policy, tiered_symbol);
        expect_cache_statistics(changed_policy, 0U, 1U, 1U);
    }
    const auto tiered_object_paths
        = cached_object_paths(tiered_cache_directory);
    const auto guarded_object = std::ranges::find_if(
        tiered_object_paths, [&](const auto& path) {
            return path != original_tiered_object;
        });
    assert(guarded_object != tiered_object_paths.end());
    const auto guarded_tiered_metadata = read_tiered_read_dedup_metadata(
        tiered_cache_directory, *guarded_object);
    assert(guarded_tiered_metadata.tier == 1U);
    assert(guarded_tiered_metadata.marked_loads == 0U);
    assert(guarded_tiered_metadata.eliminated_loads == 0U);
    assert(guarded_tiered_metadata.marked_value_loads == 0U);
    assert(guarded_tiered_metadata.eliminated_value_loads == 0U);
    assert(guarded_tiered_metadata.emitted_instructions
        == guarded_tiered_metadata.selection_instructions);
    assert(cached_object_paths(tiered_cache_directory).size() == 2U);

    const auto fused_tiered_cache_directory
        = cache_directory.parent_path()
            / (cache_directory.filename().string() + "-tiered-fused-read-dedup");
    constexpr std::string_view fused_tiered_symbol
        = "persistent_cache_fused_tiered_required_read";
    constexpr std::array<std::uint32_t, 3> fused_tiered_widths {
        8U, 8U, 8U
    };
    constexpr std::array<ValueKind, 3> fused_tiered_kinds {
        ValueKind::logic4, ValueKind::logic4, ValueKind::logic4
    };
    const auto fused_required_read
        = make_cached_fused_required_read_process();
    auto fused_options = LlvmJitOptions {
        optimization, fused_tiered_cache_directory
    };
    fused_options.debug_instrumentation = false;
    {
        LlvmJit fused_jit { fused_options };
        fused_jit.add_masked_process(
            fused_tiered_symbol, fused_required_read,
            fused_tiered_widths, fused_tiered_kinds, 2U, true, true);
        assert(fused_jit.lookup(fused_tiered_symbol));
        expect_cache_statistics(fused_jit, 0U, 1U, 1U);
    }
    assert(cached_object_paths(fused_tiered_cache_directory).size() == 1U);
    const auto fused_tiered_metadata = read_tiered_read_dedup_metadata(
        fused_tiered_cache_directory);
    assert(fused_tiered_metadata.tier == 1U);
    assert(fused_tiered_metadata.marked_value_loads >= 2U);
    // At O2, LLVM's ordinary optimization pipeline can eliminate the
    // same-member duplicate reads before the tiered direct-read pass runs.
    // Keep the custom-pass contribution requirement on O0, where those loads
    // remain and the custom pass is the one that removes them.
    if (optimization == JitOptimizationLevel::o0) {
        assert(fused_tiered_metadata.eliminated_value_loads >= 1U);
    }
    assert(fused_tiered_metadata.eliminated_loads
        >= fused_tiered_metadata.eliminated_value_loads);
    {
        LlvmJit fused_warm { fused_options };
        fused_warm.add_masked_process(
            fused_tiered_symbol, fused_required_read,
            fused_tiered_widths, fused_tiered_kinds, 2U, true, true);
        assert(fused_warm.lookup(fused_tiered_symbol));
        expect_cache_statistics(fused_warm, 1U, 0U, 0U);
    }
    const auto fused_warm_metadata = read_tiered_read_dedup_metadata(
        fused_tiered_cache_directory);
    assert(fused_warm_metadata.tier == fused_tiered_metadata.tier);
    assert(fused_warm_metadata.marked_value_loads
        == fused_tiered_metadata.marked_value_loads);
    assert(fused_warm_metadata.eliminated_value_loads
        == fused_tiered_metadata.eliminated_value_loads);

    // Unrelated elaborated signals do not invalidate a process-local object.
    {
        LlvmJit changed_widths { options };
        const std::array<std::uint32_t, 2> other_widths { 8, 2 };
        changed_widths.add_process(
            symbol, make_cached_process("10100101"), other_widths);
        run_cached_process(changed_widths, symbol,
            EncodedSignal { UINT64_C(0xa5), 0 });
        expect_cache_statistics(changed_widths, 1, 0, 0);
    }
    assert(cached_object_paths(cache_directory).size() == 2);

    constexpr std::string_view signal_symbol = "persistent_cache_signal_process";
    const std::array<std::uint32_t, 3> signal_widths { 8, 8, 1 };
    {
        LlvmJit cold { options };
        cold.add_process(
            signal_symbol, make_cached_signal_process(0), signal_widths);
        run_cached_signal_process(cold, signal_symbol);
        expect_cache_statistics(cold, 0, 1, 1);
    }
    assert(cached_object_paths(cache_directory).size() == 3);

    // Changing only an unreferenced signal width reuses the cached object.
    {
        LlvmJit unrelated_width { options };
        const std::array<std::uint32_t, 3> widths_with_unrelated_change { 8, 8, 2 };
        unrelated_width.add_process(
            signal_symbol, make_cached_signal_process(0),
            widths_with_unrelated_change);
        run_cached_signal_process(unrelated_width, signal_symbol);
        expect_cache_statistics(unrelated_width, 1, 0, 0);
    }
    assert(cached_object_paths(cache_directory).size() == 3);

    // A referenced signal's width remains part of the process cache key.
    {
        LlvmJit referenced_width { options };
        const std::array<std::uint32_t, 3> widths_with_referenced_change { 9, 8, 1 };
        referenced_width.add_process(
            signal_symbol, make_cached_signal_process(0),
            widths_with_referenced_change);
        run_cached_signal_process(referenced_width, signal_symbol);
        expect_cache_statistics(referenced_width, 0, 1, 1);
    }
    assert(cached_object_paths(cache_directory).size() == 4);

    // Signal IDs remain process inputs even when the referenced widths match.
    {
        LlvmJit changed_signal { options };
        changed_signal.add_process(
            signal_symbol, make_cached_signal_process(1), signal_widths);
        run_cached_signal_process(changed_signal, signal_symbol);
        expect_cache_statistics(changed_signal, 0, 1, 1);
    }
    assert(cached_object_paths(cache_directory).size() == 5);

    constexpr std::string_view scheduled_symbol = "persistent_cache_scheduled_process";
    const std::array<std::uint32_t, 1> scheduled_widths { 8 };
    {
        LlvmJit cold { options };
        cold.add_process(
            scheduled_symbol, make_cached_scheduled_process(true, 3),
            scheduled_widths);
        run_cached_scheduled_process(
            cold, scheduled_symbol, ScheduledWriteKind::after, 3);
        expect_cache_statistics(cold, 0, 1, 1);
    }
    assert(cached_object_paths(cache_directory).size() == 6);
    {
        LlvmJit warm { options };
        warm.add_process(
            scheduled_symbol, make_cached_scheduled_process(true, 3),
            scheduled_widths);
        run_cached_scheduled_process(
            warm, scheduled_symbol, ScheduledWriteKind::after, 3);
        expect_cache_statistics(warm, 1, 0, 0);
    }

    // Delayed-write delay and operation kind both participate in identity.
    {
        LlvmJit changed_delay { options };
        changed_delay.add_process(
            scheduled_symbol, make_cached_scheduled_process(true, 4),
            scheduled_widths);
        run_cached_scheduled_process(
            changed_delay, scheduled_symbol, ScheduledWriteKind::after, 4);
        expect_cache_statistics(changed_delay, 0, 1, 1);
    }
    assert(cached_object_paths(cache_directory).size() == 7);
    {
        LlvmJit changed_kind { options };
        changed_kind.add_process(
            scheduled_symbol, make_cached_scheduled_process(false, 0),
            scheduled_widths);
        run_cached_scheduled_process(
            changed_kind, scheduled_symbol, ScheduledWriteKind::update, 0);
        expect_cache_statistics(changed_kind, 0, 1, 1);
    }
    assert(cached_object_paths(cache_directory).size() == 8);

    constexpr std::string_view wait_symbol = "persistent_cache_wait_process";
    const std::array<std::uint32_t, 2> wait_widths { 1, 1 };
    {
        LlvmJit cold { options };
        cold.add_process(
            wait_symbol, make_cached_wait_process(false, { 0, 1 }),
            wait_widths);
        materialize_cached_wait_process(cold, wait_symbol);
        expect_cache_statistics(cold, 0, 1, 1);
    }
    assert(cached_object_paths(cache_directory).size() == 9);
    {
        LlvmJit warm { options };
        warm.add_process(
            wait_symbol, make_cached_wait_process(false, { 0, 1 }),
            wait_widths);
        materialize_cached_wait_process(warm, wait_symbol);
        expect_cache_statistics(warm, 1, 0, 0);
    }

    // Wait operands, referenced widths, operation kind, and static edge rules
    // participate in native cache identity.
    {
        LlvmJit changed_operands { options };
        changed_operands.add_process(
            wait_symbol, make_cached_wait_process(false, { 1, 0 }),
            wait_widths);
        materialize_cached_wait_process(changed_operands, wait_symbol);
        expect_cache_statistics(changed_operands, 0, 1, 1);
    }
    assert(cached_object_paths(cache_directory).size() == 10);
    {
        LlvmJit changed_kind { options };
        changed_kind.add_process(
            wait_symbol, make_cached_wait_process(true, { 0, 1 }),
            wait_widths);
        materialize_cached_wait_process(changed_kind, wait_symbol);
        expect_cache_statistics(changed_kind, 0, 1, 1);
    }
    assert(cached_object_paths(cache_directory).size() == 11);
    {
        LlvmJit changed_width { options };
        const std::array<std::uint32_t, 2> wider_wait_signal { 2, 1 };
        changed_width.add_process(
            wait_symbol, make_cached_wait_process(false, { 0, 1 }),
            wider_wait_signal);
        materialize_cached_wait_process(changed_width, wait_symbol);
        expect_cache_statistics(changed_width, 0, 1, 1);
    }
    assert(cached_object_paths(cache_directory).size() == 12);
    {
        LlvmJit changed_dynamic_edge { options };
        changed_dynamic_edge.add_process(
            wait_symbol,
            make_cached_wait_process(
                false, { 0, 1 },
                { EdgeKind::posedge, EdgeKind::any }),
            wait_widths);
        materialize_cached_wait_process(
            changed_dynamic_edge, wait_symbol);
        expect_cache_statistics(changed_dynamic_edge, 0, 1, 1);
    }
    assert(cached_object_paths(cache_directory).size() == 13);
    {
        LlvmJit changed_edge { options };
        auto edge_process = make_cached_wait_process(true, { 0, 1 });
        edge_process.static_sensitivity.front().edge = EdgeKind::posedge;
        changed_edge.add_process(
            wait_symbol, edge_process, wait_widths);
        materialize_cached_wait_process(changed_edge, wait_symbol);
        expect_cache_statistics(changed_edge, 0, 1, 1);
    }
    assert(cached_object_paths(cache_directory).size() == 14);

    constexpr std::string_view assertion_symbol = "persistent_cache_assertion_process";
    const std::array<std::uint32_t, 0> no_signals { };
    {
        LlvmJit cold { options };
        cold.add_process(
            assertion_symbol,
            make_cached_assertion_process(AssertionSeverity::error, 7),
            no_signals);
        run_cached_assertion_process(cold, assertion_symbol);
        expect_cache_statistics(cold, 0, 1, 1);
    }
    assert(cached_object_paths(cache_directory).size() == 15);
    {
        LlvmJit warm { options };
        warm.add_process(
            assertion_symbol,
            make_cached_assertion_process(AssertionSeverity::error, 7),
            no_signals);
        run_cached_assertion_process(warm, assertion_symbol);
        expect_cache_statistics(warm, 1, 0, 0);
    }

    // Assertion diagnostic metadata is immutable generated behavior and must
    // therefore participate in native object identity.
    {
        LlvmJit changed_metadata { options };
        changed_metadata.add_process(
            assertion_symbol,
            make_cached_assertion_process(AssertionSeverity::failure, 8),
            no_signals);
        run_cached_assertion_process(changed_metadata, assertion_symbol);
        expect_cache_statistics(changed_metadata, 0, 1, 1);
    }
    assert(cached_object_paths(cache_directory).size() == 16);

    constexpr std::string_view debug_symbol = "persistent_cache_debug_point_process";
    {
        LlvmJit cold { options };
        cold.add_process(
            debug_symbol,
            make_cached_debug_point_process(DebugPointKind::statement, 11),
            no_signals);
        assert(cold.lookup(debug_symbol));
        expect_cache_statistics(cold, 0, 1, 1);
    }
    assert(cached_object_paths(cache_directory).size() == 17);
    {
        LlvmJit warm { options };
        warm.add_process(
            debug_symbol,
            make_cached_debug_point_process(DebugPointKind::statement, 11),
            no_signals);
        assert(warm.lookup(debug_symbol));
        expect_cache_statistics(warm, 1, 0, 0);
    }

    // Scope is immutable debugger/specialization provenance even when the
    // operation kind and source location are unchanged.
    {
        LlvmJit changed_metadata { options };
        changed_metadata.add_process(
            debug_symbol,
            make_cached_debug_point_process(
                DebugPointKind::statement,
                11,
                "cached_debug_point_process.scan.$when_11_5_42"),
            no_signals);
        assert(changed_metadata.lookup(debug_symbol));
        expect_cache_statistics(changed_metadata, 0, 1, 1);
    }
    assert(cached_object_paths(cache_directory).size() == 18);

    // A WaitOn timeout changes generated boundary metadata and cache identity.
    {
        LlvmJit changed_timeout { options };
        auto timed_wait = make_cached_wait_process(false, { 0, 1 });
        fsim::runtime::simir::operation_get<WaitOn>(timed_wait.operations.front()).timeout = 5;
        changed_timeout.add_process(
            wait_symbol, timed_wait, wait_widths);
        materialize_cached_wait_process(
            changed_timeout, wait_symbol);
        expect_cache_statistics(changed_timeout, 0, 1, 1);
    }
    assert(cached_object_paths(cache_directory).size() == 19);

    constexpr std::string_view wildcard_symbol = "persistent_cache_wildcard_case";
    const std::array<std::uint32_t, 1> wildcard_widths { 1 };
    const auto make_wildcard_process =
        [](const BinaryOperator operation) {
            Process process;
            process.id = 0;
            process.name = "cached_wildcard_case";
            process.register_count = 3;
            process.operations = {
                LoadConstant {
                    0, PackedLogic4::from_msb_string("10X1") },
                LoadConstant {
                    1, PackedLogic4::from_msb_string("1011") },
                Binary { operation, 2, 0, 1 },
                WriteBlocking { 0, 2 },
                Halt { },
            };
            return process;
        };
    const auto run_wildcard_process =
        [&](LlvmJit& jit, const Logic4 expected) {
            TestRuntime runtime;
            auto descriptor = abi(runtime);
            assert(
                jit.execute(jit.lookup(wildcard_symbol), descriptor)
                == JitExecutionStatus::completed);
            assert(runtime.signals[0] == encode(expected));
        };
    {
        LlvmJit cold { options };
        cold.add_process(
            wildcard_symbol,
            make_wildcard_process(BinaryOperator::casez_equal),
            wildcard_widths);
        run_wildcard_process(cold, Logic4::zero);
        expect_cache_statistics(cold, 0, 1, 1);
    }
    assert(cached_object_paths(cache_directory).size() == 20);
    {
        LlvmJit warm { options };
        warm.add_process(
            wildcard_symbol,
            make_wildcard_process(BinaryOperator::casez_equal),
            wildcard_widths);
        run_wildcard_process(warm, Logic4::zero);
        expect_cache_statistics(warm, 1, 0, 0);
    }
    // The wildcard matching policy is generated behavior and participates in
    // native object identity.
    {
        LlvmJit changed_operator { options };
        changed_operator.add_process(
            wildcard_symbol,
            make_wildcard_process(BinaryOperator::casex_equal),
            wildcard_widths);
        run_wildcard_process(changed_operator, Logic4::one);
        expect_cache_statistics(changed_operator, 0, 1, 1);
    }
    assert(cached_object_paths(cache_directory).size() == 21);
    {
        LlvmJit one_sided_wildcard { options };
        one_sided_wildcard.add_process(
            wildcard_symbol,
            make_wildcard_process(BinaryOperator::wildcard_equal),
            wildcard_widths);
        run_wildcard_process(one_sided_wildcard, Logic4::x);
        expect_cache_statistics(one_sided_wildcard, 0, 1, 1);
    }
    assert(cached_object_paths(cache_directory).size() == 22);

    constexpr std::string_view power_symbol = "persistent_cache_power_operator";
    const std::array<std::uint32_t, 1> power_widths { 8 };
    const auto make_power_process =
        [](const BinaryOperator operation) {
            Process process;
            process.id = 0;
            process.name = "cached_power_operator";
            process.register_count = 3;
            process.operations = {
                LoadConstant {
                    0, PackedLogic4::from_msb_string("00000011") },
                LoadConstant {
                    1, PackedLogic4::from_msb_string("00000100") },
                Binary { operation, 2, 0, 1 },
                WriteBlocking { 0, 2 },
                Halt { },
            };
            return process;
        };
    const auto run_power_process =
        [&](LlvmJit& jit, const std::string_view expected) {
            TestRuntime runtime;
            auto descriptor = abi(runtime);
            assert(
                jit.execute(jit.lookup(power_symbol), descriptor)
                == JitExecutionStatus::completed);
            const auto encoded = PackedLogic4::from_msb_string(expected).low_word();
            assert((
                runtime.signals[0]
                == EncodedSignal { encoded.aval, encoded.bval }));
        };
    {
        LlvmJit multiply { options };
        multiply.add_process(
            power_symbol,
            make_power_process(BinaryOperator::multiply_unsigned),
            power_widths);
        run_power_process(multiply, "00001100");
        expect_cache_statistics(multiply, 0, 1, 1);
    }
    assert(cached_object_paths(cache_directory).size() == 23);
    {
        LlvmJit power { options };
        power.add_process(
            power_symbol,
            make_power_process(BinaryOperator::power_unsigned),
            power_widths);
        run_power_process(power, "01010001");
        expect_cache_statistics(power, 0, 1, 1);
    }
    assert(cached_object_paths(cache_directory).size() == 24);

    const auto wide_projected_cache_directory
        = cache_directory.parent_path()
            / (cache_directory.filename().string()
                + "-wide-projected-packed");
    test_cached_wide_projected_write(
        optimization, wide_projected_cache_directory);

    const auto bound_signal_cache_directory
        = cache_directory.parent_path()
            / (cache_directory.filename().string()
                + "-bound-signal-operands");
    test_cached_bound_signal_callback_operands(
        optimization, bound_signal_cache_directory);
}

void test_optimization_cache_invalidation(
    const std::filesystem::path& cache_directory)
{
    const std::array<std::uint32_t, 2> widths { 8, 1 };
    constexpr std::string_view symbol = "optimization_cache_process";
    const auto process = make_cached_process("10100101");

    {
        LlvmJit o0 {
            LlvmJitOptions { JitOptimizationLevel::o0, cache_directory }
        };
        o0.add_process(symbol, process, widths);
        run_cached_process(o0, symbol, EncodedSignal { UINT64_C(0xa5), 0 });
        expect_cache_statistics(o0, 0, 1, 1);
    }
    {
        LlvmJit o2 {
            LlvmJitOptions { JitOptimizationLevel::o2, cache_directory }
        };
        o2.add_process(symbol, process, widths);
        run_cached_process(o2, symbol, EncodedSignal { UINT64_C(0xa5), 0 });
        expect_cache_statistics(o2, 0, 1, 1);
    }
    assert(cached_object_paths(cache_directory).size() == 2);
    {
        LlvmJit warm_o2 {
            LlvmJitOptions { JitOptimizationLevel::o2, cache_directory }
        };
        warm_o2.add_process(symbol, process, widths);
        run_cached_process(warm_o2, symbol,
            EncodedSignal { UINT64_C(0xa5), 0 });
        expect_cache_statistics(warm_o2, 1, 0, 0);
    }
}

void test_cache_pruning_integration(
    const std::filesystem::path& cache_directory)
{
    fsim::compiler::ObjectCache storage {
        cache_directory / "llvm" / "objects"
    };
    fsim::compiler::CacheKeyBuilder builder;
    const auto key = builder.add("seed", "prune-me").finish();
    const std::array payload {
        std::byte { 0xde }, std::byte { 0xad }, std::byte { 0xbe }, std::byte { 0xef }
    };
    std::error_code error;
    assert(storage.store(key, payload, error));
    const auto encoded_size = std::filesystem::file_size(storage.path_for(key), error);
    assert(!error);

    LlvmJitOptions options { JitOptimizationLevel::o2, cache_directory };
    options.cache_maximum_bytes.reset();
    options.cache_maximum_entries = 0;
    options.cache_maximum_age.reset();
    LlvmJit pruned { options };
    const auto statistics = pruned.cache_statistics();
    assert(statistics.pruned_entries == 1);
    assert(statistics.pruned_bytes == encoded_size);
    assert(statistics.prune_failures == 0);
    assert(!std::filesystem::exists(storage.path_for(key)));

    // Cache maintenance is best-effort: an unusable cache root is reflected in
    // telemetry but never prevents construction of a valid JIT.
    const auto broken_directory = cache_directory.parent_path() / "broken";
    std::filesystem::create_directories(broken_directory / "llvm", error);
    assert(!error);
    {
        std::ofstream file {
            broken_directory / "llvm" / "objects", std::ios::binary
        };
        file << "not a directory";
    }
    LlvmJit broken {
        LlvmJitOptions { JitOptimizationLevel::o2, broken_directory }
    };
    assert(broken.cache_statistics().prune_failures == 1);
}

void test_inertial_cache_identity(
    const std::filesystem::path& cache_directory)
{
    constexpr std::string_view symbol { "cached_inertial" };
    const std::array<std::uint32_t, 1> widths { 8 };
    const auto run =
        [&](const TransitionDelays delays,
            const std::size_t hits,
            const std::size_t misses) {
            LlvmJit jit {
                LlvmJitOptions {
                    JitOptimizationLevel::o2, cache_directory }
            };
            jit.add_process(
                symbol, make_cached_inertial_process(delays), widths);
            TestRuntime runtime;
            auto descriptor = abi(runtime);
            assert(
                jit.execute(jit.lookup(symbol), descriptor)
                == JitExecutionStatus::completed);
            assert((
                runtime.inertial_writes
                == std::vector<InertialWrite> {
                    { 0,
                        encode(PackedLogic4::from_msb_string("10100101")),
                        0,
                        0,
                        delays.rise,
                        delays.fall,
                        delays.turnoff } }));
            expect_cache_statistics(jit, hits, misses, misses);
        };
    run({ 2, 3, 4 }, 0, 1);
    run({ 2, 3, 4 }, 1, 0);
    run({ 5, 3, 4 }, 0, 1);
    run({ 2, 6, 4 }, 0, 1);
    run({ 2, 3, 7 }, 0, 1);
}

void test_projected_cache_identity(
    const std::filesystem::path& cache_directory)
{
    constexpr std::string_view symbol { "cached_projected" };
    const std::array<std::uint32_t, 1> widths { 8 };
    const auto run =
        [&](const std::uint64_t delay,
            const std::uint64_t rejection,
            const ProjectedDelayMode mode,
            const std::size_t hits,
            const std::size_t misses) {
            LlvmJit jit {
                LlvmJitOptions {
                    JitOptimizationLevel::o2, cache_directory }
            };
            jit.add_process(
                symbol,
                make_cached_projected_process(
                    delay, rejection, mode),
                widths);
            TestRuntime runtime;
            auto descriptor = abi(runtime);
            assert(
                jit.execute(jit.lookup(symbol), descriptor)
                == JitExecutionStatus::completed);
            assert((
                runtime.projected_writes
                == std::vector<ProjectedWrite> {
                    { 0,
                        encode(PackedLogic4::from_msb_string("10100101")),
                        0,
                        0,
                        delay,
                        rejection,
                        static_cast<std::uint32_t>(mode) } }));
            expect_cache_statistics(jit, hits, misses, misses);
        };
    run(5, 2, ProjectedDelayMode::inertial, 0, 1);
    run(5, 2, ProjectedDelayMode::inertial, 1, 0);
    run(6, 2, ProjectedDelayMode::inertial, 0, 1);
    run(5, 1, ProjectedDelayMode::inertial, 0, 1);
    run(5, 0, ProjectedDelayMode::transport, 0, 1);
}

void test_projected_waveform_cache_identity(
    const std::filesystem::path& cache_directory)
{
    constexpr std::string_view symbol { "cached_projected_waveform" };
    const std::array<std::uint32_t, 1> widths { 8 };
    const auto run =
        [&](const std::uint64_t second_delay,
            const std::uint64_t rejection,
            const ProjectedDelayMode mode,
            const std::size_t hits,
            const std::size_t misses) {
            LlvmJit jit {
                LlvmJitOptions {
                    JitOptimizationLevel::o2, cache_directory }
            };
            jit.add_process(
                symbol,
                make_cached_projected_waveform_process(
                    second_delay, rejection, mode),
                widths);
            TestRuntime runtime;
            auto descriptor = abi(runtime);
            assert(
                jit.execute(jit.lookup(symbol), descriptor)
                == JitExecutionStatus::completed);
            assert(runtime.projected_writes.size() == 2);
            assert(runtime.projected_writes[0].delay == 5);
            assert(runtime.projected_writes[1].delay == second_delay);
            assert(runtime.projected_writes[0].rejection == rejection);
            assert(runtime.projected_writes[0].mode
                == static_cast<std::uint32_t>(mode));
            expect_cache_statistics(jit, hits, misses, misses);
        };
    run(9, 2, ProjectedDelayMode::inertial, 0, 1);
    run(9, 2, ProjectedDelayMode::inertial, 1, 0);
    run(10, 2, ProjectedDelayMode::inertial, 0, 1);
    run(9, 1, ProjectedDelayMode::inertial, 0, 1);
    run(9, 0, ProjectedDelayMode::transport, 0, 1);
}

void test_signed_shift_cache_identity(
    const std::filesystem::path& cache_directory)
{
    constexpr std::string_view symbol { "cached_signed_shift" };
    const std::array<std::uint32_t, 1> widths { 4 };
    const auto run =
        [&](const bool signed_amount,
            const std::uint64_t hits,
            const std::uint64_t misses,
            const std::string_view expected) {
            Process process;
            process.id = 22;
            process.name = std::string { symbol };
            process.register_count = 3;
            process.operations = {
                LoadConstant {
                    0,
                    PackedLogic4::from_msb_string("1001") },
                LoadConstant {
                    1,
                    PackedLogic4::from_msb_string("1111") },
                Shift {
                    ShiftOperator::logical_left,
                    2,
                    0,
                    1,
                    signed_amount },
                WriteBlocking { 0, 2 },
                Halt { },
            };
            LlvmJit jit {
                LlvmJitOptions {
                    JitOptimizationLevel::o2,
                    cache_directory }
            };
            jit.add_process(symbol, process, widths);
            TestRuntime runtime;
            auto descriptor = abi(runtime);
            assert(
                jit.execute(jit.lookup(symbol), descriptor)
                == JitExecutionStatus::completed);
            const auto encoded = PackedLogic4::from_msb_string(expected).low_word();
            assert((
                runtime.signals[0]
                == EncodedSignal { encoded.aval, encoded.bval }));
            expect_cache_statistics(jit, hits, misses, misses);
        };

    run(false, 0, 1, "0000");
    run(true, 0, 1, "0100");
    run(true, 1, 0, "0100");
    assert(cached_object_paths(cache_directory).size() == 2);
}

void test_integer_cache_identity(
    const std::filesystem::path& cache_directory)
{
    constexpr std::string_view symbol { "cached_integer" };
    const std::array<std::uint32_t, 1> widths { 32 };
    const auto integer = [](const std::int32_t value) {
        return PackedLogic4::from_aval_bval(
            32, static_cast<std::uint32_t>(value), 0);
    };
    const auto run =
        [&](const IntegerBinaryOperator operation,
            const std::int32_t lower,
            const std::int32_t upper,
            const std::int32_t expected,
            const std::size_t hits,
            const std::size_t misses) {
            Process process;
            process.id = 33;
            process.name = "cached_integer";
            process.register_count = 3;
            process.operations = {
                LoadConstant { 0, integer(3) },
                LoadConstant { 1, integer(2) },
                IntegerBinary { operation, 2, 0, 1 },
                IntegerCheck { 2, lower, upper },
                WriteBlocking { 0, 2 },
                Halt { }
            };
            LlvmJit jit {
                LlvmJitOptions {
                    JitOptimizationLevel::o2, cache_directory }
            };
            jit.add_process(symbol, process, widths);
            TestRuntime runtime;
            auto descriptor = abi(runtime);
            assert(
                jit.execute(jit.lookup(symbol), descriptor)
                == JitExecutionStatus::completed);
            assert((
                runtime.signals[0]
                == EncodedSignal {
                    static_cast<std::uint32_t>(expected), 0 }));
            expect_cache_statistics(jit, hits, misses, misses);
        };
    run(IntegerBinaryOperator::add, -5, 7, 5, 0, 1);
    run(IntegerBinaryOperator::add, -5, 7, 5, 1, 0);
    run(IntegerBinaryOperator::subtract, -5, 7, 1, 0, 1);
    run(IntegerBinaryOperator::add, -5, 8, 5, 0, 1);
    assert(cached_object_paths(cache_directory).size() == 3);
}

void test_signal_kind_cache_identity(
    const std::filesystem::path& cache_directory)
{
    constexpr std::string_view symbol = "cached_signal_kind";
    const std::array<std::uint32_t, 2> widths { 8, 8 };
    const auto process = make_cached_signal_process(0);
    const auto options = LlvmJitOptions {
        JitOptimizationLevel::o2, cache_directory
    };

    {
        LlvmJit cold { options };
        const std::array<ValueKind, 2> kinds {
            ValueKind::logic4, ValueKind::logic4
        };
        cold.add_process(symbol, process, widths, kinds);
        run_cached_signal_process(cold, symbol);
        expect_cache_statistics(cold, 0, 1, 1);
    }
    {
        LlvmJit unrelated_change { options };
        const std::array<ValueKind, 2> kinds {
            ValueKind::logic4, ValueKind::logic9
        };
        unrelated_change.add_process(
            symbol, process, widths, kinds);
        run_cached_signal_process(unrelated_change, symbol);
        expect_cache_statistics(unrelated_change, 1, 0, 0);
    }
    {
        LlvmJit referenced_change { options };
        const std::array<ValueKind, 2> kinds {
            ValueKind::logic9, ValueKind::logic4
        };
        referenced_change.add_process(
            symbol, process, widths, kinds);
        run_cached_signal_process(referenced_change, symbol);
        expect_cache_statistics(referenced_change, 0, 1, 1);
    }
    {
        LlvmJit exact_warm { options };
        const std::array<ValueKind, 2> kinds {
            ValueKind::logic9, ValueKind::logic4
        };
        exact_warm.add_process(symbol, process, widths, kinds);
        run_cached_signal_process(exact_warm, symbol);
        expect_cache_statistics(exact_warm, 1, 0, 0);
    }
    assert(cached_object_paths(cache_directory).size() == 2);
}

void test_wide_constant_cache_identity(
    const std::filesystem::path& cache_directory)
{
    constexpr std::string_view symbol = "cached_wide_constant";
    const std::array<std::uint32_t, 0> no_signals { };
    const auto options = LlvmJitOptions {
        JitOptimizationLevel::o2, cache_directory
    };
    const auto materialize = [&](const std::string& spelling,
                                 const bool logic9,
                                 const std::uint64_t hits,
                                 const std::uint64_t misses) {
        Process process;
        process.id = 130;
        process.name = symbol;
        process.register_count = 1;
        process.register_value_kinds = {
            logic9 ? ValueKind::logic9 : ValueKind::logic4
        };
        process.operations = {
            LoadConstant {
                0,
                logic9
                    ? PackedLogic4::from_logic9_msb_string(spelling)
                    : PackedLogic4::from_msb_string(spelling) },
            Halt { },
        };
        LlvmJit jit { options };
        jit.add_process(symbol, process, no_signals);
        assert(jit.lookup(symbol));
        expect_cache_statistics(jit, hits, misses, misses);
    };

    auto upper_one = std::string(137, '0');
    upper_one[0] = '1';
    auto second_upper_one = upper_one;
    second_upper_one[1] = '1';
    auto upper_x = upper_one;
    upper_x[2] = 'x';
    auto upper_z = upper_one;
    upper_z[2] = 'z';
    auto upper_u = upper_one;
    upper_u[2] = 'U';
    auto upper_w = upper_one;
    upper_w[2] = 'W';
    materialize(upper_one, false, 0, 1);
    materialize(second_upper_one, false, 0, 1);
    materialize(upper_x, false, 0, 1);
    materialize(upper_z, false, 0, 1);
    materialize(upper_z, false, 1, 0);
    materialize(upper_u, true, 0, 1);
    materialize(upper_w, true, 0, 1);
    materialize(upper_w, true, 1, 0);
    assert(cached_object_paths(cache_directory).size() == 6);
}

void test_coverage_query_cache_identity(
    const std::filesystem::path& cache_directory)
{
    constexpr std::string_view symbol = "cached_coverage_query";
    const std::array<std::uint32_t, 0> no_signals { };
    const auto options = LlvmJitOptions {
        JitOptimizationLevel::o2, cache_directory
    };
    const auto materialize = [&](const CoverageQueryKind kind,
                                 const std::uint64_t hits,
                                 const std::uint64_t misses) {
        Process process;
        process.id = 131;
        process.name = symbol;
        process.register_count = 1;
        process.operations = {
            CoverageQuery { 0, kind },
            Halt { },
        };
        LlvmJit jit { options };
        jit.add_process(symbol, process, no_signals);
        assert(jit.lookup(symbol));
        expect_cache_statistics(jit, hits, misses, misses);
    };
    materialize(CoverageQueryKind::overall_type, 0, 1);
    materialize(CoverageQueryKind::overall_type, 1, 0);
    materialize(CoverageQueryKind::overall_instance, 0, 1);
    materialize(CoverageQueryKind::overall_instance, 1, 0);
    assert(cached_object_paths(cache_directory).size() == 2);
}

void test_coverage_sample_cache_identity(
    const std::filesystem::path& cache_directory)
{
    constexpr std::string_view symbol = "cached_coverage_sample";
    const std::array<std::uint32_t, 0> no_signals { };
    const auto options = LlvmJitOptions {
        JitOptimizationLevel::o2, cache_directory
    };
    const auto materialize =
        [&](const frontend::SystemVerilogScalarKind scalar_kind,
            const std::uint64_t hits,
            const std::uint64_t misses) {
            Process process;
            process.id = 132;
            process.name = symbol;
            process.register_count = 1;
            process.operations = {
                LoadConstant { 0, PackedLogic4::from_aval_bval(64U, 0U, 0U) },
                CoverageSample { "work.real_group@instance", { 0U }, { 64U },
                    { 0U }, { scalar_kind },
                    CoverageSampleTrigger::procedural },
                Halt { },
            };
            LlvmJit jit { options };
            jit.add_process(symbol, process, no_signals);
            assert(jit.lookup(symbol));
            expect_cache_statistics(jit, hits, misses, misses);
        };
    materialize(frontend::SystemVerilogScalarKind::None, 0U, 1U);
    materialize(frontend::SystemVerilogScalarKind::None, 1U, 0U);
    materialize(frontend::SystemVerilogScalarKind::Real, 0U, 1U);
    materialize(frontend::SystemVerilogScalarKind::Real, 1U, 0U);
    assert(cached_object_paths(cache_directory).size() == 2U);
}

void test_random_distribution_cache_identity(
    const std::filesystem::path& cache_directory)
{
    constexpr std::string_view symbol = "cached_random_distribution";
    const std::array<std::uint32_t, 0> no_signals { };
    const auto options = LlvmJitOptions {
        JitOptimizationLevel::o2, cache_directory
    };
    const auto materialize = [&](const RandomDistributionKind kind,
                                 const RegisterId second,
                                 const std::uint32_t source_line,
                                 const std::uint64_t hits,
                                 const std::uint64_t misses) {
        Process process;
        process.id = 132;
        process.name = symbol;
        process.register_count = 5;
        process.operations = {
            LoadConstant { 1, PackedLogic4::from_aval_bval(32, 1, 0) },
            LoadConstant { 2, PackedLogic4::from_aval_bval(32, 2, 0) },
            LoadConstant { 3, PackedLogic4::from_aval_bval(32, 3, 0) },
            LoadConstant { 4, PackedLogic4::from_aval_bval(32, 4, 0) },
            RandomDistribution {
                0, 1, kind, 2, second,
                SourceLocation { "random_distribution.sv", source_line, 9 } },
            Halt { },
        };
        LlvmJit jit { options };
        jit.add_process(symbol, process, no_signals);
        assert(jit.lookup(symbol));
        expect_cache_statistics(jit, hits, misses, misses);
    };
    materialize(RandomDistributionKind::uniform, 3, 17, 0, 1);
    materialize(RandomDistributionKind::uniform, 3, 17, 1, 0);
    materialize(RandomDistributionKind::uniform, 3, 18, 0, 1);
    materialize(RandomDistributionKind::normal, 3, 17, 0, 1);
    materialize(RandomDistributionKind::uniform, 4, 17, 0, 1);
    materialize(RandomDistributionKind::erlang, 3, 17, 0, 1);
    assert(cached_object_paths(cache_directory).size() == 5);
}

void test_system_command_cache_identity(
    const std::filesystem::path& cache_directory)
{
    constexpr std::string_view symbol = "cached_system_command";
    const std::array<std::uint32_t, 0> no_signals { };
    const auto options = LlvmJitOptions {
        JitOptimizationLevel::o2, cache_directory
    };
    const auto materialize = [&](
                                 const std::optional<StringRegisterId> command,
                                 const std::optional<RegisterId> destination,
                                 const std::uint64_t hits,
                                 const std::uint64_t misses) {
        Process process;
        process.id = 133;
        process.name = symbol;
        process.register_count = 2;
        process.string_register_count = 2;
        process.operations = {
            LoadStringConstant { 0, "first" },
            LoadStringConstant { 1, "second" },
            SystemCommand { command, destination },
            Halt { },
        };
        LlvmJit jit { options };
        jit.add_process(symbol, process, no_signals);
        assert(jit.lookup(symbol));
        expect_cache_statistics(jit, hits, misses, misses);
    };
    // The second numeric register is intentionally unused (and has width 0);
    // replaying its cached frame metadata must preserve that valid layout.
    materialize(0, 0, 0, 1);
    materialize(0, 0, 1, 0);
    materialize(1, 0, 0, 1);
    materialize(0, 1, 0, 1);
    materialize(std::nullopt, std::nullopt, 0, 1);
    assert(cached_object_paths(cache_directory).size() == 4);
}

void test_inline_constraint_cache_identity(
    const std::filesystem::path& cache_directory)
{
    constexpr std::string_view symbol = "cached_inline_constraint";
    const std::array<std::uint32_t, 0> no_signals { };
    const auto options = LlvmJitOptions {
        JitOptimizationLevel::o2, cache_directory
    };
    const auto materialize = [&](const std::string& spelling,
                                 const std::uint64_t hits,
                                 const std::uint64_t misses) {
        runtime::SystemVerilogConstraintTemplate name;
        name.kind = runtime::SystemVerilogConstraintTemplateKind::Name;
        name.text = "wide";
        runtime::SystemVerilogConstraintTemplate constant;
        constant.kind = runtime::SystemVerilogConstraintTemplateKind::Constant;
        constant.constant = PackedLogic4::from_msb_string(spelling);
        constant.profile = {
            runtime::SystemVerilogConstraintDomainKind::BitVector,
            137,
            false,
            "logic[136:0]",
            true
        };
        runtime::SystemVerilogConstraintTemplate predicate;
        predicate.kind = runtime::SystemVerilogConstraintTemplateKind::Binary;
        predicate.text = "==";
        predicate.operands = { std::move(name), std::move(constant) };

        ClassMethodCall randomize;
        randomize.destination = 0;
        randomize.receiver = 1;
        randomize.method_identity = "cached_inline_constraint::$randomize";
        randomize.result_width = 32;
        randomize.inline_constraints.push_back(std::move(predicate));
        Process process;
        process.id = 131;
        process.name = symbol;
        process.register_count = 2;
        process.operations = {
            LoadConstant { 1, PackedLogic4(64, Logic4::zero) },
            std::move(randomize),
            Halt { },
        };
        LlvmJit jit { options };
        jit.add_process(symbol, process, no_signals);
        assert(jit.lookup(symbol));
        expect_cache_statistics(jit, hits, misses, misses);
    };

    auto upper_one = std::string(137, '0');
    upper_one.front() = '1';
    auto distinct = upper_one;
    distinct[1] = '1';
    materialize(upper_one, 0, 1);
    materialize(distinct, 0, 1);
    materialize(distinct, 1, 0);
    assert(cached_object_paths(cache_directory).size() == 2);
}

void test_static_pattern_cache_identity(
    const std::filesystem::path& cache_directory)
{
    const auto make_process =
        [](const std::uint8_t explicit_value,
            const std::int32_t explicit_index,
            const bool explicit_first,
            const bool default_pattern,
            const std::int32_t left = 2,
            const std::uint32_t element_width = 8,
            const bool two_state = false,
            const std::uint32_t source_line = 17) {
            ContainerType fixed;
            fixed.element_width = element_width;
            fixed.two_state = two_state;
            fixed.fixed = true;
            fixed.index_left = left;
            fixed.index_right = 0;
            Process process;
            process.id = 30;
            process.name = "cached_static_pattern";
            process.container_register_count = 1;
            process.container_register_types = { fixed };
            process.operations.push_back(
                DebugPoint {
                    DebugPointKind::statement,
                    SourceLocation {
                        "cached_static_pattern.sv",
                        source_line,
                        5 } });
            RegisterId next_register { };
            const auto load_value =
                [&](const std::uint8_t value) {
                    const auto result = next_register++;
                    process.operations.push_back(
                        LoadConstant {
                            result,
                            PackedLogic4::from_aval_bval(
                                element_width, value, 0) });
                    return result;
                };
            if (default_pattern) {
                RegisterId default_value { };
                RegisterId selected_value { };
                if (explicit_first) {
                    selected_value = load_value(explicit_value);
                    default_value = load_value(0x22);
                } else {
                    default_value = load_value(0x22);
                    selected_value = load_value(explicit_value);
                }
                for (auto index = left; index >= 0; --index) {
                    const auto index_register = next_register++;
                    process.operations.push_back(
                        LoadConstant {
                            index_register,
                            PackedLogic4::from_aval_bval(
                                32,
                                static_cast<std::uint32_t>(index),
                                0) });
                    process.operations.push_back(
                        ContainerWrite {
                            0,
                            index_register,
                            index == explicit_index
                                ? selected_value
                                : default_value,
                            true });
                }
            } else {
                std::vector<RegisterId> values;
                for (auto index = left; index >= 0; --index) {
                    values.push_back(
                        load_value(
                            index == explicit_index
                                ? explicit_value
                                : 0x22));
                }
                for (std::int32_t offset = 0;
                    offset <= left;
                    ++offset) {
                    const auto index = left - offset;
                    const auto index_register = next_register++;
                    process.operations.push_back(
                        LoadConstant {
                            index_register,
                            PackedLogic4::from_aval_bval(
                                32,
                                static_cast<std::uint32_t>(index),
                                0) });
                    process.operations.push_back(
                        ContainerWrite {
                            0,
                            index_register,
                            values[static_cast<std::size_t>(offset)],
                            true });
                }
            }
            process.register_count = next_register;
            process.operations.push_back(Halt { });
            return process;
        };
    constexpr std::string_view symbol = "cached_static_pattern";
    const std::array<std::uint32_t, 0> no_signals { };
    const auto options = LlvmJitOptions {
        JitOptimizationLevel::o2, cache_directory
    };
    const auto materialize =
        [&](const Process& process,
            const std::uint64_t hits,
            const std::uint64_t misses) {
            LlvmJit jit { options };
            jit.add_process(symbol, process, no_signals);
            assert(jit.lookup(symbol));
            expect_cache_statistics(
                jit, hits, misses, misses);
        };
    materialize(
        make_process(0x33, 2, false, true), 0, 1);
    materialize(
        make_process(0x33, 2, false, true), 1, 0);
    materialize(
        make_process(0x44, 2, false, true), 0, 1);
    materialize(
        make_process(0x33, 1, false, true), 0, 1);
    materialize(
        make_process(0x33, 2, true, true), 0, 1);
    materialize(
        make_process(0x33, 2, false, false), 0, 1);
    materialize(
        make_process(0x33, 2, false, true, 3), 0, 1);
    materialize(
        make_process(0x03, 2, false, true, 2, 4, true), 0, 1);
    materialize(
        make_process(0x33, 2, false, true, 2, 8, false, 18),
        0,
        1);
    assert(cached_object_paths(cache_directory).size() == 8);
}

} // namespace fsim::tests::compiler
