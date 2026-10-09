// SPDX-License-Identifier: Apache-2.0
#include "../../src/app/application_internal.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

#if defined(FSIM_HAS_LLVM)

using namespace fsim;
using namespace fsim::runtime;
using namespace fsim::runtime::simir;

constexpr std::size_t kIndexWidth = 137U;

PackedLogic4 key_with_bit(const std::size_t bit)
{
    PackedLogic4 result(kIndexWidth, Logic4::zero);
    result.set(bit, Logic4::one);
    return result;
}

ContainerType wide_string_associative_type()
{
    ContainerType result;
    result.element_kind = ContainerElementKind::String;
    result.element_width = 0U;
    result.associative = true;
    result.index_width = static_cast<std::uint32_t>(kIndexWidth);
    result.signed_indices = false;
    return result;
}

ContainerType wide_aggregate_associative_type()
{
    ContainerType member;
    member.element_kind = ContainerElementKind::Packed;
    member.element_width = 8U;
    member.fixed = true;
    member.index_left = 0;
    member.index_right = 0;

    ContainerType result;
    result.element_kind = ContainerElementKind::Aggregate;
    result.element_width = 0U;
    result.associative = true;
    result.index_width = static_cast<std::uint32_t>(kIndexWidth);
    result.signed_indices = false;
    result.element_nominal_type = "wide_record_t";
    result.element_types = { member };
    result.member_names = { "value" };
    return result;
}

ContainerType wide_aggregate_element_type()
{
    auto result = wide_aggregate_associative_type();
    result.associative = false;
    result.aggregate_value = true;
    return result;
}

ContainerValue aggregate_element(const std::uint32_t value)
{
    auto result = default_container_value(wide_aggregate_element_type());
    result.nested_elements.front().elements.front()
        = PackedLogic4::from_aval_bval(8U, value, 0U);
    return result;
}

ContainerValue initial_string_associative_value()
{
    auto result = default_container_value(wide_string_associative_type());
    result.keys = { key_with_bit(100U), key_with_bit(120U) };
    result.string_elements = { "at-100", "at-120" };
    return result;
}

ContainerValue initial_aggregate_source()
{
    auto result = default_container_value(wide_aggregate_associative_type());
    result.keys = { key_with_bit(96U), key_with_bit(136U) };
    result.nested_elements = {
        aggregate_element(0x11U), aggregate_element(0xa5U) };
    return result;
}

ContainerValue initial_aggregate_target()
{
    auto result = default_container_value(wide_aggregate_associative_type());
    result.keys = { key_with_bit(64U) };
    result.nested_elements = { aggregate_element(0x22U) };
    return result;
}

struct Outcome {
    RunResult run;
    ContainerValue strings;
    ContainerValue aggregate;
    std::string read_value;
    PackedLogic4 found;
    PackedLogic4 next_key;
    PackedLogic4 traversed;
};

Outcome run_numeric_key_methods(
    const compiler::JitOptimizationLevel level,
    const bool compiled)
{
    const std::array<std::uint32_t, 0U> no_signal_widths { };
    Interpreter interpreter;
    const auto string_object = interpreter.add_container_object({
        "wide_string_assoc", initial_string_associative_value(), std::nullopt });
    const auto source_object = interpreter.add_container_object({
        "wide_aggregate_source", initial_aggregate_source(), std::nullopt });
    const auto target_object = interpreter.add_container_object({
        "wide_aggregate_target", initial_aggregate_target(), std::nullopt });
    const auto readback_object = interpreter.add_string_object({
        "wide_string_readback", { } });

    Process process;
    process.id = 0U;
    process.name = "wide_associative_numeric_methods";
    process.register_count = 5U;
    process.string_register_count = 2U;
    process.container_register_count = 3U;
    process.container_register_types = {
        wide_string_associative_type(),
        wide_aggregate_associative_type(),
        wide_aggregate_associative_type(),
    };
    process.debug_locals = {
        { "found", "logic", 2U, 32U, { }, std::nullopt, std::nullopt,
            ValueKind::logic4, { } },
        { "next_key", "logic", 3U,
            static_cast<std::uint32_t>(kIndexWidth), { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } },
        { "traversed", "logic", 4U, 32U, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } },
    };
    process.operations = {
        ReadContainerObject { 0U, string_object },
        ReadContainerObject { 1U, source_object },
        ReadContainerObject { 2U, target_object },
        LoadConstant { 0U, key_with_bit(120U) },
        ContainerExists { 2U, 0U, 0U, false },
        ContainerStringRead { 1U, 0U, 0U, false, false, false, { } },
        WriteStringObject { readback_object, 1U },
        LoadStringConstant { 0U, "at-136" },
        LoadConstant { 0U, key_with_bit(136U) },
        ContainerStringWrite { 0U, 0U, 0U, false, false, false, { } },
        LoadConstant { 0U, key_with_bit(120U) },
        TraverseContainer {
            4U, 0U, 0U, ContainerTraversal::next, false },
        CopyRegister { 3U, 0U },
        LoadConstant { 0U, key_with_bit(136U) },
        DeleteContainer { 0U, 0U, false },
        LoadConstant { 0U, key_with_bit(130U) },
        LoadConstant { 1U, key_with_bit(136U) },
        CopyContainerAggregateElement { 2U, 0U, 1U, 1U, false, false },
        WriteContainerObject { string_object, 0U, std::nullopt },
        WriteContainerObject { target_object, 2U, std::nullopt },
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
        interpreter.container_object_value(string_object),
        interpreter.container_object_value(target_object),
        interpreter.string_object_value(readback_object),
        interpreter.read_debug_local(process_id, 0U),
        interpreter.read_debug_local(process_id, 1U),
        interpreter.read_debug_local(process_id, 2U),
    };
}

