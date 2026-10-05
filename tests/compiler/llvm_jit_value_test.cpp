// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace fsim::tests::compiler {

namespace {

struct RequiredDirectReadCallbackCounts {
  std::uint32_t narrow { };
  std::uint32_t packed { };
  std::uint32_t logic9 { };
};

} // namespace

extern "C" std::uint64_t count_required_direct_read_callback(
    void* opaque, const std::uint32_t, std::uint64_t* const bval)
{
  auto& counts = *static_cast<RequiredDirectReadCallbackCounts*>(opaque);
  ++counts.narrow;
  *bval = 0U;
  return 0U;
}

extern "C" std::uint32_t count_required_direct_packed_read_callback(
    void* opaque,
    const std::uint32_t,
    const std::uint32_t width,
    std::uint64_t* const aval,
    std::uint64_t* const bval,
    std::uint64_t* const plane2,
    std::uint64_t* const plane3)
{
  auto& counts = *static_cast<RequiredDirectReadCallbackCounts*>(opaque);
  ++counts.packed;
  const auto words = (static_cast<std::size_t>(width) + 63U) / 64U;
  for (std::size_t index = 0U; index < words; ++index) {
    aval[index] = 0U;
    bval[index] = 0U;
    if (plane2 != nullptr) {
      plane2[index] = 0U;
    }
    if (plane3 != nullptr) {
      plane3[index] = 0U;
    }
  }
  return 0U;
}

extern "C" void count_required_direct_logic9_read_callback(
    void* opaque,
    const std::uint32_t,
    fsim_jit_logic9_word_v2* const value)
{
  auto& counts = *static_cast<RequiredDirectReadCallbackCounts*>(opaque);
  ++counts.logic9;
  value->planes[0] = 0U;
  value->planes[1] = 0U;
  value->planes[2] = 0U;
  value->planes[3] = 0U;
}

void test_required_direct_signal_reads_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
  auto options = LlvmJitOptions { optimization, { } };
  options.require_direct_read_signals = true;
  LlvmJit jit { options };
  RequiredDirectReadCallbackCounts callback_counts;

  const auto run_logic4_read = [&](const std::uint32_t width,
                                   const std::string_view suffix) {
    const auto words = static_cast<std::size_t>(
        (static_cast<std::uint64_t>(width) + 63U) / 64U);
    Process process;
    process.id = width;
    process.name = "required_direct_logic4_read";
    process.register_count = 1U;
    process.operations = { ReadSignal { 0U, 0U }, Halt { } };
    const std::array<std::uint32_t, 1> signal_widths { width };
    const auto process_symbol = std::string { symbol } + std::string { suffix };
    jit.add_process(process_symbol, process, signal_widths);
    const auto handle = jit.lookup(process_symbol);
    const auto layout = jit.frame_layout(handle);
    assert((layout.direct_read_signals
        == std::vector<runtime::simir::SignalId> { 0U }));

    TestRuntime storage;
    auto descriptor = abi(storage);
    auto services = copy_jit_services(descriptor);
    services.read_signal = count_required_direct_read_callback;
    services.read_signal_packed = count_required_direct_packed_read_callback;
    descriptor.services = &services;
    descriptor.context = &callback_counts;
    const std::array<std::uint32_t, 1> read_map { 0U };
    descriptor.direct_read_signals = read_map.data();
    descriptor.direct_read_signal_count = 1U;
    descriptor.direct_signal_count = 1U;

    std::vector<std::uint64_t> input_aval(words);
    std::vector<std::uint64_t> input_bval(words);
    if (width <= 64U) {
      input_aval[0] = UINT64_C(0x15);
      input_bval[0] = UINT64_C(0x09);
      descriptor.direct_signal_aval = input_aval.data();
      descriptor.direct_signal_bval = input_bval.data();
    } else {
      input_aval[0] = UINT64_C(0x8123456789abcdef);
      input_aval[1] = UINT64_C(0x1000000000000001);
      input_aval[2] = UINT64_C(0x1);
      input_bval[0] = UINT64_C(0x0400000000000000);
      input_bval[1] = UINT64_C(0x2000000000000000);
      input_bval[2] = 0U;
      const std::array<std::uint32_t, 1> offsets { 0U };
      descriptor.direct_wide_signal_aval = input_aval.data();
      descriptor.direct_wide_signal_bval = input_bval.data();
      descriptor.direct_wide_signal_offsets = offsets.data();
      descriptor.direct_wide_signal_offset_count = 1U;
      descriptor.direct_wide_word_count = static_cast<std::uint32_t>(words);

      std::vector<std::uint64_t> register_aval(layout.register_word_count);
      std::vector<std::uint64_t> register_bval(layout.register_word_count);
      std::vector<std::uint8_t> initialized(layout.register_count);
      fsim_jit_frame_v2 frame { };
      jit.initialize_frame(
          handle, frame, register_aval, register_bval, initialized);
      auto result = new_resume_result();
      assert(jit.resume(handle, descriptor, frame, result)
          == JitResumeStatus::completed);
      assert(callback_counts.narrow == 0U);
      assert(callback_counts.packed == 0U);
      assert(std::ranges::equal(input_aval, register_aval));
      assert(std::ranges::equal(input_bval, register_bval));

      input_aval[0] ^= UINT64_C(0x20);
      input_bval[1] ^= UINT64_C(0x08);
      jit.initialize_frame(
          handle, frame, register_aval, register_bval, initialized);
      result = new_resume_result();
      assert(jit.resume(handle, descriptor, frame, result)
          == JitResumeStatus::completed);
      assert(callback_counts.packed == 0U);
      assert(std::ranges::equal(input_aval, register_aval));
      assert(std::ranges::equal(input_bval, register_bval));

      auto aliased_descriptor = descriptor;
      aliased_descriptor.direct_wide_signal_bval
          = aliased_descriptor.direct_wide_signal_aval;
      jit.initialize_frame(
          handle, frame, register_aval, register_bval, initialized);
      result = new_resume_result();
      assert(jit.resume(handle, aliased_descriptor, frame, result)
          == JitResumeStatus::completed);
      assert(callback_counts.packed == 0U);
      assert(std::ranges::equal(input_aval, register_aval));
      assert(std::ranges::equal(input_aval, register_bval));

      auto invalid_descriptor = descriptor;
      const std::array<std::uint32_t, 1> invalid_offsets { 1U };
      invalid_descriptor.direct_wide_signal_offsets
          = invalid_offsets.data();
      invalid_descriptor.direct_wide_word_count = 2U;
      std::vector<std::uint64_t> invalid_register_aval(
          layout.register_word_count, UINT64_C(0xaaaaaaaaaaaaaaaa));
      std::vector<std::uint64_t> invalid_register_bval(
          layout.register_word_count, UINT64_C(0x5555555555555555));
      std::vector<std::uint8_t> invalid_initialized(
          layout.register_count, 0x5aU);
      fsim_jit_frame_v2 invalid_frame { };
      jit.initialize_frame(
          handle, invalid_frame,
          invalid_register_aval, invalid_register_bval,
          invalid_initialized);
      const auto before_aval = invalid_register_aval;
      const auto before_bval = invalid_register_bval;
      const auto before_initialized = invalid_initialized;
      const auto before_pc = invalid_frame.program_counter;
      auto invalid_result = new_resume_result();
      expect_error(
          [&] {
            (void)jit.resume(
                handle, invalid_descriptor, invalid_frame, invalid_result);
          },
          "wide direct-read span is too small");
      assert(invalid_frame.state == FSIM_JIT_FRAME_STATE_READY_V2);
      assert(invalid_frame.program_counter == before_pc);
      assert(invalid_register_aval == before_aval);
      assert(invalid_register_bval == before_bval);
      assert(invalid_initialized == before_initialized);
      assert(callback_counts.packed == 0U);
      return;
    }

    std::vector<std::uint64_t> register_aval(layout.register_word_count);
    std::vector<std::uint64_t> register_bval(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v2 frame { };
    jit.initialize_frame(handle, frame, register_aval, register_bval, initialized);
    auto result = new_resume_result();
    assert(jit.resume(handle, descriptor, frame, result)
        == JitResumeStatus::completed);
    assert(callback_counts.narrow == 0U);
    assert(PackedLogic4::from_word_planes(
        width, register_aval, register_bval)
        == PackedLogic4::from_aval_bval(width, input_aval[0], input_bval[0]));

    input_aval[0] ^= UINT64_C(0x04);
    input_bval[0] ^= UINT64_C(0x02);
    jit.initialize_frame(
        handle, frame, register_aval, register_bval, initialized);
    result = new_resume_result();
    assert(jit.resume(handle, descriptor, frame, result)
        == JitResumeStatus::completed);
    assert(callback_counts.narrow == 0U);
    assert(PackedLogic4::from_word_planes(
        width, register_aval, register_bval)
        == PackedLogic4::from_aval_bval(width, input_aval[0], input_bval[0]));

    auto aliased_descriptor = descriptor;
    aliased_descriptor.direct_signal_bval
        = aliased_descriptor.direct_signal_aval;
    jit.initialize_frame(
        handle, frame, register_aval, register_bval, initialized);
    result = new_resume_result();
    assert(jit.resume(handle, aliased_descriptor, frame, result)
        == JitResumeStatus::completed);
    assert(callback_counts.narrow == 0U);
    assert(PackedLogic4::from_word_planes(
        width, register_aval, register_bval)
        == PackedLogic4::from_aval_bval(width, input_aval[0], input_aval[0]));

    auto invalid_descriptor = descriptor;
    const std::array<std::uint64_t, 2> invalid_aval { 0U, UINT64_C(0x15) };
    const std::array<std::uint64_t, 2> invalid_bval { 0U, UINT64_C(0x09) };
    const std::array<std::uint32_t, 1> invalid_map { 1U };
    invalid_descriptor.direct_signal_aval = invalid_aval.data();
    invalid_descriptor.direct_signal_bval = invalid_bval.data();
    invalid_descriptor.direct_read_signals = invalid_map.data();
    invalid_descriptor.direct_signal_count = 2U;
    std::vector<std::uint64_t> invalid_register_aval(
        layout.register_word_count, UINT64_C(0xaaaaaaaaaaaaaaaa));
    std::vector<std::uint64_t> invalid_register_bval(
        layout.register_word_count, UINT64_C(0x5555555555555555));
    std::vector<std::uint8_t> invalid_initialized(
        layout.register_count, 0x5aU);
    fsim_jit_frame_v2 invalid_frame { };
    jit.initialize_frame(
        handle, invalid_frame,
        invalid_register_aval, invalid_register_bval,
        invalid_initialized);
    const auto before_aval = invalid_register_aval;
    const auto before_bval = invalid_register_bval;
    const auto before_initialized = invalid_initialized;
    auto invalid_result = new_resume_result();
    expect_error(
        [&] {
          (void)jit.resume(
              handle, invalid_descriptor, invalid_frame, invalid_result);
        },
        "direct-read map does not match");
    assert(invalid_frame.state == FSIM_JIT_FRAME_STATE_READY_V2);
    assert(invalid_frame.program_counter == 0U);
    assert(invalid_register_aval == before_aval);
    assert(invalid_register_bval == before_bval);
    assert(invalid_initialized == before_initialized);
    assert(callback_counts.narrow == 0U);
  };

  run_logic4_read(5U, "_narrow");
  run_logic4_read(129U, "_wide");

  Process logic9_process;
  logic9_process.id = 200U;
  logic9_process.name = "required_direct_logic9_read";
  logic9_process.register_count = 1U;
  logic9_process.register_value_kinds = { ValueKind::logic9 };
  logic9_process.operations = { ReadSignal { 0U, 0U }, Halt { } };
  const std::array<std::uint32_t, 1> logic9_widths { 4U };
  const std::array<ValueKind, 1> logic9_kinds { ValueKind::logic9 };
  const auto logic9_symbol = std::string { symbol } + "_logic9";
  jit.add_process(
      logic9_symbol, logic9_process, logic9_widths, logic9_kinds);
  const auto logic9_handle = jit.lookup(logic9_symbol);
  const auto logic9_layout = jit.frame_layout(logic9_handle);
  auto logic9_input = PackedLogic4::from_logic9_msb_string("UX0-");
  const auto logic9_word = logic9_input.logic9_low_word();
  std::array<std::array<std::uint64_t, 1>, 4> input_planes { };
  for (std::size_t plane = 0U; plane < 4U; ++plane) {
    input_planes[plane][0] = logic9_word.planes[plane];
  }
  const std::array<std::uint32_t, 1> logic9_map { 0U };
  TestRuntime logic9_storage;
  auto logic9_descriptor = abi(logic9_storage);
  auto logic9_services = copy_jit_services(logic9_descriptor);
  logic9_services.read_signal = count_required_direct_read_callback;
  logic9_services.read_signal_logic9
      = count_required_direct_logic9_read_callback;
  logic9_descriptor.services = &logic9_services;
  logic9_descriptor.context = &callback_counts;
  logic9_descriptor.direct_signal_aval = input_planes[0].data();
  logic9_descriptor.direct_signal_bval = input_planes[1].data();
  logic9_descriptor.direct_signal_logic9_plane0 = input_planes[0].data();
  logic9_descriptor.direct_signal_logic9_plane1 = input_planes[1].data();
  logic9_descriptor.direct_signal_logic9_plane2 = input_planes[2].data();
  logic9_descriptor.direct_signal_logic9_plane3 = input_planes[3].data();
  logic9_descriptor.direct_read_signals = logic9_map.data();
  logic9_descriptor.direct_read_signal_count = 1U;
  logic9_descriptor.direct_signal_count = 1U;
  std::vector<std::uint64_t> logic9_register_aval(
      logic9_layout.register_word_count);
  std::vector<std::uint64_t> logic9_register_bval(
      logic9_layout.register_word_count);
  std::vector<std::uint64_t> logic9_register_plane2(
      logic9_layout.register_word_count);
  std::vector<std::uint64_t> logic9_register_plane3(
      logic9_layout.register_word_count);
  std::vector<std::uint8_t> logic9_initialized(
      logic9_layout.register_count);
  fsim_jit_frame_v2 logic9_frame { };
  jit.initialize_frame(
      logic9_handle, logic9_frame,
      logic9_register_aval, logic9_register_bval, logic9_initialized,
      logic9_register_plane2, logic9_register_plane3);
  auto logic9_result = new_resume_result();
  assert(jit.resume(
      logic9_handle, logic9_descriptor, logic9_frame, logic9_result)
      == JitResumeStatus::completed);
  assert(callback_counts.narrow == 0U);
  assert(callback_counts.logic9 == 0U);
  assert(logic9_register_aval[0] == logic9_word.planes[0]);
  assert(logic9_register_bval[0] == logic9_word.planes[1]);
  assert(logic9_register_plane2[0] == logic9_word.planes[2]);
  assert(logic9_register_plane3[0] == logic9_word.planes[3]);

  logic9_input = PackedLogic4::from_logic9_msb_string("ZZU1");
  const auto changed_logic9_word = logic9_input.logic9_low_word();
  for (std::size_t plane = 0U; plane < 4U; ++plane) {
    input_planes[plane][0] = changed_logic9_word.planes[plane];
  }
  jit.initialize_frame(
      logic9_handle, logic9_frame,
      logic9_register_aval, logic9_register_bval, logic9_initialized,
      logic9_register_plane2, logic9_register_plane3);
  logic9_result = new_resume_result();
  assert(jit.resume(
      logic9_handle, logic9_descriptor, logic9_frame, logic9_result)
      == JitResumeStatus::completed);
  assert(callback_counts.logic9 == 0U);
  assert(logic9_register_aval[0] == changed_logic9_word.planes[0]);
  assert(logic9_register_bval[0] == changed_logic9_word.planes[1]);
  assert(logic9_register_plane2[0] == changed_logic9_word.planes[2]);
  assert(logic9_register_plane3[0] == changed_logic9_word.planes[3]);

  const std::array<std::uint64_t, 1U> equal_logic9_plane { 0U };
  auto equal_logic9_descriptor = logic9_descriptor;
  equal_logic9_descriptor.direct_signal_aval = equal_logic9_plane.data();
  equal_logic9_descriptor.direct_signal_bval = equal_logic9_plane.data();
  equal_logic9_descriptor.direct_signal_logic9_plane0
      = equal_logic9_plane.data();
  equal_logic9_descriptor.direct_signal_logic9_plane1
      = equal_logic9_plane.data();
  equal_logic9_descriptor.direct_signal_logic9_plane2
      = equal_logic9_plane.data();
  equal_logic9_descriptor.direct_signal_logic9_plane3
      = equal_logic9_plane.data();
  jit.initialize_frame(
      logic9_handle, logic9_frame,
      logic9_register_aval, logic9_register_bval, logic9_initialized,
      logic9_register_plane2, logic9_register_plane3);
  logic9_result = new_resume_result();
  assert(jit.resume(
      logic9_handle, equal_logic9_descriptor, logic9_frame, logic9_result)
      == JitResumeStatus::completed);
  assert(callback_counts.logic9 == 0U);
  assert(logic9_register_aval[0U] == 0U);
  assert(logic9_register_bval[0U] == 0U);
  assert(logic9_register_plane2[0U] == 0U);
  assert(logic9_register_plane3[0U] == 0U);

  auto invalid_logic9_descriptor = logic9_descriptor;
  invalid_logic9_descriptor.direct_signal_logic9_plane3 = nullptr;
  fsim_jit_frame_v2 invalid_logic9_frame { };
  jit.initialize_frame(
      logic9_handle, invalid_logic9_frame,
      logic9_register_aval, logic9_register_bval, logic9_initialized,
      logic9_register_plane2, logic9_register_plane3);
  const auto before_logic9_aval = logic9_register_aval;
  const auto before_logic9_bval = logic9_register_bval;
  const auto before_logic9_plane2 = logic9_register_plane2;
  const auto before_logic9_plane3 = logic9_register_plane3;
  const auto before_logic9_initialized = logic9_initialized;
  auto invalid_logic9_result = new_resume_result();
  expect_error(
      [&] {
        (void)jit.resume(
            logic9_handle,
            invalid_logic9_descriptor,
            invalid_logic9_frame,
            invalid_logic9_result);
      },
      "narrow Logic9 direct-read planes are null");
  assert(invalid_logic9_frame.state == FSIM_JIT_FRAME_STATE_READY_V2);
  assert(invalid_logic9_frame.program_counter == 0U);
  assert(logic9_register_aval == before_logic9_aval);
  assert(logic9_register_bval == before_logic9_bval);
  assert(logic9_register_plane2 == before_logic9_plane2);
  assert(logic9_register_plane3 == before_logic9_plane3);
  assert(logic9_initialized == before_logic9_initialized);
  assert(callback_counts.logic9 == 0U);

  Process logic9_wide_process;
  logic9_wide_process.id = 202U;
  logic9_wide_process.name = "required_direct_wide_logic9_read";
  logic9_wide_process.register_count = 1U;
  logic9_wide_process.register_value_kinds = { ValueKind::logic9 };
  logic9_wide_process.operations = {
      ReadSignal { 0U, 0U }, Halt { }
  };
  const std::array<std::uint32_t, 1U> logic9_wide_widths { 129U };
  const auto logic9_wide_symbol = std::string { symbol } + "_logic9_wide";
  jit.add_process(logic9_wide_symbol, logic9_wide_process,
      logic9_wide_widths, logic9_kinds);
  const auto logic9_wide_handle = jit.lookup(logic9_wide_symbol);
  const auto logic9_wide_layout = jit.frame_layout(logic9_wide_handle);
  const std::array<std::uint32_t, 1U> logic9_wide_read_map { 0U };
  const std::array<std::uint32_t, 1U> logic9_wide_offsets { 0U };
  const std::array<std::uint64_t, 3U> shared_logic9_wide_plane { };
  TestRuntime logic9_wide_storage;
  auto logic9_wide_descriptor = abi(logic9_wide_storage);
  auto logic9_wide_services = copy_jit_services(logic9_wide_descriptor);
  logic9_wide_services.read_signal = count_required_direct_read_callback;
  logic9_wide_services.read_signal_logic9
      = count_required_direct_logic9_read_callback;
  logic9_wide_descriptor.services = &logic9_wide_services;
  logic9_wide_descriptor.context = &callback_counts;
  logic9_wide_descriptor.direct_read_signals
      = logic9_wide_read_map.data();
  logic9_wide_descriptor.direct_read_signal_count = 1U;
  logic9_wide_descriptor.direct_signal_count = 1U;
  logic9_wide_descriptor.direct_wide_signal_aval
      = shared_logic9_wide_plane.data();
  logic9_wide_descriptor.direct_wide_signal_bval
      = shared_logic9_wide_plane.data();
  logic9_wide_descriptor.direct_wide_signal_logic9_plane2
      = shared_logic9_wide_plane.data();
  logic9_wide_descriptor.direct_wide_signal_logic9_plane3
      = shared_logic9_wide_plane.data();
  logic9_wide_descriptor.direct_wide_signal_offsets
      = logic9_wide_offsets.data();
  logic9_wide_descriptor.direct_wide_signal_offset_count = 1U;
  logic9_wide_descriptor.direct_wide_word_count = 3U;
  std::vector<std::uint64_t> logic9_wide_register_aval(
      logic9_wide_layout.register_word_count);
  std::vector<std::uint64_t> logic9_wide_register_bval(
      logic9_wide_layout.register_word_count);
  std::vector<std::uint64_t> logic9_wide_register_plane2(
      logic9_wide_layout.register_word_count);
  std::vector<std::uint64_t> logic9_wide_register_plane3(
      logic9_wide_layout.register_word_count);
  std::vector<std::uint8_t> logic9_wide_initialized(
      logic9_wide_layout.register_count);
  fsim_jit_frame_v2 logic9_wide_frame { };
  jit.initialize_frame(
      logic9_wide_handle, logic9_wide_frame,
      logic9_wide_register_aval, logic9_wide_register_bval,
      logic9_wide_initialized, logic9_wide_register_plane2,
      logic9_wide_register_plane3);
  auto logic9_wide_result = new_resume_result();
  assert(jit.resume(logic9_wide_handle, logic9_wide_descriptor,
            logic9_wide_frame, logic9_wide_result)
      == JitResumeStatus::completed);
  assert(callback_counts.logic9 == 0U);
  assert(std::ranges::all_of(logic9_wide_register_aval,
      [](const auto value) { return value == 0U; }));
  assert(std::ranges::all_of(logic9_wide_register_bval,
      [](const auto value) { return value == 0U; }));
  assert(std::ranges::all_of(logic9_wide_register_plane2,
      [](const auto value) { return value == 0U; }));
  assert(std::ranges::all_of(logic9_wide_register_plane3,
      [](const auto value) { return value == 0U; }));

  Process sampled_process;
  sampled_process.id = 201U;
  sampled_process.name = "required_direct_rejects_sampled_read";
  sampled_process.register_count = 1U;
  sampled_process.operations = {
      ReadSignal { 0U, 0U, SignalReadKind::sampled, 1U }, Halt { }
  };
  expect_unsupported(
      [&] {
        jit.add_process(
            std::string { symbol } + "_sampled",
            sampled_process,
            std::array<std::uint32_t, 1> { 8U });
      },
      "required direct reads support only current signals");
}

