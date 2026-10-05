// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace fsim::tests::compiler {
namespace {

using namespace llvm_jit_test_detail;

struct AddressRange {
    const void* data { };
    std::size_t bytes { };
};

struct CallbackState {
    std::uint32_t expected_width { };
    std::array<AddressRange, 4> frame_planes { };
    std::vector<std::uint64_t> signal_aval;
    std::vector<std::uint64_t> signal_bval;
    std::vector<std::uint64_t> signal_plane2;
    std::vector<std::uint64_t> signal_plane3;
    std::vector<std::uint64_t> container_aval;
    std::vector<std::uint64_t> container_bval;
    std::vector<std::uint64_t> container_write_aval;
    std::vector<std::uint64_t> container_write_bval;
    std::size_t partial_words { std::numeric_limits<std::size_t>::max() };
    bool fail_signal_read { };
    bool fail_container_read { };
    bool fail_container_write { };
    std::uint32_t signal_reads { };
    std::uint32_t container_reads { };
    std::uint32_t container_read_index64_calls { };
    std::uint32_t container_read_index64_process { };
    std::uint32_t container_read_index64_instruction { };
    std::uint32_t container_read_index64_container { };
    std::uint32_t container_read_index64_flags { };
    std::uint64_t container_read_index64_aval { };
    std::uint64_t container_read_index64_bval { };
    std::uint32_t container_read_index64_word_count { };
    std::uint32_t container_writes { };
    std::uint32_t checked_pointer_ranges { };
    bool fail_signal_write { };
    std::uint32_t signal_writes { };
    std::uint32_t projected_writes { };
    std::uint32_t expected_offset { };
    std::uint32_t expected_mode { };
    std::uint64_t expected_delay { };
    std::uint32_t expected_domain { };
    std::array<std::vector<std::uint64_t>, 4U> written_planes;

    std::vector<std::uintptr_t> signal_aval_addresses;
    std::vector<std::uintptr_t> container_aval_addresses;
};

[[nodiscard]] bool overlaps(
    const void* const pointer,
    const std::size_t bytes,
    const AddressRange range)
{
    if (pointer == nullptr || range.data == nullptr || bytes == 0U
        || range.bytes == 0U) {
        return false;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(pointer);
    const auto range_begin = reinterpret_cast<std::uintptr_t>(range.data);
    assert(bytes <= std::numeric_limits<std::uintptr_t>::max() - begin);
    assert(range.bytes <= std::numeric_limits<std::uintptr_t>::max() - range_begin);
    const auto end = begin + bytes;
    const auto range_end = range_begin + range.bytes;
    return begin < range_end && range_begin < end;
}

void check_callback_range(
    CallbackState& state,
    const std::uint64_t* const pointer,
    const std::uint32_t words)
{
    assert(pointer != nullptr);
    const auto bytes = static_cast<std::size_t>(words) * sizeof(std::uint64_t);
    for (const auto range : state.frame_planes) {
        assert(!overlaps(pointer, bytes, range));
    }
    ++state.checked_pointer_ranges;
}

template <class Source>
void copy_callback_words(
    const Source& source,
    std::uint64_t* const destination,
    const std::uint32_t words,
    const std::size_t count)
{
    assert(source.size() == words);
    assert(destination != nullptr);
    std::copy_n(source.begin(), std::min<std::size_t>(count, words),
        destination);
}

extern "C" std::uint32_t read_signal_packed_into_scratch(
    void* const opaque,
    const std::uint32_t signal,
    const std::uint32_t width,
    std::uint64_t* const aval,
    std::uint64_t* const bval,
    std::uint64_t* const plane2,
    std::uint64_t* const plane3)
{
    auto& state = *static_cast<CallbackState*>(opaque);
    assert(signal == 0U);
    assert(width == state.expected_width);
    assert(width > 64U);
    const auto words = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(width) + 63U) / 64U);
    check_callback_range(state, aval, words);
    state.signal_aval_addresses.push_back(
        reinterpret_cast<std::uintptr_t>(aval));
    check_callback_range(state, bval, words);
    assert((plane2 == nullptr) == state.signal_plane2.empty());
    assert((plane3 == nullptr) == state.signal_plane3.empty());
    if (plane2 != nullptr) {
        check_callback_range(state, plane2, words);
        check_callback_range(state, plane3, words);
    }
    ++state.signal_reads;
    const auto count = state.fail_signal_read
        ? state.partial_words
        : static_cast<std::size_t>(words);
    copy_callback_words(state.signal_aval, aval, words, count);
    copy_callback_words(state.signal_bval, bval, words, count);
    if (plane2 != nullptr) {
        copy_callback_words(state.signal_plane2, plane2, words, count);
        copy_callback_words(state.signal_plane3, plane3, words, count);
    }
    return state.fail_signal_read ? 1U : 0U;
}

extern "C" std::uint32_t read_container_packed_into_scratch(
    void* const opaque,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t,
    const std::uint64_t,
    const std::uint64_t,
    std::uint64_t* const aval,
    std::uint64_t* const bval,
    const std::uint32_t words)
{
    auto& state = *static_cast<CallbackState*>(opaque);
    check_callback_range(state, aval, words);
    check_callback_range(state, bval, words);
    state.container_aval_addresses.push_back(
        reinterpret_cast<std::uintptr_t>(aval));
    ++state.container_reads;
    const auto count = state.fail_container_read
        ? state.partial_words
        : static_cast<std::size_t>(words);
    copy_callback_words(state.container_aval, aval, words, count);
    copy_callback_words(state.container_bval, bval, words, count);
    return state.fail_container_read ? 1U : 0U;
}

