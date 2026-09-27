// SPDX-License-Identifier: Apache-2.0
#include "../../src/app/application_internal.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

#if defined(FSIM_HAS_LLVM)

using namespace fsim;
using namespace fsim::runtime;
using namespace fsim::runtime::simir;

constexpr std::uint32_t wide_width = 137U;

struct Capture {
    RunResult run;
    ContainerValue data;
    ContainerValue byte_count;
};

struct FixedMemoryCapture {
    RunResult run;
    ContainerValue memory;
    ContainerValue byte_count;
};

struct FileScanCapture {
    RunResult run;
    std::array<PackedLogic4, 5U> outputs;
};

struct Logic9FileCapture {
    RunResult run;
    std::array<PackedLogic4, 3U> outputs;
};

void write_input(
    const std::filesystem::path& path,
    const std::span<const std::uint8_t> bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!bytes.empty()) {
        output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    }
    assert(output.good());
}

PackedLogic4 packed_from_bytes(
    const std::span<const std::uint8_t> bytes,
    const std::uint32_t width)
{
    PackedLogic4 result(width, Logic4::zero);
    const auto byte_count = (width + 7U) / 8U;
    for (std::size_t byte_index = 0U;
        byte_index < bytes.size() && byte_index < byte_count;
        ++byte_index) {
        const auto offset
            = static_cast<std::size_t>(byte_count - byte_index - 1U) * 8U;
        for (std::size_t bit = 0U; bit < 8U; ++bit) {
            const auto destination = offset + bit;
            if (destination < width
                && ((bytes[byte_index] >> bit) & 1U) != 0U) {
                result.set(destination, Logic4::one);
            }
        }
    }
    return result;
}

PackedLogic4 xor_mask(PackedLogic4 value)
{
    for (const auto bit : { 0U, 64U, 136U }) {
        value.set(
            bit,
            value.get(bit) == Logic4::one ? Logic4::zero : Logic4::one);
    }
    return value;
}

Capture run_packed_target(
    const std::filesystem::path& input,
    const bool compiled,
    const compiler::JitOptimizationLevel level)
{
    const std::array<std::uint32_t, 0U> no_signal_widths { };
    Interpreter interpreter;
    interpreter.set_file_root(input.parent_path());
    ContainerType data_type;
    data_type.element_width = wide_width;
    const auto data_object = interpreter.add_container_object({
        "fread_data",
        ContainerValue { data_type, { PackedLogic4(wide_width, Logic4::zero) }, { } },
        std::nullopt,
    });
    ContainerType count_type;
    count_type.element_width = 32U;
    const auto count_object = interpreter.add_container_object({
        "fread_byte_count",
        ContainerValue { count_type, { PackedLogic4(32U, Logic4::zero) }, { } },
        std::nullopt,
    });

    Process process;
    process.id = 0U;
    process.name = "wide_file_binary_read";
    process.register_count = 6U;
    process.string_register_count = 2U;
    process.operations = {
        LoadStringConstant { 0U, input.filename().string() },
        LoadStringConstant { 1U, "rb" },
        FileOpen { 0U, 0U, 1U },
        FileBinaryRead {
            2U, 0U, 1U, FileBinaryTargetKind::packed_register,
            wide_width, false, 0U, 0U, false, false },
        LoadConstant {
            3U,
            [] {
                PackedLogic4 value(wide_width, Logic4::zero);
                value.set(0U, Logic4::one);
                value.set(64U, Logic4::one);
                value.set(136U, Logic4::one);
                return value;
            }() },
        Binary { BinaryOperator::bit_xor, 4U, 1U, 3U },
        LoadConstant { 5U, PackedLogic4(32U, Logic4::zero) },
        WriteContainerObjectElement {
            data_object, 5U, 4U, false, false, false,
            std::nullopt, std::nullopt },
        WriteContainerObjectElement {
            count_object, 5U, 2U, false, false, false,
            std::nullopt, std::nullopt },
        FileClose { 0U },
        Halt { },
    };

    std::unique_ptr<compiler::LlvmJit> jit;
    std::optional<compiler::JitProcessHandle> handle;
    if (compiled) {
        compiler::LlvmJitOptions options;
        options.optimization = level;
        options.debug_instrumentation = false;
        jit = std::make_unique<compiler::LlvmJit>(std::move(options));
        jit->add_process(process.name, process, no_signal_widths);
        handle = jit->lookup(process.name);
    }

    const auto process_id = interpreter.add_process(std::move(process));
    if (compiled) {
        interpreter.set_process_executor(
            process_id,
            std::make_unique<app::application_detail::LlvmProcessExecutor>(
                *jit, *handle, interpreter.process_program(process_id),
                no_signal_widths,
                std::span<const ValueKind> { },
                std::span<const ResolutionKind> { }));
    }
    const auto run = interpreter.run();
    return {
        run,
        interpreter.container_object_value(data_object),
        interpreter.container_object_value(count_object),
    };
}

