// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/simir_region_graph.hpp"
#include "simir_region_graph_bindings.hpp"
#include "simir_region_graph_driver_class.hpp"
#include "simir_region_graph_program_access.hpp"

#include <unordered_map>
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <typeindex>
#include <typeinfo>
#include <type_traits>
#include <utility>

namespace fsim::runtime::simir {
namespace {

void require_signal(SignalId signal, std::size_t count)
{
    if (signal >= count) {
        throw std::invalid_argument("RegionGraph references a missing signal");
    }
}

RegionUpdateKind update_kind(SignalUpdateDomain domain)
{
    switch (domain) {
    case SignalUpdateDomain::generic:
        return RegionUpdateKind::generic;
    case SignalUpdateDomain::systemverilog_active:
        return RegionUpdateKind::systemverilog_active;
    case SignalUpdateDomain::systemverilog_nba:
        return RegionUpdateKind::systemverilog_nba;
    }
    return RegionUpdateKind::mixed_or_unknown;
}

bool plain_register_operation(const Operation& operation)
{
    return visit_operation([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, DebugPoint>
            || std::is_same_v<T, LoadConstant>
            || std::is_same_v<T, CopyRegister>
            || std::is_same_v<T, Shift>
            || std::is_same_v<T, Extract>
            || std::is_same_v<T, Concatenate>
            || std::is_same_v<T, ConditionalSelect>
            || std::is_same_v<T, UnaryNot>) {
            return true;
        } else if constexpr (std::is_same_v<T, Binary>) {
            return value.operation == BinaryOperator::bit_and
                || value.operation == BinaryOperator::bit_or
                || value.operation == BinaryOperator::bit_xor
                || value.operation == BinaryOperator::add_unsigned;
        } else if constexpr (std::is_same_v<T, Reduction>) {
            return value.operation == ReductionOperator::bit_and
                || value.operation == ReductionOperator::bit_or
                || value.operation == ReductionOperator::bit_xor;
        }
        return false;
    }, operation);
}

enum class ReadRegisterDefinitionKind : std::uint8_t {
    read_signal,
    copy_register,
    other,
};

enum class ReadRegisterUseKind : std::uint8_t {
    extract,
    copy_register,
    other,
};

struct ReadRegisterDefinition {
    std::size_t instruction { };
    ReadRegisterDefinitionKind kind { ReadRegisterDefinitionKind::other };
};

struct ReadRegisterUse {
    std::size_t instruction { };
    RegisterId destination { };
    ReadRegisterUseKind kind { ReadRegisterUseKind::other };
    std::uint32_t offset { };
    std::uint32_t width { };
};

struct StaticReadRange {
    std::uint32_t offset { };
    std::uint32_t width { };
};

struct ReadRegisterFlow {
    std::map<RegisterId, std::vector<ReadRegisterDefinition>> definitions;
    std::map<RegisterId, std::vector<ReadRegisterUse>> uses;
    std::map<std::size_t, ReadSignal> reads;
    bool complete { true };
};

ReadRegisterFlow build_read_register_flow(const OperationList& operations)
{
    ReadRegisterFlow flow;
    for (std::size_t instruction = 0U;
         instruction < operations.size(); ++instruction) {
        const auto& operation = operations.at(instruction);
        const auto define = [&](const RegisterId destination,
                                const ReadRegisterDefinitionKind kind
                                    = ReadRegisterDefinitionKind::other) {
            flow.definitions[destination].push_back({ instruction, kind });
        };
        const auto use = [&](const RegisterId source,
                             const ReadRegisterUseKind kind
                                 = ReadRegisterUseKind::other,
                             const RegisterId destination = 0U,
                             const std::uint32_t offset = 0U,
                             const std::uint32_t width = 0U) {
            flow.uses[source].push_back(
                { instruction, destination, kind, offset, width });
        };
        const auto note_register_effects = [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, ReadSignal>) {
                flow.reads.emplace(instruction, value);
                define(value.destination,
                    ReadRegisterDefinitionKind::read_signal);
            } else if constexpr (std::is_same_v<T, LoadConstant>) {
                define(value.destination);
            } else if constexpr (std::is_same_v<T, CopyRegister>) {
                define(value.destination,
                    ReadRegisterDefinitionKind::copy_register);
                use(value.source, ReadRegisterUseKind::copy_register,
                    value.destination);
            } else if constexpr (std::is_same_v<T, Extract>) {
                define(value.destination);
                use(value.source, ReadRegisterUseKind::extract,
                    value.destination, value.offset, value.width);
            } else if constexpr (std::is_same_v<T, DynamicExtract>) {
                define(value.destination);
                use(value.source);
                use(value.selection.index);
            } else if constexpr (std::is_same_v<T, DynamicPartSelect>) {
                define(value.destination);
                use(value.source);
                use(value.base);
            } else if constexpr (std::is_same_v<T, Concatenate>) {
                define(value.destination);
                for (const auto source : value.operands) {
                    use(source);
                }
            } else if constexpr (std::is_same_v<T, UnaryNot>
                || std::is_same_v<T, LogicalNot>
                || std::is_same_v<T, ConvertToTwoState>
                || std::is_same_v<T, Reduction>
                || std::is_same_v<T, CountOnes>
                || std::is_same_v<T, CountBits>
                || std::is_same_v<T, IntegerUnary>) {
                define(value.destination);
                use(value.source);
            } else if constexpr (std::is_same_v<T, IntegerCheck>) {
                use(value.source);
            } else if constexpr (std::is_same_v<T, Shift>) {
                define(value.destination);
                use(value.value);
                use(value.amount);
            } else if constexpr (std::is_same_v<T, Binary>
                || std::is_same_v<T, LogicalBinary>
                || std::is_same_v<T, IntegerBinary>
                || std::is_same_v<T, SystemVerilogScalarBinary>) {
                define(value.destination);
                use(value.lhs);
                use(value.rhs);
            } else if constexpr (std::is_same_v<T, SystemVerilogMath>) {
                define(value.destination);
                use(value.first);
                use(value.second);
            } else if constexpr (std::is_same_v<T, ConditionalSelect>) {
                define(value.destination);
                use(value.condition);
                use(value.when_true);
                use(value.when_false);
            } else if constexpr (std::is_same_v<T, Insert>) {
                define(value.destination);
                use(value.target);
                use(value.source);
            } else if constexpr (std::is_same_v<T, DynamicInsert>) {
                define(value.destination);
                use(value.target);
                use(value.source);
                use(value.selection.index);
            } else if constexpr (std::is_same_v<T, DynamicPartInsert>) {
                define(value.destination);
                use(value.target);
                use(value.source);
                use(value.selection.base);
            } else if constexpr (std::is_same_v<T, WriteBlocking>
                || std::is_same_v<T, WriteUpdate>
                || std::is_same_v<T, WriteAfter>
                || std::is_same_v<T, WriteInertial>
                || std::is_same_v<T, WriteProjected>) {
                use(value.source);
            } else if constexpr (std::is_same_v<T, WriteDelayed>) {
                use(value.source);
                if (value.delay.source) {
                    use(*value.delay.source);
                }
            } else if constexpr (std::is_same_v<T, WriteBlockingSlice>
                || std::is_same_v<T, WriteUpdateSlice>
                || std::is_same_v<T, WriteAfterSlice>
                || std::is_same_v<T, WriteInertialSlice>
                || std::is_same_v<T, WriteProjectedSlice>) {
                use(value.source);
            } else if constexpr (std::is_same_v<T, WriteBlockingDynamicSlice>
                || std::is_same_v<T, WriteUpdateDynamicSlice>
                || std::is_same_v<T, WriteAfterDynamicSlice>
                || std::is_same_v<T, WriteInertialDynamicSlice>
                || std::is_same_v<T, WriteProjectedDynamicSlice>) {
                use(value.source);
                use(value.selection.index);
            } else if constexpr (std::is_same_v<T,
                                 WriteBlockingDynamicPartSlice>
                || std::is_same_v<T, WriteUpdateDynamicPartSlice>
                || std::is_same_v<T, WriteAfterDynamicPartSlice>
                || std::is_same_v<T, WriteInertialDynamicPartSlice>) {
                use(value.source);
                use(value.selection.base);
            } else if constexpr (std::is_same_v<T, SignalEvent>
                || std::is_same_v<T, SignalLastValue>
                || std::is_same_v<T, SignalLastEvent>
                || std::is_same_v<T, SignalActive>
                || std::is_same_v<T, SignalLastActive>
                || std::is_same_v<T, SignalDriving>
                || std::is_same_v<T, SignalDrivingValue>
                || std::is_same_v<T, ReadSimulationTime>) {
                define(value.destination);
            } else if constexpr (std::is_same_v<T, Branch>) {
                use(value.condition);
            } else if constexpr (std::is_same_v<T, DebugPoint>
                || std::is_same_v<T, Jump>
                || std::is_same_v<T, WaitRegion>
                || std::is_same_v<T, WaitSensitivity>
                || std::is_same_v<T, WaitForever>
                || std::is_same_v<T, Yield>
                || std::is_same_v<T, Halt>
                || std::is_same_v<T, Pause>
                || std::is_same_v<T, Stop>) {
                // These operations carry no packed-register operands.
            } else if constexpr (std::is_same_v<T, WaitFor>
                || std::is_same_v<T, WaitOn>
                || std::is_same_v<T, WaitOrder>) {
                // These wait forms have optional or required register uses
                // and results. Until those fields are modeled, fail closed.
                flow.complete = false;
            } else {
                flow.complete = false;
            }
        };
        visit_operation(note_register_effects, operation);
    }
    return flow;
}

std::map<std::size_t, std::vector<StaticReadRange>>
infer_static_read_ranges(
    const ProcessProgramView& program,
    const std::span<const RegionSignalDescriptor> descriptors)
{
    const auto& operations = program.operations();
    std::map<std::size_t, std::vector<StaticReadRange>> ranges;
    const auto flow = build_read_register_flow(operations);
    if (!flow.complete) {
        return ranges;
    }

    for (const auto& [instruction, read] : flow.reads) {
        if (read.kind != SignalReadKind::current || read.ticks != 1U
            || read.clock || read.gate) {
            continue;
        }
        const auto definition = flow.definitions.find(read.destination);
        if (definition == flow.definitions.end()
            || definition->second.size() != 1U
            || definition->second.front().instruction != instruction
            || definition->second.front().kind
                != ReadRegisterDefinitionKind::read_signal) {
            continue;
        }

        const auto signal = operations.signal(read.signal);
        if (signal >= descriptors.size()) {
            continue;
        }
        std::vector<RegisterId> pending { read.destination };
        std::vector<RegisterId> visited;
        std::vector<StaticReadRange> candidate_ranges;
        bool exact = true;
        while (!pending.empty() && exact) {
            const auto source = pending.back();
            pending.pop_back();
            if (std::ranges::find(visited, source) != visited.end()) {
                exact = false;
                break;
            }
            visited.push_back(source);

            const auto uses = flow.uses.find(source);
            if (uses == flow.uses.end()) {
                continue;
            }
            for (const auto& use : uses->second) {
                if (use.kind == ReadRegisterUseKind::extract) {
                    const auto source_width = descriptors[signal].width;
                    if (use.width == 0U || use.offset >= source_width
                        || use.width > source_width - use.offset) {
                        exact = false;
                        break;
                    }
                    candidate_ranges.push_back({ use.offset, use.width });
                    continue;
                }
                if (use.kind != ReadRegisterUseKind::copy_register) {
                    exact = false;
                    break;
                }
                const auto destination
                    = flow.definitions.find(use.destination);
                if (destination == flow.definitions.end()
                    || destination->second.size() != 1U
                    || destination->second.front().instruction
                        != use.instruction
                    || destination->second.front().kind
                        != ReadRegisterDefinitionKind::copy_register) {
                    exact = false;
                    break;
                }
                pending.push_back(use.destination);
            }
        }
        if (!exact || candidate_ranges.empty()) {
            continue;
        }
        if (std::ranges::any_of(program.debug_locals(), [&](const auto& local) {
                return std::ranges::find(visited, local.register_id)
                    != visited.end();
            })) {
            continue;
        }
        ranges.emplace(instruction, std::move(candidate_ranges));
    }
    return ranges;
}

// Access completeness is independent from the smaller pure-execution subset.
// The default stays opaque so a newly added operation cannot silently become
// proof that the whole signal inventory is complete.
template<typename FindContainer, typename Observe, typename ObserveRead,
    typename RecordWrite, typename RecordContainerWrite>