extern "C" std::uint32_t capture_container_read_index64_into_scratch(
    void* const opaque,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint32_t container,
    const std::uint32_t flags,
    const std::uint64_t index_aval,
    const std::uint64_t index_bval,
    std::uint64_t* const aval,
    std::uint64_t* const bval,
    const std::uint32_t words)
{
    auto& state = *static_cast<CallbackState*>(opaque);
    check_callback_range(state, aval, words);
    check_callback_range(state, bval, words);
    state.container_read_index64_process = process;
    state.container_read_index64_instruction = instruction;
    state.container_read_index64_container = container;
    state.container_read_index64_flags = flags;
    state.container_read_index64_aval = index_aval;
    state.container_read_index64_bval = index_bval;
    state.container_read_index64_word_count = words;
    state.container_aval_addresses.push_back(
        reinterpret_cast<std::uintptr_t>(aval));
    ++state.container_read_index64_calls;
    copy_callback_words(
        state.container_aval, aval, words,
        static_cast<std::size_t>(words));
    copy_callback_words(
        state.container_bval, bval, words,
        static_cast<std::size_t>(words));
    return 0U;
}

extern "C" std::uint32_t write_container_packed_from_scratch(
    void* const opaque,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t,
    const std::uint64_t,
    const std::uint64_t,
    const std::uint64_t* const aval,
    const std::uint64_t* const bval,
    const std::uint32_t words)
{
    auto& state = *static_cast<CallbackState*>(opaque);
    check_callback_range(state, aval, words);
    check_callback_range(state, bval, words);
    state.container_aval_addresses.push_back(
        reinterpret_cast<std::uintptr_t>(aval));
    state.container_write_aval.assign(aval, aval + words);
    state.container_write_bval.assign(bval, bval + words);
    ++state.container_writes;
    return state.fail_container_write ? 1U : 0U;
}

extern "C" std::uint32_t write_signal_packed_from_scratch(
    void* const opaque,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const std::uint32_t mode,
    const std::uint64_t delay,
    const std::uint64_t* const aval,
    const std::uint64_t* const bval,
    const std::uint64_t* const plane2,
    const std::uint64_t* const plane3,
    const std::uint32_t domain)
{
    auto& state = *static_cast<CallbackState*>(opaque);
    assert(signal == 0U && offset == state.expected_offset);
    assert(width == state.expected_width && mode == state.expected_mode);
    assert(delay == state.expected_delay && domain == state.expected_domain);
    const auto words = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(width) + 63U) / 64U);
    const std::array pointers { aval, bval, plane2, plane3 };
    assert((plane2 == nullptr) == state.signal_plane2.empty());
    assert((plane3 == nullptr) == state.signal_plane3.empty());
    for (std::size_t plane = 0U; plane < pointers.size(); ++plane) {
        if (pointers[plane] != nullptr) {
            check_callback_range(state, pointers[plane], words);
            state.written_planes[plane].assign(
                pointers[plane], pointers[plane] + words);
        }
    }
    ++state.signal_writes;
    return state.fail_signal_write ? 1U : 0U;
}

extern "C" std::uint32_t write_projected_signal_packed_from_scratch(
    void* const opaque,
    const std::uint32_t signal,
    const std::uint32_t width,
    const std::uint64_t* const aval,
    const std::uint64_t* const bval,
    const std::uint64_t* const plane2,
    const std::uint64_t* const plane3)
{
    auto& state = *static_cast<CallbackState*>(opaque);
    assert(signal == 0U && width == state.expected_width);
    const auto words = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(width) + 63U) / 64U);
    const std::array pointers { aval, bval, plane2, plane3 };
    assert((plane2 == nullptr) == state.signal_plane2.empty());
    assert((plane3 == nullptr) == state.signal_plane3.empty());
    for (std::size_t plane = 0U; plane < pointers.size(); ++plane) {
        if (pointers[plane] != nullptr) {
            check_callback_range(state, pointers[plane], words);
            state.written_planes[plane].assign(
                pointers[plane], pointers[plane] + words);
        }
    }
    ++state.signal_writes;
    ++state.projected_writes;
    return state.fail_signal_write ? 1U : 0U;
}

struct RuntimeDescriptor {
    TestRuntime defaults;
    fsim_jit_runtime_instance_v2 value { };
    fsim_jit_services_v2 services { };

    explicit RuntimeDescriptor(CallbackState& state)
        : value(abi(defaults))
        , services(copy_jit_services(value))
    {
        services.read_signal_packed = &read_signal_packed_into_scratch;
        services.write_signal_packed = &write_signal_packed_from_scratch;
        services.write_projected_signal_packed
            = &write_projected_signal_packed_from_scratch;
        services.container_read_packed
            = &read_container_packed_into_scratch;
        services.container_write_packed
            = &write_container_packed_from_scratch;
        value.services = &services;
        value.context = &state;
    }
};

[[nodiscard]] LlvmJit make_jit(
    const JitOptimizationLevel optimization,
    const bool debug_instrumentation = true)
{
    LlvmJitOptions options;
    options.optimization = optimization;
    options.cache_directory.clear();
    options.debug_instrumentation = debug_instrumentation;
    return LlvmJit { options };
}

[[nodiscard]] std::vector<std::uint64_t> pattern(
    const std::uint32_t width,
    const std::uint64_t seed)
{
    const auto words = static_cast<std::size_t>(
        (static_cast<std::uint64_t>(width) + 63U) / 64U);
    std::vector<std::uint64_t> result(words);
    for (std::size_t index = 0U; index < words; ++index) {
        result[index] = seed ^ (UINT64_C(0x9e3779b97f4a7c15) * (index + 1U));
    }
    const auto top_bits = width % 64U;
    if (top_bits != 0U) {
        result.back() &= (UINT64_C(1) << top_bits) - 1U;
    }
    return result;
}

