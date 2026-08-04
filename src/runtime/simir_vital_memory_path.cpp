// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

namespace fsim::runtime::simir {
namespace {

constexpr auto kNoTime = std::numeric_limits<SimulationTick>::max();

enum class Level : std::uint8_t { unknown, low, high, high_z };

Level level(const Logic9 value) noexcept {
  if (value == Logic9::zero || value == Logic9::l) return Level::low;
  if (value == Logic9::one || value == Logic9::h) return Level::high;
  if (value == Logic9::z) return Level::high_z;
  return Level::unknown;
}

struct SelectedDelay {
  SimulationTick propagation{};
  SimulationTick retain{kNoTime};
};

SelectedDelay select_delay(
    const Logic9 previous,
    const Logic9 current,
    const VitalMemoryPathDelay& delay) noexcept {
  const auto& value = delay.values;
  if (delay.shape == VitalMemoryPathDelayShape::single) {
    return {value[0], kNoTime};
  }
  const auto old_level = level(previous);
  const auto new_level = level(current);
  const auto rise = value[0];
  const auto fall = value[1];
  if (delay.shape == VitalMemoryPathDelayShape::delay01) {
    if (new_level == Level::low) return {fall, kNoTime};
    if (new_level == Level::high) return {rise, kNoTime};
    if (old_level == Level::low) return {rise, kNoTime};
    if (old_level == Level::high) return {fall, kNoTime};
    if (old_level == Level::high_z) {
      return {std::min(rise, fall), kNoTime};
    }
    return {std::max(rise, fall), kNoTime};
  }
  if (delay.shape == VitalMemoryPathDelayShape::delay01z) {
    if (old_level == Level::low) {
      if (new_level == Level::low) return {fall, value[2]};
      if (new_level == Level::high) return {rise, value[2]};
      return {std::min(rise, fall), value[2]};
    }
    if (old_level == Level::high) {
      if (new_level == Level::low) return {fall, value[4]};
      if (new_level == Level::high) return {rise, value[4]};
      return {std::min(fall, rise), value[4]};
    }
    return {std::max(fall, rise), std::min(value[4], value[2])};
  }

  if (old_level == Level::low) {
    if (new_level == Level::low) return {fall, value[6]};
    if (new_level == Level::high) return {rise, value[6]};
    if (new_level == Level::high_z) return {value[2], value[6]};
    return {std::min(rise, value[2]), value[6]};
  }
  if (old_level == Level::high) {
    if (new_level == Level::low) return {fall, value[8]};
    if (new_level == Level::high) return {rise, value[8]};
    if (new_level == Level::high_z) return {value[4], value[8]};
    return {std::min(fall, value[4]), value[8]};
  }
  if (old_level == Level::high_z) {
    if (new_level == Level::low) return {value[5], value[11]};
    if (new_level == Level::high) return {value[3], value[11]};
    if (new_level == Level::high_z) {
      return {std::max(value[4], value[2]), value[11]};
    }
    return {std::min(value[3], value[5]), value[11]};
  }
  if (new_level == Level::low) {
    return {std::max(fall, value[5]), std::min(value[8], value[6])};
  }
  if (new_level == Level::high) {
    return {std::max(rise, value[3]), std::min(value[8], value[6])};
  }
  if (new_level == Level::high_z) {
    return {std::max(value[4], value[2]), std::min(value[8], value[6])};
  }
  return {std::max(fall, rise), std::min(value[8], value[6])};
}

std::size_t subword_count(
    const std::size_t width,
    const std::optional<std::size_t> bits_per_subword) {
  if (!bits_per_subword) return 1U;
  if (*bits_per_subword == 0U) {
    throw std::invalid_argument{"VITAL memory subword size must be positive"};
  }
  return width == 0U ? 0U : 1U + (width - 1U) / *bits_per_subword;
}

std::vector<bool> expand_conditions(
    const std::vector<bool>& conditions,
    const std::size_t width,
    const std::optional<std::size_t> bits_per_subword) {
  if (width == 0U) return {};
  if (conditions.empty()) {
    throw std::invalid_argument{"VITAL memory path condition is empty"};
  }
  if (conditions.size() == 1U) return std::vector<bool>(width, conditions[0]);
  if (conditions.size() == width) return conditions;
  const auto groups = subword_count(width, bits_per_subword);
  if (conditions.size() != groups || !bits_per_subword) {
    throw std::invalid_argument{
        "VITAL memory path condition dimensions do not match the output"};
  }
  std::vector<bool> result(width);
  for (std::size_t bit = 0; bit < width; ++bit) {
    result[bit] = conditions[bit / *bits_per_subword];
  }
  return result;
}

std::vector<SimulationTick> normalize_word_change_times(
    const std::vector<SimulationTick>& times,
    const std::size_t bits_per_subword) {
  auto result = times;
  for (std::size_t first = 0; first < times.size(); first += bits_per_subword) {
    const auto last = std::min(times.size(), first + bits_per_subword);
    SimulationTick newest{};
    bool found{};
    for (std::size_t bit = first; bit < last; ++bit) {
      if (times[bit] == kNoTime) continue;
      newest = found ? std::max(newest, times[bit]) : times[bit];
      found = true;
    }
    for (std::size_t bit = first; bit < last; ++bit) {
      result[bit] = found ? newest : kNoTime;
    }
  }
  return result;
}

}  // namespace

void initialize_vital_memory_path_delay(
    std::vector<VitalMemoryScheduleData>& schedule,
    const PackedLogic4& output_data,
    const std::optional<std::size_t> bits_per_subword,
    const SimulationTick now) {
  (void)subword_count(output_data.width(), bits_per_subword);
  if (!schedule.empty() && schedule.size() != output_data.width()) {
    throw std::invalid_argument{
        "VITAL memory schedule width does not match the output"};
  }
  if (schedule.empty()) {
    schedule.resize(output_data.width());
    for (std::size_t bit = 0; bit < schedule.size(); ++bit) {
      schedule[bit].schedule_value = output_data.get_logic9(bit);
      schedule[bit].last_output_value = output_data.get_logic9(bit);
      schedule[bit].schedule_time = now;
    }
  }
  for (std::size_t bit = 0; bit < schedule.size(); ++bit) {
    auto& data = schedule[bit];
    const auto output = output_data.get_logic9(bit);
    if (data.schedule_value != output && data.schedule_time <= now) {
      data.last_output_value = data.schedule_value;
    }
    data.output_data = output;
    data.bits_per_subword = bits_per_subword;
    data.propagation_delay = kNoTime;
    data.output_retain_delay = kNoTime;
    data.input_age = kNoTime;
  }
}

void add_vital_memory_path_delay(
    std::vector<VitalMemoryScheduleData>& schedule,
    const std::vector<SimulationTick>& input_change_times,
    const std::vector<VitalMemoryPathDelay>& delays,
    const VitalMemoryTimingArc arc,
    const std::vector<bool>& conditions,
    const SimulationTick now,
    const bool output_retain,
    const VitalMemoryRetainBehavior retain_behavior) {
  if (schedule.empty()) return;
  if (input_change_times.empty()) {
    throw std::invalid_argument{"VITAL memory path input is empty"};
  }
  const auto bits_per_subword = schedule.front().bits_per_subword;
  for (const auto& data : schedule) {
    if (data.bits_per_subword != bits_per_subword) {
      throw std::invalid_argument{
          "VITAL memory schedule has inconsistent subword geometry"};
    }
  }
  const auto selected_conditions =
      expand_conditions(conditions, schedule.size(), bits_per_subword);
  auto change_times = input_change_times;
  if (output_retain
      && retain_behavior == VitalMemoryRetainBehavior::word_corrupt
      && arc == VitalMemoryTimingArc::parallel) {
    if (!bits_per_subword) {
      change_times = normalize_word_change_times(change_times, schedule.size());
    } else {
      change_times = normalize_word_change_times(change_times, *bits_per_subword);
    }
  }

  std::size_t expected_delays{};
  switch (arc) {
    case VitalMemoryTimingArc::parallel:
      if (input_change_times.size() < schedule.size()) {
        throw std::invalid_argument{
            "parallel VITAL memory path input is narrower than the output"};
      }
      expected_delays = schedule.size();
      break;
    case VitalMemoryTimingArc::cross:
      if (schedule.size() > std::numeric_limits<std::size_t>::max()
              / input_change_times.size()) {
        throw std::length_error{"VITAL memory cross-path matrix is too large"};
      }
      expected_delays = schedule.size() * input_change_times.size();
      break;
    case VitalMemoryTimingArc::subword: {
      if (!bits_per_subword) {
        throw std::invalid_argument{
            "subword VITAL memory path requires a subword size"};
      }
      const auto groups = subword_count(schedule.size(), bits_per_subword);
      if (input_change_times.size() < groups
          || schedule.size() > std::numeric_limits<std::size_t>::max() / groups) {
        throw std::invalid_argument{
            "subword VITAL memory path dimensions do not match"};
      }
      expected_delays = schedule.size() * groups;
      break;
    }
  }
  if (delays.size() != expected_delays) {
    throw std::invalid_argument{
        "VITAL memory path delay dimensions do not match the arc"};
  }

  for (std::size_t output = 0; output < schedule.size(); ++output) {
    auto& data = schedule[output];
    if (!selected_conditions[output]) continue;
    if (!output_retain && data.schedule_value == data.output_data
        && data.schedule_time <= now) {
      continue;
    }
    std::size_t first_input{};
    std::size_t last_input{};
    switch (arc) {
      case VitalMemoryTimingArc::parallel:
        first_input = output;
        last_input = output + 1U;
        break;
      case VitalMemoryTimingArc::cross:
        first_input = 0U;
        last_input = input_change_times.size();
        break;
      case VitalMemoryTimingArc::subword:
        first_input = output / *bits_per_subword;
        last_input = first_input + 1U;
        break;
    }
    for (std::size_t input = first_input; input < last_input; ++input) {
      const auto delay_index = arc == VitalMemoryTimingArc::cross
          ? output + schedule.size() * input
          : arc == VitalMemoryTimingArc::subword
              ? output + schedule.size() * input
              : output;
      auto candidate = select_delay(
          data.last_output_value, data.output_data, delays[delay_index]);
      if (!output_retain) candidate.retain = kNoTime;
      const auto change = change_times[input];
      const auto age = change == kNoTime || change > now
          ? kNoTime : now - change;
      if (age < data.input_age) {
        data.propagation_delay = candidate.propagation;
        data.output_retain_delay = candidate.retain;
        data.input_age = age;
      } else if (age == data.input_age) {
        data.propagation_delay =
            std::min(data.propagation_delay, candidate.propagation);
        if (output_retain) {
          data.output_retain_delay =
              std::min(data.output_retain_delay, candidate.retain);
        }
      }
    }
  }
}

std::vector<VitalMemoryScheduledValue> schedule_vital_memory_path_delay(
    std::vector<VitalMemoryScheduleData>& schedule,
    const std::vector<VitalMemoryPortFlag>& port_flags,
    const std::array<Logic9, 9>& output_map,
    const SimulationTick now) {
  std::vector<VitalMemoryScheduledValue> result;
  if (schedule.empty()) return result;
  if (port_flags.empty()) {
    throw std::invalid_argument{"VITAL memory path port flag is empty"};
  }
  const auto bits_per_subword = schedule.front().bits_per_subword;
  const auto groups = subword_count(schedule.size(), bits_per_subword);
  if (port_flags.size() != 1U && port_flags.size() != schedule.size()
      && port_flags.size() != groups) {
    throw std::invalid_argument{
        "VITAL memory path port flags do not match the output"};
  }
  const auto flag_for = [&](const std::size_t bit) -> const VitalMemoryPortFlag& {
    if (port_flags.size() == 1U) return port_flags.front();
    if (port_flags.size() == schedule.size()) return port_flags[bit];
    return port_flags[bit / *bits_per_subword];
  };
  for (std::size_t bit = 0; bit < schedule.size(); ++bit) {
    auto& data = schedule[bit];
    if (flag_for(bit).output_disable || data.propagation_delay == kNoTime) {
      continue;
    }
    const auto age = data.input_age;
    if (age < data.output_retain_delay
        && data.output_retain_delay < data.propagation_delay) {
      const auto retain_delay = data.output_retain_delay - age;
      if (retain_delay > kNoTime - now) {
        throw std::overflow_error{
            "simulation time overflow while scheduling VITAL memory retain"};
      }
      result.push_back({bit, Logic9::x, retain_delay});
    }
    if (age > data.propagation_delay) continue;
    const auto ordinal = static_cast<std::size_t>(data.output_data);
    if (ordinal >= output_map.size()) {
      throw std::invalid_argument{"VITAL memory output map is malformed"};
    }
    const auto delay = data.propagation_delay - age;
    if (delay > kNoTime - now) {
      throw std::overflow_error{
          "simulation time overflow while scheduling a VITAL memory path"};
    }
    const auto value = output_map[ordinal];
    result.push_back({bit, value, delay});
    data.schedule_value = data.output_data;
    data.schedule_time = now + delay;
  }
  return result;
}

void project_vital_memory_path_delay(
    ProcessExecutionContext& context,
    const SignalId output,
    std::vector<VitalMemoryScheduleData>& schedule,
    const std::vector<VitalMemoryPortFlag>& port_flags,
    const std::array<Logic9, 9>& output_map,
    const SimulationTick now) {
  const auto selected = schedule_vital_memory_path_delay(
      schedule, port_flags, output_map, now);
  std::size_t first{};
  while (first < selected.size()) {
    const auto bit = selected[first].bit;
    std::vector<ProjectedWaveformValue> waveform;
    while (first < selected.size() && selected[first].bit == bit) {
      PackedLogic4 value(1U);
      value.fill(selected[first].value);
      waveform.push_back({std::move(value), selected[first].delay});
      ++first;
    }
    context.write_projected_waveform_slice(
        output, std::move(waveform), bit, 0U,
        ProjectedDelayMode::transport);
  }
}

}  // namespace fsim::runtime::simir
