// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhdl_psl.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace fsim::runtime {
namespace {

    template<typename Value>
    void reserve_append(std::vector<Value>& values)
    {
        if (values.size() != values.capacity()) {
            return;
        }
        const auto required = values.size() + 1U;
        const auto grown = values.capacity() > values.max_size() / 2U
            ? values.max_size()
            : 2U * values.capacity();
        values.reserve(std::max(required, grown));
    }

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
    std::size_t clock_binding { };
    std::size_t active_attempts { };
    bool enabled { true };
};

struct VhdlPslAttemptEngine::ActiveAttempt {
    std::size_t monitor { };
    std::size_t snapshot { };
    bool active { true };
};

struct VhdlPslAttemptEngine::ClockBinding {
    std::string identity;
    std::vector<VhdlPslClockedSample> samples;
    VhdlPslTruth previous { VhdlPslTruth::unknown };
    VhdlPslTruth current { VhdlPslTruth::unknown };
    bool current_present { };
    bool rising_edge { };
    bool falling_edge { };
    bool append_sample { };
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
    const auto clock = std::ranges::find(clock_bindings_, plan.clock_identity,
        &ClockBinding::identity);
    const auto new_clock = clock == clock_bindings_.end();
    const auto clock_binding = new_clock
        ? clock_bindings_.size()
        : static_cast<std::size_t>(clock - clock_bindings_.begin());
    const auto clock_storage = new_clock
        ? 2U * (sizeof(ClockBinding) + plan.clock_identity.size())
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
    reserve_append(monitors_);
    if (new_clock) {
        reserve_append(clock_bindings_);
        ClockBinding binding;
        binding.identity = plan.clock_identity;
        clock_bindings_.push_back(std::move(binding));
    }
    monitors_.push_back(Monitor { std::move(plan), clock_binding, 0U, true });
    ++enabled_monitor_count_;
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
    if (enabled) {
        ++enabled_monitor_count_;
    } else {
        --enabled_monitor_count_;
    }
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

bool VhdlPslAttemptEngine::clock_edge(const Monitor& monitor) const noexcept
{
    const auto& binding = clock_bindings_[monitor.clock_binding];
    return monitor.plan.edge == VhdlPslClockEdge::rising
        ? binding.rising_edge
        : monitor.plan.edge == VhdlPslClockEdge::falling
        ? binding.falling_edge
        : binding.rising_edge || binding.falling_edge;
}

void VhdlPslAttemptEngine::complete(ActiveAttempt& active,
    const VhdlPslAttemptOutcome outcome, const SimulationTick time,
    const std::uint64_t delta)
{
    auto& snapshot = attempts_.at(active.snapshot);
    snapshot.outcome = outcome;
    snapshot.end_time = time;
    snapshot.end_delta = delta;
    auto& monitor = monitors_[active.monitor];
    const auto& history = clock_bindings_[monitor.clock_binding].samples;
    snapshot.end_sample = history.empty() ? snapshot.start_sample
                                          : history.size() - 1U;
    active.active = false;
    --active_count_;
    --monitor.active_attempts;
    if (monitor.plan.complete) {
        try {
            monitor.plan.complete(snapshot);
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
    auto& history = clock_bindings_[monitor.clock_binding].samples;
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
    for (auto& binding : clock_bindings_) {
        binding.current_present = false;
        binding.rising_edge = false;
        binding.falling_edge = false;
        binding.append_sample = false;
        if (const auto current = clocks.find(binding.identity);
            current != clocks.end()) {
            binding.current = current->second;
            binding.current_present = true;
            if (binding.previous != VhdlPslTruth::unknown
                && binding.current != VhdlPslTruth::unknown) {
                binding.rising_edge = binding.previous
                        == VhdlPslTruth::false_value
                    && binding.current == VhdlPslTruth::true_value;
                binding.falling_edge = binding.previous
                        == VhdlPslTruth::true_value
                    && binding.current == VhdlPslTruth::false_value;
            }
        }
    }
    std::size_t spawning { };
    if (enabled_monitor_count_ != 0U) {
        for (auto& monitor : monitors_) {
            if (!monitor.enabled || !clock_edge(monitor)) {
                continue;
            }
            ++spawning;
            clock_bindings_[monitor.clock_binding].append_sample = true;
        }
    }
    std::size_t sampled_clocks { };
    for (const auto& binding : clock_bindings_) {
        if (!binding.append_sample) {
            continue;
        }
        ++sampled_clocks;
        if (binding.samples.size() == limits_.maximum_history_samples) {
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
    std::size_t additional_storage { };
    if (sampled_clocks != 0U) {
        if (2U * sizeof(VhdlPslClockedSample)
            > std::numeric_limits<std::size_t>::max() / sampled_clocks) {
            throw VhdlPslResourceError { VhdlPslResourceKind::storage_bytes,
                limits_.maximum_storage_bytes };
        }
        additional_storage += 2U * sizeof(VhdlPslClockedSample)
            * sampled_clocks;
        std::size_t value_bytes = sizeof(VhdlPslSampleValues)
            + 2U * sizeof(void*);
        for (const auto& [name, unused] : values) {
            (void)unused;
            value_bytes += sizeof(VhdlPslSampleValues::value_type)
                + 3U * sizeof(void*) + 2U * name.size();
        }
        if (value_bytes
            > std::numeric_limits<std::size_t>::max() - additional_storage) {
            throw VhdlPslResourceError { VhdlPslResourceKind::storage_bytes,
                limits_.maximum_storage_bytes };
        }
        additional_storage += value_bytes;
    }
    for (std::size_t index = 0U; index < monitors_.size(); ++index) {
        if (!monitors_[index].enabled || !clock_edge(monitors_[index])) {
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
    for (auto& binding : clock_bindings_) {
        if (binding.current_present) {
            binding.previous = binding.current;
        }
    }
    if (spawning == 0U && active_count_ == 0U) {
        return;
    }
    auto retained_values = sampled_clocks == 0U
        ? std::shared_ptr<const VhdlPslSampleValues> { }
        : std::make_shared<const VhdlPslSampleValues>(std::move(values));
    const auto& current_values = retained_values ? *retained_values : values;
    for (auto& binding : clock_bindings_) {
        if (!binding.append_sample) {
            continue;
        }
        binding.samples.push_back(
            VhdlPslClockedSample { time, delta, retained_values });
    }

    std::size_t evaluation_count { };
    for (std::size_t index = 0U; index < monitors_.size(); ++index) {
        auto& monitor = monitors_[index];
        if (!monitor.enabled) {
            continue;
        }
        const auto edge = clock_edge(monitor);
        if (!edge && monitor.active_attempts == 0U) {
            continue;
        }
        if (!edge) {
            evaluate_active(index, false, current_values, time, delta, false,
                evaluation_count);
            continue;
        }
        auto& history = clock_bindings_[monitor.clock_binding].samples;
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
        ++monitor.active_attempts;
        evaluate_active(index, true, *history.back().values, time, delta,
            false, evaluation_count);
    }
}

void VhdlPslAttemptEngine::finish(
    const SimulationTick time, const std::uint64_t delta)
{
    if (active_count_ == 0U) {
        return;
    }
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
    for (auto& binding : clock_bindings_) {
        binding.previous = VhdlPslTruth::unknown;
        binding.current = VhdlPslTruth::unknown;
        binding.current_present = false;
        binding.rising_edge = false;
        binding.falling_edge = false;
        binding.append_sample = false;
        binding.samples.clear();
    }
    for (auto& monitor : monitors_) {
        monitor.active_attempts = 0U;
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
