// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/simir_vital.hpp"

#include <array>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {
namespace {

void require(const bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

fsim::runtime::simir::VitalMemoryPathDelay delay01zx(
    const std::array<fsim::runtime::SimulationTick, 12>& values) {
  return {fsim::runtime::simir::VitalMemoryPathDelayShape::delay01zx, values};
}

class RecordingContext final
    : public fsim::runtime::simir::ProcessExecutionContext {
 public:
  struct Write {
    fsim::runtime::simir::SignalId signal{};
    std::size_t offset{};
    std::vector<fsim::runtime::simir::ProjectedWaveformValue> waveform;
  };

  [[nodiscard]] fsim::runtime::PackedLogic4 read_signal(
      fsim::runtime::simir::SignalId) const override {
    return fsim::runtime::PackedLogic4{};
  }
  void write_blocking(
      fsim::runtime::simir::SignalId,
      fsim::runtime::PackedLogic4) override {}
  void write_blocking_slice(
      fsim::runtime::simir::SignalId,
      fsim::runtime::PackedLogic4,
      std::size_t) override {}
  void write_update(
      fsim::runtime::simir::SignalId,
      fsim::runtime::PackedLogic4) override {}
  void write_update_slice(
      fsim::runtime::simir::SignalId,
      fsim::runtime::PackedLogic4,
      std::size_t) override {}
  void write_after(
      fsim::runtime::simir::SignalId,
      fsim::runtime::PackedLogic4,
      fsim::runtime::SimulationTick) override {}
  void write_after_slice(
      fsim::runtime::simir::SignalId,
      fsim::runtime::PackedLogic4,
      std::size_t,
      fsim::runtime::SimulationTick) override {}
  void write_inertial(
      fsim::runtime::simir::SignalId,
      fsim::runtime::PackedLogic4,
      const fsim::runtime::simir::TransitionDelays&) override {}
  void write_inertial_slice(
      fsim::runtime::simir::SignalId,
      fsim::runtime::PackedLogic4,
      std::size_t,
      const fsim::runtime::simir::TransitionDelays&) override {}
  void write_projected_waveform_slice(
      const fsim::runtime::simir::SignalId signal,
      std::vector<fsim::runtime::simir::ProjectedWaveformValue> waveform,
      const std::size_t offset,
      fsim::runtime::SimulationTick,
      const fsim::runtime::simir::ProjectedDelayMode mode) override {
    require(mode == fsim::runtime::simir::ProjectedDelayMode::transport,
            "memory paths use projected transport scheduling");
    writes.push_back({signal, offset, std::move(waveform)});
  }

  std::vector<Write> writes;
};

}  // namespace

void test_vital_memory_path_delays() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;
  constexpr auto no_time = std::numeric_limits<SimulationTick>::max();
  const std::array<Logic9, 9> identity{
      Logic9::u, Logic9::x, Logic9::zero, Logic9::one, Logic9::z,
      Logic9::w, Logic9::l, Logic9::h, Logic9::dont_care};

  std::vector<VitalMemoryScheduleData> schedule;
  initialize_vital_memory_path_delay(
      schedule, PackedLogic4::from_msb_string("00"), 1U, 5U);
  require(schedule.size() == 2U && schedule[0].last_output_value == Logic9::zero,
          "memory path initialization records each output bit");

  initialize_vital_memory_path_delay(
      schedule, PackedLogic4::from_msb_string("11"), 1U, 20U);
  std::vector<VitalMemoryPathDelay> cross_delays{
      delay01zx({9, 9, 9, 9, 9, 9, 3, 0, 4, 0, 0, 5}),
      delay01zx({8, 8, 8, 8, 8, 8, 3, 0, 4, 0, 0, 5}),
      delay01zx({6, 6, 6, 6, 6, 6, 2, 0, 4, 0, 0, 5}),
      delay01zx({7, 7, 7, 7, 7, 7, 3, 0, 4, 0, 0, 5}),
  };
  add_vital_memory_path_delay(
      schedule, {18U, 18U}, cross_delays, VitalMemoryTimingArc::cross,
      {true}, 20U, true);
  require(schedule[0].propagation_delay == 6U
              && schedule[1].propagation_delay == 7U,
          "simultaneous cross paths select the shortest propagation delay");
  require(schedule[0].output_retain_delay == 2U,
          "simultaneous retained paths select the shortest retain delay");
  const auto waveforms = schedule_vital_memory_path_delay(
      schedule, {VitalMemoryPortFlag{}}, identity, 20U);
  require(waveforms.size() == 3U
              && waveforms[0].bit == 0U
              && waveforms[0].value == Logic9::one
              && waveforms[0].delay == 4U,
          "memory path scheduling subtracts input age from propagation delay");
  require(waveforms[1].bit == 1U && waveforms[1].value == Logic9::x
              && waveforms[1].delay == 1U,
          "memory path scheduling emits retained-output corruption");
  require(waveforms[2].bit == 1U && waveforms[2].value == Logic9::one
              && waveforms[2].delay == 5U,
          "retained corruption precedes the final projected value");
  auto projected_schedule = schedule;
  RecordingContext context;
  project_vital_memory_path_delay(
      context, 17U, projected_schedule, {VitalMemoryPortFlag{}},
      identity, 20U);
  require(context.writes.size() == 2U
              && context.writes[0].signal == 17U
              && context.writes[0].offset == 0U
              && context.writes[0].waveform.size() == 1U
              && context.writes[1].offset == 1U
              && context.writes[1].waveform.size() == 2U,
          "memory path elements enter the common projected waveform surface");