bool visit_known_region_accesses(
    const Operation& operation,
    FindContainer&& find_container,
    Observe&& observe,
    ObserveRead&& observe_read,
    RecordWrite&& record_write,
    RecordContainerWrite&& record_container_write)
{
    return visit_operation([&](const auto& value) -> bool {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, ReadSignal>) {
            observe_read(value, value.kind == SignalReadKind::current
                    ? RegionObservation::none
                    : RegionObservation::previous | RegionObservation::events);
            if (value.clock) {
                observe(*value.clock, RegionObservation::events);
            }
            if (value.gate) {
                observe(*value.gate, RegionObservation::current);
            }
            return true;
        } else if constexpr (std::is_same_v<T, SignalEvent>) {
            observe(value.signal, RegionObservation::events);
            return true;
        } else if constexpr (std::is_same_v<T, SignalLastValue>) {
            observe(value.signal, RegionObservation::previous);
            return true;
        } else if constexpr (std::is_same_v<T, SignalLastEvent>
            || std::is_same_v<T, SignalActive>
            || std::is_same_v<T, SignalLastActive>) {
            observe(value.signal, RegionObservation::events);
            return true;
        } else if constexpr (std::is_same_v<T, SignalDriving>
            || std::is_same_v<T, SignalDrivingValue>) {
            observe(value.signal, RegionObservation::drivers);
            return true;
        } else if constexpr (std::is_same_v<T, WriteBlocking>
            || std::is_same_v<T, WriteUpdate>
            || std::is_same_v<T, WriteAfter>
            || std::is_same_v<T, WriteInertial>
            || std::is_same_v<T, WriteDelayed>
            || std::is_same_v<T, WriteProjected>
            || std::is_same_v<T, WriteProjectedWaveform>
            || std::is_same_v<T, WriteBlockingSlice>
            || std::is_same_v<T, WriteUpdateSlice>
            || std::is_same_v<T, WriteAfterSlice>
            || std::is_same_v<T, WriteInertialSlice>
            || std::is_same_v<T, WriteProjectedSlice>
            || std::is_same_v<T, WriteProjectedWaveformSlice>
            || std::is_same_v<T, WriteBlockingDynamicSlice>
            || std::is_same_v<T, WriteUpdateDynamicSlice>
            || std::is_same_v<T, WriteAfterDynamicSlice>
            || std::is_same_v<T, WriteInertialDynamicSlice>
            || std::is_same_v<T, WriteInertialDynamicPartSlice>
            || std::is_same_v<T, WriteProjectedDynamicSlice>
            || std::is_same_v<T, WriteProjectedWaveformDynamicSlice>
            || std::is_same_v<T, WriteBlockingDynamicPartSlice>
            || std::is_same_v<T, WriteUpdateDynamicPartSlice>
            || std::is_same_v<T, WriteAfterDynamicPartSlice>) {
            record_write(value.signal);
            return true;
        } else if constexpr (std::is_same_v<T, ForceSignalSlice>
            || std::is_same_v<T, ReleaseSignalSlice>) {
            observe(value.signal, RegionObservation::mutation);
            return true;
        } else if constexpr (std::is_same_v<T, WaitOn>) {
            for (const auto signal : value.signals) {
                observe(signal, RegionObservation::events);
            }
            return true;
        } else if constexpr (std::is_same_v<T, WaitPla>) {
            for (const auto signal : value.signals) {
                observe(signal, RegionObservation::events);
            }
            // The selected personality memory is outside the signal graph.
            return false;
        } else if constexpr (std::is_same_v<T, WaitOrder>) {
            for (const auto signal : value.events) {
                observe(signal, RegionObservation::events);
            }
            return true;
        } else if constexpr (std::is_same_v<T, EventAlias>) {
            observe(value.target,
                RegionObservation::events | RegionObservation::mutation);
            if (value.has_source) {
                observe(value.source, RegionObservation::events);
            }
            return true;
        } else if constexpr (std::is_same_v<T, EventTriggered>) {
            observe(value.event, RegionObservation::events);
            return true;
        } else if constexpr (std::is_same_v<T, MonitorInstall>) {
            for (const auto& item : value.values) {
                if (item.kind == MonitorValueKind::signal) {
                    observe(item.signal, RegionObservation::current);
                }
            }
            return true;
        } else if constexpr (std::is_same_v<T, VitalTimingCheck>) {
            observe(value.test_signal, RegionObservation::events);
            observe(value.reference_signal.value_or(value.test_signal),
                RegionObservation::events);
            if (value.trigger_signal) {
                observe(*value.trigger_signal, RegionObservation::events);
                record_write(*value.trigger_signal);
            }
            return true;
        } else if constexpr (std::is_same_v<T, VitalDelay>) {
            record_write(value.output);
            return true;
        } else if constexpr (std::is_same_v<T, ReadContainerObject>) {
            const auto* descriptor = find_container(value.object);
            if (descriptor == nullptr) {
                return false;
            }
            for (const auto& binding : descriptor->bindings) {
                if (binding.readable) {
                    observe(binding.signal, RegionObservation::none);
                }
            }
            return descriptor->complete;
        } else if constexpr (std::is_same_v<T, WriteContainerObject>) {
            const auto* descriptor = find_container(value.object);
            if (descriptor != nullptr) {
                for (const auto& binding : descriptor->bindings) {
                    if (binding.writable) {
                        record_container_write(binding);
                    }
                }
            }
            if (value.transaction_signal) {
                record_write(*value.transaction_signal);
            }
            return descriptor != nullptr && descriptor->complete;
        } else if constexpr (std::is_same_v<T, WriteContainerObjectElement>) {
            const auto* descriptor = find_container(value.object);
            if (descriptor != nullptr) {
                for (const auto& binding : descriptor->bindings) {
                    if (binding.writable) {
                        record_container_write(binding);
                    }
                }
            }
            if (value.transaction_signal) {
                record_write(*value.transaction_signal);
            }
            return descriptor != nullptr && descriptor->complete;
        } else if constexpr (std::is_same_v<T, LoadConstant>
            || std::is_same_v<T, CopyRegister>
            || std::is_same_v<T, ConvertToTwoState>
            || std::is_same_v<T, UnaryNot>
            || std::is_same_v<T, LogicalNot>
            || std::is_same_v<T, LogicalBinary>
            || std::is_same_v<T, Reduction>
            || std::is_same_v<T, CountOnes>
            || std::is_same_v<T, CountBits>
            || std::is_same_v<T, Shift>
            || std::is_same_v<T, Extract>
            || std::is_same_v<T, DynamicExtract>
            || std::is_same_v<T, DynamicPartSelect>
            || std::is_same_v<T, Concatenate>
            || std::is_same_v<T, Binary>
            || std::is_same_v<T, Insert>
            || std::is_same_v<T, DynamicInsert>
            || std::is_same_v<T, DynamicPartInsert>
            || std::is_same_v<T, IntegerUnary>
            || std::is_same_v<T, IntegerBinary>
            || std::is_same_v<T, IntegerCheck>
            || std::is_same_v<T, SystemVerilogScalarBinary>
            || std::is_same_v<T, SystemVerilogMath>
            || std::is_same_v<T, ConditionalSelect>
            || std::is_same_v<T, ReadSimulationTime>
            // Register-only: draws from the process's own generator state and
            // optional bound registers; it has no signal access.
            || std::is_same_v<T, RandomValue>
            || std::is_same_v<T, DebugPoint>
            || std::is_same_v<T, Jump>
            || std::is_same_v<T, Branch>
            || std::is_same_v<T, WaitFor>
            || std::is_same_v<T, WaitRegion>
            || std::is_same_v<T, WaitSensitivity>
            || std::is_same_v<T, WaitForever>
            || std::is_same_v<T, Yield>
            || std::is_same_v<T, Halt>
            || std::is_same_v<T, Pause>
            || std::is_same_v<T, Stop>) {
            return true;
        } else if constexpr (std::is_same_v<T, ResizeContainer>
            || std::is_same_v<T, CopyContainerRegister>
            || std::is_same_v<T, ConditionalContainerSelect>
            || std::is_same_v<T, CompareContainers>
            || std::is_same_v<T, ContainerSize>
            || std::is_same_v<T, ContainerReduction>
            || std::is_same_v<T, OrderContainer>
            || std::is_same_v<T, LocateContainer>
            || std::is_same_v<T, ContainerRead>
            || std::is_same_v<T, ContainerWrite>
            || std::is_same_v<T, ContainerStringRead>
            || std::is_same_v<T, ContainerStringWrite>
            || std::is_same_v<T, FormatContainerPattern>
            || std::is_same_v<T, ContainerElementRead>
            || std::is_same_v<T, ContainerElementWrite>
            || std::is_same_v<T, ContainerAggregateRead>
            || std::is_same_v<T, ContainerAggregateWrite>
            || std::is_same_v<T, CopyContainerAggregateElement>
            || std::is_same_v<T, DeleteContainer>
            || std::is_same_v<T, ContainerExists>
            || std::is_same_v<T, TraverseContainer>
            || std::is_same_v<T, PushContainer>
            || std::is_same_v<T, AppendContainer>
            || std::is_same_v<T, PopContainer>) {
            // These operations address only this process's container-register
            // file. Object-ID operations remain opaque because an object can
            // be bound to signals through ContainerSignalAlias.
            return true;
        } else if constexpr (std::is_same_v<T, LoadStringConstant>
            || std::is_same_v<T, CopyStringRegister>
            || std::is_same_v<T, ConcatenateStrings>
            || std::is_same_v<T, CompareStrings>
            || std::is_same_v<T, StringLength>
            || std::is_same_v<T, StringIndex>
            || std::is_same_v<T, StringReplaceByte>
            || std::is_same_v<T, StringMethod>
            || std::is_same_v<T, PlusArgSelect>) {
            // These operations use process-local registers, plus PlusArgSelect's
            // interpreter-owned command-line snapshot. Shared StringObjectId
            // access remains opaque.
            return true;
        } else if constexpr (std::is_same_v<T, Call>
            || std::is_same_v<T, Return>
            || std::is_same_v<T, CallableFramePush>
            || std::is_same_v<T, CallableFramePop>) {
            // Both fixed-register and runtime-owned call stacks are local to
            // this process. The complete operation stream is scanned, so a
            // call's statically addressed body contributes its own accesses.
            return true;
        } else if constexpr (std::is_same_v<T, Fork>
            || std::is_same_v<T, ForkEnd>
            || std::is_same_v<T, WaitFork>
            || std::is_same_v<T, DisableFork>
            || std::is_same_v<T, DisableBlock>) {
            // These operations change only the dynamic process/control graph.
            // Child instruction streams are cloned from this same immutable
            // program, whose fixed signal accesses are inventoried separately.
            // Fork-written signal ownership is marked uncertain by the graph
            // builder after its writer list has been assembled.
            return true;
        } else if constexpr (std::is_same_v<T, Assert>
            || std::is_same_v<T, Display>
            || std::is_same_v<T, FormatDisplay>
            || std::is_same_v<T, StringDisplay>
            || std::is_same_v<T, TimeDisplay>
            || std::is_same_v<T, Report>
            || std::is_same_v<T, StringReport>) {
            // Operands are process-local registers, retained text, or time.
            // The arbitrary output/report callback is guarded at its runtime
            // invocation boundary, where every signal is materialized and
            // dependent certificates are invalidated. These operations stay
            // non-pure; they simply do not make signal-ID inventory opaque.
            return true;
        }
        return false;
    }, operation);
}

bool fork_control_access_is_known(const Fork& fork,
    const std::size_t instruction, const std::size_t operation_count)
{
    if ((fork.join != ForkJoinKind::all
            && fork.join != ForkJoinKind::any
            && fork.join != ForkJoinKind::none)
        || instruction + 1U >= operation_count) {
        return false;
    }
    for (std::size_t branch_index = 0U;
         branch_index < fork.branches.size(); ++branch_index) {
        const auto branch = fork.branches[branch_index];
        const auto earlier_branches_end = fork.branches.begin()
            + static_cast<std::ptrdiff_t>(branch_index);
        if (branch >= operation_count || branch <= instruction + 1U
            || std::ranges::find(
                   fork.branches.begin(), earlier_branches_end, branch)
                != earlier_branches_end) {
            return false;
        }
    }
    return true;
}

std::string static_loop_failure(const ProcessProgramView& process)
{
    const auto& operations = process.operations();
    if (operations.size() < 3U) return "size_lt3";
    if (process.static_sensitivity().empty()) return "no_static_sensitivity";
    if (process.final() || process.observed() || process.reactive()
        || process.postponed()) return "final_observed_reactive_postponed";
    if (process.switch_source() || process.switch_target()
        || process.switch_control() || process.switch_bidirectional()
        || process.switch_resistive()) return "switch";
    if (process.string_register_count() != 0U) return "string_registers";
    if (process.container_register_count() != 0U) return "container_registers";
    if (!process.debug_locals().empty() || !process.debug_string_locals().empty()
        || !process.debug_container_locals().empty()) return "debug_locals";
    if (!process.static_trigger_regions().empty()) return "static_trigger_regions";
    const auto count = operations.size();
    const auto tail = operations.expanded(count - 1U);
    const auto* jump = operation_get_if<Jump>(&tail);
    if (jump == nullptr || jump->target != 0U) return "no_tail_jump";
    if (!operation_holds<WaitSensitivity>(operations.expanded(count - 2U)))
        return "no_wait_sensitivity";
    return "ok";
}

