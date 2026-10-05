// SPDX-License-Identifier: Apache-2.0
#include "../../src/app/application_internal.hpp"
#include "../../src/app/application_container_profile.hpp"

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

struct FileCacheMutationCapture {
    RunResult run;
    std::array<PackedLogic4, 6U> outputs;
};

struct ContainerSnapshotCapture {
    RunResult run;
    std::array<PackedLogic4, 10U> outputs;
};

struct ExposedContainerReadCapture {
    RunResult run;
    PackedLogic4 output;
    PackedLogic4 retained_value;
};

struct ContainerSliceReadCapture {
    RunResult run;
    PackedLogic4 output;
};

struct ContainerObjectReadCapture {
    RunResult run;
    PackedLogic4 output;
    std::optional<ContainerValue> debug_local;
    std::string error;
    std::optional<PackedLogic4> second_output;
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

FileCacheMutationCapture run_file_cache_mutations(
    const std::filesystem::path& input,
    const bool compiled,
    const compiler::JitOptimizationLevel level)
{
    const std::array<std::uint32_t, 6U> signal_widths {
        8U, 8U, 8U, 8U, 16U, 16U };
    const std::array<ValueKind, 6U> signal_kinds {
        ValueKind::logic4, ValueKind::logic4, ValueKind::logic4,
        ValueKind::logic4, ValueKind::logic4, ValueKind::logic4 };
    const std::array<ResolutionKind, 6U> signal_resolutions {
        ResolutionKind::none, ResolutionKind::none,
        ResolutionKind::none, ResolutionKind::none,
        ResolutionKind::none, ResolutionKind::none };
    Interpreter interpreter;
    interpreter.set_file_root(input.parent_path());
    const auto zero8 = PackedLogic4(8U, Logic4::zero);
    std::array<SignalId, 6U> outputs { };
    const std::array<std::string_view, 6U> names {
        "scan_target", "scan_observed", "fread_target",
        "fread_observed", "container_bridge", "container_observed" };
    for (std::size_t index = 0U; index < outputs.size(); ++index) {
        auto initial = PackedLogic4(
            signal_widths[index],
            index == 4U ? Logic4::zero : Logic4::x);
        if (index == 0U) {
            initial = PackedLogic4::from_aval_bval(8U, 0x11U, 0U);
        } else if (index == 2U) {
            initial = PackedLogic4::from_aval_bval(8U, 0x22U, 0U);
        }
        outputs[index] = interpreter.add_signal({
            "file_cache_" + std::string { names[index] },
            initial,
            signal_resolutions[index], signal_kinds[index] });
    }

    ContainerType memory_type;
    memory_type.element_width = 8U;
    memory_type.fixed = true;
    memory_type.index_left = 0;
    memory_type.index_right = 1;
    memory_type.dimensions = { { 0, 1 } };
    const auto memory = interpreter.add_container_object({
        "file_cache_memory",
        ContainerValue {
            memory_type,
            { zero8, zero8 },
            { },
        },
        std::nullopt,
    });
    interpreter.add_container_signal_alias({
        memory, outputs[4], true, true });

    // Pin the primed targets before start so the JIT must use read_signal's
    // callback path rather than loading the direct planes for these reads.
    const auto& scan_target_reference = interpreter.signal_value(outputs[0]);
    const auto& fread_target_reference = interpreter.signal_value(outputs[2]);
    const auto& container_bridge_reference
        = interpreter.signal_value(outputs[4]);

    Process process;
    process.id = 0U;
    process.name = "file_callback_signal_cache_mutations";
    process.register_count = 8U;
    process.string_register_count = 3U;
    process.operations = {
        LoadStringConstant { 0U, "a5" },
        LoadStringConstant { 1U, input.filename().string() },
        LoadStringConstant { 2U, "rb" },
        FileOpen { 2U, 1U, 2U },
        ReadSignal { 0U, outputs[0] },
        FileScan {
            3U, 2U, 0U, true,
            { { "", InputScanFormat::hexadecimal, 0U, false,
                { InputScanTargetKind::packed_signal,
                    outputs[0], 8U, false } } },
            "", false, std::nullopt, false },
        ReadSignal { 1U, outputs[0] },
        WriteBlocking { outputs[1], 1U },
        ReadSignal { 0U, outputs[2] },
        FileBinaryRead {
            3U, 2U, outputs[2], FileBinaryTargetKind::packed_signal,
            8U, false, 0U, 0U, false, false },
        ReadSignal { 1U, outputs[2] },
        WriteBlocking { outputs[3], 1U },
        FileClose { 2U },
        LoadStringConstant {
            1U, "cache-container.bin" },
        FileOpen { 2U, 1U, 2U },
        LoadConstant { 4U, PackedLogic4::from_aval_bval(32U, 0U, 0U) },
        LoadConstant { 5U, PackedLogic4::from_aval_bval(32U, 2U, 0U) },
        ReadSignal { 6U, outputs[4] },
        FileBinaryRead {
            3U, 2U, memory, FileBinaryTargetKind::container_object,
            8U, false, 4U, 5U, true, true },
        ReadSignal { 7U, outputs[4] },
        WriteBlocking { outputs[5], 7U },
        FileClose { 2U },
        Halt { },
    };

    std::unique_ptr<compiler::LlvmJit> jit;
    std::optional<compiler::JitProcessHandle> handle;
    if (compiled) {
        compiler::LlvmJitOptions options;
        options.optimization = level;
        options.debug_instrumentation = false;
        jit = std::make_unique<compiler::LlvmJit>(std::move(options));
        jit->add_process(process.name, process, signal_widths, signal_kinds);
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
    std::array<PackedLogic4, 6U> values;
    for (std::size_t index = 0U; index < outputs.size(); ++index) {
        values[index] = interpreter.signal_value(outputs[index]);
    }
    assert(scan_target_reference
        == PackedLogic4::from_aval_bval(8U, 0xa5U, 0U));
    assert(fread_target_reference
        == PackedLogic4::from_aval_bval(8U, 0x96U, 0U));
    assert(container_bridge_reference
        == PackedLogic4::from_aval_bval(16U, 0x1234U, 0U));
    return { run, std::move(values) };
}

ContainerSnapshotCapture run_container_object_snapshot(
    const bool compiled,
    const compiler::JitOptimizationLevel level)
{
    const std::array<std::uint32_t, 10U> signal_widths {
        8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 16U, 8U };
    const std::array<ValueKind, 10U> signal_kinds {
        ValueKind::logic4, ValueKind::logic4, ValueKind::logic4,
        ValueKind::logic4, ValueKind::logic4, ValueKind::logic4,
        ValueKind::logic4, ValueKind::logic4, ValueKind::logic4,
        ValueKind::logic4 };
    const std::array<ResolutionKind, 10U> signal_resolutions {
        ResolutionKind::none, ResolutionKind::none,
        ResolutionKind::none, ResolutionKind::none,
        ResolutionKind::none, ResolutionKind::none,
        ResolutionKind::none, ResolutionKind::none,
        ResolutionKind::none, ResolutionKind::none };
    Interpreter interpreter;
    std::array<SignalId, 10U> outputs { };
    for (std::size_t index = 0U; index < outputs.size(); ++index) {
        const auto initial = index == 8U
            ? PackedLogic4::from_aval_bval(16U, 0x1122U, 0U)
            : PackedLogic4(8U, Logic4::x);
        outputs[index] = interpreter.add_signal({
            "container_snapshot_" + std::to_string(index), initial,
            signal_resolutions[index], signal_kinds[index] });
    }

    ContainerType memory_type;
    memory_type.element_width = 8U;
    memory_type.fixed = true;
    memory_type.index_left = 0;
    memory_type.index_right = 1;
    memory_type.dimensions = { { 0, 1 } };
    const auto object = interpreter.add_container_object({
        "container_snapshot_memory",
        ContainerValue {
            memory_type,
            { PackedLogic4::from_aval_bval(8U, 0x11U, 0U),
                PackedLogic4::from_aval_bval(8U, 0x22U, 0U) },
            { },
        },
        std::nullopt,
    });
    interpreter.add_container_signal_alias({
        object, outputs[8], true, true });

    Process observer;
    observer.id = 0U;
    observer.name = "container_object_snapshot_across_wait";
    observer.register_count = 4U;
    observer.container_register_count = 1U;
    observer.container_register_types = { memory_type };
    observer.operations = {
        LoadConstant { 0U, PackedLogic4::from_aval_bval(32U, 0U, 0U) },
        LoadConstant { 1U, PackedLogic4::from_aval_bval(32U, 1U, 0U) },
        ReadContainerObject { 0U, object },
        WaitFor { 1U },
        ContainerRead { 2U, 0U, 0U, false, false, false },
        ContainerRead { 3U, 0U, 1U, false, false, false },
        WriteBlocking { outputs[0], 2U },
        WriteBlocking { outputs[1], 3U },
        Halt { },
    };

    Process writer;
    writer.id = 1U;
    writer.name = "container_object_snapshot_mutators";
    writer.register_count = 13U;
    writer.container_register_count = 6U;
    writer.container_register_types = {
        memory_type, memory_type, memory_type, memory_type, memory_type,
        memory_type };
    writer.operations = {
        LoadConstant { 0U, PackedLogic4::from_aval_bval(32U, 0U, 0U) },
        LoadConstant { 1U, PackedLogic4::from_aval_bval(8U, 0x77U, 0U) },
        LoadConstant { 4U, PackedLogic4::from_aval_bval(32U, 1U, 0U) },
        LoadConstant { 5U, PackedLogic4::from_aval_bval(32U, 0U, 0U) },
        LoadConstant { 6U, PackedLogic4::from_aval_bval(1U, 1U, 0U) },
        LoadConstant { 9U, PackedLogic4::from_aval_bval(16U, 0x3344U, 0U) },
        ReadContainerObject { 0U, object },
        WriteContainerObjectElement {
            object, 0U, 1U, false, false, false, std::nullopt, std::nullopt },
        ContainerRead { 2U, 0U, 0U, false, false, false },
        ReadContainerObject { 1U, object },
        ContainerRead { 3U, 1U, 0U, false, false, false },
        ReadContainerObject { 2U, object },
        WriteContainerObjectElement {
            object, 4U, 6U, false, false, false, std::nullopt,
            DynamicPartIndex { 5U, 0, 0, 0U, 1U, true, true } },
        ContainerRead { 7U, 2U, 4U, false, false, false },
        ReadContainerObject { 3U, object },
        ContainerRead { 8U, 3U, 4U, false, false, false },
        ReadContainerObject { 5U, object },
        WriteBlocking { outputs[8], 9U },
        ContainerRead { 12U, 5U, 0U, false, false, false },
        ContainerRead { 10U, 1U, 0U, false, false, false },
        ReadContainerObject { 4U, object },
        ContainerRead { 11U, 4U, 0U, false, false, false },
        WriteBlocking { outputs[2], 2U },
        WriteBlocking { outputs[3], 3U },
        WriteBlocking { outputs[4], 7U },
        WriteBlocking { outputs[5], 8U },
        WriteBlocking { outputs[6], 10U },
        WriteBlocking { outputs[7], 11U },
        WriteBlocking { outputs[9], 12U },
        Halt { },
    };

    std::unique_ptr<compiler::LlvmJit> jit;
    std::optional<compiler::JitProcessHandle> observer_handle;
    std::optional<compiler::JitProcessHandle> writer_handle;
    if (compiled) {
        compiler::LlvmJitOptions options;
        options.optimization = level;
        options.debug_instrumentation = false;
        jit = std::make_unique<compiler::LlvmJit>(std::move(options));
        assert(jit->supports_process(observer, signal_widths));
        assert(jit->supports_process(writer, signal_widths));
        jit->add_process(
            observer.name, observer, signal_widths, signal_kinds);
        jit->add_process(
            writer.name, writer, signal_widths, signal_kinds);
        observer_handle = jit->lookup(observer.name);
        writer_handle = jit->lookup(writer.name);
    }

    const auto observer_id = interpreter.add_process(std::move(observer));
    const auto writer_id = interpreter.add_process(std::move(writer));
    if (compiled) {
        interpreter.set_process_executor(
            observer_id,
            std::make_unique<app::application_detail::LlvmProcessExecutor>(
                *jit, *observer_handle, interpreter.process_program(observer_id),
                signal_widths, signal_kinds, signal_resolutions));
        interpreter.set_process_executor(
            writer_id,
            std::make_unique<app::application_detail::LlvmProcessExecutor>(
                *jit, *writer_handle, interpreter.process_program(writer_id),
                signal_widths, signal_kinds, signal_resolutions));
    }

    const auto run = interpreter.run();
    std::array<PackedLogic4, 10U> values;
    for (std::size_t index = 0U; index < outputs.size(); ++index) {
        values[index] = interpreter.signal_value(outputs[index]);
    }
    return { run, std::move(values) };
}

ExposedContainerReadCapture run_exposed_container_object_read(
    const bool compiled,
    const compiler::JitOptimizationLevel level,
    const std::uint32_t selected_index)
{
    const std::array<std::uint32_t, 2U> signal_widths { 8U, 8U };
    const std::array<ValueKind, 2U> signal_kinds {
        ValueKind::logic4, ValueKind::logic4 };
    const std::array<ResolutionKind, 2U> signal_resolutions {
        ResolutionKind::none, ResolutionKind::none };
    Interpreter interpreter;
    const auto source = interpreter.add_signal({
        "exposed_container_source",
        PackedLogic4::from_aval_bval(8U, 0x22U, 0U),
        ResolutionKind::none, ValueKind::logic4 });
    const auto output = interpreter.add_signal({
        "exposed_container_output", PackedLogic4(8U, Logic4::x),
        ResolutionKind::none, ValueKind::logic4 });

    ContainerType type;
    type.element_width = 8U;
    type.two_state = true;
    type.fixed = true;
    type.index_left = 5;
    type.index_right = 5;
    type.dimensions = { { 5, 5 } };
    const auto object = interpreter.add_container_object({
        "exposed_container_memory",
        ContainerValue {
            type,
            { PackedLogic4::from_aval_bval(8U, 0x11U, 0U) },
            { },
        },
        std::nullopt,
    });
    interpreter.add_container_signal_alias({ object, source, true, true });
    const auto& retained = interpreter.container_object_value(object);
    assert(retained.elements.size() == 1U);
    assert(retained.elements[0]
        == PackedLogic4::from_aval_bval(8U, 0x22U, 0U));

    Process process;
    process.id = 0U;
    process.name = "exposed_container_single_use_read";
    process.register_count = 3U;
    process.container_register_count = 1U;
    process.container_register_types = { type };
    process.operations = {
        LoadConstant { 0U, PackedLogic4::from_aval_bval(8U, 0x55U, 0U) },
        LoadConstant {
            1U, PackedLogic4::from_aval_bval(32U, selected_index, 0U) },
        WriteBlocking { source, 0U },
        ReadContainerObject { 0U, object },
        ContainerRead { 2U, 0U, 1U, false, false, false },
        WriteBlocking { output, 2U },
        Halt { },
    };

    std::unique_ptr<compiler::LlvmJit> jit;
    std::optional<compiler::JitProcessHandle> handle;
    if (compiled) {
        compiler::LlvmJitOptions options;
        options.optimization = level;
        options.debug_instrumentation = false;
        jit = std::make_unique<compiler::LlvmJit>(std::move(options));
        assert(jit->supports_process(process, signal_widths));
        jit->add_process(process.name, process, signal_widths, signal_kinds);
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
        run, interpreter.signal_value(output), retained.elements[0] };
}

ContainerSliceReadCapture run_container_slice_single_use_read(
    const bool compiled,
    const compiler::JitOptimizationLevel level)
{
    const std::array<std::uint32_t, 1U> signal_widths { 8U };
    const std::array<ValueKind, 1U> signal_kinds { ValueKind::logic4 };
    const std::array<ResolutionKind, 1U> signal_resolutions {
        ResolutionKind::none };
    Interpreter interpreter;
    const auto output = interpreter.add_signal({
        "container_slice_read_output", PackedLogic4(8U, Logic4::x),
        ResolutionKind::none, ValueKind::logic4 });

    ContainerType base_type;
    base_type.element_width = 8U;
    base_type.fixed = true;
    base_type.index_left = 0;
    base_type.index_right = 3;
    base_type.dimensions = { { 0, 3 } };
    const auto base = interpreter.add_container_object({
        "container_slice_read_base",
        ContainerValue {
            base_type,
            { PackedLogic4::from_aval_bval(8U, 0x10U, 0U),
                PackedLogic4::from_aval_bval(8U, 0x20U, 0U),
                PackedLogic4::from_aval_bval(8U, 0x30U, 0U),
                PackedLogic4::from_aval_bval(8U, 0x40U, 0U) },
            { },
        },
        std::nullopt,
    });
    ContainerType slice_type;
    slice_type.element_width = 8U;
    slice_type.fixed = true;
    slice_type.index_left = 0;
    slice_type.index_right = 1;
    slice_type.dimensions = { { 0, 1 } };
    const auto slice = interpreter.add_container_object({
        "container_slice_read_view",
        ContainerValue {
            slice_type,
            { PackedLogic4::from_aval_bval(8U, 0U, 0U),
                PackedLogic4::from_aval_bval(8U, 0U, 0U) },
            { },
        },
        ContainerSliceAlias { base, 1, 2 },
    });

    Process process;
    process.id = 0U;
    process.name = "container_slice_single_use_read";
    process.register_count = 2U;
    process.container_register_count = 1U;
    process.container_register_types = { slice_type };
    process.operations = {
        LoadConstant { 0U, PackedLogic4::from_aval_bval(32U, 0U, 0U) },
        ReadContainerObject { 0U, slice },
        ContainerRead { 1U, 0U, 0U, false, false, false },
        WriteBlocking { output, 1U },
        Halt { },
    };

    std::unique_ptr<compiler::LlvmJit> jit;
    std::optional<compiler::JitProcessHandle> handle;
    if (compiled) {
        compiler::LlvmJitOptions options;
        options.optimization = level;
        options.debug_instrumentation = false;
        jit = std::make_unique<compiler::LlvmJit>(std::move(options));
        assert(jit->supports_process(process, signal_widths));
        jit->add_process(process.name, process, signal_widths, signal_kinds);
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
    return { run, interpreter.signal_value(output) };
}

ContainerObjectReadCapture run_single_use_container_object_read(
    const bool compiled,
    const compiler::JitOptimizationLevel level,
    const std::uint32_t element_width,
    const bool fixed,
    const PackedLogic4& index_value,
    const bool expose_debug_local)
{
    const std::array<std::uint32_t, 1U> signal_widths { element_width };
    const std::array<ValueKind, 1U> signal_kinds { ValueKind::logic4 };
    const std::array<ResolutionKind, 1U> signal_resolutions {
        ResolutionKind::none };
    Interpreter interpreter;
    const auto output = interpreter.add_signal({
        "single_use_container_object_output",
        PackedLogic4(element_width, Logic4::x),
        ResolutionKind::none, ValueKind::logic4 });

    ContainerType type;
    type.element_width = element_width;
    type.fixed = fixed;
    type.index_left = 0;
    type.index_right = 1;
    if (fixed) {
        type.dimensions = { { 0, 1 } };
    }
    PackedLogic4 first(element_width, Logic4::zero);
    first.set(0U, Logic4::one);
    if (element_width > 64U) {
        first.set(64U, Logic4::one);
    }
    const auto second = PackedLogic4(element_width, Logic4::zero);
    const auto object = interpreter.add_container_object({
        "single_use_container_object",
        ContainerValue { type, { first, second }, { } }, std::nullopt });

    Process process;
    process.id = 0U;
    process.name = "single_use_container_object_read";
    process.register_count = 2U;
    process.container_register_count = 1U;
    process.container_register_types = { type };
    if (expose_debug_local) {
        process.debug_container_locals.push_back(
            { "snapshot", 0U, type, { } });
    }
    process.operations = {
        LoadConstant { 0U, index_value },
        ReadContainerObject { 0U, object },
        ContainerRead { 1U, 0U, 0U, true, false, false },
        WriteBlocking { output, 1U },
        Halt { },
    };

    std::unique_ptr<compiler::LlvmJit> jit;
    std::optional<compiler::JitProcessHandle> handle;
    if (compiled) {
        compiler::LlvmJitOptions options;
        options.optimization = level;
        options.debug_instrumentation = false;
        jit = std::make_unique<compiler::LlvmJit>(std::move(options));
        assert(jit->supports_process(process, signal_widths));
        jit->add_process(process.name, process, signal_widths, signal_kinds);
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

    RunResult run;
    std::string error;
    try {
        run = interpreter.run();
    } catch (const InterpreterError& exception) {
        error = exception.what();
    }
    std::optional<ContainerValue> debug_local;
    if (error.empty() && expose_debug_local) {
        debug_local = interpreter.read_debug_container_local(process_id, 0U);
    }
    return {
        run,
        error.empty() ? interpreter.signal_value(output)
                      : PackedLogic4(element_width, Logic4::x),
        std::move(debug_local), std::move(error), { } };
}

ContainerObjectReadCapture run_container_index64_read(
    const bool compiled,
    const compiler::JitOptimizationLevel level,
    const ContainerType& type,
    const std::vector<PackedLogic4>& elements,
    const PackedLogic4& index_value,
    const bool linear_index,
    const bool signed_index,
    const bool expose_debug_local,
    const bool retain_for_second_read)
{
    const std::array<std::uint32_t, 2U> signal_widths {
        type.element_width, type.element_width };
    const std::array<ValueKind, 2U> signal_kinds {
        ValueKind::logic4, ValueKind::logic4 };
    const std::array<ResolutionKind, 2U> signal_resolutions {
        ResolutionKind::none, ResolutionKind::none };
    Interpreter interpreter;
    const auto output = interpreter.add_signal({
        "container_index64_output",
        PackedLogic4(type.element_width, Logic4::x),
        ResolutionKind::none, ValueKind::logic4 });
    const auto second_output = interpreter.add_signal({
        "container_index64_second_output",
        PackedLogic4(type.element_width, Logic4::x),
        ResolutionKind::none, ValueKind::logic4 });
    const auto object = interpreter.add_container_object({
        "container_index64_object",
        ContainerValue { type, elements, { } }, std::nullopt });

    Process process;
    process.id = 0U;
    process.name = "container_index64_read";
    process.register_count = retain_for_second_read ? 3U : 2U;
    process.container_register_count = 1U;
    process.container_register_types = { type };
    if (expose_debug_local) {
        process.debug_container_locals.push_back(
            { "snapshot", 0U, type, { } });
    }
    process.operations = {
        LoadConstant { 0U, index_value },
        ReadContainerObject { 0U, object },
        ContainerRead {
            1U, 0U, 0U, signed_index, linear_index, false },
    };
    if (retain_for_second_read) {
        process.operations.push_back(ContainerRead {
            2U, 0U, 0U, signed_index, linear_index, false });
    }
    process.operations.push_back(WriteBlocking { output, 1U });
    if (retain_for_second_read) {
        process.operations.push_back(WriteBlocking { second_output, 2U });
    }
    process.operations.push_back(Halt { });

    std::unique_ptr<compiler::LlvmJit> jit;
    std::optional<compiler::JitProcessHandle> handle;
    if (compiled) {
        compiler::LlvmJitOptions options;
        options.optimization = level;
        options.debug_instrumentation = false;
        jit = std::make_unique<compiler::LlvmJit>(std::move(options));
        assert(jit->supports_process(process, signal_widths));
        jit->add_process(process.name, process, signal_widths, signal_kinds);
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

    RunResult run;
    std::string error;
    try {
        run = interpreter.run();
    } catch (const InterpreterError& exception) {
        error = exception.what();
    }
    std::optional<ContainerValue> debug_local;
    if (error.empty() && expose_debug_local) {
        debug_local = interpreter.read_debug_container_local(process_id, 0U);
    }
    std::optional<PackedLogic4> second_value;
    if (error.empty() && retain_for_second_read) {
        second_value = interpreter.signal_value(second_output);
    }
    return {
        run,
        error.empty() ? interpreter.signal_value(output)
                      : PackedLogic4(type.element_width, Logic4::x),
        std::move(debug_local), std::move(error), std::move(second_value) };
}

ContainerObjectReadCapture run_native_callable_container_read(
    const bool compiled,
    const compiler::JitOptimizationLevel level,
    const bool native_isolated)
{
    const std::array<std::uint32_t, 1U> signal_widths { 8U };
    const std::array<ValueKind, 1U> signal_kinds { ValueKind::logic4 };
    const std::array<ResolutionKind, 1U> signal_resolutions {
        ResolutionKind::none };
    Interpreter interpreter;
    const auto output = interpreter.add_signal({
        "native_callable_container_output", PackedLogic4(8U, Logic4::x),
        ResolutionKind::none, ValueKind::logic4 });

    ContainerType type;
    type.element_width = 8U;
    type.fixed = true;
    type.index_width = 64U;
    type.signed_indices = false;
    type.index_left = 0;
    type.index_right = 1;
    type.dimensions = { { 0, 1 } };
    const auto object = interpreter.add_container_object({
        "native_callable_container_object",
        ContainerValue {
            type,
            { PackedLogic4::from_aval_bval(8U, 0x5aU, 0U),
                PackedLogic4::from_aval_bval(8U, 0x27U, 0U) },
            { },
        },
        std::nullopt,
    });

    Process process;
    process.id = 0U;
    process.name = native_isolated
        ? "native_callable_container_isolated"
        : "native_callable_container_conservative";
    process.register_count = 2U;
    process.container_register_count = 1U;
    process.container_register_types = { type };
    process.operations = {
        LoadConstant {
            0U, PackedLogic4::from_aval_bval(64U, 0U, 0U) },
        LoadConstant { 1U, PackedLogic4(8U, Logic4::x) },
        CallableFramePush {
            1U, { }, { }, { 0U }, native_isolated },
        Call { 8U, 4U, { } },
        CallableFramePop { 1U, { }, { }, { } },
        WriteBlocking { output, 1U },
        Halt { },
        Halt { },
        ReadContainerObject { 0U, object },
        ContainerRead { 1U, 0U, 0U, false, false, false },
        Return { { } },
    };

    std::unique_ptr<compiler::LlvmJit> jit;
    std::optional<compiler::JitProcessHandle> handle;
    if (compiled) {
        compiler::LlvmJitOptions options;
        options.optimization = level;
        options.debug_instrumentation = false;
        jit = std::make_unique<compiler::LlvmJit>(std::move(options));
        assert(jit->supports_process(process, signal_widths));
        jit->add_process(process.name, process, signal_widths, signal_kinds);
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
    const auto& retained = interpreter.container_object_value(object);
    assert(retained.elements.size() == 2U);
    assert(retained.elements[0]
        == PackedLogic4::from_aval_bval(8U, 0x5aU, 0U));
    assert(retained.elements[1]
        == PackedLogic4::from_aval_bval(8U, 0x27U, 0U));
    return { run, interpreter.signal_value(output), { }, { }, { } };
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
    const std::array<std::uint8_t, 3U> cache_mutation_bytes {
        0x96U, 0x12U, 0x34U };
    const auto cache_mutation_path = directory / "cache-mutations.bin";
    const std::array<std::uint8_t, 2U> cache_container_bytes {
        0x12U, 0x34U };
    const auto cache_container_path = directory / "cache-container.bin";
    write_input(full_path, full_bytes);
    write_input(partial_path, partial_bytes);
    write_input(bounded_path, bounded_bytes);
    write_input(scan_partial_path, scan_partial_bytes);
    write_input(scan_empty_path, scan_empty_bytes);
    write_input(cache_mutation_path, cache_mutation_bytes);
    write_input(cache_container_path, cache_container_bytes);

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

    const auto cache_reference = run_file_cache_mutations(
        cache_mutation_path, false, compiler::JitOptimizationLevel::o0);
    assert(cache_reference.run.status == RunStatus::completed);
    assert(cache_reference.outputs[0]
        == PackedLogic4::from_aval_bval(8U, 0xa5U, 0U));
    assert(cache_reference.outputs[1]
        == PackedLogic4::from_aval_bval(8U, 0xa5U, 0U));
    assert(cache_reference.outputs[2]
        == PackedLogic4::from_aval_bval(8U, 0x96U, 0U));
    assert(cache_reference.outputs[3]
        == PackedLogic4::from_aval_bval(8U, 0x96U, 0U));
    assert(cache_reference.outputs[4]
        == PackedLogic4::from_aval_bval(16U, 0x1234U, 0U));
    assert(cache_reference.outputs[5]
        == PackedLogic4::from_aval_bval(16U, 0x1234U, 0U));
    for (const auto level : {
             compiler::JitOptimizationLevel::o0,
             compiler::JitOptimizationLevel::o2 }) {
        const auto native = run_file_cache_mutations(
            cache_mutation_path, true, level);
        require_same_run(cache_reference.run, native.run);
        assert(native.outputs == cache_reference.outputs);
    }

    const auto snapshot_reference = run_container_object_snapshot(
        false, compiler::JitOptimizationLevel::o0);
    assert(snapshot_reference.run.status == RunStatus::completed);
    const std::array<PackedLogic4, 10U> snapshot_expected {
        PackedLogic4::from_aval_bval(8U, 0x11U, 0U),
        PackedLogic4::from_aval_bval(8U, 0x22U, 0U),
        PackedLogic4::from_aval_bval(8U, 0x11U, 0U),
        PackedLogic4::from_aval_bval(8U, 0x77U, 0U),
        PackedLogic4::from_aval_bval(8U, 0x22U, 0U),
        PackedLogic4::from_aval_bval(8U, 0x23U, 0U),
        PackedLogic4::from_aval_bval(8U, 0x77U, 0U),
        PackedLogic4::from_aval_bval(8U, 0x33U, 0U),
        PackedLogic4::from_aval_bval(16U, 0x3344U, 0U),
        PackedLogic4::from_aval_bval(8U, 0x77U, 0U),
    };
    assert(snapshot_reference.outputs == snapshot_expected);
    auto& container_profile
        = app::application_detail::container_callback_profile();
    const bool container_profile_was_enabled = container_profile.enabled;
    container_profile.enabled = true;
    const auto borrowed_read_count_before
        = container_profile.fused_object_borrow_reads;
    for (const auto level : {
             compiler::JitOptimizationLevel::o0,
             compiler::JitOptimizationLevel::o2 }) {
        const auto native = run_container_object_snapshot(true, level);
        require_same_run(snapshot_reference.run, native.run);
        assert(native.outputs == snapshot_expected);
    }
    assert(container_profile.fused_object_borrow_reads
        == borrowed_read_count_before + 4U);
    for (const auto& [selected_index, expected] : {
             std::pair {
                 5U, PackedLogic4::from_aval_bval(8U, 0x55U, 0U) },
             std::pair {
                 6U, PackedLogic4::from_aval_bval(8U, 0U, 0U) },
         }) {
        const auto exposed_reference = run_exposed_container_object_read(
            false, compiler::JitOptimizationLevel::o0, selected_index);
        assert(exposed_reference.run.status == RunStatus::completed);
        assert(exposed_reference.output == expected);
        assert(exposed_reference.retained_value
            == PackedLogic4::from_aval_bval(8U, 0x55U, 0U));
        for (const auto level : {
                 compiler::JitOptimizationLevel::o0,
                 compiler::JitOptimizationLevel::o2 }) {
            const auto native = run_exposed_container_object_read(
                true, level, selected_index);
            require_same_run(exposed_reference.run, native.run);
            assert(native.output == expected);
            assert(native.retained_value
                == PackedLogic4::from_aval_bval(8U, 0x55U, 0U));
        }
    }
    const auto slice_reference = run_container_slice_single_use_read(
        false, compiler::JitOptimizationLevel::o0);
    assert(slice_reference.run.status == RunStatus::completed);
    assert(slice_reference.output
        == PackedLogic4::from_aval_bval(8U, 0x20U, 0U));
    for (const auto level : {
             compiler::JitOptimizationLevel::o0,
             compiler::JitOptimizationLevel::o2 }) {
        const auto native = run_container_slice_single_use_read(true, level);
        require_same_run(slice_reference.run, native.run);
        assert(native.output == slice_reference.output);
    }
    assert(container_profile.fused_object_borrow_reads
        == borrowed_read_count_before + 10U);

    const auto debug_local_reference
        = run_single_use_container_object_read(
            false,
            compiler::JitOptimizationLevel::o0,
            8U,
            true,
            PackedLogic4::from_aval_bval(32U, 0U, 0U),
            true);
    assert(debug_local_reference.run.status == RunStatus::completed);
    assert(debug_local_reference.output
        == PackedLogic4::from_aval_bval(8U, 1U, 0U));
    assert(debug_local_reference.debug_local.has_value());
    const auto expected_debug_elements = std::vector<PackedLogic4> {
        PackedLogic4::from_aval_bval(8U, 1U, 0U),
        PackedLogic4::from_aval_bval(8U, 0U, 0U),
    };
    assert(debug_local_reference.debug_local->elements
        == expected_debug_elements);
    const auto debug_local_borrow_count
        = container_profile.fused_object_borrow_reads;
    for (const auto level : {
             compiler::JitOptimizationLevel::o0,
             compiler::JitOptimizationLevel::o2 }) {
        const auto native = run_single_use_container_object_read(
            true,
            level,
            8U,
            true,
            PackedLogic4::from_aval_bval(32U, 0U, 0U),
            true);
        require_same_run(debug_local_reference.run, native.run);
        assert(native.output == debug_local_reference.output);
        assert(native.debug_local.has_value());
        assert(native.debug_local->elements == expected_debug_elements);
    }
    assert(container_profile.fused_object_borrow_reads
        == debug_local_borrow_count);

    const std::array<std::pair<PackedLogic4, std::string_view>, 4U>
        invalid_dynamic_indices {
            std::pair {
                PackedLogic4::from_aval_bval(32U, 0U, 1U),
                std::string_view { "container index must be a known integral value" },
            },
            std::pair {
                PackedLogic4::from_aval_bval(32U, 1U, 1U),
                std::string_view { "container index must be a known integral value" },
            },
            std::pair {
                PackedLogic4::from_aval_bval(
                    32U, std::numeric_limits<std::uint32_t>::max(), 0U),
                std::string_view { "container index cannot be negative" },
            },
            std::pair {
                PackedLogic4::from_aval_bval(32U, 2U, 0U),
                std::string_view { "container index is out of range" },
            },
    };
    for (const auto& [index_value, expected_error] : invalid_dynamic_indices) {
        const auto reference = run_single_use_container_object_read(
            false,
            compiler::JitOptimizationLevel::o0,
            8U,
            false,
            index_value,
            false);
        assert(!reference.error.empty());
        assert(reference.error.find(expected_error) != std::string::npos);
        const auto borrow_count_before
            = container_profile.fused_object_borrow_reads;
        for (const auto level : {
                 compiler::JitOptimizationLevel::o0,
                 compiler::JitOptimizationLevel::o2 }) {
            const auto native = run_single_use_container_object_read(
                true,
                level,
                8U,
                false,
                index_value,
                false);
            assert(native.error == reference.error);
            assert(native.error.find(expected_error) != std::string::npos);
        }
        assert(container_profile.fused_object_borrow_reads
            == borrow_count_before + 2U);
    }

    const auto wide_fallback_reference
        = run_single_use_container_object_read(
            false,
            compiler::JitOptimizationLevel::o0,
            129U,
            true,
            PackedLogic4::from_aval_bval(32U, 0U, 0U),
            false);
    auto wide_fallback_expected = PackedLogic4(129U, Logic4::zero);
    wide_fallback_expected.set(0U, Logic4::one);
    wide_fallback_expected.set(64U, Logic4::one);
    assert(wide_fallback_reference.run.status == RunStatus::completed);
    assert(wide_fallback_reference.output == wide_fallback_expected);
    const auto wide_fallback_borrow_count
        = container_profile.fused_object_borrow_reads;
    for (const auto level : {
             compiler::JitOptimizationLevel::o0,
             compiler::JitOptimizationLevel::o2 }) {
        const auto native = run_single_use_container_object_read(
            true,
            level,
            129U,
            true,
            PackedLogic4::from_aval_bval(32U, 0U, 0U),
            false);
        require_same_run(wide_fallback_reference.run, native.run);
        assert(native.output == wide_fallback_expected);
    }
    assert(container_profile.fused_object_borrow_reads
        == wide_fallback_borrow_count);

    const auto index64_profile_start = [&] {
        return std::array<std::uint64_t, 5U> {
            container_profile.read_index64_callbacks,
            container_profile.read_words,
            container_profile.read_packed_elements,
            container_profile.generic,
            container_profile.fused_object_borrow_reads,
        };
    };
    for (const bool native_isolated : { true, false }) {
        const auto reference = run_native_callable_container_read(
            false, compiler::JitOptimizationLevel::o0,
            native_isolated);
        assert(reference.run.status == RunStatus::completed);
        assert(reference.output
            == PackedLogic4::from_aval_bval(8U, 0x5aU, 0U));
        const auto borrowed_before
            = container_profile.fused_object_borrow_reads;
        for (const auto level : {
                 compiler::JitOptimizationLevel::o0,
                 compiler::JitOptimizationLevel::o2 }) {
            const auto native = run_native_callable_container_read(
                true, level, native_isolated);
            require_same_run(reference.run, native.run);
            assert(native.output == reference.output);
        }
        assert(container_profile.fused_object_borrow_reads
            == borrowed_before + (native_isolated ? 2U : 0U));
    }
    const auto verify_index64_case = [&](const ContainerType& type,
                                         const std::vector<PackedLogic4>& elements,
                                         const PackedLogic4& index_value,
                                         const bool linear_index,
                                         const bool signed_index,
                                         const PackedLogic4& expected,
                                         const std::string_view expected_error,
                                         const bool expose_debug_local,
                                         const bool retain_for_second_read,
                                         const std::uint64_t expected_generic_per_run,
                                         const std::uint64_t expected_borrow_per_run) {
        const auto reference = run_container_index64_read(
            false,
            compiler::JitOptimizationLevel::o0,
            type,
            elements,
            index_value,
            linear_index,
            signed_index,
            expose_debug_local,
            retain_for_second_read);
        if (expected_error.empty()) {
            assert(reference.error.empty());
            assert(reference.run.status == RunStatus::completed);
            assert(reference.output == expected);
            if (retain_for_second_read) {
                assert(reference.second_output.has_value());
                assert(*reference.second_output == expected);
            }
            if (expose_debug_local) {
                assert(reference.debug_local.has_value());
                assert(reference.debug_local->type == type);
                assert(reference.debug_local->elements == elements);
            }
        } else {
            assert(!reference.error.empty());
            assert(reference.error.find(expected_error) != std::string::npos);
        }

        const auto before = index64_profile_start();
        for (const auto level : {
                 compiler::JitOptimizationLevel::o0,
                 compiler::JitOptimizationLevel::o2 }) {
            const auto native = run_container_index64_read(
                true,
                level,
                type,
                elements,
                index_value,
                linear_index,
                signed_index,
                expose_debug_local,
                retain_for_second_read);
            if (expected_error.empty()) {
                require_same_run(reference.run, native.run);
                assert(native.error.empty());
                assert(native.output == expected);
                if (retain_for_second_read) {
                    assert(native.second_output.has_value());
                    assert(*native.second_output == expected);
                }
                if (expose_debug_local) {
                    assert(native.debug_local.has_value());
                    assert(native.debug_local->type == type);
                    assert(native.debug_local->elements == elements);
                }
            } else {
                assert(native.error == reference.error);
                assert(native.error.find(expected_error) != std::string::npos);
            }
        }
        const auto reads_per_run = retain_for_second_read ? 2U : 1U;
        assert(container_profile.read_index64_callbacks
            == before[0] + reads_per_run * 2U);
        assert(container_profile.read_words
            == before[1] + reads_per_run * 2U);
        assert(container_profile.read_packed_elements
            == before[2]
                + (type.element_width > 64U ? reads_per_run * 2U : 0U));
        assert(container_profile.generic
            == before[3] + expected_generic_per_run * 2U);
        assert(container_profile.fused_object_borrow_reads
            == before[4] + expected_borrow_per_run * 2U);
    };

    const auto packed_byte = [](const std::uint8_t value) {
        return PackedLogic4::from_aval_bval(8U, value, 0U);
    };
    const std::vector<PackedLogic4> four_bytes {
        packed_byte(0x11U),
        packed_byte(0x22U),
        packed_byte(0x33U),
        packed_byte(0x44U),
    };
    const auto make_fixed_type = [](const std::int32_t left,
                                    const std::int32_t right) {
        ContainerType type;
        type.element_width = 8U;
        type.fixed = true;
        type.index_left = left;
        type.index_right = right;
        type.dimensions = { { left, right } };
        return type;
    };
    const auto ascending_negative = make_fixed_type(-2, 1);
    const auto descending_negative = make_fixed_type(1, -2);
    const auto dynamic_type = [] {
        ContainerType type;
        type.element_width = 8U;
        return type;
    }();
    const auto minus_one64 = PackedLogic4::from_aval_bval(
        64U, std::numeric_limits<std::uint64_t>::max(), 0U);
    auto x_index64 = PackedLogic4(64U, Logic4::zero);
    x_index64.set(40U, Logic4::x);
    auto z_index64 = PackedLogic4(64U, Logic4::zero);
    z_index64.set(40U, Logic4::z);
    const auto unknown_index_error
        = std::string_view { "container index must be a known integral value" };
    const auto range_error
        = std::string_view { "container index is out of range" };
    const auto size_error
        = std::string_view { "container index is too large" };
    const auto size_maximum = static_cast<std::uint64_t>(
        std::numeric_limits<std::size_t>::max());
    const auto high32_error = static_cast<std::uint64_t>(0x1'0000'0000ULL)
            > size_maximum
        ? size_error
        : range_error;

    verify_index64_case(
        ascending_negative,
        four_bytes,
        minus_one64,
        false,
        true,
        packed_byte(0x22U),
        { }, false, false, 0U, 1U);
    verify_index64_case(
        descending_negative,
        four_bytes,
        minus_one64,
        false,
        true,
        packed_byte(0x33U),
        { }, false, false, 0U, 1U);
    verify_index64_case(
        ascending_negative,
        four_bytes,
        x_index64,
        false,
        true,
        PackedLogic4(8U, Logic4::x),
        { }, false, false, 0U, 1U);
    verify_index64_case(
        ascending_negative,
        four_bytes,
        z_index64,
        false,
        true,
        PackedLogic4(8U, Logic4::x),
        { }, false, false, 0U, 1U);
    auto two_state_fixed = ascending_negative;
    two_state_fixed.two_state = true;
    verify_index64_case(
        two_state_fixed,
        four_bytes,
        x_index64,
        false,
        true,
        PackedLogic4(8U, Logic4::zero),
        { }, false, false, 0U, 1U);

    // Fixed-linear indexing consumes a linear offset; the same numeric value
    // would name a different element under the array's declared coordinates.
    verify_index64_case(
        ascending_negative,
        four_bytes,
        PackedLogic4::from_aval_bval(64U, 1U, 0U),
        true,
        false,
        packed_byte(0x22U),
        { }, false, false, 0U, 1U);
    verify_index64_case(
        ascending_negative,
        four_bytes,
        PackedLogic4::from_aval_bval(64U, 0x1'0000'0000ULL, 0U),
        true,
        false,
        PackedLogic4(8U, Logic4::x),
        { }, false, false, 0U, 1U);
    const auto ordinary_fixed = make_fixed_type(0, 3);
    verify_index64_case(
        ordinary_fixed,
        four_bytes,
        PackedLogic4::from_aval_bval(64U, 0x1'0000'0000ULL, 0U),
        false,
        true,
        PackedLogic4(8U, Logic4::x),
        { }, false, false, 0U, 1U);
    verify_index64_case(
        ascending_negative,
        four_bytes,
        minus_one64,
        true,
        false,
        PackedLogic4(8U, Logic4::x),
        { }, false, false, 0U, 1U);

    verify_index64_case(
        dynamic_type,
        four_bytes,
        x_index64,
        false,
        true,
        PackedLogic4(8U, Logic4::x),
        unknown_index_error, false, false, 0U, 1U);
    verify_index64_case(
        dynamic_type,
        four_bytes,
        z_index64,
        false,
        true,
        PackedLogic4(8U, Logic4::x),
        unknown_index_error, false, false, 0U, 1U);
    verify_index64_case(
        dynamic_type,
        four_bytes,
        PackedLogic4::from_aval_bval(64U, 0x1'0000'0000ULL, 0U),
        false,
        true,
        PackedLogic4(8U, Logic4::x),
        high32_error, false, false, 0U, 1U);
    verify_index64_case(
        dynamic_type,
        four_bytes,
        minus_one64,
        false,
        true,
        PackedLogic4(8U, Logic4::x),
        "container index cannot be negative", false, false, 0U, 1U);
    verify_index64_case(
        dynamic_type,
        four_bytes,
        PackedLogic4::from_aval_bval(64U, 0x1'0000'0000ULL, 0U),
        false,
        false,
        PackedLogic4(8U, Logic4::x),
        high32_error, false, false, 0U, 1U);
    verify_index64_case(
        dynamic_type,
        four_bytes,
        PackedLogic4::from_aval_bval(64U, 4U, 0U),
        false,
        false,
        PackedLogic4(8U, Logic4::x),
        range_error, false, false, 0U, 1U);
    verify_index64_case(
        dynamic_type,
        four_bytes,
        PackedLogic4::from_aval_bval(64U, size_maximum, 0U),
        false,
        false,
        PackedLogic4(8U, Logic4::x),
        range_error, false, false, 0U, 1U);
    if (std::numeric_limits<std::size_t>::digits < 64U) {
        const auto beyond_size_maximum = size_maximum + 1U;
        verify_index64_case(
            dynamic_type,
            four_bytes,
            PackedLogic4::from_aval_bval(
                64U, beyond_size_maximum, 0U),
            false,
            false,
            PackedLogic4(8U, Logic4::x),
            size_error, false, false, 0U, 1U);
    }

    // The 64-bit callback must be selected, and its fused single-use path
    // must borrow only for a narrow packed element.
    verify_index64_case(
        ordinary_fixed,
        four_bytes,
        PackedLogic4::from_aval_bval(64U, 2U, 0U),
        false,
        false,
        packed_byte(0x33U),
        { }, false, false, 0U, 1U);
    verify_index64_case(
        ordinary_fixed,
        four_bytes,
        PackedLogic4::from_aval_bval(64U, 2U, 0U),
        false,
        false,
        packed_byte(0x33U),
        { }, false, true, 0U, 0U);
    verify_index64_case(
        ordinary_fixed,
        four_bytes,
        PackedLogic4::from_aval_bval(64U, 2U, 0U),
        false,
        false,
        packed_byte(0x33U),
        { }, true, false, 0U, 0U);

    ContainerType wide_type;
    wide_type.element_width = 129U;
    wide_type.fixed = true;
    wide_type.index_left = 0;
    wide_type.index_right = 1;
    wide_type.dimensions = { { 0, 1 } };
    PackedLogic4 wide_first(129U, Logic4::zero);
    wide_first.set(0U, Logic4::one);
    wide_first.set(64U, Logic4::one);
    const std::vector<PackedLogic4> wide_elements {
        wide_first,
        PackedLogic4(129U, Logic4::zero),
    };
    verify_index64_case(
        wide_type,
        wide_elements,
        PackedLogic4::from_aval_bval(64U, 0U, 0U),
        false,
        false,
        wide_first,
        { }, false, false, 0U, 0U);

    container_profile.enabled = container_profile_was_enabled;

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