void test_required_direct_read_cache_identity()
{
  const auto unique = std::chrono::steady_clock::now()
      .time_since_epoch().count();
  const auto directory = std::filesystem::temp_directory_path()
      / ("fsim-required-direct-read-" + std::to_string(unique));
  std::filesystem::remove_all(directory);

  Process process;
  process.id = 202U;
  process.name = "required_direct_read_cache_identity";
  process.register_count = 1U;
  process.operations = { ReadSignal { 0U, 0U }, Halt { } };
  const std::array<std::uint32_t, 1> widths { 8U };
  constexpr std::string_view symbol = "fsim_required_read_cache_identity";
  std::array<std::uint64_t, 2> unchecked_layout_id { };
  std::array<std::uint64_t, 2> required_layout_id { };

  auto options = LlvmJitOptions {
      JitOptimizationLevel::o0, directory };
  {
    LlvmJit unchecked { options };
    unchecked.add_process(symbol, process, widths);
    const auto handle = unchecked.lookup(symbol);
    const auto layout = unchecked.frame_layout(handle);
    unchecked_layout_id = { layout.layout_id_low, layout.layout_id_high };
    assert(unchecked.cache_statistics().misses == 1U);
  }

  options.require_direct_read_signals = true;
  {
    LlvmJit required { options };
    required.add_process(symbol, process, widths);
    const auto handle = required.lookup(symbol);
    const auto layout = required.frame_layout(handle);
    required_layout_id = { layout.layout_id_low, layout.layout_id_high };
    assert((layout.direct_read_signals
        == std::vector<runtime::simir::SignalId> { 0U }));
    assert(required_layout_id != unchecked_layout_id);
    const auto stats = required.cache_statistics();
    assert(stats.hits == 0U);
    assert(stats.misses == 1U);
  }
  {
    LlvmJit required_warm { options };
    required_warm.add_process(symbol, process, widths);
    const auto handle = required_warm.lookup(symbol);
    const auto layout = required_warm.frame_layout(handle);
    assert(required_layout_id[0] == layout.layout_id_low);
    assert(required_layout_id[1] == layout.layout_id_high);
    const auto stats = required_warm.cache_statistics();
    assert(stats.hits == 1U);
    assert(stats.misses == 0U);
  }
  std::filesystem::remove_all(directory);
}

void test_required_direct_read_lease_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
  auto options = LlvmJitOptions { optimization, { } };
  LlvmJit jit { options };

  Process process;
  process.id = 203U;
  process.name = "required_direct_read_lease";
  process.register_count = 1U;
  process.operations = { ReadSignal { 0U, 0U }, Halt { } };
  const std::array<std::uint32_t, 1> widths { 8U };
  const auto strict_symbol = std::string { symbol } + "_strict";
  const auto guarded_symbol = std::string { symbol } + "_guarded";
  const std::array entries {
      JitProcessModuleEntry {
          strict_symbol,
          &process,
          { },
          { },
          fsim::compiler::JitBackendTierHint::none,
          1U,
          true,
          false },
      JitProcessModuleEntry {
          guarded_symbol,
          &process,
          { },
          { },
          fsim::compiler::JitBackendTierHint::none,
          1U,
          false,
          false }
  };
  jit.add_process_module(symbol, entries, widths);
  const auto handle = jit.lookup(strict_symbol);
  const auto binding = jit.bind(handle);
  const auto guarded_handle = jit.lookup(guarded_symbol);
  const auto guarded_binding = jit.bind(guarded_handle);
  const auto layout = jit.frame_layout(handle);
  assert(layout.register_count == 1U);
  assert((layout.direct_read_signals
      == std::vector<runtime::simir::SignalId> { 0U }));

  TestRuntime storage;
  auto descriptor = abi(storage);
  RequiredDirectReadCallbackCounts callback_counts;
  auto services = copy_jit_services(descriptor);
  services.read_signal = count_required_direct_read_callback;
  services.read_signal_packed = count_required_direct_packed_read_callback;
  descriptor.services = &services;
  descriptor.context = &callback_counts;
  const std::array<std::uint32_t, 1> read_map { 0U };
  descriptor.direct_read_signals = read_map.data();
  descriptor.direct_read_signal_count = 1U;
  descriptor.direct_signal_count = 1U;
  std::array<std::uint64_t, 1> input_aval { UINT64_C(0x35) };
  std::array<std::uint64_t, 1> input_bval { UINT64_C(0x04) };
  descriptor.direct_signal_aval = input_aval.data();
  descriptor.direct_signal_bval = input_bval.data();

  std::vector<std::uint64_t> register_aval(layout.register_word_count);
  std::vector<std::uint64_t> register_bval(layout.register_word_count);
  std::vector<std::uint8_t> initialized(layout.register_count);
  fsim_jit_frame_v2 frame { };
  jit.initialize_frame(
      handle, frame, register_aval, register_bval, initialized);
  auto result = new_resume_result();
  const auto lease = jit.bind_required_direct_read_prevalidated(
      binding, descriptor, frame, result);
  assert(lease);

  const auto run_with_lease = [&] {
    jit.initialize_frame(
        handle, frame, register_aval, register_bval, initialized);
    result = new_resume_result();
    const auto status = jit.resume_required_direct_read_prevalidated(
        *lease, binding, descriptor, frame, result);
    assert(status == JitResumeStatus::completed);
    assert(initialized[0] == 1U);
    assert(PackedLogic4::from_word_planes(
        8U, register_aval, register_bval)
        == PackedLogic4::from_aval_bval(8U, input_aval[0], input_bval[0]));
  };

  run_with_lease();
  assert(callback_counts.narrow == 0U);
  input_aval[0] = UINT64_C(0x92);
  input_bval[0] = UINT64_C(0x20);
  run_with_lease();
  assert(callback_counts.narrow == 0U);

  // Replacing the address of the direct-read map revokes the lease before
  // native entry and leaves the frame untouched for the guarded fallback.
  const std::array<std::uint32_t, 1> replacement_map { 0U };
  descriptor.direct_read_signals = replacement_map.data();
  jit.initialize_frame(
      handle, frame, register_aval, register_bval, initialized);
  result = new_resume_result();
  result.status = FSIM_JIT_RESUME_STATUS_PAUSED_V2;
  result.instruction = 37U;
  result.delay = 41U;
  const auto before_aval = register_aval;
  const auto before_bval = register_bval;
  const auto before_map_result = result;
  const auto declined = jit.resume_required_direct_read_prevalidated(
      *lease, binding, descriptor, frame, result);
  assert(!declined);
  assert(frame.state == FSIM_JIT_FRAME_STATE_READY_V2);
  assert(frame.program_counter == 0U);
  assert(register_aval == before_aval);
  assert(register_bval == before_bval);
  assert(result.abi_version == before_map_result.abi_version);
  assert(result.struct_size == before_map_result.struct_size);
  assert(result.status == before_map_result.status);
  assert(result.instruction == before_map_result.instruction);
  assert(result.delay == before_map_result.delay);
  assert(callback_counts.narrow == 0U);

  descriptor.direct_read_signals = read_map.data();
  const auto check_decline_without_mutation =
      [&](const std::uint32_t state, const std::uint32_t program_counter) {
    jit.initialize_frame(
        handle, frame, register_aval, register_bval, initialized);
    frame.state = state;
    frame.program_counter = program_counter;
    frame.last_instruction = 17U;
    std::ranges::fill(register_aval, UINT64_C(0xaaaaaaaaaaaaaaaa));
    std::ranges::fill(register_bval, UINT64_C(0x5555555555555555));
    std::ranges::fill(initialized, 0x5aU);
    result = new_resume_result();
    result.status = FSIM_JIT_RESUME_STATUS_PAUSED_V2;
    result.instruction = 19U;
    result.delay = 23U;
    const auto before_frame = frame;
    const auto before_aval = register_aval;
    const auto before_bval = register_bval;
    const auto before_initialized = initialized;
    const auto before_result = result;
    const auto declined_terminal
        = jit.resume_required_direct_read_prevalidated(
            *lease, binding, descriptor, frame, result);
    assert(!declined_terminal);
    assert(frame.state == before_frame.state);
    assert(frame.abi_version == before_frame.abi_version);
    assert(frame.struct_size == before_frame.struct_size);
    assert(frame.layout_id_low == before_frame.layout_id_low);
    assert(frame.layout_id_high == before_frame.layout_id_high);
    assert(frame.register_count == before_frame.register_count);
    assert(frame.program_counter == before_frame.program_counter);
    assert(frame.last_instruction == before_frame.last_instruction);
    assert(frame.register_aval == before_frame.register_aval);
    assert(frame.register_bval == before_frame.register_bval);
    assert(frame.register_initialized == before_frame.register_initialized);
    assert(frame.register_logic9_plane2
        == before_frame.register_logic9_plane2);
    assert(frame.register_logic9_plane3
        == before_frame.register_logic9_plane3);
    assert(frame.native_call_depth == before_frame.native_call_depth);
    assert(frame.native_call_reserved == before_frame.native_call_reserved);
    for (std::size_t index = 0U;
        index < FSIM_JIT_NATIVE_CALL_STACK_CAPACITY_V2; ++index) {
      assert(frame.native_return_stack[index]
          == before_frame.native_return_stack[index]);
    }
    assert(register_aval == before_aval);
    assert(register_bval == before_bval);
    assert(initialized == before_initialized);
    assert(result.status == before_result.status);
    assert(result.instruction == before_result.instruction);
    assert(result.delay == before_result.delay);
    assert(result.abi_version == before_result.abi_version);
    assert(result.struct_size == before_result.struct_size);
  };
  check_decline_without_mutation(
      FSIM_JIT_FRAME_STATE_COMPLETED_V2, 0U);
  check_decline_without_mutation(
      FSIM_JIT_FRAME_STATE_STOPPED_V2, 0U);
  check_decline_without_mutation(
      FSIM_JIT_FRAME_STATE_READY_V2, 2U);

  // Revoking the required lease by changing map identity still leaves a
  // valid direct-read capability for the ordinary guarded entry.
  descriptor.direct_read_signals = replacement_map.data();
  const auto guarded_layout = jit.frame_layout(guarded_handle);
  std::vector<std::uint64_t> guarded_aval(guarded_layout.register_word_count);
  std::vector<std::uint64_t> guarded_bval(guarded_layout.register_word_count);
  std::vector<std::uint8_t> guarded_initialized(
      guarded_layout.register_count);
  fsim_jit_frame_v2 guarded_frame { };
  jit.initialize_frame(
      guarded_handle,
      guarded_frame,
      guarded_aval,
      guarded_bval,
      guarded_initialized);
  auto guarded_result = new_resume_result();
  assert(!jit.bind_required_direct_read_prevalidated(
      guarded_binding, descriptor, guarded_frame, guarded_result));
  assert(jit.resume_prevalidated(
      guarded_binding, descriptor, guarded_frame, guarded_result)
      == JitResumeStatus::completed);
  assert(callback_counts.narrow == 0U);
  assert(PackedLogic4::from_word_planes(
      8U, guarded_aval, guarded_bval)
      == PackedLogic4::from_aval_bval(8U, input_aval[0], input_bval[0]));

  // Removing the direct map makes the guarded entry take its checked
  // callback route, independently of the required-lease revocation above.
  descriptor.direct_read_signals = nullptr;
  descriptor.direct_read_signal_count = 0U;
  jit.initialize_frame(
      guarded_handle, guarded_frame, guarded_aval, guarded_bval,
      guarded_initialized);
  guarded_result = new_resume_result();
  assert(jit.resume_prevalidated(
      guarded_binding, descriptor, guarded_frame, guarded_result)
      == JitResumeStatus::completed);
  assert(callback_counts.narrow == 1U);
  assert(PackedLogic4::from_word_planes(
      8U, guarded_aval, guarded_bval)
      == PackedLogic4::from_aval_bval(8U, 0U, 0U));

  // Binding itself performs the full map validation and declines malformed
  // capabilities before a lease can be reused.
  const std::array<std::uint32_t, 1> invalid_map { 1U };
  std::array<std::uint64_t, 2> invalid_aval { };
  std::array<std::uint64_t, 2> invalid_bval { };
  descriptor.direct_read_signals = invalid_map.data();
  descriptor.direct_signal_count = 2U;
  descriptor.direct_signal_aval = invalid_aval.data();
  descriptor.direct_signal_bval = invalid_bval.data();
  jit.initialize_frame(
      handle, frame, register_aval, register_bval, initialized);
  std::ranges::fill(register_aval, UINT64_C(0xaaaaaaaaaaaaaaaa));
  std::ranges::fill(register_bval, UINT64_C(0x5555555555555555));
  std::ranges::fill(initialized, 0x5aU);
  result = new_resume_result();
  result.status = FSIM_JIT_RESUME_STATUS_PAUSED_V2;
  result.instruction = 29U;
  result.delay = 31U;
  const auto invalid_before_aval = register_aval;
  const auto invalid_before_bval = register_bval;
  const auto invalid_before_initialized = initialized;
  const auto invalid_before_result = result;
  const auto invalid_lease = jit.bind_required_direct_read_prevalidated(
      binding, descriptor, frame, result);
  assert(!invalid_lease);
  assert(frame.state == FSIM_JIT_FRAME_STATE_READY_V2);
  assert(frame.program_counter == 0U);
  assert(register_aval == invalid_before_aval);
  assert(register_bval == invalid_before_bval);
  assert(initialized == invalid_before_initialized);
  assert(result.status == invalid_before_result.status);
  assert(result.instruction == invalid_before_result.instruction);
  assert(result.delay == invalid_before_result.delay);
  assert(callback_counts.narrow == 1U);

  // A nonidentity map is accepted only with an explicit, type-exact
  // elaborated instance binding. The identity API still rejects it.
  descriptor.direct_read_signal_count = 1U;
  invalid_aval[1] = UINT64_C(0xa5);
  invalid_bval[1] = UINT64_C(0x18);
  using InstanceBinding = fsim::compiler::JitDirectReadInstanceBinding;
  const std::array<InstanceBinding, 1> mapped_binding {
      InstanceBinding { 1U, 8U, ValueKind::logic4 }
  };
  const auto mapped_declines = [&](const std::span<const InstanceBinding> map) {
    const auto before_aval = register_aval;
    const auto before_bval = register_bval;
    const auto before_initialized = initialized;
    const auto before_frame = frame;
    const auto before_result = result;
    assert(!jit.bind_mapped_required_direct_read_prevalidated(
        binding, descriptor, frame, result, map));
    assert(register_aval == before_aval && register_bval == before_bval);
    assert(initialized == before_initialized);
    assert(frame.state == before_frame.state);
    assert(frame.program_counter == before_frame.program_counter);
    assert(result.status == before_result.status);
    assert(result.instruction == before_result.instruction);
    assert(result.delay == before_result.delay);
  };
  mapped_declines({ });
  auto wrong_binding = mapped_binding;
  wrong_binding[0].signal = 0U;
  mapped_declines(wrong_binding);
  wrong_binding = mapped_binding;
  wrong_binding[0].width = 7U;
  mapped_declines(wrong_binding);
  wrong_binding = mapped_binding;
  wrong_binding[0].kind = ValueKind::logic9;
  mapped_declines(wrong_binding);
  descriptor.direct_signal_count = 1U;
  mapped_declines(mapped_binding);
  descriptor.direct_signal_count = 2U;
  descriptor.direct_signal_bval = nullptr;
  mapped_declines(mapped_binding);
  descriptor.direct_signal_bval = invalid_bval.data();
  ++frame.layout_id_low;
  mapped_declines(mapped_binding);
  --frame.layout_id_low;
  assert(!jit.bind_required_direct_read_prevalidated(
      binding, descriptor, frame, result));
  const auto mapped_lease = jit.bind_mapped_required_direct_read_prevalidated(
      binding, descriptor, frame, result, mapped_binding);
  assert(mapped_lease);
  for (const auto value : { UINT64_C(0xa5), UINT64_C(0x52) }) {
    invalid_aval[1] = value;
    jit.initialize_frame(handle, frame, register_aval, register_bval, initialized);
    result = new_resume_result();
    assert(jit.resume_required_direct_read_prevalidated(
        *mapped_lease, binding, descriptor, frame, result)
        == JitResumeStatus::completed);
    assert(PackedLogic4::from_word_planes(8U, register_aval, register_bval)
        == PackedLogic4::from_aval_bval(8U, value, invalid_bval[1]));
    assert(callback_counts.narrow == 1U);
  }
  const std::array<std::uint32_t, 1> moved_map { 1U };
  descriptor.direct_read_signals = moved_map.data();
  jit.initialize_frame(handle, frame, register_aval, register_bval, initialized);
  result = new_resume_result();
  assert(!jit.resume_required_direct_read_prevalidated(
      *mapped_lease, binding, descriptor, frame, result));
  assert(frame.program_counter == 0U && initialized[0] == 0U);
  assert(jit.bind_mapped_required_direct_read_prevalidated(
      binding, descriptor, frame, result, mapped_binding));

  Process branch_process;
  branch_process.id = 204U;
  branch_process.name = "required_direct_read_lease_branch_error";
  branch_process.register_count = 1U;
  branch_process.operations = {
      ReadSignal { 0U, 0U },
      Branch { 0U, 2U, 3U, UnknownBranchPolicy::error },
      Halt { },
      Halt { }
  };
  const std::array<std::uint32_t, 1> branch_widths { 1U };
  const auto branch_symbol = std::string { symbol } + "_branch_strict";
  const std::array branch_entries {
      JitProcessModuleEntry {
          branch_symbol,
          &branch_process,
          { },
          { },
          fsim::compiler::JitBackendTierHint::none,
          1U,
          true,
          false }
  };
  jit.add_process_module(
      std::string { symbol } + "_branch_module",
      branch_entries,
      branch_widths);
  const auto branch_handle = jit.lookup(branch_symbol);
  const auto branch_binding = jit.bind(branch_handle);
  const auto branch_layout = jit.frame_layout(branch_handle);
  std::vector<std::uint64_t> branch_register_aval(
      branch_layout.register_word_count);
  std::vector<std::uint64_t> branch_register_bval(
      branch_layout.register_word_count);
  std::vector<std::uint8_t> branch_initialized(
      branch_layout.register_count);
  fsim_jit_frame_v2 branch_frame { };
  jit.initialize_frame(
      branch_handle,
      branch_frame,
      branch_register_aval,
      branch_register_bval,
      branch_initialized);
  TestRuntime branch_storage;
  auto branch_descriptor = abi(branch_storage);
  auto branch_services = copy_jit_services(branch_descriptor);
  branch_services.read_signal = count_required_direct_read_callback;
  branch_descriptor.services = &branch_services;
  branch_descriptor.context = &callback_counts;
  const std::array<std::uint32_t, 1> branch_read_map { 0U };
  branch_descriptor.direct_read_signals = branch_read_map.data();
  branch_descriptor.direct_read_signal_count = 1U;
  branch_descriptor.direct_signal_count = 1U;
  std::array<std::uint64_t, 1> branch_input_aval { 0U };
  std::array<std::uint64_t, 1> branch_input_bval { 1U };
  branch_descriptor.direct_signal_aval = branch_input_aval.data();
  branch_descriptor.direct_signal_bval = branch_input_bval.data();
  auto branch_result = new_resume_result();
  const auto branch_lease = jit.bind_required_direct_read_prevalidated(
      branch_binding, branch_descriptor, branch_frame, branch_result);
  assert(branch_lease);
  bool generated_error_propagated = false;
  try {
    (void)jit.resume_required_direct_read_prevalidated(
        *branch_lease,
        branch_binding,
        branch_descriptor,
        branch_frame,
        branch_result);
  } catch (const LlvmJitGeneratedRuntimeError& error) {
    generated_error_propagated = true;
    assert(error.reason()
        == JitGeneratedRuntimeErrorReason::unknown_branch_condition);
  }
  assert(generated_error_propagated);
  assert(branch_frame.state == FSIM_JIT_FRAME_STATE_RUNTIME_ERROR_V2);
  assert(branch_result.status == FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR_V2);
  assert(callback_counts.narrow == 1U);
}

