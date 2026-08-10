// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhdl_psl.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace fsim::runtime {
namespace {

    [[nodiscard]] std::string resource_message(
        const VhdlPslResourceKind kind, const std::size_t limit)
    {
        const auto resource = kind == VhdlPslResourceKind::monitors
            ? "monitor"
            : kind == VhdlPslResourceKind::history_samples
            ? "history-sample"
            : kind == VhdlPslResourceKind::active_attempts
            ? "active-attempt"
            : kind == VhdlPslResourceKind::lifetime_attempts
            ? "lifetime-attempt"
            : kind == VhdlPslResourceKind::temporal_steps
            ? "per-evaluation temporal-step"
            : kind == VhdlPslResourceKind::storage_bytes
            ? "owned-storage byte"
            : "per-observation evaluation";
        return "VHDL PSL " + std::string { resource } + " work limit of "
            + std::to_string(limit) + " exceeded";
    }

} // namespace

struct VhdlPslAttemptEngine::Monitor {
    VhdlPslMonitorPlan plan;
    bool enabled { true };
};

struct VhdlPslAttemptEngine::ActiveAttempt {
    std::size_t monitor { };
    std::size_t snapshot { };
    bool active { true };
};

VhdlPslAttemptEngine::~VhdlPslAttemptEngine() = default;

VhdlPslResourceError::VhdlPslResourceError(
    const VhdlPslResourceKind kind, const std::size_t limit)
    : std::runtime_error(resource_message(kind, limit))
    , kind_(kind)
    , limit_(limit)
{
}

VhdlPslAttemptEngine::VhdlPslAttemptEngine(VhdlPslExecutionLimits limits)
    : limits_(limits)
{
    if (limits_.maximum_monitors == 0U
        || limits_.maximum_history_samples == 0U
        || limits_.maximum_active_attempts == 0U
        || limits_.maximum_lifetime_attempts == 0U
        || limits_.maximum_evaluations_per_observation == 0U
        || limits_.maximum_temporal_steps_per_evaluation == 0U
        || limits_.maximum_storage_bytes == 0U) {
        throw std::invalid_argument("VHDL PSL execution limits must be positive");
    }
}

std::size_t VhdlPslAttemptEngine::add_monitor(VhdlPslMonitorPlan plan)
{
    if (monitors_.size() == limits_.maximum_monitors) {
        throw VhdlPslResourceError {
            VhdlPslResourceKind::monitors, limits_.maximum_monitors
        };
    }
    if (plan.identity.empty() || plan.clock_identity.empty()
        || !plan.evaluate) {
        throw std::invalid_argument(
            "a VHDL PSL monitor requires identity, clock, and evaluator");
    }
    if (std::ranges::any_of(monitors_, [&](const Monitor& monitor) {
            return monitor.plan.identity == plan.identity;
        })) {
        throw std::invalid_argument("duplicate VHDL PSL monitor identity");
    }
    const auto new_clock = !histories_.contains(plan.clock_identity);
    const auto clock_storage = new_clock
        ? 2U * (3U * sizeof(void*) + sizeof(std::string) + plan.clock_identity.size())
            + sizeof(std::vector<VhdlPslClockedSample>)
        : 0U;
    // The factor of two covers geometric vector-capacity growth. The fixed
    // allowance covers the type-erased evaluator and allocator bookkeeping,
    // whose exact allocation size is not observable through std::function.
    const auto additional = 2U * sizeof(Monitor) + 512U
        + 2U * (plan.identity.size() + plan.instance_identity.size() + plan.clock_identity.size())
        + clock_storage;
    if (additional > limits_.maximum_storage_bytes - storage_bytes_) {
        throw VhdlPslResourceError { VhdlPslResourceKind::storage_bytes,
            limits_.maximum_storage_bytes };
    }
    monitors_.push_back(Monitor { std::move(plan), true });
    if (new_clock) {
        const auto& identity = monitors_.back().plan.clock_identity;
        histories_.try_emplace(identity);
        previous_clocks_.try_emplace(identity, VhdlPslTruth::unknown);
    }
    monitor_storage_bytes_ += additional;
    storage_bytes_ += additional;
    return monitors_.size() - 1U;
}

void VhdlPslAttemptEngine::set_enabled(
    const std::string_view identity, const bool enabled)
{
    const auto found = std::ranges::find_if(monitors_, [&](const Monitor& monitor) {
        return monitor.plan.identity == identity;
    });
    if (found == monitors_.end()) {
        throw std::out_of_range("unknown VHDL PSL monitor identity");
    }
    if (found->enabled == enabled) {
        return;
    }
    found->enabled = enabled;
    if (!enabled) {
        const auto index = static_cast<std::size_t>(found - monitors_.begin());
        for (auto& active : active_) {
            if (active.active && active.monitor == index) {
                complete(active, VhdlPslAttemptOutcome::aborted, last_time_,
                    last_delta_);
            }
        }
    }
}