bool static_loop(const ProcessProgramView& process)
{
    const auto& operations = process.operations();
    if (operations.size() < 3U
        || process.static_sensitivity().empty()
        || process.final() || process.observed() || process.reactive()
        || process.postponed() || process.switch_source()
        || process.switch_target() || process.switch_control()
        || process.switch_bidirectional() || process.switch_resistive()
        || process.string_register_count() != 0U
        || process.container_register_count() != 0U
        || !process.debug_locals().empty()
        || !process.debug_string_locals().empty()
        || !process.debug_container_locals().empty()
        || !process.static_trigger_regions().empty()) {
        return false;
    }
    const auto count = operations.size();
    const auto tail = operations.expanded(count - 1U);
    const auto* jump = operation_get_if<Jump>(&tail);
    return jump != nullptr && jump->target == 0U
        && operation_holds<WaitSensitivity>(operations.expanded(count - 2U));
}

template<typename FindContainer>
bool ownership_matches_operations(const ProcessProgramView& process,
    std::span<const RegionSignalDescriptor> signals,
    FindContainer&& find_container,
    bool& operation_write_ranges_exact)
{
    const auto& operations = process.operations();
    const auto& driver_regions = process.driver_regions();
    bool exact_ranges = true;
    std::vector<std::uint32_t> widths(process.register_count(), 0U);
    std::vector<bool> branch_entries(operations.size(), false);
    for (std::size_t index = 0; index < operations.size(); ++index) {
        const auto& operation = operations.at(index);
        if (const auto* fork = operation_get_if<Fork>(&operation)) {
            for (const auto branch : fork->branches) {
                if (branch >= branch_entries.size()) {
                    return false;
                }
                branch_entries[branch] = true;
            }
        }
    }
    const auto forget_widths = [&] {
        std::ranges::fill(widths, 0U);
        return true;
    };
    const auto width_of = [&](RegisterId id) {
        return id < widths.size() ? widths[id] : 0U;
    };
    // In flow-insensitive mode a register's width is the single width every
    // definition agrees on; a conflicting definition makes it unknown.
    bool flow_insensitive = false;
    bool widths_changed = false;
    std::vector<std::uint8_t> width_conflicts;
    const auto assign_width = [&](RegisterId id, std::uint64_t width) {
        if (id >= widths.size() || width == 0U
            || width > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        const auto narrowed = static_cast<std::uint32_t>(width);
        if (flow_insensitive) {
            if (width_conflicts[id] != 0U) {
                return true;
            }
            if (widths[id] != 0U && widths[id] != narrowed) {
                width_conflicts[id] = 1U;
                widths[id] = 0U;
                widths_changed = true;
                return true;
            }
            if (widths[id] != narrowed) {
                widths[id] = narrowed;
                widths_changed = true;
            }
            return true;
        }
        widths[id] = narrowed;
        return true;
    };
    const auto covered = [&](SignalId signal, std::uint32_t offset,
                             std::uint32_t width, bool whole) {
        if (signal >= signals.size() || width == 0U
            || offset >= signals[signal].width
            || width > signals[signal].width - offset
            || (whole && width != signals[signal].width)) {
            return false;
        }
        return std::ranges::any_of(driver_regions, [&](const auto& owner) {
            return owner.signal == signal && (owner.whole
                || (owner.offset <= offset
                    && std::uint64_t { offset } + width
                        <= std::uint64_t { owner.offset } + owner.width));
        });
    };
    const auto exact_driver_range = [&](const SignalId signal,
                                        const std::uint32_t offset,
                                        const std::uint32_t width) {
        if (signal >= signals.size() || width == 0U) {
            return false;
        }
        std::size_t exact_regions { };
        for (const auto& owner : driver_regions) {
            if (owner.signal != signal) {
                continue;
            }
            const bool exact = owner.whole
                ? offset == 0U && width == signals[signal].width
                : owner.offset == offset && owner.width == width;
            exact_regions += exact;
        }
        return exact_regions == 1U;
    };
    const auto step = [&](const std::size_t index) -> bool {
        const auto& stored_operation = operations.at(index);
        if (operation_holds<DebugPoint>(stored_operation)) {
            // Source markers do not define or consume register widths.
            return true;
        }
        const auto operation = operations.expanded(index);
        return visit_operation([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, LoadConstant>) {
                return assign_width(value.destination, value.value.width());
            } else if constexpr (std::is_same_v<T, ReadSignal>) {
                return value.signal < signals.size()
                    && assign_width(value.destination, signals[value.signal].width);
            } else if constexpr (std::is_same_v<T, CopyRegister>
                || std::is_same_v<T, UnaryNot>) {
                return assign_width(value.destination, width_of(value.source));
            } else if constexpr (std::is_same_v<T, Shift>) {
                const auto value_width = width_of(value.value);
                const auto amount_width = width_of(value.amount);
                const auto valid_operation
                    = value.operation == ShiftOperator::logical_left
                    || value.operation == ShiftOperator::logical_right
                    || value.operation == ShiftOperator::arithmetic_right
                    || value.operation == ShiftOperator::arithmetic_left
                    || value.operation == ShiftOperator::rotate_left
                    || value.operation == ShiftOperator::rotate_right;
                return valid_operation && value_width != 0U
                    && amount_width != 0U
                    && assign_width(value.destination, value_width);
            } else if constexpr (std::is_same_v<T, Binary>) {
                const auto left = width_of(value.lhs);
                const auto right = width_of(value.rhs);
                const bool comparison
                    = value.operation == BinaryOperator::equal
                    || value.operation == BinaryOperator::case_equal
                    || value.operation == BinaryOperator::casez_equal
                    || value.operation == BinaryOperator::casex_equal
                    || value.operation == BinaryOperator::wildcard_equal
                    || value.operation == BinaryOperator::not_equal
                    || value.operation == BinaryOperator::less_unsigned
                    || value.operation == BinaryOperator::less_equal_unsigned
                    || value.operation == BinaryOperator::greater_unsigned
                    || value.operation
                        == BinaryOperator::greater_equal_unsigned
                    || value.operation == BinaryOperator::less_signed
                    || value.operation == BinaryOperator::less_equal_signed
                    || value.operation == BinaryOperator::greater_signed
                    || value.operation == BinaryOperator::greater_equal_signed
                    || value.operation == BinaryOperator::vhdl_match_equal;
                return left != 0U && right != 0U
                    && assign_width(value.destination,
                        comparison ? 1U : std::max(left, right));
            } else if constexpr (std::is_same_v<T, LogicalNot>) {
                return width_of(value.source) != 0U
                    && assign_width(value.destination, 1U);
            } else if constexpr (std::is_same_v<T, LogicalBinary>) {
                return width_of(value.lhs) != 0U && width_of(value.rhs) != 0U
                    && assign_width(value.destination, 1U);
            } else if constexpr (std::is_same_v<T, ConvertToTwoState>) {
                return assign_width(value.destination, width_of(value.source));
            } else if constexpr (std::is_same_v<T, Insert>) {
                const auto target = width_of(value.target);
                const auto source = width_of(value.source);
                return target != 0U && source != 0U
                    && value.offset < target && source <= target - value.offset
                    && assign_width(value.destination, target);
            } else if constexpr (std::is_same_v<T, Branch>) {
                return width_of(value.condition) == 1U;
            } else if constexpr (std::is_same_v<T, ConditionalSelect>) {
                const auto when_true = width_of(value.when_true);
                const auto when_false = width_of(value.when_false);
                const auto destination = width_of(value.destination);
                return width_of(value.condition) == 1U
                    && when_true != 0U && when_true == when_false
                    && (destination == 0U || destination == when_true)
                    && assign_width(value.destination, when_true);
            } else if constexpr (std::is_same_v<T, Reduction>) {
                return width_of(value.source) != 0U
                    && assign_width(value.destination, 1U);
            } else if constexpr (std::is_same_v<T, Extract>) {
                const auto source = width_of(value.source);
                return value.offset < source && value.width <= source - value.offset
                    && assign_width(value.destination, value.width);
            } else if constexpr (std::is_same_v<T, Concatenate>) {
                std::uint64_t total { };
                for (const auto operand : value.operands) {
                    const auto width = width_of(operand);
                    if (width == 0U) {
                        return false;
                    }
                    total += width;
                }
                return total == value.width
                    && assign_width(value.destination, total);
            } else if constexpr (std::is_same_v<T, WriteBlocking>) {
                const auto valid_write = process.scheduling_domain()
                        == ProcessSchedulingDomain::systemverilog
                    && covered(value.signal, 0U, width_of(value.source), true);
                if (valid_write && !exact_driver_range(value.signal, 0U,
                        width_of(value.source))) {
                    exact_ranges = false;
                }
                return valid_write;
            } else if constexpr (std::is_same_v<T, WriteUpdate>
                || std::is_same_v<T, WriteProjected>) {
                const auto width = width_of(value.source);
                const auto valid_write
                    = covered(value.signal, 0U, width, true);
                if (valid_write
                    && !exact_driver_range(value.signal, 0U, width)) {
                    exact_ranges = false;
                }
                return valid_write;
            } else if constexpr (std::is_same_v<T, WriteUpdateSlice>
                || std::is_same_v<T, WriteProjectedSlice>) {
                const auto width = width_of(value.source);
                const auto valid_write = covered(value.signal, value.offset,
                    width, false);
                if (valid_write && !exact_driver_range(value.signal,
                        value.offset, width)) {
                    exact_ranges = false;
                }
                return valid_write;
            } else if constexpr (std::is_same_v<T, ReadContainerObject>) {
                const auto* container = find_container(value.object);
                return container != nullptr && container->complete;
            } else if constexpr (std::is_same_v<T, WriteContainerObject>
                || std::is_same_v<T, WriteContainerObjectElement>) {
                exact_ranges = false;
                const auto* container = find_container(value.object);
                if (container == nullptr || !container->complete) {
                    return false;
                }
                for (const auto& binding : container->bindings) {
                    if (binding.writable
                        && !binding.indirect_write
                        && !covered(binding.signal, 0U,
                            signals[binding.signal].width, true)) {
                        return false;
                    }
                }
                if (value.transaction_signal
                    && !std::ranges::any_of(driver_regions,
                        [&](const auto& writer) {
                            return writer.signal == *value.transaction_signal;
                        })) {
                    return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, WaitSensitivity>) {
                return static_loop(process) && index + 2U == operations.size();
            } else if constexpr (std::is_same_v<T, Jump>) {
                return (static_loop(process) && index + 1U == operations.size()
                           && value.target == 0U)
                    || (value.target > index
                        && value.target < operations.size());
            } else if constexpr (std::is_same_v<T, Fork>
                || std::is_same_v<T, ForkEnd>
                || std::is_same_v<T, WaitFork>
                || std::is_same_v<T, DisableFork>
                || std::is_same_v<T, DisableBlock>
                || std::is_same_v<T, WaitFor>
                || std::is_same_v<T, Halt>) {
                // Forks share lexical registers. Prove writes only from
                // definitions in the same uninterrupted straight-line block;
                // never carry a width across a branch or suspension boundary.
                // This proves ownership, not eligibility for pure execution.
                return forget_widths();
            } else {
                return std::is_same_v<T, DebugPoint>;
            }
        }, operation);
    };
    const bool has_fork = std::ranges::any_of(branch_entries,
        [](const bool entry) { return entry; });
    const auto final_loop_jump = [&](const std::size_t index) {
        if (index + 1U != operations.size()) {
            return false;
        }
        const auto tail = operations.expanded(index);
        const auto* jump = operation_get_if<Jump>(&tail);
        return jump != nullptr && jump->target == 0U && static_loop(process);
    };
    bool has_control_flow = false;
    for (std::size_t index = 0; index < operations.size(); ++index) {
        const auto& stored = operations.at(index);
        if (operation_holds<Branch>(stored)
            || (operation_holds<Jump>(stored) && !final_loop_jump(index))) {
            has_control_flow = true;
            break;
        }
    }
    if (has_fork || !has_control_flow) {
        for (std::size_t index = 0; index < operations.size(); ++index) {
            if (branch_entries[index]) {
                forget_widths();
            }
            if (!step(index)) {
                return false;
            }
        }
        operation_write_ranges_exact = exact_ranges;
        return true;
    }
    // Forward branches without forks: widths are flow-insensitive. Iterate
    // definitions to a fixpoint, then validate every instruction against the
    // agreed widths. Each pass is linear in the program length.
    for (std::size_t index = 0; index < operations.size(); ++index) {
        const auto operation = operations.expanded(index);
        if (const auto* jump = operation_get_if<Jump>(&operation)) {
            if (!final_loop_jump(index)
                && (jump->target <= index
                    || jump->target >= operations.size())) {
                return false;
            }
        } else if (const auto* branch = operation_get_if<Branch>(&operation)) {
            if (branch->when_true <= index || branch->when_false <= index
                || branch->when_true >= operations.size()
                || branch->when_false >= operations.size()) {
                return false;
            }
        }
    }
    flow_insensitive = true;
    width_conflicts.assign(widths.size(), 0U);
    std::ranges::fill(widths, 0U);
    for (std::size_t pass = 0U; pass <= widths.size(); ++pass) {
        widths_changed = false;
        for (std::size_t index = 0; index < operations.size(); ++index) {
            (void)step(index);
        }
        if (!widths_changed) {
            break;
        }
    }
    exact_ranges = true;
    for (std::size_t index = 0; index < operations.size(); ++index) {
        if (!step(index)) {
            return false;
        }
    }
    operation_write_ranges_exact = exact_ranges;
    return true;
}

void canonicalize_accesses(std::vector<RegionAccess>& accesses)
{
    // Here the normalizer's grouping identity denotes a process, not a signal.
    // Its interval semantics are identical for both halves of the graph.
    std::vector<Sensitivity> intervals;
    intervals.reserve(accesses.size());
    for (const auto& access : accesses) {
        intervals.push_back({ access.process, access.edge, access.offset, access.width });
    }
    normalize_sensitivities(intervals);
    accesses.clear();
    for (const auto& interval : intervals) {
        accesses.push_back({ interval.signal, interval.offset, interval.width, interval.edge });
    }
}

void classify_drivers(RegionSignalNode& signal)
{
    signal.drivers = detail::classify_region_driver_class(signal,
        detail::RegionDriverClassificationMode::runtime_capabilities);
}

template<typename Reason>
constexpr std::size_t reason_index(Reason reason)
{
    return static_cast<std::size_t>(reason);
}

bool has_observation(RegionObservation observations, RegionObservation capability)
{
    return (static_cast<std::uint32_t>(observations)
        & static_cast<std::uint32_t>(capability)) != 0U;
}

constexpr std::array<std::pair<RegionObservation, RegionBoundaryReason>, 8U>
    observation_boundary_reasons {{
        { RegionObservation::current, RegionBoundaryReason::observed_current },
        { RegionObservation::previous, RegionBoundaryReason::observed_previous },
        { RegionObservation::drivers, RegionBoundaryReason::observed_drivers },
        { RegionObservation::pending, RegionBoundaryReason::observed_pending },
        { RegionObservation::events, RegionBoundaryReason::observed_events },
        { RegionObservation::mutation, RegionBoundaryReason::observed_mutation },
        { RegionObservation::coverage, RegionBoundaryReason::observed_coverage },
        { RegionObservation::unknown, RegionBoundaryReason::observed_unknown },
    }};

void count_process_exclusion(RegionCertificateInventory& inventory,
    RegionProcessExclusionReason reason)
{
    ++inventory.process_exclusion_counts[reason_index(reason)];
}

void count_boundary_reason(RegionCertificateInventory& inventory,
    RegionBoundaryReason reason)
{
    ++inventory.boundary_reason_counts[reason_index(reason)];
}

RegionCertificateInventory build_certificate_inventory(
    const std::vector<RegionProcessNode>& processes,
    const std::vector<RegionSignalNode>& signals,
    const std::vector<std::uint64_t>& capability_epochs,
    const std::vector<std::size_t>& signal_alias_family_by_signal)
{
    RegionCertificateInventory inventory;
    inventory.access_inventory_complete = std::ranges::none_of(processes,
        &RegionProcessNode::dependencies_unknown);

    std::vector<bool> candidate_process(processes.size(), false);
    for (std::size_t index = 0U; index < processes.size(); ++index) {
        const auto& process = processes[index];
        if (!process.pure) {
            count_process_exclusion(inventory,
                RegionProcessExclusionReason::not_pure);
        }
        const bool sv_active
            = process.scheduling_domain
                    == ProcessSchedulingDomain::systemverilog
            && process.update_kind == RegionUpdateKind::systemverilog_active;
        const bool vhdl_projected
            = process.scheduling_domain == ProcessSchedulingDomain::generic
            && process.update_kind == RegionUpdateKind::vhdl_projected;
        const bool generic_update
            = process.scheduling_domain == ProcessSchedulingDomain::generic
            && process.update_kind == RegionUpdateKind::generic;
        if (!sv_active && !vhdl_projected && !generic_update) {
            count_process_exclusion(inventory,
                RegionProcessExclusionReason::wrong_scheduling_domain);
        }
        if (!sv_active && !vhdl_projected && !generic_update) {
            count_process_exclusion(inventory,
                RegionProcessExclusionReason::wrong_update_kind);
        }
        if (process.cyclic_or_dependent_on_cycle) {
            count_process_exclusion(inventory,
                RegionProcessExclusionReason::cyclic_or_dependent_on_cycle);
        }
        if (process.dependencies_unknown) {
            count_process_exclusion(inventory,
                RegionProcessExclusionReason::unknown_dependencies);
        }
        if (std::ranges::any_of(process.sensitivities,
                [](const auto& sensitivity) {
                    return sensitivity.edge != EdgeKind::any;
                })) {
            count_process_exclusion(inventory,
                RegionProcessExclusionReason::edge_sensitivity);
        }
        candidate_process[index] = process.pure
            && (sv_active || vhdl_projected || generic_update)
            && !process.cyclic_or_dependent_on_cycle
            && !process.dependencies_unknown
            && std::ranges::none_of(process.sensitivities,
                [](const auto& sensitivity) {
                    return sensitivity.edge != EdgeKind::any;
                });
    }

    struct IndexedAccess {
        const RegionAccess* access { };
        bool writer { };
    };
    struct AccessInterval {
        ProcessId process { };
        std::uint32_t begin { };
        std::uint32_t end { };
    };
    struct WriterCluster {
        std::uint32_t begin { };
        std::uint32_t end { };
        ProcessId representative { };
    };

    std::vector<std::vector<ProcessId>> adjacent(processes.size());
    const auto connect = [&](const ProcessId left, const ProcessId right) {
        if (left == right) {
            return;
        }
        adjacent[left].push_back(right);
        adjacent[right].push_back(left);
    };
    const auto no_alias_family = std::numeric_limits<std::size_t>::max();

    // These synthetic edges preserve certificate connectivity only. The
    // topological dependency graph below still uses every known access row.
    for (SignalId signal_id = 0U; signal_id < signals.size(); ++signal_id) {
        const auto& signal = signals[signal_id];
        const auto& descriptor = signal.descriptor;
        const bool supported_resolution
            = descriptor.resolution == ResolutionKind::none
            || descriptor.resolution == ResolutionKind::sv_wire;
        const bool supported_value_kind
            = descriptor.value_kind == ValueKind::logic4
            || descriptor.value_kind == ValueKind::logic9;
        const bool known_driver_class = [&] {
            switch (signal.drivers) {
            case RegionDriverClass::undriven:
            case RegionDriverClass::single_whole:
            case RegionDriverClass::single_partial:
            case RegionDriverClass::disjoint_partial:
                return true;
            case RegionDriverClass::resolved:
            case RegionDriverClass::unknown:
                return false;
            }
            return false;
        }();
        const bool exact_interval_signal
            = inventory.access_inventory_complete
            && descriptor.width != 0U
            && supported_resolution
            && supported_value_kind
            && known_driver_class
            && !descriptor.implicit_driver
            && !descriptor.external_driver
            && !descriptor.event_variable
            && descriptor.observations == RegionObservation::none
            && signal.observations == RegionObservation::none
            && !signal.writers_unknown
            && !signal.dynamic_fork_writers
            && !signal.partial_projected_transactions
            && signal_id < signal_alias_family_by_signal.size()
            && signal_alias_family_by_signal[signal_id] == no_alias_family;

        std::vector<IndexedAccess> accesses;
        accesses.reserve(signal.readers.size() + signal.writers.size());
        const auto append_accesses = [&](const std::vector<RegionAccess>& rows,
                                         const bool writer) {
            for (const auto& access : rows) {
                if (access.process < candidate_process.size()
                    && candidate_process[access.process]) {
                    accesses.push_back({ &access, writer });
                }
            }
        };
        append_accesses(signal.readers, false);
        append_accesses(signal.writers, true);
        std::ranges::sort(accesses, [&](const IndexedAccess& left,
                                        const IndexedAccess& right) {
            const auto key = [&](const IndexedAccess& indexed) {
                const auto& process = processes[indexed.access->process];
                return std::tuple { process.scheduling_domain,
                    process.update_kind, indexed.access->process,
                    indexed.writer, indexed.access->offset,
                    indexed.access->width, indexed.access->edge };
            };
            return key(left) < key(right);
        });

        for (std::size_t group_begin = 0U;
             group_begin < accesses.size();) {
            const auto& first_access = *accesses[group_begin].access;
            const auto& group_process = processes[first_access.process];
            auto group_end = group_begin + 1U;
            while (group_end < accesses.size()) {
                const auto& access = *accesses[group_end].access;
                const auto& process = processes[access.process];
                if (process.scheduling_domain
                        != group_process.scheduling_domain
                    || process.update_kind != group_process.update_kind) {
                    break;
                }
                ++group_end;
            }

            std::vector<ProcessId> touching;
            bool has_candidate_writer { };
            for (auto index = group_begin; index < group_end; ++index) {
                const auto& indexed = accesses[index];
                const auto process = indexed.access->process;
                if (touching.empty() || touching.back() != process) {
                    touching.push_back(process);
                }
                has_candidate_writer = has_candidate_writer || indexed.writer;
            }
            if (!has_candidate_writer) {
                group_begin = group_end;
                continue;
            }

            const auto make_interval = [&](const IndexedAccess& indexed,
                                           AccessInterval& interval) {
                const auto& access = *indexed.access;
                if (access.edge != EdgeKind::any) {
                    return false;
                }
                interval.process = access.process;
                if (access.width == 0U) {
                    interval.begin = 0U;
                    interval.end = descriptor.width;
                    return true;
                }
                if (access.offset >= descriptor.width
                    || access.width > descriptor.width - access.offset) {
                    return false;
                }
                interval.begin = access.offset;
                interval.end = access.offset + access.width;
                return true;
            };

            if (!exact_interval_signal) {
                // Preserve the conservative prior writer-star for signals
                // whose ownership, shape, or observation contract is broad.
                for (auto index = 1U; index < touching.size(); ++index) {
                    connect(touching.front(), touching[index]);
                }
                group_begin = group_end;
                continue;
            }

            std::vector<AccessInterval> writer_ranges;
            std::vector<AccessInterval> reader_ranges;
            writer_ranges.reserve(group_end - group_begin);
            reader_ranges.reserve(group_end - group_begin);
            bool valid_ranges = true;
            for (auto index = group_begin; index < group_end; ++index) {
                AccessInterval interval;
                if (!make_interval(accesses[index], interval)) {
                    valid_ranges = false;
                    break;
                }
                (accesses[index].writer ? writer_ranges : reader_ranges)
                    .push_back(interval);
            }
            if (!valid_ranges) {
                for (auto index = 1U; index < touching.size(); ++index) {
                    connect(touching.front(), touching[index]);
                }
                group_begin = group_end;
                continue;
            }

            std::ranges::sort(writer_ranges,
                [](const AccessInterval& left, const AccessInterval& right) {
                    return std::tuple { left.begin, left.end, left.process }
                        < std::tuple { right.begin, right.end, right.process };
                });
            std::vector<WriterCluster> clusters;
            clusters.reserve(writer_ranges.size());
            for (const auto& writer : writer_ranges) {
                if (clusters.empty()
                    || writer.begin >= clusters.back().end) {
                    clusters.push_back({ writer.begin, writer.end,
                        writer.process });
                } else {
                    clusters.back().end = std::max(
                        clusters.back().end, writer.end);
                }
            }

            std::size_t cluster_index { };
            for (const auto& writer : writer_ranges) {
                while (cluster_index + 1U < clusters.size()
                    && writer.begin >= clusters[cluster_index].end) {
                    ++cluster_index;
                }
                connect(clusters[cluster_index].representative,
                    writer.process);
            }

            // A contiguous read that reaches multiple writer clusters connects
            // those clusters. Each adjacent cluster gap needs one synthetic
            // edge regardless of how many readers bridge across it.
            std::vector<std::size_t> next_unjoined_gap(clusters.size());
            for (std::size_t gap = 0U;
                 gap < next_unjoined_gap.size(); ++gap) {
                next_unjoined_gap[gap] = gap;
            }
            const auto find_unjoined_gap = [&](std::size_t gap) {
                auto root = gap;
                while (next_unjoined_gap[root] != root) {
                    root = next_unjoined_gap[root];
                }
                while (next_unjoined_gap[gap] != gap) {
                    const auto next = next_unjoined_gap[gap];
                    next_unjoined_gap[gap] = root;
                    gap = next;
                }
                return root;
            };
            for (const auto& reader : reader_ranges) {
                const auto first_cluster = std::upper_bound(
                    clusters.begin(), clusters.end(), reader.begin,
                    [](const std::uint32_t begin,
                       const WriterCluster& cluster) {
                        return begin < cluster.end;
                    });
                const auto past_last_cluster = std::lower_bound(
                    clusters.begin(), clusters.end(), reader.end,
                    [](const WriterCluster& cluster,
                       const std::uint32_t end) {
                        return cluster.begin < end;
                    });
                if (first_cluster == past_last_cluster) {
                    continue;
                }
                const auto first_index = static_cast<std::size_t>(
                    first_cluster - clusters.begin());
                const auto past_last_index = static_cast<std::size_t>(
                    past_last_cluster - clusters.begin());
                connect(reader.process,
                    clusters[first_index].representative);
                auto gap = find_unjoined_gap(first_index);
                while (gap < past_last_index - 1U) {
                    connect(clusters[gap].representative,
                        clusters[gap + 1U].representative);
                    next_unjoined_gap[gap] = find_unjoined_gap(gap + 1U);
                    gap = find_unjoined_gap(gap);
                }
            }
            group_begin = group_end;
        }
    }
    for (auto& neighbors : adjacent) {
        std::ranges::sort(neighbors);
        neighbors.erase(std::ranges::unique(neighbors).begin(), neighbors.end());
    }

    constexpr auto no_component = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> component_for_process(
        processes.size(), no_component);
    std::vector<bool> visited(processes.size(), false);
    std::vector<std::vector<ProcessId>> member_groups;
    for (std::size_t seed = 0U; seed < processes.size(); ++seed) {
        if (!candidate_process[seed] || visited[seed]) {
            continue;
        }
        const auto component_id = member_groups.size();
        std::vector<ProcessId> members { static_cast<ProcessId>(seed) };
        visited[seed] = true;
        component_for_process[seed] = component_id;
        for (std::size_t cursor = 0U; cursor < members.size(); ++cursor) {
            const auto process = members[cursor];
            for (const auto neighbor : adjacent[process]) {
                if (!visited[neighbor]) {
                    visited[neighbor] = true;
                    component_for_process[neighbor] = component_id;
                    members.push_back(neighbor);
                }
            }
        }
        std::ranges::sort(members);
        member_groups.push_back(std::move(members));
    }

    std::vector<std::vector<SignalId>> signals_by_component(
        member_groups.size());
    std::vector<std::size_t> touching_components;
    for (SignalId signal_id = 0U; signal_id < signals.size(); ++signal_id) {
        touching_components.clear();
        const auto add_component = [&](const RegionAccess& access) {
            if (access.process < component_for_process.size()) {
                const auto component = component_for_process[access.process];
                if (component != no_component) {
                    touching_components.push_back(component);
                }
            }
        };
        for (const auto& access : signals[signal_id].readers) {
            add_component(access);
        }
        for (const auto& access : signals[signal_id].writers) {
            add_component(access);
        }
        std::ranges::sort(touching_components);
        touching_components.erase(
            std::ranges::unique(touching_components).begin(),
            touching_components.end());
        for (const auto component : touching_components) {
            signals_by_component[component].push_back(signal_id);
        }
    }

    inventory.components.reserve(member_groups.size());
    for (std::size_t component_id = 0U;
         component_id < member_groups.size(); ++component_id) {
        RegionComponentCertificate component;
        component.members = std::move(member_groups[component_id]);
        for (const auto process : component.members) {
            component.captured_epochs.push_back(
                { process, capability_epochs.at(process) });
        }

        const auto belongs_to_component = [&](const RegionAccess& access) {
            return access.process < component_for_process.size()
                && component_for_process[access.process] == component_id;
        };
        for (const auto signal_id : signals_by_component[component_id]) {
            const auto& signal = signals[signal_id];
            std::array<bool,
                static_cast<std::size_t>(RegionBoundaryReason::count)> reasons { };
            const auto mark = [&](RegionBoundaryReason reason) {
                reasons[reason_index(reason)] = true;
            };
            const bool has_member_writer = std::ranges::any_of(signal.writers,
                belongs_to_component);
            if (!has_member_writer) {
                mark(signal.writers.empty()
                        ? RegionBoundaryReason::no_internal_writer
                        : RegionBoundaryReason::writer_outside_component);
            }
            if (std::ranges::any_of(signal.writers,
                    [&](const auto& access) {
                        return !belongs_to_component(access);
                    })) {
                mark(RegionBoundaryReason::writer_outside_component);
            }
            if (std::ranges::any_of(signal.readers,
                    [&](const auto& access) {
                        return !belongs_to_component(access);
                    })) {
                mark(RegionBoundaryReason::reader_outside_component);
            }
            switch (signal.drivers) {
            case RegionDriverClass::undriven:
                mark(RegionBoundaryReason::no_internal_writer);
                break;
            case RegionDriverClass::single_whole:
                break;
            case RegionDriverClass::single_partial:
            case RegionDriverClass::disjoint_partial:
                mark(RegionBoundaryReason::partial_driver);
                break;
            case RegionDriverClass::resolved:
                mark(RegionBoundaryReason::resolved_driver_class);
                break;
            case RegionDriverClass::unknown:
                mark(RegionBoundaryReason::unknown_driver_ownership);
                break;
            }
            if (signal.writers_unknown || signal.dynamic_fork_writers) {
                mark(RegionBoundaryReason::unknown_driver_ownership);
            }
            if (signal.descriptor.resolution != ResolutionKind::none
                && signal.descriptor.resolution != ResolutionKind::sv_wire) {
                mark(RegionBoundaryReason::unsupported_resolution_mode);
            }
            if (signal.descriptor.implicit_driver) {
                mark(RegionBoundaryReason::implicit_driver);
            }
            if (signal.descriptor.external_driver) {
                mark(RegionBoundaryReason::external_driver);
            }
            if (signal.descriptor.event_variable) {
                mark(RegionBoundaryReason::event_signal);
            }
            if (signal.descriptor.width == 0U) {
                mark(RegionBoundaryReason::unsupported_width);
            }
            if (signal.descriptor.value_kind != ValueKind::logic4
                && signal.descriptor.value_kind != ValueKind::logic9) {
                mark(RegionBoundaryReason::unsupported_value_kind);
            }
            if (signal.partial_projected_transactions) {
                mark(RegionBoundaryReason::partial_projected_transactions);
            }
            if (!inventory.access_inventory_complete) {
                mark(RegionBoundaryReason::access_inventory_incomplete);
            }
            for (const auto& [capability, reason]
                : observation_boundary_reasons) {
                if (has_observation(signal.observations, capability)) {
                    mark(reason);
                }
            }

            const bool hidden = std::ranges::none_of(reasons,
                [](bool excluded) { return excluded; });
            if (hidden) {
                component.structural_internal_signal_candidates.push_back(signal_id);
                if (signal.descriptor.resolution == ResolutionKind::sv_wire) {
                    component.runtime_single_driver_proof_signals.push_back(signal_id);
                    component.requires_runtime_single_driver_proof = true;
                }
            } else {
                component.boundary_signals.push_back(signal_id);
                for (std::size_t reason = 0U; reason < reasons.size(); ++reason) {
                    if (reasons[reason]) {
                        count_boundary_reason(inventory,
                            static_cast<RegionBoundaryReason>(reason));
                    }
                }
            }
        }

        if (!inventory.access_inventory_complete) {
            component.status
                = RegionComponentCertificateStatus::incomplete_access_inventory;
        } else if (component.structural_internal_signal_candidates.empty()) {
            component.status = RegionComponentCertificateStatus::no_internal_state;
        } else {
            component.status
                = RegionComponentCertificateStatus::structural_candidate;
        }
        inventory.components.push_back(std::move(component));
    }
    return inventory;
}

} // namespace