void test_systemverilog_scalar_transport_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol) {
  using fsim::runtime::SystemVerilogScalarKind;
  using fsim::runtime::SystemVerilogScalarValue;

  const auto short_value = SystemVerilogScalarValue::shortreal(-1.25F);
  const auto real_value = SystemVerilogScalarValue::real(-0.0);
  const auto time_value = SystemVerilogScalarValue::time(
      UINT64_C(9007199254740993));
  const auto short_payload =
      fsim::runtime::encode_systemverilog_scalar_payload(short_value);
  const auto real_payload =
      fsim::runtime::encode_systemverilog_scalar_payload(real_value);
  const auto time_payload =
      fsim::runtime::encode_systemverilog_scalar_payload(time_value);
  assert(short_payload && real_payload && time_payload);

  Process process;
  process.id = 0;
  process.name = "systemverilog_scalar_transport";
  process.register_count = 3;
  process.operations = {
      LoadConstant{0, short_payload.value},
      WriteBlocking{0, 0},
      LoadConstant{1, real_payload.value},
      WriteBlocking{1, 1},
      LoadConstant{2, time_payload.value},
      WriteBlocking{2, 2},
      Halt{}};
  const std::array<std::uint32_t, 3> widths{32, 64, 64};
  LlvmJit jit{LlvmJitOptions{optimization, {}}};
  jit.add_process(symbol, process, widths);

  TestRuntime runtime;
  auto descriptor = abi(runtime);
  assert(jit.execute(jit.lookup(symbol), descriptor)
         == JitExecutionStatus::completed);
  assert((runtime.signals[0]
          == EncodedSignal{short_value.bits, 0}));
  assert((runtime.signals[1]
          == EncodedSignal{real_value.bits, 0}));
  assert((runtime.signals[2]
          == EncodedSignal{time_value.bits, 0}));
  const auto decoded = fsim::runtime::decode_systemverilog_scalar_payload(
      PackedLogic4::from_aval_bval(
          64, runtime.signals[2].aval, runtime.signals[2].bval),
      SystemVerilogScalarKind::Time);
  assert(decoded && decoded.value == time_value);

  const auto factor = fsim::runtime::encode_systemverilog_scalar_payload(
      SystemVerilogScalarValue::real(1.5));
  const auto multiplier = fsim::runtime::encode_systemverilog_scalar_payload(
      SystemVerilogScalarValue::real(2.0));
  assert(factor && multiplier);
  Process scalar_binary;
  scalar_binary.id = 1;
  scalar_binary.name = "systemverilog_scalar_binary";
  scalar_binary.register_count = 3;
  scalar_binary.operations = {
      LoadConstant{0, factor.value},
      LoadConstant{1, multiplier.value},
      SystemVerilogScalarBinary{
          fsim::runtime::SystemVerilogScalarBinaryOperator::Multiply,
          2, 0, 1,
          SystemVerilogScalarKind::Real,
          SystemVerilogScalarKind::Real,
          SystemVerilogScalarKind::Real},
      Halt{}};
  const auto binary_symbol = std::string{symbol} + "_binary";
  jit.add_process(
      binary_symbol,
      scalar_binary,
      std::array<std::uint32_t, 3>{64, 64, 64});
  (void)jit.lookup(binary_symbol);
}

void test_wide_register_frame_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    for (const auto width : { 65U, 100U, 129U, 257U, 1024U }) {
        const auto word_count = (width + 63U) / 64U;
        const auto instance_symbol = std::string { symbol }
            + "_" + std::to_string(width);
        auto value = PackedLogic4(width, fsim::runtime::Logic4::zero);
        value.set(0, fsim::runtime::Logic4::one);
        value.set(width / 3U, fsim::runtime::Logic4::x);
        value.set(width / 2U, fsim::runtime::Logic4::z);
        value.set(width - 1U, fsim::runtime::Logic4::one);

        Process process;
        process.id = 91;
        process.name = "wide_register_frame";
        process.register_count = 2;
        process.operations = {
            LoadConstant { 0, value },
            CopyRegister { 1, 0 },
            Pause { },
            CopyRegister { 1, 0 },
            Pause { },
            Stop { },
        };

        LlvmJit jit { LlvmJitOptions { optimization, { } } };
        assert(jit.supports_process(process, { }));
        jit.add_process(instance_symbol, process, { });
        const auto handle = jit.lookup(instance_symbol);
        const auto layout = jit.frame_layout(handle);
        assert(layout.tracks_register_initialization);
        assert(layout.register_count == 2);
        assert(layout.register_word_count == 2U * word_count);
        assert((layout.register_widths
            == std::vector<std::uint32_t> { width, width }));
        assert((layout.register_word_offsets
            == std::vector<std::uint32_t> { 0U, word_count }));

        std::vector<std::uint64_t> aval(layout.register_word_count);
        std::vector<std::uint64_t> bval(layout.register_word_count);
        std::vector<std::uint8_t> initialized(layout.register_count);
        fsim_jit_frame_v2 frame { };
        jit.initialize_frame(handle, frame, aval, bval, initialized);
        TestRuntime runtime;
        auto descriptor = abi(runtime);
        auto result = new_resume_result();
        assert(jit.resume(handle, descriptor, frame, result)
            == JitResumeStatus::paused);
        assert(result.instruction == 2);
        assert((initialized == std::vector<std::uint8_t> { 1, 1 }));
        for (std::size_t destination = 0; destination < 2; ++destination) {
            const auto offset = layout.register_word_offsets[destination];
            assert(std::ranges::equal(value.aval_words(),
                std::span { aval }.subspan(offset, value.aval_words().size())));
            assert(std::ranges::equal(value.bval_words(),
                std::span { bval }.subspan(offset, value.bval_words().size())));
        }

        // A resumed load must use the current frame, including all live bits in
        // the last partial word, rather than a value folded from initialization.
        auto changed = PackedLogic4(width, fsim::runtime::Logic4::z);
        changed.set(width - 1U, fsim::runtime::Logic4::x);
        std::ranges::copy(changed.aval_words(), aval.begin());
        std::ranges::copy(changed.bval_words(), bval.begin());
        assert(jit.resume(handle, descriptor, frame, result)
            == JitResumeStatus::paused);
        assert(result.instruction == 4U);
        assert(std::ranges::equal(changed.aval_words(),
            std::span { aval }.subspan(word_count, word_count)));
        assert(std::ranges::equal(changed.bval_words(),
            std::span { bval }.subspan(word_count, word_count)));
    }
}

void test_wide_transient_register_frame_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    for (const auto width : { 65U, 100U, 129U, 257U, 1024U }) {
        const auto word_count = (width + 63U) / 64U;
        const auto instance_symbol = std::string { symbol }
            + "_" + std::to_string(width);
        auto value = PackedLogic4(width, Logic4::zero);
        value.set(0, Logic4::one);
        value.set(width / 3U, Logic4::x);
        value.set(width / 2U, Logic4::z);
        value.set(width - 1U, Logic4::one);

        Process process;
        process.id = 98;
        process.name = "wide_transient_register_frame";
        process.register_count = 2;
        process.static_sensitivity.push_back({ 0U, EdgeKind::any });
        process.operations = {
            LoadConstant { 0, value },
            CopyRegister { 1, 0 },
            WriteUpdate { 0, 1 },
            WaitSensitivity { },
            Jump { 0 },
        };
        const std::array<std::uint32_t, 1> widths { width };

        auto options = LlvmJitOptions { optimization, { } };
        options.debug_instrumentation = false;
        LlvmJit jit { std::move(options) };
        assert(jit.supports_process(process, widths));
        jit.add_process(instance_symbol, process, widths);
        const auto handle = jit.lookup(instance_symbol);
        const auto layout = jit.frame_layout(handle);
        assert(layout.register_word_count == 2U * word_count);

        std::vector<std::uint64_t> aval(
            layout.register_word_count, 0xaaaaaaaaaaaaaaaaULL);
        std::vector<std::uint64_t> bval(
            layout.register_word_count, 0x5555555555555555ULL);
        std::vector<std::uint8_t> initialized(layout.register_count);
        fsim_jit_frame_v2 frame { };
        jit.initialize_frame(handle, frame, aval, bval, initialized);
        std::ranges::fill(aval, 0xaaaaaaaaaaaaaaaaULL);
        std::ranges::fill(bval, 0x5555555555555555ULL);
        const auto initial_aval = aval;
        const auto initial_bval = bval;
        TestRuntime runtime;
        auto descriptor = abi(runtime);
        auto services = copy_jit_services(descriptor);
        services.execute_signal_operation = nullptr;
        descriptor.services = &services;
        auto result = new_resume_result();

        for (std::size_t resume = 0; resume < 2; ++resume) {
            assert(jit.resume(handle, descriptor, frame, result)
                == JitResumeStatus::wait_sensitivity);
            assert(result.instruction == 3U);
            assert(frame.program_counter == 4U);
            assert(std::ranges::equal(
                value.aval_words(), runtime.wide_signal_aval[0]));
            assert(std::ranges::equal(
                value.bval_words(), runtime.wide_signal_bval[0]));
            assert(aval == initial_aval);
            assert(bval == initial_bval);
        }
    }
}

void test_optimized_frame_initialization_elision_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    Process process;
    process.id = 99;
    process.name = "optimized_frame_initialization_elision";
    process.register_count = 2;
    process.operations = {
        LoadConstant {
            0, PackedLogic4::from_aval_bval(
                   64, UINT64_C(0x0123456789abcdef), 0) },
        CopyRegister { 1, 0 },
        Pause { },
        Stop { },
    };

    LlvmJitOptions options;
    options.optimization = optimization;
    options.debug_instrumentation = false;
    LlvmJit jit { options };
    jit.add_process(symbol, process, { });
    const auto handle = jit.lookup(symbol);
    const auto layout = jit.frame_layout(handle);
    assert(!layout.tracks_register_initialization);

    std::vector<std::uint64_t> aval(layout.register_word_count);
    std::vector<std::uint64_t> bval(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v2 frame { };
    jit.initialize_frame(handle, frame, aval, bval, initialized);
    TestRuntime runtime;
    auto descriptor = abi(runtime);
    auto result = new_resume_result();
    assert(jit.resume(handle, descriptor, frame, result)
        == JitResumeStatus::paused);
    assert(result.instruction == 2);
    assert((initialized == std::vector<std::uint8_t> { 0, 0 }));
    assert((aval == std::vector<std::uint64_t> {
        UINT64_C(0x0123456789abcdef),
        UINT64_C(0x0123456789abcdef) }));
    assert((bval == std::vector<std::uint64_t> { 0, 0 }));
}

void test_wide_signal_read_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    Process process;
    process.id = 96;
    process.name = "wide_signal_read";
    process.register_count = 1;
    process.operations = {
        ReadSignal { 0, 0 },
        Pause { },
        Stop { },
    };
    const std::array<std::uint32_t, 1> widths { 257 };

    LlvmJit jit { LlvmJitOptions { optimization, { } } };
    assert(jit.supports_process(process, widths));
    jit.add_process(symbol, process, widths);
    const auto handle = jit.lookup(symbol);
    const auto layout = jit.frame_layout(handle);
    assert((layout.direct_read_signals
        == std::vector<runtime::simir::SignalId> { 0 }));
    std::vector<std::uint64_t> aval(layout.register_word_count);
    std::vector<std::uint64_t> bval(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v2 frame { };
    jit.initialize_frame(handle, frame, aval, bval, initialized);

    const std::vector<std::uint64_t> expected_aval {
        UINT64_C(0x0123456789abcdef),
        UINT64_C(0xfedcba9876543210),
        UINT64_C(0x1111222233334444),
        UINT64_C(0xaaaabbbbccccdddd),
        UINT64_C(1),
    };
    const std::vector<std::uint64_t> expected_bval {
        0U, UINT64_C(0x8000000000000000), 0U, 1U, 0U,
    };
    TestRuntime runtime;
    runtime.wide_signal_aval[0].assign(expected_aval.size(), 0U);
    runtime.wide_signal_bval[0].assign(expected_bval.size(), 0U);
    auto descriptor = abi(runtime);
    auto services = copy_jit_services(descriptor);
    services.execute_signal_operation = nullptr;
    descriptor.services = &services;
    const std::array<std::uint32_t, 1> direct_signals { 0 };
    const std::array<std::uint32_t, 1> direct_offsets { 0 };
    descriptor.direct_read_signals = direct_signals.data();
    descriptor.direct_read_signal_count = 1;
    descriptor.direct_wide_signal_aval = expected_aval.data();
    descriptor.direct_wide_signal_bval = expected_bval.data();
    descriptor.direct_wide_signal_offsets = direct_offsets.data();
    descriptor.direct_wide_signal_offset_count = 1;
    descriptor.direct_wide_word_count = 5;
    auto result = new_resume_result();
    assert(jit.resume(handle, descriptor, frame, result)
        == JitResumeStatus::paused);
    assert(initialized[0] == 1U);
    assert(aval == expected_aval);
    assert(bval == expected_bval);

    Process fallback_process;
    fallback_process.id = 97;
    fallback_process.name = "wide_alias_direct_read_fallback";
    fallback_process.register_count = 1;
    fallback_process.operations = {
        ReadSignal { 0, 0 },
        WriteBlocking { 1U, 0U },
        Halt { },
    };
    const auto fallback_symbol = std::string { symbol } + "_fallback";
    const std::array<std::uint32_t, 2> fallback_widths { 257U, 257U };
    jit.add_process(fallback_symbol, fallback_process, fallback_widths);
    const auto fallback_handle = jit.lookup(fallback_symbol);
    assert((jit.frame_layout(fallback_handle).direct_read_signals
        == std::vector<runtime::simir::SignalId> { 0U }));
    const auto fallback_layout = jit.frame_layout(fallback_handle);
    std::vector<std::uint64_t> fallback_register_aval(
        fallback_layout.register_word_count);
    std::vector<std::uint64_t> fallback_register_bval(
        fallback_layout.register_word_count);
    std::vector<std::uint8_t> fallback_initialized(
        fallback_layout.register_count);
    fsim_jit_frame_v2 fallback_frame { };
    jit.initialize_frame(
        fallback_handle,
        fallback_frame,
        fallback_register_aval,
        fallback_register_bval,
        fallback_initialized);

    TestRuntime fallback_runtime;
    fallback_runtime.wide_signal_aval[0] = expected_aval;
    fallback_runtime.wide_signal_bval[0] = expected_bval;
    auto fallback_descriptor = abi(fallback_runtime);
    const std::array<std::uint32_t, 1> unsupported_map {
        std::numeric_limits<std::uint32_t>::max()
    };
    const std::array<std::uint64_t, 5> stale_direct_aval {
        UINT64_C(0xdeadbeef), UINT64_C(0xdeadbeef),
        UINT64_C(0xdeadbeef), UINT64_C(0xdeadbeef), UINT64_C(0xdeadbeef)
    };
    const std::array<std::uint64_t, 5> stale_direct_bval { };
    const std::array<std::uint32_t, 1> fallback_direct_offsets { 0U };
    fallback_descriptor.direct_read_signals = unsupported_map.data();
    fallback_descriptor.direct_read_signal_count = 1U;
    fallback_descriptor.direct_wide_signal_aval = stale_direct_aval.data();
    fallback_descriptor.direct_wide_signal_bval = stale_direct_bval.data();
    fallback_descriptor.direct_wide_signal_offsets
        = fallback_direct_offsets.data();
    fallback_descriptor.direct_wide_signal_offset_count = 1U;
    fallback_descriptor.direct_wide_word_count = 5U;
    auto fallback_result = new_resume_result();
    assert(jit.resume(
               fallback_handle,
               fallback_descriptor,
               fallback_frame,
               fallback_result)
        == JitResumeStatus::completed);
    assert(fallback_runtime.packed_signal_reads == 1U);
    assert(fallback_runtime.wide_signal_aval[1] == expected_aval);
    assert(fallback_runtime.wide_signal_bval[1] == expected_bval);
}

