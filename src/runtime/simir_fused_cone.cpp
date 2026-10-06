// SPDX-License-Identifier: Apache-2.0
//
// Runtime support for fused combinational cones (A2). While a cone is fused,
// its internal nets are registers of the sink's fused body and are never
// published. Observation of an internal net recomputes the cone's internal
// values from the committed boundary inputs and commits each original
// member's contribution under that member's ProcessId, so values and driver
// records match unfused execution. Members are pure functions of their
// inputs, so the recomputed values are exact for the current inputs.
#include "simir_internal.hpp"

#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

namespace {

constexpr auto no_fused_cone = std::numeric_limits<std::uint32_t>::max();

} // namespace

void InterpreterProgramAccess::install_fused_cones(Interpreter& interpreter,
    std::vector<FusedConeRuntimeSpec> cones,
    const std::vector<ProcessId>& dormant)
{
    auto& impl = *interpreter.impl_;
    if (impl.started) {
        throw std::logic_error(
            "fused cones must be installed before simulation starts");
    }
    impl.fused_cone_by_signal.assign(impl.signals.size(), no_fused_cone);
    impl.fusion_dormant_process.assign(impl.processes.size(), 0U);
    for (const auto process : dormant) {
        if (process >= impl.fusion_dormant_process.size()) {
            throw std::out_of_range("fused cone member is not installed");
        }
        impl.fusion_dormant_process[process] = 1U;
    }
    impl.fused_cones.clear();
    impl.fused_cones.reserve(cones.size());
    for (auto& cone : cones) {
        const auto index = static_cast<std::uint32_t>(impl.fused_cones.size());
        for (const auto signal : cone.internal_signals) {
            if (signal >= impl.fused_cone_by_signal.size()) {
                throw std::out_of_range("fused cone signal is not declared");
            }
            impl.fused_cone_by_signal[signal] = index;
        }
        Interpreter::Impl::FusedConeState state;
        state.spec = std::move(cone);
        impl.fused_cones.push_back(std::move(state));
    }
}

bool InterpreterProgramAccess::fusion_dormant(
    const Interpreter& interpreter, const ProcessId process) noexcept
{
    const auto& flags = interpreter.impl_->fusion_dormant_process;
    return process < flags.size() && flags[process] != 0U;
}