namespace detail {

RegionDriverClass classify_region_driver_class(
    const RegionSignalNode& signal,
    const RegionDriverClassificationMode mode)
{
    if (signal.writers_unknown || signal.dynamic_fork_writers) {
        return RegionDriverClass::unknown;
    }
    if (mode == RegionDriverClassificationMode::persisted_structure
        && !signal.descriptor.external_driver) {
        // RegionGraph has already computed this exact class in runtime mode;
        // reuse it unless the dynamic external-driver capability overrides it.
        return signal.drivers;
    }
    if (signal.descriptor.implicit_driver
        || (mode == RegionDriverClassificationMode::runtime_capabilities
            && signal.descriptor.external_driver)) {
        return RegionDriverClass::resolved;
    }
    if (signal.writers.empty()) {
        return RegionDriverClass::undriven;
    }
    if (signal.writers.size() == 1U) {
        const auto& writer = signal.writers.front();
        return writer.width == 0U
                || (writer.offset == 0U
                    && writer.width == signal.descriptor.width)
            ? RegionDriverClass::single_whole
            : RegionDriverClass::single_partial;
    }
    auto by_offset = signal.writers;
    std::ranges::sort(by_offset, {}, &RegionAccess::offset);
    std::uint64_t previous_end { };
    for (const auto& writer : by_offset) {
        if (writer.width == 0U || writer.offset < previous_end) {
            return RegionDriverClass::resolved;
        }
        previous_end = std::uint64_t { writer.offset } + writer.width;
    }
    return RegionDriverClass::disjoint_partial;
}

} // namespace detail