void test_element_alias_wide_direct_read_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    constexpr runtime::simir::SignalId aggregate_proxy { 0U };
    constexpr runtime::simir::SignalId element_alias { 1U };
    Process process;
    process.id = 95U;
    process.name = "element_alias_wide_direct_read";
    process.register_count = 2U;
    process.operations = {
        ReadSignal { 0U, element_alias },
        ReadSignal { 1U, aggregate_proxy },
        Pause { },
        Stop { },
    };
    const std::array<std::uint32_t, 2U> widths { 129U, 129U };

    LlvmJit jit { LlvmJitOptions { optimization, { } } };
    assert(jit.supports_process(process, widths));
    jit.add_process(symbol, process, widths);
    const auto handle = jit.lookup(symbol);
    const auto layout = jit.frame_layout(handle);
    assert(layout.direct_read_signals.size() == 2U);
    assert(std::ranges::find(
        layout.direct_read_signals, element_alias)
        != layout.direct_read_signals.end());
    assert(std::ranges::find(
        layout.direct_read_signals, aggregate_proxy)
        != layout.direct_read_signals.end());

    std::vector<std::uint64_t> register_aval(layout.register_word_count);
    std::vector<std::uint64_t> register_bval(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v2 frame { };
    jit.initialize_frame(
        handle, frame, register_aval, register_bval, initialized);

    const std::array<std::uint64_t, 3U> expected_leaf_aval {
        UINT64_C(0x0123456789abcdef),
        UINT64_C(0xfedcba9876543210),
        UINT64_C(1),
    };
    const std::array<std::uint64_t, 3U> expected_leaf_bval {
        0U, UINT64_C(0x8000000000000000), 0U,
    };
    const std::array<std::uint64_t, 3U> expected_proxy_aval {
        UINT64_C(0x1122334455667788),
        UINT64_C(0x8877665544332211),
        UINT64_C(1),
    };
    const std::array<std::uint64_t, 3U> expected_proxy_bval {
        0U, UINT64_C(0x8000000000000000), 0U,
    };
    std::array<std::uint64_t, 6U> direct_aval { };
    std::array<std::uint64_t, 6U> direct_bval { };
    std::copy(expected_leaf_aval.begin(), expected_leaf_aval.end(),
        direct_aval.begin() + 3);
    std::copy(expected_leaf_bval.begin(), expected_leaf_bval.end(),
        direct_bval.begin() + 3);
    TestRuntime runtime;
    runtime.wide_signal_aval[aggregate_proxy].assign(
        expected_proxy_aval.begin(), expected_proxy_aval.end());
    runtime.wide_signal_bval[aggregate_proxy].assign(
        expected_proxy_bval.begin(), expected_proxy_bval.end());
    runtime.wide_signal_aval[element_alias].assign(3U, 0U);
    runtime.wide_signal_bval[element_alias].assign(3U, 0U);
    auto descriptor = abi(runtime);
    std::vector<std::uint32_t> direct_signals(
        layout.direct_read_signals.size(),
        std::numeric_limits<std::uint32_t>::max());
    for (std::size_t slot = 0U;
         slot < layout.direct_read_signals.size();
         ++slot) {
        if (layout.direct_read_signals[slot] == element_alias) {
            direct_signals[slot] = element_alias;
        }
    }
    const std::array<std::uint32_t, 2U> direct_offsets { 0U, 3U };
    descriptor.direct_read_signals = direct_signals.data();
    descriptor.direct_read_signal_count
        = static_cast<std::uint32_t>(direct_signals.size());
    descriptor.direct_wide_signal_aval = direct_aval.data();
    descriptor.direct_wide_signal_bval = direct_bval.data();
    descriptor.direct_wide_signal_offsets = direct_offsets.data();
    descriptor.direct_wide_signal_offset_count
        = static_cast<std::uint32_t>(direct_offsets.size());
    descriptor.direct_wide_word_count
        = static_cast<std::uint32_t>(direct_aval.size());

    auto result = new_resume_result();
    assert(jit.resume(handle, descriptor, frame, result)
        == JitResumeStatus::paused);
    assert(initialized[0U] == 1U && initialized[1U] == 1U);
    assert(std::equal(
        expected_leaf_aval.begin(), expected_leaf_aval.end(),
        register_aval.begin()));
    assert(std::equal(
        expected_leaf_bval.begin(), expected_leaf_bval.end(),
        register_bval.begin()));
    assert(std::equal(
        expected_proxy_aval.begin(), expected_proxy_aval.end(),
        register_aval.begin() + 3));
    assert(std::equal(
        expected_proxy_bval.begin(), expected_proxy_bval.end(),
        register_bval.begin() + 3));
    // The proxy's runtime map slot remains invalid, so its read takes the
    // checked callback. The mapped leaf returns deliberately conflicting
    // callback planes, proving the O0/O2 result came from direct leaf planes.
    assert(runtime.packed_signal_reads == 1U);
}

void test_wide_signal_attributes_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    for (const auto value_kind : {
             runtime::simir::ValueKind::logic4,
             runtime::simir::ValueKind::logic9 }) {
        Process process;
        process.id = 97;
        process.name = "wide_signal_attributes";
        process.register_count = 2;
        process.register_value_kinds = { value_kind, value_kind };
        process.operations = {
            SignalLastValue { 0, 0 },
            SignalDrivingValue { 1, 1 },
            Halt { },
        };
        const std::array<std::uint32_t, 2> widths { 129U, 129U };
        const std::array value_kinds { value_kind, value_kind };
        const auto suffix = value_kind == runtime::simir::ValueKind::logic9
            ? "_logic9"
            : "_logic4";
        const auto process_symbol = std::string { symbol } + suffix;

        LlvmJit jit { LlvmJitOptions { optimization, { } } };
        assert(jit.supports_process(process, widths, value_kinds));
        jit.add_process(process_symbol, process, widths, value_kinds);
        const auto handle = jit.lookup(process_symbol);
        const auto layout = jit.frame_layout(handle);
        assert(layout.register_widths.size() == 2U);
        assert(layout.register_widths[0] == 129U);
        assert(layout.register_widths[1] == 129U);
        assert(layout.uses_logic9
            == (value_kind == runtime::simir::ValueKind::logic9));
        std::vector<std::uint64_t> aval(layout.register_word_count);
        std::vector<std::uint64_t> bval(layout.register_word_count);
        std::vector<std::uint64_t> plane2(
            layout.uses_logic9 ? layout.register_word_count : 0U);
        std::vector<std::uint64_t> plane3(
            layout.uses_logic9 ? layout.register_word_count : 0U);
        std::vector<std::uint8_t> initialized(layout.register_count);
        fsim_jit_frame_v2 frame { };
        jit.initialize_frame(
            handle, frame, aval, bval, initialized, plane2, plane3);
        TestRuntime runtime;
        auto descriptor = abi(runtime);
        auto result = new_resume_result();
        assert(jit.resume(handle, descriptor, frame, result)
            == JitResumeStatus::simir_boundary);
        assert(result.instruction == 0U);
        assert(frame.program_counter == 1U);
        assert(jit.resume(handle, descriptor, frame, result)
            == JitResumeStatus::simir_boundary);
        assert(result.instruction == 1U);
        assert(frame.program_counter == 2U);
        assert(jit.resume(handle, descriptor, frame, result)
            == JitResumeStatus::completed);
    }
}

void test_wide_signal_write_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    auto whole = PackedLogic4(257, Logic4::zero);
    whole.set(0, Logic4::one);
    whole.set(128, Logic4::x);
    whole.set(256, Logic4::one);
    auto slice = PackedLogic4(129, Logic4::zero);
    slice.set(0, Logic4::z);
    slice.set(64, Logic4::one);
    slice.set(128, Logic4::x);

    Process process;
    process.id = 97;
    process.name = "wide_signal_write";
    process.register_count = 2;
    process.operations = {
        LoadConstant { 0, whole },
        LoadConstant { 1, slice },
        WriteBlocking { 0, 0 },
        WriteUpdate { 1, 0 },
        WriteAfter { 2, 0, 11 },
        WriteBlockingSlice { 3, 1, 64 },
        WriteUpdateSlice { 4, 1, 64 },
        WriteAfterSlice { 5, 1, 64, 13 },
        Pause { },
        Stop { },
    };
    const std::array<std::uint32_t, 6> widths {
        257, 257, 257, 257, 257, 257
    };

    LlvmJit jit { LlvmJitOptions { optimization, { } } };
    assert(jit.supports_process(process, widths));
    jit.add_process(symbol, process, widths);
    const auto handle = jit.lookup(symbol);
    const auto layout = jit.frame_layout(handle);
    std::vector<std::uint64_t> aval(layout.register_word_count);
    std::vector<std::uint64_t> bval(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v2 frame { };
    jit.initialize_frame(handle, frame, aval, bval, initialized);
    TestRuntime runtime;
    auto descriptor = abi(runtime);
    auto services = copy_jit_services(descriptor);
    services.execute_signal_operation = nullptr;
    descriptor.services = &services;
    auto result = new_resume_result();
    assert(jit.resume(handle, descriptor, frame, result)
        == JitResumeStatus::paused);
    assert((runtime.packed_signal_write_modes
        == std::vector<std::uint32_t> {
            FSIM_JIT_PACKED_SIGNAL_WRITE_BLOCKING_V2,
            FSIM_JIT_PACKED_SIGNAL_WRITE_UPDATE_V2,
            FSIM_JIT_PACKED_SIGNAL_WRITE_AFTER_V2,
            FSIM_JIT_PACKED_SIGNAL_WRITE_BLOCKING_SLICE_V2,
            FSIM_JIT_PACKED_SIGNAL_WRITE_UPDATE_SLICE_V2,
            FSIM_JIT_PACKED_SIGNAL_WRITE_AFTER_SLICE_V2,
        }));
    assert((runtime.packed_signal_write_signals
        == std::vector<std::uint32_t> { 0, 1, 2, 3, 4, 5 }));
    for (std::uint32_t signal = 0; signal < 3; ++signal) {
        assert(std::ranges::equal(
            whole.aval_words(), runtime.wide_signal_aval[signal]));
        assert(std::ranges::equal(
            whole.bval_words(), runtime.wide_signal_bval[signal]));
    }
    for (std::uint32_t signal = 3; signal < 6; ++signal) {
        assert(std::ranges::equal(
            slice.aval_words(), runtime.wide_signal_aval[signal]));
        assert(std::ranges::equal(
            slice.bval_words(), runtime.wide_signal_bval[signal]));
    }
    assert(runtime.packed_signal_write_offset == 64U);
    assert(runtime.packed_signal_write_width == 129U);
    assert(runtime.packed_signal_write_delay == 13U);
}

void test_wide_container_operations_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    Process process;
    process.id = 94;
    process.name = "wide_container_operations";
    process.register_count = 2;
    process.container_register_count = 1;
    ContainerType type;
    type.fixed = true;
    type.index_left = 0;
    type.index_right = 0;
    type.element_kind = ContainerElementKind::Packed;
    type.element_width = 257;
    process.container_register_types.push_back(type);
    process.operations = {
        LoadConstant {
            0, PackedLogic4::from_aval_bval(32, 0U, 0U) },
        ContainerRead { 1, 0, 0, true, false, false },
        ContainerWrite { 0, 0, 1, true, false, false },
        Pause { },
        Stop { },
    };

    LlvmJit jit { LlvmJitOptions { optimization, { } } };
    assert(jit.supports_process(process, { }));
    jit.add_process(symbol, process, { });
    const auto handle = jit.lookup(symbol);
    const auto layout = jit.frame_layout(handle);
    std::vector<std::uint64_t> aval(layout.register_word_count);
    std::vector<std::uint64_t> bval(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v2 frame { };
    jit.initialize_frame(handle, frame, aval, bval, initialized);

    TestRuntime runtime;
    runtime.container_read_aval = {
        UINT64_C(0x0123456789abcdef),
        UINT64_C(0xfedcba9876543210),
        UINT64_C(0x1111222233334444),
        UINT64_C(0xaaaabbbbccccdddd),
        UINT64_C(1)
    };
    runtime.container_read_bval = {
        0U, UINT64_C(0x8000000000000000), 0U, 1U, 0U
    };
    auto descriptor = abi(runtime);
    auto result = new_resume_result();
    assert(jit.resume(handle, descriptor, frame, result)
        == JitResumeStatus::paused);
    assert(runtime.container_packed_reads == 1U);
    assert(runtime.container_packed_writes == 1U);
    assert(runtime.container_write_aval == runtime.container_read_aval);
    assert(runtime.container_write_bval == runtime.container_read_bval);
}

