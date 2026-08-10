// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/scheduler.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime {

enum class VhdlPslTruth : std::uint8_t {
    false_value,
    true_value,
    unknown,
};

enum class VhdlPslClockEdge : std::uint8_t {
    rising,
    falling,
    either,
};

enum class VhdlPslAttemptOutcome : std::uint8_t {
    pending,
    pass,
    failure,
    vacuous,
    aborted,
};

enum class VhdlPslDirectiveKind : std::uint8_t {
    assertion,
    assumption,
    restriction,
    cover,
};

using VhdlPslSampleValues
    = std::map<std::string, VhdlPslTruth, std::less<>>;

struct VhdlPslClockedSample {
    SimulationTick time { };
    std::uint64_t delta { };
    std::shared_ptr<const VhdlPslSampleValues> values;
};

struct VhdlPslEvaluationContext {
    std::span<const VhdlPslClockedSample> samples;
    std::size_t start_sample { };
    const VhdlPslSampleValues& current_values;
    bool clock_edge { };
    bool end_of_run { };
    std::size_t maximum_temporal_steps { 4'194'304U };
};

using VhdlPslEvaluator
    = std::function<VhdlPslAttemptOutcome(const VhdlPslEvaluationContext&)>;

struct VhdlPslAttemptSnapshot;
using VhdlPslCompletionHook
    = std::function<void(const VhdlPslAttemptSnapshot&)>;

struct VhdlPslMonitorPlan {
    std::string identity;
    std::string instance_identity;
    std::uint32_t source_span { };
    std::uint32_t slot { };
    VhdlPslDirectiveKind directive_kind { VhdlPslDirectiveKind::assertion };
    std::string clock_identity;
    VhdlPslClockEdge edge { VhdlPslClockEdge::rising };
    VhdlPslEvaluator evaluate;
    VhdlPslCompletionHook complete;
    bool strong { true };
};

struct VhdlPslAttemptSnapshot {
    std::uint64_t attempt { };
    std::string monitor;
    std::string instance_identity;
    std::uint32_t source_span { };
    std::uint32_t slot { };
    VhdlPslDirectiveKind directive_kind { VhdlPslDirectiveKind::assertion };
    std::string clock_identity;
    std::size_t start_sample { };
    std::size_t end_sample { };
    SimulationTick start_time { };
    SimulationTick end_time { };
    std::uint64_t end_delta { };
    VhdlPslAttemptOutcome outcome { VhdlPslAttemptOutcome::pending };

    friend bool operator==(const VhdlPslAttemptSnapshot&,
        const VhdlPslAttemptSnapshot&)
        = default;
};

struct VhdlPslExecutionLimits {
    std::size_t maximum_monitors { 4'096U };
    std::size_t maximum_history_samples { 1'048'576U };
    std::size_t maximum_active_attempts { 1'048'576U };
    std::size_t maximum_lifetime_attempts { 4'194'304U };
    std::size_t maximum_evaluations_per_observation { 4'194'304U };
    std::size_t maximum_temporal_steps_per_evaluation { 4'194'304U };
    std::size_t maximum_storage_bytes { 512U * 1024U * 1024U };
};

enum class VhdlPslResourceKind : std::uint8_t {
    monitors,
    history_samples,
    active_attempts,
    lifetime_attempts,
    evaluations,
    temporal_steps,
    storage_bytes,
};

class VhdlPslResourceError final : public std::runtime_error {
public:
    VhdlPslResourceError(VhdlPslResourceKind kind, std::size_t limit);

    [[nodiscard]] VhdlPslResourceKind kind() const noexcept { return kind_; }
    [[nodiscard]] std::size_t limit() const noexcept { return limit_; }

private:
    VhdlPslResourceKind kind_;
    std::size_t limit_;
};

class VhdlPslAttemptEngine {
public:
    explicit VhdlPslAttemptEngine(VhdlPslExecutionLimits limits = { });
    ~VhdlPslAttemptEngine();
    VhdlPslAttemptEngine(const VhdlPslAttemptEngine&) = delete;
    VhdlPslAttemptEngine& operator=(const VhdlPslAttemptEngine&) = delete;

    std::size_t add_monitor(VhdlPslMonitorPlan plan);
    void set_enabled(std::string_view identity, bool enabled);
    void observe(const std::map<std::string, VhdlPslTruth, std::less<>>& clocks,
        VhdlPslSampleValues values, SimulationTick time,
        std::uint64_t delta);
    void finish(SimulationTick time, std::uint64_t delta);
    void reset() noexcept;

    [[nodiscard]] const std::vector<VhdlPslAttemptSnapshot>& attempts() const
        noexcept;
    [[nodiscard]] std::size_t active_attempt_count() const noexcept;
    [[nodiscard]] std::size_t storage_bytes() const noexcept;

private:
    struct Monitor;
    struct ActiveAttempt;

    [[nodiscard]] bool clock_edge(const Monitor& monitor,
        VhdlPslTruth previous, VhdlPslTruth current) const noexcept;
    void evaluate_active(std::size_t monitor, bool edge,
        const VhdlPslSampleValues& values, SimulationTick time,
        std::uint64_t delta, bool end_of_run,
        std::size_t& evaluation_count);
    void complete(ActiveAttempt& active, VhdlPslAttemptOutcome outcome,
        SimulationTick time, std::uint64_t delta);

    VhdlPslExecutionLimits limits_;
    std::vector<Monitor> monitors_;
    std::vector<ActiveAttempt> active_;
    std::vector<VhdlPslAttemptSnapshot> attempts_;
    std::map<std::string, VhdlPslTruth, std::less<>> previous_clocks_;
    std::map<std::string, std::vector<VhdlPslClockedSample>, std::less<>>
        histories_;
    std::uint64_t next_attempt_ { 1U };
    std::size_t active_count_ { };
    std::size_t monitor_storage_bytes_ { };
    std::size_t storage_bytes_ { };
    SimulationTick last_time_ { };
    std::uint64_t last_delta_ { };
};

} // namespace fsim::runtime