bool RegionSignalAliasFamilyDescriptor::project_range(
    const std::uint32_t offset,
    const std::uint32_t requested_width,
    std::vector<RegionSignalAliasRange>& ranges) const
{
    ranges.clear();
    if (!complete || width == 0U || leaves.empty() || offset >= width) {
        return false;
    }
    const auto range_width = requested_width == 0U
        ? width - offset
        : requested_width;
    if (range_width == 0U || range_width > width - offset) {
        return false;
    }
    const auto range_end = offset + range_width;
    std::uint64_t covered_width { };
    for (const auto& leaf : leaves) {
        if (leaf.width == 0U || leaf.offset >= width
            || leaf.width > width - leaf.offset) {
            return false;
        }
        const auto leaf_end = leaf.offset + leaf.width;
        const auto begin = std::max(offset, leaf.offset);
        const auto end = std::min(range_end, leaf_end);
        if (begin >= end) {
            continue;
        }
        ranges.push_back({ leaf.signal,
            static_cast<std::uint32_t>(begin - leaf.offset),
            static_cast<std::uint32_t>(end - begin) });
        covered_width += end - begin;
    }
    return covered_width == range_width;
}

RegionGraph RegionGraph::build(std::span<const Process* const> programs,
    std::span<const RegionSignalDescriptor> descriptors,
    const bool collect_opaque_operation_counts,
    const std::span<const RegionContainerDescriptor> container_descriptors,
    const std::span<const std::uint8_t> process_access_complete,
    const std::span<const RegionSignalAliasFamilyDescriptor>
        signal_alias_families)
{
    if (!process_access_complete.empty()
        && process_access_complete.size() != programs.size()) {
        throw std::invalid_argument(
            "RegionGraph executor-access inventory has the wrong size");
    }
    std::vector<ProcessProgramView> process_views;
    process_views.reserve(programs.size());
    for (const auto* program : programs) {
        if (program == nullptr) {
            process_views.emplace_back();
        } else {
            process_views.emplace_back(*program);
        }
    }
    return region_graph_detail::RegionGraphProgramBuilder::build(
        process_views, descriptors, collect_opaque_operation_counts,
        container_descriptors, process_access_complete,
        signal_alias_families);
}