void test_fused_container_object_read_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol,
    const std::size_t index_width,
    const bool logic9_index)
{
    Process process;
    process.id = 108;
    process.name = "fused_container_object_read";
    process.register_count = 2;
    if (logic9_index) {
        process.register_value_kinds = {
            ValueKind::logic9, ValueKind::logic4
        };
    }
    process.container_register_count = 1;
    ContainerType type;
    type.fixed = true;
    type.index_left = 0;
    type.index_right = 0;
    type.element_kind = ContainerElementKind::Packed;
    type.element_width = 8;
    process.container_register_types.push_back(type);
    const auto index_value = logic9_index
        ? PackedLogic4::from_logic9_msb_string(
              std::string(index_width, '0'))
        : PackedLogic4::from_aval_bval(index_width, 0U, 0U);
    process.operations = {
        LoadConstant {
            0, index_value },
        ReadContainerObject { 0, 7 },
        LoadConstant { 0, index_value },
        LoadConstant { 0, index_value },
        LoadConstant { 0, index_value },
        ContainerRead { 1, 0, 0, true, false, false },
        WriteBlocking { 0, 1 },
        Pause { },
        Stop { },
    };

    LlvmJitOptions options;
    options.optimization = optimization;
    options.cache_directory.clear();
    options.debug_instrumentation = false;
    LlvmJit jit { options };
    const std::array<std::uint32_t, 1> widths { 8 };
    assert(jit.supports_process(process, widths));
    jit.add_process(symbol, process, widths);
    const auto handle = jit.lookup(symbol);
    const auto layout = jit.frame_layout(handle);
    assert(layout.uses_logic9 == logic9_index);
    std::vector<std::uint64_t> aval(layout.register_word_count);
    std::vector<std::uint64_t> bval(layout.register_word_count);
    std::vector<std::uint64_t> plane2(layout.register_word_count);
    std::vector<std::uint64_t> plane3(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v2 frame { };
    jit.initialize_frame(
        handle, frame, aval, bval, initialized, plane2, plane3);

    TestRuntime runtime;
    auto descriptor = abi(runtime);
    auto services = copy_jit_services(descriptor);
    services.container_operation = [](
        void* opaque, std::uint32_t, const std::uint32_t instruction,
        std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t,
        std::uint64_t* result_aval, std::uint64_t* result_bval) {
        auto& observed = *static_cast<TestRuntime*>(opaque);
        ++observed.container_packed_writes;
        observed.report_instructions.push_back(instruction);
        if (instruction == 5U) {
            *result_aval = UINT64_C(0x5a);
            *result_bval = 0U;
        }
        return std::uint32_t { };
    };
    services.container_read_word = [](
        void* opaque, std::uint32_t, const std::uint32_t instruction,
        std::uint32_t, const std::uint32_t flags,
        std::uint64_t, std::uint64_t,
        std::uint64_t* result_aval, std::uint64_t* result_bval) {
        auto& observed = *static_cast<TestRuntime*>(opaque);
        ++observed.container_packed_reads;
        observed.container_write_aval[0] = instruction;
        observed.container_write_aval[1] = flags;
        *result_aval = UINT64_C(0x5a);
        *result_bval = 0U;
        return std::uint32_t { };
    };
    services.container_read_packed_index64 = [](
        void* opaque, std::uint32_t, const std::uint32_t instruction,
        std::uint32_t, const std::uint32_t flags,
        const std::uint64_t index_aval,
        const std::uint64_t index_bval,
        std::uint64_t* result_aval,
        std::uint64_t* result_bval,
        const std::uint32_t word_count) {
        assert(index_aval == 0U && index_bval == 0U);
        assert(word_count == 1U);
        auto& observed = *static_cast<TestRuntime*>(opaque);
        ++observed.container_packed_reads;
        observed.container_write_aval[0] = instruction;
        observed.container_write_aval[1] = flags;
        *result_aval = UINT64_C(0x5a);
        *result_bval = 0U;
        return std::uint32_t { };
    };
    descriptor.services = &services;
    auto result = new_resume_result();
    assert(jit.resume(handle, descriptor, frame, result)
        == JitResumeStatus::paused);
    const bool fused = (index_width == 32U || index_width == 64U)
        && !logic9_index;
    assert(runtime.container_packed_writes == (fused ? 0U : 2U));
    assert(runtime.container_packed_reads == (fused ? 1U : 0U));
    if (fused) {
        assert(runtime.container_write_aval[0] == 5U);
        assert((runtime.container_write_aval[1] >> 8U) == 4U);
    } else {
        assert((runtime.report_instructions
            == std::vector<std::uint32_t> { 1U, 5U }));
    }
    assert((runtime.writes
        == std::vector<std::pair<std::uint32_t, EncodedSignal>> {
            { 0U, { UINT64_C(0x5a), 0U } }
        }));
}

void test_fused_container_object_native_frame_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol,
    const bool native_isolated)
{
    Process process;
    process.id = 109U;
    process.name = "fused_container_object_native_frame";
    process.register_count = 2U;
    process.container_register_count = 1U;
    ContainerType type;
    type.fixed = true;
    type.index_left = 0;
    type.index_right = 0;
    type.element_kind = ContainerElementKind::Packed;
    type.element_width = 8U;
    process.container_register_types.push_back(type);
    process.operations = {
        LoadConstant { 1U, PackedLogic4(8U, Logic4::x) },
        CallableFramePush { 6U, { }, { }, { 0U }, native_isolated },
        Call { 7U, 3U, { } },
        CallableFramePop { 6U, { }, { }, { } },
        WriteBlocking { 0U, 1U },
        Pause { },
        Stop { },
        ReadContainerObject { 0U, 7U },
        LoadConstant {
            0U, PackedLogic4::from_aval_bval(32U, 0U, 0U) },
        ContainerRead { 1U, 0U, 0U, true, false, false },
        Return { },
    };

    LlvmJitOptions options;
    options.optimization = optimization;
    options.cache_directory.clear();
    options.debug_instrumentation = false;
    LlvmJit jit { options };
    const std::array<std::uint32_t, 1> widths { 8U };
    assert(jit.supports_process(process, widths));
    jit.add_process(symbol, process, widths);
    const auto handle = jit.lookup(symbol);
    const auto layout = jit.frame_layout(handle);
    std::vector<std::uint64_t> aval(layout.register_word_count);
    std::vector<std::uint64_t> bval(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v2 frame { };
    jit.initialize_frame(handle, frame, aval, bval, initialized);
    if (!native_isolated) {
        // The checked scheduler executes the push and call before resuming the
        // callable body. Start at that body entry to inspect its generated
        // fusion flags without emulating the interpreter-owned frame state.
        frame.program_counter = 7U;
    }

    TestRuntime runtime;
    auto descriptor = abi(runtime);
    auto services = copy_jit_services(descriptor);
    services.container_read_word = [](
        void* opaque, std::uint32_t, const std::uint32_t instruction,
        std::uint32_t, const std::uint32_t flags,
        std::uint64_t, std::uint64_t,
        std::uint64_t* result_aval, std::uint64_t* result_bval) {
        auto& observed = *static_cast<TestRuntime*>(opaque);
        ++observed.container_packed_reads;
        observed.container_write_aval[0] = instruction;
        observed.container_write_aval[1] = flags;
        *result_aval = UINT64_C(0x5a);
        *result_bval = 0U;
        return std::uint32_t { };
    };
    descriptor.services = &services;
    auto result = new_resume_result();
    const auto status = jit.resume(handle, descriptor, frame, result);
    assert(runtime.container_packed_reads == 1U);
    assert(runtime.container_write_aval[0] == 9U);
    assert(((runtime.container_write_aval[1] & 4U) != 0U)
        == native_isolated);
    assert((runtime.container_write_aval[1] >> 8U) == 2U);
    if (native_isolated) {
        assert(status == JitResumeStatus::paused);
        assert(frame.native_call_depth == 0U);
        assert((runtime.writes
            == std::vector<std::pair<std::uint32_t, EncodedSignal>> {
                { 0U, { UINT64_C(0x5a), 0U } }
            }));
    } else {
        assert(status == JitResumeStatus::simir_boundary);
        assert(result.instruction == 10U);
        assert(runtime.writes.empty());
    }
}

void test_wide_value_operations_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    auto wide = PackedLogic4(257, Logic4::zero);
    wide.set(0, Logic4::one);
    wide.set(2, Logic4::one);
    wide.set(256, Logic4::one);
    auto one = PackedLogic4(257, Logic4::zero);
    one.set(0, Logic4::one);
    auto three = one;
    three.set(1, Logic4::one);
    auto wide_part = PackedLogic4(129, Logic4::zero);
    wide_part.set(0, Logic4::one);
    wide_part.set(64, Logic4::x);
    wide_part.set(128, Logic4::z);
    auto unknown_addend = wide;
    unknown_addend.set(64, Logic4::x);
    auto all_ones = PackedLogic4(257, Logic4::one);

    Process process;
    process.id = 92;
    process.name = "wide_value_operations";
    process.register_count = 66;
    process.operations = {
        LoadConstant { 0, wide },
        LoadConstant { 1, one },
        LoadConstant { 2, three },
        Binary { BinaryOperator::bit_and, 3, 0, 2 },
        Binary { BinaryOperator::bit_or, 4, 0, 2 },
        Binary { BinaryOperator::bit_xor, 5, 0, 2 },
        Binary { BinaryOperator::add_unsigned, 6, 0, 2 },
        Binary { BinaryOperator::subtract_unsigned, 7, 0, 2 },
        Binary { BinaryOperator::multiply_unsigned, 8, 0, 2 },
        Binary { BinaryOperator::power_unsigned, 9, 0, 2 },
        Binary { BinaryOperator::divide_unsigned, 10, 0, 1 },
        Binary { BinaryOperator::modulo_unsigned, 11, 0, 1 },
        Binary { BinaryOperator::add_signed, 12, 0, 2 },
        Binary { BinaryOperator::subtract_signed, 13, 0, 2 },
        Binary { BinaryOperator::multiply_signed, 14, 0, 2 },
        Binary { BinaryOperator::power_signed, 15, 0, 2 },
        Binary { BinaryOperator::divide_signed, 16, 0, 1 },
        Binary { BinaryOperator::remainder_signed, 17, 0, 1 },
        Binary { BinaryOperator::modulo_signed, 18, 0, 1 },
        Binary { BinaryOperator::equal, 19, 0, 0 },
        Binary { BinaryOperator::not_equal, 20, 0, 2 },
        Binary { BinaryOperator::less_unsigned, 21, 2, 0 },
        Binary { BinaryOperator::less_signed, 22, 0, 2 },
        Binary { BinaryOperator::case_equal, 23, 0, 0 },
        UnaryNot { 24, 0 },
        LogicalNot { 25, 0 },
        LogicalBinary { LogicalBinaryOperator::logical_and, 26, 0, 2 },
        Reduction { ReductionOperator::bit_xor, 27, 0 },
        CountOnes { 28, 0 },
        Concatenate { 29, { 0, 2 }, 514 },
        Extract { 30, 29, 0, 257 },
        Insert { 31, 0, 19, 128 },
        ConditionalSelect { 32, 19, 0, 2 },
        Shift { ShiftOperator::logical_left, 33, 0, 1, false },
        Shift { ShiftOperator::logical_right, 34, 0, 1, false },
        Shift { ShiftOperator::arithmetic_left, 35, 0, 1, false },
        Shift { ShiftOperator::arithmetic_right, 36, 0, 1, false },
        Shift { ShiftOperator::rotate_left, 37, 0, 1, false },
        Shift { ShiftOperator::rotate_right, 38, 0, 1, false },
        LoadConstant { 39, PackedLogic4::from_aval_bval(32, 128, 0) },
        LoadConstant { 40, PackedLogic4::from_aval_bval(32, 256, 0) },
        DynamicExtract { 41, 0, DynamicIndex { 40, 256, 0, 0 } },
        DynamicInsert { 42, 0, 19, DynamicIndex { 39, 256, 0, 0 } },
        DynamicPartSelect { 43, 0, 40, 256, 0, 1, true, true, false, 0 },
        DynamicPartInsert {
            44, 0, 19, DynamicPartIndex { 39, 256, 0, 0, 1, true, true } },
        LoadConstant { 45, wide_part },
        LoadConstant { 46, PackedLogic4::from_aval_bval(32, 64, 0) },
        DynamicPartInsert {
            47, 0, 45,
            DynamicPartIndex { 46, 256, 0, 0, 129, true, true } },
        DynamicPartSelect {
            48, 47, 46, 256, 0, 129, true, true, false, 0 },
        ConvertToTwoState { 49, 45 },
        LoadConstant { 50, unknown_addend },
        Binary { BinaryOperator::add_unsigned, 51, 50, 1 },
        Binary { BinaryOperator::add_signed, 52, 50, 1 },
        LoadConstant { 53, all_ones },
        Binary { BinaryOperator::add_unsigned, 54, 53, 1 },
        Binary { BinaryOperator::add_signed, 55, 53, 1 },
        Binary { BinaryOperator::add_unsigned, 56, 1, 1 },
        LoadConstant { 57, PackedLogic4(257, Logic4::zero) },
        Binary { BinaryOperator::power_unsigned, 58, 57, 57 },
        Binary { BinaryOperator::power_signed, 59, 57, 57 },
        Binary { BinaryOperator::power_signed, 60, 57, 53 },
        Binary { BinaryOperator::power_signed, 61, 1, 53 },
        Binary { BinaryOperator::power_signed, 62, 53, 53 },
        Binary { BinaryOperator::power_signed, 63, 56, 53 },
        Binary { BinaryOperator::power_unsigned, 64, 57, 50 },
        Binary { BinaryOperator::power_signed, 65, 0, 50 },
        Pause { },
        Stop { },
    };

    LlvmJit jit { LlvmJitOptions { optimization, { } } };
    assert(jit.supports_process(process, { }));
    jit.add_process(symbol, process, { });
    const auto handle = jit.lookup(symbol);
    const auto layout = jit.frame_layout(handle);
    std::vector<std::uint64_t> aval(layout.register_word_count);
    std::vector<std::uint64_t> bval(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v2 frame { };
    jit.initialize_frame(handle, frame, aval, bval, initialized);
    TestRuntime runtime;
    auto descriptor = abi(runtime);
    auto result = new_resume_result();
    assert(jit.resume(handle, descriptor, frame, result)
        == JitResumeStatus::paused);

    const auto read = [&](const RegisterId id) {
        const auto width = layout.register_widths[id];
        const auto offset = layout.register_word_offsets[id];
        const auto words = (static_cast<std::size_t>(width) + 63U) / 64U;
        return PackedLogic4::from_word_planes(
            width,
            std::span { aval }.subspan(offset, words),
            std::span { bval }.subspan(offset, words));
    };
    const auto scalar = [&](const RegisterId id) {
        const auto value = read(id).known_unsigned_value();
        assert(value);
        return *value;
    };
    const auto expect_wide = [&](const RegisterId id,
                                 const PackedLogic4& expected) {
        assert(read(id).to_msb_string() == expected.to_msb_string());
    };

    assert(scalar(3) == 1);
    auto expected_or = wide;
    expected_or.set(1, Logic4::one);
    expect_wide(4, expected_or);
    auto expected_xor = expected_or;
    expected_xor.set(0, Logic4::zero);
    expect_wide(5, expected_xor);
    auto expected_add = PackedLogic4(257, Logic4::zero);
    expected_add.set(3, Logic4::one);
    expected_add.set(256, Logic4::one);
    expect_wide(6, expected_add);
    auto expected_subtract = PackedLogic4(257, Logic4::zero);
    expected_subtract.set(1, Logic4::one);
    expected_subtract.set(256, Logic4::one);
    expect_wide(7, expected_subtract);
    auto expected_multiply = PackedLogic4(257, Logic4::zero);
    for (const auto bit : { 0U, 1U, 2U, 3U, 256U }) {
        expected_multiply.set(bit, Logic4::one);
    }
    expect_wide(8, expected_multiply);
    auto expected_power = PackedLogic4(257, Logic4::zero);
    for (const auto bit : { 0U, 2U, 3U, 4U, 5U, 6U, 256U }) {
        expected_power.set(bit, Logic4::one);
    }
    for (const auto id : { 9U, 15U }) {
        expect_wide(id, expected_power);
    }
    for (const auto id : { 10U, 16U }) {
        expect_wide(id, wide);
    }
    for (const auto id : { 11U, 17U, 18U }) {
        assert(scalar(id) == 0);
    }
    expect_wide(12, expected_add);
    expect_wide(13, expected_subtract);
    expect_wide(14, expected_multiply);
    for (const auto id : { 19U, 20U, 21U, 22U, 23U, 26U, 27U }) {
        assert(scalar(id) == 1);
    }
    const auto inverted = read(24);
    assert(inverted.get(0) == Logic4::zero);
    assert(inverted.get(1) == Logic4::one);
    assert(inverted.get(2) == Logic4::zero);
    assert(inverted.get(255) == Logic4::one);
    assert(inverted.get(256) == Logic4::zero);
    assert(scalar(25) == 0);
    assert(scalar(28) == 3);
    const auto concatenated = read(29);
    assert(concatenated.get(0) == Logic4::one);
    assert(concatenated.get(1) == Logic4::one);
    assert(concatenated.get(256) == Logic4::zero);
    assert(concatenated.get(257) == Logic4::one);
    assert(concatenated.get(259) == Logic4::one);
    assert(concatenated.get(513) == Logic4::one);
    expect_wide(30, three);
    auto expected_insert = wide;
    expected_insert.set(128, Logic4::one);
    expect_wide(31, expected_insert);
    expect_wide(32, wide);
    auto expected_left = PackedLogic4(257, Logic4::zero);
    expected_left.set(1, Logic4::one);
    expected_left.set(3, Logic4::one);
    expect_wide(33, expected_left);
    auto expected_right = PackedLogic4(257, Logic4::zero);
    expected_right.set(1, Logic4::one);
    expected_right.set(255, Logic4::one);
    expect_wide(34, expected_right);
    auto expected_arithmetic_left = expected_left;
    expected_arithmetic_left.set(0, Logic4::one);
    expect_wide(35, expected_arithmetic_left);
    auto expected_arithmetic_right = expected_right;
    expected_arithmetic_right.set(256, Logic4::one);
    expect_wide(36, expected_arithmetic_right);
    expect_wide(37, expected_arithmetic_left);
    expect_wide(38, expected_arithmetic_right);
    assert(scalar(41) == 1);
    expect_wide(42, expected_insert);
    assert(scalar(43) == 1);
    expect_wide(44, expected_insert);
    auto expected_wide_part_insert = wide;
    for (std::size_t bit = 0; bit < wide_part.width(); ++bit) {
        expected_wide_part_insert.set(64 + bit, wide_part.get(bit));
    }
    expect_wide(47, expected_wide_part_insert);
    expect_wide(48, wide_part);
    auto expected_two_state = PackedLogic4(129, Logic4::zero);
    expected_two_state.set(0, Logic4::one);
    expect_wide(49, expected_two_state);
    const auto all_unknown = PackedLogic4(257, Logic4::x);
    expect_wide(51, all_unknown);
    expect_wide(52, all_unknown);
    const auto wrapped_zero = PackedLogic4(257, Logic4::zero);
    expect_wide(54, wrapped_zero);
    expect_wide(55, wrapped_zero);
    auto expected_two = PackedLogic4(257, Logic4::zero);
    expected_two.set(1, Logic4::one);
    expect_wide(56, expected_two);
    expect_wide(58, one);
    expect_wide(59, one);
    expect_wide(60, all_unknown);
    expect_wide(61, one);
    expect_wide(62, all_ones);
    expect_wide(63, wrapped_zero);
    expect_wide(64, all_unknown);
    expect_wide(65, all_unknown);
}

void test_constant_dynamic_part_select_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    const auto rom = PackedLogic4::from_logic9_msb_string(
        "01UXZWLH-10HLWZX");
    Process process;
    process.id = 108;
    process.name = "constant_dynamic_part_select";
    process.register_count = 3;
    process.register_value_kinds = {
        ValueKind::logic4, ValueKind::logic9, ValueKind::logic9
    };
    process.operations = {
        LoadConstant {
            0, PackedLogic4::from_aval_bval(32, 8, 0) },
        LoadConstant { 1, rom },
        DynamicPartSelect {
            2, 1, 0, 15, 0, 8, true, true, false, 0 },
        WriteBlocking { 0, 2 },
        Halt { }
    };
    const std::array<std::uint32_t, 1> widths { 8 };
    const std::array<ValueKind, 1> kinds { ValueKind::logic9 };
    auto options = LlvmJitOptions { optimization, { } };
    options.debug_instrumentation = false;
    LlvmJit jit { options };
    jit.add_process(symbol, process, widths, kinds);

    TestRuntime runtime;
    auto descriptor = abi(runtime);
    assert(jit.execute(jit.lookup(symbol), descriptor)
        == JitExecutionStatus::completed);
    const auto expected = rom.extract_bits(8, 8).logic9_low_word().planes;
    assert(runtime.logic9_signals[0] == expected);
}

void test_logic4_constant_dynamic_part_select_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    const auto rom = PackedLogic4::from_aval_bval(
        16, UINT64_C(0xa53c), UINT64_C(0x0082));
    Process process;
    process.id = 109;
    process.name = "logic4_constant_dynamic_part_select";
    process.register_count = 3;
    process.register_value_kinds = {
        ValueKind::logic4, ValueKind::logic4, ValueKind::logic4
    };
    process.operations = {
        LoadConstant {
            0, PackedLogic4::from_aval_bval(32, 8, 0) },
        LoadConstant { 1, rom },
        DynamicPartSelect {
            2, 1, 0, 15, 0, 8, true, true, false, 0 },
        WriteBlocking { 0, 2 },
        Halt { }
    };
    const std::array<std::uint32_t, 1> widths { 8 };
    const std::array<ValueKind, 1> kinds { ValueKind::logic4 };
    auto options = LlvmJitOptions { optimization, { } };
    options.debug_instrumentation = false;
    LlvmJit jit { options };
    jit.add_process(symbol, process, widths, kinds);

    TestRuntime runtime;
    auto descriptor = abi(runtime);
    assert(jit.execute(jit.lookup(symbol), descriptor)
        == JitExecutionStatus::completed);
    const auto expected = rom.extract_bits(8, 8).low_word();
    const auto encoded_expected
        = EncodedSignal { expected.aval, expected.bval };
    assert(runtime.signals[0] == encoded_expected);
}

void test_affine_dynamic_extract_fusion_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    constexpr std::uint32_t result_width = 128U;
    auto source = PackedLogic4(result_width * 2U, Logic4::zero);
    for (std::uint32_t bit = 0U; bit < result_width; ++bit) {
        if (bit % 5U == 1U || bit % 7U == 3U) {
            source.set(result_width + bit, Logic4::one);
        }
    }
    const auto expected = source.extract_bits(result_width, result_width);

    Process process;
    process.id = 110;
    process.name = "affine_dynamic_extract_fusion";
    constexpr RegisterId source_register = 0U;
    constexpr RegisterId index_register = 1U;
    constexpr RegisterId result_register = 2U;
    constexpr RegisterId first_temporary = 3U;
    process.register_count = first_temporary + 7U * result_width;
    process.operations = {
        LoadConstant { source_register, source },
        LoadConstant {
            index_register,
            PackedLogic4::from_aval_bval(32U, 1U, 0U) },
        LoadConstant {
            result_register,
            PackedLogic4(result_width, Logic4::zero) },
    };
    for (std::uint32_t element = 0U; element < result_width; ++element) {
        const auto temporary = static_cast<RegisterId>(
            first_temporary + 7U * element);
        process.operations.emplace_back(LoadConstant {
            temporary,
            PackedLogic4::from_aval_bval(32U, 0U, 0U) });
        process.operations.emplace_back(LoadConstant {
            temporary + 1U,
            PackedLogic4::from_aval_bval(32U, result_width, 0U) });
        process.operations.emplace_back(IntegerBinary {
            IntegerBinaryOperator::multiply,
            temporary + 2U,
            index_register,
            temporary + 1U });
        process.operations.emplace_back(IntegerBinary {
            IntegerBinaryOperator::add,
            temporary + 3U,
            temporary,
            temporary + 2U });
        process.operations.emplace_back(LoadConstant {
            temporary + 4U,
            PackedLogic4::from_aval_bval(32U, element, 0U) });
        process.operations.emplace_back(IntegerBinary {
            IntegerBinaryOperator::add,
            temporary + 5U,
            temporary + 3U,
            temporary + 4U });
        process.operations.emplace_back(DynamicExtract {
            temporary + 6U,
            source_register,
            DynamicIndex {
                temporary + 5U,
                static_cast<std::int64_t>(result_width - 1U),
                0,
                0 } });
        process.operations.emplace_back(Insert {
            result_register,
            result_register,
            temporary + 6U,
            element });
    }
    process.operations.emplace_back(Pause { });
    process.operations.emplace_back(Stop { });

    auto options = LlvmJitOptions { optimization, { } };
    options.debug_instrumentation = false;
    LlvmJit jit { options };
    assert(jit.supports_process(process, { }));
    jit.add_process(symbol, process, { });
    const auto handle = jit.lookup(symbol);
    const auto layout = jit.frame_layout(handle);
    std::vector<std::uint64_t> aval(layout.register_word_count);
    std::vector<std::uint64_t> bval(layout.register_word_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v2 frame { };
    jit.initialize_frame(handle, frame, aval, bval, initialized);
    TestRuntime runtime;
    auto descriptor = abi(runtime);
    auto result = new_resume_result();
    assert(jit.resume(handle, descriptor, frame, result)
        == JitResumeStatus::paused);

    const auto offset = layout.register_word_offsets[result_register];
    const auto words = (result_width + 63U) / 64U;
    const auto actual = PackedLogic4::from_word_planes(
        result_width,
        std::span { aval }.subspan(offset, words),
        std::span { bval }.subspan(offset, words));
    assert(actual.to_msb_string() == expected.to_msb_string());
}

void test_fused_dynamic_part_signal_read_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    Process process;
    process.id = 109;
    process.name = "fused_dynamic_part_signal_read";
    process.register_count = 3;
    process.register_value_kinds = {
        ValueKind::logic4, ValueKind::logic9, ValueKind::logic9
    };
    process.operations = {
        LoadConstant {
            0, PackedLogic4::from_aval_bval(32, 64, 0) },
        ReadSignal { 1, 0 },
        DynamicPartSelect {
            2, 1, 0, 127, 0, 8, true, true, false, 0 },
        WriteBlocking { 1, 2 },
        Halt { }
    };
    const std::array<std::uint32_t, 2> widths { 128, 8 };
    const std::array<ValueKind, 2> kinds {
        ValueKind::logic9, ValueKind::logic9
    };
    auto options = LlvmJitOptions { optimization, { } };
    options.debug_instrumentation = false;
    LlvmJit jit { options };
    jit.add_process(symbol, process, widths, kinds);

    TestRuntime runtime;
    auto descriptor = abi(runtime);
    const auto run = [&](const std::string_view text) {
        const auto source = PackedLogic4::from_logic9_msb_string(text);
        assert(source.width() == 128);
        const auto assign_plane = [&](const std::size_t plane,
                                      std::vector<std::uint64_t>& destination) {
            const auto words = source.logic9_plane_words(plane);
            destination.assign(words.begin(), words.end());
        };
        assign_plane(0, runtime.wide_signal_aval[0]);
        assign_plane(1, runtime.wide_signal_bval[0]);
        assign_plane(2, runtime.wide_signal_logic9_plane2[0]);
        assign_plane(3, runtime.wide_signal_logic9_plane3[0]);
        assert(jit.execute(jit.lookup(symbol), descriptor)
            == JitExecutionStatus::completed);
        assert(runtime.logic9_signals[1]
            == source.extract_bits(64, 8).logic9_low_word().planes);
    };
    run("01UXZWLH-10HLWZX01UXZWLH-10HLWZX01UXZWLH-10HLWZX01UXZWLH-10HLWZX"
        "01UXZWLH-10HLWZX01UXZWLH-10HLWZX01UXZWLH-10HLWZX01UXZWLH-10HLWZX");
    run("HLWZX-1001UXZWLHHLWZX-1001UXZWLHHLWZX-1001UXZWLHHLWZX-1001UXZWLH"
        "HLWZX-1001UXZWLHHLWZX-1001UXZWLHHLWZX-1001UXZWLHHLWZX-1001UXZWLH");
    assert(runtime.dynamic_part_signal_reads == 2U);
    assert(runtime.packed_signal_reads == 0U);
}

