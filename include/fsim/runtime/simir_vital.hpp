// SPDX-License-Identifier: Apache-2.0
#pragma once

// Internal declaration fragment included by simir.hpp after common IDs and
// assertion metadata have been declared.

enum class VitalTimingCheckKind : std::uint8_t {
  setup_hold,
  recovery_removal,
  period_pulse,
  in_phase_skew,
  out_phase_skew,
};

[[nodiscard]] constexpr std::size_t vital_x01_ordinal(
    const Logic9 value) noexcept {
  if (value == Logic9::zero || value == Logic9::l) return 1U;
  if (value == Logic9::one || value == Logic9::h) return 2U;
  return 0U;
}

[[nodiscard]] constexpr std::uint16_t vital_edge_symbol_mask(
    const Logic9 previous,
    const Logic9 current) noexcept {
  constexpr std::array<std::array<std::uint16_t, 3>, 3> masks{{
      {{0U, 0xDA08U, 0xB504U}},
      {{0xA150U, 0U, 0x8145U}},
      {{0xC2A0U, 0x828AU, 0U}},
  }};
  return masks[vital_x01_ordinal(previous)][vital_x01_ordinal(current)];
}

[[nodiscard]] constexpr bool vital_edge_symbol_matches(
    const Logic9 previous,
    const Logic9 current,
    const std::uint16_t selected_symbols) noexcept {
  return (vital_edge_symbol_mask(previous, current)
          & selected_symbols) != 0U;
}

/// One intrinsic VITAL timing-check call site.
///
/// Signal delays are represented by ordinary elaborated VHDL 'delayed
/// signals. The operation therefore receives already sampled signal IDs and
/// contains only immutable check metadata; its instruction index supplies the
/// stable per-call state identity.
struct VitalTimingCheck {
  RegisterId destination{};
  VitalTimingCheckKind kind{VitalTimingCheckKind::setup_hold};
  SignalId test_signal{};
  std::uint32_t test_offset{};
  std::optional<SignalId> reference_signal;
  std::uint32_t reference_offset{};
  std::optional<SignalId> trigger_signal;
  std::array<SimulationTick, 4> limits{};
  std::uint16_t reference_edges{0xffffU};
  bool active_low{};
  bool check_enabled{true};
  std::array<bool, 4> enables{true, true, true, true};
  bool x_on{true};
  bool message_on{true};
  AssertionSeverity severity{AssertionSeverity::warning};
  std::string message;
  SourceLocation source;
};

struct VitalTimingState {
  bool initialized{};
  bool last_violation{};
  bool setup_enabled{};
  bool hold_enabled{};
  Logic9 test{Logic9::u};
  Logic9 reference{Logic9::u};
  std::optional<SimulationTick> test_event;
  std::optional<SimulationTick> reference_event;
  std::array<std::optional<SimulationTick>, 2> test_direction;
  std::array<std::optional<SimulationTick>, 2> reference_direction;
  std::array<std::optional<SimulationTick>, 4> skew_deadlines;
  std::optional<SimulationTick> scheduled_trigger;
  std::optional<SimulationTick> trigger_request;
  bool trigger_level{};
};

enum class VitalDelayKind : std::uint8_t { signal, wire, path };
enum class VitalDelayShape : std::uint8_t { single, delay01, delay01z };
enum class VitalGlitchMode : std::uint8_t {
  on_event,
  on_detect,
  inertial,
  transport,
};

struct VitalPathCandidate {
  RegisterId input_change_time{};
  RegisterId condition{};
  std::array<RegisterId, 6> delays{};
};

/// One intrinsic VITAL signal, wire, or path-delay call site.
struct VitalDelay {
  VitalDelayKind kind{VitalDelayKind::signal};
  VitalDelayShape shape{VitalDelayShape::single};
  SignalId output{};
  RegisterId source{};
  std::optional<RegisterId> glitch_data;
  std::vector<VitalPathCandidate> paths;
  std::array<RegisterId, 6> default_delays{};
  VitalGlitchMode mode{VitalGlitchMode::on_event};
  RegisterId output_map{};
  bool x_on{true};
  bool message_on{true};
  bool negative_preemption{};
  bool ignore_default_delay{};
  bool reject_fast_path{};
  AssertionSeverity severity{AssertionSeverity::warning};
  std::string message;
  SourceLocation source_location;
};

struct VitalDelayState {
  bool initialized{};
  bool last_glitch{};
  Logic9 last_value{Logic9::u};
  Logic9 scheduled_value{Logic9::u};
  SimulationTick scheduled_time{};
  SimulationTick glitch_time{};
};

struct VitalPathRuntimeValue {
  SimulationTick input_change_time{};
  bool condition{};
  std::array<SimulationTick, 6> delays{};
};

struct VitalDelayRuntimeValues {
  Logic9 source{Logic9::u};
  std::vector<VitalPathRuntimeValue> paths;
  std::array<SimulationTick, 6> default_delays{};
  std::array<Logic9, 9> output_map{};
};