RegionGraph region_graph_detail::RegionGraphProgramBuilder::build(
    std::span<const ProcessProgramView> programs,
    std::span<const RegionSignalDescriptor> descriptors,
    const bool collect_opaque_operation_counts,
    const std::span<const RegionContainerDescriptor> container_descriptors,
    const std::span<const std::uint8_t> process_access_complete,
    const std::span<const RegionSignalAliasFamilyDescriptor>
        signal_alias_families)
{
    if (!process_access_complete.empty()
        && process_access_complete.size() != programs.size()) {
        throw std::invalid_argument(
            "RegionGraph executor-access inventory has the wrong size");
    }
    RegionGraph graph;
    std::optional<std::map<std::type_index, std::size_t>>
        opaque_operation_counts;
    if (collect_opaque_operation_counts) {
        opaque_operation_counts.emplace();
    }
    graph.signals_.resize(descriptors.size());
    graph.processes_.resize(programs.size());
    graph.capability_epochs_.assign(programs.size(), 1U);
    constexpr auto no_topological_rank
        = std::numeric_limits<std::size_t>::max();
    graph.topological_rank_by_process_.assign(
        programs.size(), no_topological_rank);
    constexpr auto no_alias_family = std::numeric_limits<std::size_t>::max();
    graph.signal_alias_family_by_signal_.assign(
        descriptors.size(), no_alias_family);
    graph.signal_alias_is_proxy_.assign(descriptors.size(), 0U);
    graph.signal_alias_families_.assign(signal_alias_families.begin(),
        signal_alias_families.end());
    std::set<ContainerObjectId> unique_alias_objects;
    for (std::size_t family_index = 0U;
         family_index < graph.signal_alias_families_.size();
         ++family_index) {
        const auto& family = graph.signal_alias_families_[family_index];
        if (!family.complete || !family.proxy_readable
            || family.width == 0U || family.leaves.empty()) {
            throw std::invalid_argument(
                "RegionGraph alias family is incomplete");
        }
        if (!unique_alias_objects.insert(family.object).second) {
            throw std::invalid_argument(
                "RegionGraph alias objects are duplicated");
        }
        require_signal(family.proxy, descriptors.size());
        if (descriptors[family.proxy].width != family.width
            || descriptors[family.proxy].value_kind != ValueKind::logic4
            || graph.signal_alias_family_by_signal_[family.proxy]
                != no_alias_family) {
            throw std::invalid_argument(
                "RegionGraph alias proxy identity or shape is invalid");
        }
        graph.signal_alias_family_by_signal_[family.proxy] = family_index;
        graph.signal_alias_is_proxy_[family.proxy] = 1U;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> intervals;
        intervals.reserve(family.leaves.size());
        std::set<SignalId> unique_leaves;
        for (std::size_t ordinal = 0U; ordinal < family.leaves.size(); ++ordinal) {
            const auto& leaf = family.leaves[ordinal];
            require_signal(leaf.signal, descriptors.size());
            if (leaf.signal == family.proxy || leaf.width == 0U
                || leaf.ordinal != ordinal
                || descriptors[leaf.signal].width != leaf.width
                || descriptors[leaf.signal].value_kind != ValueKind::logic4
                || graph.signal_alias_family_by_signal_[leaf.signal]
                    != no_alias_family
                || !unique_leaves.insert(leaf.signal).second
                || leaf.offset >= family.width
                || leaf.width > family.width - leaf.offset) {
                throw std::invalid_argument(
                    "RegionGraph alias leaf identity or shape is invalid");
            }
            graph.signal_alias_family_by_signal_[leaf.signal] = family_index;
            intervals.emplace_back(leaf.offset, leaf.offset + leaf.width);
        }
        std::ranges::sort(intervals);
        std::uint32_t next_offset { };
        for (const auto& [begin, end] : intervals) {
            if (begin != next_offset || end <= begin) {
                throw std::invalid_argument(
                    "RegionGraph alias leaves do not exactly cover the proxy");
            }
            next_offset = end;
        }
        if (next_offset != family.width) {
            throw std::invalid_argument(
                "RegionGraph alias leaves do not exactly cover the proxy");
        }
    }
    std::map<ContainerObjectId, const RegionContainerDescriptor*>
        containers_by_id;
    for (const auto& container : container_descriptors) {
        if (!containers_by_id.emplace(container.object, &container).second) {
            throw std::invalid_argument(
                "RegionGraph container identities are duplicated");
        }
        for (const auto& binding : container.bindings) {
            require_signal(binding.signal, descriptors.size());
        }
    }
    for (const auto& family : graph.signal_alias_families_) {
        const auto found = containers_by_id.find(family.object);
        if (found == containers_by_id.end() || !found->second->complete) {
            throw std::invalid_argument(
                "RegionGraph alias family has no complete container binding");
        }
        const auto& bindings = found->second->bindings;
        const auto proxy_binding = std::ranges::find_if(bindings,
            [&](const RegionContainerSignalBinding& binding) {
                return binding.signal == family.proxy;
            });
        if (proxy_binding == bindings.end() || !proxy_binding->readable
            || !proxy_binding->indirect_write || !proxy_binding->writable) {
            throw std::invalid_argument(
                "RegionGraph alias proxy binding is inconsistent");
        }
        for (const auto& leaf : family.leaves) {
            const auto leaf_binding = std::ranges::find_if(bindings,
                [&](const RegionContainerSignalBinding& binding) {
                    return binding.signal == leaf.signal;
                });
            if (leaf_binding == bindings.end() || !leaf_binding->readable
                || !leaf_binding->writable || leaf_binding->indirect_write) {
                throw std::invalid_argument(
                    "RegionGraph alias leaf binding is inconsistent");
            }
        }
    }
    for (std::size_t index = 0; index < descriptors.size(); ++index) {
        graph.signals_[index].descriptor = descriptors[index];
        graph.signals_[index].observations = descriptors[index].observations;
        if (descriptors[index].width == 0U) {
            graph.signals_[index].observations = graph.signals_[index].observations
                | RegionObservation::unknown;
            graph.signals_[index].writers_unknown = true;
        }
    }
    for (const auto& family : graph.signal_alias_families_) {
        auto family_observations = graph.signals_[family.proxy].observations;
        for (const auto& leaf : family.leaves) {
            family_observations = family_observations
                | graph.signals_[leaf.signal].observations;
        }
        graph.signals_[family.proxy].observations = family_observations;
        for (const auto& leaf : family.leaves) {
            graph.signals_[leaf.signal].observations = family_observations;
        }
    }
    // Diagnostic-only purity census (FSIM_PROFILE_REGION_PURITY): records
    // the first reason each process loses region purity.
    static const bool purity_profile
        = std::getenv("FSIM_PROFILE_REGION_PURITY") != nullptr;
    std::map<std::string, std::pair<std::uint64_t, std::string>> purity_reasons;
    // Static read ranges depend only on the operations and the widths of the
    // signals they read; processes sharing an operation body share both
    // (sharing requires equal widths and value kinds), so the analysis runs
    // once per body.
    std::unordered_map<const void*,
        std::map<std::size_t, std::vector<StaticReadRange>>> read_ranges_by_body;
    for (std::size_t index = 0; index < programs.size(); ++index) {
        const auto& program = programs[index];
        std::string first_impurity;
        const auto note_impurity = [&](const bool pure_now, std::string reason) {
            if (purity_profile && !pure_now && first_impurity.empty()) {
                first_impurity = std::move(reason);
            }
        };
        if (!program.valid() || program.id() != index) {
            throw std::invalid_argument("RegionGraph process identities are not dense");
        }
        auto& node = graph.processes_[index];
        node.process = program.id();
        node.scheduling_domain = program.scheduling_domain();
        node.dependencies_unknown = !process_access_complete.empty()
            && process_access_complete[index] == 0U;
        node.sensitivities = program.static_sensitivity();
        normalize_sensitivities(node.sensitivities);
        node.reads = node.sensitivities;
        node.writes = program.driver_regions();
        node.pure = static_loop(program);
        if (purity_profile) {
            note_impurity(node.pure, "not_static_loop:" + static_loop_failure(program));
        }
        const bool has_static_loop = node.pure;
        bool has_dynamic_fork { };
        bool ownership_complete = true;
        bool has_update_kind { };
        const auto record_update_kind = [&](SignalId signal, RegionUpdateKind kind) {
            if (!std::ranges::any_of(node.writes, [signal](const auto& writer) {
                    return writer.signal == signal;
                })) {
                ownership_complete = false;
                auto& target = graph.signals_[signal];
                target.writers_unknown = true;
                target.observations = target.observations | RegionObservation::unknown;
                target.invalidation_dependencies.push_back(node.process);
            }
            if (!has_update_kind) {
                node.update_kind = kind;
                has_update_kind = true;
            } else if (node.update_kind != kind) {
                node.update_kind = RegionUpdateKind::mixed_or_unknown;
            }
        };
        const auto add_alias_read_access = [&](const SignalId signal,
                                               const std::uint32_t offset,
                                               const std::uint32_t width) {
            if (signal >= graph.signal_alias_family_by_signal_.size()) {
                return;
            }
            const auto family_index
                = graph.signal_alias_family_by_signal_[signal];
            if (family_index == std::numeric_limits<std::size_t>::max()
                || graph.signal_alias_is_proxy_[signal] == 0U) {
                return;
            }
            const auto& family = graph.signal_alias_families_[family_index];
            std::vector<RegionSignalAliasRange> projected;
            if (!family.project_range(offset, width, projected)) {
                graph.signals_[signal].observations
                    = graph.signals_[signal].observations
                    | RegionObservation::unknown;
                for (const auto& leaf : family.leaves) {
                    auto& target = graph.signals_[leaf.signal];
                    target.observations = target.observations
                        | RegionObservation::unknown;
                    target.invalidation_dependencies.push_back(node.process);
                }
                node.dependencies_unknown = true;
                return;
            }
            for (const auto& range : projected) {
                graph.signals_[range.signal].readers.push_back({
                    node.process, range.offset, range.width, EdgeKind::any });
            }
        };
        const auto observe_alias_family = [&](const SignalId signal,
                                              const RegionObservation capability) {
            if (signal >= graph.signal_alias_family_by_signal_.size()) {
                return;
            }
            const auto family_index
                = graph.signal_alias_family_by_signal_[signal];
            if (family_index == std::numeric_limits<std::size_t>::max()) {
                return;
            }
            const auto& family = graph.signal_alias_families_[family_index];
            graph.signals_[family.proxy].observations
                = graph.signals_[family.proxy].observations | capability;
            for (const auto& leaf : family.leaves) {
                graph.signals_[leaf.signal].observations
                    = graph.signals_[leaf.signal].observations | capability;
            }
        };
        const auto observe_access = [&](SignalId signal,
                                        RegionObservation capability,
                                        const std::uint32_t offset,
                                        const std::uint32_t width) {
            require_signal(signal, graph.signals_.size());
            auto& observed = graph.signals_[signal].observations;
            observed = observed | capability;
            observe_alias_family(signal, capability);
            node.reads.push_back({ signal, EdgeKind::any, offset, width });
            add_alias_read_access(signal, offset, width);
        };
        const auto observe = [&](SignalId signal,
                                 RegionObservation capability) {
            observe_access(signal, capability, 0U, 0U);
        };
        const auto* const body = program.operations().body_identity();
        const auto& read_ranges = body != nullptr
            ? [&]() -> const std::map<std::size_t, std::vector<StaticReadRange>>& {
                  auto found = read_ranges_by_body.find(body);
                  if (found == read_ranges_by_body.end()) {
                      found = read_ranges_by_body.emplace(body,
                          infer_static_read_ranges(program, descriptors)).first;
                  }
                  return found->second;
              }()
            : read_ranges_by_body.emplace(nullptr,
                  std::map<std::size_t, std::vector<StaticReadRange>> { }).first->second
                  = infer_static_read_ranges(program, descriptors);
        const auto& operations = program.operations();
        std::size_t current_instruction { };
        const auto observe_read = [&](const ReadSignal& read,
                                      const RegionObservation capability) {
            require_signal(read.signal, descriptors.size());
            const auto found = read_ranges.find(current_instruction);
            if (found != read_ranges.end()) {
                const auto width = descriptors[read.signal].width;
                if (std::ranges::any_of(found->second,
                        [width](const StaticReadRange& range) {
                            return range.width == 0U || range.offset >= width
                                || range.width > width - range.offset;
                        })) {
                    observe_access(read.signal, capability, 0U, 0U);
                    return;
                }
                for (const auto& range : found->second) {
                    observe_access(read.signal, capability,
                        range.offset,
                        range.offset == 0U && range.width == width
                            ? 0U : range.width);
                }
            } else {
                observe_access(read.signal, capability, 0U, 0U);
            }
        };
        const auto record_write_access = [&](SignalId signal) {
            require_signal(signal, graph.signals_.size());
            if (!std::ranges::any_of(node.writes, [signal](const auto& writer) {
                    return writer.signal == signal;
                })) {
                ownership_complete = false;
                auto& target = graph.signals_[signal];
                target.writers_unknown = true;
                target.observations = target.observations
                    | RegionObservation::unknown;
                target.invalidation_dependencies.push_back(node.process);
            }
        };
        const auto record_container_write_access = [&](
            const RegionContainerSignalBinding& binding) {
            const auto signal = binding.signal;
            require_signal(signal, descriptors.size());
            const auto width = descriptors[signal].width;
            const bool owns_full_signal = !binding.indirect_write
                && width != 0U && std::ranges::any_of(
                node.writes, [signal, width](const auto& writer) {
                    return writer.signal == signal
                        && (writer.whole || (writer.offset == 0U
                            && writer.width == width));
                });
            if (!owns_full_signal) {
                ownership_complete = false;
                auto& target = graph.signals_[signal];
                target.writers_unknown = true;
                target.observations = target.observations
                    | RegionObservation::unknown;
                target.invalidation_dependencies.push_back(node.process);
            }
            const bool has_full_access = std::ranges::any_of(
                graph.signals_[signal].writers,
                [process = node.process, width](const auto& writer) {
                    return writer.process == process
                        && writer.offset == 0U
                        && (writer.width == 0U || writer.width == width);
                });
            if (!has_full_access) {
                graph.signals_[signal].writers.push_back(
                    { node.process, 0U, 0U });
            }
        };
        const auto find_container = [&](ContainerObjectId object) {
            const auto found = containers_by_id.find(object);
            return found == containers_by_id.end() ? nullptr : found->second;
        };
        for (const auto endpoint : { program.switch_source(),
                 program.switch_target(), program.switch_control() }) {
            if (endpoint) {
                observe(*endpoint, RegionObservation::unknown);
            }
        }
        for (const auto& sensitivity : node.sensitivities) {
            require_signal(sensitivity.signal, graph.signals_.size());
            const auto width = descriptors[sensitivity.signal].width;
            if (sensitivity.width != 0U
                && (sensitivity.offset >= width
                    || sensitivity.width > width - sensitivity.offset)) {
                throw std::invalid_argument("RegionGraph sensitivity range is invalid");
            }
            if (sensitivity.edge != EdgeKind::any) {
                observe(sensitivity.signal, RegionObservation::events);
                node.pure = false;
                note_impurity(false, "edge_sensitivity");
            } else {
                add_alias_read_access(sensitivity.signal,
                    sensitivity.offset, sensitivity.width);
            }
        }
        for (std::size_t instruction = 0; instruction < operations.size();
             ++instruction) {
            current_instruction = instruction;
            const auto& stored_operation = operations.at(instruction);
            if (operation_holds<DebugPoint>(stored_operation)) {
                // Debug metadata remains in the program, but contributes no
                // graph access, dependency, or purity facts.
                continue;
            }
            if (plain_register_operation(stored_operation)) {
                // No signal, container or fork access, and supported: nothing
                // to record (and no instance field to expand).
                continue;
            }
            const auto operation = operations.expanded(instruction);
            bool fork_control_known = true;
            if (const auto* fork = operation_get_if<Fork>(&operation)) {
                fork_control_known = fork_control_access_is_known(
                    *fork, instruction, operations.size());
            }
            has_dynamic_fork = has_dynamic_fork
                || operation_holds<Fork>(operation);
            const bool loop_tail = has_static_loop
                && instruction + 2U >= operations.size();
            bool supported = loop_tail || plain_register_operation(operation);
            visit_operation([&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, ReadSignal>) {
                    supported = value.kind == SignalReadKind::current
                        && value.ticks == 1U && !value.clock && !value.gate;
                } else if constexpr (std::is_same_v<T, WriteBlocking>) {
                    require_signal(value.signal, descriptors.size());
                    record_update_kind(value.signal,
                        RegionUpdateKind::systemverilog_active);
                    supported = node.scheduling_domain
                        == ProcessSchedulingDomain::systemverilog;
                } else if constexpr (std::is_same_v<T, WriteUpdate>
                    || std::is_same_v<T, WriteUpdateSlice>) {
                    require_signal(value.signal, descriptors.size());
                    record_update_kind(value.signal, update_kind(value.domain));
                    supported = value.domain == SignalUpdateDomain::systemverilog_active
                        || value.domain == SignalUpdateDomain::generic;
                } else if constexpr (std::is_same_v<T, WriteProjected>
                    || std::is_same_v<T, WriteProjectedWaveform>
                    || std::is_same_v<T, WriteProjectedSlice>
                    || std::is_same_v<T, WriteProjectedWaveformSlice>
                    || std::is_same_v<T, WriteProjectedDynamicSlice>
                    || std::is_same_v<T, WriteProjectedWaveformDynamicSlice>) {
                    require_signal(value.signal, descriptors.size());
                    record_update_kind(value.signal, RegionUpdateKind::vhdl_projected);
                    if constexpr (std::is_same_v<T, WriteProjectedSlice>
                        || std::is_same_v<T, WriteProjectedWaveformSlice>
                        || std::is_same_v<T, WriteProjectedDynamicSlice>
                        || std::is_same_v<T, WriteProjectedWaveformDynamicSlice>) {
                        graph.signals_[value.signal].partial_projected_transactions = true;
                    }
                    if constexpr (std::is_same_v<T, WriteProjected>
                        || std::is_same_v<T, WriteProjectedSlice>) {
                        supported = value.delay == 0U && value.rejection == 0U
                            && value.mode == ProjectedDelayMode::inertial;
                    } else {
                        // The waveform and dynamically selected forms retain
                        // ordered or partial projected transactions. They are
                        // graph-visible VHDL effects, but not one scalar
                        // publication that a region can stage safely.
                        supported = false;
                    }
                }
            }, operation);
            const bool access_known = fork_control_known
                && visit_known_region_accesses(
                operation, find_container, observe, observe_read,
                record_write_access,
                record_container_write_access);
            if (!access_known) {
                node.dependencies_unknown = true;
                if (opaque_operation_counts) {
                    const auto operation_type = visit_operation(
                        [](const auto& value) {
                            return std::type_index { typeid(value) };
                        }, operation);
                    ++(*opaque_operation_counts)[operation_type];
                }
                // Preserve a local invalidation edge when an opaque operation
                // still names one fixed signal, while keeping the whole-graph
                // inventory fail-closed for arbitrary hidden accesses.
                visit_operation([&](const auto& value) {
                    if constexpr (requires { value.signal; }) {
                        observe(value.signal, RegionObservation::unknown);
                    }
                }, operation);
            }
            if (purity_profile && node.pure && !supported) {
                note_impurity(false, "operation:" + std::string {
                    visit_operation([](const auto& value) {
                        return std::string_view { typeid(value).name() };
                    }, operation) }
                    + visit_operation([](const auto& value) -> std::string {
                        using T = std::decay_t<decltype(value)>;
                        if constexpr (std::is_same_v<T, Binary>) {
                            return ":" + std::to_string(
                                static_cast<int>(value.operation));
                        }
                        return {};
                    }, operation));
            }
            node.pure = node.pure && supported;
        }
        if (node.pure && ownership_complete && !has_update_kind
            && node.writes.empty()
            && node.scheduling_domain == ProcessSchedulingDomain::systemverilog) {
            // A pure reader has no update operation to classify. Its explicit
            // SV scheduling domain still permits exact Active-region execution
            // alongside its producers, with no driver or publication invented.
            node.update_kind = RegionUpdateKind::systemverilog_active;
            has_update_kind = true;
        }
        if (!ownership_complete || !has_update_kind
            || (node.scheduling_domain == ProcessSchedulingDomain::systemverilog
                && node.update_kind != RegionUpdateKind::systemverilog_active)
            || node.update_kind == RegionUpdateKind::mixed_or_unknown) {
            note_impurity(!node.pure,
                !ownership_complete ? "ownership_incomplete"
                : !has_update_kind ? "no_update_kind"
                : "update_kind_mismatch");
            node.pure = false;
        }
        normalize_sensitivities(node.reads);
        for (const auto& read : node.reads) {
            if (descriptors[read.signal].width == 0U) {
                node.pure = false;
            }
            graph.signals_[read.signal].readers.push_back(
                { node.process, read.offset, read.width, read.edge });
        }
        for (const auto& write : node.writes) {
            require_signal(write.signal, descriptors.size());
            const auto width = descriptors[write.signal].width;
            if (width == 0U) {
                node.pure = false;
            }
            if (!write.whole && (write.width == 0U || write.offset >= width
                    || write.width > width - write.offset)) {
                throw std::invalid_argument("RegionGraph driver range is invalid");
            }
            const RegionAccess access { node.process,
                write.whole ? 0U : write.offset,
                write.whole ? 0U : write.width };
            const auto family_index
                = graph.signal_alias_family_by_signal_[write.signal];
            if (family_index != std::numeric_limits<std::size_t>::max()
                && graph.signal_alias_is_proxy_[write.signal] != 0U) {
                const auto& family
                    = graph.signal_alias_families_[family_index];
                std::vector<RegionSignalAliasRange> projected;
                if (!family.project_range(access.offset, access.width,
                        projected)) {
                    graph.signals_[write.signal].writers_unknown = true;
                    graph.signals_[write.signal].observations
                        = graph.signals_[write.signal].observations
                        | RegionObservation::unknown;
                    for (const auto& leaf : family.leaves) {
                        auto& target = graph.signals_[leaf.signal];
                        target.writers_unknown = true;
                        target.observations = target.observations
                            | RegionObservation::unknown;
                    }
                } else {
                    for (const auto& range : projected) {
                        graph.signals_[range.signal].writers.push_back({
                            node.process, range.offset, range.width });
                    }
                }
            } else {
                graph.signals_[write.signal].writers.push_back(access);
            }
        }
        if (has_dynamic_fork) {
            // The immutable body bounds child accesses, but its future
            // ProcessIds are not part of the current writer inventory. Keep
            // this separate from unproven accesses/ranges: structural regions
            // reject both, while existing cohorts can use the proven current
            // owners until spawn invalidates them before a child is runnable.
            for (const auto& write : node.writes) {
                const auto family_index
                    = graph.signal_alias_family_by_signal_[write.signal];
                if (family_index != std::numeric_limits<std::size_t>::max()
                    && graph.signal_alias_is_proxy_[write.signal] != 0U) {
                    const auto& family
                        = graph.signal_alias_families_[family_index];
                    std::vector<RegionSignalAliasRange> projected;
                    if (family.project_range(write.whole ? 0U : write.offset,
                            write.whole ? 0U : write.width, projected)) {
                        for (const auto& range : projected) {
                            graph.signals_[range.signal]
                                .dynamic_fork_writers = true;
                        }
                    } else {
                        for (const auto& leaf : family.leaves) {
                            graph.signals_[leaf.signal]
                                .dynamic_fork_writers = true;
                        }
                    }
                } else {
                    graph.signals_[write.signal].dynamic_fork_writers = true;
                }
            }
        }
        if (!node.writes.empty()) {
            bool operation_write_ranges_exact { };
            if (!ownership_matches_operations(program, descriptors,
                    find_container, operation_write_ranges_exact)) {
                note_impurity(!node.pure, "ownership_mismatch");
                node.pure = false;
                for (const auto& write : node.writes) {
                    auto& target = graph.signals_[write.signal];
                    target.writers_unknown = true;
                    target.observations = target.observations
                        | RegionObservation::unknown;
                }
            } else {
                node.operation_write_ranges_exact
                    = operation_write_ranges_exact;
            }
        }
        if (purity_profile && !first_impurity.empty()) {
            auto& entry = purity_reasons[first_impurity];
            if (entry.first++ == 0U) {
                entry.second = std::to_string(program.id());
            }
        }
    }
    if (purity_profile) {
        for (const auto& [reason, entry] : purity_reasons) {
            std::cerr << "fsim-profile: region-purity reason=" << reason
                      << " processes=" << entry.first
                      << " example=" << entry.second << '\n';
        }
    }

    // Retain all ownership and observation edges in the inventory. For pure
    // region ordering, cut sequential, opaque and language/publication-domain
    // boundaries. Feedback through a register is not a combinational cycle.
    // The traversal remains bipartite: never expand a writer-reader product.
    struct Dependency {
        ProcessSchedulingDomain scheduling_domain;
        RegionUpdateKind update_kind;
        std::size_t pending_writers { };
        std::vector<ProcessId> readers;
    };
    std::vector<Dependency> dependencies;
    std::vector<std::size_t> pending_process(programs.size());
    std::vector<std::vector<std::size_t>> outputs(programs.size());
    for (auto& signal : graph.signals_) {
        canonicalize_accesses(signal.readers);
        canonicalize_accesses(signal.writers);
        classify_drivers(signal);
        const auto first_dependency = dependencies.size();
        const auto dependency_for = [&](const RegionProcessNode& process) {
            for (auto index = first_dependency; index < dependencies.size(); ++index) {
                if (dependencies[index].scheduling_domain == process.scheduling_domain
                    && dependencies[index].update_kind == process.update_kind) {
                    return index;
                }
            }
            dependencies.push_back({ process.scheduling_domain, process.update_kind, 0U, { } });
            return dependencies.size() - 1U;
        };
        for (const auto& access : signal.writers) {
            signal.invalidation_dependencies.push_back(access.process);
            const auto& process = graph.processes_[access.process];
            if (process.pure) {
                const auto dependency = dependency_for(process);
                ++dependencies[dependency].pending_writers;
                outputs[access.process].push_back(dependency);
            }
        }
        for (const auto& access : signal.readers) {
            signal.invalidation_dependencies.push_back(access.process);
            const auto& process = graph.processes_[access.process];
            if (process.pure) {
                const auto dependency = dependency_for(process);
                if (dependencies[dependency].pending_writers != 0U) {
                    dependencies[dependency].readers.push_back(access.process);
                    ++pending_process[access.process];
                }
            }
        }
        std::ranges::sort(signal.invalidation_dependencies);
        signal.invalidation_dependencies.erase(
            std::ranges::unique(signal.invalidation_dependencies).begin(),
            signal.invalidation_dependencies.end());
    }
    // Observation can run from a no-throw mutation hook. Freeze each family
    // closure here so invalidation never allocates or exposes a partial epoch
    // update after public state has changed.
    graph.signal_alias_invalidation_dependencies_.resize(
        graph.signal_alias_families_.size());
    for (std::size_t family_index = 0U;
         family_index < graph.signal_alias_families_.size();
         ++family_index) {
        const auto& family = graph.signal_alias_families_[family_index];
        auto& family_dependencies
            = graph.signal_alias_invalidation_dependencies_[family_index];
        const auto append_dependencies = [&](const SignalId signal) {
            const auto& member_dependencies
                = graph.signals_[signal].invalidation_dependencies;
            family_dependencies.insert(family_dependencies.end(),
                member_dependencies.begin(), member_dependencies.end());
        };
        append_dependencies(family.proxy);
        for (const auto& leaf : family.leaves) {
            append_dependencies(leaf.signal);
        }
        std::ranges::sort(family_dependencies);
        family_dependencies.erase(std::ranges::unique(family_dependencies).begin(),
            family_dependencies.end());
    }
    std::priority_queue<ProcessId, std::vector<ProcessId>, std::greater<>> ready;
    for (std::size_t process = 0; process < programs.size(); ++process) {
        if (graph.processes_[process].pure && pending_process[process] == 0U) {
            ready.push(static_cast<ProcessId>(process));
        }
    }
    while (!ready.empty()) {
        const auto process = ready.top();
        ready.pop();
        graph.topological_order_.push_back(process);
        graph.topological_rank_by_process_[process]
            = graph.topological_order_.size() - 1U;
        // Canonical entries can include several disjoint ranges per owner.
        // Each contributes one pending edge and is retired exactly once.
        for (const auto dependency_id : outputs[process]) {
            auto& dependency = dependencies[dependency_id];
            if (--dependency.pending_writers == 0U) {
                for (const auto reader : dependency.readers) {
                    if (--pending_process[reader] == 0U) {
                        ready.push(reader);
                    }
                }
            }
        }
    }
    for (std::size_t process = 0; process < programs.size(); ++process) {
        graph.processes_[process].cyclic_or_dependent_on_cycle
            = graph.processes_[process].pure && pending_process[process] != 0U;
    }
    graph.certificate_inventory_ = build_certificate_inventory(
        graph.processes_, graph.signals_, graph.capability_epochs_,
        graph.signal_alias_family_by_signal_);
    constexpr auto no_certificate_component
        = std::numeric_limits<std::size_t>::max();
    graph.certificate_component_by_process_.assign(
        graph.processes_.size(), no_certificate_component);
    graph.component_epochs_stale_.assign(
        graph.certificate_inventory_.components.size(), 0U);
    for (std::size_t component = 0U;
         component < graph.certificate_inventory_.components.size();
         ++component) {
        for (const auto process
            : graph.certificate_inventory_.components[component].members) {
            auto& process_component
                = graph.certificate_component_by_process_.at(process);
            if (process_component != no_certificate_component) {
                throw std::logic_error {
                    "RegionGraph process belongs to multiple certificate components"
                };
            }
            process_component = component;
        }
    }
    if (opaque_operation_counts) {
        graph.certificate_inventory_.opaque_operation_counts.reserve(
            opaque_operation_counts->size());
        for (const auto& [operation_type, incidences] : *opaque_operation_counts) {
            graph.certificate_inventory_.opaque_operation_counts.push_back(
                { operation_type.name(), incidences });
        }
        std::ranges::sort(
            graph.certificate_inventory_.opaque_operation_counts,
            {}, &RegionOpaqueOperationCount::type_name);
    }
    return graph;
}