void test_wide_single_bit_dynamic_part_select_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    constexpr std::uint32_t source_width = 130U;
    struct SelectionCase {
        PackedLogic4 base;
        std::int64_t left { };
        std::int64_t right { };
        std::uint32_t base_offset { };
        bool increasing { };
        bool source_descending { };
        bool two_state { };
    };
    const auto known_base = [](const std::int32_t value) {
        return PackedLogic4::from_aval_bval(
            32U, static_cast<std::uint32_t>(value), 0U);
    };
    const auto unknown_base = PackedLogic4::from_aval_bval(
        32U, UINT32_C(0x00000040), UINT32_C(0x00000001));

    auto logic4_source = PackedLogic4(source_width, Logic4::zero);
    constexpr std::array logic4_pattern {
        Logic4::zero, Logic4::one, Logic4::x, Logic4::z
    };
    for (std::uint32_t bit = 0U; bit < source_width; ++bit) {
        logic4_source.set(bit, logic4_pattern[bit % logic4_pattern.size()]);
    }
    const std::vector<SelectionCase> logic4_cases {
        { known_base(63), 129, 0, 0, true, true, false },
        { known_base(64), 129, 0, 0, false, true, false },
        { known_base(127), 129, 0, 0, true, false, false },
        { known_base(128), 129, 0, 0, false, false, false },
        { known_base(129), 129, 0, 0, true, true, false },
        { known_base(65), 0, 129, 0, true, false, false },
        { known_base(1), 2, 0, 63, false, true, false },
        { known_base(-1), 0, -128, 1, false, false, false },
        { unknown_base, 129, 0, 0, true, true, false },
        { known_base(-1), 129, 0, 0, true, true, false },
        { known_base(130), 129, 0, 0, false, false, false },
        { known_base(130), 129, 0, 0, true, true, true },
    };

    constexpr std::string_view logic9_pattern { "01UXZWLH-" };
    std::string logic9_text;
    logic9_text.reserve(source_width);
    for (std::size_t bit = source_width; bit > 0U; --bit) {
        logic9_text.push_back(
            logic9_pattern[(bit - 1U) % logic9_pattern.size()]);
    }
    const auto logic9_source =
        PackedLogic4::from_logic9_msb_string(logic9_text);
    std::vector<SelectionCase> logic9_cases;
    logic9_cases.reserve(20U);
    for (std::int32_t bit = 0; bit < 9; ++bit) {
        logic9_cases.push_back(
            { known_base(bit), 129, 0, 0, true, true, false });
    }
    for (const auto bit : { 63, 64, 127, 128, 129 }) {
        logic9_cases.push_back(
            { known_base(bit), 129, 0, 0, false, false, false });
    }
    logic9_cases.push_back(
        { known_base(65), 0, 129, 0, true, false, false });
    logic9_cases.push_back(
        { known_base(1), 2, 0, 63, false, true, false });
    logic9_cases.push_back(
        { known_base(-1), 0, -128, 1, false, false, false });
    logic9_cases.push_back(
        { known_base(-1), 129, 0, 0, true, true, false });
    logic9_cases.push_back(
        { known_base(130), 129, 0, 0, false, false, false });
    logic9_cases.push_back(
        { unknown_base, 129, 0, 0, true, true, false });

    const auto run = [&](const std::string_view suffix,
                         const ValueKind kind,
                         const PackedLogic4& source,
                         const std::vector<SelectionCase>& cases) {
        constexpr std::size_t signal_capacity = 16U;
        for (std::size_t first = 0; first < cases.size();
            first += signal_capacity) {
            const auto last = std::min(first + signal_capacity, cases.size());
            Process process;
            process.id = kind == ValueKind::logic9 ? 141U : 140U;
            process.name = "wide_single_bit_dynamic_part_select";
            process.register_count = 3U;
            process.register_value_kinds = { kind, ValueKind::logic4, kind };
            process.operations.emplace_back(LoadConstant { 0, source });
            std::vector<PackedLogic4> expected;
            expected.reserve(last - first);
            for (std::size_t index = first; index < last; ++index) {
                const auto& selection = cases[index];
                process.operations.emplace_back(
                    LoadConstant { 1, selection.base });
                process.operations.emplace_back(DynamicPartSelect {
                    2,
                    0,
                    1,
                    selection.left,
                    selection.right,
                    1,
                    selection.increasing,
                    selection.source_descending,
                    selection.two_state,
                    selection.base_offset });
                process.operations.emplace_back(WriteBlocking {
                    static_cast<std::uint32_t>(index - first), 2 });
                expected.push_back(runtime::simir::dynamic_part_select_value(
                    source,
                    selection.base,
                    selection.left,
                    selection.right,
                    selection.base_offset,
                    1U,
                    selection.increasing,
                    selection.source_descending,
                    selection.two_state));
            }
            process.operations.emplace_back(Halt { });

            const std::vector<std::uint32_t> signal_widths(last - first, 1U);
            const std::vector<ValueKind> signal_kinds(last - first, kind);
            auto options = LlvmJitOptions { optimization, { } };
            options.debug_instrumentation = false;
            LlvmJit jit { options };
            auto process_symbol = std::string { symbol };
            process_symbol.append(suffix);
            process_symbol.append(std::to_string(first));
            jit.add_process(
                process_symbol, process, signal_widths, signal_kinds);

            TestRuntime runtime;
            auto descriptor = abi(runtime);
            assert(jit.execute(jit.lookup(process_symbol), descriptor)
                == JitExecutionStatus::completed);
            for (std::size_t index = 0; index < expected.size(); ++index) {
                if (kind == ValueKind::logic9) {
                    assert(runtime.logic9_signals[index]
                        == expected[index].logic9_low_word().planes);
                } else {
                    const auto value = expected[index].low_word();
                    const EncodedSignal expected_signal {
                        value.aval, value.bval
                    };
                    assert(runtime.signals[index] == expected_signal);
                }
            }
        }
    };

    run("_logic4", ValueKind::logic4, logic4_source, logic4_cases);
    run("_logic9", ValueKind::logic9, logic9_source, logic9_cases);
}

void test_wide_dynamic_part_select_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    struct SelectionCase {
        PackedLogic4 base;
        std::int64_t left { };
        std::int64_t right { };
        std::uint32_t width { };
        bool increasing { };
        bool source_descending { };
        bool two_state { };
        std::uint32_t base_offset { };
    };
    const auto known_base = [](const std::int32_t value) {
        return PackedLogic4::from_aval_bval(
            32U, static_cast<std::uint32_t>(value), 0U);
    };
    const auto unknown_base = PackedLogic4::from_aval_bval(
        32U, UINT32_C(0x00000040), UINT32_C(0x00000001));
    const auto z_base = PackedLogic4::from_aval_bval(
        32U, 0U, UINT32_C(0x00000001));
    const std::vector<SelectionCase> aligned_logic4_cases {
        { known_base(60), 8191, 0, 8, true, true, false, 0 },
        { known_base(8125), 0, 8191, 8, true, true, false, 0 },
        { known_base(3), 8191, 0, 8, false, false, false, 0 },
        { known_base(124), 127, 0, 8, true, true, false, 8064 },
        { unknown_base, 8191, 0, 8, true, true, false, 0 },
        { z_base, 8191, 0, 8, true, true, false, 0 },
        { known_base(-1), 8191, 0, 8, true, true, false, 0 },
        { known_base(8192), 8191, 0, 8, true, true, true, 0 },
    };
    const std::vector<SelectionCase> aligned_logic9_cases {
        { known_base(60), 8191, 0, 8, true, true, false, 0 },
        { known_base(8125), 0, 8191, 8, true, true, false, 0 },
        { known_base(3), 8191, 0, 8, false, false, false, 0 },
        { known_base(124), 127, 0, 8, true, true, false, 8064 },
        { unknown_base, 8191, 0, 8, true, true, false, 0 },
        { z_base, 8191, 0, 8, true, true, false, 0 },
        { known_base(-1), 8191, 0, 8, true, true, false, 0 },
        { unknown_base, 8191, 0, 8, true, true, true, 0 },
        { known_base(8192), 8191, 0, 8, true, true, true, 0 },
    };
    const std::vector<SelectionCase> odd_65_logic4_cases {
        { known_base(60), 64, 0, 8, true, true, false, 0 },
        { known_base(64), 64, 0, 8, true, true, false, 0 },
        { unknown_base, 64, 0, 8, true, true, false, 0 },
    };
    const std::vector<SelectionCase> odd_65_logic9_cases {
        { known_base(60), 64, 0, 8, true, true, true, 0 },
        { unknown_base, 64, 0, 8, true, true, true, 0 },
    };
    const std::vector<SelectionCase> odd_logic4_cases {
        { known_base(60), 126, 0, 8, true, true, false, 0 },
        { known_base(123), 126, 0, 8, true, true, false, 0 },
        { known_base(64), 0, 126, 8, true, true, false, 0 },
        { unknown_base, 126, 0, 8, true, true, false, 0 },
    };

    const auto make_source = [](const std::uint32_t width,
                                const ValueKind kind) {
        if (kind == ValueKind::logic9) {
            constexpr std::string_view pattern { "01UXZWLH-" };
            std::string text;
            text.reserve(width);
            for (std::size_t bit = width; bit > 0U; --bit) {
                text.push_back(pattern[(bit - 1U) % pattern.size()]);
            }
            return PackedLogic4::from_logic9_msb_string(text);
        }
        auto source = PackedLogic4(width, Logic4::zero);
        constexpr std::array pattern {
            Logic4::zero, Logic4::one, Logic4::x, Logic4::z
        };
        for (std::uint32_t bit = 0; bit < width; ++bit) {
            source.set(bit, pattern[bit % pattern.size()]);
        }
        return source;
    };
    const auto run = [&](const std::string_view suffix,
                         const std::uint32_t source_width,
                         const ValueKind kind,
                         const std::vector<SelectionCase>& cases,
                         const bool debug_instrumentation) {
        Process process;
        process.id = kind == ValueKind::logic9 ? 145U : 144U;
        process.name = "wide_dynamic_part_select";
        process.register_count = 3U;
        process.register_value_kinds = {
            kind, ValueKind::logic4, kind
        };
        process.operations.emplace_back(ReadSignal { 0, 0 });
        std::vector<std::uint32_t> signal_widths { source_width };
        std::vector<ValueKind> signal_kinds { kind };
        std::vector<PackedLogic4> expected;
        expected.reserve(cases.size());
        const auto source = make_source(source_width, kind);
        for (std::size_t index = 0; index < cases.size(); ++index) {
            const auto& selection = cases[index];
            process.operations.emplace_back(
                LoadConstant { 1, selection.base });
            process.operations.emplace_back(DynamicPartSelect {
                2,
                0,
                1,
                selection.left,
                selection.right,
                selection.width,
                selection.increasing,
                selection.source_descending,
                selection.two_state,
                selection.base_offset });
            process.operations.emplace_back(WriteBlocking {
                static_cast<std::uint32_t>(index + 1U), 2 });
            signal_widths.push_back(selection.width);
            signal_kinds.push_back(kind);
            expected.push_back(runtime::simir::dynamic_part_select_value(
                source,
                selection.base,
                selection.left,
                selection.right,
                selection.base_offset,
                selection.width,
                selection.increasing,
                selection.source_descending,
                selection.two_state));
        }
        process.operations.emplace_back(Halt { });

        auto source_symbol = std::string { symbol };
        source_symbol.append(suffix);
        auto options = LlvmJitOptions { optimization, { } };
        options.debug_instrumentation = debug_instrumentation;
        LlvmJit jit { options };
        jit.add_process(source_symbol, process, signal_widths, signal_kinds);

        TestRuntime runtime;
        const auto assign_plane = [&](const std::size_t plane,
                                      std::vector<std::uint64_t>& destination) {
            const auto words = kind == ValueKind::logic9
                ? source.logic9_plane_words(plane)
                : (plane == 0U ? source.aval_words() : source.bval_words());
            destination.assign(words.begin(), words.end());
        };
        assign_plane(0U, runtime.wide_signal_aval[0]);
        assign_plane(1U, runtime.wide_signal_bval[0]);
        if (kind == ValueKind::logic9) {
            assign_plane(2U, runtime.wide_signal_logic9_plane2[0]);
            assign_plane(3U, runtime.wide_signal_logic9_plane3[0]);
        }
        auto descriptor = abi(runtime);
        assert(jit.execute(jit.lookup(source_symbol), descriptor)
            == JitExecutionStatus::completed);
        assert(runtime.packed_signal_reads == 1U);
        for (std::size_t index = 0; index < expected.size(); ++index) {
            if (kind == ValueKind::logic9) {
                assert(runtime.logic9_signals[index + 1U]
                    == expected[index].logic9_low_word().planes);
            } else {
                const auto value = expected[index].low_word();
                assert((runtime.signals[index + 1U]
                    == EncodedSignal { value.aval, value.bval }));
            }
        }
    };

    run(
        "_8192_logic4",
        8192U,
        ValueKind::logic4,
        aligned_logic4_cases,
        false);
    run(
        "_8192_logic9",
        8192U,
        ValueKind::logic9,
        aligned_logic9_cases,
        false);
    run(
        "_8192_logic4_debug",
        8192U,
        ValueKind::logic4,
        aligned_logic4_cases,
        true);
    run(
        "_65_logic4_fallback",
        65U,
        ValueKind::logic4,
        odd_65_logic4_cases,
        false);
    run(
        "_65_logic9_fallback",
        65U,
        ValueKind::logic9,
        odd_65_logic9_cases,
        false);
    run(
        "_127_logic4_fallback",
        127U,
        ValueKind::logic4,
        odd_logic4_cases,
        false);
}

