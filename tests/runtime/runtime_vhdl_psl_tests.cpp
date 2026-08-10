// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/vhdl_psl.hpp"

#include <stdexcept>
#include <string_view>

namespace fsim::tests::runtime {
namespace {

    void require(const bool condition, const std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error(std::string { message });
        }
    }

    [[nodiscard]] fsim::runtime::VhdlPslTruth value(
        const fsim::runtime::VhdlPslSampleValues& values,
        const std::string_view name)
    {
        const auto found = values.find(name);
        return found == values.end()
            ? fsim::runtime::VhdlPslTruth::unknown
            : found->second;
    }

    [[nodiscard]] fsim::runtime::VhdlPslMonitorPlan delayed_monitor(
        std::string identity)
    {
        using namespace fsim::runtime;
        VhdlPslMonitorPlan plan;
        plan.identity = std::move(identity);
        plan.clock_identity = "rise:clk";
        plan.evaluate = [](const VhdlPslEvaluationContext& context) {
            if (value(context.current_values, "reset")
                == VhdlPslTruth::true_value) {
                return VhdlPslAttemptOutcome::aborted;
            }
            const auto& first = *context.samples[context.start_sample].values;
            if (value(first, "request") != VhdlPslTruth::true_value) {
                return VhdlPslAttemptOutcome::vacuous;
            }
            if (context.samples.size() <= context.start_sample + 1U) {
                return VhdlPslAttemptOutcome::pending;
            }
            return value(*context.samples[context.start_sample + 1U].values,
                       "acknowledge")
                    == VhdlPslTruth::true_value
                ? VhdlPslAttemptOutcome::pass
                : VhdlPslAttemptOutcome::failure;
        };
        return plan;
    }

    void observe(fsim::runtime::VhdlPslAttemptEngine& engine,
        const fsim::runtime::VhdlPslTruth clock,
        const fsim::runtime::VhdlPslTruth request,
        const fsim::runtime::VhdlPslTruth acknowledge,
        const fsim::runtime::VhdlPslTruth reset, const std::uint64_t time)
    {
        using namespace fsim::runtime;
        engine.observe({ { "rise:clk", clock } },
            { { "request", request }, { "acknowledge", acknowledge },
                { "reset", reset } },
            time, 0U);
    }

} // namespace

