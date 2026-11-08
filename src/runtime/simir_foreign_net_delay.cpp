// SPDX-License-Identifier: Apache-2.0

// The delayed reach of a foreign write of a net into the group-mode SDF load
// copies of the net (Interpreter::add_foreign_net_delay_load). Both engines
// share it: it sits on the Interpreter's foreign write entry points, below
// which the interpreter and the v4 kernel diverge, and writes the copy
// through the same entry points a foreign client of the copy would use
// (deposit_kernel_owned, deposit_signal, force_signal, release_signal and the
// slice forms), which hand a kernel-owned copy to its slot.

#include "simir_events.hpp"
#include "simir_internal.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <optional>

namespace fsim::runtime::simir {

void Interpreter::add_foreign_net_delay_load(const SignalId net,
    const SignalId copy, std::vector<ForeignNetDelayGroup> groups)
{
    if (impl_->started) {
        throw std::logic_error { "a foreign net delay load is declared "
                                 "before the simulation starts" };
    }
    (void)impl_->get_signal(net);
    (void)impl_->get_signal(copy);
    Impl::ForeignNetDelayLoad load;
    load.copy = copy;
    load.groups = std::move(groups);
    impl_->foreign_net_delay_loads[net].push_back(std::move(load));
    if (!impl_->net_delay_force_before) {
        impl_->net_delay_force_before = [this](const SignalId signal) {
            return foreign_net_delay_before(signal);
        };
        impl_->net_delay_force_after = [this](const SignalId signal,
                                           const ForeignNetDelayKind kind,
                                           const std::optional<PackedLogic4>& before,
                                           const std::size_t offset,
                                           const std::size_t width) {
            foreign_net_delay_after(signal, kind, before, offset, width);
        };
    }
}

std::optional<PackedLogic4> Interpreter::foreign_net_delay_before(
    const SignalId signal) const
{
    if (impl_->foreign_net_delay_loads.empty() || !impl_->started
        || !impl_->foreign_net_delay_loads.contains(signal)) {
        return std::nullopt;
    }
    return signal_value_snapshot(signal);
}

namespace {

using Bits = std::vector<std::pair<std::uint32_t, Logic4>>;

// The run of consecutive bits of `bits` (ascending) that starts at `first`.
std::size_t run_end(const Bits& bits, const std::size_t first)
{
    auto end = first + 1U;
    while (end < bits.size() && bits[end].first == bits[end - 1U].first + 1U) {
        ++end;
    }
    return end;
}

} // namespace

void Interpreter::foreign_net_delay_after(const SignalId signal,
    const ForeignNetDelayKind kind, const std::optional<PackedLogic4>& before,
    const std::size_t offset, const std::size_t width)
{
    if (!before) {
        return;
    }
    auto& impl = *impl_;
    if (kind == ForeignNetDelayKind::release && impl.static_kernel
        && signal < impl.static_kernel_owned_signal.size()
        && impl.static_kernel_owned_signal[signal] != 0U) {
        // A released net the kernel owns holds its stored value until its
        // drivers run again (Active region): its value after the release,
        // and so the transition of each bit, is known in the Inactive region.
        rerun_kernel_drivers(signal);
        impl.scheduler.schedule_at(impl.scheduler.now(),
            SchedulerPhase::inactive, 0U,
            [this, signal, kept = *before, offset, width](Scheduler&) {
                foreign_net_delay_settle(signal,
                    ForeignNetDelayKind::release, kept, offset, width);
            });
        return;
    }
    foreign_net_delay_settle(signal, kind, *before, offset, width);
}

void Interpreter::foreign_net_delay_settle(const SignalId signal,
    const ForeignNetDelayKind kind, const PackedLogic4& before,
    const std::size_t offset, const std::size_t width)
{
    using Pending = Impl::ForeignNetDelayPending;
    auto& impl = *impl_;
    auto& loads = impl.foreign_net_delay_loads.at(signal);
    const auto after = signal_value_snapshot(signal);
    const auto now = impl.scheduler.now();
    const auto last_bit = std::min(offset + width, after.width());
    for (std::size_t index = 0U; index < loads.size(); ++index) {
        auto& load = loads[index];
        const auto current = signal_value_snapshot(load.copy);
        // The delay of each bit's change: the smallest over the groups that
        // drive the bit, each at its transition (a bit that does not change
        // takes none). A deposit that does not change the net reaches no
        // copy; a force and a release always do, to hold or free the copy.
        std::map<SimulationTick, Bits> chunks;
        for (auto bit = offset; bit < last_bit; ++bit) {
            const auto to = after.get(bit);
            std::optional<SimulationTick> on_way;
            auto from = kind == ForeignNetDelayKind::release
                ? before.get(bit)
                : current.get(bit);
            if (kind != ForeignNetDelayKind::release) {
                // A pending write is the value the bit is on its way to
                // (transport): the change starts from the last one.
                for (const auto& pending : load.pending) {
                    if (pending.kind == ForeignNetDelayKind::release
                        || (on_way && pending.time < *on_way)) {
                        continue;
                    }
                    for (const auto& [at, value] : pending.bits) {
                        if (at == bit) {
                            from = value;
                            on_way = pending.time;
                        }
                    }
                }
            }
            if (kind == ForeignNetDelayKind::deposit
                && (before.get(bit) == to || from == to)) {
                continue;
            }
            SimulationTick delay = std::numeric_limits<SimulationTick>::max();
            if (from == to) {
                // A bit already on its way to the value (a force statement
                // that forces it again as it re-evaluates) takes it when it
                // arrives, and the later write replaces the pending one.
                delay = on_way ? *on_way - now : 0U;
            } else {
                const auto transition
                    = Impl::PendingNetDelayWrites::transition(from, to);
                for (const auto& group : load.groups) {
                    if (bit < group.offset || bit - group.offset >= group.width) {
                        continue;
                    }
                    const auto put = impl.net_delay_overrides.find(group.driver);
                    const auto& table = put != impl.net_delay_overrides.end()
                        ? put->second
                        : group.table;
                    delay = std::min(delay, table.delay[transition]);
                }
                if (delay == std::numeric_limits<SimulationTick>::max()) {
                    delay = 0U;
                }
            }
            if (delay > std::numeric_limits<SimulationTick>::max() - now) {
                throw std::overflow_error {
                    "simulation time overflow while scheduling interconnect delay"
                };
            }
            chunks[delay].emplace_back(static_cast<std::uint32_t>(bit), to);
        }
        if (chunks.empty()) {
            continue;
        }
        // A write that matures at or after a pending one's time ends it
        // (transport): the bits it covers leave the pending writes.
        for (const auto& [delay, bits] : chunks) {
            for (auto& pending : load.pending) {
                if (pending.time < now + delay) {
                    continue;
                }
                std::erase_if(pending.bits, [&](const auto& entry) {
                    return std::ranges::any_of(bits, [&](const auto& each) {
                        return each.first == entry.first;
                    });
                });
            }
        }
        std::erase_if(load.pending,
            [](const Pending& pending) { return pending.bits.empty(); });
        for (auto& [delay, bits] : chunks) {
            if (delay == 0U) {
                foreign_net_delay_mature(signal, index, kind, bits);
                continue;
            }
            Pending later;
            later.serial = load.next_serial++;
            later.time = now + delay;
            later.kind = kind;
            later.bits = std::move(bits);
            const auto serial = later.serial;
            load.pending.push_back(std::move(later));
            impl.scheduler.schedule_internal_at(
                now + delay, SchedulerPhase::active, 0U,
                events::make<events::ForeignNetDelayMature>(
                    events::ForeignNetDelayMature::Payload {
                        this, signal, index, serial }));
        }
    }
}

void Interpreter::foreign_net_delay_mature(const SignalId signal,
    const std::size_t index, const ForeignNetDelayKind kind,
    const std::vector<std::pair<std::uint32_t, Logic4>>& written)
{
    const auto copy = impl_->foreign_net_delay_loads.at(signal).at(index).copy;
    auto bits = written;
    std::ranges::sort(bits);
    if (kind == ForeignNetDelayKind::deposit) {
        // The copy keeps the value until a driver update resolves it again.
        auto value = signal_value_snapshot(copy);
        for (const auto& [bit, level] : bits) {
            value.set(bit, level);
        }
        if (!impl_->deposit_kernel_owned(copy, value)) {
            deposit_signal(copy, std::move(value),
                ProcessSchedulingDomain::systemverilog);
        }
        return;
    }
    const auto full = signal_value_snapshot(copy).width();
    for (std::size_t first = 0U; first < bits.size();) {
        const auto end = run_end(bits, first);
        const auto low = bits[first].first;
        const auto count = end - first;
        if (kind == ForeignNetDelayKind::force) {
            PackedLogic4 value(count, Logic4::zero);
            for (std::size_t at = 0U; at < count; ++at) {
                value.set(at, bits[first + at].second);
            }
            if (count == full) {
                force_signal(copy, std::move(value),
                    ProcessSchedulingDomain::systemverilog);
            } else {
                force_signal_slice(copy, std::move(value), low);
            }
        } else if (signal_is_forced(copy)) {
            if (count == full) {
                release_signal(
                    copy, ProcessSchedulingDomain::systemverilog);
            } else {
                release_signal_slice(copy, low, count);
            }
        }
        first = end;
    }
    if (kind == ForeignNetDelayKind::release) {
        // A net the kernel owns returns to its drivers' value.
        rerun_kernel_drivers(copy);
    }
}

} // namespace fsim::runtime::simir