FixedMemoryCapture run_fixed_memory_target(
    const std::filesystem::path& input,
    const bool compiled,
    const compiler::JitOptimizationLevel level)
{
    const std::array<std::uint32_t, 0U> no_signal_widths { };
    Interpreter interpreter;
    interpreter.set_file_root(input.parent_path());
    ContainerType memory_type;
    memory_type.element_width = 8U;
    memory_type.fixed = true;
    memory_type.index_left = 0;
    memory_type.index_right = 2;
    memory_type.dimensions = { { 0, 2 } };
    const auto memory_object = interpreter.add_container_object({
        "fread_fixed_memory",
        ContainerValue {
            memory_type,
            { PackedLogic4(8U, Logic4::zero),
                PackedLogic4(8U, Logic4::zero),
                PackedLogic4(8U, Logic4::zero) },
            { },
        },
        std::nullopt,
    });
    ContainerType count_type;
    count_type.element_width = 32U;
    const auto count_object = interpreter.add_container_object({
        "fread_memory_byte_count",
        ContainerValue { count_type, { PackedLogic4(32U, Logic4::zero) }, { } },
        std::nullopt,
    });

    Process process;
    process.id = 0U;
    process.name = "fread_fixed_memory_bounds";
    process.register_count = 5U;
    process.string_register_count = 2U;
    process.operations = {
        LoadStringConstant { 0U, input.filename().string() },
        LoadStringConstant { 1U, "rb" },
        FileOpen { 0U, 0U, 1U },
        LoadConstant { 2U, PackedLogic4::from_aval_bval(32U, 1U, 0U) },
        LoadConstant { 3U, PackedLogic4::from_aval_bval(32U, 1U, 0U) },
        FileBinaryRead {
            1U, 0U, memory_object, FileBinaryTargetKind::container_object,
            8U, false, 2U, 3U, true, true },
        LoadConstant { 4U, PackedLogic4(32U, Logic4::zero) },
        WriteContainerObjectElement {
            count_object, 4U, 1U, false, false, false,
            std::nullopt, std::nullopt },
        FileClose { 0U },
        Halt { },
    };

    std::unique_ptr<compiler::LlvmJit> jit;
    std::optional<compiler::JitProcessHandle> handle;
    if (compiled) {
        compiler::LlvmJitOptions options;
        options.optimization = level;
        options.debug_instrumentation = false;
        jit = std::make_unique<compiler::LlvmJit>(std::move(options));
        jit->add_process(process.name, process, no_signal_widths);
        handle = jit->lookup(process.name);
    }

    const auto process_id = interpreter.add_process(std::move(process));
    if (compiled) {
        interpreter.set_process_executor(
            process_id,
            std::make_unique<app::application_detail::LlvmProcessExecutor>(
                *jit, *handle, interpreter.process_program(process_id),
                no_signal_widths,
                std::span<const ValueKind> { },
                std::span<const ResolutionKind> { }));
    }
    const auto run = interpreter.run();
    return {
        run,
        interpreter.container_object_value(memory_object),
        interpreter.container_object_value(count_object),
    };
}