void test_vhdl_psl_attempt_engine()
{
    using namespace fsim::runtime;
    VhdlPslAttemptEngine engine;
    engine.add_monitor(delayed_monitor("response"));

    observe(engine, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, 0U);
    observe(engine, VhdlPslTruth::true_value, VhdlPslTruth::true_value,
        VhdlPslTruth::false_value, VhdlPslTruth::false_value, 1U);
    observe(engine, VhdlPslTruth::false_value, VhdlPslTruth::true_value,
        VhdlPslTruth::false_value, VhdlPslTruth::false_value, 2U);
    observe(engine, VhdlPslTruth::true_value, VhdlPslTruth::true_value,
        VhdlPslTruth::true_value, VhdlPslTruth::false_value, 3U);
    observe(engine, VhdlPslTruth::false_value, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, VhdlPslTruth::false_value, 4U);
    observe(engine, VhdlPslTruth::true_value, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, VhdlPslTruth::false_value, 5U);

    require(engine.attempts().size() == 3U,
        "each rising edge starts one deterministically ordered PSL attempt");
    require(engine.attempts()[0].outcome == VhdlPslAttemptOutcome::pass
            && engine.attempts()[1].outcome
                == VhdlPslAttemptOutcome::failure
            && engine.attempts()[2].outcome
                == VhdlPslAttemptOutcome::vacuous,
        "overlapping attempts retain pass, failure, and vacuous outcomes");
    require(engine.attempts()[0].attempt == 1U
            && engine.attempts()[1].attempt == 2U
            && engine.attempts()[0].start_sample == 0U
            && engine.attempts()[0].end_sample == 1U,
        "attempt and sampled-clock identities are stable");

    VhdlPslAttemptEngine asynchronous;
    asynchronous.add_monitor(delayed_monitor("async_abort"));
    observe(asynchronous, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, 10U);
    observe(asynchronous, VhdlPslTruth::true_value,
        VhdlPslTruth::true_value, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, 11U);
    observe(asynchronous, VhdlPslTruth::true_value,
        VhdlPslTruth::true_value, VhdlPslTruth::false_value,
        VhdlPslTruth::true_value, 12U);
    require(asynchronous.attempts().front().outcome
                == VhdlPslAttemptOutcome::aborted
            && asynchronous.attempts().front().end_time == 12U,
        "an asynchronous abort is observed between clock edges");

    VhdlPslAttemptEngine completion;
    auto strong = delayed_monitor("strong");
    auto weak = delayed_monitor("weak");
    weak.strong = false;
    completion.add_monitor(std::move(strong));
    completion.add_monitor(std::move(weak));
    observe(completion, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, 20U);
    observe(completion, VhdlPslTruth::true_value,
        VhdlPslTruth::true_value, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, 21U);
    require(completion.attempts().size() == 2U
            && completion.attempts()[0].start_sample == 0U
            && completion.attempts()[1].start_sample == 0U,
        "monitors on one clock share one sampled history");
    completion.finish(22U, 0U);
    require(completion.attempts()[0].outcome
                == VhdlPslAttemptOutcome::failure
            && completion.attempts()[1].outcome
                == VhdlPslAttemptOutcome::vacuous,
        "finite-run completion distinguishes strong failure from weak vacuity");

    VhdlPslAttemptEngine disabled;
    disabled.add_monitor(delayed_monitor("disabled"));
    observe(disabled, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, 30U);
    observe(disabled, VhdlPslTruth::true_value,
        VhdlPslTruth::true_value, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, 31U);
    disabled.set_enabled("disabled", false);
    require(disabled.attempts().front().outcome
                == VhdlPslAttemptOutcome::aborted
            && disabled.active_attempt_count() == 0U,
        "disabling a monitor cleans up every active attempt");
    disabled.reset();
    require(disabled.attempts().empty()
            && disabled.active_attempt_count() == 0U,
        "reset releases histories, outcomes, and active attempts");

    VhdlPslAttemptEngine unknown_clock;
    auto immediate = delayed_monitor("unknown-clock");
    immediate.evaluate = [](const VhdlPslEvaluationContext&) {
        return VhdlPslAttemptOutcome::pass;
    };
    unknown_clock.add_monitor(std::move(immediate));
    observe(unknown_clock, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, 40U);
    observe(unknown_clock, VhdlPslTruth::unknown,
        VhdlPslTruth::false_value, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, 41U);
    observe(unknown_clock, VhdlPslTruth::true_value,
        VhdlPslTruth::false_value, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, 42U);
    require(unknown_clock.attempts().empty(),
        "an unknown clock value cannot manufacture an edge");

    const VhdlPslSampleValues* left_values { };
    const VhdlPslSampleValues* right_values { };
    VhdlPslAttemptEngine shared_observation;
    auto left = delayed_monitor("left-root");
    left.clock_identity = "rise:left";
    left.evaluate = [&](const VhdlPslEvaluationContext& context) {
        left_values = context.samples.back().values.get();
        return VhdlPslAttemptOutcome::pass;
    };
    auto right = delayed_monitor("right-root");
    right.clock_identity = "rise:right";
    right.evaluate = [&](const VhdlPslEvaluationContext& context) {
        right_values = context.samples.back().values.get();
        return VhdlPslAttemptOutcome::pass;
    };
    shared_observation.add_monitor(std::move(left));
    shared_observation.add_monitor(std::move(right));
    shared_observation.observe(
        { { "rise:left", VhdlPslTruth::false_value },
            { "rise:right", VhdlPslTruth::false_value } },
        { { "left::sample", VhdlPslTruth::true_value },
            { "right::sample", VhdlPslTruth::false_value } },
        50U, 0U);
    shared_observation.observe(
        { { "rise:left", VhdlPslTruth::true_value },
            { "rise:right", VhdlPslTruth::true_value } },
        { { "left::sample", VhdlPslTruth::true_value },
            { "right::sample", VhdlPslTruth::false_value } },
        51U, 0U);
    require(left_values != nullptr && left_values == right_values,
        "simultaneous root clocks share one immutable value snapshot");
}

