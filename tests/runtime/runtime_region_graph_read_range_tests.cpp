// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/simir_region_graph.hpp"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using fsim::runtime::Logic4;
using fsim::runtime::PackedLogic4;
using namespace fsim::runtime::simir;

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

RegionGraph build_read_graph(Process& process)
{
    const std::array<const Process*, 1U> programs { &process };
    const std::array<RegionSignalDescriptor, 2U> signals {
        RegionSignalDescriptor { 16U },
        RegionSignalDescriptor { 16U },
    };
    return RegionGraph::build(programs, signals);
}

void set_program(Process& process, std::vector<Operation> operations,
    std::vector<Sensitivity> sensitivities = { })
{
    process.id = 0U;
    process.name = "read_range_witness";
    process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    process.register_count = 4U;
    process.static_sensitivity = std::move(sensitivities);
    process.operations = std::move(operations);
}

void check_static_extract_ranges()
{
    Process process;
    set_program(process, {
        ReadSignal { 0U, 0U },
        Extract { 1U, 0U, 3U, 5U },
        Halt { },
    });
    const auto graph = build_read_graph(process);
    const auto readers = graph.signals()[0U].readers;
    require(readers == std::vector<RegionAccess> {
                { 0U, 3U, 5U, EdgeKind::any } },
        "a current whole read used only by one constant Extract records that exact demand");

    set_program(process, {
        ReadSignal { 0U, 0U },
        CopyRegister { 1U, 0U },
        Extract { 2U, 1U, 0U, 2U },
        Extract { 3U, 1U, 9U, 4U },
        Halt { },
    });
    const auto copied_graph = build_read_graph(process);
    const auto copied_readers = copied_graph.signals()[0U].readers;
    require(copied_readers == std::vector<RegionAccess> {
                { 0U, 0U, 2U, EdgeKind::any },
                { 0U, 9U, 4U, EdgeKind::any } },
        "copy-only aliases preserve every disjoint constant Extract range");
}

void check_ambiguous_reads_stay_whole()
{
    Process process;
    set_program(process, {
        ReadSignal { 0U, 0U },
        Extract { 1U, 0U, 3U, 5U },
        UnaryNot { 2U, 0U },
        Halt { },
    });
    auto graph = build_read_graph(process);
    require(graph.signals()[0U].readers
            == std::vector<RegionAccess> { { 0U, 0U, 0U, EdgeKind::any } },
        "an additional full-value use keeps the whole read dependency");

    set_program(process, {
        ReadSignal { 0U, 0U },
        DynamicExtract { 1U, 0U, DynamicIndex { 2U, 15, 0, 0U, false } },
        Halt { },
    });
    graph = build_read_graph(process);
    require(graph.signals()[0U].readers
            == std::vector<RegionAccess> { { 0U, 0U, 0U, EdgeKind::any } },
        "a dynamic Extract keeps the whole read dependency");

    set_program(process, {
        ReadSignal { 0U, 0U },
        LoadConstant { 0U, PackedLogic4(16U, Logic4::zero) },
        Extract { 1U, 0U, 3U, 5U },
        Halt { },
    });
    graph = build_read_graph(process);
    require(graph.signals()[0U].readers
            == std::vector<RegionAccess> { { 0U, 0U, 0U, EdgeKind::any } },
        "a read register redefinition keeps the original read conservative");

    set_program(process, {
        ReadSignal { 0U, 0U },
        Extract { 1U, 0U, 3U, 5U },
        Branch { 0U, 2U, 2U, UnknownBranchPolicy::when_false },
        Halt { },
    });
    graph = build_read_graph(process);
    require(graph.signals()[0U].readers
            == std::vector<RegionAccess> { { 0U, 0U, 0U, EdgeKind::any } },
        "a raw read value used as branch control keeps the whole read");

    set_program(process, {
        ReadSignal { 0U, 0U },
        Extract { 1U, 0U, 14U, 4U },
        Halt { },
    });
    graph = build_read_graph(process);
    require(graph.signals()[0U].readers
            == std::vector<RegionAccess> { { 0U, 0U, 0U, EdgeKind::any } },
        "an invalid static range keeps the whole read");
}

void check_explicit_sensitivity_is_preserved()
{
    Process process;
    set_program(process, {
        ReadSignal { 0U, 0U },
        Extract { 1U, 0U, 3U, 5U },
        Halt { },
    }, { { 0U, EdgeKind::any } });
    const auto graph = build_read_graph(process);
    require(graph.signals()[0U].readers
            == std::vector<RegionAccess> { { 0U, 0U, 0U, EdgeKind::any } },
        "a whole explicit sensitivity remains a whole dependency beside a precise value demand");
}

void check_wait_operands_keep_reads_whole()
{
    std::vector<Operation> waits;
    WaitFor delay;
    delay.source = 0U;
    waits.push_back(delay);
    WaitOn event_wait;
    event_wait.signals = { 1U };
    event_wait.timeout = 1U;
    event_wait.timeout_result = 0U;
    waits.push_back(event_wait);
    waits.push_back(WaitOrder { { 1U }, 0U });
    for (const auto& wait : waits) {
        Process process;
        set_program(process, {
            ReadSignal { 0U, 0U }, wait,
            Extract { 1U, 0U, 3U, 5U }, Halt { },
        });
        const auto graph = build_read_graph(process);
        require(graph.signals()[0U].readers
                == std::vector<RegionAccess> { { 0U, 0U, 0U, EdgeKind::any } },
            "wait value uses and result definitions prevent unsafe read narrowing");
    }

    Process process;
    set_program(process, {
        ReadSignal { 0U, 0U }, CopyRegister { 1U, 0U },
        LoadConstant { 1U, PackedLogic4(16U, Logic4::zero) },
        Extract { 2U, 1U, 3U, 5U }, Halt { },
    });
    const auto graph = build_read_graph(process);
    require(graph.signals()[0U].readers
            == std::vector<RegionAccess> { { 0U, 0U, 0U, EdgeKind::any } },
        "redefining a copied register breaks single-definition read provenance");
}