FileScanCapture run_file_scan(
    const std::filesystem::path& input,
    const bool compiled,
    const compiler::JitOptimizationLevel level)
{
    const std::array<std::uint32_t, 5U> signal_widths {
        wide_width, wide_width, 1U, 32U, 32U };
    const std::array<ValueKind, 5U> signal_kinds {
        ValueKind::logic4, ValueKind::logic4, ValueKind::logic4,
        ValueKind::logic4, ValueKind::logic4 };
    const std::array<ResolutionKind, 5U> signal_resolutions {
        ResolutionKind::none, ResolutionKind::none, ResolutionKind::none,
        ResolutionKind::none, ResolutionKind::none };
    Interpreter interpreter;
    interpreter.set_file_root(input.parent_path());
    std::array<SignalId, 5U> outputs { };
    for (std::size_t index = 0; index < outputs.size(); ++index) {
        outputs[index] = interpreter.add_signal({
            "file_scan_output_" + std::to_string(index),
            PackedLogic4(signal_widths[index], Logic4::x),
            signal_resolutions[index], signal_kinds[index] });
    }

    PackedLogic4 initial_target(wide_width, Logic4::zero);
    initial_target.set(0U, Logic4::one);
    initial_target.set(64U, Logic4::z);
    initial_target.set(101U, Logic4::one);
    initial_target.set(136U, Logic4::x);
    Process process;
    process.id = 0U;
    process.name = "wide_file_scan_copy_out";
    process.register_count = 6U;
    process.string_register_count = 3U;
    process.operations = {
        LoadConstant { 0U, PackedLogic4(wide_width, Logic4::one) },
        LoadConstant { 1U, initial_target },
        LoadConstant { 2U, PackedLogic4(1U, Logic4::one) },
        LoadConstant { 3U, PackedLogic4(32U, Logic4::one) },
        LoadStringConstant { 0U, input.filename().string() },
        LoadStringConstant { 1U, "r" },
        FileOpen { 4U, 0U, 1U },
        FileScan {
            3U, 4U, 0U, false,
            {
                { "", InputScanFormat::decimal, 0U, false,
                    { InputScanTargetKind::packed_register,
                        0U, wide_width, false } },
                { "", InputScanFormat::decimal, 0U, false,
                    { InputScanTargetKind::packed_register,
                        1U, wide_width, false } },
            },
            "", false, 2U },
        LoadStringConstant { 2U, "7" },
        FileScan {
            5U, 4U, 2U, true,
            { { "", InputScanFormat::decimal, 0U, false,
                { InputScanTargetKind::packed_register,
                    5U, 32U, false } } },
            "", false, std::nullopt, false },
        FileClose { 4U },
        WriteBlocking { outputs[0], 0U },
        WriteBlocking { outputs[1], 1U },
        WriteBlocking { outputs[2], 2U },
        WriteBlocking { outputs[3], 3U },
        WriteBlocking { outputs[4], 5U },
        Halt { },
    };

    std::unique_ptr<compiler::LlvmJit> jit;
    std::optional<compiler::JitProcessHandle> handle;
    if (compiled) {
        compiler::LlvmJitOptions options;
        options.optimization = level;
        options.debug_instrumentation = false;
        jit = std::make_unique<compiler::LlvmJit>(std::move(options));
        jit->add_process(
            process.name, process, signal_widths, signal_kinds);
        handle = jit->lookup(process.name);
    }

    const auto process_id = interpreter.add_process(std::move(process));
    if (compiled) {
        interpreter.set_process_executor(
            process_id,
            std::make_unique<app::application_detail::LlvmProcessExecutor>(
                *jit, *handle, interpreter.process_program(process_id),
                signal_widths, signal_kinds, signal_resolutions));
    }
    const auto run = interpreter.run();
    return {
        run,
        { interpreter.signal_value(outputs[0]),
            interpreter.signal_value(outputs[1]),
            interpreter.signal_value(outputs[2]),
            interpreter.signal_value(outputs[3]),
            interpreter.signal_value(outputs[4]) },
    };
}

Logic9FileCapture run_logic9_file_target(
    const std::filesystem::path& input,
    const bool compiled,
    const compiler::JitOptimizationLevel level)
{
    const std::array<std::uint32_t, 3U> signal_widths { 1U, 1U, 1U };
    const std::array<ValueKind, 3U> signal_kinds {
        ValueKind::logic4, ValueKind::logic4, ValueKind::logic4 };
    const std::array<ResolutionKind, 3U> signal_resolutions {
        ResolutionKind::none, ResolutionKind::none, ResolutionKind::none };
    Interpreter interpreter;
    interpreter.set_file_root(input.parent_path());
    std::array<SignalId, 3U> outputs { };
    for (std::size_t index = 0; index < outputs.size(); ++index) {
        outputs[index] = interpreter.add_signal({
            "logic9_fread_bit_" + std::to_string(index),
            PackedLogic4(1U, Logic4::x),
            signal_resolutions[index], signal_kinds[index] });
    }

    Process process;
    process.id = 0U;
    process.name = "logic9_file_binary_read";
    process.register_count = 6U;
    process.string_register_count = 2U;
    process.register_value_kinds = {
        ValueKind::logic4, ValueKind::logic9, ValueKind::logic4,
        ValueKind::logic4, ValueKind::logic4, ValueKind::logic4 };
    process.operations = {
        LoadStringConstant { 0U, input.filename().string() },
        LoadStringConstant { 1U, "rb" },
        FileOpen { 0U, 0U, 1U },
        FileBinaryRead {
            2U, 0U, 1U, FileBinaryTargetKind::packed_register,
            wide_width, false, 0U, 0U, false, false },
        Extract { 3U, 1U, 0U, 1U },
        Extract { 4U, 1U, 64U, 1U },
        Extract { 5U, 1U, 136U, 1U },
        WriteBlocking { outputs[0], 3U },
        WriteBlocking { outputs[1], 4U },
        WriteBlocking { outputs[2], 5U },
        FileClose { 0U },
        Halt { },
    };

    std::unique_ptr<compiler::LlvmJit> jit;
    std::optional<compiler::JitProcessHandle> handle;
    if (compiled) {
        compiler::LlvmJitOptions options;
        options.optimization = level;
        options.debug_instrumentation = false;
        jit = std::make_unique<compiler::LlvmJit>(std::move(options));
        jit->add_process(
            process.name, process, signal_widths, signal_kinds);
        handle = jit->lookup(process.name);
    }

    const auto process_id = interpreter.add_process(std::move(process));
    if (compiled) {
        interpreter.set_process_executor(
            process_id,
            std::make_unique<app::application_detail::LlvmProcessExecutor>(
                *jit, *handle, interpreter.process_program(process_id),
                signal_widths, signal_kinds, signal_resolutions));
    }
    const auto run = interpreter.run();
    return {
        run,
        { interpreter.signal_value(outputs[0]),
            interpreter.signal_value(outputs[1]),
            interpreter.signal_value(outputs[2]) },
    };
}

