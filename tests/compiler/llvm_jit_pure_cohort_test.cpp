// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fsim::tests::compiler {

namespace {

using namespace fsim::compiler;
using namespace fsim::runtime::simir;

constexpr std::size_t cohort_member_count = 2U;
constexpr std::size_t dense_signal_count = 8U;
constexpr std::array<char, 4> logic4_values { '0', '1', 'X', 'Z' };
constexpr std::array<std::array<std::uint32_t, 2>, cohort_member_count>
    remapped_read_signals { { { 5U, 3U }, { 6U, 2U } } };

enum class InitialUpdateState : std::uint8_t {
  clean,
  pending_overlap,
  stable_shadow,
};

struct MemberState {
  std::vector<std::uint64_t> register_aval;
  std::vector<std::uint64_t> register_bval;
  std::vector<std::uint8_t> register_initialized;
  fsim_jit_frame_v1 frame { };
  fsim_jit_resume_result_v1 result { };
  TestRuntime callbacks;
  fsim_jit_runtime_v1 runtime { };
  std::array<std::uint64_t, dense_signal_count> direct_signal_aval { };
  std::array<std::uint64_t, dense_signal_count> direct_signal_bval { };
  std::array<std::uint32_t, 2> direct_read_signals { };
  fsim_jit_update_slot_v1 update_slot { };
  std::array<std::uint64_t, 1> update_active_words { };
  std::uint8_t queued { };
  std::uint8_t waiting { };
  std::uint8_t process_status { };
};

struct FrameSnapshot {
  std::uint32_t abi_version { };
  std::uint32_t struct_size { };
  std::uint64_t layout_id_low { };
  std::uint64_t layout_id_high { };
  std::uint32_t register_count { };
  std::uint32_t program_counter { };
  std::uint32_t state { };
  std::uint32_t last_instruction { };
  const std::uint64_t* register_aval_pointer { };
  const std::uint64_t* register_bval_pointer { };
  const std::uint8_t* register_initialized_pointer { };
  const std::uint64_t* register_logic9_plane2_pointer { };
  const std::uint64_t* register_logic9_plane3_pointer { };
  std::uint32_t native_call_depth { };
  std::uint32_t native_call_reserved { };
  std::array<std::uint32_t, FSIM_JIT_NATIVE_CALL_STACK_CAPACITY_V1>
      native_return_stack { };
  std::vector<std::uint64_t> register_aval;
  std::vector<std::uint64_t> register_bval;
  std::vector<std::uint8_t> register_initialized;

  friend bool operator==(const FrameSnapshot&, const FrameSnapshot&) = default;
};

struct ResultSnapshot {
  std::uint32_t abi_version { };
  std::uint32_t struct_size { };
  std::uint32_t status { };
  std::uint32_t instruction { };
  std::uint64_t delay { };

  friend bool operator==(const ResultSnapshot&, const ResultSnapshot&) = default;
};

struct UpdateSlotSnapshot {
  std::uint64_t aval { };
  std::uint64_t bval { };
  std::uint64_t logic9_plane2 { };
  std::uint64_t logic9_plane3 { };
  std::uint64_t mask { };
  std::uint32_t active { };
  std::uint32_t reserved { };
  const std::uint64_t* wide_aval { };
  const std::uint64_t* wide_bval { };
  const std::uint64_t* wide_mask { };
  std::uint32_t word_count { };
  std::uint32_t width { };

  friend bool operator==(const UpdateSlotSnapshot&,
      const UpdateSlotSnapshot&) = default;
};

struct CallbackSnapshot {
  std::array<EncodedSignal, 16> signals { };
  std::vector<std::pair<std::uint32_t, EncodedSignal>> writes;
  std::vector<ScheduledWrite> scheduled_writes;
  std::uint32_t assertion_count { };
  std::uint32_t failed_process { };
  std::uint32_t failed_instruction { };
  std::string assertion_message;

  friend bool operator==(const CallbackSnapshot&,
      const CallbackSnapshot&) = default;
};

struct RuntimeDescriptorSnapshot {
  std::uint32_t flags { };
  const std::uint64_t* direct_signal_aval { };
  const std::uint64_t* direct_signal_bval { };
  const std::uint32_t* direct_read_signals { };
  std::uint32_t direct_read_signal_count { };
  std::uint32_t direct_signal_count { };
  fsim_jit_update_slot_v1* direct_update_slots { };
  std::uint32_t direct_update_slot_count { };
  std::uint64_t* direct_update_active_words { };
  std::uint32_t direct_update_active_word_count { };

  friend bool operator==(const RuntimeDescriptorSnapshot&,
      const RuntimeDescriptorSnapshot&) = default;
};

struct MemberSnapshot {
  FrameSnapshot frame;
  ResultSnapshot result;
  UpdateSlotSnapshot update_slot;
  CallbackSnapshot callbacks;
  RuntimeDescriptorSnapshot runtime;
  std::array<std::uint64_t, dense_signal_count> direct_signal_aval { };
  std::array<std::uint64_t, dense_signal_count> direct_signal_bval { };
  std::array<std::uint32_t, 2> direct_read_signals { };
  std::array<std::uint64_t, 1> update_active_words { };
  std::uint8_t queued { };
  std::uint8_t waiting { };
  std::uint8_t process_status { };
  std::uint32_t entry_status { };
  bool has_failure { };

  friend bool operator==(const MemberSnapshot&,
      const MemberSnapshot&) = default;
};

struct CohortSnapshot {
  std::array<MemberSnapshot, cohort_member_count> members;
  std::size_t resumed { };

