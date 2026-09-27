// SPDX-License-Identifier: Apache-2.0
#include "../../src/app/application_internal.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <iostream>
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
constexpr std::size_t kElementWidth = 129U;

PackedLogic4 key_with_bit(const std::size_t bit)
{
    PackedLogic4 result(kIndexWidth, Logic4::zero);
    result.set(bit, Logic4::one);
    return result;
}

PackedLogic4 index_one()
{
    PackedLogic4 result(kIndexWidth, Logic4::zero);
    result.set(0U, Logic4::one);
    return result;
}

PackedLogic4 value_with_bits(const std::initializer_list<std::size_t> bits)
{
    PackedLogic4 result(kElementWidth, Logic4::zero);
    for (const auto bit : bits) {
        result.set(bit, Logic4::one);
    }
    return result;
}

PackedLogic4 value_with_upper_xz()
{
    PackedLogic4 result(kElementWidth, Logic4::zero);
    result.set(3U, Logic4::one);
    result.set(72U, Logic4::x);
    result.set(128U, Logic4::z);
    return result;
}

ContainerType wide_associative_type()
{
    ContainerType result;
    result.element_width = static_cast<std::uint32_t>(kElementWidth);
    result.associative = true;
    result.index_width = static_cast<std::uint32_t>(kIndexWidth);
    result.signed_indices = false;
    return result;
}

ContainerValue initial_wide_associative_value()
{
    const auto type = wide_associative_type();
    return {
        type,
        { value_with_bits({ 0U }), value_with_upper_xz(),
            value_with_bits({ 128U }) },
        { key_with_bit(64U), key_with_bit(96U), key_with_bit(136U) },
    };
}

struct Outcome {
    RunResult run;
    ContainerValue value;
};