void compile_file_only_logic9_frame_reload()
{
    const std::array<std::uint32_t, 1U> signal_widths { 1U };
    const std::array<ValueKind, 1U> signal_kinds { ValueKind::logic4 };
    Process process;
    process.id = 0U;
    process.name = "file_only_logic9_frame_reload";
    process.register_count = 4U;
    process.register_value_kinds = {
        ValueKind::logic4, ValueKind::logic9,
        ValueKind::logic4, ValueKind::logic4 };
    process.operations = {
        LoadConstant { 0U, PackedLogic4(32U, Logic4::zero) },
        FileBinaryRead {
            2U, 0U, 1U, FileBinaryTargetKind::packed_register,
            wide_width, false, 0U, 0U, false, false },
        Extract { 3U, 1U, 136U, 1U },
        WriteBlocking { 0U, 3U },
        Halt { },
    };
    compiler::LlvmJitOptions options;
    options.debug_instrumentation = false;
    compiler::LlvmJit jit { std::move(options) };
    jit.add_process(process.name, process, signal_widths, signal_kinds);
    const auto handle = jit.lookup(process.name);
    assert(jit.frame_layout(handle).uses_logic9);
}

void require_same_run(const RunResult& expected, const RunResult& actual)
{
    assert(actual.status == expected.status);
    assert(actual.time == expected.time);
    assert(actual.delta == expected.delta);
}

#endif

} // namespace

