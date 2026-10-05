// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_region_frontier_test_support.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::compiler::test {
namespace {

using namespace runtime::simir;

constexpr ProcessId graph_producer_process_id = 0U;
constexpr ProcessId graph_consumer_process_id = 1U;
constexpr ProcessId producer_process_id = 7U;
constexpr ProcessId consumer_process_id = 23U;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

[[nodiscard]] ProcessSchedulingDomain scheduling_domain_for(
    const SignalUpdateDomain domain)
{
    return domain == SignalUpdateDomain::systemverilog_active
        ? ProcessSchedulingDomain::systemverilog
        : ProcessSchedulingDomain::generic;
}

[[nodiscard]] RegionUpdateKind update_kind_for(
    const SignalUpdateDomain domain)
{
    return domain == SignalUpdateDomain::systemverilog_active
        ? RegionUpdateKind::systemverilog_active
        : RegionUpdateKind::generic;
}

[[nodiscard]] RegionConeActivationKernel build_frontier_kernel(
    const std::vector<std::uint32_t>& input_widths,
    const std::uint32_t internal_width,
    const Operation& producer_transform,
    const RegisterId transformed_register,
    const SignalUpdateDomain write_domain,
    const std::size_t producer_register_count,
    const std::vector<RegisterId>& source_registers,
    const ValueKind value_kind = ValueKind::logic4)
{
    require(!input_widths.empty() && internal_width != 0U
            && input_widths.size() == source_registers.size(),
        "frontier fixture signal widths are nonzero");

    const auto internal_signal_id = static_cast<SignalId>(input_widths.size());
    const auto boundary_signal_id = static_cast<SignalId>(input_widths.size() + 1U);
    std::vector<RegionSignalDescriptor> signals;
    signals.reserve(input_widths.size() + 2U);
    for (const auto width : input_widths) {
        require(width != 0U,
            "frontier fixture signal widths are nonzero");
        signals.emplace_back(width);
    }
    signals.emplace_back(internal_width);
    signals.emplace_back(internal_width);
    signals[boundary_signal_id].observations = RegionObservation::current;
    for (auto& signal : signals) {
        signal.value_kind = value_kind;
    }

    Process producer;
    producer.id = graph_producer_process_id;
    producer.name = "frontier_test_producer";
    producer.scheduling_domain = scheduling_domain_for(write_domain);
    producer.register_count = producer_register_count;
    producer.register_value_kinds.assign(
        producer_register_count, value_kind);
    producer.driver_regions = {
        { internal_signal_id, 0U, 0U, true },
    };
    for (std::size_t index = 0U; index < input_widths.size(); ++index) {
        const auto signal = static_cast<SignalId>(index);
        producer.static_sensitivity.push_back({ signal, EdgeKind::any });
        producer.operations.push_back(ReadSignal {
            source_registers[index], signal,
        });
    }
    if (const auto* copy = operation_get_if<CopyRegister>(&producer_transform)) {
        producer.operations.push_back(*copy);
    } else if (const auto* reduction
        = operation_get_if<Reduction>(&producer_transform)) {
        producer.operations.push_back(*reduction);
    } else if (const auto* unary
        = operation_get_if<UnaryNot>(&producer_transform)) {
        producer.operations.push_back(*unary);
    } else if (const auto* binary
        = operation_get_if<Binary>(&producer_transform)) {
        producer.operations.push_back(*binary);
    } else if (const auto* select
        = operation_get_if<ConditionalSelect>(&producer_transform)) {
        producer.operations.push_back(*select);
    } else if (const auto* extract
        = operation_get_if<Extract>(&producer_transform)) {
        producer.operations.push_back(*extract);
    } else if (const auto* concatenate
        = operation_get_if<Concatenate>(&producer_transform)) {
        producer.operations.push_back(*concatenate);
    } else {
        throw std::invalid_argument {
            "frontier producer transform must be a supported packed opcode"
        };
    }
    producer.operations.push_back(WriteUpdate {
        internal_signal_id, transformed_register, write_domain,
    });
    producer.operations.push_back(WaitSensitivity { });
    producer.operations.push_back(Jump { 0U });

    Process consumer;
    consumer.id = graph_consumer_process_id;
    consumer.name = "frontier_test_consumer";
    consumer.scheduling_domain = scheduling_domain_for(write_domain);
    consumer.register_count = 1U;
    consumer.register_value_kinds = { value_kind };
    consumer.static_sensitivity = {
        { internal_signal_id, EdgeKind::any },
    };
    consumer.driver_regions = {
        { boundary_signal_id, 0U, 0U, true },
    };
    consumer.operations = {
        ReadSignal { 0U, internal_signal_id },
        WriteUpdate { boundary_signal_id, 0U, write_domain },
        WaitSensitivity { },
        Jump { 0U },
    };

    const std::array<const Process*, 2U> programs { &producer, &consumer };
    const auto graph = RegionGraph::build(programs, signals);
    const auto& components = graph.certificate_inventory().components;
    require(components.size() == 1U
            && components.front().members
                == std::vector<ProcessId> {
                    graph_producer_process_id, graph_consumer_process_id,
                }
            && components.front().structural_internal_signal_candidates
                == std::vector<SignalId> { internal_signal_id },
        "the graph certifies a two-member internal producer-consumer edge");

    auto program = graph.build_compute_program(0U, programs);
    require(program.has_value(),
        "the certified static two-member fixture builds a compute kernel");
    auto kernel = std::move(program->activation_kernel);
    for (auto& member : kernel.members) {
        if (member.process == graph_producer_process_id) {
            member.process = producer_process_id;
        } else if (member.process == graph_consumer_process_id) {
            member.process = consumer_process_id;
        }
    }
    for (auto& output : kernel.outputs) {
        if (output.owner == graph_producer_process_id) {
            output.owner = producer_process_id;
        } else if (output.owner == graph_consumer_process_id) {
            output.owner = consumer_process_id;
        }
    }
    require(kernel.members.size() == 2U
            && kernel.members[0U].process == producer_process_id
            && kernel.members[1U].process == consumer_process_id,
        "nonconsecutive original process ids map to two local member slots");
    require(kernel.internal_signals
                == std::vector<SignalId> { internal_signal_id },
        "the producer output is the one internal commit plane");
    require(kernel.outputs.size() == 2U,
        "the producer and consumer each retain one output site");

    const auto producer_output = std::ranges::find_if(kernel.outputs,
        [](const RegionConeOutputBinding& output) {
            return output.owner == producer_process_id;
        });
    const auto consumer_output = std::ranges::find_if(kernel.outputs,
        [](const RegionConeOutputBinding& output) {
            return output.owner == consumer_process_id;
        });
    const auto update_kind = update_kind_for(write_domain);
    require(producer_output != kernel.outputs.end()
            && producer_output->signal == internal_signal_id
            && producer_output->width == internal_width
            && producer_output->domain == write_domain
            && producer_output->update_kind == update_kind,
        "the producer output preserves its signal width and update domain");
    require(consumer_output != kernel.outputs.end()
            && consumer_output->signal == boundary_signal_id
            && consumer_output->width == internal_width
            && consumer_output->domain == write_domain
            && consumer_output->update_kind == update_kind,
        "the consumer output preserves its signal width and update domain");
    return kernel;
}

} // namespace