bool VhdlPslAttemptEngine::clock_edge(const Monitor& monitor,
    const VhdlPslTruth previous, const VhdlPslTruth current) const noexcept
{
    if (previous == VhdlPslTruth::unknown
        || current == VhdlPslTruth::unknown) {
        return false;
    }
    const auto rising = previous == VhdlPslTruth::false_value
        && current == VhdlPslTruth::true_value;
    const auto falling = previous == VhdlPslTruth::true_value
        && current == VhdlPslTruth::false_value;
    return monitor.plan.edge == VhdlPslClockEdge::rising
        ? rising
        : monitor.plan.edge == VhdlPslClockEdge::falling ? falling
                                                         : rising || falling;
}

void VhdlPslAttemptEngine::complete(ActiveAttempt& active,
    const VhdlPslAttemptOutcome outcome, const SimulationTick time,
    const std::uint64_t delta)
{
    auto& snapshot = attempts_.at(active.snapshot);
    snapshot.outcome = outcome;
    snapshot.end_time = time;
    snapshot.end_delta = delta;
    const auto& history = histories_.at(snapshot.clock_identity);
    snapshot.end_sample = history.empty() ? snapshot.start_sample
                                          : history.size() - 1U;
    active.active = false;
    --active_count_;
    if (monitors_[active.monitor].plan.complete) {
        try {
            monitors_[active.monitor].plan.complete(snapshot);
        } catch (...) {
            // Assertion observers cannot corrupt or roll back committed
            // simulation state. The owning application may retain its own
            // contained observer-failure record.
        }
    }
}

void VhdlPslAttemptEngine::evaluate_active(const std::size_t monitor_index,
    const bool edge, const VhdlPslSampleValues& values,
    const SimulationTick time, const std::uint64_t delta,
    const bool end_of_run, std::size_t& evaluation_count)
{
    auto& monitor = monitors_.at(monitor_index);
    auto& history = histories_.at(monitor.plan.clock_identity);
    for (auto& active : active_) {
        if (!active.active || active.monitor != monitor_index) {
            continue;
        }
        if (++evaluation_count
            > limits_.maximum_evaluations_per_observation) {
            throw VhdlPslResourceError { VhdlPslResourceKind::evaluations,
                limits_.maximum_evaluations_per_observation };
        }
        auto outcome = monitor.plan.evaluate(VhdlPslEvaluationContext {
            history, attempts_[active.snapshot].start_sample, values, edge,
            end_of_run, limits_.maximum_temporal_steps_per_evaluation });
        if (end_of_run && outcome == VhdlPslAttemptOutcome::pending) {
            outcome = monitor.plan.strong ? VhdlPslAttemptOutcome::failure
                                          : VhdlPslAttemptOutcome::vacuous;
        }
        if (outcome != VhdlPslAttemptOutcome::pending) {
            complete(active, outcome, time, delta);
        }
    }
}