int main()
{
#if defined(FSIM_HAS_LLVM)
    const auto serial
        = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path()
        / ("fsim-wide-fread-" + std::to_string(serial));
    std::filesystem::create_directories(directory);

    const std::array<std::uint8_t, 18U> full_bytes {
        0x01U, 0x02U, 0x80U, 0x11U, 0x22U, 0x44U,
        0x88U, 0x10U, 0x20U, 0x40U, 0x80U, 0x03U,
        0x06U, 0x0cU, 0x18U, 0x30U, 0x60U, 0xc0U,
    };
    const std::array<std::uint8_t, 7U> partial_bytes {
        0x01U, 0x02U, 0x80U, 0x11U, 0x22U, 0x44U, 0x88U,
    };
    const std::array<std::uint8_t, 2U> scan_partial_bytes { '1', '7' };
    const std::array<std::uint8_t, 0U> scan_empty_bytes { };
    const std::array<std::uint8_t, 3U> bounded_bytes {
        0x31U, 0xa5U, 0x7cU,
    };
    const auto full_path = directory / "full.bin";
    const auto partial_path = directory / "partial.bin";
    const auto bounded_path = directory / "bounded.bin";
    const auto scan_partial_path = directory / "scan-partial.txt";
    const auto scan_empty_path = directory / "scan-empty.txt";
    write_input(full_path, full_bytes);
    write_input(partial_path, partial_bytes);
    write_input(bounded_path, bounded_bytes);
    write_input(scan_partial_path, scan_partial_bytes);
    write_input(scan_empty_path, scan_empty_bytes);

    for (const auto& [path, bytes] : {
             std::pair { full_path, std::span<const std::uint8_t> { full_bytes } },
             std::pair { partial_path, std::span<const std::uint8_t> { partial_bytes } },
         }) {
        const auto reference = run_packed_target(
            path, false, compiler::JitOptimizationLevel::o0);
        assert(reference.run.status == RunStatus::completed);
        assert(reference.data.elements.front()
            == xor_mask(packed_from_bytes(bytes, wide_width)));
        assert(reference.byte_count.elements.front()
            == PackedLogic4::from_aval_bval(
                32U, static_cast<std::uint32_t>(bytes.size()), 0U));
        for (const auto level : {
                 compiler::JitOptimizationLevel::o0,
                 compiler::JitOptimizationLevel::o2 }) {
            const auto native = run_packed_target(path, true, level);
            require_same_run(reference.run, native.run);
            assert(native.data == reference.data);
            assert(native.byte_count == reference.byte_count);
        }
    }

    const auto bounded_reference = run_fixed_memory_target(
        bounded_path, false, compiler::JitOptimizationLevel::o0);
    assert(bounded_reference.run.status == RunStatus::completed);
    assert(bounded_reference.memory.elements[0]
        == PackedLogic4(8U, Logic4::zero));
    assert(bounded_reference.memory.elements[1]
        == PackedLogic4::from_aval_bval(8U, bounded_bytes[0], 0U));
    assert(bounded_reference.memory.elements[2]
        == PackedLogic4(8U, Logic4::zero));
    assert(bounded_reference.byte_count.elements.front()
        == PackedLogic4::from_aval_bval(32U, 1U, 0U));
    for (const auto level : {
             compiler::JitOptimizationLevel::o0,
             compiler::JitOptimizationLevel::o2 }) {
        const auto native = run_fixed_memory_target(bounded_path, true, level);
        require_same_run(bounded_reference.run, native.run);
        assert(native.memory == bounded_reference.memory);
        assert(native.byte_count == bounded_reference.byte_count);
    }

    PackedLogic4 initial_target(wide_width, Logic4::zero);
    initial_target.set(0U, Logic4::one);
    initial_target.set(64U, Logic4::z);
    initial_target.set(101U, Logic4::one);
    initial_target.set(136U, Logic4::x);
    for (const auto& [path, assignment_count] : {
             std::pair { scan_partial_path, 1U },
             std::pair { scan_empty_path, 0U },
         }) {
        const auto reference = run_file_scan(
            path, false, compiler::JitOptimizationLevel::o0);
        assert(reference.run.status == RunStatus::completed);
        auto expected_target = PackedLogic4(
            wide_width,
            assignment_count == 0U ? Logic4::one : Logic4::zero);
        if (assignment_count != 0U) {
            expected_target.set(0U, Logic4::one);
            expected_target.set(4U, Logic4::one);
        }
        assert(reference.outputs[0] == expected_target);
        assert(reference.outputs[1] == initial_target);
        assert(reference.outputs[2]
            == PackedLogic4::from_aval_bval(1U, 0U, 0U));
        assert(reference.outputs[3]
            == PackedLogic4::from_aval_bval(
                32U,
                assignment_count == 0U
                    ? std::numeric_limits<std::uint32_t>::max()
                    : assignment_count,
                0U));
        assert(reference.outputs[4]
            == PackedLogic4::from_aval_bval(32U, 1U, 0U));
        for (const auto level : {
                 compiler::JitOptimizationLevel::o0,
                 compiler::JitOptimizationLevel::o2 }) {
            const auto native = run_file_scan(path, true, level);
            require_same_run(reference.run, native.run);
            assert(native.outputs == reference.outputs);
        }
    }

    const auto logic9_reference = run_logic9_file_target(
        full_path, false, compiler::JitOptimizationLevel::o0);
    assert(logic9_reference.run.status == RunStatus::completed);
    const auto logic9_expected = packed_from_bytes(full_bytes, wide_width);
    for (std::size_t index = 0; index < logic9_reference.outputs.size(); ++index) {
        const auto bit = std::array<std::size_t, 3U> { 0U, 64U, 136U }[index];
        assert(logic9_reference.outputs[index]
            == PackedLogic4(1U, logic9_expected.get(bit)));
    }
    for (const auto level : {
             compiler::JitOptimizationLevel::o0,
             compiler::JitOptimizationLevel::o2 }) {
        const auto native = run_logic9_file_target(full_path, true, level);
        require_same_run(logic9_reference.run, native.run);
        assert(native.outputs == logic9_reference.outputs);
    }
    compile_file_only_logic9_frame_reload();

    std::error_code error;
    std::filesystem::remove_all(directory, error);
#endif
    return 0;
}