  friend bool operator==(const CohortSnapshot&,
      const CohortSnapshot&) = default;
};

[[nodiscard]] Process make_logic4_bit_and_process(
    const std::uint32_t id, const std::uint32_t bit,
    const bool tracks_initialization)
{
  Process process;
  process.id = id;
  process.name = "logic4_bit_and_cohort_" + std::to_string(id);
  process.register_count = 9U;
  process.initialize = false;
  process.static_sensitivity = {
      { 0U, EdgeKind::any }, { 1U, EdgeKind::any }
  };
  if (tracks_initialization) {
    for (const auto& [register_id, width] : {
             std::pair<RegisterId, std::uint32_t> { 0U, 2U },
             { 2U, 2U }, { 4U, 1U }, { 6U, 1U }, { 8U, 1U } }) {
      DebugLocal local;
      local.name = "r" + std::to_string(register_id);
      local.type_name = "logic";
      local.register_id = register_id;
      local.width = width;
      process.debug_locals.push_back(std::move(local));
    }
  }
  process.operations = {
      DebugPoint { },
      DebugPoint { },
      ReadSignal { 0U, 0U },
      Extract { 4U, 0U, bit, 1U },
      ReadSignal { 2U, 1U },
      Extract { 6U, 2U, bit, 1U },
      Binary { BinaryOperator::bit_and, 8U, 4U, 6U },
      WriteUpdateSlice { 2U, 8U, bit },
      WaitSensitivity { },
      Jump { 0U },
  };
  return process;
}

[[nodiscard]] EncodedSignal encode_two_bits(
    const char most_significant, const char least_significant)
{
  return encode(PackedLogic4::from_msb_string(
      std::string { most_significant, least_significant }));
}

[[nodiscard]] char bit_and_result(const char lhs, const char rhs)
{
  if (lhs == '0' || rhs == '0') {
    return '0';
  }
  if (lhs == '1' && rhs == '1') {
    return '1';
  }
  return 'X';
}

[[nodiscard]] std::size_t logic4_index(const char value)
{
  for (std::size_t index = 0; index < logic4_values.size(); ++index) {
    if (logic4_values[index] == value) {
      return index;
    }
  }
  assert(false);
  return 0U;
}

struct SourceBits {
  char lhs_msb { };
  char lhs_lsb { };
  char rhs_msb { };
  char rhs_lsb { };
};

[[nodiscard]] SourceBits source_bits(const std::size_t member_index,
    const char selected_lhs, const char selected_rhs)
{
  const auto lhs_distractor = logic4_values[
      (logic4_index(selected_lhs) + member_index + 1U) % logic4_values.size()];
  const auto rhs_distractor = logic4_values[
      (logic4_index(selected_rhs) + member_index + 2U) % logic4_values.size()];
  return member_index == 0U
      ? SourceBits { lhs_distractor, selected_lhs,
            rhs_distractor, selected_rhs }
      : SourceBits { selected_lhs, lhs_distractor,
            selected_rhs, rhs_distractor };
}

[[nodiscard]] fsim::compiler::JitProcessCohortLogic4BitAndRegisterSlot
register_slot(const JitProcessFrameLayout& layout, const RegisterId id)
{
  assert(id < layout.register_count);
  return {
      id,
      layout.register_word_offsets[id],
      layout.register_widths[id],
      false,
  };
}

[[nodiscard]] fsim::compiler::JitProcessCohortLogic4BitAndMember
make_member_shape(const JitProcessFrameLayout& layout,
    const std::uint32_t bit, const bool frame_resident)
{
  using Member = fsim::compiler::JitProcessCohortLogic4BitAndMember;
  Member result;
  result.read_lhs = register_slot(layout, 0U);
  result.read_rhs = register_slot(layout, 2U);
  result.extract_lhs = register_slot(layout, 4U);
  result.extract_rhs = register_slot(layout, 6U);
  result.result = register_slot(layout, 8U);
  for (auto* slot : { &result.read_lhs, &result.read_rhs,
           &result.extract_lhs, &result.extract_rhs, &result.result }) {
    slot->frame_resident = frame_resident;
  }
  result.direct_read_lhs_slot = 0U;
  result.direct_read_rhs_slot = 1U;
  result.extract_lhs_offset = bit;
  result.extract_rhs_offset = bit;
  result.direct_update_slot = 0U;
  result.update_offset = bit;
  result.tracks_register_initialization
      = layout.tracks_register_initialization;
  return result;
}

void configure_direct_runtime(MemberState& member,
    const std::size_t member_index, const char lhs_msb,
    const char lhs_lsb, const char rhs_msb, const char rhs_lsb,
    const InitialUpdateState initial_update)
{
  member.callbacks = TestRuntime { };
  member.runtime = abi(member.callbacks);
  member.direct_signal_aval.fill(0U);
  member.direct_signal_bval.fill(0U);
  member.direct_read_signals = remapped_read_signals[member_index];

  const auto lhs = encode_two_bits(lhs_msb, lhs_lsb);
  const auto rhs = encode_two_bits(rhs_msb, rhs_lsb);
  member.callbacks.signals[0] = lhs;
  member.callbacks.signals[1] = rhs;
  member.callbacks.signals[2] = encode_two_bits('0', '0');
  const auto lhs_actual = member.direct_read_signals[0];
  const auto rhs_actual = member.direct_read_signals[1];
  member.direct_signal_aval[lhs_actual] = lhs.aval;
  member.direct_signal_bval[lhs_actual] = lhs.bval;
  member.direct_signal_aval[rhs_actual] = rhs.aval;
  member.direct_signal_bval[rhs_actual] = rhs.bval;

  member.runtime.direct_signal_aval = member.direct_signal_aval.data();
  member.runtime.direct_signal_bval = member.direct_signal_bval.data();
  member.runtime.direct_read_signals = member.direct_read_signals.data();
  member.runtime.direct_read_signal_count
      = static_cast<std::uint32_t>(member.direct_read_signals.size());
  member.runtime.direct_signal_count
      = static_cast<std::uint32_t>(member.direct_signal_aval.size());

  member.update_slot = { };
  member.update_slot.width = 2U;
  member.update_slot.word_count = 1U;
  member.update_active_words.fill(0U);
  const auto bit = static_cast<std::uint32_t>(member_index);
  const auto bit_mask = UINT64_C(1) << bit;
  if (initial_update == InitialUpdateState::pending_overlap) {
    member.update_slot.mask = bit_mask;
    member.update_slot.active = 1U;
    member.update_active_words[0] = 1U;
  } else if (initial_update == InitialUpdateState::stable_shadow) {
    member.update_slot.mask = UINT64_C(1) << (1U - bit);
    member.update_slot.active = 1U;
    member.update_slot.reserved = 1U;
    member.update_active_words[0] = 1U;
  }
  member.runtime.direct_update_slots = &member.update_slot;
  member.runtime.direct_update_slot_count = 1U;
  member.runtime.direct_update_active_words
      = member.update_active_words.data();
  member.runtime.direct_update_active_word_count
      = static_cast<std::uint32_t>(member.update_active_words.size());
  member.runtime.flags = 0U;
}

void reset_member(MemberState& member, const LlvmJit& jit,
    const JitProcessHandle handle,
    const std::size_t member_index, const char selected_lhs,
    const char selected_rhs, const InitialUpdateState initial_update)
{
  std::ranges::fill(member.register_aval, UINT64_C(0));
  std::ranges::fill(member.register_bval, UINT64_C(0));
  std::ranges::fill(member.register_initialized, UINT8_C(0));
  member.frame = { };
  jit.initialize_frame(handle, member.frame, member.register_aval,
      member.register_bval, member.register_initialized);
  member.result = new_resume_result();
  member.queued = 1U;
  member.waiting = 1U;
  member.process_status = 2U;
  const auto sources = source_bits(member_index, selected_lhs, selected_rhs);
  configure_direct_runtime(member, member_index, sources.lhs_msb,
      sources.lhs_lsb, sources.rhs_msb, sources.rhs_lsb, initial_update);
}

[[nodiscard]] FrameSnapshot snapshot_frame(const MemberState& member)
{
  const auto& frame = member.frame;
  FrameSnapshot result;
  result.abi_version = frame.abi_version;
  result.struct_size = frame.struct_size;
  result.layout_id_low = frame.layout_id_low;
  result.layout_id_high = frame.layout_id_high;
  result.register_count = frame.register_count;
  result.program_counter = frame.program_counter;
  result.state = frame.state;
  result.last_instruction = frame.last_instruction;
  result.register_aval_pointer = frame.register_aval;
  result.register_bval_pointer = frame.register_bval;
  result.register_initialized_pointer = frame.register_initialized;
  result.register_logic9_plane2_pointer = frame.register_logic9_plane2;
  result.register_logic9_plane3_pointer = frame.register_logic9_plane3;
  result.native_call_depth = frame.native_call_depth;
  result.native_call_reserved = frame.native_call_reserved;
  std::ranges::copy(frame.native_return_stack,
      result.native_return_stack.begin());
  result.register_aval = member.register_aval;
  result.register_bval = member.register_bval;
  result.register_initialized = member.register_initialized;
  return result;
}

[[nodiscard]] MemberSnapshot snapshot_member(
    const MemberState& member,
    const fsim::compiler::JitProcessCohortResumeEntry& entry)
{
  const auto& result = member.result;
  const auto& slot = member.update_slot;
  const auto& runtime = member.runtime;
  const auto& callbacks = member.callbacks;
  return {
      snapshot_frame(member),
      { result.abi_version, result.struct_size, result.status,
          result.instruction, result.delay },
      { slot.aval, slot.bval, slot.logic9_plane2, slot.logic9_plane3,
          slot.mask, slot.active, slot.reserved, slot.wide_aval,
          slot.wide_bval, slot.wide_mask, slot.word_count, slot.width },
      { callbacks.signals, callbacks.writes, callbacks.scheduled_writes,
          callbacks.assertion_count, callbacks.failed_process,
          callbacks.failed_instruction, callbacks.assertion_message },
      { runtime.flags, runtime.direct_signal_aval,
          runtime.direct_signal_bval, runtime.direct_read_signals,
          runtime.direct_read_signal_count, runtime.direct_signal_count,
          runtime.direct_update_slots, runtime.direct_update_slot_count,
          runtime.direct_update_active_words,
          runtime.direct_update_active_word_count },
      member.direct_signal_aval,
      member.direct_signal_bval,
      member.direct_read_signals,
      member.update_active_words,
      member.queued,
      member.waiting,
      member.process_status,
      entry.status,
      static_cast<bool>(entry.failure),
  };
}

[[nodiscard]] CohortSnapshot snapshot_cohort(
    const std::array<MemberState, cohort_member_count>& members,
    const std::array<fsim::compiler::JitProcessCohortResumeEntry,
        cohort_member_count>& entries,
    const std::size_t resumed)
{
  return {
      { snapshot_member(members[0], entries[0]),
          snapshot_member(members[1], entries[1]) },
      resumed,
  };
}

void expect_register(const MemberState& member,
    const JitProcessFrameLayout& layout, const RegisterId id,
    const EncodedSignal expected)
{
  const auto word_offset = layout.register_word_offsets[id];
  const auto mask = low_mask(layout.register_widths[id]);
  assert((member.frame.register_aval[word_offset] & mask) == expected.aval);
  assert((member.frame.register_bval[word_offset] & mask) == expected.bval);
  assert(member.frame.register_initialized[id]
      == (layout.tracks_register_initialization ? 1U : 0U));
}

void expect_member_result(const MemberState& member,
    const JitProcessFrameLayout& layout, const std::size_t member_index,
    const char selected_lhs, const char selected_rhs)
{
  if (member_index != 0U) {
    // This member has no debug locals. Generic lowering keeps its temporary
    // registers local to each activation and leaves frame storage untouched.
    assert(std::ranges::all_of(member.register_aval,
        [](const auto value) { return value == 0U; }));
    assert(std::ranges::all_of(member.register_bval,
        [](const auto value) { return value == 0U; }));
    assert(std::ranges::all_of(member.register_initialized,
        [](const auto value) { return value == 0U; }));
    return;
  }
  const auto sources = source_bits(member_index, selected_lhs, selected_rhs);
  expect_register(member, layout, 0U,
      encode_two_bits(sources.lhs_msb, sources.lhs_lsb));
  expect_register(member, layout, 2U,
      encode_two_bits(sources.rhs_msb, sources.rhs_lsb));
  expect_register(member, layout, 4U,
      encode(PackedLogic4::from_msb_string(
          std::string { selected_lhs })));
  expect_register(member, layout, 6U,
      encode(PackedLogic4::from_msb_string(
          std::string { selected_rhs })));
  const auto result = bit_and_result(selected_lhs, selected_rhs);
  expect_register(member, layout, 8U,
      encode(PackedLogic4::from_msb_string(std::string { result })));

  for (const auto unused : { 1U, 3U, 5U, 7U }) {
    assert(member.frame.register_initialized[unused] == 0U);
  }
}

void expect_update_slot(const MemberState& member,
    const std::size_t member_index, const char selected_lhs,
    const char selected_rhs, const InitialUpdateState initial_update)
{
  const auto result = bit_and_result(selected_lhs, selected_rhs);
  const auto encoded = encode(
      PackedLogic4::from_msb_string(std::string { result }));
  const auto bit = static_cast<std::uint32_t>(member_index);
  const auto bit_mask = UINT64_C(1) << bit;
  const bool stable_match = initial_update == InitialUpdateState::stable_shadow
      && result == '0';
  const bool shadow_change
      = initial_update != InitialUpdateState::stable_shadow || !stable_match;
  const auto expected_mask = initial_update == InitialUpdateState::stable_shadow
      ? (UINT64_C(1) << (1U - bit)) | (shadow_change ? bit_mask : 0U)
      : bit_mask;
  const auto expected_aval = shadow_change ? encoded.aval << bit : UINT64_C(0);
  const auto expected_bval = shadow_change ? encoded.bval << bit : UINT64_C(0);
  assert(member.update_slot.aval == expected_aval);
  assert(member.update_slot.bval == expected_bval);
  assert(member.update_slot.mask == expected_mask);
  assert(member.update_slot.active == 1U);
  assert(member.update_active_words[0] == 1U);
}

void expect_success_state(const CohortSnapshot& snapshot,
    const std::array<MemberState, cohort_member_count>& members,
    const std::array<JitProcessFrameLayout, cohort_member_count>& layouts,
    const std::size_t row, const InitialUpdateState initial_update)
{
  assert(snapshot.resumed == cohort_member_count);
  for (std::size_t index = 0; index < cohort_member_count; ++index) {
    const auto& state = snapshot.members[index];
    assert(state.result.abi_version == FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1);
    assert(state.result.struct_size
        == static_cast<std::uint32_t>(sizeof(fsim_jit_resume_result_v1)));
    assert(state.result.status == FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY);
    assert(state.result.instruction == 8U);
    assert(state.result.delay == 0U);
    assert(state.entry_status == FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY);
    assert(state.frame.program_counter == 9U);
    assert(state.frame.state == FSIM_JIT_FRAME_STATE_READY);
    assert(state.frame.last_instruction == 8U);
    assert(state.frame.native_call_depth == 0U);
    assert(std::ranges::all_of(state.frame.native_return_stack,
        [](const auto value) { return value == 0U; }));
    assert(state.queued == 0U);
    assert(state.waiting == 1U);
    assert(state.process_status == 2U);
    assert(!state.has_failure);

    const auto lhs = logic4_values[row / 4U];
    const auto rhs = logic4_values[row % 4U];
    expect_member_result(members[index], layouts[index], index, lhs, rhs);
    expect_update_slot(
        members[index], index, lhs, rhs, initial_update);
  }
}

} // namespace

namespace {

using fsim::compiler::JitPureWaveMember;
using fsim::compiler::JitPureWaveMemberBinding;
using fsim::runtime::Logic4;

constexpr std::size_t pure_wave_member_count = 13U;
constexpr std::size_t pure_wave_signal_count = 48U;
constexpr std::size_t pure_wave_wide_words_per_signal = 2U;
constexpr std::array<std::uint32_t, 7> reducer_read_offsets {
  105U, 90U, 75U, 60U, 45U, 30U, 15U
};
constexpr std::array<std::uint32_t, 7> reducer_selector_offsets {
  14U, 13U, 12U, 11U, 10U, 9U, 8U
};
constexpr std::array<std::uint32_t, 7> reducer_write_offsets {
  90U, 75U, 60U, 45U, 30U, 15U, 0U
};

enum class PureWaveShape : std::uint8_t {
  reducer,
  xor_reduce,
  copy,
  bit_and,
};

struct PureWaveMemberState {
  Process process;
  JitProcessHandle handle;
  JitProcessFrameLayout layout;
  std::array<SignalId, 3> mapped_signals { };
  std::vector<SignalId> read_signals;
  std::vector<SignalId> update_signals;
  std::vector<std::uint64_t> register_aval;
  std::vector<std::uint64_t> register_bval;
  std::vector<std::uint8_t> register_initialized;
  fsim_jit_frame_v1 frame { };
  fsim_jit_resume_result_v1 result { };
  TestRuntime callbacks;
  fsim_jit_runtime_v1 runtime { };
  std::vector<std::uint64_t> direct_signal_aval;
  std::vector<std::uint64_t> direct_signal_bval;
  std::vector<std::uint64_t> direct_wide_signal_aval;
  std::vector<std::uint64_t> direct_wide_signal_bval;
  std::vector<std::uint32_t> direct_wide_signal_offsets;
  std::vector<std::uint32_t> signal_widths;
  std::vector<std::uint64_t> update_wide_aval;
  std::vector<std::uint64_t> update_wide_bval;
  std::vector<std::uint64_t> update_wide_mask;
  fsim_jit_update_slot_v1 update_slot { };
  std::vector<std::uint64_t> update_active_words;
  std::uint8_t queued { };
  std::uint8_t waiting { };
  std::uint8_t process_status { };
};

class PureWaveFixtureExecutor final : public ProcessExecutor {
public:
  [[nodiscard]] ProcessResumeResult resume(
      ProcessExecutionContext&, const InstructionIndex start) override
  {
    return { start, start };
  }
};

struct PureWaveRuntimeSnapshot {
  std::uint32_t flags { };
  const std::uint64_t* direct_signal_aval { };
  const std::uint64_t* direct_signal_bval { };
  const std::uint32_t* direct_read_signals { };
  std::uint32_t direct_read_signal_count { };
  std::uint32_t direct_signal_count { };
  const std::uint64_t* direct_wide_signal_aval { };
  const std::uint64_t* direct_wide_signal_bval { };
  const std::uint32_t* direct_wide_signal_offsets { };
  std::uint32_t direct_wide_signal_offset_count { };
  std::uint32_t direct_wide_word_count { };
  fsim_jit_update_slot_v1* direct_update_slots { };
  std::uint32_t direct_update_slot_count { };
  std::uint64_t* direct_update_active_words { };
  std::uint32_t direct_update_active_word_count { };

