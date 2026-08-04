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

SimulationTick vital_transition_delay(
    const Logic9 previous,
    const Logic9 current,
    const VitalDelayShape shape,
    const std::array<SimulationTick, 6>& delays) noexcept {
  if (shape == VitalDelayShape::single) return delays[0];
  const auto before = vital_x01_ordinal(previous);
  const auto after = vital_x01_ordinal(current);
  const bool before_z = previous == Logic9::z;
  const bool after_z = current == Logic9::z;
  const auto rise = delays[0];
  const auto fall = delays[1];
  if (shape == VitalDelayShape::delay01) {
    if (after == 1U) return fall;
    if (after == 2U) return rise;
    if (before == 1U) return rise;
    if (before == 2U) return fall;
    if (before_z) return std::min(rise, fall);
    return std::max(rise, fall);
  }
  if (before == 1U && after == 2U) return rise;
  if (before == 2U && after == 1U) return fall;
  if (before == 1U && after_z) return delays[2];
  if (before_z && after == 2U) return delays[3];
  if (before == 2U && after_z) return delays[4];
  if (before_z && after == 1U) return delays[5];
  if (before == 1U) return after == 1U ? fall : std::min(rise, delays[2]);
  if (before == 2U) return after == 2U ? rise : std::min(fall, delays[4]);
  if (before_z) {
    if (after == 1U) return delays[5];
    if (after == 2U) return delays[3];
    return std::min(delays[3], delays[5]);
  }
  if (after == 1U) return std::max(fall, delays[5]);
  if (after == 2U) return std::max(rise, delays[3]);
  if (after_z) return std::max(delays[2], delays[4]);
  return std::max(rise, fall);
}

PackedLogic4 vital_scalar(const Logic9 value) {
  PackedLogic4 result(1U);
  result.fill(value);
  return result;
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

void Interpreter::Impl::execute_vital_delay(
    const ProcessId process,
    const InstructionIndex instruction,
    const VitalDelay& operation,
    const VitalDelayRuntimeValues& values) {
  auto& state = get_process(process).vital_delay_states[instruction];
  auto delay = vital_transition_delay(
      state.last_value, values.source,
      operation.shape, values.default_delays);
  bool selected{};
  for (const auto& path : values.paths) {
    if (!path.condition
        || path.input_change_time
            == std::numeric_limits<SimulationTick>::max()) {
      continue;
    }
    const auto path_delay = vital_transition_delay(
        state.last_value, values.source, operation.shape, path.delays);
    const auto remaining = path_delay > path.input_change_time
        ? path_delay - path.input_change_time : SimulationTick{};
    if (!selected || remaining < delay) delay = remaining;
    selected = true;
  }
  if (operation.kind == VitalDelayKind::path && !selected
      && operation.ignore_default_delay) {
    state.last_value = values.source;
    return;
  }
  if (delay > std::numeric_limits<SimulationTick>::max() - scheduler.now()) {
    throw std::overflow_error{
        "simulation time overflow while scheduling a VITAL delay"};
  }
  auto output = values.source;
  if (operation.shape == VitalDelayShape::delay01z) {
    const auto ordinal = static_cast<std::size_t>(values.source);
    if (ordinal >= values.output_map.size()) {
      throw std::invalid_argument{"VITAL output map index is outside its range"};
    }
    output = values.output_map[ordinal];
  }
  const auto now = scheduler.now();
  const auto target = now + delay;
  const bool glitch = operation.kind == VitalDelayKind::path
      && state.initialized && output != state.scheduled_value
      && now < state.scheduled_time;
  state.last_glitch = glitch;
  if (glitch) state.glitch_time = now;
  if (glitch && operation.message_on && report_hook) {
    report_hook(
        process, operation.message, operation.severity,
        operation.source_location, now, scheduler.delta());
  }
  if (glitch && operation.reject_fast_path && target < state.scheduled_time) {
    state.last_value = values.source;
    return;
  }
  if (glitch && !operation.negative_preemption
      && target < state.scheduled_time) {
    delay = state.scheduled_time - now;
  }
  if (glitch && operation.x_on
      && operation.mode == VitalGlitchMode::on_detect) {
    std::vector<ProjectedWaveformValue> waveform;
    waveform.push_back({vital_scalar(Logic9::x), 0U});
    if (delay != 0U) waveform.push_back({vital_scalar(output), delay});
    schedule_projected_waveform(
        process, operation.output, waveform, std::nullopt, 0U,
        ProjectedDelayMode::transport);
    state.scheduled_value = delay == 0U ? Logic9::x : output;
    state.scheduled_time = now + delay;
  } else if (glitch && operation.x_on
             && operation.mode == VitalGlitchMode::on_event) {
    const auto event_delay = state.scheduled_time - now;
    std::vector<ProjectedWaveformValue> waveform;
    waveform.push_back({vital_scalar(Logic9::x), std::min(event_delay, delay)});
    if (delay > event_delay) waveform.push_back({vital_scalar(output), delay});
    schedule_projected_waveform(
        process, operation.output, waveform, std::nullopt, 0U,
        ProjectedDelayMode::transport);
    state.scheduled_value = delay > event_delay ? output : Logic9::x;
    state.scheduled_time = now + delay;
  } else {
    const auto mode = operation.mode == VitalGlitchMode::inertial
        ? ProjectedDelayMode::inertial : ProjectedDelayMode::transport;
    schedule_projected(
        process, operation.output, vital_scalar(output), std::nullopt,
        delay, mode == ProjectedDelayMode::inertial ? delay : 0U, mode);
    state.scheduled_value = output;
    state.scheduled_time = now + delay;
  }
  state.initialized = true;
  state.last_value = values.source;
}

void Interpreter::Impl::execute_vital_delay_operation(
    const ProcessId process_id,
    ProcessState& process,
    const InstructionIndex instruction,
    const VitalDelay& operation) {
  const auto tick = [&](const RegisterId id) {
    const auto word = get_register(process, id).low_word();
    if (word.bval != 0U) {
      throw std::invalid_argument{"VITAL delay contains an unknown time value"};
    }
    return static_cast<SimulationTick>(word.aval);
  };
  VitalDelayRuntimeValues values;
  values.source = get_register(process, operation.source).get_logic9(0U);
  for (std::size_t index = 0; index < 6U; ++index) {
    values.default_delays[index] = tick(operation.default_delays[index]);
  }
  if (operation.shape == VitalDelayShape::delay01z) {
    const auto& map = get_register(process, operation.output_map);
    for (std::size_t index = 0; index < 9U; ++index) {
      values.output_map[index] = map.get_logic9(8U - index);
    }
  }
  values.paths.reserve(operation.paths.size());
  for (const auto& path : operation.paths) {
    VitalPathRuntimeValue value;
    value.input_change_time = tick(path.input_change_time);
    const auto condition = get_register(process, path.condition).low_word();
    if (condition.bval != 0U) {
      throw std::invalid_argument{
          "VITAL path condition contains an unknown value"};
    }
    value.condition = (condition.aval & 1U) != 0U;
    for (std::size_t index = 0; index < 6U; ++index) {
      value.delays[index] = tick(path.delays[index]);
    }
    values.paths.push_back(value);
  }
  execute_vital_delay(process_id, instruction, operation, values);
}

}  // namespace fsim::runtime::simir