void VhdlPslAttemptEngine::observe(
    const std::map<std::string, VhdlPslTruth, std::less<>>& clocks,
    VhdlPslSampleValues values, const SimulationTick time,
    const std::uint64_t delta)
{
    std::vector<bool> edges(monitors_.size());
    for (std::size_t index = 0U; index < monitors_.size(); ++index) {
        const auto& monitor = monitors_[index];
        const auto current = clocks.find(monitor.plan.clock_identity);
        const auto previous = previous_clocks_.find(monitor.plan.clock_identity);
        if (current != clocks.end() && previous != previous_clocks_.end()) {
            edges[index] = clock_edge(
                monitor, previous->second, current->second);
        }
    }
    std::map<std::string, bool, std::less<>> edge_histories;
    std::size_t spawning { };
    for (std::size_t index = 0U; index < monitors_.size(); ++index) {
        if (!monitors_[index].enabled || !edges[index]) {
            continue;
        }
        ++spawning;
        edge_histories.emplace(monitors_[index].plan.clock_identity, true);
    }
    for (const auto& [clock, unused] : edge_histories) {
        (void)unused;
        if (histories_.at(clock).size()
            == limits_.maximum_history_samples) {
            throw VhdlPslResourceError {
                VhdlPslResourceKind::history_samples,
                limits_.maximum_history_samples
            };
        }
    }
    if (spawning > limits_.maximum_active_attempts - active_count_) {
        throw VhdlPslResourceError { VhdlPslResourceKind::active_attempts,
            limits_.maximum_active_attempts };
    }
    if (spawning > limits_.maximum_lifetime_attempts - attempts_.size()) {
        throw VhdlPslResourceError { VhdlPslResourceKind::lifetime_attempts,
            limits_.maximum_lifetime_attempts };
    }
    if (active_count_ + spawning
        > limits_.maximum_evaluations_per_observation) {
        throw VhdlPslResourceError { VhdlPslResourceKind::evaluations,
            limits_.maximum_evaluations_per_observation };
    }
    // Histories share one immutable value map per observation. Charge each
    // history's possible vector-capacity growth and the shared ordered-map
    // nodes/string storage before any state is published.
    std::size_t value_bytes = sizeof(VhdlPslSampleValues)
        + 2U * sizeof(void*);
    for (const auto& [name, unused] : values) {
        (void)unused;
        value_bytes += sizeof(VhdlPslSampleValues::value_type)
            + 3U * sizeof(void*) + 2U * name.size();
    }
    std::size_t additional_storage { };
    if (!edge_histories.empty()
        && 2U * sizeof(VhdlPslClockedSample)
            > (std::numeric_limits<std::size_t>::max() - additional_storage)
                / edge_histories.size()) {
        throw VhdlPslResourceError { VhdlPslResourceKind::storage_bytes,
            limits_.maximum_storage_bytes };
    }
    additional_storage += 2U * sizeof(VhdlPslClockedSample)
        * edge_histories.size();
    if (!edge_histories.empty()) {
        if (value_bytes
            > std::numeric_limits<std::size_t>::max() - additional_storage) {
            throw VhdlPslResourceError { VhdlPslResourceKind::storage_bytes,
                limits_.maximum_storage_bytes };
        }
        additional_storage += value_bytes;
    }
    for (std::size_t index = 0U; index < monitors_.size(); ++index) {
        if (!monitors_[index].enabled || !edges[index]) {
            continue;
        }
        const auto& plan = monitors_[index].plan;
        const auto attempt_bytes = 2U * sizeof(ActiveAttempt)
            + 2U * sizeof(VhdlPslAttemptSnapshot)
            + 2U * (plan.identity.size() + plan.instance_identity.size() + plan.clock_identity.size());
        if (attempt_bytes
            > std::numeric_limits<std::size_t>::max() - additional_storage) {
            throw VhdlPslResourceError { VhdlPslResourceKind::storage_bytes,
                limits_.maximum_storage_bytes };
        }
        additional_storage += attempt_bytes;
    }
    if (additional_storage > limits_.maximum_storage_bytes - storage_bytes_) {
        throw VhdlPslResourceError { VhdlPslResourceKind::storage_bytes,
            limits_.maximum_storage_bytes };
    }
    storage_bytes_ += additional_storage;
    last_time_ = time;
    last_delta_ = delta;
    for (auto& [clock, previous] : previous_clocks_) {
        if (const auto current = clocks.find(clock); current != clocks.end()) {
            previous = current->second;
        }
    }
    auto retained_values = edge_histories.empty()
        ? std::shared_ptr<const VhdlPslSampleValues> { }
        : std::make_shared<const VhdlPslSampleValues>(std::move(values));
    const auto& current_values = retained_values ? *retained_values : values;
    for (const auto& [clock, unused] : edge_histories) {
        (void)unused;
        histories_.at(clock).push_back(
            VhdlPslClockedSample { time, delta, retained_values });
    }

    std::size_t evaluation_count { };
    for (std::size_t index = 0U; index < monitors_.size(); ++index) {
        auto& monitor = monitors_[index];
        if (!monitor.enabled) {
            continue;
        }
        if (!edges[index]) {
            evaluate_active(index, false, current_values, time, delta, false,
                evaluation_count);
            continue;
        }
        auto& history = histories_[monitor.plan.clock_identity];
        VhdlPslAttemptSnapshot snapshot;
        snapshot.attempt = next_attempt_++;
        snapshot.monitor = monitor.plan.identity;
        snapshot.instance_identity = monitor.plan.instance_identity;
        snapshot.source_span = monitor.plan.source_span;
        snapshot.slot = monitor.plan.slot;
        snapshot.directive_kind = monitor.plan.directive_kind;
        snapshot.clock_identity = monitor.plan.clock_identity;
        snapshot.start_sample = history.size() - 1U;
        snapshot.end_sample = snapshot.start_sample;
        snapshot.start_time = time;
        snapshot.end_time = time;
        snapshot.end_delta = delta;
        attempts_.push_back(std::move(snapshot));
        active_.push_back(
            ActiveAttempt { index, attempts_.size() - 1U, true });
        ++active_count_;
        evaluate_active(index, true, *history.back().values, time, delta,
            false, evaluation_count);
    }
}

void VhdlPslAttemptEngine::finish(
    const SimulationTick time, const std::uint64_t delta)
{
    const VhdlPslSampleValues empty;
    std::size_t evaluation_count { };
    for (std::size_t index = 0U; index < monitors_.size(); ++index) {
        evaluate_active(index, false, empty, time, delta, true,
            evaluation_count);
    }
}

void VhdlPslAttemptEngine::reset() noexcept
{
    active_.clear();
    attempts_.clear();
    for (auto& [unused, previous] : previous_clocks_) {
        (void)unused;
        previous = VhdlPslTruth::unknown;
    }
    for (auto& [unused, history] : histories_) {
        (void)unused;
        history.clear();
    }
    next_attempt_ = 1U;
    active_count_ = 0U;
    storage_bytes_ = monitor_storage_bytes_;
    last_time_ = 0U;
    last_delta_ = 0U;
}

const std::vector<VhdlPslAttemptSnapshot>& VhdlPslAttemptEngine::attempts() const
    noexcept
{
    return attempts_;
}

std::size_t VhdlPslAttemptEngine::active_attempt_count() const noexcept
{
    return active_count_;
}

std::size_t VhdlPslAttemptEngine::storage_bytes() const noexcept
{
    return storage_bytes_;
}

} // namespace fsim::runtime