namespace detail {

template<typename SignalRecord>
RegionGraphContainerBindings build_region_graph_container_bindings_impl(
    const std::span<const SignalRecord> signals,
    const std::span<const ContainerObject> objects,
    const std::span<const ContainerSignalAlias> direct_aliases,
    const std::span<const ContainerElementSignalAlias> element_aliases,
    const std::span<const ContainerAggregateSignalAlias> aggregate_aliases)
{
    RegionGraphContainerBindings result;
    result.containers.reserve(objects.size());
    result.complete_alias_member.assign(signals.size(), 0U);

    std::vector<std::vector<const ContainerSignalAlias*>> direct_by_object(
        objects.size());
    std::vector<std::vector<const ContainerElementSignalAlias*>>
        elements_by_object(objects.size());
    std::vector<std::vector<const ContainerAggregateSignalAlias*>>
        aggregate_by_object(objects.size());
    std::vector<std::vector<ContainerObjectId>> direct_by_signal(
        signals.size());
    std::vector<std::optional<std::pair<ContainerObjectId, std::size_t>>>
        element_by_signal(signals.size());
    std::vector<std::optional<ContainerObjectId>> aggregate_by_signal(
        signals.size());
    std::vector<std::uint8_t> duplicate_element_signal(signals.size(), 0U);
    std::vector<std::uint8_t> duplicate_aggregate_signal(
        signals.size(), 0U);

    for (const auto& alias : direct_aliases) {
        if (alias.object >= objects.size() || alias.signal >= signals.size()) {
            throw std::invalid_argument(
                "RegionGraph direct container alias is invalid");
        }
        direct_by_object[alias.object].push_back(&alias);
        direct_by_signal[alias.signal].push_back(alias.object);
    }
    for (const auto& alias : element_aliases) {
        if (alias.object >= objects.size() || alias.signal >= signals.size()) {
            throw std::invalid_argument(
                "RegionGraph element container alias is invalid");
        }
        elements_by_object[alias.object].push_back(&alias);
        if (element_by_signal[alias.signal]) {
            duplicate_element_signal[alias.signal] = 1U;
        } else {
            element_by_signal[alias.signal]
                = std::pair { alias.object,
                    static_cast<std::size_t>(alias.ordinal) };
        }
    }
    for (const auto& alias : aggregate_aliases) {
        if (alias.object >= objects.size() || alias.signal >= signals.size()) {
            throw std::invalid_argument(
                "RegionGraph aggregate container alias is invalid");
        }
        aggregate_by_object[alias.object].push_back(&alias);
        if (aggregate_by_signal[alias.signal]) {
            duplicate_aggregate_signal[alias.signal] = 1U;
        } else {
            aggregate_by_signal[alias.signal] = alias.object;
        }
    }

    for (ContainerObjectId object = 0U; object < objects.size(); ++object) {
        const auto& value = objects[object];
        RegionContainerDescriptor descriptor;
        descriptor.object = object;
        descriptor.complete = !value.slice_alias;
        for (const auto* alias : direct_by_object[object]) {
            descriptor.bindings.push_back({
                alias->signal, alias->readable, alias->writable });
        }
        for (const auto* alias : elements_by_object[object]) {
            descriptor.bindings.push_back({
                alias->signal, alias->readable, alias->writable });
        }
        for (const auto* alias : aggregate_by_object[object]) {
            descriptor.bindings.push_back({
                alias->signal, alias->readable, true, true });
        }
        result.containers.push_back(std::move(descriptor));

        if (aggregate_by_object[object].size() != 1U
            || !direct_by_object[object].empty()) {
            continue;
        }
        const auto& aggregate = *aggregate_by_object[object].front();
        const auto& type = value.initial_value.type;
        const auto& elements = elements_by_object[object];
        if (value.slice_alias || !type.fixed || type.dimensions.empty()
            || type.element_kind != ContainerElementKind::Packed
            || type.two_state || type.element_width == 0U
            || !aggregate.readable || aggregate.object != object
            || elements.empty()
            || elements.size() != value.initial_value.elements.size()) {
            continue;
        }

        const auto bridge_width = container_signal_bridge_width(type);
        if (aggregate.signal >= signals.size() || !bridge_width
            || *bridge_width / type.element_width != elements.size()
            || *bridge_width % type.element_width != 0U
            || *bridge_width != signals[aggregate.signal].initial_value.width()
            || *bridge_width > std::numeric_limits<std::uint32_t>::max()
            || signals[aggregate.signal].value_kind != ValueKind::logic4
            || signals[aggregate.signal].initial_value.is_logic9()
            || signals[aggregate.signal].resolution != ResolutionKind::sv_wire
            || duplicate_aggregate_signal[aggregate.signal]
            || aggregate_by_signal[aggregate.signal]
                != std::optional<ContainerObjectId> { object }
            || duplicate_element_signal[aggregate.signal]
            || element_by_signal[aggregate.signal]
            || !direct_by_signal[aggregate.signal].empty()) {
            continue;
        }

        std::vector<const ContainerElementSignalAlias*> by_ordinal(
            elements.size());
        bool valid = true;
        for (const auto* element : elements) {
            if (element->object != object
                || element->ordinal >= by_ordinal.size()
                || by_ordinal[element->ordinal]
                || !element->readable || !element->writable) {
                valid = false;
                break;
            }
            by_ordinal[element->ordinal] = element;
        }
        if (!valid) {
            continue;
        }

        RegionSignalAliasFamilyDescriptor family;
        family.object = object;
        family.proxy = aggregate.signal;
        family.width = static_cast<std::uint32_t>(*bridge_width);
        family.complete = true;
        family.proxy_readable = aggregate.readable;
        family.proxy_writable = aggregate.writable;
        family.leaves.reserve(by_ordinal.size());
        for (std::size_t ordinal = 0U; ordinal < by_ordinal.size(); ++ordinal) {
            const auto* element = by_ordinal[ordinal];
            if (element == nullptr
                || duplicate_element_signal[element->signal]
                || element_by_signal[element->signal]
                    != std::optional<std::pair<ContainerObjectId, std::size_t>> {
                        std::pair { object, ordinal } }
                || duplicate_aggregate_signal[element->signal]
                || aggregate_by_signal[element->signal]
                || !direct_by_signal[element->signal].empty()) {
                valid = false;
                break;
            }
            const auto& signal = signals[element->signal];
            if (signal.initial_value.width() != type.element_width
                || signal.value_kind != ValueKind::logic4
                || signal.initial_value.is_logic9()
                || signal.resolution != ResolutionKind::sv_wire) {
                valid = false;
                break;
            }
            const auto offset = (by_ordinal.size() - ordinal - 1U)
                * static_cast<std::size_t>(type.element_width);
            if (offset > std::numeric_limits<std::uint32_t>::max()) {
                valid = false;
                break;
            }
            family.leaves.push_back({ element->signal,
                static_cast<std::uint32_t>(ordinal),
                static_cast<std::uint32_t>(offset), type.element_width });
        }
        if (!valid) {
            continue;
        }
        result.complete_alias_member[family.proxy] = 1U;
        for (const auto& leaf : family.leaves) {
            result.complete_alias_member[leaf.signal] = 1U;
        }
        result.alias_families.push_back(std::move(family));
    }
    return result;
}

RegionGraphContainerBindings build_region_graph_container_bindings(
    const std::span<const Signal> signals,
    const std::span<const ContainerObject> objects,
    const std::span<const ContainerSignalAlias> direct_aliases,
    const std::span<const ContainerElementSignalAlias> element_aliases,
    const std::span<const ContainerAggregateSignalAlias> aggregate_aliases)
{
    return build_region_graph_container_bindings_impl(signals, objects,
        direct_aliases, element_aliases, aggregate_aliases);
}

RegionGraphContainerBindings build_region_graph_container_bindings(
    const std::span<const SignalHot> signals,
    const std::span<const ContainerObject> objects,
    const std::span<const ContainerSignalAlias> direct_aliases,
    const std::span<const ContainerElementSignalAlias> element_aliases,
    const std::span<const ContainerAggregateSignalAlias> aggregate_aliases)
{
    return build_region_graph_container_bindings_impl(signals, objects,
        direct_aliases, element_aliases, aggregate_aliases);
}

} // namespace detail