void test_dynamic_part_interval_operations_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    constexpr std::uint32_t source_width = 257U;
    const auto reference_insert = [](
        PackedLogic4 target,
        const PackedLogic4& insertion,
        const PackedLogic4& base,
        const DynamicPartIndex& selection) {
        const auto write = runtime::simir::dynamic_part_write_value(
            insertion, base, selection);
        if (write) {
            target.insert_bits(write->value, write->offset);
        }
        return target;
    };
    struct SelectionCase {
        DynamicPartIndex selection;
        bool two_state { };
        RegisterId selected_register { };
        RegisterId inserted_register { };
        RegisterId insertion_source { };
    };
    const std::array selections {
        SelectionCase {
            DynamicPartIndex { 1, 256, 0, 0, 8, true, true },
            false, 6U, 7U, 2U
        },
        SelectionCase {
            DynamicPartIndex { 1, 256, 1, 1, 65, true, true },
            true, 8U, 9U, 3U
        },
        SelectionCase {
            DynamicPartIndex { 1, 0, 256, 0, 129, true, false },
            false, 10U, 11U, 4U
        },
        SelectionCase {
            DynamicPartIndex { 1, 0, -256, 0, 65, true, true },
            false, 12U, 13U, 3U
        },
        SelectionCase {
            DynamicPartIndex { 1, 256, 0, 0, 129, true, true },
            false, 14U, 15U, 4U
        }
    };
    const auto make_value = [](
                                const std::uint32_t width,
                                const ValueKind kind,
                                const std::size_t rotation) {
        if (kind == ValueKind::logic9) {
            constexpr std::string_view pattern { "01UXZWLH-" };
            std::string text(width, '0');
            for (std::size_t bit = 0; bit < width; ++bit) {
                text[width - bit - 1U]
                    = pattern[(bit + rotation) % pattern.size()];
            }
            return PackedLogic4::from_logic9_msb_string(text);
        }
        constexpr std::array logic4_pattern {
            Logic4::zero, Logic4::one, Logic4::x, Logic4::z
        };
        auto value = PackedLogic4(width, Logic4::zero);
        for (std::uint32_t bit = 0; bit < width; ++bit) {
            value.set(
                bit,
                logic4_pattern[(bit + rotation) % logic4_pattern.size()]);
        }
        return value;
    };
    const auto make_base = [](const std::int32_t value) {
        return PackedLogic4::from_aval_bval(
            32U, static_cast<std::uint32_t>(value), 0U);
    };
    const std::array bases {
        make_base(0),
        make_base(64),
        make_base(256),
        make_base(-1),
        make_base(-256),
        PackedLogic4::from_aval_bval(32U, 1U, 1U),
        PackedLogic4::from_aval_bval(32U, 0U, 1U)
    };

    const auto run = [&](const ValueKind kind,
                         const std::string_view suffix) {
        Process process;
        process.id = kind == ValueKind::logic9 ? 173U : 172U;
        process.name = "dynamic_part_interval_operations";
        process.register_count = 16U;
        process.register_value_kinds.assign(16U, kind);
        process.register_value_kinds[1] = ValueKind::logic4;
        process.operations = {
            ReadSignal { 0U, 0U },
            ReadSignal { 1U, 1U },
            ReadSignal { 2U, 2U },
            ReadSignal { 3U, 3U },
            ReadSignal { 4U, 4U },
            CopyRegister { 5U, 0U }
        };
        for (const auto& selection : selections) {
            const auto& index = selection.selection;
            process.operations.emplace_back(DynamicPartSelect {
                selection.selected_register,
                5U,
                index.base,
                index.left,
                index.right,
                index.width,
                index.increasing,
                index.source_descending,
                selection.two_state,
                index.base_offset });
            process.operations.emplace_back(DynamicPartInsert {
                selection.inserted_register,
                5U,
                selection.insertion_source,
                index });
        }
        process.operations.emplace_back(Pause { });
        process.operations.emplace_back(Stop { });
        const std::array<std::uint32_t, 10> output_widths {
            8U, source_width, 65U, source_width, 129U,
            source_width, 65U, source_width, 129U, source_width
        };
        for (std::size_t index = 0; index < output_widths.size(); ++index) {
            DebugLocal local;
            local.name = "dynamic_part_interval_result_"
                + std::to_string(index);
            local.type_name = "logic";
            local.register_id = static_cast<RegisterId>(index + 6U);
            local.width = output_widths[index];
            local.value_kind = kind;
            process.debug_locals.push_back(std::move(local));
        }

        const std::array<std::uint32_t, 5> signal_widths {
            source_width, 32U, 8U, 65U, 129U
        };
        const std::array<ValueKind, 5> signal_kinds {
            kind, ValueKind::logic4, kind, kind, kind
        };
        auto options = LlvmJitOptions { optimization, { } };
        options.debug_instrumentation = false;
        LlvmJit jit { options };
        auto process_symbol = std::string { symbol };
        process_symbol.append(suffix);
        jit.add_process(
            process_symbol, process, signal_widths, signal_kinds);
        const auto handle = jit.lookup(process_symbol);
        const auto layout = jit.frame_layout(handle);
        for (std::uint32_t id = 6U; id < 16U; ++id) {
            assert(layout.register_values_persistent[id] != 0U);
        }
        std::vector<std::uint64_t> aval(layout.register_word_count);
        std::vector<std::uint64_t> bval(layout.register_word_count);
        std::vector<std::uint8_t> initialized(layout.register_count);
        std::vector<std::uint64_t> plane2(layout.register_word_count);
        std::vector<std::uint64_t> plane3(layout.register_word_count);

        const auto assign_input = [](
                                     TestRuntime& runtime,
                                     const std::uint32_t signal,
                                     const PackedLogic4& value) {
            if (value.width() <= 64U) {
                if (value.is_logic9()) {
                    runtime.logic9_signals[signal]
                        = value.logic9_low_word().planes;
                } else {
                    runtime.signals[signal] = encode(value);
                }
                return;
            }
            const auto copy_plane = [&value](
                                        const std::size_t plane,
                                        std::vector<std::uint64_t>& output) {
                const auto values = value.is_logic9()
                    ? value.logic9_plane_words(plane)
                    : (plane == 0U ? value.aval_words()
                                   : value.bval_words());
                output.assign(values.begin(), values.end());
            };
            copy_plane(0U, runtime.wide_signal_aval[signal]);
            copy_plane(1U, runtime.wide_signal_bval[signal]);
            if (value.is_logic9()) {
                copy_plane(2U, runtime.wide_signal_logic9_plane2[signal]);
                copy_plane(3U, runtime.wide_signal_logic9_plane3[signal]);
            }
        };
        const auto read_register = [&] (
                                       const std::uint32_t id,
                                       const std::vector<std::uint64_t>& values2,
                                       const std::vector<std::uint64_t>& values3) {
            const auto width = layout.register_widths[id];
            const auto offset = layout.register_word_offsets[id];
            const auto words = static_cast<std::size_t>((width + 63U) / 64U);
            const auto value_aval = std::span { aval }.subspan(offset, words);
            const auto value_bval = std::span { bval }.subspan(offset, words);
            if (kind == ValueKind::logic9) {
                return PackedLogic4::from_logic9_word_planes(
                    width,
                    value_aval,
                    value_bval,
                    std::span { values2 }.subspan(offset, words),
                    std::span { values3 }.subspan(offset, words));
            }
            return PackedLogic4::from_word_planes(
                width, value_aval, value_bval);
        };

        for (std::size_t iteration = 0; iteration < bases.size(); ++iteration) {
            const auto source = make_value(source_width, kind, iteration);
            const auto insert8 = make_value(8U, kind, iteration + 1U);
            const auto insert65 = make_value(65U, kind, iteration + 2U);
            const auto insert129 = make_value(129U, kind, iteration + 3U);
            TestRuntime runtime;
            assign_input(runtime, 0U, source);
            runtime.signals[1] = encode(bases[iteration]);
            assign_input(runtime, 2U, insert8);
            assign_input(runtime, 3U, insert65);
            assign_input(runtime, 4U, insert129);
            auto descriptor = abi(runtime);
            fsim_jit_frame_v2 frame { };
            if (kind == ValueKind::logic9) {
                jit.initialize_frame(
                    handle, frame, aval, bval, initialized, plane2, plane3);
            } else {
                jit.initialize_frame(handle, frame, aval, bval, initialized);
            }
            auto result = new_resume_result();
            assert(jit.resume(handle, descriptor, frame, result)
                == JitResumeStatus::paused);

            const std::array insert_sources {
                insert8, insert65, insert129
            };
            for (const auto& selection : selections) {
                const auto& index = selection.selection;
                const auto expected_select
                    = runtime::simir::dynamic_part_select_value(
                        source,
                        bases[iteration],
                        index.left,
                        index.right,
                        index.base_offset,
                        index.width,
                        index.increasing,
                        index.source_descending,
                        selection.two_state);
                const auto source_index =
                    selection.insertion_source == 2U ? 0U
                    : selection.insertion_source == 3U ? 1U : 2U;
                const auto expected_insert
                    = reference_insert(
                        source,
                        insert_sources[source_index],
                        bases[iteration],
                        index);
                assert(read_register(
                           selection.selected_register, plane2, plane3)
                    == expected_select);
                assert(read_register(
                           selection.inserted_register, plane2, plane3)
                    == expected_insert);
            }
        }
    };

    run(ValueKind::logic4, "_logic4");
    run(ValueKind::logic9, "_logic9");

    const auto run_word_edges = [&](const ValueKind kind,
                                    const std::string_view suffix) {
        constexpr std::array<std::uint32_t, 3> widths {
            1U, 256U, 1024U
        };
        constexpr RegisterId base_register = 3U;
        Process process;
        process.id = kind == ValueKind::logic9 ? 175U : 174U;
        process.name = "dynamic_part_interval_word_edges";
        process.register_count = 16U;
        process.register_value_kinds.assign(16U, kind);
        process.register_value_kinds[base_register] = ValueKind::logic4;
        process.operations = {
            ReadSignal { 0U, 0U },
            ReadSignal { 1U, 1U },
            ReadSignal { 2U, 2U },
            ReadSignal { base_register, 3U },
            ReadSignal { 4U, 4U },
            ReadSignal { 5U, 5U },
            ReadSignal { 6U, 6U }
        };
        for (std::size_t index = 0; index < widths.size(); ++index) {
            const auto width = widths[index];
            const auto source_register = static_cast<RegisterId>(7U + index);
            const auto select_register = static_cast<RegisterId>(10U + index);
            const auto insert_register = static_cast<RegisterId>(13U + index);
            const DynamicPartIndex selection {
                base_register,
                static_cast<std::int64_t>(width - 1U),
                0,
                0U,
                width,
                true,
                true
            };
            process.operations.emplace_back(
                CopyRegister { source_register, static_cast<RegisterId>(index) });
            process.operations.emplace_back(DynamicPartSelect {
                select_register,
                source_register,
                base_register,
                selection.left,
                selection.right,
                selection.width,
                selection.increasing,
                selection.source_descending,
                false,
                selection.base_offset });
            process.operations.emplace_back(DynamicPartInsert {
                insert_register,
                source_register,
                static_cast<RegisterId>(4U + index),
                selection });
        }
        process.operations.emplace_back(Pause { });
        process.operations.emplace_back(Stop { });
        for (std::size_t index = 0; index < widths.size(); ++index) {
            for (const auto reg : {
                     static_cast<RegisterId>(10U + index),
                     static_cast<RegisterId>(13U + index)}) {
                DebugLocal local;
                local.name = "dynamic_part_word_edge_" + std::to_string(reg);
                local.type_name = "logic";
                local.register_id = reg;
                local.width = widths[index];
                local.value_kind = kind;
                process.debug_locals.push_back(std::move(local));
            }
        }

        std::array<std::uint32_t, 7> signal_widths {
            widths[0], widths[1], widths[2], 32U,
            widths[0], widths[1], widths[2]
        };
        std::array<ValueKind, 7> signal_kinds {
            kind, kind, kind, ValueKind::logic4, kind, kind, kind
        };
        auto options = LlvmJitOptions { optimization, { } };
        options.debug_instrumentation = false;
        LlvmJit jit { options };
        auto process_symbol = std::string { symbol };
        process_symbol.append(suffix);
        jit.add_process(
            process_symbol, process, signal_widths, signal_kinds);
        const auto handle = jit.lookup(process_symbol);
        const auto layout = jit.frame_layout(handle);
        for (const auto reg : { 10U, 11U, 12U, 13U, 14U, 15U }) {
            assert(layout.register_values_persistent[reg] != 0U);
        }
        std::vector<std::uint64_t> aval(layout.register_word_count);
        std::vector<std::uint64_t> bval(layout.register_word_count);
        std::vector<std::uint8_t> initialized(layout.register_count);
        std::vector<std::uint64_t> plane2(layout.register_word_count);
        std::vector<std::uint64_t> plane3(layout.register_word_count);
        const auto assign_input = [&](TestRuntime& runtime,
                                      const std::uint32_t signal,
                                      const PackedLogic4& value) {
            if (value.width() <= 64U) {
                if (value.is_logic9()) {
                    runtime.logic9_signals[signal]
                        = value.logic9_low_word().planes;
                } else {
                    runtime.signals[signal] = encode(value);
                }
                return;
            }
            for (std::size_t plane = 0; plane < 4U; ++plane) {
                auto& output = plane == 0U ? runtime.wide_signal_aval[signal]
                    : plane == 1U ? runtime.wide_signal_bval[signal]
                    : plane == 2U ? runtime.wide_signal_logic9_plane2[signal]
                                  : runtime.wide_signal_logic9_plane3[signal];
                if (kind == ValueKind::logic9) {
                    const auto words = value.logic9_plane_words(plane);
                    output.assign(words.begin(), words.end());
                } else if (plane < 2U) {
                    const auto words = plane == 0U
                        ? value.aval_words() : value.bval_words();
                    output.assign(words.begin(), words.end());
                } else {
                    output.clear();
                }
            }
        };
        const auto read_register = [&](const std::uint32_t reg) {
            const auto width = layout.register_widths[reg];
            const auto offset = layout.register_word_offsets[reg];
            const auto words = static_cast<std::size_t>((width + 63U) / 64U);
            const auto word_aval = std::span { aval }.subspan(offset, words);
            const auto word_bval = std::span { bval }.subspan(offset, words);
            if (kind == ValueKind::logic9) {
                return PackedLogic4::from_logic9_word_planes(
                    width,
                    word_aval,
                    word_bval,
                    std::span { plane2 }.subspan(offset, words),
                    std::span { plane3 }.subspan(offset, words));
            }
            return PackedLogic4::from_word_planes(width, word_aval, word_bval);
        };
        const std::array<std::int32_t, 5> edge_bases {
            0, 1, 127, -1, std::numeric_limits<std::int32_t>::min()
        };
        for (std::size_t iteration = 0;
             iteration < edge_bases.size() + 1U;
             ++iteration) {
            const auto base = iteration == edge_bases.size()
                ? PackedLogic4::from_aval_bval(32U, 0U, 1U)
                : make_base(edge_bases[iteration]);
            TestRuntime runtime;
            for (std::size_t index = 0; index < widths.size(); ++index) {
                const auto source = make_value(
                    widths[index], kind, iteration + index);
                const auto insertion = make_value(
                    widths[index], kind, iteration + index + 3U);
                assign_input(runtime, static_cast<std::uint32_t>(index), source);
                assign_input(
                    runtime, static_cast<std::uint32_t>(4U + index), insertion);
            }
            runtime.signals[3U] = encode(base);
            auto descriptor = abi(runtime);
            fsim_jit_frame_v2 frame { };
            if (kind == ValueKind::logic9) {
                jit.initialize_frame(
                    handle, frame, aval, bval, initialized, plane2, plane3);
            } else {
                jit.initialize_frame(handle, frame, aval, bval, initialized);
            }
            auto result = new_resume_result();
            assert(jit.resume(handle, descriptor, frame, result)
                == JitResumeStatus::paused);
            for (std::size_t index = 0; index < widths.size(); ++index) {
                const auto source = make_value(
                    widths[index], kind, iteration + index);
                const auto insertion = make_value(
                    widths[index], kind, iteration + index + 3U);
                const auto selection = DynamicPartIndex {
                    base_register,
                    static_cast<std::int64_t>(widths[index] - 1U),
                    0,
                    0U,
                    widths[index],
                    true,
                    true
                };
                assert(read_register(static_cast<std::uint32_t>(10U + index))
                    == runtime::simir::dynamic_part_select_value(
                        source, base, selection.left, selection.right,
                        selection.base_offset, selection.width,
                        selection.increasing, selection.source_descending,
                        false));
                assert(read_register(static_cast<std::uint32_t>(13U + index))
                    == reference_insert(
                        source, insertion, base, selection));
            }
        }
    };

    run_word_edges(ValueKind::logic4, "_logic4_word_edges");
    run_word_edges(ValueKind::logic9, "_logic9_word_edges");
}

void test_scalar_truth_tables_and_64_bits() {
  LlvmJit jit;
  Process scalar;
  scalar.id = 0;
  scalar.name = "scalar";
  scalar.register_count = 9;
  scalar.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      Binary{BinaryOperator::bit_and, 2, 0, 1},
      WriteBlocking{2, 2},
      Binary{BinaryOperator::bit_or, 3, 0, 1},
      WriteBlocking{3, 3},
      Binary{BinaryOperator::bit_xor, 4, 0, 1},
      WriteBlocking{4, 4},
      Binary{BinaryOperator::add_unsigned, 5, 0, 1},
      WriteBlocking{5, 5},
      UnaryNot{6, 0},
      WriteBlocking{6, 6},
      Binary{BinaryOperator::equal, 7, 0, 1},
      WriteBlocking{7, 7},
      Binary{BinaryOperator::case_equal, 8, 0, 1},
      WriteBlocking{8, 8},
      Halt{},
  };
  const std::array<std::uint32_t, 9> scalar_widths{
      1, 1, 1, 1, 1, 1, 1, 1, 1};
  jit.add_process("scalar_truth_table", scalar, scalar_widths);
  const auto scalar_handle = jit.lookup("scalar_truth_table");

  constexpr std::array states{Logic4::zero, Logic4::one, Logic4::x,
                              Logic4::z};
  for (const auto lhs : states) {
    for (const auto rhs : states) {
      TestRuntime runtime;
      runtime.signals[0] = encode(lhs);
      runtime.signals[1] = encode(rhs);
      auto descriptor = abi(runtime);
      assert(jit.execute(scalar_handle, descriptor) ==
             JitExecutionStatus::completed);
      assert(runtime.signals[2] == encode(fsim::runtime::logic_and(lhs, rhs)));
      assert(runtime.signals[3] == encode(fsim::runtime::logic_or(lhs, rhs)));
      assert(runtime.signals[4] == encode(fsim::runtime::logic_xor(lhs, rhs)));
      // The carry is discarded for a one-bit unsigned addition, while any
      // arithmetic X/Z operand makes the complete result unknown.
      const auto known =
          [](const Logic4 value) {
            return value == Logic4::zero || value == Logic4::one;
          };
      assert(
          runtime.signals[5]
          == encode(
              known(lhs) && known(rhs)
                  ? fsim::runtime::logic_xor(lhs, rhs)
                  : Logic4::x));
      assert(runtime.signals[6] == encode(fsim::runtime::logic_not(lhs)));
      assert(runtime.signals[7] == encode(equality(lhs, rhs)));
      assert(
          runtime.signals[8]
          == encode(
              lhs == rhs ? Logic4::one : Logic4::zero));
    }
  }

  Process wide;
  wide.id = 1;
  wide.name = "wide";
  wide.register_count = 2;
  wide.operations = {
      ReadSignal{0, 0},
      UnaryNot{1, 0},
      WriteBlocking{1, 1},
      Halt{},
  };
  const std::array<std::uint32_t, 2> wide_widths{64, 64};
  jit.add_process("wide_not", wide, wide_widths);
  const auto wide_handle = jit.lookup("wide_not");
  TestRuntime runtime;
  runtime.signals[0] = {UINT64_C(0x0123456789abcdef),
                        UINT64_C(0x8000000000000000)};
  auto descriptor = abi(runtime);
  assert(jit.execute(wide_handle, descriptor) ==
         JitExecutionStatus::completed);
  assert((runtime.signals[1] ==
          EncodedSignal{UINT64_C(0xfedcba9876543210) |
                            UINT64_C(0x8000000000000000),
                        UINT64_C(0x8000000000000000)}));
}

void test_wildcard_case_matching_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{level, {}}};
  Process process;
  process.id = 0;
  process.name = std::string{symbol};
  process.register_count = 5;
  process.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      Binary{BinaryOperator::casez_equal, 2, 0, 1},
      WriteBlocking{2, 2},
      Binary{BinaryOperator::casex_equal, 3, 0, 1},
      WriteBlocking{3, 3},
      Binary{BinaryOperator::wildcard_equal, 4, 0, 1},
      WriteBlocking{4, 4},
      Halt{},
  };
  const std::array<std::uint32_t, 5> widths{1, 1, 1, 1, 1};
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  constexpr std::array states{
      Logic4::zero, Logic4::one, Logic4::x, Logic4::z};
  for (const auto lhs : states) {
    for (const auto rhs : states) {
      TestRuntime runtime;
      runtime.signals[0] = encode(lhs);
      runtime.signals[1] = encode(rhs);
      auto descriptor = abi(runtime);
      assert(
          jit.execute(handle, descriptor)
          == JitExecutionStatus::completed);
      const auto casez_match =
          lhs == Logic4::z || rhs == Logic4::z || lhs == rhs;
      const auto casex_match =
          lhs == Logic4::x || lhs == Logic4::z
          || rhs == Logic4::x || rhs == Logic4::z || lhs == rhs;
      const auto wildcard_match =
          rhs == Logic4::x || rhs == Logic4::z
              ? Logic4::one
              : lhs == Logic4::x || lhs == Logic4::z
                  ? Logic4::x
                  : lhs == rhs ? Logic4::one : Logic4::zero;
      assert(
          runtime.signals[2]
          == encode(casez_match ? Logic4::one : Logic4::zero));
      assert(
          runtime.signals[3]
          == encode(casex_match ? Logic4::one : Logic4::zero));
      assert(runtime.signals[4] == encode(wildcard_match));
    }
  }

  const auto vector_symbol = std::string{symbol} + "_vectors";
  Process vector;
  vector.id = 1;
  vector.name = vector_symbol;
  vector.register_count = 4;
  vector.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      Binary{BinaryOperator::equal, 2, 0, 1},
      WriteBlocking{2, 2},
      Binary{BinaryOperator::wildcard_equal, 3, 0, 1},
      WriteBlocking{3, 3},
      Halt{},
  };
  const std::array<std::uint32_t, 4> vector_widths{2, 2, 1, 1};
  jit.add_process(vector_symbol, vector, vector_widths);
  const auto vector_handle = jit.lookup(vector_symbol);
  for (const auto& [lhs, rhs, wildcard_expected] :
       std::array{
           std::tuple{
               std::string_view{"X0"},
               std::string_view{"X1"},
               Logic4::zero},
           std::tuple{
               std::string_view{"X0"},
               std::string_view{"01"},
               Logic4::x}}) {
    TestRuntime runtime;
    runtime.signals[0] = encode(PackedLogic4::from_msb_string(lhs));
    runtime.signals[1] = encode(PackedLogic4::from_msb_string(rhs));
    auto descriptor = abi(runtime);
    assert(
        jit.execute(vector_handle, descriptor)
        == JitExecutionStatus::completed);
    assert(runtime.signals[2] == encode(Logic4::x));
    assert(runtime.signals[3] == encode(wildcard_expected));
  }
}

void test_conditional_select_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{level, {}}};
  Process process;
  process.id = 0;
  process.name = std::string{symbol};
  process.register_count = 4;
  process.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      ReadSignal{2, 2},
      ConditionalSelect{3, 0, 1, 2},
      WriteBlocking{3, 3},
      Halt{},
  };
  const std::array<std::uint32_t, 4> widths{1, 4, 4, 4};
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  const auto when_true =
      PackedLogic4::from_msb_string("101Z").low_word();
  const auto when_false =
      PackedLogic4::from_msb_string("100Z").low_word();
  for (const auto& [condition, expected] :
       std::array{
           std::pair{Logic4::zero, std::string_view{"100Z"}},
           std::pair{Logic4::one, std::string_view{"101Z"}},
           std::pair{Logic4::x, std::string_view{"10XZ"}},
           std::pair{Logic4::z, std::string_view{"10XZ"}}}) {
    TestRuntime runtime;
    runtime.signals[0] = encode(condition);
    runtime.signals[1] = {when_true.aval, when_true.bval};
    runtime.signals[2] = {when_false.aval, when_false.bval};
    auto descriptor = abi(runtime);
    assert(
        jit.execute(handle, descriptor)
        == JitExecutionStatus::completed);
    const auto expected_word =
        PackedLogic4::from_msb_string(expected).low_word();
    assert((runtime.signals[3] == EncodedSignal{
        expected_word.aval, expected_word.bval}));
  }
}