void check_read_metadata_and_debug_exposure()
{
    for (unsigned variant = 0U; variant < 4U; ++variant) {
        ReadSignal read { 0U, 0U };
        if (variant == 0U) {
            read.kind = SignalReadKind::past;
        } else if (variant == 1U) {
            read.ticks = 2U;
        } else if (variant == 2U) {
            read.clock = 1U;
        } else {
            read.gate = 1U;
        }
        Process process;
        set_program(process, {
            read, Extract { 1U, 0U, 3U, 5U }, Halt { },
        });
        const auto graph = build_read_graph(process);
        require(graph.signals()[0U].readers
                == std::vector<RegionAccess> { { 0U, 0U, 0U, EdgeKind::any } },
            "history, unusual ticks, clock and gate metadata retain whole dependencies");
    }

    Process process;
    set_program(process, {
        ReadSignal { 0U, 0U }, CopyRegister { 1U, 0U },
        Extract { 2U, 1U, 3U, 5U }, Halt { },
    });
    DebugLocal local;
    local.name = "full_copy";
    local.register_id = 1U;
    local.width = 16U;
    process.debug_locals.push_back(std::move(local));
    const auto graph = build_read_graph(process);
    require(graph.signals()[0U].readers
            == std::vector<RegionAccess> { { 0U, 0U, 0U, EdgeKind::any } },
        "a debugger-visible full copy prevents read-demand narrowing");
}

void check_partial_sensitivity_does_not_prove_value_demand()
{
    Process process;
    set_program(process, {
        ReadSignal { 0U, 0U }, Extract { 1U, 0U, 3U, 5U },
        UnaryNot { 2U, 0U }, Halt { },
    }, { { 0U, EdgeKind::any, 3U, 5U } });
    const auto graph = build_read_graph(process);
    require(graph.signals()[0U].readers
            == std::vector<RegionAccess> { { 0U, 0U, 0U, EdgeKind::any } },
        "a partial sensitivity cannot narrow an independently full value use");
}

void check_alias_range_projection()
{
    Process process;
    set_program(process, {
        ReadSignal { 0U, 0U }, Extract { 1U, 0U, 6U, 4U }, Halt { },
    });
    const std::array<const Process*, 1U> processes { &process };
    const std::array<RegionSignalDescriptor, 3U> signals {
        RegionSignalDescriptor { 16U },
        RegionSignalDescriptor { 8U },
        RegionSignalDescriptor { 8U },
    };
    const std::array<RegionContainerDescriptor, 1U> containers {
        RegionContainerDescriptor { 0U, {
            { 0U, true, true, true },
            { 1U, true, true, false },
            { 2U, true, true, false },
        }, true },
    };
    const std::array<RegionSignalAliasFamilyDescriptor, 1U> aliases {
        RegionSignalAliasFamilyDescriptor { 0U, 0U, 16U,
            { { 1U, 0U, 0U, 8U }, { 2U, 1U, 8U, 8U } },
            true, true, true },
    };
    const auto graph = RegionGraph::build(
        processes, signals, false, containers, { }, aliases);
    require(graph.signals()[0U].readers
                == std::vector<RegionAccess> { { 0U, 6U, 4U, EdgeKind::any } }
            && graph.signals()[1U].readers
                == std::vector<RegionAccess> { { 0U, 6U, 2U, EdgeKind::any } }
            && graph.signals()[2U].readers
                == std::vector<RegionAccess> { { 0U, 0U, 2U, EdgeKind::any } },
        "a precise proxy read projects exactly into both intersected leaves");
}

void check_shared_operation_signal_mapping_and_override()
{
    Process representative;
    set_program(representative, {
        ReadSignal { 0U, 0U }, Extract { 1U, 0U, 3U, 5U }, Halt { },
    });
    Process instance;
    set_program(instance, {
        ReadSignal { 0U, 1U }, Extract { 1U, 0U, 3U, 5U }, Halt { },
    });
    const std::array<Signal, 2U> signals {
        Signal { "canonical", PackedLogic4(16U, Logic4::zero) },
        Signal { "instance", PackedLogic4(16U, Logic4::zero) },
    };
    require(share_process_operations(representative, instance, signals),
        "read-range witness shares a body with instance signal remapping");
    instance.operations.replace(1U, Extract { 1U, 0U, 7U, 3U });
    require(instance.operations.shares_body_with(representative.operations),
        "the instance Extract override retains the shared body");
    const auto graph = build_read_graph(instance);
    require(graph.signals()[0U].readers.empty()
            && graph.signals()[1U].readers
                == std::vector<RegionAccess> { { 0U, 7U, 3U, EdgeKind::any } }
            && instance.operations.shares_body_with(representative.operations),
        "read-demand analysis respects overrides and signal maps without expanding storage");
}

} // namespace

int main()
{
    check_static_extract_ranges();
    check_ambiguous_reads_stay_whole();
    check_explicit_sensitivity_is_preserved();
    check_wait_operands_keep_reads_whole();
    check_read_metadata_and_debug_exposure();
    check_partial_sensitivity_does_not_prove_value_demand();
    check_alias_range_projection();
    check_shared_operation_signal_mapping_and_override();
}