std::span<const ProcessId> RegionGraph::observe_signal(
    const SignalId signal, const RegionObservation capabilities)
{
    if (signal >= signals_.size()) {
        throw std::out_of_range("RegionGraph observation signal is invalid");
    }
    const auto family_index = signal_alias_family_by_signal_[signal];
    std::span<const ProcessId> invalidated;
    if (family_index == std::numeric_limits<std::size_t>::max()) {
        auto& node = signals_[signal];
        node.observations = node.observations | capabilities;
        invalidated = node.invalidation_dependencies;
    } else {
        const auto& family = signal_alias_families_[family_index];
        signals_[family.proxy].observations
            = signals_[family.proxy].observations | capabilities;
        for (const auto& leaf : family.leaves) {
            signals_[leaf.signal].observations
                = signals_[leaf.signal].observations | capabilities;
        }
        invalidated = signal_alias_invalidation_dependencies_[family_index];
    }
    constexpr auto no_certificate_component
        = std::numeric_limits<std::size_t>::max();
    for (const auto process : invalidated) {
        auto& epoch = capability_epochs_[process];
        if (epoch != 0U) {
            epoch = epoch == std::numeric_limits<std::uint64_t>::max()
                ? 0U : epoch + 1U;
        }
        const auto component = certificate_component_by_process_[process];
        if (component != no_certificate_component
            && component_epochs_stale_[component] == 0U) {
            component_epochs_stale_[component] = 1U;
        }
    }
    return invalidated;
}

std::uint64_t RegionGraph::capability_epoch(const ProcessId process) const
{
    return capability_epochs_.at(process);
}

bool RegionGraph::component_epochs_current(const std::size_t component) const
{
    static_cast<void>(certificate_inventory_.components.at(component));
    return component_epochs_stale_.at(component) == 0U;
}

} // namespace fsim::runtime::simir