void test_comparisons_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{level, {}}};
  Process process;
  process.id = 0;
  process.name = std::string{symbol};
  process.register_count = 9;
  process.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      Binary{BinaryOperator::not_equal, 2, 0, 1},
      WriteBlocking{2, 2},
      Binary{BinaryOperator::less_unsigned, 3, 0, 1},
      WriteBlocking{3, 3},
      Binary{BinaryOperator::less_equal_unsigned, 4, 0, 1},
      WriteBlocking{4, 4},
      Binary{BinaryOperator::greater_unsigned, 5, 0, 1},
      WriteBlocking{5, 5},
      Binary{BinaryOperator::greater_equal_unsigned, 6, 0, 1},
      WriteBlocking{6, 6},
      LogicalNot{7, 0},
      WriteBlocking{7, 7},
      Halt{},
  };
  const std::array<std::uint32_t, 8> widths{
      4, 4, 1, 1, 1, 1, 1, 1};
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  struct TestCase {
    std::string_view lhs;
    std::string_view rhs;
    std::array<Logic4, 6> expected;
  };
  const std::array cases{
      TestCase{
          "0010",
          "0011",
          {Logic4::one, Logic4::one, Logic4::one,
           Logic4::zero, Logic4::zero, Logic4::zero}},
      TestCase{
          "0000",
          "0000",
          {Logic4::zero, Logic4::zero, Logic4::one,
           Logic4::zero, Logic4::one, Logic4::one}},
      TestCase{
          "00X0",
          "0011",
          {Logic4::x, Logic4::x, Logic4::x,
           Logic4::x, Logic4::x, Logic4::x}},
      TestCase{
          "01Z0",
          "0011",
          {Logic4::x, Logic4::x, Logic4::x,
           Logic4::x, Logic4::x, Logic4::zero}},
  };
  for (const auto& test : cases) {
    TestRuntime runtime;
    const auto lhs =
        PackedLogic4::from_msb_string(test.lhs).low_word();
    const auto rhs =
        PackedLogic4::from_msb_string(test.rhs).low_word();
    runtime.signals[0] = {lhs.aval, lhs.bval};
    runtime.signals[1] = {rhs.aval, rhs.bval};
    auto descriptor = abi(runtime);
    assert(
        jit.execute(handle, descriptor)
        == JitExecutionStatus::completed);
    for (std::size_t index = 0; index < test.expected.size();
         ++index) {
      assert(
          runtime.signals[index + 2]
          == encode(test.expected[index]));
    }
  }
}

void test_logical_binary_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{level, {}}};
  Process process;
  process.id = 0;
  process.name = std::string{symbol};
  process.register_count = 4;
  process.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      LogicalBinary{
          LogicalBinaryOperator::logical_and, 2, 0, 1},
      WriteBlocking{2, 2},
      LogicalBinary{
          LogicalBinaryOperator::logical_or, 3, 0, 1},
      WriteBlocking{3, 3},
      Halt{},
  };
  const std::array<std::uint32_t, 4> widths{4, 2, 1, 1};
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  struct TestCase {
    std::string_view lhs;
    std::string_view rhs;
    Logic4 expected_and;
    Logic4 expected_or;
  };
  const std::array cases{
      TestCase{"0000", "X1", Logic4::zero, Logic4::one},
      TestCase{"00X0", "00", Logic4::zero, Logic4::x},
      TestCase{"00X0", "01", Logic4::x, Logic4::one},
      TestCase{"0010", "ZZ", Logic4::x, Logic4::one},
      TestCase{"0010", "01", Logic4::one, Logic4::one},
  };
  for (const auto& test : cases) {
    TestRuntime runtime;
    const auto lhs =
        PackedLogic4::from_msb_string(test.lhs).low_word();
    const auto rhs =
        PackedLogic4::from_msb_string(test.rhs).low_word();
    runtime.signals[0] = {lhs.aval, lhs.bval};
    runtime.signals[1] = {rhs.aval, rhs.bval};
    auto descriptor = abi(runtime);
    assert(
        jit.execute(handle, descriptor)
        == JitExecutionStatus::completed);
    assert(runtime.signals[2] == encode(test.expected_and));
    assert(runtime.signals[3] == encode(test.expected_or));
  }
}

void test_reduction_and_shift_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{level, {}}};
  Process process;
  process.id = 0;
  process.name = std::string{symbol};
  process.register_count = 11;
  process.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      Reduction{ReductionOperator::bit_and, 2, 0},
      WriteBlocking{2, 2},
      Reduction{ReductionOperator::bit_or, 3, 0},
      WriteBlocking{3, 3},
      Reduction{ReductionOperator::bit_xor, 4, 0},
      WriteBlocking{4, 4},
      Shift{ShiftOperator::logical_left, 5, 0, 1},
      WriteBlocking{5, 5},
      Shift{ShiftOperator::logical_right, 6, 0, 1},
      WriteBlocking{6, 6},
      Shift{ShiftOperator::arithmetic_right, 7, 0, 1},
      WriteBlocking{7, 7},
      Shift{ShiftOperator::arithmetic_left, 8, 0, 1},
      WriteBlocking{8, 8},
      Shift{ShiftOperator::rotate_left, 9, 0, 1},
      WriteBlocking{9, 9},
      Shift{ShiftOperator::rotate_right, 10, 0, 1},
      WriteBlocking{10, 10},
      Halt{},
  };
  const std::array<std::uint32_t, 11> widths{
      4, 3, 1, 1, 1, 4, 4, 4, 4, 4, 4};
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  struct TestCase {
    std::string_view value;
    std::string_view amount;
    std::string_view expected_and;
    std::string_view expected_or;
    std::string_view expected_xor;
    std::string_view expected_left;
    std::string_view expected_right;
    std::string_view expected_arithmetic_right;
    std::string_view expected_arithmetic_left;
    std::string_view expected_rotate_left;
    std::string_view expected_rotate_right;
  };
  const std::array cases{
      TestCase{
          "1111", "001", "1", "1", "0", "1110", "0111",
          "1111", "1111", "1111", "1111"},
      TestCase{
          "1011", "000", "0", "1", "1", "1011", "1011",
          "1011", "1011", "1011", "1011"},
      TestCase{
          "10X1", "001", "0", "1", "X", "0X10", "010X",
          "110X", "0X11", "0X11", "110X"},
      TestCase{
          "11X1", "011", "X", "1", "X", "1000", "0001",
          "1111", "1111", "111X", "1X11"},
      TestCase{
          "00X0", "0X1", "0", "X", "X", "XXXX", "XXXX",
          "XXXX", "XXXX", "XXXX", "XXXX"},
      TestCase{
          "Z001", "100", "0", "1", "X", "0000", "0000",
          "ZZZZ", "1111", "Z001", "Z001"},
      TestCase{
          "1001", "101", "0", "1", "0", "0000", "0000",
          "1111", "1111", "0011", "1100"},
  };
  for (const auto& test : cases) {
    TestRuntime runtime;
    const auto value =
        PackedLogic4::from_msb_string(test.value).low_word();
    const auto amount =
        PackedLogic4::from_msb_string(test.amount).low_word();
    runtime.signals[0] = {value.aval, value.bval};
    runtime.signals[1] = {amount.aval, amount.bval};
    auto descriptor = abi(runtime);
    assert(
        jit.execute(handle, descriptor)
        == JitExecutionStatus::completed);
    const std::array expected{
        test.expected_and,
        test.expected_or,
        test.expected_xor,
        test.expected_left,
        test.expected_right,
        test.expected_arithmetic_right,
        test.expected_arithmetic_left,
        test.expected_rotate_left,
        test.expected_rotate_right};
    for (std::size_t index = 0; index < expected.size();
         ++index) {
      const auto encoded =
          PackedLogic4::from_msb_string(expected[index]).low_word();
      assert((
          runtime.signals[index + 2]
          == EncodedSignal{encoded.aval, encoded.bval}));
    }
  }

  Process rotation_process;
  rotation_process.id = 1;
  rotation_process.name = std::string{symbol} + "_rotation_boundaries";
  rotation_process.register_count = 8;
  rotation_process.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      ReadSignal{4, 4},
      Shift{ShiftOperator::rotate_left, 2, 0, 1},
      WriteBlocking{2, 2},
      Shift{ShiftOperator::rotate_right, 3, 0, 1},
      WriteBlocking{3, 3},
      Shift{ShiftOperator::rotate_left, 6, 4, 1},
      WriteBlocking{4, 6},
      Shift{ShiftOperator::rotate_right, 7, 4, 1},
      WriteBlocking{5, 7},
      Halt{},
  };
  const std::array<std::uint32_t, 6> rotation_widths{
      3, 8, 3, 3, 1, 1};
  const auto rotation_symbol =
      std::string{symbol} + "_rotation_boundaries";
  LlvmJit rotation_jit{LlvmJitOptions{level, {}}};
  rotation_jit.add_process(
      rotation_symbol, rotation_process, rotation_widths);
  const auto rotation_handle = rotation_jit.lookup(rotation_symbol);

  struct RotationCase {
    std::string_view amount;
    std::string_view expected_left;
    std::string_view expected_right;
    std::string_view one_bit_value;
    std::string_view expected_one_bit;
  };
  const std::array rotation_cases{
      RotationCase{"00001010", "011", "110", "1", "1"},
      RotationCase{"00001000", "110", "011", "0", "0"},
      RotationCase{"11111111", "101", "101", "1", "1"},
      RotationCase{"0000000X", "XXX", "XXX", "1", "X"},
  };
  const auto encoded = [](const std::string_view digits) {
    const auto value = PackedLogic4::from_msb_string(digits).low_word();
    return EncodedSignal{value.aval, value.bval};
  };
  for (const auto& test : rotation_cases) {
    TestRuntime runtime;
    runtime.signals[0] = encoded("101");
    runtime.signals[1] = encoded(test.amount);
    runtime.signals[4] = encoded(test.one_bit_value);
    auto descriptor = abi(runtime);
    assert(
        rotation_jit.execute(rotation_handle, descriptor)
        == JitExecutionStatus::completed);
    assert(runtime.signals[2] == encoded(test.expected_left));
    assert(runtime.signals[3] == encoded(test.expected_right));
    assert(runtime.signals[4] == encoded(test.expected_one_bit));
    assert(runtime.signals[5] == encoded(test.expected_one_bit));
  }
}

void test_signed_shift_counts_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{level, {}}};
  Process process;
  process.id = 0;
  process.name = std::string{symbol};
  process.register_count = 8;
  process.operations = {
      ReadSignal{0, 0},
      ReadSignal{1, 1},
      Shift{
          ShiftOperator::logical_left, 2, 0, 1, true},
      WriteBlocking{2, 2},
      Shift{
          ShiftOperator::logical_right, 3, 0, 1, true},
      WriteBlocking{3, 3},
      Shift{
          ShiftOperator::arithmetic_left, 4, 0, 1, true},
      WriteBlocking{4, 4},
      Shift{
          ShiftOperator::arithmetic_right, 5, 0, 1, true},
      WriteBlocking{5, 5},
      Shift{
          ShiftOperator::rotate_left, 6, 0, 1, true},
      WriteBlocking{6, 6},
      Shift{
          ShiftOperator::rotate_right, 7, 0, 1, true},
      WriteBlocking{7, 7},
      Halt{},
  };
  const std::array<std::uint32_t, 8> widths{
      4, 4, 4, 4, 4, 4, 4, 4};
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  struct TestCase {
    std::string_view amount;
    std::array<std::string_view, 6> expected;
  };
  const std::array cases{
      TestCase{
          "0001",
          {"0X10", "010X", "0X11", "110X", "0X11", "110X"}},
      TestCase{
          "1111",
          {"010X", "0X10", "110X", "0X11", "110X", "0X11"}},
      TestCase{
          "1011",
          {"0000", "0000", "1111", "1111", "110X", "0X11"}},
      TestCase{
          "0X01",
          {"XXXX", "XXXX", "XXXX", "XXXX", "XXXX", "XXXX"}},
  };
  for (const auto& test : cases) {
    TestRuntime runtime;
    const auto value =
        PackedLogic4::from_msb_string("10X1").low_word();
    const auto amount =
        PackedLogic4::from_msb_string(test.amount).low_word();
    runtime.signals[0] = {value.aval, value.bval};
    runtime.signals[1] = {amount.aval, amount.bval};
    auto descriptor = abi(runtime);
    assert(
        jit.execute(handle, descriptor)
        == JitExecutionStatus::completed);
    for (std::size_t index = 0;
         index < test.expected.size(); ++index) {
      const auto encoded =
          PackedLogic4::from_msb_string(
              test.expected[index])
              .low_word();
      assert((
          runtime.signals[index + 2]
          == EncodedSignal{encoded.aval, encoded.bval}));
    }
  }
}

void test_dynamic_part_write_closed_form_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
  const auto repeated_value = [](
      const std::string_view pattern,
      const std::size_t width,
      const bool logic9,
      const std::size_t rotation) {
    std::string digits;
    digits.reserve(width);
    for (std::size_t bit = width; bit > 0U; --bit) {
      digits.push_back(
          pattern[((bit - 1U) + rotation) % pattern.size()]);
    }
    return logic9
        ? PackedLogic4::from_logic9_msb_string(digits)
        : PackedLogic4::from_msb_string(digits);
  };
  const auto base_value = [](const std::int32_t value) {
    return PackedLogic4::from_aval_bval(
        32U, static_cast<std::uint32_t>(value), 0U);
  };
  const auto unknown_base = PackedLogic4::from_aval_bval(32U, 1U, 1U);
  struct SliceShape {
    DynamicPartIndex selection;
    std::vector<std::int32_t> bases;
  };
  const std::array shapes {
      SliceShape {
          DynamicPartIndex { 1U, 0, 0, 0U, 1U, true, true },
          { -1, 0, 1 }
      },
      SliceShape {
          DynamicPartIndex { 1U, 7, 0, 3U, 8U, true, true },
          { -1, 0, 1, 7, 8, 11 }
      },
      // Fast ascending interval: source_descending=false and increasing=true.
      // This exercises selected_right = base + width - 1 and the ascending
      // selected-right subtraction branch.
      SliceShape {
          DynamicPartIndex { 1U, 0, 7, 2U, 8U, true, false },
          { -1, 0, 1, 7, 8 }
      },
      // The same fast ascending interval with increasing=false exercises
      // selected_right = base while retaining source_descending=false.
      SliceShape {
          DynamicPartIndex { 1U, 0, 7, 1U, 8U, false, false },
          { -1, 0, 1, 7, 8 }
      },
      SliceShape {
          DynamicPartIndex { 1U, 30, 0, 2U, 31U, true, true },
          { -15, -1, 0, 30, 31 }
      },
      SliceShape {
          DynamicPartIndex { 1U, 62, 0, 1U, 63U, true, true },
          { -1, 0, 62, 63, 64 }
      },
      SliceShape {
          DynamicPartIndex { 1U, 63, 0, 0U, 64U, true, true },
          { -40, -1, 0, 63, 64, 127 }
      },
      // Fast descending interval with increasing=false and
      // source_descending=true exercises selected_right = base - width + 1.
      SliceShape {
          DynamicPartIndex { 1U, 7, 0, 0U, 8U, false, true },
          { 0, 1, 7, 8, 15 }
      },
      SliceShape {
          DynamicPartIndex { 1U, 31, -32, 0U, 64U, true, true },
          { -40, -32, -1, 0, 31, 63 }
      },
      // This mismatched direction is not emitted by normal lowering. Reject
      // it at JIT admission before native and interpreter paths can diverge.
      SliceShape {
          DynamicPartIndex { 1U, 3, 0, 0U, 2U, true, false },
          { 3, 4 }
      },
      // Extreme declared bounds are deliberately rejected by the closed-form
      // guard and remain no-op cases in the existing fallback.
      SliceShape {
          DynamicPartIndex {
              1U,
              std::numeric_limits<std::int64_t>::max(),
              std::numeric_limits<std::int64_t>::max(),
              0U,
              1U,
              true,
              true
          },
          { 0 }
      },
      SliceShape {
          DynamicPartIndex {
              1U,
              std::numeric_limits<std::int64_t>::min(),
              std::numeric_limits<std::int64_t>::min(),
              0U,
              1U,
              true,
              true
          },
          { 0 }
      }
  };
  const auto initial_logic4 = repeated_value("01XZ", 64U, false, 0U);
  const auto initial_logic9 = repeated_value("UX01ZWLH-", 64U, true, 0U);
  LlvmJit jit { LlvmJitOptions { optimization, { } } };
  const auto run_slice_cases = [&](
      const std::string_view kind_suffix,
      const ValueKind kind,
      const PackedLogic4& initial_target) {
    for (std::size_t shape_index = 0U;
         shape_index < shapes.size();
         ++shape_index) {
      const auto& shape = shapes[shape_index];
      Process process;
      process.id = static_cast<std::uint32_t>(
          (kind == ValueKind::logic9 ? 240U : 220U) + shape_index);
      process.name = "dynamic_part_write_closed_form";
      process.register_count = 2U;
      process.register_value_kinds = { kind, ValueKind::logic4 };
      process.operations = {
          ReadSignal { 0U, 2U },
          ReadSignal { 1U, 1U },
          WriteBlockingDynamicPartSlice {
              0U, 0U, shape.selection },
          Halt { }
      };
      const std::array<std::uint32_t, 3> signal_widths {
          64U, 32U, shape.selection.width
      };
      const std::array<ValueKind, 3> signal_kinds {
          kind, ValueKind::logic4, kind
      };
      auto process_symbol = std::string { symbol };
      process_symbol.append("_");
      process_symbol.append(kind_suffix);
      process_symbol.append("_");
      process_symbol.append(std::to_string(shape_index));
      const auto bound_fits = [](const std::int64_t bound) {
        return bound >= std::numeric_limits<std::int32_t>::min()
            && bound <= std::numeric_limits<std::int32_t>::max();
      };
      const bool invalid_bounds = !bound_fits(shape.selection.left)
          || !bound_fits(shape.selection.right);
      const bool invalid_direction = shape.selection.source_descending
          != (shape.selection.left >= shape.selection.right);
      if (invalid_bounds || invalid_direction) {
        const auto expected_error = invalid_bounds
            ? std::string_view {
                "dynamic part-select write bounds must fit signed 32-bit integers"
              }
            : std::string_view {
                "dynamic part-select write direction conflicts with declared bounds"
              };
        bool rejected = false;
        try {
          jit.add_process(process_symbol, process,
                          signal_widths, signal_kinds);
        } catch (const LlvmJitError& error) {
          rejected = std::string_view { error.what() }.find(expected_error)
              != std::string_view::npos;
        }
        assert(rejected);
        continue;
      }
      jit.add_process(process_symbol, process, signal_widths, signal_kinds);
      const auto handle = jit.lookup(process_symbol);
      TestRuntime runtime;
      auto descriptor = abi(runtime);
      std::size_t source_rotation = 0U;
      const auto check_base = [&](const PackedLogic4& packed_base) {
        const auto pattern = kind == ValueKind::logic9
            ? std::string_view { "UX01ZWLH-" }
            : std::string_view { "01XZ" };
        const auto runtime_source = repeated_value(
            pattern, shape.selection.width,
            kind == ValueKind::logic9, source_rotation++);
        if (kind == ValueKind::logic9) {
          runtime.logic9_signals[0] = initial_target.logic9_low_word().planes;
          runtime.logic9_signals[2]
              = runtime_source.logic9_low_word().planes;
        } else {
          const auto initial_word = initial_target.low_word();
          runtime.signals[0] = {
              initial_word.aval, initial_word.bval
          };
          const auto source_word = runtime_source.low_word();
          runtime.signals[2] = { source_word.aval, source_word.bval };
        }
        const auto base_word = packed_base.low_word();
        runtime.signals[1] = { base_word.aval, base_word.bval };
        runtime.writes.clear();
        assert(jit.execute(handle, descriptor)
            == JitExecutionStatus::completed);

        auto expected = initial_target;
        const auto write = runtime::simir::dynamic_part_write_value(
            runtime_source, packed_base, shape.selection);
        if (write) {
          for (std::size_t bit = 0U; bit < write->value.width(); ++bit) {
            const auto target_bit = write->offset + bit;
            if (kind == ValueKind::logic9) {
              expected.set_logic9(
                  target_bit, write->value.get_logic9(bit));
            } else {
              expected.set(target_bit, write->value.get(bit));
            }
          }
        }
        if (kind == ValueKind::logic9) {
          assert(runtime.logic9_signals[0]
              == expected.logic9_low_word().planes);
        } else {
          const auto expected_word = expected.low_word();
          assert((runtime.signals[0]
              == EncodedSignal {
                  expected_word.aval, expected_word.bval }));
        }
        if (kind == ValueKind::logic4) {
          assert(runtime.writes.size() == (write ? 1U : 0U));
          if (write) {
            assert(runtime.writes.front().first == 0U);
          }
        }
      };
      for (const auto base : shape.bases) {
        check_base(base_value(base));
      }
      check_base(unknown_base);
    }
  };
  run_slice_cases("logic4", ValueKind::logic4, initial_logic4);
  run_slice_cases("logic9", ValueKind::logic9, initial_logic9);
}


} // namespace fsim::tests::compiler