  friend bool operator==(const PureWaveRuntimeSnapshot&,
      const PureWaveRuntimeSnapshot&) = default;
};

struct PureWaveMemberSnapshot {
  FrameSnapshot frame;
  ResultSnapshot result;
  UpdateSlotSnapshot update_slot;
  CallbackSnapshot callbacks;
  PureWaveRuntimeSnapshot runtime;
  std::vector<std::uint64_t> direct_signal_aval;
  std::vector<std::uint64_t> direct_signal_bval;
  std::vector<std::uint64_t> direct_wide_signal_aval;
  std::vector<std::uint64_t> direct_wide_signal_bval;
  std::vector<std::uint64_t> update_wide_aval;
  std::vector<std::uint64_t> update_wide_bval;
  std::vector<std::uint64_t> update_wide_mask;
  std::vector<std::uint64_t> update_active_words;
  std::vector<SignalId> read_signals;
  std::vector<std::uint32_t> wide_signal_offsets;
  std::uint8_t queued { };
  std::uint8_t waiting { };
  std::uint8_t process_status { };
  std::uint32_t entry_status { };
  bool has_failure { };

  friend bool operator==(const PureWaveMemberSnapshot&,
      const PureWaveMemberSnapshot&) = default;
};

[[nodiscard]] Logic4 decode_logic4(const char value)
{
  switch (value) {
  case '0': return Logic4::zero;
  case '1': return Logic4::one;
  case 'X': return Logic4::x;
  case 'Z': return Logic4::z;
  default: assert(false); return Logic4::x;
  }
}

[[nodiscard]] Process make_pure_wave_reducer(
    const std::uint32_t id, const std::uint32_t read_offset,
    const std::uint32_t selector_offset, const std::uint32_t write_offset)
{
  Process process;
  process.id = id;
  process.name = "pure_wave_reducer_" + std::to_string(id);
  process.register_count = 20U;
  process.initialize = false;
  process.static_sensitivity = {
    { 1U, EdgeKind::any }, { 0U, EdgeKind::any }
  };
  process.driver_regions = { { 0U, write_offset, 15U, false } };
  process.operations = {
    DebugPoint { },
    DebugPoint { },
    ReadSignal { 0U, 0U },
    Extract { 1U, 0U, read_offset, 15U },
    Extract { 2U, 1U, selector_offset, 1U },
    LoadConstant { 4U, PackedLogic4::from_msb_string("1") },
    LoadConstant { 5U, PackedLogic4::from_msb_string("0") },
    Binary { BinaryOperator::case_equal, 6U, 2U, 4U },
    Binary { BinaryOperator::case_equal, 7U, 2U, 5U },
    Branch { 6U, 10U, 16U, UnknownBranchPolicy::when_false },
    ReadSignal { 8U, 0U },
    Extract { 9U, 8U, read_offset, 15U },
    ReadSignal { 10U, 1U },
    Binary { BinaryOperator::bit_xor, 11U, 9U, 10U },
    CopyRegister { 3U, 11U },
    Jump { 28U },
    Branch { 7U, 17U, 21U, UnknownBranchPolicy::when_false },
    ReadSignal { 12U, 0U },
    Extract { 13U, 12U, read_offset, 15U },
    CopyRegister { 3U, 13U },
    Jump { 28U },
    ReadSignal { 14U, 0U },
    Extract { 15U, 14U, read_offset, 15U },
    ReadSignal { 16U, 1U },
    Binary { BinaryOperator::bit_xor, 17U, 15U, 16U },
    ReadSignal { 18U, 0U },
    Extract { 19U, 18U, read_offset, 15U },
    ConditionalSelect { 3U, 2U, 17U, 19U },
    WriteUpdateSlice { 0U, 3U, write_offset },
    WaitSensitivity { },
    Jump { 0U },
  };
  return process;
}

[[nodiscard]] Process make_pure_wave_xor(const std::uint32_t id)
{
  Process process;
  process.id = id;
  process.name = "pure_wave_xor_" + std::to_string(id);
  process.register_count = 2U;
  process.initialize = false;
  process.static_sensitivity = { { 0U, EdgeKind::any } };
  process.driver_regions = { { 1U, 0U, 1U, false } };
  process.operations = {
    DebugPoint { },
    DebugPoint { },
    ReadSignal { 0U, 0U },
    Reduction { ReductionOperator::bit_xor, 1U, 0U },
    WriteUpdateSlice { 1U, 1U, 0U },
    WaitSensitivity { },
    Jump { 0U },
  };
  return process;
}

[[nodiscard]] Process make_pure_wave_copy(const std::uint32_t id)
{
  Process process;
  process.id = id;
  process.name = "pure_wave_copy_" + std::to_string(id);
  process.register_count = 1U;
  process.initialize = false;
  process.static_sensitivity = { { 0U, EdgeKind::any } };
  process.driver_regions = { { 1U, 105U, 15U, false } };
  process.operations = {
    DebugPoint { },
    DebugPoint { },
    ReadSignal { 0U, 0U },
    WriteUpdateSlice { 1U, 0U, 105U },
    WaitSensitivity { },
    Jump { 0U },
  };
  return process;
}

[[nodiscard]] Process make_pure_wave_and(const std::uint32_t id)
{
  Process process;
  process.id = id;
  process.name = "pure_wave_and_" + std::to_string(id);
  process.register_count = 9U;
  process.initialize = false;
  process.static_sensitivity = {
    { 0U, EdgeKind::any }, { 1U, EdgeKind::any }
  };
  process.driver_regions = { { 2U, 0U, 1U, false } };
  process.operations = {
    DebugPoint { },
    DebugPoint { },
    ReadSignal { 0U, 0U },
    Extract { 4U, 0U, 0U, 1U },
    ReadSignal { 2U, 1U },
    Extract { 6U, 2U, 0U, 1U },
    Binary { BinaryOperator::bit_and, 8U, 4U, 6U },
    WriteUpdateSlice { 2U, 8U, 0U },
    WaitSensitivity { },
    Jump { 0U },
  };
  return process;
}

void remap_pure_wave_process(Process& process,
    const std::array<SignalId, 3>& mapped_signals,
    const std::uint32_t instance_id)
{
  const auto remap = [&](const SignalId signal) {
    assert(signal < mapped_signals.size());
    return mapped_signals[signal];
  };
  process.id = instance_id;
  process.name += "_instance_" + std::to_string(instance_id);
  for (auto& sensitivity : process.static_sensitivity) {
    sensitivity.signal = remap(sensitivity.signal);
  }
  for (auto& region : process.driver_regions) {
    region.signal = remap(region.signal);
  }
  for (auto& operation : process.operations) {
    if (auto* read = operation_get_if<ReadSignal>(&operation)) {
      read->signal = remap(read->signal);
    } else if (auto* write = operation_get_if<WriteUpdateSlice>(&operation)) {
      write->signal = remap(write->signal);
    }
  }
}

[[nodiscard]] std::array<std::uint32_t, 3> pure_wave_signal_widths(
    const PureWaveShape shape)
{
  switch (shape) {
  case PureWaveShape::reducer: return { 120U, 15U, 1U };
  case PureWaveShape::xor_reduce: return { 8U, 15U, 1U };
  case PureWaveShape::copy: return { 15U, 120U, 1U };
  case PureWaveShape::bit_and: return { 15U, 15U, 15U };
  }
  assert(false);
  return { };
}

[[nodiscard]] Process make_pure_wave_process(
    const PureWaveShape shape, const std::size_t member_index)
{
  switch (shape) {
  case PureWaveShape::reducer:
    assert(member_index < reducer_read_offsets.size());
    return make_pure_wave_reducer(
        static_cast<std::uint32_t>(member_index),
        reducer_read_offsets[member_index],
        reducer_selector_offsets[member_index],
        reducer_write_offsets[member_index]);
  case PureWaveShape::xor_reduce:
    return make_pure_wave_xor(7U);
  case PureWaveShape::copy:
    return make_pure_wave_copy(8U);
  case PureWaveShape::bit_and:
    return make_pure_wave_and(9U);
  }
  assert(false);
  return { };
}

[[nodiscard]] FrameSnapshot snapshot_pure_wave_frame(
    const PureWaveMemberState& member)
{
  const auto& frame = member.frame;
  FrameSnapshot result;
  result.abi_version = frame.abi_version;
  result.struct_size = frame.struct_size;
  result.layout_id_low = frame.layout_id_low;
  result.layout_id_high = frame.layout_id_high;
  result.register_count = frame.register_count;
  result.program_counter = frame.program_counter;
  result.state = frame.state;
  result.last_instruction = frame.last_instruction;
  result.register_aval_pointer = frame.register_aval;
  result.register_bval_pointer = frame.register_bval;
  result.register_initialized_pointer = frame.register_initialized;
  result.register_logic9_plane2_pointer = frame.register_logic9_plane2;
  result.register_logic9_plane3_pointer = frame.register_logic9_plane3;
  result.native_call_depth = frame.native_call_depth;
  result.native_call_reserved = frame.native_call_reserved;
  std::ranges::copy(frame.native_return_stack,
      result.native_return_stack.begin());
  result.register_aval = member.register_aval;
  result.register_bval = member.register_bval;
  result.register_initialized = member.register_initialized;
  return result;
}

[[nodiscard]] PureWaveMemberSnapshot snapshot_pure_wave_member(
    const PureWaveMemberState& member,
    const JitProcessCohortResumeEntry& entry)
{
  const auto& result = member.result;
  const auto& slot = member.update_slot;
  const auto& runtime = member.runtime;
  const auto& callbacks = member.callbacks;
  return {
    snapshot_pure_wave_frame(member),
    { result.abi_version, result.struct_size, result.status,
      result.instruction, result.delay },
    { slot.aval, slot.bval, slot.logic9_plane2, slot.logic9_plane3,
      slot.mask, slot.active, slot.reserved, slot.wide_aval,
      slot.wide_bval, slot.wide_mask, slot.word_count, slot.width },
    { callbacks.signals, callbacks.writes, callbacks.scheduled_writes,
      callbacks.assertion_count, callbacks.failed_process,
      callbacks.failed_instruction, callbacks.assertion_message },
    { runtime.flags, runtime.direct_signal_aval,
      runtime.direct_signal_bval, runtime.direct_read_signals,
      runtime.direct_read_signal_count, runtime.direct_signal_count,
      runtime.direct_wide_signal_aval, runtime.direct_wide_signal_bval,
      runtime.direct_wide_signal_offsets,
      runtime.direct_wide_signal_offset_count,
      runtime.direct_wide_word_count, runtime.direct_update_slots,
      runtime.direct_update_slot_count, runtime.direct_update_active_words,
      runtime.direct_update_active_word_count },
    member.direct_signal_aval,
    member.direct_signal_bval,
    member.direct_wide_signal_aval,
    member.direct_wide_signal_bval,
    member.update_wide_aval,
    member.update_wide_bval,
    member.update_wide_mask,
    member.update_active_words,
    member.read_signals,
    member.direct_wide_signal_offsets,
    member.queued,
    member.waiting,
    member.process_status,
    entry.status,
    static_cast<bool>(entry.failure),
  };
}

[[nodiscard]] std::vector<PureWaveMemberSnapshot> snapshot_pure_wave(
    const std::array<PureWaveMemberState, pure_wave_member_count>& members,
    const std::array<JitProcessCohortResumeEntry,
        pure_wave_member_count>& entries)
{
  std::vector<PureWaveMemberSnapshot> snapshots;
  snapshots.reserve(members.size());
  for (std::size_t index = 0; index < members.size(); ++index) {
    snapshots.push_back(snapshot_pure_wave_member(members[index], entries[index]));
  }
  return snapshots;
}

void configure_pure_wave_member(PureWaveMemberState& member,
    const std::size_t member_index, const PureWaveShape shape,
    const std::size_t row)
{
  std::ranges::fill(member.direct_signal_aval, UINT64_C(0));
  std::ranges::fill(member.direct_signal_bval, UINT64_C(0));
  std::ranges::fill(member.direct_wide_signal_aval, UINT64_C(0));
  std::ranges::fill(member.direct_wide_signal_bval, UINT64_C(0));

  const auto set_signal = [&](const SignalId signal,
                              const PackedLogic4& value) {
    assert(signal < member.signal_widths.size());
    assert(value.width() == member.signal_widths[signal]);
    const auto offset = member.direct_wide_signal_offsets[signal];
    const auto words = value.aval_words();
    const auto bwords = value.bval_words();
    assert(words.size() <= pure_wave_wide_words_per_signal);
    std::ranges::copy(words,
        member.direct_wide_signal_aval.begin() + offset);
    std::ranges::copy(bwords,
        member.direct_wide_signal_bval.begin() + offset);
    if (!words.empty()) {
      member.direct_signal_aval[signal] = words.front();
      member.direct_signal_bval[signal] = bwords.front();
    }
  };

  switch (shape) {
  case PureWaveShape::reducer: {
    const auto alignment = member_index;
    assert(alignment < reducer_read_offsets.size());
    PackedLogic4 old_slice(15U, Logic4::zero);
    for (std::size_t bit = 0; bit < old_slice.width(); ++bit) {
      old_slice.set(bit,
          decode_logic4(logic4_values[(bit + row + alignment) % 4U]));
    }
    const auto selector = logic4_values[row % logic4_values.size()];
    old_slice.set(reducer_selector_offsets[alignment],
        decode_logic4(selector));
    PackedLogic4 red(120U, Logic4::zero);
    red.insert_bits(old_slice, reducer_read_offsets[alignment]);
    PackedLogic4 prim(15U, Logic4::zero);
    for (std::size_t bit = 0; bit < prim.width(); ++bit) {
      prim.set(bit,
          decode_logic4(logic4_values[(bit + row + alignment + 1U) % 4U]));
    }
    if (alignment == 2U && row == 1U) {
      prim.set(0U, Logic4::one);
    }
    set_signal(member.mapped_signals[0], red);
    set_signal(member.mapped_signals[1], prim);
    break;
  }
  case PureWaveShape::xor_reduce: {
    PackedLogic4 input(8U, Logic4::zero);
    input.set(row % input.width(),
        decode_logic4(logic4_values[row % logic4_values.size()]));
    set_signal(member.mapped_signals[0], input);
    break;
  }
  case PureWaveShape::copy: {
    PackedLogic4 input(15U, Logic4::zero);
    for (std::size_t bit = 0; bit < input.width(); ++bit) {
      input.set(bit,
          decode_logic4(logic4_values[(bit + row) % logic4_values.size()]));
    }
    set_signal(member.mapped_signals[0], input);
    break;
  }
  case PureWaveShape::bit_and: {
    PackedLogic4 lhs(15U, Logic4::zero);
    PackedLogic4 rhs(15U, Logic4::zero);
    lhs.set(0U, decode_logic4(logic4_values[row / 4U]));
    rhs.set(0U, decode_logic4(logic4_values[row % 4U]));
    set_signal(member.mapped_signals[0], lhs);
    set_signal(member.mapped_signals[1], rhs);
    break;
  }
  }
}

void clear_pure_wave_update(
    PureWaveMemberState& member, const bool waiting_on_static = false)
{
  member.update_slot.aval = 0U;
  member.update_slot.bval = 0U;
  member.update_slot.logic9_plane2 = 0U;
  member.update_slot.logic9_plane3 = 0U;
  member.update_slot.mask = 0U;
  member.update_slot.active = 0U;
  member.update_slot.reserved = 0U;
  std::ranges::fill(member.update_wide_aval, UINT64_C(0));
  std::ranges::fill(member.update_wide_bval, UINT64_C(0));
  std::ranges::fill(member.update_wide_mask, UINT64_C(0));
  std::ranges::fill(member.update_active_words, UINT64_C(0));
  member.result = new_resume_result();
  member.queued = 1U;
  member.waiting = waiting_on_static ? 1U : 0U;
  member.process_status = 2U;
}

void seed_pure_wave_wide_update(PureWaveMemberState& member,
    const PackedLogic4& target_value, const bool stable_shadow)
{
  constexpr std::uint32_t target_offset = 60U;
  constexpr std::uint32_t target_width = 15U;
  constexpr std::uint64_t first_target_word_mask
      = std::numeric_limits<std::uint64_t>::max() << target_offset;
  constexpr std::uint64_t second_target_word_mask
      = (UINT64_C(1) << (target_offset + target_width - 64U)) - 1U;
  constexpr std::uint64_t second_word_valid_mask
      = (UINT64_C(1) << (120U - 64U)) - 1U;
  assert(member.update_slot.width == 120U);
  assert(member.update_slot.word_count == 2U);
  assert(target_value.width() == target_width);

  PackedLogic4 value(120U, Logic4::zero);
  for (std::size_t bit = 0; bit < value.width(); ++bit) {
    value.set(bit, decode_logic4(logic4_values[(bit + 1U) % 4U]));
  }
  value.insert_bits(target_value, target_offset);
  const auto aval = value.aval_words();
  const auto bval = value.bval_words();
  assert(aval.size() == member.update_wide_aval.size());
  assert(bval.size() == member.update_wide_bval.size());
  std::ranges::copy(aval, member.update_wide_aval.begin());
  std::ranges::copy(bval, member.update_wide_bval.begin());

  std::array<std::uint64_t, 2> mask {
    UINT64_C(0xa55aa55aa55aa55a) & ~first_target_word_mask,
    UINT64_C(0x5aa55aa55aa55aa5) & second_word_valid_mask
        & ~second_target_word_mask,
  };
  if (!stable_shadow) {
    mask[0] |= UINT64_C(1) << 61U;
    mask[1] |= UINT64_C(1) << 7U;
  }
  std::ranges::copy(mask, member.update_wide_mask.begin());
  member.update_slot.aval = aval[0];
  member.update_slot.bval = bval[0];
  member.update_slot.mask = mask[0];
  member.update_slot.active = 1U;
  member.update_slot.reserved = stable_shadow ? 1U : 0U;
  member.update_active_words[0] = 1U;
}

void expect_pure_wave_output(const PureWaveMemberState& member,
    std::size_t member_index, PureWaveShape shape);

void expect_pure_wave_matches_generic(
    const std::array<PureWaveMemberState, pure_wave_member_count>& members,
    const std::array<PureWaveShape, pure_wave_member_count>& shapes,
    const std::vector<PureWaveMemberSnapshot>& before,
    const std::vector<PureWaveMemberSnapshot>& generic,
    const std::vector<PureWaveMemberSnapshot>& pure_wave)
{
  assert(before.size() == members.size());
  assert(generic.size() == members.size());
  assert(pure_wave.size() == members.size());
  for (std::size_t index = 0; index < members.size(); ++index) {
    assert(pure_wave[index].frame == before[index].frame);
    assert(pure_wave[index].result == before[index].result);
    assert(pure_wave[index].entry_status == before[index].entry_status);
    assert(pure_wave[index].has_failure == before[index].has_failure);
    assert(pure_wave[index].callbacks == before[index].callbacks);
    assert(pure_wave[index].runtime == before[index].runtime);
    assert(pure_wave[index].direct_signal_aval
        == before[index].direct_signal_aval);
    assert(pure_wave[index].direct_signal_bval
        == before[index].direct_signal_bval);
    assert(pure_wave[index].direct_wide_signal_aval
        == before[index].direct_wide_signal_aval);
    assert(pure_wave[index].direct_wide_signal_bval
        == before[index].direct_wide_signal_bval);
    assert(pure_wave[index].update_slot == generic[index].update_slot);
    assert(pure_wave[index].update_wide_aval
        == generic[index].update_wide_aval);
    assert(pure_wave[index].update_wide_bval
        == generic[index].update_wide_bval);
    assert(pure_wave[index].update_wide_mask
        == generic[index].update_wide_mask);
    assert(pure_wave[index].update_active_words
        == generic[index].update_active_words);
    assert(pure_wave[index].queued == generic[index].queued);
    assert(pure_wave[index].waiting == generic[index].waiting);
    assert(pure_wave[index].process_status == generic[index].process_status);
    expect_pure_wave_output(members[index], index, shapes[index]);
  }
}

void expect_pure_wave_wide_slice_preserved(
    const PureWaveMemberSnapshot& before,
    const PureWaveMemberSnapshot& after,
    const bool expect_target_value_unchanged)
{
  constexpr std::uint64_t first_target_word_mask
      = std::numeric_limits<std::uint64_t>::max() << 60U;
  constexpr std::uint64_t second_target_word_mask
      = (UINT64_C(1) << 11U) - 1U;
  constexpr std::array<std::uint64_t, 2> target_masks {
    first_target_word_mask, second_target_word_mask,
  };
  assert(before.update_wide_aval.size() == target_masks.size());
  assert(before.update_wide_bval.size() == target_masks.size());
  assert(before.update_wide_mask.size() == target_masks.size());
  assert(after.update_wide_aval.size() == target_masks.size());
  assert(after.update_wide_bval.size() == target_masks.size());
  assert(after.update_wide_mask.size() == target_masks.size());
  for (std::size_t word = 0; word < target_masks.size(); ++word) {
    const auto outside_mask = ~target_masks[word];
    assert((after.update_wide_aval[word] & outside_mask)
        == (before.update_wide_aval[word] & outside_mask));
    assert((after.update_wide_bval[word] & outside_mask)
        == (before.update_wide_bval[word] & outside_mask));
    assert((after.update_wide_mask[word] & outside_mask)
        == (before.update_wide_mask[word] & outside_mask));
    assert((after.update_wide_mask[word] & target_masks[word])
        == target_masks[word]);
    if (expect_target_value_unchanged) {
      assert((after.update_wide_aval[word] & target_masks[word])
          == (before.update_wide_aval[word] & target_masks[word]));
      assert((after.update_wide_bval[word] & target_masks[word])
          == (before.update_wide_bval[word] & target_masks[word]));
    }
  }
}

void expect_pure_wave_wait_result(
    const PureWaveMemberState& member,
    const JitProcessCohortResumeEntry& entry,
    const PureWaveShape shape)
{
  const auto wait_instruction = shape == PureWaveShape::reducer ? 29U
      : shape == PureWaveShape::xor_reduce ? 5U
      : shape == PureWaveShape::copy ? 4U : 8U;
  assert(entry.status == FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY);
  assert(member.result.status == FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY);
  assert(member.result.instruction == wait_instruction);
  assert(member.result.delay == 0U);
  assert(member.frame.program_counter == wait_instruction + 1U);
  assert(member.frame.state == FSIM_JIT_FRAME_STATE_READY);
  assert(member.frame.last_instruction == wait_instruction);
  assert(member.frame.native_call_depth == 0U);
  assert(member.queued == 0U);
  assert(member.waiting == 1U);
  assert(member.process_status == 2U);
  assert(member.update_slot.active == 1U);
  assert(member.update_active_words[0] == 1U);
}

[[nodiscard]] PackedLogic4 pure_wave_signal_value(
    const PureWaveMemberState& member, const SignalId signal)
{
  const auto width = member.signal_widths[signal];
  const auto word_count = (width + 63U) / 64U;
  const auto offset = member.direct_wide_signal_offsets[signal];
  return PackedLogic4::from_word_planes(width,
      std::span<const std::uint64_t>(member.direct_wide_signal_aval)
          .subspan(offset, word_count),
      std::span<const std::uint64_t>(member.direct_wide_signal_bval)
          .subspan(offset, word_count));
}

[[nodiscard]] PackedLogic4 pure_wave_update_value(
    const PureWaveMemberState& member)
{
  if (member.update_slot.width > 64U) {
    return PackedLogic4::from_word_planes(member.update_slot.width,
        member.update_wide_aval, member.update_wide_bval);
  }
  return PackedLogic4::from_aval_bval(member.update_slot.width,
      member.update_slot.aval, member.update_slot.bval);
}

[[nodiscard]] PackedLogic4 expected_pure_wave_output(
    const PureWaveMemberState& member, const std::size_t member_index,
    const PureWaveShape shape)
{
  switch (shape) {
  case PureWaveShape::reducer: {
    const auto read_offset = reducer_read_offsets[member_index];
    const auto selector_offset = reducer_selector_offsets[member_index];
    const auto old_value
        = pure_wave_signal_value(member, member.mapped_signals[0])
              .extract_bits(read_offset, 15U);
    const auto prim
        = pure_wave_signal_value(member, member.mapped_signals[1]);
    const auto product = binary_value(BinaryOperator::bit_xor, old_value, prim);
    const auto selector = old_value.get(selector_offset);
    if (selector == Logic4::zero) {
      return old_value;
    }
    if (selector == Logic4::one) {
      return product;
    }
    PackedLogic4 merged(15U, Logic4::x);
    for (std::size_t bit = 0; bit < merged.width(); ++bit) {
      if (old_value.get(bit) == product.get(bit)) {
        merged.set(bit, old_value.get(bit));
      }
    }
    return merged;
  }
  case PureWaveShape::xor_reduce: {
    const auto input = pure_wave_signal_value(member, member.mapped_signals[0]);
    bool parity = false;
    bool unknown = false;
    for (std::size_t bit = 0; bit < input.width(); ++bit) {
      const auto value = input.get(bit);
      if (value == Logic4::one) {
        parity = !parity;
      } else if (value != Logic4::zero) {
        unknown = true;
      }
    }
    return PackedLogic4::from_msb_string(
        unknown ? "X" : parity ? "1" : "0");
  }
  case PureWaveShape::copy:
    return pure_wave_signal_value(member, member.mapped_signals[0]);
  case PureWaveShape::bit_and: {
    const auto lhs = pure_wave_signal_value(
        member, member.mapped_signals[0]).extract_bits(0U, 1U);
    const auto rhs = pure_wave_signal_value(
        member, member.mapped_signals[1]).extract_bits(0U, 1U);
    return binary_value(BinaryOperator::bit_and, lhs, rhs);
  }
  }
  assert(false);
  return PackedLogic4 { };
}

void expect_pure_wave_output(const PureWaveMemberState& member,
    const std::size_t member_index, const PureWaveShape shape)
{
  const auto offset = shape == PureWaveShape::reducer
      ? reducer_write_offsets[member_index]
      : shape == PureWaveShape::copy ? 105U : 0U;
  const auto width = shape == PureWaveShape::reducer ? 15U
      : shape == PureWaveShape::copy ? 15U : 1U;
  const auto staged = pure_wave_update_value(member).extract_bits(offset, width);
  assert(staged == expected_pure_wave_output(member, member_index, shape));
}

void test_pure_wave_at_level(const JitOptimizationLevel optimization)
{
  LlvmJitOptions options;
  options.optimization = optimization;
  options.debug_instrumentation = false;
  options.require_direct_update_slots = true;
  LlvmJit jit { options };

  constexpr std::array<PureWaveShape, pure_wave_member_count> shapes {
    PureWaveShape::reducer, PureWaveShape::reducer,
    PureWaveShape::reducer, PureWaveShape::reducer,
    PureWaveShape::reducer, PureWaveShape::reducer,
    PureWaveShape::reducer, PureWaveShape::xor_reduce,
    PureWaveShape::copy, PureWaveShape::bit_and,
    PureWaveShape::bit_and, PureWaveShape::bit_and,
    PureWaveShape::bit_and,
  };
  std::array<PureWaveMemberState, pure_wave_member_count> members;
  std::array<JitProcessCohortResumeEntry, pure_wave_member_count> entries;
  std::array<JitProcessHandle, pure_wave_member_count> handles;
  std::array<JitProcessFrameLayout, pure_wave_member_count> layouts;

  for (std::size_t index = 0; index < members.size(); ++index) {
    const auto canonical = make_pure_wave_process(shapes[index], index);
    const auto canonical_name = canonical.name;
    const auto widths = pure_wave_signal_widths(shapes[index]);
    const std::array<ValueKind, 3> kinds {
      ValueKind::logic4, ValueKind::logic4, ValueKind::logic4
    };
    if (index >= 10U) {
      handles[index] = handles[9];
      layouts[index] = layouts[9];
      members[index].process
          = make_pure_wave_and(static_cast<std::uint32_t>(index));
    } else {
      jit.add_process(canonical_name, canonical, widths, kinds);
      handles[index] = jit.lookup(canonical_name);
      assert(handles[index]);
      layouts[index] = jit.frame_layout(handles[index]);
      members[index].process = canonical;
    }
    auto& member = members[index];
    assert(member.process.operations.size()
        == (shapes[index] == PureWaveShape::reducer ? 31U
            : shapes[index] == PureWaveShape::xor_reduce ? 7U
            : shapes[index] == PureWaveShape::copy ? 6U : 10U));
    assert(member.process.debug_locals.empty());
    member.handle = handles[index];
    member.layout = layouts[index];
    if (index < 9U) {
      member.mapped_signals = {
        static_cast<SignalId>(4U + index * 3U),
        static_cast<SignalId>(5U + index * 3U),
        static_cast<SignalId>(6U + index * 3U),
      };
    } else if (index < 11U) {
      member.mapped_signals = {
        31U, 32U, static_cast<SignalId>(33U + index - 9U),
      };
    } else {
      member.mapped_signals = {
        35U, 36U, static_cast<SignalId>(37U + index - 11U),
      };
    }
    remap_pure_wave_process(member.process, member.mapped_signals,
        static_cast<std::uint32_t>(100U + index));
    assert(!member.layout.tracks_register_initialization);

    for (const auto canonical_signal : member.layout.direct_read_signals) {
      member.read_signals.push_back(member.mapped_signals[canonical_signal]);
    }
    SignalId canonical_output = 0U;
    if (shapes[index] == PureWaveShape::xor_reduce
        || shapes[index] == PureWaveShape::copy) {
      canonical_output = 1U;
    } else if (shapes[index] == PureWaveShape::bit_and) {
      canonical_output = 2U;
    }
    member.update_signals = { member.mapped_signals[canonical_output] };

    member.register_aval.resize(member.layout.register_word_count);
    member.register_bval.resize(member.layout.register_word_count);
    member.register_initialized.resize(member.layout.register_count);
    member.direct_signal_aval.resize(pure_wave_signal_count);
    member.direct_signal_bval.resize(pure_wave_signal_count);
    member.direct_wide_signal_aval.resize(
        pure_wave_signal_count * pure_wave_wide_words_per_signal);
    member.direct_wide_signal_bval.resize(
        pure_wave_signal_count * pure_wave_wide_words_per_signal);
    member.direct_wide_signal_offsets.resize(pure_wave_signal_count);
    member.signal_widths.resize(pure_wave_signal_count);
    for (std::size_t signal = 0; signal < pure_wave_signal_count; ++signal) {
      member.direct_wide_signal_offsets[signal]
          = static_cast<std::uint32_t>(
              signal * pure_wave_wide_words_per_signal);
    }
    for (std::size_t signal = 0; signal < 3U; ++signal) {
      member.signal_widths[member.mapped_signals[signal]] = widths[signal];
    }
    const auto output_width = widths[canonical_output];
    const auto output_words = (output_width + 63U) / 64U;
    member.update_wide_aval.resize(output_words);
    member.update_wide_bval.resize(output_words);
    member.update_wide_mask.resize(output_words);
    member.update_active_words.resize(1U);
    member.update_slot.wide_aval = member.update_wide_aval.data();
    member.update_slot.wide_bval = member.update_wide_bval.data();
    member.update_slot.wide_mask = member.update_wide_mask.data();
    member.update_slot.word_count
        = static_cast<std::uint32_t>(output_words);
    member.update_slot.width = output_width;
    member.callbacks = TestRuntime { };
    member.runtime = abi(member.callbacks);
    member.runtime.direct_signal_aval = member.direct_signal_aval.data();
    member.runtime.direct_signal_bval = member.direct_signal_bval.data();
    member.runtime.direct_read_signals = member.read_signals.data();
    member.runtime.direct_read_signal_count
        = static_cast<std::uint32_t>(member.read_signals.size());
    member.runtime.direct_signal_count
        = static_cast<std::uint32_t>(pure_wave_signal_count);
    member.runtime.direct_wide_signal_aval
        = member.direct_wide_signal_aval.data();
    member.runtime.direct_wide_signal_bval
        = member.direct_wide_signal_bval.data();
    member.runtime.direct_wide_signal_offsets
        = member.direct_wide_signal_offsets.data();
    member.runtime.direct_wide_signal_offset_count
        = static_cast<std::uint32_t>(pure_wave_signal_count);
    member.runtime.direct_wide_word_count
        = static_cast<std::uint32_t>(member.direct_wide_signal_aval.size());
    member.runtime.direct_update_slots = &member.update_slot;
    member.runtime.direct_update_slot_count = 1U;
    member.runtime.direct_update_active_words
        = member.update_active_words.data();
    member.runtime.direct_update_active_word_count
        = static_cast<std::uint32_t>(member.update_active_words.size());
    member.runtime.flags = 0U;
    entries[index] = JitProcessCohortResumeEntry {
      jit.bind(member.handle), member.runtime, member.frame, member.result,
      &member.queued, &member.waiting, &member.process_status,
    };
  }
  for (std::size_t index = 9U; index < members.size(); ++index) {
    assert(handles[index] == handles[9]);
  }
  for (const auto first : { 9U, 11U }) {
    const auto second = first + 1U;
    assert(members[first].mapped_signals[0]
        == members[second].mapped_signals[0]);
    assert(members[first].mapped_signals[1]
        == members[second].mapped_signals[1]);
    assert(members[first].mapped_signals[2]
        != members[second].mapped_signals[2]);
    assert(members[first].process.operations.size()
        == members[second].process.operations.size());
    assert(operation_get<ReadSignal>(
        members[first].process.operations[2]).signal
        == operation_get<ReadSignal>(
            members[second].process.operations[2]).signal);
    assert(operation_get<WriteUpdateSlice>(
        members[first].process.operations[7]).signal
        != operation_get<WriteUpdateSlice>(
            members[second].process.operations[7]).signal);
    assert(members[first].update_signals != members[second].update_signals);
  }

  for (std::size_t alignment = 0;
       alignment < reducer_read_offsets.size(); ++alignment) {
    const auto& process = members[alignment].process;
    assert(operation_get<Extract>(process.operations[3]).offset
        == reducer_read_offsets[alignment]);
    assert(operation_get<Extract>(process.operations[4]).offset
        == reducer_selector_offsets[alignment]);
    assert(operation_get<WriteUpdateSlice>(process.operations[28]).offset
        == reducer_write_offsets[alignment]);
  }
  assert(reducer_write_offsets[2] == 60U);
  assert(reducer_write_offsets[2] + 15U > 64U);
  assert(members[9].read_signals == members[10].read_signals);
  assert(members[11].read_signals == members[12].read_signals);
  assert(members[9].read_signals != members[11].read_signals);

  for (const auto [first, second] : {
           std::pair<std::size_t, std::size_t> { 9U, 10U },
           { 11U, 12U } }) {
    auto& shared = members[first];
    auto& peer = members[second];
    peer.runtime.direct_signal_aval = shared.direct_signal_aval.data();
    peer.runtime.direct_signal_bval = shared.direct_signal_bval.data();
    peer.runtime.direct_read_signals = shared.read_signals.data();
    peer.runtime.direct_wide_signal_aval
        = shared.direct_wide_signal_aval.data();
    peer.runtime.direct_wide_signal_bval
        = shared.direct_wide_signal_bval.data();
    peer.runtime.direct_wide_signal_offsets
        = shared.direct_wide_signal_offsets.data();
  }
  assert(members[9].runtime.direct_signal_aval
      == members[10].runtime.direct_signal_aval);
  assert(members[11].runtime.direct_signal_aval
      == members[12].runtime.direct_signal_aval);
  assert(members[9].runtime.direct_signal_aval
      != members[11].runtime.direct_signal_aval);

  for (std::size_t index = 0; index < members.size(); ++index) {
    configure_pure_wave_member(members[index], index, shapes[index], 0U);
    jit.initialize_frame(members[index].handle, members[index].frame,
        members[index].register_aval, members[index].register_bval,
        members[index].register_initialized);
    members[index].result = new_resume_result();
    members[index].queued = 1U;
    members[index].waiting = 0U;
    members[index].process_status = 2U;
  }
  assert(jit.resume_cohort_prevalidated(entries) == entries.size());
  for (std::size_t index = 0; index < entries.size(); ++index) {
    expect_pure_wave_wait_result(members[index], entries[index], shapes[index]);
  }
  for (auto& member : members) {
    clear_pure_wave_update(member, true);
  }
  for (auto& entry : entries) {
    entry.status = 0U;
    entry.failure = { };
  }
  const auto generic_binding = jit.bind_cohort_prevalidated(entries);
  assert(generic_binding);

  // Singleton warm resumes leave the private cohort result value-initialized.
  for (auto& member : members) {
    member.result = { };
    assert(member.result.abi_version == 0U);
    assert(member.result.struct_size == 0U);
  }
  std::array<JitPureWaveMemberBinding, pure_wave_member_count> member_bindings;
  for (std::size_t index = 0; index < members.size(); ++index) {
    const JitPureWaveMember candidate {
      entries[index], &members[index].process,
      std::span<const SignalId>(members[index].update_signals),
    };
    const auto binding = jit.bind_pure_wave_member_prevalidated(candidate);
    assert(binding);
    member_bindings[index] = *binding;
  }
  constexpr std::array<std::size_t, 11> task_ends {
    1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 11U, 13U
  };
  const auto wave_binding
      = jit.bind_pure_wave_prevalidated(member_bindings, task_ends);
  assert(wave_binding);
  std::array<JitPureWaveMemberLease, pure_wave_member_count> member_leases;
  std::array<const JitPureWaveMemberLease*, pure_wave_member_count>
      member_lease_pointers;
  std::array<PureWaveFixtureExecutor, pure_wave_member_count>
      prepared_executors;
  std::array<ProcessUpdateSlotView, pure_wave_member_count>
      prepared_update_slots;
  std::array<PureWavePreparedMember, pure_wave_member_count>
      prepared_members;
  std::array<const PureWavePreparedMember*, pure_wave_member_count>
      prepared_member_pointers;
  std::uint64_t prepared_owner_identity { };
  for (std::size_t index = 0; index < member_bindings.size(); ++index) {
    const auto lease = jit.acquire_pure_wave_member_lease(
        member_bindings[index]);
    assert(lease);
    member_leases[index] = *lease;
    assert(member_leases[index]);
    member_lease_pointers[index] = &member_leases[index];

    auto& member = members[index];
    auto& prepared = prepared_members[index];
    const bool wide_update = member.update_slot.width > 64U;
    const auto word_count = member.update_slot.word_count;
    prepared_update_slots[index] = {
      member.update_signals[0], member.update_slot.width, word_count,
      &member.update_slot.active,
      wide_update ? member.update_wide_aval.data()
                  : &member.update_slot.aval,
      wide_update ? member.update_wide_bval.data()
                  : &member.update_slot.bval,
      wide_update ? member.update_wide_mask.data()
                  : &member.update_slot.mask,
    };
    prepared.process = member.process.id;
    prepared.executor = &prepared_executors[index];
    prepared.owner = &prepared_owner_identity;
    prepared.domain = member_leases[index].prepared_domain();
    prepared.compiler_view = member_leases[index].prepared_view();
    prepared.update_batch = {
      member.process.id,
      std::span<const ProcessUpdateSlotView>(
          &prepared_update_slots[index], 1U),
      member.update_active_words,
    };
    if (shapes[index] == PureWaveShape::bit_and) {
      prepared.and_lhs = member.read_signals[0];
      prepared.and_rhs = member.read_signals[1];
    }
    prepared.resume_instruction = member.frame.program_counter;
    prepared.owner_epoch = 1U;
    prepared.generation = 1U;
    prepared.compiler_generation
        = member_leases[index].prepared_generation();
    switch (shapes[index]) {
    case PureWaveShape::reducer:
      prepared.shape = PureWavePreparedShape::reducer31;
      break;
    case PureWaveShape::xor_reduce:
      prepared.shape = PureWavePreparedShape::reduction7;
      break;
    case PureWaveShape::copy:
      prepared.shape = PureWavePreparedShape::wide_copy6;
      break;
    case PureWaveShape::bit_and:
      prepared.shape = PureWavePreparedShape::logic4_bit_and;
      break;
    }
    prepared.valid = true;
    assert(prepared.compiler_view != nullptr);
    assert(prepared.domain != nullptr);
    prepared_member_pointers[index] = &prepared;
  }
  for (const auto& prepared : prepared_members) {
    assert(prepared.domain == prepared_members[0].domain);
  }

  for (std::size_t index = 0; index < members.size(); ++index) {
    configure_pure_wave_member(members[index], index, shapes[index], 0U);
    clear_pure_wave_update(members[index]);
  }
  for (auto& entry : entries) {
    entry.status = 0U;
    entry.failure = { };
  }
  assert(jit.resume_cohort_prevalidated(generic_binding, entries)
      == entries.size());
  const auto generic_virgin_result = snapshot_pure_wave(members, entries);
  for (std::size_t index = 0; index < members.size(); ++index) {
    expect_pure_wave_wait_result(members[index], entries[index], shapes[index]);
    expect_pure_wave_output(members[index], index, shapes[index]);
  }

  for (std::size_t index = 0; index < members.size(); ++index) {
    configure_pure_wave_member(members[index], index, shapes[index], 0U);
    clear_pure_wave_update(members[index], true);
    members[index].result = { };
  }
  for (auto& entry : entries) {
    entry.status = 0U;
    entry.failure = { };
  }
  const auto before_virgin_result_wave = snapshot_pure_wave(members, entries);
  assert(jit.try_resume_pure_wave_prevalidated(*wave_binding));
  const auto virgin_result_wave = snapshot_pure_wave(members, entries);
  expect_pure_wave_matches_generic(members, shapes,
      before_virgin_result_wave, generic_virgin_result, virgin_result_wave);
  for (std::size_t index = 0; index < members.size(); ++index) {
    assert(before_virgin_result_wave[index].result.abi_version == 0U);
    assert(before_virgin_result_wave[index].result.struct_size == 0U);
    assert(virgin_result_wave[index].result.abi_version == 0U);
    assert(virgin_result_wave[index].result.struct_size == 0U);
  }

  for (std::size_t index = 0; index < members.size(); ++index) {
    configure_pure_wave_member(members[index], index, shapes[index], 0U);
    clear_pure_wave_update(members[index], true);
    members[index].result = { };
  }
  for (auto& entry : entries) {
    entry.status = 0U;
    entry.failure = { };
  }
  const auto before_leased_wave = snapshot_pure_wave(members, entries);
  const auto leased_wave_count = jit.try_resume_pure_wave_members_prevalidated(
      member_lease_pointers, task_ends);
  assert(leased_wave_count);
  assert(*leased_wave_count == task_ends.size());
  const auto leased_wave = snapshot_pure_wave(members, entries);
  expect_pure_wave_matches_generic(members, shapes,
      before_leased_wave, generic_virgin_result, leased_wave);
  for (std::size_t index = 0; index < members.size(); ++index) {
    assert(before_leased_wave[index].result.abi_version == 0U);
    assert(before_leased_wave[index].result.struct_size == 0U);
    assert(leased_wave[index].result.abi_version == 0U);
    assert(leased_wave[index].result.struct_size == 0U);
  }

  auto invalid_task_ends = task_ends;
  invalid_task_ends[0] = 0U;
  for (auto& member : members) {
    clear_pure_wave_update(member, true);
  }
  for (auto& entry : entries) {
    entry.status = 0U;
    entry.failure = { };
  }
  const auto before_invalid_task_ends = snapshot_pure_wave(members, entries);
  assert(!jit.try_resume_pure_wave_members_prevalidated(
      member_lease_pointers, invalid_task_ends));
  assert(!jit.try_resume_pure_wave_prepared_members_prevalidated(
      prepared_member_pointers, invalid_task_ends));
  assert(snapshot_pure_wave(members, entries) == before_invalid_task_ends);

  std::array<bool, logic4_values.size()> seen_reducer_selectors { };
  std::array<bool, 16U> seen_and_pairs { };
  for (std::size_t row = 0; row < 16U; ++row) {
    for (std::size_t index = 0; index < members.size(); ++index) {
      configure_pure_wave_member(members[index], index, shapes[index], row);
    }
    for (auto& member : members) {
      clear_pure_wave_update(member);
    }
    for (auto& entry : entries) {
      entry.status = 0U;
      entry.failure = { };
    }
    assert(jit.resume_cohort_prevalidated(generic_binding, entries)
        == entries.size());
    const auto generic = snapshot_pure_wave(members, entries);
    for (std::size_t index = 0; index < members.size(); ++index) {
      expect_pure_wave_wait_result(members[index], entries[index], shapes[index]);
      expect_pure_wave_output(members[index], index, shapes[index]);
    }

    for (auto& member : members) {
      clear_pure_wave_update(member, true);
    }
    for (auto& entry : entries) {
      entry.status = 0U;
      entry.failure = { };
    }
    const auto before_wave = snapshot_pure_wave(members, entries);
    assert(jit.try_resume_pure_wave_prevalidated(*wave_binding));
    const auto pure_wave = snapshot_pure_wave(members, entries);
    expect_pure_wave_matches_generic(
        members, shapes, before_wave, generic, pure_wave);

    for (auto& member : members) {
      clear_pure_wave_update(member, true);
      member.result = { };
    }
    for (auto& entry : entries) {
      entry.status = 0U;
      entry.failure = { };
    }
    const auto before_prepared_wave = snapshot_pure_wave(members, entries);
    const auto prepared_wave_count
        = jit.try_resume_pure_wave_prepared_members_prevalidated(
            prepared_member_pointers, task_ends);
    assert(prepared_wave_count);
    assert(*prepared_wave_count == task_ends.size());
    const auto prepared_wave = snapshot_pure_wave(members, entries);
    expect_pure_wave_matches_generic(
        members, shapes, before_prepared_wave, generic, prepared_wave);
    for (std::size_t index = 0; index < members.size(); ++index) {
      assert(before_prepared_wave[index].result.abi_version == 0U);
      assert(before_prepared_wave[index].result.struct_size == 0U);
      assert(prepared_wave[index].result.abi_version == 0U);
      assert(prepared_wave[index].result.struct_size == 0U);
    }
    seen_reducer_selectors[row % 4U] = true;
    seen_and_pairs[row] = true;
  }
  assert(std::ranges::all_of(seen_reducer_selectors,
      [](const auto seen) { return seen; }));
  assert(std::ranges::all_of(seen_and_pairs,
      [](const auto seen) { return seen; }));

  for (const auto [row, stable_shadow] : {
           std::pair<std::size_t, bool> { 0U, true },
           { 1U, false } }) {
    for (std::size_t index = 0; index < members.size(); ++index) {
      configure_pure_wave_member(members[index], index, shapes[index], row);
      clear_pure_wave_update(members[index]);
    }
    for (auto& entry : entries) {
      entry.status = 0U;
      entry.failure = { };
    }
    const auto seeded_value = stable_shadow
        ? expected_pure_wave_output(members[2], 2U, PureWaveShape::reducer)
        : PackedLogic4(15U, Logic4::zero);
    seed_pure_wave_wide_update(members[2], seeded_value, stable_shadow);
    const auto generic_initial = snapshot_pure_wave(members, entries);
    assert(jit.resume_cohort_prevalidated(generic_binding, entries)
        == entries.size());
    const auto generic = snapshot_pure_wave(members, entries);
    for (std::size_t index = 0; index < members.size(); ++index) {
      expect_pure_wave_wait_result(members[index], entries[index], shapes[index]);
      expect_pure_wave_output(members[index], index, shapes[index]);
    }
    expect_pure_wave_wide_slice_preserved(
        generic_initial[2], generic[2], stable_shadow);

    for (std::size_t index = 0; index < members.size(); ++index) {
      configure_pure_wave_member(members[index], index, shapes[index], row);
      clear_pure_wave_update(members[index], true);
    }
    for (auto& entry : entries) {
      entry.status = 0U;
      entry.failure = { };
    }
    seed_pure_wave_wide_update(members[2], seeded_value, stable_shadow);
    const auto before_wave = snapshot_pure_wave(members, entries);
    assert(jit.try_resume_pure_wave_prevalidated(*wave_binding));
    const auto pure_wave = snapshot_pure_wave(members, entries);
    expect_pure_wave_matches_generic(
        members, shapes, before_wave, generic, pure_wave);
    expect_pure_wave_wide_slice_preserved(
        before_wave[2], pure_wave[2], stable_shadow);

    for (std::size_t index = 0; index < members.size(); ++index) {
      configure_pure_wave_member(members[index], index, shapes[index], row);
      clear_pure_wave_update(members[index], true);
      members[index].result = { };
    }
    for (auto& entry : entries) {
      entry.status = 0U;
      entry.failure = { };
    }
    seed_pure_wave_wide_update(members[2], seeded_value, stable_shadow);
    const auto before_prepared_wave = snapshot_pure_wave(members, entries);
    const auto prepared_wave_count
        = jit.try_resume_pure_wave_prepared_members_prevalidated(
            prepared_member_pointers, task_ends);
    assert(prepared_wave_count);
    assert(*prepared_wave_count == task_ends.size());
    const auto prepared_wave = snapshot_pure_wave(members, entries);
    expect_pure_wave_matches_generic(
        members, shapes, before_prepared_wave, generic, prepared_wave);
    expect_pure_wave_wide_slice_preserved(
        before_prepared_wave[2], prepared_wave[2], stable_shadow);
    for (std::size_t index = 0; index < members.size(); ++index) {
      assert(before_prepared_wave[index].result.abi_version == 0U);
      assert(before_prepared_wave[index].result.struct_size == 0U);
      assert(prepared_wave[index].result.abi_version == 0U);
      assert(prepared_wave[index].result.struct_size == 0U);
    }
  }

  auto incompatible_middle = members[4].process;
  auto& incompatible_reduction
      = operation_get<Binary>(incompatible_middle.operations[13]);
  incompatible_reduction.operation = BinaryOperator::bit_or;
  const auto before_incompatible = snapshot_pure_wave(members, entries);
  const JitPureWaveMember invalid_member {
    entries[4], &incompatible_middle,
    std::span<const SignalId>(members[4].update_signals),
  };
  assert(!jit.bind_pure_wave_member_prevalidated(invalid_member));
  assert(snapshot_pure_wave(members, entries) == before_incompatible);
  auto incompatible_bindings = member_bindings;
  incompatible_bindings[4] = JitPureWaveMemberBinding { };
  assert(!jit.bind_pure_wave_prevalidated(incompatible_bindings, task_ends));
  assert(snapshot_pure_wave(members, entries) == before_incompatible);

  const auto expect_member_bind_decline_without_mutation
      = [&](Process& process, const std::size_t index) {
    const auto before = snapshot_pure_wave(members, entries);
    const JitPureWaveMember candidate {
      entries[index], &process,
      std::span<const SignalId>(members[index].update_signals),
    };
    assert(!jit.bind_pure_wave_member_prevalidated(candidate));
    assert(snapshot_pure_wave(members, entries) == before);
  };

  auto mismatched_branch_extract = members[2].process;
  ++operation_get<Extract>(mismatched_branch_extract.operations[11]).offset;
  expect_member_bind_decline_without_mutation(mismatched_branch_extract, 2U);

  auto noncurrent_repeated_read = members[2].process;
  operation_get<ReadSignal>(noncurrent_repeated_read.operations[10]).kind
      = SignalReadKind::sampled;
  expect_member_bind_decline_without_mutation(noncurrent_repeated_read, 2U);

  auto aliased_register_roles = members[2].process;
  const auto one_register
      = operation_get<LoadConstant>(aliased_register_roles.operations[5])
            .destination;
  auto& zero_constant
      = operation_get<LoadConstant>(aliased_register_roles.operations[6]);
  zero_constant.destination = one_register;
  operation_get<Binary>(aliased_register_roles.operations[8]).rhs
      = one_register;
  expect_member_bind_decline_without_mutation(aliased_register_roles, 2U);

  for (auto& member : members) {
    clear_pure_wave_update(member, true);
  }
  for (auto& entry : entries) {
    entry.status = 0U;
    entry.failure = { };
  }
  const auto expected_pc = members[1].frame.program_counter;
  members[1].frame.program_counter = 1U;
  const auto before_bad_second_member = snapshot_pure_wave(members, entries);
  assert(!jit.try_resume_pure_wave_prevalidated(*wave_binding));
  assert(snapshot_pure_wave(members, entries) == before_bad_second_member);
  members[1].frame.program_counter = expected_pc;

  constexpr std::size_t released_member_index = 6U;
  assert(jit.release_pure_wave_member_binding(
      member_bindings[released_member_index]));
  assert(member_leases[released_member_index]);
  const auto before_released_lease = snapshot_pure_wave(members, entries);
  assert(!jit.try_resume_pure_wave_members_prevalidated(
      member_lease_pointers, task_ends));
  assert(!jit.try_resume_pure_wave_prepared_members_prevalidated(
      prepared_member_pointers, task_ends));
  assert(snapshot_pure_wave(members, entries) == before_released_lease);

  assert(jit.release_cohort_binding(*wave_binding));
  for (std::size_t index = 0; index < member_bindings.size(); ++index) {
    if (index != released_member_index) {
      assert(jit.release_pure_wave_member_binding(member_bindings[index]));
    }
  }
  assert(jit.release_cohort_binding(generic_binding));
}

} // namespace