std::string run_wide_dynamic_string_index_failure(
    const compiler::JitOptimizationLevel level,
    const bool compiled,
    const PackedLogic4& index_value)
{
    const std::array<std::uint32_t, 0U> no_signal_widths { };
    ContainerType type;
    type.element_kind = ContainerElementKind::String;
    type.element_width = 0U;
    auto value = default_container_value(type);
    resize_container_value(value, 1U);

    Interpreter interpreter;
    const auto object = interpreter.add_container_object({
        "dynamic_strings", std::move(value), std::nullopt });
    Process process;
    process.id = 0U;
    process.name = "wide_dynamic_string_index";
    process.register_count = 1U;
    process.string_register_count = 1U;
    process.container_register_count = 1U;
    process.container_register_types = { type };
    process.operations = {
        ReadContainerObject { 0U, object },
        LoadConstant { 0U, index_value },
        ContainerStringRead { 0U, 0U, 0U, false, false, false, { } },
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
    try {
        (void)interpreter.run();
    } catch (const InterpreterError& error) {
        return error.what();
    }
    return { };
}

std::string run_unknown_wide_exists(
    const compiler::JitOptimizationLevel level,
    const bool compiled)
{
    const std::array<std::uint32_t, 0U> no_signal_widths { };
    auto value = default_container_value(wide_string_associative_type());
    value.keys = { key_with_bit(120U) };
    value.string_elements = { "present" };
    Interpreter interpreter;
    const auto object = interpreter.add_container_object({
        "unknown_wide_key", std::move(value), std::nullopt });
    auto unknown = PackedLogic4(kIndexWidth, Logic4::zero);
    unknown.set(100U, Logic4::x);
    Process process;
    process.id = 0U;
    process.name = "unknown_wide_associative_exists";
    process.register_count = 2U;
    process.container_register_count = 1U;
    process.container_register_types = { wide_string_associative_type() };
    process.operations = {
        ReadContainerObject { 0U, object },
        LoadConstant { 0U, unknown },
        ContainerExists { 1U, 0U, 0U, false },
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
    try {
        (void)interpreter.run();
    } catch (const InterpreterError& error) {
        return error.what();
    }
    return { };
}

PackedLogic4 mixed_unknown_wide_key(const Logic4 first_unknown)
{
    PackedLogic4 result(kIndexWidth, Logic4::zero);
    result.set(136U, Logic4::one);
    result.set(101U, first_unknown);
    result.set(
        7U,
        first_unknown == Logic4::x ? Logic4::z : Logic4::x);
    return result;
}

struct TraversalEdgeOutcome {
    RunResult run;
    PackedLogic4 status;
    PackedLogic4 key;
};

TraversalEdgeOutcome run_wide_traverse_edge(
    const std::string_view name,
    const ContainerTraversal traversal,
    const bool empty,
    const PackedLogic4& input_key,
    const compiler::JitOptimizationLevel level,
    const bool compiled)
{
    const std::array<std::uint32_t, 0U> no_signal_widths { };
    std::unique_ptr<compiler::LlvmJit> jit;
    std::optional<compiler::JitProcessHandle> handle;
    Interpreter interpreter;
    auto initial = default_container_value(wide_string_associative_type());
    if (!empty) {
        initial.keys = { key_with_bit(100U), key_with_bit(120U) };
        initial.string_elements = { "first", "last" };
    }
    const auto object = interpreter.add_container_object({
        "wide_traverse_edges", std::move(initial), std::nullopt });

    Process process;
    process.id = 0U;
    process.name = "wide_traverse_" + std::string { name };
    process.register_count = 3U;
    process.container_register_count = 1U;
    process.container_register_types = { wide_string_associative_type() };
    // Inspect a copy of the input key so the source index stays transient.
    process.debug_locals = {
        { "status", "logic", 1U, 32U, { }, std::nullopt, std::nullopt,
            ValueKind::logic4, { } },
        { "observed_key", "logic", 2U,
            static_cast<std::uint32_t>(kIndexWidth), { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } },
    };
    process.operations = {
        ReadContainerObject { 0U, object },
        LoadConstant { 0U, input_key },
        TraverseContainer { 1U, 0U, 0U, traversal, false },
        CopyRegister { 2U, 0U },
        Halt { },
    };

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
        interpreter.read_debug_local(process_id, 0U),
        interpreter.read_debug_local(process_id, 1U),
    };
}

void check_wide_traverse_edge(
    const std::string_view name,
    const ContainerTraversal traversal,
    const bool empty,
    const PackedLogic4& input_key,
    const std::uint32_t expected_status,
    const PackedLogic4& expected_key)
{
    using compiler::JitOptimizationLevel;
    const auto reference = run_wide_traverse_edge(
        name, traversal, empty, input_key,
        JitOptimizationLevel::o0, false);
    assert(reference.run.status == RunStatus::completed);
    assert(reference.status
        == PackedLogic4::from_aval_bval(32U, expected_status, 0U));
    assert(reference.key == expected_key);

    for (const auto level : {
             JitOptimizationLevel::o0, JitOptimizationLevel::o2 }) {
        const auto native = run_wide_traverse_edge(
            name, traversal, empty, input_key, level, true);
        assert(native.run.status == reference.run.status);
        assert(native.run.time == reference.run.time);
        assert(native.run.delta == reference.run.delta);
        assert(native.status == reference.status);
        assert(native.key == reference.key);
    }
}

#endif

} // namespace