RegionConeActivationKernel make_certified_frontier_kernel(
    const std::uint32_t width, const SignalUpdateDomain write_domain,
    const ValueKind value_kind)
{
    return build_frontier_kernel({ width }, width,
        Operation { CopyRegister { 1U, 0U } }, 1U, write_domain, 2U, { 0U },
        value_kind);
}

RegionConeActivationKernel remap_frontier_physical_ids(
    RegionConeActivationKernel kernel, const SignalId signal_delta,
    const ProcessId process_delta)
{
    const auto remap_id = [](std::uint32_t& id, const std::uint32_t delta,
                              const bool allow_sentinel) {
        if (allow_sentinel && id == std::numeric_limits<std::uint32_t>::max()) {
            return;
        }
        if (delta > std::numeric_limits<std::uint32_t>::max() - id) {
            throw std::invalid_argument {
                "frontier fixture physical ID remap overflows"
            };
        }
        id += delta;
    };

    remap_id(kernel.program.id, process_delta, true);
    for (auto& sensitivity : kernel.program.static_sensitivity) {
        remap_id(sensitivity.signal, signal_delta, false);
    }
    for (auto& region : kernel.program.driver_regions) {
        remap_id(region.signal, signal_delta, false);
    }
    for (auto& input : kernel.inputs) {
        remap_id(input.signal, signal_delta, false);
    }
    for (auto& signal : kernel.internal_signals) {
        remap_id(signal, signal_delta, false);
    }
    for (auto& member : kernel.members) {
        remap_id(member.process, process_delta, false);
        for (auto& sensitivity : member.sensitivities) {
            remap_id(sensitivity.signal, signal_delta, false);
        }
    }
    for (auto& output : kernel.outputs) {
        remap_id(output.owner, process_delta, true);
        remap_id(output.signal, signal_delta, false);
    }
    for (auto& input : kernel.constant_inputs) {
        remap_id(input.owner, process_delta, true);
        remap_id(input.signal, signal_delta, false);
    }
    return kernel;
}