void attach_frame_ranges(
    CallbackState& state,
    const std::vector<std::uint64_t>& aval,
    const std::vector<std::uint64_t>& bval,
    const std::vector<std::uint64_t>& plane2,
    const std::vector<std::uint64_t>& plane3)
{
    state.frame_planes = {
        AddressRange { aval.data(), aval.size() * sizeof(std::uint64_t) },
        AddressRange { bval.data(), bval.size() * sizeof(std::uint64_t) },
        AddressRange { plane2.data(), plane2.size() * sizeof(std::uint64_t) },
        AddressRange { plane3.data(), plane3.size() * sizeof(std::uint64_t) },
    };
}

void initialize_frame(
    const LlvmJit& jit,
    const JitProcessHandle handle,
    fsim_jit_frame_v2& frame,
    std::vector<std::uint64_t>& aval,
    std::vector<std::uint64_t>& bval,
    std::vector<std::uint8_t>& initialized,
    std::vector<std::uint64_t>& plane2,
    std::vector<std::uint64_t>& plane3)
{
    const auto layout = jit.frame_layout(handle);
    if (layout.uses_logic9) {
        jit.initialize_frame(
            handle, frame, aval, bval, initialized, plane2, plane3);
    } else {
        jit.initialize_frame(handle, frame, aval, bval, initialized);
    }
}

void seed_register(
    const LlvmJit& jit,
    const JitProcessHandle handle,
    const RegisterId register_id,
    std::vector<std::uint64_t>& aval,
    std::vector<std::uint64_t>& bval,
    std::vector<std::uint64_t>& plane2,
    std::vector<std::uint64_t>& plane3,
    const std::vector<std::uint64_t>& initial_aval,
    const std::vector<std::uint64_t>& initial_bval,
    const std::vector<std::uint64_t>& initial_plane2 = { },
    const std::vector<std::uint64_t>& initial_plane3 = { })
{
    const auto layout = jit.frame_layout(handle);
    assert(initial_aval.size() == initial_bval.size());
    const auto width = layout.register_widths.at(register_id);
    const auto word_count = static_cast<std::size_t>(
        (static_cast<std::uint64_t>(width) + 63U) / 64U);
    assert(initial_aval.size() <= word_count);
    assert(initial_plane2.empty() == initial_plane3.empty());
    assert(initial_plane2.empty() || initial_plane2.size() == initial_aval.size());
    const auto offset = layout.register_word_offsets.at(register_id);
    assert(offset <= aval.size() && initial_aval.size() <= aval.size() - offset);
    assert(offset <= bval.size() && initial_bval.size() <= bval.size() - offset);
    assert(initial_plane2.empty()
        || (offset <= plane2.size()
            && initial_plane2.size() <= plane2.size() - offset));
    assert(initial_plane3.empty()
        || (offset <= plane3.size()
            && initial_plane3.size() <= plane3.size() - offset));
    std::ranges::copy(initial_aval,
        aval.begin() + static_cast<std::ptrdiff_t>(offset));
    std::ranges::copy(initial_bval,
        bval.begin() + static_cast<std::ptrdiff_t>(offset));
    if (!initial_plane2.empty()) {
        std::ranges::copy(initial_plane2,
            plane2.begin() + static_cast<std::ptrdiff_t>(offset));
        std::ranges::copy(initial_plane3,
            plane3.begin() + static_cast<std::ptrdiff_t>(offset));
    }
}