void test_logic4_bit_and_cohort_at_level(
    const JitOptimizationLevel optimization)
{
  LlvmJitOptions options;
  options.optimization = optimization;
  options.debug_instrumentation = false;
  options.require_direct_update_slots = true;
  LlvmJit jit { options };
  const std::array<std::uint32_t, 3> signal_widths { 2U, 2U, 2U };
  const std::array<ValueKind, 3> signal_kinds {
      ValueKind::logic4, ValueKind::logic4, ValueKind::logic4
  };

  std::array<JitProcessHandle, cohort_member_count> handles;
  std::array<JitProcessFrameLayout, cohort_member_count> layouts;
  std::array<fsim::compiler::JitProcessCohortLogic4BitAndMember,
      cohort_member_count> shapes;
  std::array<MemberState, cohort_member_count> members;
  for (std::size_t index = 0; index < cohort_member_count; ++index) {
    const auto process = make_logic4_bit_and_process(
        static_cast<std::uint32_t>(index), static_cast<std::uint32_t>(index),
        index == 0U);
    jit.add_process(process.name, process, signal_widths, signal_kinds);
    handles[index] = jit.lookup(process.name);
    assert(handles[index]);
    layouts[index] = jit.frame_layout(handles[index]);
    assert((layouts[index].direct_read_signals
        == std::vector<SignalId> { 0U, 1U }));
    assert((layouts[index].direct_update_signals
        == std::vector<SignalId> { 2U }));
    assert(layouts[index].tracks_register_initialization == (index == 0U));
    assert(layouts[index].register_word_offsets[0] == 0U);
    assert(layouts[index].register_word_offsets[2] == 1U);
    assert(layouts[index].register_word_offsets[4] == 2U);
    assert(layouts[index].register_word_offsets[6] == 3U);
    assert(layouts[index].register_word_offsets[8] == 4U);
    assert(layouts[index].register_word_offsets[2] != 2U);
    shapes[index] = make_member_shape(
        layouts[index], static_cast<std::uint32_t>(index), index == 0U);
    members[index].register_aval.resize(layouts[index].register_word_count);
    members[index].register_bval.resize(layouts[index].register_word_count);
    members[index].register_initialized.resize(layouts[index].register_count);
  }

  std::array<fsim::compiler::JitProcessCohortResumeEntry,
      cohort_member_count> entries {
      fsim::compiler::JitProcessCohortResumeEntry {
          jit.bind(handles[0]), members[0].runtime, members[0].frame,
          members[0].result, &members[0].queued, &members[0].waiting,
          &members[0].process_status },
      fsim::compiler::JitProcessCohortResumeEntry {
          jit.bind(handles[1]), members[1].runtime, members[1].frame,
          members[1].result, &members[1].queued, &members[1].waiting,
          &members[1].process_status },
  };

  std::array<MemberState, cohort_member_count> compact_members;
  std::array<JitProcessFrameLayout, cohort_member_count> compact_layouts {
      layouts[1], layouts[1]
  };
  std::array<fsim::compiler::JitProcessCohortLogic4BitAndMember,
      cohort_member_count> compact_shapes { shapes[1], shapes[1] };
  for (auto& member : compact_members) {
    member.register_aval.resize(layouts[1].register_word_count);
    member.register_bval.resize(layouts[1].register_word_count);
    member.register_initialized.resize(layouts[1].register_count);
  }
  std::array<fsim::compiler::JitProcessCohortResumeEntry,
      cohort_member_count> compact_entries {
      fsim::compiler::JitProcessCohortResumeEntry {
          jit.bind(handles[1]), compact_members[0].runtime,
          compact_members[0].frame, compact_members[0].result,
          &compact_members[0].queued, &compact_members[0].waiting,
          &compact_members[0].process_status },
      fsim::compiler::JitProcessCohortResumeEntry {
          jit.bind(handles[1]), compact_members[1].runtime,
          compact_members[1].frame, compact_members[1].result,
          &compact_members[1].queued, &compact_members[1].waiting,
          &compact_members[1].process_status },
  };

  const auto share_compact_input_planes = [&] {
    compact_members[1].runtime.direct_signal_aval
        = compact_members[0].direct_signal_aval.data();
    compact_members[1].runtime.direct_signal_bval
        = compact_members[0].direct_signal_bval.data();
    compact_members[1].runtime.direct_read_signals
        = compact_members[0].direct_read_signals.data();
  };

  const auto reset_compact_cohort = [&] {
    for (std::size_t index = 0; index < cohort_member_count; ++index) {
      reset_member(compact_members[index], jit, handles[1], 1U,
          logic4_values[0], logic4_values[0], InitialUpdateState::clean);
      compact_entries[index].status = 0U;
      compact_entries[index].failure = { };
    }
    share_compact_input_planes();
  };

  const auto reset_cohort = [&](const std::size_t row,
                                const InitialUpdateState initial_update) {
    for (std::size_t index = 0; index < cohort_member_count; ++index) {
      reset_member(members[index], jit, handles[index], index,
          logic4_values[row / 4U], logic4_values[row % 4U], initial_update);
      entries[index].status = 0U;
      entries[index].failure = { };
    }
  };
  reset_cohort(0U, InitialUpdateState::clean);

  const auto initial_warm_count
      = jit.resume_cohort_prevalidated(entries);
  assert(initial_warm_count == cohort_member_count);
  for (std::size_t index = 0; index < cohort_member_count; ++index) {
    assert(entries[index].status
        == FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY);
    assert(members[index].frame.program_counter == 9U);
    assert(members[index].frame.state == FSIM_JIT_FRAME_STATE_READY);
    assert(members[index].frame.last_instruction == 8U);
  }
  const auto generic_binding = jit.bind_cohort_prevalidated(entries);
  assert(generic_binding);
  const auto specialized_binding = jit.bind_logic4_bit_and_cohort_prevalidated(
      entries, shapes);
  assert(specialized_binding);

  reset_compact_cohort();
  const auto initial_compact_warm_count
      = jit.resume_cohort_prevalidated(compact_entries);
  assert(initial_compact_warm_count == cohort_member_count);
  for (std::size_t index = 0; index < cohort_member_count; ++index) {
    assert(compact_entries[index].status
        == FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY);
    assert(compact_members[index].frame.program_counter == 9U);
    assert(compact_members[index].frame.state == FSIM_JIT_FRAME_STATE_READY);
    assert(compact_members[index].frame.last_instruction == 8U);
  }
  share_compact_input_planes();
  const auto second_compact_read = compact_members[1].direct_read_signals[0];
  compact_members[1].runtime.direct_read_signals
      = compact_members[1].direct_read_signals.data();
  compact_members[1].direct_read_signals[0] = 5U;
  const auto before_different_input_pair
      = snapshot_cohort(compact_members, compact_entries, 0U);
  const auto different_input_pair_binding
      = jit.bind_compact_logic4_bit_and_cohort_prevalidated(
          compact_entries, compact_shapes);
  assert(!different_input_pair_binding);
  assert(snapshot_cohort(compact_members, compact_entries, 0U)
      == before_different_input_pair);
  compact_members[1].direct_read_signals[0] = second_compact_read;

  for (std::size_t index = 0; index < cohort_member_count; ++index) {
    const auto sources = source_bits(1U, logic4_values[0], logic4_values[0]);
    configure_direct_runtime(compact_members[index], 1U,
        sources.lhs_msb, sources.lhs_lsb,
        sources.rhs_msb, sources.rhs_lsb, InitialUpdateState::clean);
  }
  share_compact_input_planes();
  const auto compact_generic_binding
      = jit.bind_cohort_prevalidated(compact_entries);
  assert(compact_generic_binding);
  const auto compact_binding = jit.bind_compact_logic4_bit_and_cohort_prevalidated(
      compact_entries, compact_shapes);
  assert(compact_binding);

  const auto prepare_activation = [&](const std::size_t row,
                                      const InitialUpdateState initial_update) {
    reset_cohort(0U, InitialUpdateState::clean);
    const auto warm_count
        = jit.resume_cohort_prevalidated(generic_binding, entries);
    assert(warm_count == cohort_member_count);
    for (std::size_t index = 0; index < cohort_member_count; ++index) {
      assert(members[index].frame.program_counter == 9U);
      assert(members[index].frame.state == FSIM_JIT_FRAME_STATE_READY);
      assert(members[index].frame.last_instruction == 8U);
      const auto sources = source_bits(index,
          logic4_values[row / 4U], logic4_values[row % 4U]);
      configure_direct_runtime(members[index], index,
          sources.lhs_msb, sources.lhs_lsb,
          sources.rhs_msb, sources.rhs_lsb, initial_update);
      members[index].result = new_resume_result();
      members[index].queued = 1U;
      members[index].waiting = 0U;
      members[index].process_status = 2U;
      entries[index].status = 0U;
      entries[index].failure = { };
    }
  };

  const auto prepare_compact_activation = [&](const std::size_t row,
                                             const InitialUpdateState initial_update) {
    reset_compact_cohort();
    const auto warm_count
        = jit.resume_cohort_prevalidated(
            compact_generic_binding, compact_entries);
    assert(warm_count == cohort_member_count);
    const auto lhs = logic4_values[row / 4U];
    const auto rhs = logic4_values[row % 4U];
    const auto sources = source_bits(1U, lhs, rhs);
    for (std::size_t index = 0; index < cohort_member_count; ++index) {
      assert(compact_members[index].frame.program_counter == 9U);
      assert(compact_members[index].frame.state == FSIM_JIT_FRAME_STATE_READY);
      assert(compact_members[index].frame.last_instruction == 8U);
      configure_direct_runtime(compact_members[index], 1U,
          sources.lhs_msb, sources.lhs_lsb,
          sources.rhs_msb, sources.rhs_lsb, initial_update);
      compact_members[index].result = new_resume_result();
      compact_members[index].queued = 1U;
      compact_members[index].waiting = 0U;
      compact_members[index].process_status = 2U;
      compact_entries[index].status = 0U;
      compact_entries[index].failure = { };
    }
    share_compact_input_planes();
  };

  for (const auto initial_update : {
           InitialUpdateState::clean,
           InitialUpdateState::pending_overlap,
           InitialUpdateState::stable_shadow }) {
    for (std::size_t row = 0; row < 16U; ++row) {
      prepare_activation(row, initial_update);
      const auto generic_count
          = jit.resume_cohort_prevalidated(generic_binding, entries);
      const auto generic = snapshot_cohort(members, entries, generic_count);
      expect_success_state(generic, members, layouts, row, initial_update);

      prepare_activation(row, initial_update);
      const auto fused_count = jit.try_resume_logic4_bit_and_cohort_prevalidated(
          *specialized_binding, entries);
      assert(fused_count);
      const auto fused = snapshot_cohort(members, entries, *fused_count);
      expect_success_state(fused, members, layouts, row, initial_update);
      assert(fused == generic);

      prepare_activation(row, initial_update);
      const auto trusted_count
          = jit.try_resume_logic4_bit_and_cohort_trusted_prevalidated(
              *specialized_binding, entries);
      assert(trusted_count);
      const auto trusted = snapshot_cohort(members, entries, *trusted_count);
      expect_success_state(trusted, members, layouts, row, initial_update);
      assert(trusted == generic);
    }
  }

  bool saw_compact_unchanged_shadow = false;
  for (const auto initial_update : {
           InitialUpdateState::clean,
           InitialUpdateState::pending_overlap,
           InitialUpdateState::stable_shadow }) {
    for (std::size_t row = 0; row < 16U; ++row) {
      prepare_compact_activation(row, initial_update);
      const auto generic_count
          = jit.resume_cohort_prevalidated(
              compact_generic_binding, compact_entries);
      assert(generic_count == cohort_member_count);
      const auto generic = snapshot_cohort(
          compact_members, compact_entries, generic_count);
      const auto lhs = logic4_values[row / 4U];
      const auto rhs = logic4_values[row % 4U];
      for (std::size_t index = 0; index < cohort_member_count; ++index) {
        assert(generic.members[index].result.status
            == FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY);
        assert(generic.members[index].result.instruction == 8U);
        assert(generic.members[index].result.delay == 0U);
        assert(generic.members[index].entry_status
            == FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY);
        assert(!generic.members[index].has_failure);
        assert(generic.members[index].queued == 0U);
        assert(generic.members[index].waiting == 1U);
        assert(generic.members[index].process_status == 2U);
        expect_member_result(compact_members[index], compact_layouts[index],
            1U, lhs, rhs);
        expect_update_slot(compact_members[index], 1U, lhs, rhs,
            initial_update);
      }

      prepare_compact_activation(row, initial_update);
      const auto before = snapshot_cohort(compact_members, compact_entries, 0U);
      const auto compact_resumed
          = jit.try_resume_compact_logic4_bit_and_cohort_prevalidated(
              *compact_binding);
      assert(compact_resumed);
      const auto compact = snapshot_cohort(
          compact_members, compact_entries, 0U);
      for (std::size_t index = 0; index < cohort_member_count; ++index) {
        const auto& before_member = before.members[index];
        const auto& compact_member = compact.members[index];
        const auto& generic_member = generic.members[index];
        assert(compact_member.frame == before_member.frame);
        assert(compact_member.result == before_member.result);
        assert(compact_member.entry_status == before_member.entry_status);
        assert(compact_member.has_failure == before_member.has_failure);
        assert(compact_member.update_slot == generic_member.update_slot);
        assert(compact_member.update_active_words
            == generic_member.update_active_words);
        assert(compact_member.queued == generic_member.queued);
        assert(compact_member.waiting == generic_member.waiting);
        assert(compact_member.process_status == generic_member.process_status);
        if (initial_update == InitialUpdateState::stable_shadow
            && bit_and_result(lhs, rhs) == '0') {
          saw_compact_unchanged_shadow = true;
          assert(compact_member.update_slot == before_member.update_slot);
          assert(compact_member.update_active_words
              == before_member.update_active_words);
        }
        expect_update_slot(compact_members[index], 1U, lhs, rhs,
            initial_update);
      }
    }
  }
  assert(saw_compact_unchanged_shadow);

  const auto expect_decline_without_mutation = [&] {
    const auto before = snapshot_cohort(members, entries, 0U);
    const auto declined = jit.try_resume_logic4_bit_and_cohort_prevalidated(
        *specialized_binding, entries);
    assert(!declined);
    assert(snapshot_cohort(members, entries, 0U) == before);
  };

  prepare_activation(5U, InitialUpdateState::clean);
  members[1].frame.program_counter = 1U;
  expect_decline_without_mutation();

  prepare_activation(5U, InitialUpdateState::clean);
  members[1].frame.program_counter = 1U;
  const auto before_trusted_bad_pc = snapshot_cohort(members, entries, 0U);
  const auto trusted_bad_pc
      = jit.try_resume_logic4_bit_and_cohort_trusted_prevalidated(
          *specialized_binding, entries);
  assert(!trusted_bad_pc);
  assert(snapshot_cohort(members, entries, 0U) == before_trusted_bad_pc);

  prepare_activation(5U, InitialUpdateState::clean);
  members[1].runtime.flags = FSIM_JIT_RUNTIME_FLAG_DEBUG_POINTS;
  expect_decline_without_mutation();

  prepare_activation(5U, InitialUpdateState::clean);
  members[1].runtime.direct_read_signals = nullptr;
  members[1].runtime.direct_read_signal_count = 0U;
  expect_decline_without_mutation();

  prepare_activation(5U, InitialUpdateState::clean);
  members[1].runtime.direct_update_slots = nullptr;
  members[1].runtime.direct_update_slot_count = 0U;
  members[1].runtime.direct_update_active_words = nullptr;
  members[1].runtime.direct_update_active_word_count = 0U;
  expect_decline_without_mutation();

  for (const auto invalid_slot : { 0U, 1U }) {
    prepare_activation(5U, InitialUpdateState::clean);
    auto invalid_shapes = shapes;
    if (invalid_slot == 0U) {
      invalid_shapes[1].direct_read_rhs_slot = 2U;
    } else {
      invalid_shapes[1].direct_update_slot = 1U;
    }
    const auto before = snapshot_cohort(members, entries, 0U);
    const auto invalid_binding = jit.bind_logic4_bit_and_cohort_prevalidated(
        entries, invalid_shapes);
    assert(!invalid_binding);
    assert(snapshot_cohort(members, entries, 0U) == before);
  }

  for (std::size_t index = 0; index < cohort_member_count; ++index) {
    const auto sources = source_bits(index, logic4_values[0], logic4_values[0]);
    configure_direct_runtime(members[index], index,
        sources.lhs_msb, sources.lhs_lsb,
        sources.rhs_msb, sources.rhs_lsb, InitialUpdateState::clean);
  }
  members[1].direct_read_signals = members[0].direct_read_signals;
  members[1].runtime.direct_read_signals
      = members[0].direct_read_signals.data();
  members[1].runtime.direct_signal_aval
      = members[0].direct_signal_aval.data();
  members[1].runtime.direct_signal_bval
      = members[0].direct_signal_bval.data();
  const auto before_resident_shape
      = snapshot_cohort(members, entries, 0U);
  const auto resident_shape_binding
      = jit.bind_compact_logic4_bit_and_cohort_prevalidated(entries, shapes);
  assert(!resident_shape_binding);
  assert(snapshot_cohort(members, entries, 0U) == before_resident_shape);

  assert(jit.release_cohort_binding(*specialized_binding));
  assert(!jit.release_cohort_binding(*specialized_binding));
  assert(jit.release_cohort_binding(*compact_binding));
  assert(jit.release_cohort_binding(compact_generic_binding));
  const auto replacement = jit.bind_logic4_bit_and_cohort_prevalidated(
      entries, shapes);
  assert(replacement);
  assert(!jit.release_cohort_binding(*specialized_binding));
  assert(jit.release_cohort_binding(*replacement));
  assert(jit.release_cohort_binding(generic_binding));
  test_pure_wave_at_level(optimization);
}

} // namespace fsim::tests::compiler