RegionConeActivationKernel make_certified_reduction_frontier_kernel(
    const std::uint32_t input_width, const ReductionOperator operation)
{
    return build_frontier_kernel({ input_width }, 1U,
        Operation { Reduction { operation, 1U, 0U } }, 1U,
        SignalUpdateDomain::systemverilog_active, 2U, { 0U });
}

RegionConeActivationKernel make_certified_unary_not_frontier_kernel(
    const std::uint32_t width, const ValueKind value_kind,
    const SignalUpdateDomain write_domain)
{
    return build_frontier_kernel({ width }, width,
        Operation { UnaryNot { 1U, 0U } }, 1U,
        write_domain, 2U, { 0U }, value_kind);
}

RegionConeActivationKernel make_certified_binary_frontier_kernel(
    const std::uint32_t width, const BinaryOperator operation,
    const ValueKind value_kind, const SignalUpdateDomain write_domain)
{
    return build_frontier_kernel({ width, width }, width,
        Operation { Binary { operation, 2U, 0U, 1U } }, 2U,
        write_domain, 3U, { 0U, 1U }, value_kind);
}

RegionConeActivationKernel make_certified_conditional_select_frontier_kernel(
    const std::uint32_t width)
{
    return build_frontier_kernel({ 1U, width, width }, width,
        Operation { ConditionalSelect { 3U, 0U, 1U, 2U } }, 3U,
        SignalUpdateDomain::systemverilog_active, 4U,
        { 0U, 1U, 2U });
}

RegionConeActivationKernel make_certified_extract_frontier_kernel(
    const std::uint32_t source_width,
    const std::uint32_t offset,
    const std::uint32_t width)
{
    require(source_width != 0U && width != 0U && offset < source_width
            && width <= source_width - offset,
        "frontier extract is a nonempty range inside its source signal");
    return build_frontier_kernel({ source_width }, width,
        Operation { Extract { 1U, 0U, offset, width } }, 1U,
        SignalUpdateDomain::systemverilog_active, 2U, { 0U });
}

RegionConeActivationKernel make_certified_concatenate_frontier_kernel(
    const std::span<const std::uint32_t> operand_widths)
{
    require(!operand_widths.empty()
            && operand_widths.size()
                < std::numeric_limits<RegisterId>::max(),
        "frontier concatenate has representable source and destination registers");
    std::uint32_t width { };
    std::vector<RegisterId> sources;
    sources.reserve(operand_widths.size());
    for (const auto operand_width : operand_widths) {
        require(operand_width != 0U
                && operand_width
                    <= std::numeric_limits<std::uint32_t>::max() - width,
            "frontier concatenate operands have a nonzero representable sum");
        width += operand_width;
        sources.push_back(static_cast<RegisterId>(sources.size()));
    }
    const auto destination = static_cast<RegisterId>(sources.size());
    const std::vector<std::uint32_t> input_widths {
        operand_widths.begin(), operand_widths.end()
    };
    return build_frontier_kernel(input_widths, width,
        Operation { Concatenate { destination, sources, width } },
        destination, SignalUpdateDomain::systemverilog_active,
        sources.size() + 1U, sources);
}

} // namespace fsim::compiler::test