void test_vhdl_psl_attempt_limits()
{
    using namespace fsim::runtime;
    VhdlPslExecutionLimits limits;
    limits.maximum_monitors = 1U;
    VhdlPslAttemptEngine monitor_limited(limits);
    monitor_limited.add_monitor(delayed_monitor("first"));
    try {
        monitor_limited.add_monitor(delayed_monitor("second"));
        require(false, "the PSL monitor ceiling must reject before mutation");
    } catch (const VhdlPslResourceError& error) {
        require(error.kind() == VhdlPslResourceKind::monitors,
            "monitor exhaustion has a distinct resource identity");
    }

    limits = { };
    limits.maximum_active_attempts = 1U;
    VhdlPslAttemptEngine active_limited(limits);
    active_limited.add_monitor(delayed_monitor("active"));
    observe(active_limited, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, 0U);
    observe(active_limited, VhdlPslTruth::true_value,
        VhdlPslTruth::true_value, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, 1U);
    observe(active_limited, VhdlPslTruth::false_value,
        VhdlPslTruth::true_value, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, 2U);
    try {
        observe(active_limited, VhdlPslTruth::true_value,
            VhdlPslTruth::true_value, VhdlPslTruth::true_value,
            VhdlPslTruth::false_value, 3U);
        require(false,
            "the active-attempt ceiling must reject before starting another");
    } catch (const VhdlPslResourceError& error) {
        require(error.kind() == VhdlPslResourceKind::active_attempts,
            "active attempt exhaustion has a distinct resource identity");
    }

    limits = { };
    limits.maximum_history_samples = 1U;
    VhdlPslAttemptEngine history_limited(limits);
    auto immediate = delayed_monitor("history");
    immediate.evaluate = [](const VhdlPslEvaluationContext&) {
        return VhdlPslAttemptOutcome::pass;
    };
    history_limited.add_monitor(std::move(immediate));
    observe(history_limited, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, 0U);
    observe(history_limited, VhdlPslTruth::true_value,
        VhdlPslTruth::false_value, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, 1U);
    observe(history_limited, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, 2U);
    try {
        observe(history_limited, VhdlPslTruth::true_value,
            VhdlPslTruth::false_value, VhdlPslTruth::false_value,
            VhdlPslTruth::false_value, 3U);
        require(false, "the sampled-history ceiling must be enforced");
    } catch (const VhdlPslResourceError& error) {
        require(error.kind() == VhdlPslResourceKind::history_samples,
            "history exhaustion has a distinct resource identity");
    }

    limits = { };
    limits.maximum_lifetime_attempts = 1U;
    VhdlPslAttemptEngine lifetime_limited(limits);
    immediate = delayed_monitor("lifetime");
    immediate.evaluate = [](const VhdlPslEvaluationContext&) {
        return VhdlPslAttemptOutcome::pass;
    };
    lifetime_limited.add_monitor(std::move(immediate));
    observe(lifetime_limited, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, 0U);
    observe(lifetime_limited, VhdlPslTruth::true_value,
        VhdlPslTruth::false_value, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, 1U);
    observe(lifetime_limited, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, 2U);
    try {
        observe(lifetime_limited, VhdlPslTruth::true_value,
            VhdlPslTruth::false_value, VhdlPslTruth::false_value,
            VhdlPslTruth::false_value, 3U);
        require(false, "the lifetime-attempt ceiling must be enforced");
    } catch (const VhdlPslResourceError& error) {
        require(error.kind() == VhdlPslResourceKind::lifetime_attempts
                && lifetime_limited.attempts().size() == 1U,
            "lifetime exhaustion rejects without a partial attempt");
    }

    limits = { };
    limits.maximum_evaluations_per_observation = 1U;
    VhdlPslAttemptEngine evaluation_limited(limits);
    evaluation_limited.add_monitor(delayed_monitor("evaluation-a"));
    auto second_clock = delayed_monitor("evaluation-b");
    evaluation_limited.add_monitor(std::move(second_clock));
    observe(evaluation_limited, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, 0U);
    try {
        observe(evaluation_limited, VhdlPslTruth::true_value,
            VhdlPslTruth::true_value, VhdlPslTruth::false_value,
            VhdlPslTruth::false_value, 1U);
        require(false, "the per-observation work ceiling must be enforced");
    } catch (const VhdlPslResourceError& error) {
        require(error.kind() == VhdlPslResourceKind::evaluations
                && evaluation_limited.attempts().empty(),
            "evaluation exhaustion rejects transactionally before sampling");
    }

    limits = { };
    limits.maximum_temporal_steps_per_evaluation = 17U;
    VhdlPslAttemptEngine temporal_limited(limits);
    bool observed_temporal_limit { };
    immediate = delayed_monitor("temporal");
    immediate.evaluate = [&](const VhdlPslEvaluationContext& context) {
        observed_temporal_limit = context.maximum_temporal_steps == 17U;
        return VhdlPslAttemptOutcome::pass;
    };
    temporal_limited.add_monitor(std::move(immediate));
    observe(temporal_limited, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, 0U);
    observe(temporal_limited, VhdlPslTruth::true_value,
        VhdlPslTruth::false_value, VhdlPslTruth::false_value,
        VhdlPslTruth::false_value, 1U);
    require(observed_temporal_limit,
        "the configured inner temporal-work ceiling reaches each evaluator");

    limits = { };
    limits.maximum_storage_bytes = 1U;
    VhdlPslAttemptEngine storage_limited(limits);
    try {
        storage_limited.add_monitor(delayed_monitor("storage"));
        require(false, "the PSL owned-storage ceiling must be enforced");
    } catch (const VhdlPslResourceError& error) {
        require(error.kind() == VhdlPslResourceKind::storage_bytes
                && storage_limited.storage_bytes() == 0U,
            "storage exhaustion rejects before retaining a monitor");
    }
}

} // namespace fsim::tests::runtime