void Interpreter::Impl::materialize_fused_cone(const std::size_t cone_index)
{
    auto& cone = fused_cones.at(cone_index);
    const auto& spec = cone.spec;
    if (cone.materialized
        && cone.boundary_revisions.size() == spec.boundary_inputs.size()) {
        bool unchanged = true;
        for (std::size_t index = 0U; index < spec.boundary_inputs.size();
             ++index) {
            if (signal_value_revisions[spec.boundary_inputs[index]]
                != cone.boundary_revisions[index]) {
                unchanged = false;
                break;
            }
        }
        if (unchanged) {
            return;
        }
    }

    struct PendingCommit {
        ProcessId process { };
        SignalId signal { };
        std::optional<std::size_t> offset;
        PackedLogic4 value;
    };
    std::vector<PendingCommit> commits;
    // Values of internal nets assembled during this pass, in topological
    // order, so later members read their producers' results.
    std::unordered_map<SignalId, PackedLogic4> internal_values;

    const auto read_committed = [&](const SignalId signal) {
        prepare_signal_observation(signal);
        if (signal < signal_container_aggregate_aliases.size()
            && signal_container_aggregate_aliases[signal]) {
            return aggregate_signal_current_value(signal);
        }
        return get_signal(signal).initial_value;
    };
    const auto is_internal = [&](const SignalId signal) {
        return signal < fused_cone_by_signal.size()
            && fused_cone_by_signal[signal] == cone_index;
    };

    for (const auto member : spec.members) {
        const ProcessProgramView program = member == spec.sink
            ? ProcessProgramView { spec.sink_original }
            : processes.program_view(member);
        const auto& operations = program.operations();
        std::vector<PackedLogic4> registers(program.register_count());
        const auto reg = [&](const RegisterId id) -> PackedLogic4& {
            if (id >= registers.size()) {
                throw std::logic_error("fused cone register is out of range");
            }
            return registers[id];
        };
        const auto record_write = [&](const SignalId signal,
                                      const PackedLogic4& value,
                                      const std::optional<std::size_t> offset) {
            if (!is_internal(signal)) {
                return;
            }
            auto& current = internal_values[signal];
            if (offset) {
                if (current.width() == 0U) {
                    current = PackedLogic4 {
                        get_signal(signal).initial_value.width(), Logic4::z
                    };
                }
                current = insert_value(std::move(current), value, *offset);
            } else {
                current = value;
            }
            commits.push_back({ member, signal, offset, value });
        };
        std::size_t pc = 0U;
        for (std::size_t steps = 0U; pc < operations.size(); ++steps) {
            if (steps > operations.size()) {
                throw std::logic_error("fused cone member did not terminate");
            }
            const auto operation = operations.expanded(pc);
            if (operation_holds<WaitSensitivity>(operation)
                || operation_holds<Halt>(operation)) {
                break;
            }
            InstructionIndex next = static_cast<InstructionIndex>(pc + 1U);
            visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, DebugPoint>) {
                } else if constexpr (std::is_same_v<T, LoadConstant>) {
                    reg(op.destination)
                        = coerce_value_kind(op.value, ValueKind::logic4);
                } else if constexpr (std::is_same_v<T, CopyRegister>) {
                    reg(op.destination)
                        = coerce_value_kind(reg(op.source), ValueKind::logic4);
                } else if constexpr (std::is_same_v<T, ReadSignal>) {
                    if (is_internal(op.signal)) {
                        const auto found = internal_values.find(op.signal);
                        reg(op.destination) = found != internal_values.end()
                            ? found->second : read_committed(op.signal);
                    } else {
                        reg(op.destination) = read_committed(op.signal);
                    }
                } else if constexpr (std::is_same_v<T, Binary>) {
                    reg(op.destination)
                        = binary_value(op.operation, reg(op.lhs), reg(op.rhs));
                } else if constexpr (std::is_same_v<T, Reduction>) {
                    reg(op.destination)
                        = reduce_value(op.operation, reg(op.source));
                } else if constexpr (std::is_same_v<T, UnaryNot>) {
                    reg(op.destination) = unary_not(reg(op.source));
                } else if constexpr (std::is_same_v<T, LogicalNot>) {
                    reg(op.destination) = logical_not(reg(op.source));
                } else if constexpr (std::is_same_v<T, LogicalBinary>) {
                    reg(op.destination)
                        = logical_binary(op.operation, reg(op.lhs), reg(op.rhs));
                } else if constexpr (std::is_same_v<T, Shift>) {
                    reg(op.destination) = shift_value(op.operation,
                        reg(op.value), reg(op.amount), op.signed_amount);
                } else if constexpr (std::is_same_v<T, Extract>) {
                    reg(op.destination)
                        = extract_value(reg(op.source), op.offset, op.width);
                } else if constexpr (std::is_same_v<T, Insert>) {
                    reg(op.destination)
                        = insert_value(reg(op.target), reg(op.source), op.offset);
                } else if constexpr (std::is_same_v<T, Concatenate>) {
                    std::vector<PackedLogic4> operands;
                    operands.reserve(op.operands.size());
                    for (const auto operand : op.operands) {
                        operands.push_back(reg(operand));
                    }
                    reg(op.destination) = concatenate_values(operands, op.width);
                } else if constexpr (std::is_same_v<T, ConditionalSelect>) {
                    reg(op.destination) = conditional_value(reg(op.condition),
                        reg(op.when_true), reg(op.when_false));
                } else if constexpr (std::is_same_v<T, ConvertToTwoState>) {
                    const auto& source = reg(op.source);
                    auto converted = PackedLogic4(source.width(), Logic4::zero);
                    for (std::size_t bit = 0; bit < source.width(); ++bit) {
                        if (to_logic4(source.get_logic9(bit)) == Logic4::one) {
                            converted.set(bit, Logic4::one);
                        }
                    }
                    reg(op.destination) = std::move(converted);
                } else if constexpr (std::is_same_v<T, WriteUpdate>
                    || std::is_same_v<T, WriteBlocking>) {
                    record_write(op.signal, reg(op.source), std::nullopt);
                } else if constexpr (std::is_same_v<T, WriteUpdateSlice>
                    || std::is_same_v<T, WriteBlockingSlice>) {
                    record_write(op.signal, reg(op.source), op.offset);
                } else if constexpr (std::is_same_v<T, Jump>) {
                    next = op.target;
                } else if constexpr (std::is_same_v<T, Branch>) {
                    const auto& condition = reg(op.condition);
                    const auto value = condition.width() == 1U
                        ? condition.get(0) : Logic4::x;
                    if (value == Logic4::one) {
                        next = op.when_true;
                    } else if (value == Logic4::zero
                        || op.unknown_policy
                            == UnknownBranchPolicy::when_false) {
                        next = op.when_false;
                    } else {
                        throw std::logic_error(
                            "fused cone branch condition is unknown");
                    }
                } else {
                    throw std::logic_error(
                        "fused cone member contains an unsupported operation");
                }
            }, operation);
            pc = next;
        }
    }

    fused_cone_materializing = true;
    try {
        for (auto& commit : commits) {
            if (commit.offset) {
                commit_driver_slice(commit.process, commit.signal,
                    std::move(commit.value), *commit.offset);
            } else {
                commit_driver(commit.process, commit.signal,
                    std::move(commit.value));
            }
        }
    } catch (...) {
        fused_cone_materializing = false;
        throw;
    }
    fused_cone_materializing = false;
    cone.boundary_revisions.resize(spec.boundary_inputs.size());
    for (std::size_t index = 0U; index < spec.boundary_inputs.size(); ++index) {
        cone.boundary_revisions[index]
            = signal_value_revisions[spec.boundary_inputs[index]];
    }
    cone.materialized = true;
}

} // namespace fsim::runtime::simir