Outcome run_wide_associative_case(
    const compiler::JitOptimizationLevel level,
    const bool compiled)
{
    const std::array<std::uint32_t, 0U> no_signal_widths { };
    Interpreter interpreter;
    const auto object = interpreter.add_container_object({
        "wide_associative",
        initial_wide_associative_value(),
        std::nullopt,
    });

    Process process;
    process.id = 0U;
    process.name = "wide_associative_index";
    process.register_count = 4U;
    process.container_register_count = 1U;
    process.container_register_types = { wide_associative_type() };
    process.operations = {
        ReadContainerObject { 0U, object },
        LoadConstant { 0U, key_with_bit(96U) },
        ContainerRead { 2U, 0U, 0U, false, false, false },
        LoadConstant { 1U, key_with_bit(136U) },
        ContainerWrite { 0U, 1U, 2U, false, false, false },
        LoadConstant { 1U, key_with_bit(120U) },
        LoadConstant { 3U, value_with_bits({ 7U, 70U, 100U }) },
        ContainerWrite { 0U, 1U, 3U, false, false, false },
        WriteContainerObject { object, 0U, std::nullopt },
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
    return { run, interpreter.container_object_value(object) };
}

ContainerType wide_queue_type()
{
    ContainerType result;
    result.element_width = static_cast<std::uint32_t>(kElementWidth);
    result.queue = true;
    result.maximum_elements = 8U;
    return result;
}

Outcome run_wide_queue_case(
    const compiler::JitOptimizationLevel level,
    const bool compiled)
{
    const std::array<std::uint32_t, 0U> no_signal_widths { };
    const auto type = wide_queue_type();
    const auto initial_element = value_with_bits({ 5U, 101U, 127U });
    Interpreter interpreter;
    const auto object = interpreter.add_container_object({
        "wide_queue", ContainerValue { type, { initial_element }, { } },
        std::nullopt,
    });

    Process process;
    process.id = 0U;
    process.name = "wide_queue_values";
    process.register_count = 4U;
    process.container_register_count = 1U;
    process.container_register_types = { type };
    process.operations = {
        ReadContainerObject { 0U, object },
        ContainerReduction {
            ContainerReductionOperator::bit_or, 1U, 0U, { } },
        PushContainer { 0U, 1U, false, std::nullopt },
        LoadConstant { 2U, value_with_upper_xz() },
        PushContainer { 0U, 2U, false, std::nullopt },
        PopContainer { 3U, 0U, false },
        PushContainer { 0U, 3U, false, std::nullopt },
        WriteContainerObject { object, 0U, std::nullopt },
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
    return { run, interpreter.container_object_value(object) };
}

ContainerType wide_nested_container_type(const bool associative)
{
    ContainerType inner;
    inner.element_width = static_cast<std::uint32_t>(kElementWidth);

    ContainerType result;
    result.element_kind = ContainerElementKind::Container;
    result.element_width = 0U;
    result.associative = associative;
    result.index_width = static_cast<std::uint32_t>(kIndexWidth);
    result.signed_indices = false;
    result.element_types = { inner };
    return result;
}

ContainerValue nested_container(
    const ContainerType& type, const PackedLogic4& value)
{
    return { type, { value }, { } };
}

struct NestedOutcome {
    RunResult run;
    ContainerValue outer;
    ContainerValue observed;
};

NestedOutcome run_wide_nested_element_case(
    const compiler::JitOptimizationLevel level,
    const bool compiled,
    const bool associative)
{
    const std::array<std::uint32_t, 0U> no_signal_widths { };
    const auto outer_type = wide_nested_container_type(associative);
    const auto inner_type = outer_type.element_types.front();
    const auto original = nested_container(
        inner_type, value_with_bits({ 18U, 90U }));
    const auto replacement = nested_container(
        inner_type, value_with_upper_xz());

    ContainerValue initial_outer;
    initial_outer.type = outer_type;
    if (associative) {
        initial_outer.keys = { key_with_bit(96U), key_with_bit(136U) };
    }
    initial_outer.nested_elements = {
        original,
        nested_container(inner_type, value_with_bits({ 44U, 110U })),
    };

    Interpreter interpreter;
    const auto outer_object = interpreter.add_container_object({
        "wide_nested_outer", initial_outer, std::nullopt,
    });
    const auto replacement_object = interpreter.add_container_object({
        "wide_nested_replacement", replacement, std::nullopt,
    });
    const auto observed_object = interpreter.add_container_object({
        "wide_nested_observed", original, std::nullopt,
    });

    Process process;
    process.id = 0U;
    process.name = "wide_nested_elements";
    process.register_count = 1U;
    process.container_register_count = 3U;
    process.container_register_types = {
        outer_type, inner_type, inner_type,
    };
    process.operations = {
        ReadContainerObject { 0U, outer_object },
        ReadContainerObject { 2U, replacement_object },
        LoadConstant {
            0U,
            associative
                ? key_with_bit(136U)
                : index_one(),
        },
        ContainerElementWrite { 0U, 0U, 2U, false },
        ContainerElementRead { 1U, 0U, 0U, false },
        WriteContainerObject { outer_object, 0U, std::nullopt },
        WriteContainerObject { observed_object, 1U, std::nullopt },
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
        interpreter.container_object_value(outer_object),
        interpreter.container_object_value(observed_object),
    };
}

ContainerType wide_fixed_leaf_type()
{
    ContainerType result;
    result.element_width = static_cast<std::uint32_t>(kElementWidth);
    result.fixed = true;
    result.index_left = 0;
    result.index_right = 0;
    result.dimensions = { { 0, 0 } };
    return result;
}

ContainerType wide_aggregate_type()
{
    ContainerType result;
    result.element_kind = ContainerElementKind::Aggregate;
    result.element_width = 0U;
    result.associative = true;
    result.index_width = static_cast<std::uint32_t>(kIndexWidth);
    result.signed_indices = false;
    result.element_types = { wide_fixed_leaf_type() };
    result.member_names = { "value" };
    return result;
}

ContainerValue wide_aggregate_record(
    const ContainerType& type, const PackedLogic4& value)
{
    auto record_type = type;
    record_type.associative = false;
    record_type.aggregate_value = true;
    record_type.maximum_elements.reset();
    record_type.dimensions.clear();
    record_type.index_left = 0;
    record_type.index_right = 0;
    auto result = default_container_value(record_type);
    result.nested_elements.front().elements.front() = value;
    return result;
}

ContainerValue wide_aggregate_value(const PackedLogic4& value)
{
    const auto type = wide_aggregate_type();
    auto result = default_container_value(type);
    result.keys = { key_with_bit(136U) };
    result.nested_elements = { wide_aggregate_record(type, value) };
    return result;
}

Outcome run_wide_aggregate_case(
    const compiler::JitOptimizationLevel level,
    const bool compiled)
{
    const std::array<std::uint32_t, 0U> no_signal_widths { };
    const auto type = wide_aggregate_type();
    Interpreter interpreter;
    const auto object = interpreter.add_container_object({
        "wide_aggregate", wide_aggregate_value(value_with_bits({ 99U })),
        std::nullopt,
    });

    Process process;
    process.id = 0U;
    process.name = "wide_aggregate_values";
    process.register_count = 4U;
    process.container_register_count = 1U;
    process.container_register_types = { type };
    process.operations = {
        ReadContainerObject { 0U, object },
        LoadConstant { 0U, key_with_bit(136U) },
        ContainerAggregateRead { 1U, 0U, 0U, { 0U }, false, false },
        LoadConstant { 2U, value_with_upper_xz() },
        ContainerAggregateWrite { 0U, 0U, 2U, { 0U }, false, false },
        ContainerAggregateRead { 3U, 0U, 0U, { 0U }, false, false },
        ContainerAggregateWrite { 0U, 0U, 3U, { 0U }, false, false },
        WriteContainerObject { object, 0U, std::nullopt },
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
    return { run, interpreter.container_object_value(object) };
}

std::string run_index_failure(
    const compiler::JitOptimizationLevel level,
    const bool compiled,
    const ContainerType& container_type,
    const ContainerValue& container_value,
    const PackedLogic4& index_value,
    const bool signed_index,
    const bool write,
    const bool linear_index = false,
    const bool logic9_index = false)
{
    const std::array<std::uint32_t, 0U> no_signal_widths { };
    Interpreter interpreter;
    const auto object = interpreter.add_container_object({
        "index_failure", container_value, std::nullopt,
    });

    Process process;
    process.id = 0U;
    process.name = "wide_container_index_failure";
    process.register_count = 2U;
    if (logic9_index) {
        process.register_value_kinds = {
            ValueKind::logic9, ValueKind::logic4,
        };
    }
    process.container_register_count = 1U;
    process.container_register_types = { container_type };
    process.operations = {
        LoadConstant { 0U, index_value },
        ReadContainerObject { 0U, object },
    };
    if (write) {
        process.operations.emplace_back(
            LoadConstant { 1U, container_value.elements.front() });
        process.operations.emplace_back(
            ContainerWrite {
                0U, 0U, 1U, signed_index, linear_index, false });
    } else {
        process.operations.emplace_back(
            ContainerRead {
                1U, 0U, 0U, signed_index, linear_index, false });
    }
    process.operations.emplace_back(Halt { });

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

void expect_failure(
    const compiler::JitOptimizationLevel level,
    const ContainerType& type,
    const ContainerValue& value,
    const PackedLogic4& index_value,
    const bool signed_index,
    const bool write,
    const bool linear_index,
    const std::string_view fragment,
    const bool logic9_index = false)
{
    const auto reference = run_index_failure(
        level, false, type, value, index_value, signed_index, write,
        linear_index, logic9_index);
    assert(reference.find(fragment) != std::string::npos);
    const auto native = run_index_failure(
        level, true, type, value, index_value, signed_index, write,
        linear_index, logic9_index);
    if (native.find(fragment) == std::string::npos) {
        std::cerr << "expected index error: " << fragment
                  << "; native error: " << native << '\n';
    }
    assert(native.find(fragment) != std::string::npos);
}

std::string run_logic9_source_failure(
    const compiler::JitOptimizationLevel level,
    const bool compiled,
    const bool aggregate)
{
    const std::array<std::uint32_t, 0U> no_signal_widths { };
    auto type = aggregate
        ? wide_aggregate_type() : wide_queue_type();
    type.element_width = aggregate ? 0U : 8U;
    if (aggregate) {
        type.index_width = 8U;
        type.element_types.front().element_width = 8U;
    }
    auto initial = ContainerValue { type, { }, { } };
    if (aggregate) {
        initial.keys = { PackedLogic4::from_msb_string("00000001") };
        initial.nested_elements = { wide_aggregate_record(
            type, PackedLogic4::from_msb_string("00000000")) };
    }
    Interpreter interpreter;
    const auto object = interpreter.add_container_object({
        "logic9_container_source", std::move(initial), std::nullopt,
    });

    Process process;
    process.id = 0U;
    process.name = aggregate
        ? "logic9_aggregate_source" : "logic9_queue_source";
    process.register_count = 2U;
    process.register_value_kinds = {
        ValueKind::logic4, ValueKind::logic9,
    };
    process.container_register_count = 1U;
    process.container_register_types = { type };
    process.operations.emplace_back(ReadContainerObject { 0U, object });
    if (aggregate) {
        process.operations.emplace_back(
            LoadConstant { 0U, PackedLogic4::from_msb_string("00000001") });
    }
    process.operations.emplace_back(LoadConstant {
        1U, PackedLogic4::from_logic9_msb_string(
            "HHHHHHHH"),
    });
    if (aggregate) {
        process.operations.emplace_back(ContainerAggregateWrite {
            0U, 0U, 1U, { 0U }, false, false,
        });
    } else {
        process.operations.emplace_back(
            PushContainer { 0U, 1U, false, std::nullopt });
    }
    process.operations.emplace_back(Halt { });

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

#endif

} // namespace

int main()
{
#if defined(FSIM_HAS_LLVM)
    using compiler::JitOptimizationLevel;
    const auto reference = run_wide_associative_case(
        JitOptimizationLevel::o0, false);
    assert(reference.run.status == RunStatus::completed);
    const auto expected_keys = std::vector<PackedLogic4> {
        key_with_bit(64U), key_with_bit(96U), key_with_bit(120U),
        key_with_bit(136U),
    };
    const auto expected_values = std::vector<PackedLogic4> {
        value_with_bits({ 0U }), value_with_upper_xz(),
        value_with_bits({ 7U, 70U, 100U }), value_with_upper_xz(),
    };
    assert(reference.value.keys == expected_keys);
    assert(reference.value.elements == expected_values);

    for (const auto level : {
             JitOptimizationLevel::o0, JitOptimizationLevel::o2 }) {
        const auto native = run_wide_associative_case(level, true);
        assert(native.run.status == reference.run.status);
        assert(native.run.time == reference.run.time);
        assert(native.run.delta == reference.run.delta);
        assert(native.value == reference.value);
    }

    const auto queue_reference = run_wide_queue_case(
        JitOptimizationLevel::o0, false);
    const auto queue_element = value_with_bits({ 5U, 101U, 127U });
    const auto queue_expected = ContainerValue {
        wide_queue_type(),
        { queue_element, queue_element, value_with_upper_xz() },
        { },
    };
    assert(queue_reference.run.status == RunStatus::completed);
    assert(queue_reference.value == queue_expected);
    for (const auto level : {
             JitOptimizationLevel::o0, JitOptimizationLevel::o2 }) {
        const auto native = run_wide_queue_case(level, true);
        assert(native.run.status == queue_reference.run.status);
        assert(native.run.time == queue_reference.run.time);
        assert(native.run.delta == queue_reference.run.delta);
        assert(native.value == queue_expected);
    }

    const auto nested_reference = run_wide_nested_element_case(
        JitOptimizationLevel::o0, false, true);
    const auto replacement = nested_container(
        wide_nested_container_type(true).element_types.front(),
        value_with_upper_xz());
    auto nested_expected = nested_reference.outer;
    nested_expected.nested_elements[1] = replacement;
    assert(nested_reference.run.status == RunStatus::completed);
    assert(nested_reference.outer == nested_expected);
    assert(nested_reference.observed == replacement);
    for (const auto level : {
             JitOptimizationLevel::o0, JitOptimizationLevel::o2 }) {
        const auto native = run_wide_nested_element_case(level, true, true);
        assert(native.run.status == nested_reference.run.status);
        assert(native.run.time == nested_reference.run.time);
        assert(native.run.delta == nested_reference.run.delta);
        assert(native.outer == nested_expected);
        assert(native.observed == replacement);
    }

    const auto dynamic_nested_reference = run_wide_nested_element_case(
        JitOptimizationLevel::o0, false, false);
    auto dynamic_nested_expected = dynamic_nested_reference.outer;
    dynamic_nested_expected.nested_elements[1] = replacement;
    assert(dynamic_nested_reference.run.status == RunStatus::completed);
    assert(dynamic_nested_reference.outer == dynamic_nested_expected);
    assert(dynamic_nested_reference.observed == replacement);
    for (const auto level : {
             JitOptimizationLevel::o0, JitOptimizationLevel::o2 }) {
        const auto native = run_wide_nested_element_case(level, true, false);
        assert(native.run.status == dynamic_nested_reference.run.status);
        assert(native.run.time == dynamic_nested_reference.run.time);
        assert(native.run.delta == dynamic_nested_reference.run.delta);
        assert(native.outer == dynamic_nested_expected);
        assert(native.observed == replacement);
    }

    const auto aggregate_reference = run_wide_aggregate_case(
        JitOptimizationLevel::o0, false);
    const auto aggregate_expected = wide_aggregate_value(value_with_upper_xz());
    assert(aggregate_reference.run.status == RunStatus::completed);
    assert(aggregate_reference.value == aggregate_expected);
    for (const auto level : {
             JitOptimizationLevel::o0, JitOptimizationLevel::o2 }) {
        const auto native = run_wide_aggregate_case(level, true);
        assert(native.run.status == aggregate_reference.run.status);
        assert(native.run.time == aggregate_reference.run.time);
        assert(native.run.delta == aggregate_reference.run.delta);
        assert(native.value == aggregate_expected);
    }

    auto unknown_x = PackedLogic4(kIndexWidth, Logic4::zero);
    unknown_x.set(100U, Logic4::x);
    auto unknown_z = PackedLogic4(kIndexWidth, Logic4::zero);
    unknown_z.set(130U, Logic4::z);
    const auto associative_type = wide_associative_type();
    const auto associative_value = initial_wide_associative_value();
    for (const auto level : {
             JitOptimizationLevel::o0, JitOptimizationLevel::o2 }) {
        expect_failure(
            level, associative_type, associative_value, unknown_x,
            false, false, false,
            "associative-array index must be a known integral value");
        expect_failure(
            level, associative_type, associative_value, unknown_z,
            false, true, false,
            "associative-array index must be a known integral value");
    }

    ContainerType logic9_key_type;
    logic9_key_type.element_width = 8U;
    logic9_key_type.associative = true;
    logic9_key_type.index_width = 8U;
    const ContainerValue logic9_key_container {
        logic9_key_type,
        { PackedLogic4::from_msb_string("00000000") },
        { PackedLogic4::from_msb_string("00000001") },
    };
    const auto logic9_key
        = PackedLogic4::from_logic9_msb_string("0000000H");
    for (const auto level : {
             JitOptimizationLevel::o0, JitOptimizationLevel::o2 }) {
        expect_failure(
            level, logic9_key_type, logic9_key_container, logic9_key,
            false, false, false,
            "associative-array index must be a known integral value", true);
        for (const auto aggregate : { false, true }) {
            const auto fragment = aggregate
                ? "aggregate member write leaf type mismatch"
                : "queue element write type mismatch";
            const auto source_reference = run_logic9_source_failure(
                level, false, aggregate);
            const auto native = run_logic9_source_failure(
                level, true, aggregate);
            assert(source_reference.find(fragment) != std::string::npos);
            assert(native.find(fragment) != std::string::npos);
        }
    }

    ContainerType dynamic_type;
    dynamic_type.element_width = 8U;
    const ContainerValue dynamic_value {
        dynamic_type, { PackedLogic4(8U, Logic4::one) }, { },
    };
    auto unsigned_overflow = PackedLogic4(kIndexWidth, Logic4::zero);
    unsigned_overflow.set(100U, Logic4::one);
    auto signed_overflow = PackedLogic4(kIndexWidth, Logic4::zero);
    signed_overflow.set(63U, Logic4::one);
    auto signed_negative = PackedLogic4(kIndexWidth, Logic4::one);
    auto signed_positive = PackedLogic4(kIndexWidth, Logic4::zero);
    signed_positive.set(30U, Logic4::one);
    auto signed_wide_positive = PackedLogic4(64U, Logic4::zero);
    signed_wide_positive.set(31U, Logic4::one);
    auto signed_wide_negative = PackedLogic4(64U, Logic4::zero);
    signed_wide_negative.set(63U, Logic4::one);
    const auto signed_narrow_negative
        = PackedLogic4::from_aval_bval(8U, 0xffU, 0U);
    for (const auto level : {
             JitOptimizationLevel::o0, JitOptimizationLevel::o2 }) {
        expect_failure(
            level, dynamic_type, dynamic_value, signed_wide_positive,
            true, false, false, "container index is out of range");
        expect_failure(
            level, dynamic_type, dynamic_value, signed_wide_negative,
            true, false, false, "container index cannot be negative");
        expect_failure(
            level, dynamic_type, dynamic_value, signed_narrow_negative,
            true, false, false, "container index cannot be negative");
        expect_failure(
            level, dynamic_type, dynamic_value, unsigned_overflow,
            false, false, false,
            "container index must be a known integral value");
        expect_failure(
            level, dynamic_type, dynamic_value, signed_overflow,
            true, true, false,
            "container index must be a known integral value");
        expect_failure(
            level, dynamic_type, dynamic_value, signed_negative,
            true, false, false, "container index cannot be negative");
        expect_failure(
            level, dynamic_type, dynamic_value, signed_positive,
            true, false, false, "container index is out of range");
    }

    ContainerType fixed_type;
    fixed_type.element_width = 8U;
    fixed_type.fixed = true;
    fixed_type.index_left = 1;
    fixed_type.index_right = 0;
    const ContainerValue fixed_value {
        fixed_type,
        { PackedLogic4(8U, Logic4::zero), PackedLogic4(8U, Logic4::one) },
        { },
    };
    for (const auto level : {
             JitOptimizationLevel::o0, JitOptimizationLevel::o2 }) {
        expect_failure(
            level, fixed_type, fixed_value, signed_positive,
            true, true, true,
            "multidimensional linear index is out of range");
    }
#endif
    return 0;
}