int main()
{
#if defined(FSIM_HAS_LLVM)
    using compiler::JitOptimizationLevel;
    const auto reference = run_numeric_key_methods(
        JitOptimizationLevel::o0, false);
    assert(reference.run.status == RunStatus::completed);
    assert(reference.read_value == "at-120");
    assert(reference.found == PackedLogic4::from_aval_bval(32U, 1U, 0U));
    assert(reference.traversed == PackedLogic4::from_aval_bval(32U, 1U, 0U));
    assert(reference.next_key == key_with_bit(136U));
    assert((reference.strings.keys
        == std::vector<PackedLogic4> {
            key_with_bit(100U), key_with_bit(120U) }));
    assert((reference.strings.string_elements
        == std::vector<std::string> { "at-100", "at-120" }));
    assert((reference.aggregate.keys
        == std::vector<PackedLogic4> {
            key_with_bit(64U), key_with_bit(130U) }));
    assert(reference.aggregate.nested_elements.size() == 2U);
    assert(reference.aggregate.nested_elements[0].nested_elements[0]
            .elements[0]
        == PackedLogic4::from_aval_bval(8U, 0x22U, 0U));
    assert(reference.aggregate.nested_elements[1].nested_elements[0]
            .elements[0]
        == PackedLogic4::from_aval_bval(8U, 0xa5U, 0U));

    for (const auto level : {
             JitOptimizationLevel::o0, JitOptimizationLevel::o2 }) {
        const auto native = run_numeric_key_methods(level, true);
        assert(native.run.status == reference.run.status);
        assert(native.run.time == reference.run.time);
        assert(native.run.delta == reference.run.delta);
        assert(native.strings == reference.strings);
        assert(native.aggregate == reference.aggregate);
        assert(native.read_value == reference.read_value);
        assert(native.found == reference.found);
        assert(native.next_key == reference.next_key);
        assert(native.traversed == reference.traversed);
    }

    auto too_large = PackedLogic4(kIndexWidth, Logic4::zero);
    too_large.set(100U, Logic4::one);
    const auto reference_index_error = run_wide_dynamic_string_index_failure(
        JitOptimizationLevel::o0, false, too_large);
    assert(reference_index_error.find(
        "container index must be a known integral value")
        != std::string::npos);
    for (const auto level : {
             JitOptimizationLevel::o0, JitOptimizationLevel::o2 }) {
        const auto native_index_error
            = run_wide_dynamic_string_index_failure(level, true, too_large);
        assert(native_index_error.find(
            "container index must be a known integral value")
            != std::string::npos);
        const auto native_unknown_error = run_unknown_wide_exists(level, true);
        assert(native_unknown_error.find(
            "associative-array index must be a known integral value")
            != std::string::npos);
    }
    const auto reference_unknown_error = run_unknown_wide_exists(
        JitOptimizationLevel::o0, false);
    assert(reference_unknown_error.find(
        "associative-array index must be a known integral value")
        != std::string::npos);

    const auto mixed_x_key = mixed_unknown_wide_key(Logic4::x);
    const auto mixed_z_key = mixed_unknown_wide_key(Logic4::z);
    check_wide_traverse_edge(
        "first_unknown_key", ContainerTraversal::first, false,
        mixed_x_key, 1U, key_with_bit(100U));
    check_wide_traverse_edge(
        "last_unknown_key", ContainerTraversal::last, false,
        mixed_z_key, 1U, key_with_bit(120U));
    check_wide_traverse_edge(
        "empty_next_xz_key", ContainerTraversal::next, true,
        mixed_x_key, 0U, mixed_x_key);
    check_wide_traverse_edge(
        "empty_previous_zx_key", ContainerTraversal::previous, true,
        mixed_z_key, 0U, mixed_z_key);
#endif
    return 0;
}