  initialize_vital_memory_path_delay(
      schedule, PackedLogic4::from_msb_string("10"), 1U, 30U);
  const VitalMemoryPathDelay scalar{
      VitalMemoryPathDelayShape::single, {11U}};
  add_vital_memory_path_delay(
      schedule, {29U, 25U}, {scalar, scalar},
      VitalMemoryTimingArc::parallel, {true, false}, 30U);
  VitalMemoryPortFlag disabled;
  disabled.output_disable = true;
  const auto gated = schedule_vital_memory_path_delay(
      schedule, {VitalMemoryPortFlag{}, disabled}, identity, 30U);
  require(gated.size() == 1U && gated[0].bit == 0U && gated[0].delay == 10U,
          "per-bit conditions and port disabling gate path scheduling");

  std::vector<VitalMemoryScheduleData> subword_schedule;
  initialize_vital_memory_path_delay(
      subword_schedule, PackedLogic4::from_msb_string("0000"), 2U, 0U);
  initialize_vital_memory_path_delay(
      subword_schedule, PackedLogic4::from_msb_string("1111"), 2U, 10U);
  std::vector<VitalMemoryPathDelay> subword_delays(
      8U, VitalMemoryPathDelay{VitalMemoryPathDelayShape::delay01,
                              {5U, 7U}});
  add_vital_memory_path_delay(
      subword_schedule, {9U, 8U}, subword_delays,
      VitalMemoryTimingArc::subword, {true, false}, 10U);
  require(subword_schedule[0].input_age == 1U
              && subword_schedule[1].input_age == 1U
              && subword_schedule[2].input_age == no_time,
          "subword paths map one input event and condition to each group");

  std::vector<VitalMemoryScheduleData> retain_schedule;
  initialize_vital_memory_path_delay(
      retain_schedule, PackedLogic4::from_msb_string("0000"), 2U, 0U);
  initialize_vital_memory_path_delay(
      retain_schedule, PackedLogic4::from_msb_string("1111"), 2U, 10U);
  std::vector<VitalMemoryPathDelay> parallel_delays(
      4U, VitalMemoryPathDelay{VitalMemoryPathDelayShape::delay01zx,
                              {9U, 9U, 9U, 9U, 9U, 9U,
                               3U, 0U, 4U, 0U, 0U, 5U}});
  add_vital_memory_path_delay(
      retain_schedule, {9U, 7U, 8U, 1U}, parallel_delays,
      VitalMemoryTimingArc::parallel, {true}, 10U, true,
      VitalMemoryRetainBehavior::word_corrupt);
  require(retain_schedule[0].input_age == 1U
              && retain_schedule[1].input_age == 1U
              && retain_schedule[2].input_age == 2U
              && retain_schedule[3].input_age == 2U,
          "word-retain paths share the newest event within each subword");

  std::vector<VitalMemoryScheduleData> z_schedule;
  initialize_vital_memory_path_delay(
      z_schedule, PackedLogic4::from_msb_string("0"), std::nullopt, 0U);
  initialize_vital_memory_path_delay(
      z_schedule, PackedLogic4::from_msb_string("Z"), std::nullopt, 5U);
  const VitalMemoryPathDelay delay01z{
      VitalMemoryPathDelayShape::delay01z, {8U, 6U, 3U, 4U, 5U, 7U}};
  add_vital_memory_path_delay(
      z_schedule, {5U}, {delay01z}, VitalMemoryTimingArc::cross,
      {true}, 5U, true);
  require(z_schedule[0].propagation_delay == 6U
              && z_schedule[0].output_retain_delay == 3U,
          "01Z paths preserve their distinct propagation and retain delays");
  auto mapped = identity;
  mapped[static_cast<std::size_t>(Logic9::z)] = Logic9::w;
  const auto mapped_waveform = schedule_vital_memory_path_delay(
      z_schedule, {VitalMemoryPortFlag{}}, mapped, 5U);
  require(mapped_waveform.size() == 2U
              && mapped_waveform[0].value == Logic9::x
              && mapped_waveform[1].value == Logic9::w,
          "memory path scheduling applies output maps after retain corruption");

  std::vector<VitalMemoryScheduleData> overflow_schedule;
  initialize_vital_memory_path_delay(
      overflow_schedule, PackedLogic4::from_msb_string("0"),
      std::nullopt, no_time - 1U);
  initialize_vital_memory_path_delay(
      overflow_schedule, PackedLogic4::from_msb_string("1"),
      std::nullopt, no_time - 1U);
  add_vital_memory_path_delay(
      overflow_schedule, {no_time - 1U},
      {VitalMemoryPathDelay{VitalMemoryPathDelayShape::single, {5U}}},
      VitalMemoryTimingArc::cross, {true}, no_time - 1U);
  bool overflow_rejected{};
  try {
    (void)schedule_vital_memory_path_delay(
        overflow_schedule, {VitalMemoryPortFlag{}}, identity, no_time - 1U);
  } catch (const std::overflow_error&) {
    overflow_rejected = true;
  }
  require(overflow_rejected, "memory path schedule time overflow is rejected");

  bool rejected{};
  try {
    add_vital_memory_path_delay(
        subword_schedule, {0U}, {scalar}, VitalMemoryTimingArc::cross,
        {true}, 10U);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "malformed path-delay matrices are rejected");

  std::vector<VitalMemoryScheduleData> null_schedule;
  initialize_vital_memory_path_delay(
      null_schedule, PackedLogic4{}, std::nullopt, 0U);
  add_vital_memory_path_delay(
      null_schedule, {}, {}, VitalMemoryTimingArc::cross, {}, 0U);
  require(schedule_vital_memory_path_delay(
              null_schedule, {}, identity, 0U).empty(),
          "null output ranges are accepted transactionally");
}

}  // namespace fsim::tests::runtime
