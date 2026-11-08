// SPDX-License-Identifier: Apache-2.0
#include "sdf_net_delay_dual_write.hpp"

#include "lowerer_driver_regions.hpp"
#include "systemverilog_template_bindings.hpp"

#include <algorithm>
#include <optional>

namespace fsim::elaboration::dual_write {

using runtime::simir::Operation;
using runtime::simir::Process;
using runtime::simir::SignalId;

namespace {

template <class Type>
SignalId* target_if(Operation& operation)
{
    auto* const value = runtime::simir::operation_get_if<Type>(&operation);
    return value != nullptr ? &value->signal : nullptr;
}

template <class... Types>
SignalId* target_of(Operation& operation)
{
    SignalId* target { };
    (void)(((target = target_if<Types>(operation)) != nullptr) || ...);
    return target;
}

} // namespace

// The signal a write operation drives, or null when `operation` is not a
// write.
SignalId* write_target(Operation& operation)
{
    using namespace runtime::simir;
    return target_of<WriteBlocking, WriteUpdate, WriteAfter, WriteInertial,
        WriteDelayed, WriteNetDelay, WriteProjected, WriteProjectedWaveform,
        WriteBlockingSlice, WriteUpdateSlice, WriteAfterSlice,
        WriteInertialSlice, WriteProjectedSlice, WriteProjectedWaveformSlice,
        WriteBlockingDynamicSlice, WriteUpdateDynamicSlice,
        WriteAfterDynamicSlice, WriteBlockingDynamicPartSlice,
        WriteUpdateDynamicPartSlice, WriteAfterDynamicPartSlice,
        WriteInertialDynamicSlice, WriteInertialDynamicPartSlice,
        WriteProjectedDynamicSlice, WriteProjectedWaveformDynamicSlice>(
        operation);
}

// The positions of the operations of `process` that write `net`. False when
// the process connects the net through a switch, or (unless `allow_reads`)
// also reads the net or has an operation that names signals in a way the
// dual write does not cover. A writer that reads the net still contributes
// exactly what it writes, so with `allow_reads` its reads, waits and
// sensitivity on the net are left alone: it keeps reading the real net.
bool net_writes(const Process& process, const SignalId net,
    std::vector<std::size_t>& positions, const bool allow_vital_delay,
    const bool allow_reads)
{
    using namespace runtime::simir;
    positions.clear();
    if (process.switch_source == net || process.switch_target == net
        || process.switch_control == net) {
        return false;
    }
    if (!allow_reads
        && std::ranges::any_of(process.static_sensitivity,
            [&](const Sensitivity& each) { return each.signal == net; })) {
        return false;
    }
    for (std::size_t index = 0U; index < process.operations.size(); ++index) {
        auto operation = process.operations.expanded(index);
        if (const auto* const target = write_target(operation)) {
            if (*target == net) {
                positions.push_back(index);
            }
            continue;
        }
        if (allow_vital_delay) {
            // A VITAL delay call assigns its output signal; the dual write
            // gives it the mirror (VitalWireCall::mirrors).
            if (const auto* const delay
                = operation_get_if<VitalDelay>(&operation);
                delay != nullptr && delay->output == net) {
                positions.push_back(index);
                continue;
            }
        }
        if (allow_reads) {
            continue;
        }
        if (const auto* const wait = operation_get_if<WaitOn>(&operation)) {
            if (std::ranges::find(wait->signals, net) != wait->signals.end()) {
                return false;
            }
            continue;
        }
        bool reads { };
        if (!remap_operation_signals(operation,
                [&](SignalId& signal) {
                    reads = reads || signal == net;
                    return true;
                },
                std::nullopt)
            || reads) {
            return false;
        }
    }
    return !positions.empty();
}

// Puts each operation of `inserted` right after the operation at its
// position (positions ascending; several at one position keep their order)
// and moves every instruction index of the process to match: jump, call,
// branch and fork targets, disable sites and intervals, wait timeout origins
// and the static trigger regions. An index that named the operation after a
// position names it still, past the inserted operations. The body becomes the
// process's own, with its instance fields expanded.
void insert_after(Process& process,
    const std::vector<std::pair<std::size_t, Operation>>& inserted)
{
    using namespace runtime::simir;
    const auto size = process.operations.size();
    std::vector<InstructionIndex> moved(size + 1U);
    std::size_t next { };
    for (std::size_t index = 0U; index <= size; ++index) {
        while (next < inserted.size() && inserted[next].first < index) {
            ++next;
        }
        moved[index] = static_cast<InstructionIndex>(index + next);
    }
    const auto move = [&](InstructionIndex& index) {
        if (index <= size) {
            index = moved[index];
        }
    };
    OperationList operations;
    operations.reserve(size + inserted.size());
    next = 0U;
    for (std::size_t index = 0U; index < size; ++index) {
        auto operation = process.operations.expanded(index);
        if (auto* const jump = operation_get_if<Jump>(&operation)) {
            move(jump->target);
        } else if (auto* const call = operation_get_if<Call>(&operation)) {
            move(call->target);
            move(call->return_target);
        } else if (auto* const branch = operation_get_if<Branch>(&operation)) {
            move(branch->when_true);
            move(branch->when_false);
        } else if (auto* const fork = operation_get_if<Fork>(&operation)) {
            std::ranges::for_each(fork->branches, move);
        } else if (auto* const disable
            = operation_get_if<DisableFork>(&operation)) {
            if (disable->site) {
                move(*disable->site);
            }
        } else if (auto* const block
            = operation_get_if<DisableBlock>(&operation)) {
            move(block->begin);
            move(block->end);
        } else if (auto* const wait = operation_get_if<WaitOn>(&operation)) {
            if (wait->timeout_origin) {
                move(*wait->timeout_origin);
            }
        }
        operations.push_back(std::move(operation));
        for (; next < inserted.size() && inserted[next].first == index;
            ++next) {
            operations.push_back(inserted[next].second);
        }
    }
    process.operations = std::move(operations);
    std::vector<Process::StaticTriggerRegion> regions(
        process.static_trigger_regions.begin(),
        process.static_trigger_regions.end());
    for (auto& region : regions) {
        move(region.begin);
        move(region.end);
    }
    process.static_trigger_regions = std::move(regions);
}

} // namespace fsim::elaboration::dual_write
