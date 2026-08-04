// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

namespace fsim::runtime::simir {
namespace {

std::optional<std::size_t> direction(
    const Logic9 previous,
    const Logic9 current) noexcept {
  const auto before = vital_x01_ordinal(previous);
  const auto after = vital_x01_ordinal(current);
  if (before == 1U && after == 2U) return 0U;
  if (before == 2U && after == 1U) return 1U;
  return std::nullopt;
}

bool too_soon(
    const std::optional<SimulationTick> previous,
    const SimulationTick now,
    const SimulationTick limit) noexcept {
  return limit != 0 && previous && now >= *previous
      && now - *previous < limit;
}

SimulationTick level_limit(
    const Logic9 value,
    const SimulationTick high,
    const SimulationTick low) noexcept {
  const auto level = vital_x01_ordinal(value);
  if (level == 2U) return high;
  if (level == 1U) return low;
  return std::max(high, low);
}

}  // namespace

Logic9 evaluate_vital_timing_check(
    const VitalTimingCheck& operation,
    VitalTimingState& state,
    const SimulationTick now,
    const Logic9 test,
    const bool test_event,
    const Logic9 reference,
    const bool reference_event,
    const bool trigger_event) {
  state.trigger_request.reset();
  if (!operation.check_enabled) {
    state.last_violation = false;
    state.test = test;
    state.reference = reference;
    return Logic9::zero;
  }
  if (!state.initialized) {
    state.initialized = true;
    state.last_violation = false;
    state.test = test;
    state.reference = reference;
    if (test_event) state.test_event = now;
    if (reference_event) state.reference_event = now;
    return Logic9::zero;
  }

  bool violation{};
  const auto test_direction = direction(state.test, test);
  const auto reference_direction = direction(state.reference, reference);
  const bool selected_reference_event = reference_event
      && vital_edge_symbol_matches(
          state.reference, reference, operation.reference_edges);

  switch (operation.kind) {
    case VitalTimingCheckKind::setup_hold: {
      if (selected_reference_event) {
        state.setup_enabled =
            state.setup_enabled && operation.enables[1];
        state.hold_enabled = operation.enables[2];
      }
      if (test_event) {
        state.setup_enabled = operation.enables[0];
        state.hold_enabled =
            state.hold_enabled && operation.enables[3];
      }
      if (selected_reference_event) {
        if (test_event) state.test_event = now;
        if (state.setup_enabled) {
          violation = violation || too_soon(
              state.test_event, now,
              level_limit(test, operation.limits[0], operation.limits[1]));
        }
        state.setup_enabled = false;
        state.reference_event = now;
      } else if (test_event) {
        if (state.hold_enabled) {
          const auto hold_limit = vital_x01_ordinal(test) == 1U
              ? operation.limits[2]
              : vital_x01_ordinal(test) == 2U
                  ? operation.limits[3]
                  : std::max(operation.limits[2], operation.limits[3]);
          violation = violation || too_soon(
              state.reference_event, now, hold_limit);
        }
        state.hold_enabled = !violation;
        state.test_event = now;
      }
      break;
    }
    case VitalTimingCheckKind::recovery_removal: {
      if (selected_reference_event) {
        state.setup_enabled =
            state.setup_enabled && operation.enables[1];
        state.hold_enabled = operation.enables[2];
      }
      if (test_event) {
        state.setup_enabled = operation.enables[0];
        state.hold_enabled =
            state.hold_enabled && operation.enables[3];
      }
      const bool inactive = operation.active_low
          ? vital_x01_ordinal(test) == 2U
          : vital_x01_ordinal(test) == 1U;
      if (selected_reference_event) {
        if (test_event) state.test_event = now;
        if (state.setup_enabled && inactive) {
          violation = violation || too_soon(
              state.test_event, now, operation.limits[0]);
        }
        state.setup_enabled = false;
        state.reference_event = now;
      } else if (test_event) {
        if (state.hold_enabled && inactive) {
          violation = violation ||
              too_soon(state.reference_event, now, operation.limits[1]);
        }
        state.hold_enabled = !violation;
        state.test_event = now;
      }
      break;
    }
    case VitalTimingCheckKind::period_pulse: {
      if (test_event) {
        const auto edges = vital_edge_symbol_mask(state.test, test);
        const bool starts_high = (edges & (std::uint16_t{1U} << 2U)) != 0U;
        const bool starts_low = (edges & (std::uint16_t{1U} << 3U)) != 0U;
        const bool ends_low = (edges & (std::uint16_t{1U} << 6U)) != 0U;
        const bool ends_high = (edges & (std::uint16_t{1U} << 7U)) != 0U;
        if (starts_high) {
          violation = violation || too_soon(
              state.test_direction[0], now, operation.limits[0]);
          state.test_direction[0] = now;
        } else if (starts_low) {
          violation = violation || too_soon(
              state.test_direction[1], now, operation.limits[0]);
          state.test_direction[1] = now;
        }
        if (ends_low) {
          violation = violation || too_soon(
              state.test_direction[1], now, operation.limits[2]);
        } else if (ends_high) {
          violation = violation || too_soon(
              state.test_direction[0], now, operation.limits[1]);
        }
        if (edges != 0U) state.test_event = now;
      }
      break;
    }
    case VitalTimingCheckKind::in_phase_skew:
    case VitalTimingCheckKind::out_phase_skew: {
      const auto test_target = test_direction
          ? std::optional<std::size_t>{*test_direction}
          : std::nullopt;
      const auto reference_target = reference_direction
          ? std::optional<std::size_t>{2U + *reference_direction}
          : std::nullopt;
      if (test_target) state.skew_deadlines[*test_target].reset();
      if (reference_target) {
        state.skew_deadlines[*reference_target].reset();
      }

      if (trigger_event && state.scheduled_trigger
          && now >= *state.scheduled_trigger) {
        state.scheduled_trigger.reset();
      }
      for (auto& deadline : state.skew_deadlines) {
        if (deadline && now >= *deadline) {
          violation = true;
          deadline.reset();
        }
      }

      const bool opposite =
          operation.kind == VitalTimingCheckKind::out_phase_skew;
      const auto schedule = [&](const std::size_t target,
                                const SimulationTick limit) {
        if (limit == std::numeric_limits<SimulationTick>::max()) return;
        const auto deadline = limit <=
                std::numeric_limits<SimulationTick>::max() - now
            ? now + limit
            : std::numeric_limits<SimulationTick>::max();
        state.skew_deadlines[target] = deadline;
      };
      if (test_direction) {
        const auto selected = *test_direction;
        const auto target_direction = opposite ? 1U - selected : selected;
        const auto target = 2U + target_direction;
        if (!reference_direction
            || *reference_direction != target_direction) {
          schedule(target, operation.limits[selected == 0U ? 0U : 2U]);
        }
        state.test_direction[selected] = now;
        state.test_event = now;
      }
      if (reference_direction) {
        const auto selected = *reference_direction;
        const auto target_direction = opposite ? 1U - selected : selected;
        const auto target = target_direction;
        if (!test_direction || *test_direction != target_direction) {
          schedule(target, operation.limits[selected == 0U ? 1U : 3U]);
        }
        state.reference_direction[selected] = now;
        state.reference_event = now;
      }

      std::optional<SimulationTick> earliest;
      for (const auto deadline : state.skew_deadlines) {
        if (deadline && (!earliest || *deadline < *earliest)) {
          earliest = deadline;
        }
      }
      if (earliest
          && (!state.scheduled_trigger
              || *earliest < *state.scheduled_trigger)) {
        state.scheduled_trigger = earliest;
        state.trigger_request = earliest;
      }
      break;
    }
  }

  state.test = test;
  state.reference = reference;
  state.last_violation = violation;
  return violation && operation.x_on ? Logic9::x : Logic9::zero;
}

Logic9 Interpreter::Impl::execute_vital_timing_check(
    const ProcessId process,
    const InstructionIndex instruction,
    const VitalTimingCheck& operation) {
  auto& state = get_process(process).vital_timing_states[instruction];
  const auto event = [&](const SignalId signal) {
    (void)get_signal(signal);
    const auto& stamp = signal_events[signal];
    return stamp && stamp->first == scheduler.now()
        && stamp->second == scheduler.delta();
  };
  const auto& test_value = get_signal(operation.test_signal).initial_value;
  const auto reference_signal =
      operation.reference_signal.value_or(operation.test_signal);
  const auto& reference_value = get_signal(reference_signal).initial_value;
  const auto result = simir::evaluate_vital_timing_check(
      operation,
      state,
      scheduler.now(),
      test_value.get_logic9(operation.test_offset),
      event(operation.test_signal),
      reference_value.get_logic9(operation.reference_offset),
      operation.reference_signal && event(reference_signal),
      operation.trigger_signal && event(*operation.trigger_signal));
  if (operation.trigger_signal && state.trigger_request) {
    PackedLogic4 trigger(1U);
    state.trigger_level = !state.trigger_level;
    trigger.fill(state.trigger_level ? Logic9::one : Logic9::zero);
    scheduler.schedule_after(
        *state.trigger_request - scheduler.now(),
        SchedulerPhase::update,
        process,
        [this, process, signal = *operation.trigger_signal,
         value = std::move(trigger)](Scheduler&) mutable {
          stage_update(process, signal, std::move(value));
        });
  }
  if (state.last_violation && operation.message_on && report_hook) {
    report_hook(
        process,
        operation.message,
        operation.severity,
        operation.source,
        scheduler.now(),
        scheduler.delta());
  }
  return result;
}

}  // namespace fsim::runtime::simir