void check_signal_read(
    const JitOptimizationLevel optimization,
    const std::uint32_t width,
    const bool fail)
{
    Process process;
    process.id = 240U;
    process.name = "wide_callback_buffer_signal_read";
    process.register_count = 2U;
    process.operations = {
        ReadSignal { 0U, 0U },
        ReadSignal { 1U, 0U },
        Pause { },
        Stop { }
    };
    const std::array<std::uint32_t, 1U> signal_widths { width };

    auto jit = make_jit(optimization);
    const auto symbol = "wide_callback_buffer_signal_"
        + std::to_string(width) + (fail ? "_failure" : "_success");
    jit.add_process(symbol, process, signal_widths);
    const auto handle = jit.lookup(symbol);
    const auto layout = jit.frame_layout(handle);
    assert(layout.register_widths.at(0U) == width);
    assert(layout.register_widths.at(1U) == width);
    assert(layout.register_values_persistent.at(0U) == 1U);
    assert(layout.register_values_persistent.at(1U) == 1U);

    std::vector<std::uint64_t> aval(layout.register_word_count);
    std::vector<std::uint64_t> bval(layout.register_word_count);
    std::vector<std::uint64_t> plane2(layout.register_word_count);
    std::vector<std::uint64_t> plane3(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v2 frame { };
    initialize_frame(jit, handle, frame, aval, bval, initialized, plane2, plane3);

    CallbackState callback;
    callback.expected_width = width;
    callback.signal_aval = pattern(width, UINT64_C(0x3b2d1f0a98765432));
    callback.signal_bval = pattern(width, UINT64_C(0x5a6b7c8d9eafb0c1));
    callback.fail_signal_read = fail;
    callback.partial_words = 1U;
    const auto sentinel_aval = pattern(width, UINT64_C(0x1122334455667788));
    const auto sentinel_bval = pattern(width, UINT64_C(0x8877665544332211));
    seed_register(jit, handle, 0U, aval, bval, plane2, plane3,
        sentinel_aval, sentinel_bval);
    attach_frame_ranges(callback, aval, bval, plane2, plane3);

    RuntimeDescriptor descriptor(callback);
    auto result = new_resume_result();
    if (fail) {
        expect_generated_runtime_error(
            [&] { static_cast<void>(jit.resume(
                handle, descriptor.value, frame, result)); },
            0U,
            JitGeneratedRuntimeErrorReason::signal_callback_failure,
            "exact-width signal runtime callback failed");
        const auto offset = layout.register_word_offsets.at(0U);
        assert(aval.at(offset) == callback.signal_aval.at(0U));
        assert(bval.at(offset) == callback.signal_bval.at(0U));
        for (std::size_t word = 1U; word < sentinel_aval.size(); ++word) {
            assert(aval.at(offset + word) == sentinel_aval[word]);
            assert(bval.at(offset + word) == sentinel_bval[word]);
        }
    } else {
        assert(jit.resume(handle, descriptor.value, frame, result)
            == JitResumeStatus::paused);
        for (const auto register_id : { 0U, 1U }) {
            const auto offset = layout.register_word_offsets.at(register_id);
            assert(std::equal(
                callback.signal_aval.begin(), callback.signal_aval.end(),
                aval.begin() + static_cast<std::ptrdiff_t>(offset)));
            assert(std::equal(
                callback.signal_bval.begin(), callback.signal_bval.end(),
                bval.begin() + static_cast<std::ptrdiff_t>(offset)));
        }
    }
    const auto expected_reads = fail ? 1U : 2U;
    assert(callback.signal_reads == expected_reads);
    assert(callback.checked_pointer_ranges == 2U * expected_reads);
    assert(callback.signal_aval_addresses.size() == expected_reads);
    if (!fail) {
        assert(callback.signal_aval_addresses[0U]
            == callback.signal_aval_addresses[1U]);
    }
}

void check_logic9_signal_read(const JitOptimizationLevel optimization)
{
    constexpr std::uint32_t width = 129U;
    Process process;
    process.id = 241U;
    process.name = "wide_callback_buffer_logic9_read";
    process.register_count = 1U;
    process.register_value_kinds = { ValueKind::logic9 };
    process.operations = { ReadSignal { 0U, 0U }, Pause { }, Stop { } };
    const std::array<std::uint32_t, 1U> signal_widths { width };
    const std::array<ValueKind, 1U> signal_kinds { ValueKind::logic9 };
    auto jit = make_jit(optimization);
    const auto symbol = "wide_callback_buffer_logic9_"
        + std::to_string(static_cast<unsigned>(optimization));
    jit.add_process(symbol, process, signal_widths, signal_kinds);
    const auto handle = jit.lookup(symbol);
    const auto layout = jit.frame_layout(handle);
    assert(layout.uses_logic9);
    std::vector<std::uint64_t> aval(layout.register_word_count);
    std::vector<std::uint64_t> bval(layout.register_word_count);
    std::vector<std::uint64_t> plane2(layout.register_word_count);
    std::vector<std::uint64_t> plane3(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v2 frame { };
    initialize_frame(jit, handle, frame, aval, bval, initialized, plane2, plane3);

    std::string text_value;
    constexpr std::array symbols { '0', '1', 'X', 'Z', 'U', 'W', 'L', 'H', '-' };
    for (std::uint32_t bit = 0U; bit < width; ++bit) {
        text_value.push_back(symbols[bit % symbols.size()]);
    }
    const auto packed = PackedLogic4::from_logic9_msb_string(text_value);
    CallbackState callback;
    callback.expected_width = width;
    callback.signal_aval.assign(packed.aval_words().begin(), packed.aval_words().end());
    callback.signal_bval.assign(packed.bval_words().begin(), packed.bval_words().end());
    callback.signal_plane2.assign(
        packed.logic9_plane_words(2U).begin(),
        packed.logic9_plane_words(2U).end());
    callback.signal_plane3.assign(
        packed.logic9_plane_words(3U).begin(),
        packed.logic9_plane_words(3U).end());
    attach_frame_ranges(callback, aval, bval, plane2, plane3);
    RuntimeDescriptor descriptor(callback);
    auto result = new_resume_result();
    assert(jit.resume(handle, descriptor.value, frame, result)
        == JitResumeStatus::paused);
    const auto offset = layout.register_word_offsets.at(0U);
    assert(std::equal(callback.signal_aval.begin(), callback.signal_aval.end(),
        aval.begin() + static_cast<std::ptrdiff_t>(offset)));
    assert(std::equal(callback.signal_bval.begin(), callback.signal_bval.end(),
        bval.begin() + static_cast<std::ptrdiff_t>(offset)));
    assert(std::equal(callback.signal_plane2.begin(), callback.signal_plane2.end(),
        plane2.begin() + static_cast<std::ptrdiff_t>(offset)));
    assert(std::equal(callback.signal_plane3.begin(), callback.signal_plane3.end(),
        plane3.begin() + static_cast<std::ptrdiff_t>(offset)));
    assert(callback.checked_pointer_ranges == 4U);
}

void check_transient_signal_callback(const JitOptimizationLevel optimization)
{
    constexpr std::uint32_t width = 129U;
    constexpr std::array<std::uint64_t, 3U> expected_aval {
        UINT64_C(0x1234), 0U, 0U
    };
    constexpr std::array<std::uint64_t, 3U> expected_bval { };
    Process process;
    process.id = 243U;
    process.name = "wide_callback_buffer_transient_read";
    process.register_count = 5U;
    process.static_sensitivity.push_back({ 0U, EdgeKind::any });
    process.operations = {
        ReadSignal { 0U, 0U },
        ReadSignal { 1U, 0U },
        LoadConstant {
            2U,
            PackedLogic4::from_word_planes(width, expected_aval, expected_bval) },
        Binary { BinaryOperator::equal, 3U, 0U, 2U },
        Assert {
            3U,
            "first transient wide callback read differs from its expected value",
            AssertionSeverity::failure,
            SourceLocation { } },
        Binary { BinaryOperator::equal, 4U, 1U, 2U },
        Assert {
            4U,
            "second transient wide callback read differs from its expected value",
            AssertionSeverity::failure,
            SourceLocation { } },
        WaitSensitivity { },
        Jump { 0U },
    };
    const std::array<std::uint32_t, 1U> signal_widths { width };
    auto jit = make_jit(optimization, false);
    const auto symbol = "wide_callback_buffer_transient_"
        + std::to_string(static_cast<unsigned>(optimization));
    jit.add_process(symbol, process, signal_widths);
    const auto handle = jit.lookup(symbol);
    const auto layout = jit.frame_layout(handle);
    assert(layout.register_values_persistent.at(0U) == 0U);
    assert(layout.register_values_persistent.at(1U) == 0U);

    std::vector<std::uint64_t> aval(layout.register_word_count);
    std::vector<std::uint64_t> bval(layout.register_word_count);
    std::vector<std::uint64_t> plane2(layout.register_word_count);
    std::vector<std::uint64_t> plane3(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v2 frame { };
    initialize_frame(jit, handle, frame, aval, bval, initialized, plane2, plane3);
    std::ranges::fill(aval, UINT64_C(0xaaaaaaaaaaaaaaaa));
    std::ranges::fill(bval, UINT64_C(0x5555555555555555));
    const auto before_aval = aval;
    const auto before_bval = bval;

    CallbackState callback;
    callback.expected_width = width;
    callback.signal_aval.assign(expected_aval.begin(), expected_aval.end());
    callback.signal_bval.assign(expected_bval.begin(), expected_bval.end());
    attach_frame_ranges(callback, aval, bval, plane2, plane3);
    RuntimeDescriptor descriptor(callback);
    auto result = new_resume_result();
    assert(jit.resume(handle, descriptor.value, frame, result)
        == JitResumeStatus::wait_sensitivity);
    assert(result.instruction == 7U);
    assert(callback.signal_reads == 2U);
    assert(callback.signal_aval_addresses.size() == 2U);
    assert(callback.signal_aval_addresses[0U]
        == callback.signal_aval_addresses[1U]);
    assert(callback.checked_pointer_ranges == 4U);
    assert(aval == before_aval);
    assert(bval == before_bval);
}

void check_signal_write(
    const JitOptimizationLevel optimization,
    const std::uint32_t width,
    const bool logic9,
    const bool fail,
    const unsigned route)
{
    const bool slice = route >= 3U && route <= 5U;
    const std::uint32_t target_width = slice ? std::max(129U, width + 7U) : width;
    CallbackState callback;
    callback.expected_width = width;
    callback.expected_offset = slice ? 7U : 0U;
    callback.fail_signal_write = fail;
    PackedLogic4 value { width, Logic4::zero };
    if (logic9) {
        std::string digits;
        constexpr std::string_view symbols { "UX01ZWLH-" };
        for (std::uint32_t bit = 0U; bit < width; ++bit) {
            digits.push_back(symbols[bit % symbols.size()]);
        }
        value = PackedLogic4::from_logic9_msb_string(digits);
        callback.signal_plane2.assign(value.logic9_plane_words(2U).begin(),
            value.logic9_plane_words(2U).end());
        callback.signal_plane3.assign(value.logic9_plane_words(3U).begin(),
            value.logic9_plane_words(3U).end());
    } else {
        const auto a = pattern(width, UINT64_C(0x0123456789abcdef));
        const auto b = pattern(width, UINT64_C(0x1020304050607080));
        value = PackedLogic4::from_word_planes(width, a, b);
    }
    Process process;
    process.id = 245U;
    process.name = "signal_write_callback_scratch";
    process.register_count = 1U;
    process.register_value_kinds = { logic9 ? ValueKind::logic9 : ValueKind::logic4 };
    process.operations = { LoadConstant { 0U, value } };
    constexpr auto domain = SignalUpdateDomain::systemverilog_active;
    switch (route) {
    case 0U:
        process.operations.push_back(WriteBlocking { 0U, 0U });
        callback.expected_mode = FSIM_JIT_PACKED_SIGNAL_WRITE_BLOCKING_V2;
        break;
    case 1U:
        process.operations.push_back(WriteUpdate { 0U, 0U, domain });
        callback.expected_mode = FSIM_JIT_PACKED_SIGNAL_WRITE_UPDATE_V2;
        callback.expected_domain = static_cast<std::uint32_t>(domain);
        break;
    case 2U:
        process.operations.push_back(WriteAfter { 0U, 0U, 3U, domain });
        callback.expected_mode = FSIM_JIT_PACKED_SIGNAL_WRITE_AFTER_V2;
        callback.expected_delay = 3U;
        callback.expected_domain = static_cast<std::uint32_t>(domain);
        break;
    case 3U:
        process.operations.push_back(WriteBlockingSlice { 0U, 0U, 7U });
        callback.expected_mode = FSIM_JIT_PACKED_SIGNAL_WRITE_BLOCKING_SLICE_V2;
        break;
    case 4U:
        process.operations.push_back(WriteUpdateSlice { 0U, 0U, 7U, domain });
        callback.expected_mode = FSIM_JIT_PACKED_SIGNAL_WRITE_UPDATE_SLICE_V2;
        callback.expected_domain = static_cast<std::uint32_t>(domain);
        break;
    case 5U:
        process.operations.push_back(WriteAfterSlice { 0U, 0U, 7U, 3U, domain });
        callback.expected_mode = FSIM_JIT_PACKED_SIGNAL_WRITE_AFTER_SLICE_V2;
        callback.expected_delay = 3U;
        callback.expected_domain = static_cast<std::uint32_t>(domain);
        break;
    default:
        assert(route == 6U);
        process.operations.push_back(WriteProjected {
            0U, 0U, 0U, 0U, ProjectedDelayMode::inertial });
        break;
    }
    process.operations.push_back(Pause { });
    process.operations.push_back(Stop { });
    const std::array widths { target_width };
    const std::array kinds { logic9 ? ValueKind::logic9 : ValueKind::logic4 };
    auto jit = make_jit(optimization);
    jit.add_process("signal_write_callback_scratch", process, widths, kinds);
    const auto handle = jit.lookup("signal_write_callback_scratch");
    const auto layout = jit.frame_layout(handle);
    std::vector<std::uint64_t> aval(layout.register_word_count);
    std::vector<std::uint64_t> bval(layout.register_word_count);
    std::vector<std::uint64_t> plane2(layout.register_word_count);
    std::vector<std::uint64_t> plane3(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v2 frame { };
    initialize_frame(jit, handle, frame, aval, bval, initialized, plane2, plane3);
    attach_frame_ranges(callback, aval, bval, plane2, plane3);
    RuntimeDescriptor descriptor(callback);
    if (route == 6U) {
        descriptor.services.write_signal_packed = nullptr;
    }
    auto result = new_resume_result();
    if (fail) {
        expect_generated_runtime_error([&] {
            static_cast<void>(jit.resume(handle, descriptor.value, frame, result));
        }, 1U, JitGeneratedRuntimeErrorReason::signal_callback_failure,
            "exact-width signal runtime callback failed");
    } else {
        assert(jit.resume(handle, descriptor.value, frame, result)
            == JitResumeStatus::paused);
    }
    const std::array expected_planes { value.aval_words(), value.bval_words(),
        value.logic9_plane_words(2U), value.logic9_plane_words(3U) };
    for (std::size_t plane = 0U; plane < (logic9 ? 4U : 2U); ++plane) {
        assert(std::ranges::equal(callback.written_planes[plane], expected_planes[plane]));
    }
    const auto register_offset = layout.register_word_offsets.at(0U);
    assert(std::equal(value.aval_words().begin(), value.aval_words().end(),
        aval.begin() + static_cast<std::ptrdiff_t>(register_offset)));
    assert(std::equal(value.bval_words().begin(), value.bval_words().end(),
        bval.begin() + static_cast<std::ptrdiff_t>(register_offset)));
    if (logic9) {
        assert(std::equal(value.logic9_plane_words(2U).begin(),
            value.logic9_plane_words(2U).end(),
            plane2.begin() + static_cast<std::ptrdiff_t>(register_offset)));
        assert(std::equal(value.logic9_plane_words(3U).begin(),
            value.logic9_plane_words(3U).end(),
            plane3.begin() + static_cast<std::ptrdiff_t>(register_offset)));
    }
    assert(callback.signal_writes == 1U);
    assert(callback.projected_writes == (route == 6U ? 1U : 0U));
    assert(callback.checked_pointer_ranges == (logic9 ? 4U : 2U));
}

void check_oversized_callback_scratch_declines()
{
    constexpr std::uint32_t width = 262145U;
    Process process;
    process.id = 244U;
    process.name = "wide_callback_buffer_over_budget";
    process.register_count = 1U;
    process.operations = { ReadSignal { 0U, 0U }, Pause { }, Stop { } };
    const std::array<std::uint32_t, 1U> signal_widths { width };
    auto jit = make_jit(JitOptimizationLevel::o0);
    expect_unsupported(
        [&] { jit.add_process("wide_callback_buffer_over_budget", process,
                    signal_widths); },
        "wide callback scratch exceeds the bounded stack budget");
}

void check_container_callbacks(
    const JitOptimizationLevel optimization,
    const std::uint32_t width)
{
    Process process;
    process.id = 242U;
    process.name = "wide_callback_buffer_container";
    process.register_count = 2U;
    process.container_register_count = 1U;
    ContainerType type;
    type.fixed = true;
    type.index_left = 0;
    type.index_right = 0;
    type.element_kind = ContainerElementKind::Packed;
    type.element_width = width;
    process.container_register_types = { type };
    process.operations = {
        LoadConstant { 0U, PackedLogic4::from_aval_bval(32U, 0U, 0U) },
        ContainerRead { 1U, 0U, 0U, true, false, false },
        ContainerWrite { 0U, 0U, 1U, true, false, false },
        Pause { },
        Stop { }
    };

    auto jit = make_jit(optimization);
    const auto symbol = "wide_callback_buffer_container_"
        + std::to_string(width) + "_"
        + std::to_string(static_cast<unsigned>(optimization));
    jit.add_process(symbol, process, { });
    const auto handle = jit.lookup(symbol);
    const auto layout = jit.frame_layout(handle);
    assert(layout.register_widths.at(1U) == width);
    std::vector<std::uint64_t> aval(layout.register_word_count);
    std::vector<std::uint64_t> bval(layout.register_word_count);
    std::vector<std::uint64_t> plane2(layout.register_word_count);
    std::vector<std::uint64_t> plane3(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v2 frame { };
    initialize_frame(jit, handle, frame, aval, bval, initialized, plane2, plane3);

    CallbackState callback;
    callback.container_aval = pattern(width, UINT64_C(0x0f1e2d3c4b5a6978));
    callback.container_bval = pattern(width, UINT64_C(0x8796a5b4c3d2e1f0));
    attach_frame_ranges(callback, aval, bval, plane2, plane3);
    RuntimeDescriptor descriptor(callback);
    // A table produced by an older v2 client ends at the previous tail. This
    // 32-bit-index process must continue to use only the legacy callbacks.
    descriptor.services.container_read_packed_index64 = nullptr;
    descriptor.services.struct_size = static_cast<std::uint32_t>(
        offsetof(fsim_jit_services_v2, container_read_packed_index64));
    auto result = new_resume_result();
    assert(jit.resume(handle, descriptor.value, frame, result)
        == JitResumeStatus::paused);
    const auto offset = layout.register_word_offsets.at(1U);
    assert(std::equal(callback.container_aval.begin(), callback.container_aval.end(),
        aval.begin() + static_cast<std::ptrdiff_t>(offset)));
    assert(std::equal(callback.container_bval.begin(), callback.container_bval.end(),
        bval.begin() + static_cast<std::ptrdiff_t>(offset)));
    assert(callback.container_write_aval == callback.container_aval);
    assert(callback.container_write_bval == callback.container_bval);
    assert(callback.container_reads == 1U);
    assert(callback.container_writes == 1U);
    assert(callback.checked_pointer_ranges == 4U);
    assert(callback.container_aval_addresses.size() == 2U);
    assert(callback.container_aval_addresses[0U]
        == callback.container_aval_addresses[1U]);

    // A failing read may mutate only part of its output. Those words must be
    // committed to the destination before the runtime-error edge, while the
    // callback must never see the register plane itself.
    CallbackState failing_read;
    failing_read.container_aval = pattern(width, UINT64_C(0xa1b2c3d4e5f60718));
    failing_read.container_bval = pattern(width, UINT64_C(0x1827364554637281));
    failing_read.fail_container_read = true;
    failing_read.partial_words = 1U;
    const auto sentinel_aval = pattern(width, UINT64_C(0x1122446688aaccee));
    const auto sentinel_bval = pattern(width, UINT64_C(0x13579bdf2468ace0));
    std::vector<std::uint64_t> failed_aval(layout.register_word_count);
    std::vector<std::uint64_t> failed_bval(layout.register_word_count);
    std::vector<std::uint64_t> failed_plane2(layout.register_word_count);
    std::vector<std::uint64_t> failed_plane3(layout.register_word_count);
    std::vector<std::uint8_t> failed_initialized(layout.register_count);
    fsim_jit_frame_v2 failed_frame { };
    initialize_frame(jit, handle, failed_frame, failed_aval, failed_bval,
        failed_initialized, failed_plane2, failed_plane3);
    seed_register(jit, handle, 1U, failed_aval, failed_bval,
        failed_plane2, failed_plane3, sentinel_aval, sentinel_bval);
    attach_frame_ranges(failing_read, failed_aval, failed_bval,
        failed_plane2, failed_plane3);
    RuntimeDescriptor failed_descriptor(failing_read);
    auto failed_result = new_resume_result();
    expect_generated_runtime_error(
        [&] { static_cast<void>(jit.resume(
            handle, failed_descriptor.value, failed_frame, failed_result)); },
        1U,
        JitGeneratedRuntimeErrorReason::container_callback_failure,
        "bounded container runtime callback failed");
    const auto failed_offset = layout.register_word_offsets.at(1U);
    assert(failed_aval.at(failed_offset) == failing_read.container_aval.at(0U));
    assert(failed_bval.at(failed_offset) == failing_read.container_bval.at(0U));
    for (std::size_t word = 1U; word < sentinel_aval.size(); ++word) {
        assert(failed_aval.at(failed_offset + word) == sentinel_aval[word]);
        assert(failed_bval.at(failed_offset + word) == sentinel_bval[word]);
    }
    assert(failing_read.checked_pointer_ranges == 2U);
    assert(failing_read.container_aval_addresses.size() == 1U);

    // The packed write callback receives a stable copy of its source. A
    // callback error must not alter the source register.
    CallbackState failing_write;
    failing_write.container_aval = callback.container_aval;
    failing_write.container_bval = callback.container_bval;
    failing_write.fail_container_write = true;
    std::vector<std::uint64_t> write_aval(layout.register_word_count);
    std::vector<std::uint64_t> write_bval(layout.register_word_count);
    std::vector<std::uint64_t> write_plane2(layout.register_word_count);
    std::vector<std::uint64_t> write_plane3(layout.register_word_count);
    std::vector<std::uint8_t> write_initialized(layout.register_count);
    fsim_jit_frame_v2 write_frame { };
    initialize_frame(jit, handle, write_frame, write_aval, write_bval,
        write_initialized, write_plane2, write_plane3);
    attach_frame_ranges(failing_write, write_aval, write_bval,
        write_plane2, write_plane3);
    RuntimeDescriptor write_descriptor(failing_write);
    auto write_result = new_resume_result();
    expect_generated_runtime_error(
        [&] { static_cast<void>(jit.resume(
            handle, write_descriptor.value, write_frame, write_result)); },
        2U,
        JitGeneratedRuntimeErrorReason::container_callback_failure,
        "bounded container runtime callback failed");
    assert(failing_write.container_write_aval == callback.container_aval);
    assert(failing_write.container_write_bval == callback.container_bval);
    assert(failing_write.checked_pointer_ranges == 4U);
    assert(failing_write.container_aval_addresses.size() == 2U);
    assert(failing_write.container_aval_addresses[0U]
        == failing_write.container_aval_addresses[1U]);
    const auto source_offset = layout.register_word_offsets.at(1U);
    assert(std::equal(
        callback.container_aval.begin(), callback.container_aval.end(),
        write_aval.begin() + static_cast<std::ptrdiff_t>(source_offset)));
    assert(std::equal(
        callback.container_bval.begin(), callback.container_bval.end(),
        write_bval.begin() + static_cast<std::ptrdiff_t>(source_offset)));
}

void check_container_read_index64_contract(
    const JitOptimizationLevel optimization)
{
    constexpr std::uint32_t width = 129U;
    constexpr std::uint64_t wide_index = UINT64_C(0x123456789abcdef0);
    Process process;
    process.id = 243U;
    process.name = "container_read_index64_abi";
    process.register_count = 2U;
    process.container_register_count = 1U;
    ContainerType type;
    type.element_kind = ContainerElementKind::Packed;
    type.element_width = width;
    process.container_register_types = { type };
    process.operations = {
        LoadConstant {
            0U,
            PackedLogic4::from_aval_bval(64U, wide_index, 0U)
        },
        ContainerRead { 1U, 0U, 0U, true, false, false },
        Pause { },
        Stop { }
    };

    auto jit = make_jit(optimization);
    jit.add_process("container_read_index64_abi", process, { });
    const auto handle = jit.lookup("container_read_index64_abi");
    const auto layout = jit.frame_layout(handle);
    assert(layout.register_widths.at(1U) == width);
    std::vector<std::uint64_t> aval(layout.register_word_count);
    std::vector<std::uint64_t> bval(layout.register_word_count);
    std::vector<std::uint64_t> plane2(layout.register_word_count);
    std::vector<std::uint64_t> plane3(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v2 frame { };
    initialize_frame(jit, handle, frame, aval, bval, initialized, plane2, plane3);

    CallbackState callback;
    callback.container_aval = pattern(width, UINT64_C(0x0f1e2d3c4b5a6978));
    callback.container_bval = pattern(width, UINT64_C(0x8796a5b4c3d2e1f0));
    attach_frame_ranges(callback, aval, bval, plane2, plane3);
    RuntimeDescriptor descriptor(callback);
    descriptor.services.container_read_packed_index64 = nullptr;
    descriptor.services.struct_size = static_cast<std::uint32_t>(
        offsetof(fsim_jit_services_v2, container_read_packed_index64));
    auto result = new_resume_result();
    expect_fatal_error(
        [&] {
            static_cast<void>(jit.resume(
                handle, descriptor.value, frame, result));
        },
        "services ABI structure is too small for "
        "container_read_packed_index64");
    assert(callback.container_read_index64_calls == 0U);
    assert(frame.program_counter == 0U);

    descriptor.services.struct_size = static_cast<std::uint32_t>(
        sizeof(fsim_jit_services_v2));
    expect_fatal_error(
        [&] {
            static_cast<void>(jit.resume(
                handle, descriptor.value, frame, result));
        },
        "container_read_packed_index64 for this process");
    assert(callback.container_read_index64_calls == 0U);
    assert(frame.program_counter == 0U);

    descriptor.services.container_read_packed_index64
        = &capture_container_read_index64_into_scratch;
    assert(jit.resume(handle, descriptor.value, frame, result)
        == JitResumeStatus::paused);
    assert(callback.container_read_index64_calls == 1U);
    assert(callback.container_read_index64_process == process.id);
    assert(callback.container_read_index64_instruction == 1U);
    assert(callback.container_read_index64_container == 0U);
    // This dynamic read uses signed-index validation (bit 1).
    assert(callback.container_read_index64_flags == 2U);
    assert(callback.container_read_index64_aval == wide_index);
    assert(callback.container_read_index64_bval == 0U);
    assert(callback.container_read_index64_word_count == 3U);
    assert(callback.checked_pointer_ranges == 2U);
    const auto offset = layout.register_word_offsets.at(1U);
    assert(std::equal(callback.container_aval.begin(),
        callback.container_aval.end(),
        aval.begin() + static_cast<std::ptrdiff_t>(offset)));
    assert(std::equal(callback.container_bval.begin(),
        callback.container_bval.end(),
        bval.begin() + static_cast<std::ptrdiff_t>(offset)));
}

} // namespace
} // namespace fsim::tests::compiler

int main()
{
    using namespace fsim::tests::compiler;
    constexpr std::array optimization_levels {
        JitOptimizationLevel::o0,
        JitOptimizationLevel::o2,
    };
    constexpr std::array widths { 65U, 129U, 256U, 1024U };
    for (const auto optimization : optimization_levels) {
        for (const auto width : widths) {
            check_signal_read(optimization, width, false);
            check_signal_read(optimization, width, true);
            check_container_callbacks(optimization, width);
            check_signal_write(optimization, width, false, false, 0U);
            check_signal_write(optimization, width, false, true, 0U);
        }
        check_logic9_signal_read(optimization);
        check_container_read_index64_contract(optimization);
        check_signal_write(optimization, 129U, true, false, 0U);
        check_signal_write(optimization, 129U, true, true, 0U);
        for (unsigned route = 1U; route <= 6U; ++route) {
            check_signal_write(optimization, 129U, false, false, route);
        }
        check_signal_write(optimization, 129U, false, true, 6U);
        check_signal_write(optimization, 129U, true, false, 6U);
        check_signal_write(optimization, 129U, true, true, 6U);
        // A one-word slice of a wide signal must also receive scratch.
        check_signal_write(optimization, 1U, false, false, 3U);
        check_signal_write(optimization, 64U, true, false, 5U);
        check_transient_signal_callback(optimization);
    }
    check_oversized_callback_scratch_declines();
    return 0;
}
