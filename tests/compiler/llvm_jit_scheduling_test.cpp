// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <iterator>

namespace fsim::tests::compiler {

namespace llvm_jit_test_detail {

extern "C" std::uint32_t bound_literal_operation(
    void* opaque,
    const std::uint32_t process,
    const std::uint32_t instruction,
    std::uint64_t,
    std::uint64_t,
    std::uint64_t,
    std::uint64_t,
    std::uint64_t* result_aval,
    std::uint64_t* result_bval)
{
  if (opaque == nullptr || result_aval == nullptr || result_bval == nullptr) {
    return 1U;
  }
  auto& runtime = *static_cast<TestRuntime*>(opaque);
  *result_aval = runtime.bound_literal_value.aval;
  *result_bval = runtime.bound_literal_value.bval;
  runtime.bound_literal_process = process;
  runtime.bound_literal_instruction = instruction;
  ++runtime.bound_literal_calls;
  return 0U;
}

} // namespace llvm_jit_test_detail

void run_at_level(const JitOptimizationLevel optimization,
                  const std::string_view symbol) {
  LlvmJit jit{LlvmJitOptions{optimization, {}}};
  const std::array<std::uint32_t, 8> widths{8, 8, 8, 8, 8, 8, 8, 1};
  const auto process = make_arithmetic_process();
  jit.add_process(symbol, process, widths);

  const auto handle = jit.lookup(symbol);
  assert(handle);
  assert(jit.lookup(symbol) == handle);
  expect_fatal_error(
      [&] { jit.add_process(symbol, process, widths); },
      "duplicate LLVM process");

  TestRuntime runtime;
  runtime.signals[0] = {0x35, 0};
  runtime.signals[1] = {0x0f, 0};
  auto descriptor = abi(runtime);
  assert(jit.execute(handle, descriptor) == JitExecutionStatus::completed);
  assert(runtime.assertion_count == 0);
  assert((runtime.signals[2] == EncodedSignal{0x05, 0}));
  assert((runtime.signals[3] == EncodedSignal{0x3f, 0}));
  assert((runtime.signals[4] == EncodedSignal{0x3a, 0}));
  assert((runtime.signals[5] == EncodedSignal{0x44, 0}));
  assert((runtime.signals[6] == EncodedSignal{0xca, 0}));
  assert((runtime.signals[7] == EncodedSignal{0, 0}));

  // Bit zero of input zero is X. This checks four-state propagation in every
  // emitted operator and takes the generated assertion failure edge.
  runtime.signals[0] = {0x35, 0x01};
  runtime.assertion_count = 0;
  runtime.assertion_message.clear();
  assert(jit.execute(handle, descriptor) ==
         JitExecutionStatus::assertion_failed);
  assert(runtime.assertion_count == 1);
  assert(runtime.failed_process == process.id);
  assert(runtime.failed_instruction == 16);
  assert(runtime.assertion_message == "unexpected sum");
  assert((runtime.signals[2] == EncodedSignal{0x05, 0x01}));
  assert((runtime.signals[3] == EncodedSignal{0x3f, 0}));
  assert((runtime.signals[4] == EncodedSignal{0x3b, 0x01}));
  assert((runtime.signals[5] == EncodedSignal{0xff, 0xff}));
  assert((runtime.signals[6] == EncodedSignal{0xcb, 0x01}));
  assert((runtime.signals[7] == EncodedSignal{1, 1}));

  auto wrong_abi = descriptor;
  wrong_abi.abi_version = 99;
  expect_fatal_error([&] { (void)jit.execute(handle, wrong_abi); },
                     "ABI version mismatch");

  TestRuntime legacy_runtime;
  legacy_runtime.signals[0] = {0x35, 0};
  legacy_runtime.signals[1] = {0x0f, 0};
  auto legacy_descriptor = abi(legacy_runtime);
  auto legacy_services = copy_jit_services(legacy_descriptor);
  legacy_services.write_update = nullptr;
  legacy_services.write_after = nullptr;
  legacy_descriptor.services = &legacy_services;
  assert(jit.execute(handle, legacy_descriptor) ==
         JitExecutionStatus::completed);

  expect_error([&] { (void)jit.lookup("not_added"); }, "was not added");
  assert(jit.cache_statistics() ==
         fsim::compiler::LlvmJitCacheStatistics{});
}

[[nodiscard]] Process make_scheduled_callback_process() {
  Process process;
  process.id = 3;
  process.name = "scheduled_callbacks";
  process.register_count = 1;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("10XZ0101")},
      WriteUpdate{2, 0},
      WriteAfter{3, 0, 0},
      WriteAfter{4, 0, std::numeric_limits<std::uint64_t>::max()},
      Halt{},
  };
  return process;
}

void test_scheduled_callbacks_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol_prefix) {
  LlvmJit jit{LlvmJitOptions{optimization, {}}};
  const auto process = make_scheduled_callback_process();
  const std::array<std::uint32_t, 5> widths{8, 8, 8, 8, 8};
  const auto symbol = std::string{symbol_prefix} + "_scheduled_callbacks";
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  TestRuntime runtime;
  auto descriptor = abi(runtime);
  assert(jit.execute(handle, descriptor) == JitExecutionStatus::completed);
  const auto value =
      encode(PackedLogic4::from_msb_string("10XZ0101"));
  const std::vector<ScheduledWrite> expected{
      {ScheduledWriteKind::update, 2, value, 0, 0, 0},
      {ScheduledWriteKind::after, 3, value, 0, 0, 0},
      {ScheduledWriteKind::after, 4, value, 0,
       std::numeric_limits<std::uint64_t>::max(),
       std::numeric_limits<std::uint64_t>::max()},
  };
  assert(runtime.scheduled_writes == expected);
  assert(runtime.writes.empty());

  {
    auto too_short = descriptor;
    auto short_services = copy_jit_services(too_short);
    short_services.struct_size = static_cast<std::uint32_t>(
        offsetof(fsim_jit_services_v2, write_update));
    too_short.services = &short_services;
    expect_fatal_error(
        [&] { (void)jit.execute(handle, too_short); },
        "services ABI structure is too small");
  }
  {
    auto missing_update = descriptor;
    auto missing_services = copy_jit_services(missing_update);
    missing_services.write_update = nullptr;
    missing_update.services = &missing_services;
    expect_fatal_error(
        [&] { (void)jit.execute(handle, missing_update); },
        "require write_update");
  }
  {
    auto missing_after = descriptor;
    auto missing_services = copy_jit_services(missing_after);
    missing_services.write_after = nullptr;
    missing_after.services = &missing_services;
    expect_fatal_error(
        [&] { (void)jit.execute(handle, missing_after); },
        "require write_after");
  }

  Process update_only;
  update_only.id = 4;
  update_only.name = "update_only";
  update_only.register_count = 1;
  update_only.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("10100101")},
      WriteUpdate{0, 0},
      Halt{},
  };
  const auto update_symbol = std::string{symbol_prefix} + "_update_only";
  jit.add_process(update_symbol, update_only, widths);
  TestRuntime update_runtime;
  auto update_descriptor = abi(update_runtime);
  auto update_services = copy_jit_services(update_descriptor);
  update_services.write_after = nullptr;
  update_descriptor.services = &update_services;
  assert(jit.execute(jit.lookup(update_symbol), update_descriptor) ==
         JitExecutionStatus::completed);
  assert(update_runtime.scheduled_writes.size() == 1);
  assert(update_runtime.scheduled_writes.front().kind ==
         ScheduledWriteKind::update);

  Process after_only;
  after_only.id = 5;
  after_only.name = "after_only";
  after_only.register_count = 1;
  after_only.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("00111100")},
      WriteAfter{1, 0, 7},
      Halt{},
  };
  const auto after_symbol = std::string{symbol_prefix} + "_after_only";
  jit.add_process(after_symbol, after_only, widths);
  TestRuntime after_runtime;
  auto after_descriptor = abi(after_runtime);
  auto after_services = copy_jit_services(after_descriptor);
  after_services.write_update = nullptr;
  after_descriptor.services = &after_services;
  assert(jit.execute(jit.lookup(after_symbol), after_descriptor) ==
         JitExecutionStatus::completed);
  assert(after_runtime.scheduled_writes.size() == 1);
  assert(after_runtime.scheduled_writes.front().kind ==
         ScheduledWriteKind::after);
  assert(after_runtime.scheduled_writes.front().delay == 7);
}

void test_direct_word_write_order_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
  Process process;
  process.id = 114;
  process.name = "direct_word_write_order";
  process.register_count = 3;
  process.operations = {
      LoadConstant { 0, PackedLogic4::from_msb_string("10100000") },
      WriteBlocking { 0, 0 },
      LoadConstant { 1, PackedLogic4::from_msb_string("0101") },
      WriteBlockingSlice { 0, 1, 0 },
      ReadSignal { 2, 0 },
      WriteAfter { 1, 2, 0 },
      WriteAfterSlice { 1, 1, 4, 0 },
      WriteAfter { 1, 0, 2 },
      Halt { },
  };
  const std::array<std::uint32_t, 2> widths { 8, 8 };
  LlvmJit jit { LlvmJitOptions { optimization, { } } };
  jit.add_process(symbol, process, widths);

  TestRuntime runtime;
  auto descriptor = abi(runtime);
  assert(jit.execute(jit.lookup(symbol), descriptor)
      == JitExecutionStatus::completed);
  assert((runtime.signals[0] == EncodedSignal { 0xa5, 0 }));
  const std::vector<ScheduledWrite> expected {
      { ScheduledWriteKind::after, 1, { 0xa5, 0 }, 0, 0, 0 },
      { ScheduledWriteKind::slice_after, 1, { 0x5, 0 },
          0, 0, 0, 4, 4 },
      { ScheduledWriteKind::after, 1, { 0xa0, 0 }, 0, 2, 2 },
  };
  assert(runtime.scheduled_writes == expected);
}

void test_inertial_callbacks_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol_prefix) {
  LlvmJit jit{LlvmJitOptions{optimization, {}}};
  Process process;
  process.id = 6;
  process.name = "inertial_callbacks";
  process.register_count = 2;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("10XZ0101")},
      WriteInertial{0, 0, {2, 3, 4}},
      LoadConstant{1, PackedLogic4::from_msb_string("XZ")},
      WriteInertialSlice{1, 1, 3, {5, 6, 7}},
      Halt{},
  };
  const std::array<std::uint32_t, 2> widths{8, 8};
  const auto symbol =
      std::string{symbol_prefix} + "_inertial_callbacks";
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  TestRuntime runtime;
  auto descriptor = abi(runtime);
  assert(
      jit.execute(handle, descriptor)
      == JitExecutionStatus::completed);
  const std::vector<InertialWrite> expected{
      {
          0,
          encode(PackedLogic4::from_msb_string("10XZ0101")),
          0,
          0,
          2,
          3,
          4},
      {
          1,
          encode(PackedLogic4::from_msb_string("XZ")),
          3,
          2,
          5,
          6,
          7},
  };
  assert(runtime.inertial_writes == expected);

  {
    auto missing = descriptor;
    auto missing_services = copy_jit_services(missing);
    missing_services.write_inertial = nullptr;
    missing.services = &missing_services;
    expect_fatal_error(
        [&] { (void)jit.execute(handle, missing); },
        "require write_inertial");
  }
  {
    auto missing = descriptor;
    auto missing_services = copy_jit_services(missing);
    missing_services.write_inertial_slice = nullptr;
    missing.services = &missing_services;
    expect_fatal_error(
        [&] { (void)jit.execute(handle, missing); },
        "require write_inertial_slice");
  }
}

void test_projected_callbacks_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol_prefix) {
  LlvmJit jit{LlvmJitOptions{optimization, {}}};
  Process process;
  process.id = 7;
  process.name = "projected_callbacks";
  process.register_count = 6;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("10XZ0101")},
      WriteProjected{
          0,
          0,
          11,
          3,
          ProjectedDelayMode::inertial},
      LoadConstant{1, PackedLogic4::from_msb_string("XZ")},
      WriteProjectedSlice{
          1,
          1,
          3,
          13,
          0,
          ProjectedDelayMode::transport},
      LoadConstant{2, PackedLogic4::from_msb_string("01010101")},
      LoadConstant{3, PackedLogic4::from_msb_string("10101010")},
      WriteProjectedWaveform{
          0,
          {{2, 17}, {3, 23}},
          4,
          ProjectedDelayMode::inertial},
      LoadConstant{4, PackedLogic4::from_msb_string("01")},
      LoadConstant{5, PackedLogic4::from_msb_string("10")},
      WriteProjectedWaveformSlice{
          1,
          {{4, 19}, {5, 29}},
          2,
          0,
          ProjectedDelayMode::transport},
      Halt{},
  };
  const std::array<std::uint32_t, 2> widths{8, 8};
  const auto symbol =
      std::string{symbol_prefix} + "_projected_callbacks";
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  TestRuntime runtime;
  auto descriptor = abi(runtime);
  assert(
      jit.execute(handle, descriptor)
      == JitExecutionStatus::completed);
  const std::vector<ProjectedWrite> expected{
      {
          0,
          encode(PackedLogic4::from_msb_string("10XZ0101")),
          0,
          0,
          11,
          3,
          FSIM_JIT_PROJECTED_INERTIAL_V2},
      {
          1,
          encode(PackedLogic4::from_msb_string("XZ")),
          3,
          2,
          13,
          0,
          FSIM_JIT_PROJECTED_TRANSPORT_V2},
      {
          0,
          encode(PackedLogic4::from_msb_string("01010101")),
          0,
          8,
          17,
          4,
          FSIM_JIT_PROJECTED_INERTIAL_V2},
      {
          0,
          encode(PackedLogic4::from_msb_string("10101010")),
          0,
          8,
          23,
          4,
          FSIM_JIT_PROJECTED_INERTIAL_V2},
      {
          1,
          encode(PackedLogic4::from_msb_string("01")),
          2,
          2,
          19,
          0,
          FSIM_JIT_PROJECTED_TRANSPORT_V2},
      {
          1,
          encode(PackedLogic4::from_msb_string("10")),
          2,
          2,
          29,
          0,
          FSIM_JIT_PROJECTED_TRANSPORT_V2},
  };
  assert(runtime.projected_writes == expected);

  {
    auto missing = descriptor;
    auto missing_services = copy_jit_services(missing);
    missing_services.write_projected = nullptr;
    missing.services = &missing_services;
    expect_fatal_error(
        [&] { (void)jit.execute(handle, missing); },
        "require write_projected");
  }
  {
    auto missing = descriptor;
    auto missing_services = copy_jit_services(missing);
    missing_services.write_projected_slice = nullptr;
    missing.services = &missing_services;
    expect_fatal_error(
        [&] { (void)jit.execute(handle, missing); },
        "require write_projected_slice");
  }
  {
    auto missing = descriptor;
    auto missing_services = copy_jit_services(missing);
    missing_services.write_projected_waveform = nullptr;
    missing.services = &missing_services;
    expect_fatal_error(
        [&] { (void)jit.execute(handle, missing); },
        "require write_projected_waveform");
  }
  {
    auto missing = descriptor;
    auto missing_services = copy_jit_services(missing);
    missing_services.write_projected_waveform_slice = nullptr;
    missing.services = &missing_services;
    expect_fatal_error(
        [&] { (void)jit.execute(handle, missing); },
        "require write_projected_waveform_slice");
  }

  Process wide;
  wide.id = 8;
  wide.name = "wide_projected_callbacks";
  wide.register_count = 5;
  wide.operations = {
      LoadConstant { 0,
          PackedLogic4::from_msb_string(
              "1XZ" + std::string(77U, '0')) },
      LoadConstant { 1,
          PackedLogic4::from_msb_string(std::string(80U, '1')) },
      LoadConstant { 2, PackedLogic4::from_aval_bval(32U, 64U, 0U) },
      LoadConstant { 3, PackedLogic4::from_msb_string("X") },
      LoadConstant { 4, PackedLogic4::from_msb_string("Z") },
      WriteProjectedWaveform { 0, { { 0, 2 }, { 1, 4 } },
          2, ProjectedDelayMode::inertial },
      WriteProjectedWaveformSlice { 1, { { 0, 3 }, { 1, 5 } },
          0, 0, ProjectedDelayMode::transport },
      WriteProjectedSlice { 1, 0, 0, 6, 0,
          ProjectedDelayMode::transport },
      WriteProjectedDynamicSlice { 1, 3,
          DynamicIndex { 2, 79, 0, 0 }, 7, 0,
          ProjectedDelayMode::transport },
      WriteProjectedWaveformDynamicSlice { 1,
          { { 3, 8 }, { 4, 9 } },
          DynamicIndex { 2, 79, 0, 0 }, 0,
          ProjectedDelayMode::transport },
      WriteProjected { 0, 0, 7, 3,
          ProjectedDelayMode::inertial },
      WriteProjected { 0, 0, 0, 0,
          ProjectedDelayMode::transport },
      Halt { }
  };
  const std::array<std::uint32_t, 2> wide_widths { 80U, 80U };
  const auto wide_symbol = std::string { symbol_prefix }
      + "_wide_projected_callbacks";
  jit.add_process(wide_symbol, wide, wide_widths);
  TestRuntime wide_runtime;
  auto wide_descriptor = abi(wide_runtime);
  auto wide_services = copy_jit_services(wide_descriptor);
  wide_services.write_signal_packed = nullptr;
  wide_services.write_projected_signal_packed = nullptr;
  wide_services.execute_signal_operation = [](
      void* context, const std::uint32_t process,
      const std::uint32_t instruction,
      fsim_jit_frame_v2* frame) {
      auto& runtime = *static_cast<TestRuntime*>(context);
      assert(process == 8U && frame != nullptr);
      assert(frame->register_initialized[0] != 0U);
      assert((frame->register_bval[1] & (UINT64_C(3) << 13U))
          == (UINT64_C(3) << 13U));
      runtime.native_service_calls.emplace_back(process, instruction);
      return std::uint32_t { 0 };
  };
  wide_descriptor.services = &wide_services;
  assert(jit.execute(jit.lookup(wide_symbol), wide_descriptor)
      == JitExecutionStatus::completed);
  assert((wide_runtime.native_service_calls
      == std::vector<std::pair<std::uint32_t, std::uint32_t>> {
          { 8U, 5U }, { 8U, 6U }, { 8U, 7U }, { 8U, 8U },
          { 8U, 9U }, { 8U, 10U }, { 8U, 11U } }));

  struct PackedProjectedCapture {
    std::uint32_t calls { };
    std::uint32_t status { };
    std::uint32_t expected_signal { };
    std::uint32_t expected_width { };
    std::array<bool, 4> expected_present { };
    std::array<std::array<std::uint64_t, 2>, 4> words { };
  };
  const auto capture_projected_planes = +[](void* const opaque,
      const std::uint32_t signal, const std::uint32_t width,
      const std::uint64_t* const aval, const std::uint64_t* const bval,
      const std::uint64_t* const plane2,
      const std::uint64_t* const plane3) -> std::uint32_t {
    if (opaque == nullptr) {
      return 1U;
    }
    auto& capture = *static_cast<PackedProjectedCapture*>(opaque);
    const std::array<const std::uint64_t*, 4> source_planes {
        aval, bval, plane2, plane3 };
    const auto word_count = static_cast<std::size_t>(
        (static_cast<std::uint64_t>(width) + 63U) / 64U);
    if (signal != capture.expected_signal || width != capture.expected_width
        || word_count != capture.words.front().size()) {
      return 2U;
    }
    for (std::size_t plane = 0U; plane < source_planes.size(); ++plane) {
      if ((source_planes[plane] != nullptr)
          != capture.expected_present[plane]) {
        return 3U;
      }
      if (source_planes[plane] != nullptr) {
        std::copy_n(source_planes[plane], word_count,
            capture.words[plane].begin());
      }
    }
    ++capture.calls;
    return capture.status;
  };
  const auto copy_expected_planes = [](
      const PackedLogic4& value) {
    std::array<std::array<std::uint64_t, 2>, 4> expected { };
    assert(value.width() == 80U);
    assert(value.aval_words().size() == 2U);
    assert(value.bval_words().size() == 2U);
    std::copy(value.aval_words().begin(), value.aval_words().end(),
        expected[0].begin());
    std::copy(value.bval_words().begin(), value.bval_words().end(),
        expected[1].begin());
    if (value.is_logic9()) {
      for (std::size_t plane = 2U; plane < 4U; ++plane) {
        const auto source = value.logic9_plane_words(plane);
        assert(source.size() == 2U);
        std::copy(source.begin(), source.end(), expected[plane].begin());
      }
    }
    return expected;
  };

  const auto wide_l4_value = PackedLogic4::from_msb_string(
      "1XZ" + std::string(77U, '0'));
  const auto wide_l4_expected = copy_expected_planes(wide_l4_value);
  const std::array<std::array<std::uint64_t, 2>, 4>
      expected_wide_l4_planes {{
          { 0U, UINT64_C(0xc000) },
          { 0U, UINT64_C(0x6000) },
          { 0U, 0U },
          { 0U, 0U },
      }};
  assert(wide_l4_expected == expected_wide_l4_planes);

  Process wide_projected_only;
  wide_projected_only.id = 9U;
  wide_projected_only.name = "wide_projected_packed_callback";
  wide_projected_only.register_count = 1U;
  wide_projected_only.operations = {
      LoadConstant { 0U, wide_l4_value },
      WriteProjected {
          0U, 0U, 0U, 0U, ProjectedDelayMode::inertial },
      Halt { }
  };
  const std::array<std::uint32_t, 1> projected_only_widths { 80U };
  const auto projected_only_symbol = std::string { symbol_prefix }
      + "_wide_projected_packed_callback";
  jit.add_process(
      projected_only_symbol, wide_projected_only, projected_only_widths);
  const auto projected_only_handle = jit.lookup(projected_only_symbol);
  TestRuntime projected_only_runtime;
  auto projected_only_descriptor = abi(projected_only_runtime);
  auto projected_only_services
      = copy_jit_services(projected_only_descriptor);
  assert(projected_only_services.struct_size
      == sizeof(fsim_jit_services_v2));
  projected_only_services.write_signal_packed = nullptr;
  projected_only_services.execute_signal_operation = nullptr;
  PackedProjectedCapture projected_capture;
  projected_capture.expected_signal = 0U;
  projected_capture.expected_width = 80U;
  projected_capture.expected_present = { true, true, false, false };
  projected_capture.words = { };
  projected_only_descriptor.context = &projected_capture;
  projected_only_services.write_projected_signal_packed
      = capture_projected_planes;
  projected_only_descriptor.services = &projected_only_services;
  assert(jit.execute(projected_only_handle, projected_only_descriptor)
      == JitExecutionStatus::completed);
  assert(projected_capture.calls == 1U);
  assert(projected_capture.words == wide_l4_expected);

  const auto layout = jit.frame_layout(projected_only_handle);
  std::vector<std::uint64_t> register_aval(layout.register_word_count);
  std::vector<std::uint64_t> register_bval(layout.register_word_count);
  std::vector<std::uint8_t> register_initialized(layout.register_count);
  fsim_jit_frame_v2 frame { };
  auto result = new_resume_result();
  const auto expect_preflight_failure_without_mutation =
      [&](const fsim_jit_runtime_instance_v2& descriptor,
          const std::string_view message) {
        jit.initialize_frame(projected_only_handle, frame, register_aval,
            register_bval, register_initialized);
        const auto frame_before = frame;
        const auto result_before = result;
        const auto aval_before = register_aval;
        const auto bval_before = register_bval;
        const auto initialized_before = register_initialized;
        expect_fatal_error(
            [&] {
              (void)jit.resume(
                  projected_only_handle, descriptor, frame, result);
            },
            message);
        assert(frame.abi_version == frame_before.abi_version);
        assert(frame.struct_size == frame_before.struct_size);
        assert(frame.layout_id_low == frame_before.layout_id_low);
        assert(frame.layout_id_high == frame_before.layout_id_high);
        assert(frame.register_count == frame_before.register_count);
        assert(frame.program_counter == frame_before.program_counter);
        assert(frame.state == frame_before.state);
        assert(frame.last_instruction == frame_before.last_instruction);
        assert(frame.register_aval == frame_before.register_aval);
        assert(frame.register_bval == frame_before.register_bval);
        assert(frame.register_initialized == frame_before.register_initialized);
        assert(frame.register_logic9_plane2
            == frame_before.register_logic9_plane2);
        assert(frame.register_logic9_plane3
            == frame_before.register_logic9_plane3);
        assert(frame.native_call_depth == frame_before.native_call_depth);
        assert(frame.native_call_reserved == frame_before.native_call_reserved);
        assert(std::equal(std::begin(frame.native_return_stack),
            std::end(frame.native_return_stack),
            std::begin(frame_before.native_return_stack)));
        assert(register_aval == aval_before);
        assert(register_bval == bval_before);
        assert(register_initialized == initialized_before);
        assert(result.abi_version == result_before.abi_version);
        assert(result.struct_size == result_before.struct_size);
        assert(result.status == result_before.status);
        assert(result.instruction == result_before.instruction);
        assert(result.delay == result_before.delay);
      };

  auto missing_packed_projected = projected_only_descriptor;
  auto missing_packed_projected_services
      = copy_jit_services(missing_packed_projected);
  missing_packed_projected_services.write_projected_signal_packed = nullptr;
  missing_packed_projected.services = &missing_packed_projected_services;
  expect_preflight_failure_without_mutation(missing_packed_projected,
      "require write_projected_signal_packed");
  assert(projected_capture.calls == 1U);

  constexpr auto legacy_service_prefix_size = offsetof(
      fsim_jit_services_v2, write_projected_signal_packed);
  static_assert(legacy_service_prefix_size == 672U);
  alignas(fsim_jit_services_v2)
      std::array<std::byte, legacy_service_prefix_size> legacy_storage { };
  const auto make_legacy_prefix = [&](const fsim_jit_services_v2& services) {
    std::memcpy(legacy_storage.data(), &services,
        legacy_service_prefix_size);
    const auto size = static_cast<std::uint32_t>(legacy_service_prefix_size);
    std::memcpy(legacy_storage.data()
            + offsetof(fsim_jit_services_v2, struct_size),
        &size, sizeof(size));
    return reinterpret_cast<const fsim_jit_services_v2*>(
        legacy_storage.data());
  };

  TestRuntime legacy_runtime;
  auto legacy_descriptor = abi(legacy_runtime);
  auto legacy_full_services = copy_jit_services(legacy_descriptor);
  assert(legacy_full_services.struct_size
      == sizeof(fsim_jit_services_v2));
  legacy_descriptor.services = make_legacy_prefix(legacy_full_services);
  assert(jit.execute(handle, legacy_descriptor)
      == JitExecutionStatus::completed);
  assert(legacy_runtime.projected_writes == expected);

  auto physically_short = projected_only_descriptor;
  physically_short.services = make_legacy_prefix(projected_only_services);
  expect_preflight_failure_without_mutation(physically_short,
      "services ABI structure is too small for "
      "write_projected_signal_packed");
  assert(projected_capture.calls == 1U);

  projected_capture.status = 7U;
  expect_generated_runtime_error(
      [&] {
        (void)jit.execute(projected_only_handle, projected_only_descriptor);
      },
      1U, JitGeneratedRuntimeErrorReason::signal_callback_failure,
      "exact-width signal runtime callback failed");
  assert(projected_capture.calls == 2U);

  std::string logic9_bits;
  logic9_bits.reserve(80U);
  constexpr std::string_view logic9_cycle { "U01XZWLH-" };
  for (std::size_t bit = 0U; bit < 80U; ++bit) {
    logic9_bits.push_back(logic9_cycle[bit % logic9_cycle.size()]);
  }
  const auto wide_l9_value = PackedLogic4::from_logic9_msb_string(logic9_bits);
  const auto wide_l9_expected = copy_expected_planes(wide_l9_value);
  Process wide_logic9_projected;
  wide_logic9_projected.id = 10U;
  wide_logic9_projected.name = "wide_logic9_projected_packed_callback";
  wide_logic9_projected.register_count = 1U;
  wide_logic9_projected.register_value_kinds = { ValueKind::logic9 };
  wide_logic9_projected.operations = {
      LoadConstant { 0U, wide_l9_value },
      WriteProjected {
          0U, 0U, 0U, 0U, ProjectedDelayMode::inertial },
      Halt { }
  };
  const std::array<std::uint32_t, 1> logic9_widths { 80U };
  const std::array<ValueKind, 1> logic9_signal_kinds { ValueKind::logic9 };
  const auto logic9_symbol = std::string { symbol_prefix }
      + "_wide_logic9_projected_packed_callback";
  jit.add_process(logic9_symbol, wide_logic9_projected,
      logic9_widths, logic9_signal_kinds);
  TestRuntime logic9_runtime;
  auto logic9_descriptor = abi(logic9_runtime);
  auto logic9_services = copy_jit_services(logic9_descriptor);
  logic9_services.write_signal_packed = nullptr;
  logic9_services.execute_signal_operation = nullptr;
  logic9_services.write_projected_signal_packed
      = capture_projected_planes;
  PackedProjectedCapture logic9_capture;
  logic9_capture.expected_signal = 0U;
  logic9_capture.expected_width = 80U;
  logic9_capture.expected_present = { true, true, true, true };
  logic9_descriptor.context = &logic9_capture;
  logic9_descriptor.services = &logic9_services;
  assert(jit.execute(jit.lookup(logic9_symbol), logic9_descriptor)
      == JitExecutionStatus::completed);
  assert(logic9_capture.calls == 1U);
  assert(logic9_capture.words == wide_l9_expected);

}

[[nodiscard]] Process make_scheduling_differential_process() {
  Process process;
  process.id = 0;
  process.name = "scheduled_differential";
  process.register_count = 3;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("10XZ0101")},
      WriteUpdate{0, 0},
      WriteAfter{1, 0, 4},
      WaitFor{2},
      LoadConstant{1, PackedLogic4::from_msb_string("00111100")},
      WriteUpdate{2, 1},
      WriteAfter{3, 1, 1},
      WaitFor{2},
      Halt{},
  };
  return process;
}

void test_scheduling_differential_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol_prefix) {
  const auto process = make_scheduling_differential_process();
  const std::array<std::uint32_t, 4> widths{8, 8, 8, 8};
  LlvmJit jit{LlvmJitOptions{optimization, {}}};
  const auto symbol = std::string{symbol_prefix} + "_scheduled_differential";
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);
  const auto layout = jit.frame_layout(handle);
  std::vector<std::uint64_t> register_aval(layout.register_word_count);
  std::vector<std::uint64_t> register_bval(layout.register_word_count);
  std::vector<std::uint8_t> register_initialized(layout.register_count);
  fsim_jit_frame_v2 frame{};
  jit.initialize_frame(
      handle, frame, register_aval, register_bval,
      register_initialized);

  TestRuntime runtime;
  auto descriptor = abi(runtime);
  auto result = new_resume_result();
  assert(jit.resume(handle, descriptor, frame, result) ==
         JitResumeStatus::wait_for);
  assert(result.instruction == 3);
  assert(result.delay == 2);
  runtime.current_time += result.delay;
  assert(jit.resume(handle, descriptor, frame, result) ==
         JitResumeStatus::wait_for);
  assert(result.instruction == 7);
  assert(result.delay == 2);
  runtime.current_time += result.delay;
  assert(jit.resume(handle, descriptor, frame, result) ==
         JitResumeStatus::completed);
  assert(result.instruction == 8);
  assert(runtime.current_time == 4);

  std::vector<ObservedWrite> generated;
  generated.reserve(runtime.scheduled_writes.size());
  for (const auto &write : runtime.scheduled_writes) {
    generated.push_back({write.due, write.signal, write.value});
  }
  const auto order = [](const ObservedWrite &lhs,
                        const ObservedWrite &rhs) {
    if (lhs.time != rhs.time) {
      return lhs.time < rhs.time;
    }
    return lhs.signal < rhs.signal;
  };
  std::sort(generated.begin(), generated.end(), order);

  fsim::runtime::simir::Interpreter interpreter;
  for (std::uint32_t signal = 0; signal < widths.size(); ++signal) {
    (void)interpreter.add_signal(
        {"scheduled" + std::to_string(signal),
         PackedLogic4::from_msb_string("XXXXXXXX")});
  }
  std::vector<ObservedWrite> reference;
  interpreter.set_signal_change_hook(
      [&](const SignalId signal, const PackedLogic4 &value,
          const fsim::runtime::SimulationTick time) {
        reference.push_back({time, signal, encode(value)});
      });
  (void)interpreter.add_process(process);
  const auto reference_result = interpreter.run();
  assert(reference_result.status == fsim::runtime::RunStatus::completed);
  assert(reference_result.time == 4);
  std::sort(reference.begin(), reference.end(), order);
  assert(generated == reference);
}

[[nodiscard]] Process
make_branch_process(const UnknownBranchPolicy unknown_policy) {
  Process process;
  process.id = 0;
  process.name = "branch";
  process.register_count = 2;
  process.operations = {
      ReadSignal{0, 0},
      Branch{0, 2, 4, unknown_policy},
      LoadConstant{1, PackedLogic4::from_msb_string("10100101")},
      Jump{5},
      LoadConstant{1, PackedLogic4::from_msb_string("00111100")},
      WriteBlocking{1, 1},
      Halt{},
  };
  return process;
}

[[nodiscard]] Process make_nested_call_process() {
  const CallStack stack{0, 1, 2};
  Process process;
  process.id = 0;
  process.name = "nested_calls";
  process.register_count = 6;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_aval_bval(32, 0, 0)},
      LoadConstant{1, PackedLogic4::from_aval_bval(32, 0, 0)},
      LoadConstant{2, PackedLogic4::from_aval_bval(32, 0, 0)},
      LoadConstant{3, PackedLogic4::from_aval_bval(8, 10, 0)},
      LoadConstant{4, PackedLogic4::from_aval_bval(8, 0, 0)},
      Call{9, 6, stack},
      WriteBlocking{0, 4},
      Halt{},
      Halt{},
      DebugPoint{
          DebugPointKind::call,
          SourceLocation{"nested_calls.simir", 1, 1}},
      LoadConstant{5, PackedLogic4::from_aval_bval(8, 0, 0)},
      Call{15, 12, stack},
      CopyRegister{4, 5},
      Return{stack},
      Halt{},
      LoadConstant{5, PackedLogic4::from_aval_bval(8, 42, 0)},
      Return{stack},
  };
  return process;
}

[[nodiscard]] EncodedSignal
run_interpreter_branch(const UnknownBranchPolicy unknown_policy,
                       const Logic4 condition) {
  fsim::runtime::simir::Interpreter interpreter;
  (void)interpreter.add_signal(
      {"condition", PackedLogic4{1, condition}});
  const auto output = interpreter.add_signal(
      {"output", PackedLogic4::from_msb_string("00000000")});
  (void)interpreter.add_process(make_branch_process(unknown_policy));
  const auto result = interpreter.run();
  assert(result.status == fsim::runtime::RunStatus::completed);
  return encode(interpreter.signal_value(output));
}

void test_native_callable_regions() {
  Process process;
  process.id = 12;
  process.name = "native_callable_region";
  process.register_count = 4;
  process.operations = {
      LoadConstant{3, PackedLogic4::from_aval_bval(8, 7, 0)},
      CallableFramePush{1, {0, 1}, {}, {}, true},
      LoadConstant{1, PackedLogic4::from_aval_bval(8, 11, 0)},
      Call{9, 4, {}},
      CopyRegister{2, 1},
      CallableFramePop{1, {2}, {}, {}},
      WriteBlocking{0, 2},
      WriteBlocking{1, 3},
      Halt{},
      DebugPoint{
          DebugPointKind::call,
          SourceLocation{"native_callable.simir", 1, 1}},
      LoadConstant{1, PackedLogic4::from_aval_bval(8, 42, 0)},
      Return{{}},
  };

  const std::array<std::uint32_t, 2> widths{8, 8};
  LlvmJit jit{LlvmJitOptions{JitOptimizationLevel::o1, {}}};
  jit.add_process("native_callable_region", process, widths);
  const auto handle = jit.lookup("native_callable_region");
  const auto layout = jit.frame_layout(handle);
  std::vector<std::uint64_t> aval(layout.register_count);
  std::vector<std::uint64_t> bval(layout.register_count);
  std::vector<std::uint8_t> initialized(layout.register_count);
  fsim_jit_frame_v2 frame{};
  jit.initialize_frame(handle, frame, aval, bval, initialized);

  TestRuntime runtime;
  auto descriptor = abi(runtime);
  descriptor.flags = FSIM_JIT_RUNTIME_FLAG_DEBUG_POINTS_V2;
  auto result = new_resume_result();
  assert(jit.resume(handle, descriptor, frame, result)
         == JitResumeStatus::debug_point);
  assert(result.instruction == 9);
  assert(frame.program_counter == 10);
  assert(frame.native_call_depth == 1);
  assert(frame.native_return_stack[0] == 4);
  assert(jit.resume(handle, descriptor, frame, result)
         == JitResumeStatus::completed);
  assert(frame.native_call_depth == 0);
  assert((runtime.signals[0] == EncodedSignal{42, 0}));
  assert((runtime.signals[1] == EncodedSignal{7, 0}));

  auto conservative = process;
  conservative.id = 13;
  conservative.name = "conservative_callable_region";
  fsim::runtime::simir::operation_get<CallableFramePush>(
      conservative.operations[1]).native_isolated = false;
  LlvmJit conservative_jit{
      LlvmJitOptions{JitOptimizationLevel::o1, {}}};
  conservative_jit.add_process(
      "conservative_callable_region", conservative, widths);
  const auto conservative_handle = conservative_jit.lookup(
      "conservative_callable_region");
  const auto conservative_layout = conservative_jit.frame_layout(
      conservative_handle);
  std::vector<std::uint64_t> conservative_aval(
      conservative_layout.register_count);
  std::vector<std::uint64_t> conservative_bval(
      conservative_layout.register_count);
  std::vector<std::uint8_t> conservative_initialized(
      conservative_layout.register_count);
  fsim_jit_frame_v2 conservative_frame{};
  conservative_jit.initialize_frame(
      conservative_handle,
      conservative_frame,
      conservative_aval,
      conservative_bval,
      conservative_initialized);
  TestRuntime conservative_runtime;
  auto conservative_descriptor = abi(conservative_runtime);
  auto conservative_result = new_resume_result();
  assert(conservative_jit.resume(
             conservative_handle,
             conservative_descriptor,
             conservative_frame,
             conservative_result)
         == JitResumeStatus::simir_boundary);
  assert(conservative_result.instruction == 1);
  assert(conservative_frame.program_counter == 1);
  assert(conservative_frame.native_call_depth == 0);

  Process nested_suspend;
  nested_suspend.id = 14;
  nested_suspend.name = "native_callable_nested_suspend";
  nested_suspend.operations = {
      CallableFramePush{1, {}, {}, {}, true},
      Call{5, 2, {}},
      CallableFramePop{1, {}, {}, {}},
      Halt{},
      Halt{},
      CallableFramePush{2, {}, {}, {}, true},
      Call{10, 7, {}},
      CallableFramePop{2, {}, {}, {}},
      Return{{}},
      Halt{},
      Yield{},
      Return{{}},
  };
  for (const auto optimization : {
           JitOptimizationLevel::o0,
           JitOptimizationLevel::o2,
       }) {
    LlvmJit nested_jit{LlvmJitOptions{optimization, {}}};
    nested_jit.add_process(
        "native_callable_nested_suspend", nested_suspend,
        std::array<std::uint32_t, 0>{});
    const auto nested_handle = nested_jit.lookup(
        "native_callable_nested_suspend");
    const auto nested_layout = nested_jit.frame_layout(nested_handle);
    std::vector<std::uint64_t> nested_aval(nested_layout.register_count);
    std::vector<std::uint64_t> nested_bval(nested_layout.register_count);
    std::vector<std::uint8_t> nested_initialized(
        nested_layout.register_count);
    fsim_jit_frame_v2 nested_frame{};
    nested_jit.initialize_frame(
        nested_handle, nested_frame, nested_aval, nested_bval,
        nested_initialized);
    TestRuntime nested_runtime;
    auto nested_descriptor = abi(nested_runtime);
    auto nested_result = new_resume_result();
    assert(nested_jit.resume(
               nested_handle, nested_descriptor, nested_frame, nested_result)
           == JitResumeStatus::yielded);
    assert(nested_result.instruction == 10);
    assert(nested_frame.program_counter == 11);
    assert(nested_frame.native_call_depth == 2);
    assert(nested_jit.resume(
               nested_handle, nested_descriptor, nested_frame, nested_result)
           == JitResumeStatus::completed);
    assert(nested_frame.native_call_depth == 0);
  }
}

void test_control_flow_at_level(const JitOptimizationLevel optimization,
                                const std::string_view symbol_prefix) {
  LlvmJit jit{LlvmJitOptions{optimization, {}}};
  const std::array<std::uint32_t, 2> widths{1, 8};
  const auto when_false_symbol =
      std::string{symbol_prefix} + "_unknown_false";
  const auto error_symbol = std::string{symbol_prefix} + "_unknown_error";
  jit.add_process(
      when_false_symbol,
      make_branch_process(UnknownBranchPolicy::when_false), widths);
  jit.add_process(
      error_symbol, make_branch_process(UnknownBranchPolicy::error), widths);
  const auto when_false_handle = jit.lookup(when_false_symbol);
  const auto error_handle = jit.lookup(error_symbol);

  const auto run_jit = [&](const JitProcessHandle handle,
                           const Logic4 condition) {
    TestRuntime runtime;
    runtime.signals[0] = encode(condition);
    auto descriptor = abi(runtime);
    assert(jit.execute(handle, descriptor) == JitExecutionStatus::completed);
    assert(runtime.assertion_count == 0);
    return runtime.signals[1];
  };

  constexpr std::array known_conditions{Logic4::zero, Logic4::one};
  for (const auto condition : known_conditions) {
    const auto reference = run_interpreter_branch(
        UnknownBranchPolicy::when_false, condition);
    assert(run_jit(when_false_handle, condition) == reference);
    assert(run_jit(error_handle, condition) ==
           run_interpreter_branch(UnknownBranchPolicy::error, condition));
  }
  assert((run_jit(when_false_handle, Logic4::zero) ==
          EncodedSignal{UINT64_C(0x3c), 0}));
  assert((run_jit(when_false_handle, Logic4::one) ==
          EncodedSignal{UINT64_C(0xa5), 0}));

  Process finite_loop;
  finite_loop.id = 27;
  finite_loop.name = "finite_control_flow_loop";
  finite_loop.register_count = 5;
  finite_loop.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      LoadConstant{1, PackedLogic4::from_msb_string("00000011")},
      LoadConstant{2, PackedLogic4::from_msb_string("00000001")},
      LoadConstant{3, PackedLogic4::from_msb_string("00000000")},
      LoadConstant{4, PackedLogic4::from_msb_string("00000000")},
      Branch{0, 6, 10, UnknownBranchPolicy::when_false},
      Binary{BinaryOperator::add_unsigned, 4, 4, 1},
      Binary{BinaryOperator::subtract_unsigned, 1, 1, 2},
      Binary{BinaryOperator::not_equal, 0, 1, 3},
      Jump{5},
      WriteBlocking{1, 4},
      Halt{},
  };
  const auto finite_loop_symbol =
      std::string{symbol_prefix} + "_finite_loop";
  jit.add_process(finite_loop_symbol, finite_loop, widths);
  const auto finite_loop_handle = jit.lookup(finite_loop_symbol);
  TestRuntime finite_loop_runtime;
  auto finite_loop_descriptor = abi(finite_loop_runtime);
  assert(jit.execute(finite_loop_handle, finite_loop_descriptor) ==
         JitExecutionStatus::completed);
  assert((finite_loop_runtime.signals[1] == EncodedSignal{6, 0}));

  // Verilog/SystemVerilog X and Z select the false edge only when the lowering
  // explicitly requested that language policy.
  constexpr std::array unknown_conditions{Logic4::x, Logic4::z};
  for (const auto condition : unknown_conditions) {
    const auto reference = run_interpreter_branch(
        UnknownBranchPolicy::when_false, condition);
    assert((reference == EncodedSignal{UINT64_C(0x3c), 0}));
    assert(run_jit(when_false_handle, condition) == reference);

    bool interpreter_rejected = false;
    try {
      (void)run_interpreter_branch(UnknownBranchPolicy::error, condition);
    } catch (const InterpreterError &error) {
      interpreter_rejected = true;
      assert(error.instruction() == 1);
      assert(std::string_view{error.what()}.find(
                 "branch condition is unknown or high impedance") !=
             std::string_view::npos);
    }
    assert(interpreter_rejected);

    TestRuntime runtime;
    runtime.signals[0] = encode(condition);
    auto descriptor = abi(runtime);
    expect_generated_runtime_error(
        [&] { (void)jit.execute(error_handle, descriptor); },
        1, JitGeneratedRuntimeErrorReason::unknown_branch_condition,
        "instruction 1: branch condition is unknown or high impedance");

    std::vector<std::uint64_t> register_aval(
        jit.frame_layout(error_handle).register_count);
    std::vector<std::uint64_t> register_bval(
        jit.frame_layout(error_handle).register_count);
    std::vector<std::uint8_t> register_initialized(
        jit.frame_layout(error_handle).register_count);
    fsim_jit_frame_v2 frame{};
    jit.initialize_frame(
        error_handle, frame, register_aval, register_bval,
        register_initialized);
    auto result = new_resume_result();
    expect_generated_runtime_error(
        [&] {
          (void)jit.resume(
              error_handle, descriptor, frame, result);
        },
        1, JitGeneratedRuntimeErrorReason::unknown_branch_condition,
        "instruction 1: branch condition is unknown or high impedance");
    assert(result.status == FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR_V2);
    assert(frame.state == FSIM_JIT_FRAME_STATE_RUNTIME_ERROR_V2);
    expect_generated_runtime_error(
        [&] {
          (void)jit.resume(
              error_handle, descriptor, frame, result);
        },
        1, JitGeneratedRuntimeErrorReason::unknown_branch_condition,
        "instruction 1: branch condition is unknown or high impedance");
  }

  LlvmJit call_jit{LlvmJitOptions{optimization, {}}};
  const auto call_symbol =
      std::string{symbol_prefix} + "_nested_calls";
  call_jit.add_process(
      call_symbol, make_nested_call_process(),
      std::array<std::uint32_t, 1>{8});
  const auto call_handle = call_jit.lookup(call_symbol);
  TestRuntime call_runtime;
  auto call_descriptor = abi(call_runtime);
  const auto call_layout = call_jit.frame_layout(call_handle);
  std::vector<std::uint64_t> call_aval(
      call_layout.register_count);
  std::vector<std::uint64_t> call_bval(
      call_layout.register_count);
  std::vector<std::uint8_t> call_initialized(
      call_layout.register_count);
  fsim_jit_frame_v2 call_frame{};
  call_jit.initialize_frame(
      call_handle,
      call_frame,
      call_aval,
      call_bval,
      call_initialized);
  auto call_result = new_resume_result();
  auto call_status = call_jit.resume(
      call_handle,
      call_descriptor,
      call_frame,
      call_result);
  if (call_status == JitResumeStatus::debug_point) {
    assert(call_result.instruction == 9);
    call_status = call_jit.resume(
        call_handle,
        call_descriptor,
        call_frame,
        call_result);
  }
  assert(call_status == JitResumeStatus::completed);
  assert((
      call_runtime.signals[0]
      == EncodedSignal{UINT64_C(42), UINT64_C(0)}));

  const auto verify_call_error =
      [&](const std::string_view suffix,
          Process process,
          const std::uint32_t instruction,
          const JitGeneratedRuntimeErrorReason reason,
          const std::string_view message) {
        const auto symbol =
            std::string{symbol_prefix} + "_" + std::string{suffix};
        call_jit.add_process(
            symbol,
            process,
            std::array<std::uint32_t, 0>{});
        TestRuntime runtime;
        auto descriptor = abi(runtime);
        expect_generated_runtime_error(
            [&] {
              (void)call_jit.execute(
                  call_jit.lookup(symbol), descriptor);
            },
            instruction,
            reason,
            message);
      };
  const CallStack one_entry_stack{0, 1, 1};
  const auto word =
      [](const std::uint64_t aval, const std::uint64_t bval = 0) {
        return PackedLogic4::from_aval_bval(
            32, aval, bval);
      };

  Process unknown_stack;
  unknown_stack.name = "unknown_call_stack";
  unknown_stack.register_count = 2;
  unknown_stack.operations = {
      LoadConstant{0, word(0, 1)},
      LoadConstant{1, word(0)},
      Return{one_entry_stack},
      Halt{}};
  verify_call_error(
      "unknown_call_stack",
      std::move(unknown_stack),
      2,
      JitGeneratedRuntimeErrorReason::call_stack_unknown,
      "instruction 2: call-stack state contains an unknown");

  Process overflowing_stack;
  overflowing_stack.name = "overflowing_call_stack";
  overflowing_stack.register_count = 2;
  overflowing_stack.operations = {
      LoadConstant{0, word(1)},
      LoadConstant{1, word(0)},
      Call{3, 3, one_entry_stack},
      Halt{}};
  verify_call_error(
      "overflowing_call_stack",
      std::move(overflowing_stack),
      2,
      JitGeneratedRuntimeErrorReason::call_stack_overflow,
      "instruction 2: call-stack capacity is exhausted");

  Process underflowing_stack;
  underflowing_stack.name = "underflowing_call_stack";
  underflowing_stack.register_count = 2;
  underflowing_stack.operations = {
      LoadConstant{0, word(0)},
      LoadConstant{1, word(0)},
      Return{one_entry_stack},
      Halt{}};
  verify_call_error(
      "underflowing_call_stack",
      std::move(underflowing_stack),
      2,
      JitGeneratedRuntimeErrorReason::call_stack_underflow,
      "instruction 2: call-stack underflow");

  Process invalid_return_stack;
  invalid_return_stack.name = "invalid_return_call_stack";
  invalid_return_stack.register_count = 2;
  invalid_return_stack.operations = {
      LoadConstant{0, word(1)},
      LoadConstant{1, word(99)},
      Return{one_entry_stack},
      Halt{}};
  verify_call_error(
      "invalid_return_call_stack",
      std::move(invalid_return_stack),
      2,
      JitGeneratedRuntimeErrorReason::call_stack_target,
      "instruction 2: call-stack return target is invalid");
}

void test_checked_integer_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol_prefix) {
  const auto integer = [](const std::int32_t value) {
    return PackedLogic4::from_aval_bval(
        32,
        static_cast<std::uint32_t>(value),
        0);
  };
  LlvmJit jit{LlvmJitOptions{optimization, {}}};

  Process success;
  success.id = 31;
  success.name = "checked_integer_success";
  success.register_count = 7;
  success.operations = {
      LoadConstant{0, integer(-5)},
      LoadConstant{1, integer(3)},
      IntegerBinary{IntegerBinaryOperator::add, 2, 0, 1},
      IntegerBinary{IntegerBinaryOperator::modulo, 3, 0, 1},
      IntegerBinary{IntegerBinaryOperator::remainder, 4, 0, 1},
      IntegerUnary{IntegerUnaryOperator::absolute, 5, 0},
      IntegerBinary{IntegerBinaryOperator::power, 6, 1, 1},
      IntegerCheck{2, -2, 2},
      WriteBlocking{0, 2},
      WriteBlocking{1, 3},
      WriteBlocking{2, 4},
      WriteBlocking{3, 5},
      WriteBlocking{4, 6},
      Halt{}};
  const auto success_symbol =
      std::string{symbol_prefix} + "_success";
  const std::array<std::uint32_t, 5> output_widths{
      32, 32, 32, 32, 32};
  jit.add_process(success_symbol, success, output_widths);
  TestRuntime runtime;
  auto descriptor = abi(runtime);
  assert(
      jit.execute(jit.lookup(success_symbol), descriptor)
      == JitExecutionStatus::completed);
  const std::array<std::int32_t, 5> expected{
      -2, 1, -2, 5, 27};
  for (std::size_t index = 0; index < expected.size(); ++index) {
    assert((
        runtime.signals[index]
        == EncodedSignal{
            static_cast<std::uint32_t>(expected[index]), 0}));
  }

  const auto add_failure =
      [&](const std::string_view suffix,
          std::vector<Operation> operations,
          const std::uint32_t instruction,
          const JitGeneratedRuntimeErrorReason reason,
          const std::string_view message) {
        Process process;
        process.id = 32;
        process.name = "checked_integer_failure";
        process.register_count = 4;
        process.operations = std::move(operations);
        const auto symbol =
            std::string{symbol_prefix} + "_" + std::string{suffix};
        jit.add_process(symbol, process, {});
        TestRuntime failed_runtime;
        auto failed_descriptor = abi(failed_runtime);
        const auto handle = jit.lookup(symbol);
        expect_generated_runtime_error(
            [&] {
              (void)jit.execute(
                  handle, failed_descriptor);
            },
            instruction,
            reason,
            message);
        const auto layout = jit.frame_layout(handle);
        std::vector<std::uint64_t> aval(layout.register_count);
        std::vector<std::uint64_t> bval(layout.register_count);
        std::vector<std::uint8_t> initialized(
            layout.register_count);
        fsim_jit_frame_v2 frame{};
        jit.initialize_frame(
            handle, frame, aval, bval, initialized);
        auto resume_result = new_resume_result();
        expect_generated_runtime_error(
            [&] {
              (void)jit.resume(
                  handle,
                  failed_descriptor,
                  frame,
                  resume_result);
            },
            instruction,
            reason,
            message);
        assert(
            resume_result.status
            == FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR_V2);
        assert(
            resume_result.delay
            == static_cast<std::uint64_t>(reason));
        assert(
            frame.state
            == FSIM_JIT_FRAME_STATE_RUNTIME_ERROR_V2);
        assert(resume_result.instruction == instruction);
        assert(frame.last_instruction == instruction);
        assert(frame.program_counter == static_cast<std::uint32_t>(reason));
        expect_generated_runtime_error(
            [&] {
              (void)jit.resume(
                  handle,
                  failed_descriptor,
                  frame,
                  resume_result);
            },
            instruction,
            reason,
            message);
      };
  // Runtime operands exercise the carry/overflow paths after either LLVM
  // optimization pipeline, rather than constant-folding the checked result.
  struct BoundaryCase {
      std::int32_t left;
      std::int32_t right;
      std::int64_t expected;
  };
  const auto minimum32 = std::numeric_limits<std::int32_t>::min();
  const auto maximum32 = std::numeric_limits<std::int32_t>::max();
  const auto check_boundaries = [&](const IntegerBinaryOperator operation,
                                    const std::string_view suffix,
                                    const std::initializer_list<BoundaryCase> cases) {
      Process boundary;
      boundary.id = 0U;
      boundary.name = std::string { symbol_prefix } + std::string { suffix };
      boundary.register_count = 3U;
      boundary.operations = {
          ReadSignal { 0U, 0U }, ReadSignal { 1U, 1U },
          IntegerBinary { operation, 2U, 0U, 1U },
          WriteBlocking { 2U, 2U }, Halt { },
      };
      const std::array<std::uint32_t, 3U> widths { 32U, 32U, 32U };
      jit.add_process(boundary.name, boundary, widths);
      const auto handle = jit.lookup(boundary.name);
      for (const auto& item : cases) {
          TestRuntime native;
          native.signals[0] = encode(integer(item.left));
          native.signals[1] = encode(integer(item.right));
          native.signals[2] = encode(integer(17));
          auto native_abi = abi(native);
          const bool overflow = item.expected < minimum32 || item.expected > maximum32;
          if (overflow) {
              expect_generated_runtime_error(
                  [&] { (void)jit.execute(handle, native_abi); },
                  2U, JitGeneratedRuntimeErrorReason::integer_overflow,
                  "instruction 2: VHDL integer arithmetic overflow");
              assert(native.signals[2] == encode(integer(17)));
          } else {
              assert(jit.execute(handle, native_abi) == JitExecutionStatus::completed);
              assert(native.signals[2]
                  == encode(integer(static_cast<std::int32_t>(item.expected))));
          }
      }
  };
  check_boundaries(IntegerBinaryOperator::add, "_add_boundaries", {
      { maximum32, 0, maximum32 }, { minimum32, 0, minimum32 },
      { maximum32, 1, std::int64_t { maximum32 } + 1 },
      { minimum32, -1, std::int64_t { minimum32 } - 1 },
      { minimum32, maximum32, -1 },
  });
  check_boundaries(IntegerBinaryOperator::subtract, "_subtract_boundaries", {
      { minimum32, 0, minimum32 }, { maximum32, 0, maximum32 },
      { minimum32, 1, std::int64_t { minimum32 } - 1 },
      { maximum32, -1, std::int64_t { maximum32 } + 1 },
      { minimum32, minimum32, 0 },
  });
  check_boundaries(IntegerBinaryOperator::multiply, "_multiply_boundaries", {
      { minimum32, 1, minimum32 }, { maximum32, 1, maximum32 },
      { minimum32, -1, -std::int64_t { minimum32 } },
      { maximum32, 2, std::int64_t { maximum32 } * 2 },
      { -46340, 46340, -INT64_C(2147395600) },
  });

  add_failure(
      "overflow",
      {
          LoadConstant{
              0, integer(std::numeric_limits<std::int32_t>::max())},
          LoadConstant{1, integer(1)},
          IntegerBinary{IntegerBinaryOperator::add, 2, 0, 1},
          Halt{}},
      2,
      JitGeneratedRuntimeErrorReason::integer_overflow,
      "instruction 2: VHDL integer arithmetic overflow");
  add_failure(
      "division_zero",
      {
          LoadConstant{0, integer(7)},
          LoadConstant{1, integer(0)},
          IntegerBinary{IntegerBinaryOperator::divide, 2, 0, 1},
          Halt{}},
      2,
      JitGeneratedRuntimeErrorReason::integer_division_by_zero,
      "instruction 2: VHDL integer division by zero");
  add_failure(
      "negative_exponent",
      {
          LoadConstant{0, integer(2)},
          LoadConstant{1, integer(-1)},
          IntegerBinary{IntegerBinaryOperator::power, 2, 0, 1},
          Halt{}},
      2,
      JitGeneratedRuntimeErrorReason::integer_negative_exponent,
      "instruction 2: VHDL integer exponent must be nonnegative");
  add_failure(
      "range",
      {
          LoadConstant{0, integer(8)},
          IntegerCheck{0, -5, 7},
          Halt{}},
      1,
      JitGeneratedRuntimeErrorReason::integer_subtype_range,
      "instruction 1: VHDL integer subtype range check failed");
  auto unknown = integer(0);
  unknown.set(7, Logic4::x);
  add_failure(
      "unknown",
      {
          LoadConstant{0, std::move(unknown)},
          IntegerUnary{IntegerUnaryOperator::absolute, 1, 0},
          Halt{}},
      1,
      JitGeneratedRuntimeErrorReason::integer_operand_unknown,
      "instruction 1: VHDL integer operand contains an unknown or "
      "high-impedance value");

  const auto make_nested_loop = [&](const std::int32_t initial,
                                    const std::int64_t upper) {
    Process process;
    process.id = 33;
    process.name = "checked_integer_nested_loop";
    process.register_count = 6;
    process.operations = {
        LoadConstant{0, integer(2)},
        LoadConstant{2, integer(1)},
        LoadConstant{3, integer(0)},
        LoadConstant{4, integer(initial)},
        Binary{BinaryOperator::not_equal, 5, 0, 3},
        Branch{5, 6, 15, UnknownBranchPolicy::when_false},
        ReadSignal{1, 0},
        Binary{BinaryOperator::not_equal, 5, 1, 3},
        Branch{5, 9, 13, UnknownBranchPolicy::when_false},
        IntegerBinary{IntegerBinaryOperator::add, 4, 4, 2},
        IntegerCheck{4, 0, upper},
        IntegerBinary{IntegerBinaryOperator::subtract, 1, 1, 2},
        Jump{7},
        IntegerBinary{IntegerBinaryOperator::subtract, 0, 0, 2},
        Jump{4},
        WriteBlocking{1, 4},
        Halt{},
    };
    return process;
  };
  const std::array<std::uint32_t, 2> loop_widths{32, 32};
  const auto loop_symbol = std::string{symbol_prefix} + "_nested_loop";
  jit.add_process(loop_symbol, make_nested_loop(0, 5), loop_widths);
  TestRuntime loop_runtime;
  loop_runtime.signals[0] = EncodedSignal{2, 0};
  auto loop_descriptor = abi(loop_runtime);
  assert(jit.execute(jit.lookup(loop_symbol), loop_descriptor)
         == JitExecutionStatus::completed);
  assert((loop_runtime.signals[1] == EncodedSignal{4, 0}));

  const auto range_symbol = std::string{symbol_prefix} + "_loop_range";
  jit.add_process(range_symbol, make_nested_loop(0, 5), loop_widths);
  TestRuntime range_runtime;
  range_runtime.signals[0] = EncodedSignal{3, 0};
  auto range_descriptor = abi(range_runtime);
  expect_generated_runtime_error(
      [&] {
        (void)jit.execute(jit.lookup(range_symbol), range_descriptor);
      },
      10,
      JitGeneratedRuntimeErrorReason::integer_subtype_range,
      "instruction 10: VHDL integer subtype range check failed");

  const auto overflow_symbol =
      std::string{symbol_prefix} + "_loop_overflow";
  jit.add_process(
      overflow_symbol,
      make_nested_loop(std::numeric_limits<std::int32_t>::max() - 1,
                       std::numeric_limits<std::int32_t>::max()),
      loop_widths);
  TestRuntime overflow_runtime;
  overflow_runtime.signals[0] = EncodedSignal{2, 0};
  auto overflow_descriptor = abi(overflow_runtime);
  expect_generated_runtime_error(
      [&] {
        (void)jit.execute(jit.lookup(overflow_symbol),
                          overflow_descriptor);
      },
      9,
      JitGeneratedRuntimeErrorReason::integer_overflow,
      "instruction 9: VHDL integer arithmetic overflow");
}

[[nodiscard]] Process make_resumable_process() {
  Process process;
  process.id = 0;
  process.name = "resumable";
  process.register_count = 2;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("10100101")},
      WriteBlocking{0, 0},
      WaitFor{5},
      UnaryNot{1, 0},
      WriteBlocking{1, 1},
      Yield{},
      Pause{},
      LoadConstant{0, PackedLogic4::from_msb_string("00111100")},
      WriteBlocking{0, 0},
      Stop{},
  };
  return process;
}

void test_resumable_at_level(const JitOptimizationLevel optimization,
                             const std::string_view symbol_prefix) {
  const auto symbol = std::string{symbol_prefix} + "_resumable";
  const auto process = make_resumable_process();
  const std::array<std::uint32_t, 2> widths{8, 8};
  LlvmJit jit{LlvmJitOptions{optimization, {}}};
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);
  const auto layout = jit.frame_layout(handle);
  assert(layout.register_count == process.register_count);
  assert(layout.layout_id_low != 0 || layout.layout_id_high != 0);

  TestRuntime runtime;
  auto descriptor = abi(runtime);
  expect_unsupported([&] { (void)jit.execute(handle, descriptor); },
                     "compiled process can suspend");
  assert(runtime.writes.empty());

  std::vector<std::uint64_t> register_aval(layout.register_count,
                                           UINT64_MAX);
  std::vector<std::uint64_t> register_bval(layout.register_count,
                                           UINT64_MAX);
  std::vector<std::uint8_t> register_initialized(
      layout.register_count, UINT8_MAX);
  fsim_jit_frame_v2 frame{};
  jit.initialize_frame(
      handle, frame, register_aval, register_bval,
      register_initialized);
  assert(std::all_of(register_aval.begin(), register_aval.end(),
                     [](const auto value) { return value == 0; }));
  assert(std::all_of(register_bval.begin(), register_bval.end(),
                     [](const auto value) { return value == 0; }));
  assert(std::all_of(
      register_initialized.begin(), register_initialized.end(),
      [](const auto value) { return value == 0; }));
  assert(frame.layout_id_low == layout.layout_id_low);
  assert(frame.layout_id_high == layout.layout_id_high);
  assert(frame.program_counter == 0);
  assert(frame.state == FSIM_JIT_FRAME_STATE_READY_V2);

  auto result = new_resume_result();
  {
    auto wrong = frame;
    wrong.abi_version = 99;
    expect_error(
        [&] { (void)jit.resume(handle, descriptor, wrong, result); },
        "frame ABI version mismatch");
  }
  {
    auto wrong = frame;
    wrong.struct_size =
        static_cast<std::uint32_t>(
            offsetof(fsim_jit_frame_v2, register_logic9_plane2) - 1U);
    expect_error(
        [&] { (void)jit.resume(handle, descriptor, wrong, result); },
        "frame ABI structure is too small");
  }
  {
    auto wrong = frame;
    wrong.layout_id_low ^= UINT64_C(1);
    expect_error(
        [&] { (void)jit.resume(handle, descriptor, wrong, result); },
        "frame layout mismatch");
  }
  {
    auto wrong = frame;
    ++wrong.register_count;
    expect_error(
        [&] { (void)jit.resume(handle, descriptor, wrong, result); },
        "frame layout mismatch");
  }
  {
    auto wrong = frame;
    wrong.register_aval = nullptr;
    expect_error(
        [&] { (void)jit.resume(handle, descriptor, wrong, result); },
        "frame register storage is null");
  }
  {
    auto wrong = frame;
    wrong.register_initialized = nullptr;
    expect_error(
        [&] { (void)jit.resume(handle, descriptor, wrong, result); },
        "frame register storage is null");
  }
  {
    auto wrong = frame;
    wrong.program_counter =
        static_cast<std::uint32_t>(process.operations.size());
    expect_error(
        [&] { (void)jit.resume(handle, descriptor, wrong, result); },
        "program counter is outside");
  }
  {
    auto wrong = frame;
    wrong.state = 99;
    expect_error(
        [&] { (void)jit.resume(handle, descriptor, wrong, result); },
        "frame state is invalid");
  }
  {
    auto wrong_result = result;
    wrong_result.abi_version = 99;
    expect_error(
        [&] { (void)jit.resume(handle, descriptor, frame, wrong_result); },
        "resume-result ABI version mismatch");
  }
  {
    auto wrong_result = result;
    wrong_result.struct_size =
        static_cast<std::uint32_t>(
            sizeof(fsim_jit_resume_result_v2) - 1U);
    expect_error(
        [&] { (void)jit.resume(handle, descriptor, frame, wrong_result); },
        "resume-result ABI structure is too small");
  }
  {
    std::array<std::uint64_t, 1> too_small_aval{};
    std::array<std::uint64_t, 1> too_small_bval{};
    std::array<std::uint8_t, 1> too_small_initialized{};
    fsim_jit_frame_v2 unused{};
    expect_error(
        [&] {
          jit.initialize_frame(
              handle, unused, too_small_aval, too_small_bval,
              too_small_initialized);
        },
        "register storage is smaller");
  }
  {
    std::vector<std::uint64_t> aliased(layout.register_count);
    std::vector<std::uint8_t> initialized(layout.register_count);
    fsim_jit_frame_v2 unused{};
    expect_error(
        [&] {
          jit.initialize_frame(
              handle, unused, aliased, aliased, initialized);
        },
        "must be distinct");
  }

  assert(jit.resume(handle, descriptor, frame, result) ==
         JitResumeStatus::wait_for);
  assert(result.status == FSIM_JIT_RESUME_STATUS_WAIT_FOR_V2);
  assert(result.instruction == 2);
  assert(result.delay == 5);
  assert(frame.program_counter == 3);
  assert(frame.last_instruction == 2);
  assert(register_aval[0] == UINT64_C(0xa5));
  assert(register_bval[0] == 0);
  assert(register_initialized[0] == 1);
  assert(register_initialized[1] == 0);

  assert(jit.resume(handle, descriptor, frame, result) ==
         JitResumeStatus::yielded);
  assert(result.status == FSIM_JIT_RESUME_STATUS_YIELDED_V2);
  assert(result.instruction == 5);
  assert(result.delay == 0);
  assert(frame.program_counter == 6);
  assert(register_aval[1] == UINT64_C(0x5a));
  assert(register_bval[1] == 0);
  assert(register_initialized[0] == 1);
  assert(register_initialized[1] == 1);

  assert(jit.resume(handle, descriptor, frame, result) ==
         JitResumeStatus::paused);
  assert(result.status == FSIM_JIT_RESUME_STATUS_PAUSED_V2);
  assert(result.instruction == 6);
  assert(result.delay == 0);
  assert(frame.program_counter == 7);
  assert(frame.state == FSIM_JIT_FRAME_STATE_READY_V2);

  assert(jit.resume(handle, descriptor, frame, result) ==
         JitResumeStatus::stopped);
  assert(result.status == FSIM_JIT_RESUME_STATUS_STOPPED_V2);
  assert(result.instruction == 9);
  assert(frame.program_counter == process.operations.size());
  assert(frame.state == FSIM_JIT_FRAME_STATE_STOPPED_V2);
  const auto writes_before_terminal_resume = runtime.writes.size();
  assert(jit.resume(handle, descriptor, frame, result) ==
         JitResumeStatus::stopped);
  assert(runtime.writes.size() == writes_before_terminal_resume);

  fsim::runtime::simir::Interpreter interpreter;
  (void)interpreter.add_signal(
      {"result0", PackedLogic4::from_msb_string("00000000")});
  (void)interpreter.add_signal(
      {"result1", PackedLogic4::from_msb_string("00000000")});
  std::vector<std::pair<std::uint32_t, EncodedSignal>> reference_writes;
  interpreter.set_signal_change_hook(
      [&](const SignalId signal, const PackedLogic4 &value,
          const fsim::runtime::SimulationTick) {
        reference_writes.emplace_back(signal, encode(value));
      });
  (void)interpreter.add_process(process);
  const auto paused_result = interpreter.run();
  assert(paused_result.status == fsim::runtime::RunStatus::stopped);
  assert(paused_result.time == 5);
  assert(!interpreter.stopped_by_design());
  interpreter.scheduler().clear_stop();
  const auto reference_result = interpreter.run();
  assert(reference_result.status == fsim::runtime::RunStatus::stopped);
  assert(interpreter.stopped_by_design());
  assert(runtime.writes == reference_writes);
  assert(encode(interpreter.signal_value(0)) == runtime.signals[0]);
  assert(encode(interpreter.signal_value(1)) == runtime.signals[1]);

  Process terminal_stop;
  terminal_stop.id = 1;
  terminal_stop.name = "terminal_stop";
  terminal_stop.operations = {Stop{}};
  const auto stop_symbol = std::string{symbol_prefix} + "_stop";
  const std::array<std::uint32_t, 0> no_signals{};
  jit.add_process(stop_symbol, terminal_stop, no_signals);
  assert(jit.execute(jit.lookup(stop_symbol), descriptor) ==
         JitExecutionStatus::stopped);

  Process safe_loop;
  safe_loop.id = 2;
  safe_loop.name = "safe_loop";
  safe_loop.register_count = 1;
  safe_loop.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("0")},
      WaitFor{1},
      UnaryNot{0, 0},
      WriteBlocking{0, 0},
      Yield{},
      Jump{1},
  };
  const auto loop_symbol = std::string{symbol_prefix} + "_safe_loop";
  const std::array<std::uint32_t, 1> scalar_width{1};
  jit.add_process(loop_symbol, safe_loop, scalar_width);
  const auto loop_handle = jit.lookup(loop_symbol);
  const auto loop_layout = jit.frame_layout(loop_handle);
  std::vector<std::uint64_t> loop_aval(loop_layout.register_count);
  std::vector<std::uint64_t> loop_bval(loop_layout.register_count);
  std::vector<std::uint8_t> loop_initialized(
      loop_layout.register_count);
  fsim_jit_frame_v2 loop_frame{};
  jit.initialize_frame(
      loop_handle, loop_frame, loop_aval, loop_bval,
      loop_initialized);
  auto loop_result = new_resume_result();
  TestRuntime loop_runtime;
  auto loop_descriptor = abi(loop_runtime);
  assert(jit.resume(
             loop_handle, loop_descriptor, loop_frame, loop_result) ==
         JitResumeStatus::wait_for);
  assert(jit.resume(
             loop_handle, loop_descriptor, loop_frame, loop_result) ==
         JitResumeStatus::yielded);
  assert((loop_runtime.signals[0] == EncodedSignal{1, 0}));
  assert(jit.resume(
             loop_handle, loop_descriptor, loop_frame, loop_result) ==
         JitResumeStatus::wait_for);
  assert(jit.resume(
             loop_handle, loop_descriptor, loop_frame, loop_result) ==
         JitResumeStatus::yielded);
  assert((loop_runtime.signals[0] == EncodedSignal{0, 0}));
}

void test_process_cohort_resume_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol_prefix)
{
  LlvmJit jit { LlvmJitOptions { optimization, { } } };
  const std::array<std::uint32_t, 2> widths { 1U, 1U };
  std::array<JitProcessHandle, 2> handles;
  for (std::size_t index = 0; index < handles.size(); ++index) {
    Process process;
    process.id = static_cast<ProcessId>(index);
    process.name = "cohort_" + std::to_string(index);
    process.register_count = 1U;
    process.static_sensitivity.push_back(
        { 0U, EdgeKind::posedge });
    process.operations = {
        LoadConstant { 0U, PackedLogic4::from_msb_string("1") },
        WriteBlocking { static_cast<SignalId>(index), 0U },
        WaitSensitivity { },
        Jump { 0U },
    };
    const auto symbol = std::string { symbol_prefix } + "_cohort_"
        + std::to_string(index);
    // Separate modules exercise a wrapper spanning hierarchy/module ownership.
    jit.add_process(symbol, process, widths);
    handles[index] = jit.lookup(symbol);
  }

  struct Frame {
    std::vector<std::uint64_t> aval;
    std::vector<std::uint64_t> bval;
    std::vector<std::uint8_t> initialized;
    fsim_jit_frame_v2 value { };
  };
  std::array<Frame, 2> frames;
  for (std::size_t index = 0; index < frames.size(); ++index) {
    const auto layout = jit.frame_layout(handles[index]);
    frames[index].aval.resize(layout.register_word_count);
    frames[index].bval.resize(layout.register_word_count);
    frames[index].initialized.resize(layout.register_count);
    jit.initialize_frame(
        handles[index], frames[index].value,
        frames[index].aval, frames[index].bval,
        frames[index].initialized);
  }

  std::array<TestRuntime, 2> runtimes;
  std::array<fsim_jit_runtime_instance_v2, 2> descriptors {
      abi(runtimes[0]), abi(runtimes[1])
  };
  std::array<fsim_jit_resume_result_v2, 2> results {
      new_resume_result(), new_resume_result()
  };
  std::array<std::uint8_t, 2> queued { 1U, 1U };
  std::array<std::uint8_t, 2> waiting { 1U, 1U };
  std::array<std::uint8_t, 2> process_status { 2U, 2U };
  std::array<fsim::compiler::JitProcessCohortResumeEntry, 2> cohort {
      fsim::compiler::JitProcessCohortResumeEntry {
          jit.bind(handles[0]), descriptors[0],
          frames[0].value, results[0], &queued[0], &waiting[0],
          &process_status[0] },
      fsim::compiler::JitProcessCohortResumeEntry {
          jit.bind(handles[1]), descriptors[1],
          frames[1].value, results[1], &queued[1], &waiting[1],
          &process_status[1] },
  };
  assert(jit.resume_cohort_prevalidated(cohort) == cohort.size());
  for (std::size_t index = 0; index < cohort.size(); ++index) {
    assert(cohort[index].status
           == FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY_V2);
    assert(results[index].instruction == 2U);
    assert(frames[index].value.program_counter == 3U);
    assert((runtimes[index].signals[index] == EncodedSignal { 1U, 0U }));
    assert(queued[index] == 0U);
    assert(waiting[index] == 1U);
    assert(process_status[index] == 2U);
  }

  const auto bound_cohort = jit.bind_cohort_prevalidated(cohort);
  assert(bound_cohort);
  runtimes[0].signals[0] = { 0U, 0U };
  runtimes[1].signals[1] = { 0U, 0U };
  results = { new_resume_result(), new_resume_result() };
  queued = { 1U, 1U };
  waiting = { 1U, 1U };
  process_status = { 2U, 2U };
  assert(jit.resume_cohort_prevalidated(bound_cohort, cohort)
         == cohort.size());
  for (std::size_t index = 0; index < cohort.size(); ++index) {
    assert(cohort[index].status
           == FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY_V2);
    assert(results[index].instruction == 2U);
    assert(frames[index].value.program_counter == 3U);
    assert((runtimes[index].signals[index] == EncodedSignal { 1U, 0U }));
    assert(queued[index] == 0U);
    assert(waiting[index] == 1U);
    assert(process_status[index] == 2U);
  }

  runtimes[0].signals[0] = { 0U, 0U };
  runtimes[1].signals[1] = { 0U, 0U };
  results = { new_resume_result(), new_resume_result() };
  queued = { 1U, 1U };
  waiting = { 1U, 1U };
  process_status = { 2U, 2U };
}

void test_ordered_cohort_cache_budget()
{
  constexpr std::size_t process_count = 6U;
  constexpr std::size_t maximum_variants = 16U;
  constexpr std::size_t capped_miss_variant = maximum_variants;
  constexpr std::size_t maximum_members = 64U;
  const std::array<std::uint32_t, 1> widths { 129U };
  LlvmJit jit { LlvmJitOptions { JitOptimizationLevel::o0, { } } };
  std::array<JitProcessHandle, process_count> handles;
  for (std::size_t index = 0U; index < handles.size(); ++index) {
    Process process;
    process.id = static_cast<ProcessId>(index);
    process.name = "bounded_ordered_cohort_" + std::to_string(index);
    process.register_count = 1U;
    process.static_sensitivity = { { 0U, EdgeKind::any } };
    process.operations = {
        ReadSignal { 0U, 0U },
        WaitSensitivity { },
        Jump { 0U },
    };
    const auto symbol = process.name;
    jit.add_process(symbol, process, widths);
    handles[index] = jit.lookup(symbol);
    assert(handles[index]);
  }

  struct FrameState {
    std::vector<std::uint64_t> aval;
    std::vector<std::uint64_t> bval;
    std::vector<std::uint8_t> initialized;
    fsim_jit_frame_v2 frame { };
    fsim_jit_resume_result_v2 result { };
  };
  std::array<FrameState, process_count> frames;
  for (std::size_t index = 0U; index < frames.size(); ++index) {
    const auto layout = jit.frame_layout(handles[index]);
    frames[index].aval.resize(layout.register_word_count);
    frames[index].bval.resize(layout.register_word_count);
    frames[index].initialized.resize(layout.register_count);
    jit.initialize_frame(
        handles[index], frames[index].frame,
        frames[index].aval, frames[index].bval,
        frames[index].initialized);
    frames[index].result = new_resume_result();
  }

  TestRuntime runtime;
  runtime.wide_signal_aval[0U] = { 0U, 0U, 0U };
  runtime.wide_signal_bval[0U] = { 0U, 0U, 0U };
  const auto descriptor = abi(runtime);
  const auto same_result = [](
      const fsim_jit_resume_result_v2& lhs,
      const fsim_jit_resume_result_v2& rhs) {
    return lhs.abi_version == rhs.abi_version
        && lhs.struct_size == rhs.struct_size
        && lhs.status == rhs.status
        && lhs.instruction == rhs.instruction
        && lhs.delay == rhs.delay;
  };
  std::array<std::uint8_t, 2> queued { 1U, 1U };
  std::array<std::uint8_t, 2> waiting { 0U, 0U };
  std::array<std::uint8_t, 2> process_status { 0U, 0U };
  const auto reset_member = [&](const std::size_t index) {
    jit.initialize_frame(
        handles[index], frames[index].frame,
        frames[index].aval, frames[index].bval,
        frames[index].initialized);
    frames[index].result = new_resume_result();
  };
  const auto make_entries = [&](const std::size_t first,
                                const std::size_t second) {
    return std::array<fsim::compiler::JitProcessCohortResumeEntry, 2> {
        fsim::compiler::JitProcessCohortResumeEntry {
            jit.bind(handles[first]), descriptor,
            frames[first].frame, frames[first].result,
            &queued[0], &waiting[0], &process_status[0] },
        fsim::compiler::JitProcessCohortResumeEntry {
            jit.bind(handles[second]), descriptor,
            frames[second].frame, frames[second].result,
            &queued[1], &waiting[1], &process_status[1] },
    };
  };

  // More than 64 members decline before frame or scheduler-state mutation and
  // do not spend one of the ordered wrapper materialization attempts.
  std::array<fsim::compiler::JitProcessCohortResumeEntry,
      maximum_members + 1U> oversized;
  std::array<std::uint8_t, maximum_members + 1U> large_queued;
  std::array<std::uint8_t, maximum_members + 1U> large_waiting;
  std::array<std::uint8_t, maximum_members + 1U> large_status;
  large_queued.fill(1U);
  large_waiting.fill(0U);
  large_status.fill(0U);
  auto& oversized_frame = frames[0];
  const auto full_frame_before = oversized_frame.frame;
  const auto frame_pc_before = oversized_frame.frame.program_counter;
  const auto frame_state_before = oversized_frame.frame.state;
  const auto register_aval_before = oversized_frame.aval;
  const auto register_bval_before = oversized_frame.bval;
  const auto register_initialized_before = oversized_frame.initialized;
  const auto result_before = oversized_frame.result;
  for (std::size_t index = 0U; index < oversized.size(); ++index) {
    oversized[index] = fsim::compiler::JitProcessCohortResumeEntry {
        jit.bind(handles[0]), descriptor,
        oversized_frame.frame, oversized_frame.result,
        &large_queued[index], &large_waiting[index], &large_status[index] };
    oversized[index].status = UINT32_C(0xdeadbeef);
  }
  assert(jit.resume_ordered_cohort_prevalidated(oversized) == 0U);
  assert(oversized_frame.frame.program_counter == frame_pc_before);
  assert(oversized_frame.frame.state == frame_state_before);
  assert(oversized_frame.frame.abi_version == full_frame_before.abi_version);
  assert(oversized_frame.frame.struct_size == full_frame_before.struct_size);
  assert(oversized_frame.frame.layout_id_low
      == full_frame_before.layout_id_low);
  assert(oversized_frame.frame.layout_id_high
      == full_frame_before.layout_id_high);
  assert(oversized_frame.frame.register_count
      == full_frame_before.register_count);
  assert(oversized_frame.frame.last_instruction
      == full_frame_before.last_instruction);
  assert(oversized_frame.frame.register_aval
      == full_frame_before.register_aval);
  assert(oversized_frame.frame.register_bval
      == full_frame_before.register_bval);
  assert(oversized_frame.frame.register_initialized
      == full_frame_before.register_initialized);
  assert(oversized_frame.frame.register_logic9_plane2
      == full_frame_before.register_logic9_plane2);
  assert(oversized_frame.frame.register_logic9_plane3
      == full_frame_before.register_logic9_plane3);
  assert(oversized_frame.frame.native_call_depth
      == full_frame_before.native_call_depth);
  assert(oversized_frame.frame.native_call_reserved
      == full_frame_before.native_call_reserved);
  assert(std::ranges::equal(
      oversized_frame.frame.native_return_stack,
      full_frame_before.native_return_stack));
  assert(oversized_frame.aval == register_aval_before);
  assert(oversized_frame.bval == register_bval_before);
  assert(oversized_frame.initialized == register_initialized_before);
  assert(same_result(oversized_frame.result, result_before));
  assert(std::ranges::all_of(oversized, [](const auto& entry) {
    return entry.status == UINT32_C(0xdeadbeef) && !entry.failure;
  }));
  assert(std::ranges::all_of(large_queued, [](const auto value) {
    return value == 1U;
  }));
  assert(std::ranges::all_of(large_waiting, [](const auto value) {
    return value == 0U;
  }));
  assert(std::ranges::all_of(large_status, [](const auto value) {
    return value == 0U;
  }));

  std::array<std::array<std::size_t, 2>, maximum_variants + 1U> variants { };
  std::size_t variant_count { };
  for (std::size_t first = 0U;
      first < process_count && variant_count < variants.size(); ++first) {
    for (std::size_t second = 0U;
        second < process_count && variant_count < variants.size(); ++second) {
      if (first != second) {
        variants[variant_count++] = { first, second };
      }
    }
  }
  assert(variant_count == variants.size());

  for (std::size_t index = 0U; index < maximum_variants; ++index) {
    const auto [first, second] = variants[index];
    reset_member(first);
    reset_member(second);
    queued = { 1U, 1U };
    waiting = { 0U, 0U };
    process_status = { 0U, 0U };
    auto entries = make_entries(first, second);
    assert(jit.resume_ordered_cohort_prevalidated(entries)
        == entries.size());
    assert(entries[0].status == FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY_V2);
    assert(entries[1].status == FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY_V2);
    assert(frames[first].frame.program_counter == 2U);
    assert(frames[second].frame.program_counter == 2U);
  }
  assert(runtime.packed_signal_reads == maximum_variants * 2U);

  const auto [miss_first, miss_second] = variants[capped_miss_variant];
  reset_member(miss_first);
  reset_member(miss_second);
  queued = { 1U, 1U };
  waiting = { 0U, 0U };
  process_status = { 0U, 0U };
  auto invalid_entries = make_entries(miss_first, miss_second);
  const auto first_frame_before = frames[miss_first].frame;
  const auto first_aval_before = frames[miss_first].aval;
  const auto first_bval_before = frames[miss_first].bval;
  const auto first_initialized_before = frames[miss_first].initialized;
  const auto first_result_before = frames[miss_first].result;
  const auto second_struct_size = frames[miss_second].frame.struct_size;
  const auto reads_before_validation = runtime.packed_signal_reads;
  invalid_entries[0].status = UINT32_C(0xcafef00d);
  invalid_entries[1].status = UINT32_C(0xcafef00d);
  frames[miss_second].frame.struct_size = 0U;
  expect_fatal_error(
      [&] { (void)jit.resume_ordered_cohort_prevalidated(invalid_entries); },
      "frame ABI structure is too small");
  frames[miss_second].frame.struct_size = second_struct_size;
  assert(runtime.packed_signal_reads == reads_before_validation);
  assert(frames[miss_first].frame.program_counter
      == first_frame_before.program_counter);
  assert(frames[miss_first].frame.state == first_frame_before.state);
  assert(frames[miss_first].aval == first_aval_before);
  assert(frames[miss_first].bval == first_bval_before);
  assert(frames[miss_first].initialized == first_initialized_before);
  assert(same_result(frames[miss_first].result, first_result_before));
  assert(invalid_entries[0].status == UINT32_C(0xcafef00d));
  assert(invalid_entries[1].status == UINT32_C(0xcafef00d));
  assert(!invalid_entries[0].failure && !invalid_entries[1].failure);
  assert(queued == (std::array<std::uint8_t, 2> { 1U, 1U }));
  assert(waiting == (std::array<std::uint8_t, 2> { 0U, 0U }));
  assert(process_status == (std::array<std::uint8_t, 2> { 0U, 0U }));

  // The seventeenth distinct ordered pair executes its validated members
  // directly after the wrapper budget is full.
  auto fallback_entries = make_entries(miss_first, miss_second);
  const auto reads_before_fallback = runtime.packed_signal_reads;
  assert(jit.resume_ordered_cohort_prevalidated(fallback_entries)
      == fallback_entries.size());
  assert(runtime.packed_signal_reads == reads_before_fallback + 2U);
  assert(fallback_entries[0].status
      == FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY_V2);
  assert(fallback_entries[1].status
      == FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY_V2);
  assert(frames[miss_first].frame.program_counter == 2U);
  assert(frames[miss_second].frame.program_counter == 2U);
  assert(queued == (std::array<std::uint8_t, 2> { 0U, 0U }));
  assert(waiting == (std::array<std::uint8_t, 2> { 1U, 1U }));
  assert(process_status == (std::array<std::uint8_t, 2> { 2U, 2U }));

  // These ABI-level probes test accepted-prefix accounting; they do not
  // certify application SV-active wave admission or semantics.
  Process stop_process;
  stop_process.id = static_cast<ProcessId>(process_count);
  stop_process.name = "bounded_ordered_stop";
  stop_process.register_count = 1U;
  stop_process.operations = {
      LoadConstant { 0U, PackedLogic4::from_msb_string("1") }, Stop { }
  };
  constexpr std::string_view stop_symbol = "bounded_ordered_stop";
  jit.add_process(stop_symbol, stop_process, widths);
  const auto stop_handle = jit.lookup(stop_symbol);
  FrameState stop_frame;
  const auto initialize_frame = [&](const JitProcessHandle handle,
                                    FrameState& state) {
    const auto layout = jit.frame_layout(handle);
    state.aval.resize(layout.register_word_count);
    state.bval.resize(layout.register_word_count);
    state.initialized.resize(layout.register_count);
    jit.initialize_frame(
        handle, state.frame, state.aval, state.bval, state.initialized);
    state.result = new_resume_result();
  };
  initialize_frame(stop_handle, stop_frame);
  reset_member(1U);
  queued = { 1U, 1U };
  waiting = { 0U, 0U };
  process_status = { 0U, 0U };
  std::array<fsim::compiler::JitProcessCohortResumeEntry, 2> stop_entries {
      fsim::compiler::JitProcessCohortResumeEntry {
          jit.bind(stop_handle), descriptor, stop_frame.frame,
          stop_frame.result, &queued[0], &waiting[0], &process_status[0] },
      fsim::compiler::JitProcessCohortResumeEntry {
          jit.bind(handles[1]), descriptor, frames[1].frame,
          frames[1].result, &queued[1], &waiting[1], &process_status[1] },
  };
  stop_entries[1].status = UINT32_C(0xdeadbeef);
  const auto suffix_frame_before_stop = frames[1].frame;
  const auto suffix_aval_before_stop = frames[1].aval;
  const auto suffix_bval_before_stop = frames[1].bval;
  const auto suffix_initialized_before_stop = frames[1].initialized;
  const auto suffix_result_before_stop = frames[1].result;
  const auto reads_before_stop = runtime.packed_signal_reads;
  assert(jit.resume_ordered_cohort_prevalidated(stop_entries) == 1U);
  assert(stop_entries[0].status == FSIM_JIT_RESUME_STATUS_STOPPED_V2);
  assert(!stop_entries[0].failure);
  assert(runtime.packed_signal_reads == reads_before_stop);
  assert(frames[1].frame.program_counter
      == suffix_frame_before_stop.program_counter);
  assert(frames[1].frame.state == suffix_frame_before_stop.state);
  assert(frames[1].aval == suffix_aval_before_stop);
  assert(frames[1].bval == suffix_bval_before_stop);
  assert(frames[1].initialized == suffix_initialized_before_stop);
  assert(same_result(frames[1].result, suffix_result_before_stop));
  assert(stop_entries[1].status == UINT32_C(0xdeadbeef));
  assert(!stop_entries[1].failure);
  assert(queued == (std::array<std::uint8_t, 2> { 0U, 1U }));
  assert(waiting == (std::array<std::uint8_t, 2> { 0U, 0U }));
  assert(process_status == (std::array<std::uint8_t, 2> { 1U, 0U }));

  Process error_process;
  error_process.id = static_cast<ProcessId>(process_count + 1U);
  error_process.name = "bounded_ordered_runtime_error";
  error_process.register_count = 1U;
  error_process.operations = { ReadSignal { 0U, 0U }, Halt { } };
  constexpr std::string_view error_symbol = "bounded_ordered_runtime_error";
  jit.add_process(error_symbol, error_process, widths);
  const auto error_handle = jit.lookup(error_symbol);
  FrameState error_frame;
  initialize_frame(error_handle, error_frame);
  reset_member(0U);
  reset_member(1U);
  TestRuntime failing_runtime;
  auto failing_descriptor = abi(failing_runtime);
  auto failing_services = copy_jit_services(failing_descriptor);
  // ABI v2 forbids callbacks from unwinding across the generated C boundary;
  // a nonzero return exercises its supported runtime-error path.
  failing_services.read_signal_packed = [](
      void* context, std::uint32_t, std::uint32_t, std::uint64_t*,
      std::uint64_t*, std::uint64_t*, std::uint64_t*) -> std::uint32_t {
    auto& failed = *static_cast<TestRuntime*>(context);
    ++failed.packed_signal_reads;
    return 1U;
  };
  failing_descriptor.services = &failing_services;
  std::array<std::uint8_t, 3> error_queued { 1U, 1U, 1U };
  std::array<std::uint8_t, 3> error_waiting { 0U, 0U, 0U };
  std::array<std::uint8_t, 3> error_process_status { 0U, 0U, 0U };
  std::array<fsim::compiler::JitProcessCohortResumeEntry, 3> error_entries {
      fsim::compiler::JitProcessCohortResumeEntry {
          jit.bind(handles[0]), descriptor, frames[0].frame,
          frames[0].result, &error_queued[0], &error_waiting[0],
          &error_process_status[0] },
      fsim::compiler::JitProcessCohortResumeEntry {
          jit.bind(error_handle), failing_descriptor, error_frame.frame,
          error_frame.result, &error_queued[1], &error_waiting[1],
          &error_process_status[1] },
      fsim::compiler::JitProcessCohortResumeEntry {
          jit.bind(handles[1]), descriptor, frames[1].frame,
          frames[1].result, &error_queued[2], &error_waiting[2],
          &error_process_status[2] },
  };
  error_entries[2].status = UINT32_C(0xdeadbeef);
  const auto suffix_frame_before_error = frames[1].frame;
  const auto suffix_aval_before_error = frames[1].aval;
  const auto suffix_bval_before_error = frames[1].bval;
  const auto suffix_initialized_before_error = frames[1].initialized;
  const auto suffix_result_before_error = frames[1].result;
  const auto reads_before_error = runtime.packed_signal_reads;
  assert(jit.resume_ordered_cohort_prevalidated(error_entries) == 2U);
  assert(error_entries[0].status
      == FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY_V2);
  assert(!error_entries[0].failure);
  assert(frames[0].frame.program_counter == 2U);
  assert(error_entries[1].status
      == FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR_V2);
  assert(error_entries[1].failure);
  assert(failing_runtime.packed_signal_reads == 1U);
  assert(runtime.packed_signal_reads == reads_before_error + 1U);
  assert(frames[1].frame.program_counter
      == suffix_frame_before_error.program_counter);
  assert(frames[1].frame.state == suffix_frame_before_error.state);
  assert(frames[1].aval == suffix_aval_before_error);
  assert(frames[1].bval == suffix_bval_before_error);
  assert(frames[1].initialized == suffix_initialized_before_error);
  assert(same_result(frames[1].result, suffix_result_before_error));
  assert(error_entries[2].status == UINT32_C(0xdeadbeef));
  assert(!error_entries[2].failure);
  assert((error_queued == std::array<std::uint8_t, 3> { 0U, 0U, 1U }));
  assert((error_waiting == std::array<std::uint8_t, 3> { 1U, 0U, 0U }));
  assert((error_process_status
      == std::array<std::uint8_t, 3> { 2U, 1U, 0U }));

  // Exact existing member order remains reusable after the new-variant cap.
  const auto [cached_first, cached_second] = variants[0];
  reset_member(cached_first);
  reset_member(cached_second);
  queued = { 1U, 1U };
  waiting = { 0U, 0U };
  process_status = { 0U, 0U };
  auto cached_entries = make_entries(cached_first, cached_second);
  const auto reads_before_cached_reuse = runtime.packed_signal_reads;
  assert(jit.resume_ordered_cohort_prevalidated(cached_entries)
      == cached_entries.size());
  assert(runtime.packed_signal_reads == reads_before_cached_reuse + 2U);
  assert(frames[cached_first].frame.program_counter == 2U);
  assert(frames[cached_second].frame.program_counter == 2U);
}

void test_class_service_boundaries_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol) {
  Process process;
  process.id = 0;
  process.name = std::string{symbol};
  process.register_count = 6;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_aval_bval(32, 7, 0)},
      ClassAllocate{
          1, "work::Item<WIDTH=130>", "work::Item", {0}, {0}, {"value"}},
      ClassPropertyRead{2, 1, "work::Item::value", 130},
      ClassPropertyWrite{1, 2, "work::Item::value"},
      ClassStaticPropertyRead{3, "work::Item::shared", 130},
      ClassStaticPropertyWrite{3, "work::Item::shared"},
      ProcessSelf{1},
      ProcessStatusQuery{4, 1},
      ProcessCompleted{5, 1},
      ProcessAwait{1},
      ProcessKill{1},
      Halt{},
  };
  const std::array<std::uint32_t, 0> widths{};
  LlvmJit jit{LlvmJitOptions{optimization, {}}};
  assert(jit.supports_process(process, widths));
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);
  const auto layout = jit.frame_layout(handle);
  std::vector<std::uint64_t> register_aval(layout.register_word_count);
  std::vector<std::uint64_t> register_bval(layout.register_word_count);
  std::vector<std::uint8_t> register_initialized(layout.register_count);
  fsim_jit_frame_v2 frame{};
  jit.initialize_frame(
      handle,
      frame,
      register_aval,
      register_bval,
      register_initialized);
  TestRuntime runtime;
  runtime.native_service_word_count = layout.register_word_count;
  const std::array<std::uint64_t, 3> wide_aval {
      UINT64_C(0x0123456789abcdef),
      UINT64_C(0xfedcba9876543210),
      UINT64_C(0x2)};
  const std::array<std::uint64_t, 3> wide_bval { };
  const auto wide_value = PackedLogic4::from_word_planes(
      130U, wide_aval, wide_bval);
  const std::array<std::uint64_t, 3> small_aval {
      UINT64_C(0xfedcba9876543210),
      UINT64_C(0x0123456789abcdef),
      UINT64_C(0x1)};
  const std::array<std::uint64_t, 3> small_bval {
      UINT64_C(0x0000000000000001), 0, 0};
  const auto small_value = PackedLogic4::from_word_planes(
      130U, small_aval, small_bval);
  runtime.class_property_reads = {
      NativeServiceRegister {
          2U, 2U, layout.register_word_offsets[2], 130U, small_value },
      NativeServiceRegister {
          4U, 3U, layout.register_word_offsets[3], 130U, wide_value },
  };
  runtime.class_property_writes = {
      NativeServiceRegister {
          3U, 2U, layout.register_word_offsets[2], 130U, PackedLogic4 { } },
      NativeServiceRegister {
          5U, 3U, layout.register_word_offsets[3], 130U, PackedLogic4 { } },
  };
  auto descriptor = abi(runtime);
  auto result = new_resume_result();

  assert(jit.resume(handle, descriptor, frame, result)
         == JitResumeStatus::simir_boundary);
  assert(result.instruction == 1 && frame.program_counter == 2);
  assert(register_initialized[0] == 1 && register_aval[0] == 7);
  register_aval[1] = UINT64_C(0x0000000100000001);
  register_bval[1] = 0;
  register_initialized[1] = 1;

  assert(jit.resume(handle, descriptor, frame, result)
         == JitResumeStatus::simir_boundary);
  assert(result.instruction == 6 && frame.program_counter == 7);
  assert(runtime.class_property_operation_calls == 4U);
  assert(register_initialized[2] == 1U);
  assert(std::ranges::equal(
      std::span<const std::uint64_t> {
          register_aval.data() + layout.register_word_offsets[2], 3U },
      small_value.aval_words()));
  assert(register_initialized[3] == 1U);
  assert(std::ranges::equal(
      std::span<const std::uint64_t> {
          register_aval.data() + layout.register_word_offsets[3], 3U },
      wide_value.aval_words()));
  assert(runtime.class_property_write_values.size() == 2U);
  assert(runtime.class_property_write_values[0] == small_value);
  assert(runtime.class_property_write_values[1] == wide_value);
  const std::array<std::pair<std::uint32_t, std::uint32_t>, 4>
      expected_class_calls { {
          { 2U, 2U }, { 2U, 3U }, { 2U, 4U }, { 2U, 5U }
      } };
  assert(std::ranges::equal(
      runtime.native_service_calls, expected_class_calls));
  for (std::uint32_t instruction = 7; instruction <= 10; ++instruction) {
    assert(jit.resume(handle, descriptor, frame, result)
           == JitResumeStatus::simir_boundary);
    assert(
        result.instruction == instruction
        && frame.program_counter == instruction + 1U);
  }
  assert(jit.resume(handle, descriptor, frame, result)
         == JitResumeStatus::completed);
  assert(result.instruction == 11 && frame.program_counter == 12);

  auto old_descriptor = descriptor;
  auto old_services = copy_jit_services(old_descriptor);
  old_services.execute_class_property_operation = nullptr;
  old_descriptor.services = &old_services;
  jit.initialize_frame(
      handle, frame, register_aval, register_bval, register_initialized);
  expect_error(
      [&] { (void)jit.resume(handle, old_descriptor, frame, result); },
      "require class-property callbacks");

  Process malformed;
  malformed.id = 1;
  malformed.name = std::string{symbol} + "_malformed";
  malformed.register_count = 2;
  malformed.operations = {
      LoadConstant{0, PackedLogic4::from_aval_bval(64, 0, 0)},
      ClassMethodCall{1, 0, "work::Item::call", {0}, {}, {}, 1, false},
      Halt{}};
  const std::array malformed_entry{
      JitProcessModuleEntry{malformed.name, &malformed}};
  expect_fatal_error(
      [&] {
        jit.add_process_module(
            std::string{symbol} + "_malformed_module",
            malformed_entry,
            widths);
      },
      "ClassMethodCall requires aligned method actual metadata");
  expect_error([&] { (void)jit.lookup(malformed.name); }, "was not added");

  Process malformed_handle;
  malformed_handle.id = 2;
  malformed_handle.name = std::string{symbol} + "_malformed_handle";
  malformed_handle.register_count = 1;
  malformed_handle.operations = {ProcessStatusQuery{0, 1}, Halt{}};
  const std::array malformed_handle_entry{
      JitProcessModuleEntry{malformed_handle.name, &malformed_handle}};
  expect_fatal_error(
      [&] {
        jit.add_process_module(
            std::string{symbol} + "_malformed_handle_module",
            malformed_handle_entry,
            widths);
      },
      "source register ID is out of range");
  expect_error(
      [&] { (void)jit.lookup(malformed_handle.name); }, "was not added");
}

void test_native_service_callbacks_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol) {
  using fsim::compiler::JitProcessCohortResumeEntry;

  Process process;
  process.id = 0;
  process.name = std::string { symbol };
  process.register_count = 2;
  const std::array<std::uint64_t, 3> actual_aval {
      UINT64_C(0x1122334455667788),
      UINT64_C(0x8877665544332211),
      UINT64_C(0x2)};
  const std::array<std::uint64_t, 3> actual_bval {
      UINT64_C(0x0000000000000001), 0, 0};
  const auto actual = PackedLogic4::from_word_planes(
      130U, actual_aval, actual_bval);
  process.operations = {
      LoadConstant{0, actual},
      CoverageSample{
          "work::coverage_instance", {0}, {130}, {0},
          {frontend::SystemVerilogScalarKind::None},
          CoverageSampleTrigger::explicit_sample},
      EventTriggered{1, 0},
      Halt{},
  };
  const std::array<std::uint32_t, 1> widths { 1U };
  LlvmJit jit { LlvmJitOptions { optimization, { } } };
  assert(jit.supports_process(process, widths));
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);
  const auto layout = jit.frame_layout(handle);
  std::vector<std::uint64_t> register_aval(layout.register_word_count);
  std::vector<std::uint64_t> register_bval(layout.register_word_count);
  std::vector<std::uint8_t> register_initialized(layout.register_count);
  fsim_jit_frame_v2 frame { };
  jit.initialize_frame(
      handle, frame, register_aval, register_bval, register_initialized);

  TestRuntime runtime;
  runtime.native_service_word_count = layout.register_word_count;
  runtime.coverage_sample_word_offset = layout.register_word_offsets[0];
  runtime.coverage_sample_width = 130U;
  runtime.event_triggered_destination = 1U;
  runtime.event_triggered_word_offset = layout.register_word_offsets[1];
  runtime.event_triggered_value = true;
  auto descriptor = abi(runtime);
  auto result = new_resume_result();
  assert(jit.resume(handle, descriptor, frame, result)
         == JitResumeStatus::completed);
  assert(runtime.coverage_sample_calls == 1U);
  assert(runtime.coverage_sample_values.size() == 1U);
  assert(runtime.coverage_sample_values.front() == actual);
  assert(runtime.event_triggered_calls == 1U);
  const std::array<std::pair<std::uint32_t, std::uint32_t>, 2>
      expected_service_calls { { { 1U, 1U }, { 3U, 2U } } };
  assert(std::ranges::equal(
      runtime.native_service_calls, expected_service_calls));
  assert(register_initialized[1] == 1U && register_aval[
      layout.register_word_offsets[1]] == 1U);

  auto old_descriptor = descriptor;
  auto old_services = copy_jit_services(old_descriptor);
  old_services.sample_coverage = nullptr;
  old_descriptor.services = &old_services;
  const auto frame_before_missing_callback = frame;
  const auto result_before_missing_callback = result;
  const auto register_aval_before_missing_callback = register_aval;
  const auto register_bval_before_missing_callback = register_bval;
  const auto register_initialized_before_missing_callback
      = register_initialized;
  const auto assert_unchanged_after_callback_rejection = [&] {
    assert(frame.abi_version == frame_before_missing_callback.abi_version);
    assert(frame.struct_size == frame_before_missing_callback.struct_size);
    assert(frame.layout_id_low
        == frame_before_missing_callback.layout_id_low);
    assert(frame.layout_id_high
        == frame_before_missing_callback.layout_id_high);
    assert(frame.register_count
        == frame_before_missing_callback.register_count);
    assert(frame.program_counter
        == frame_before_missing_callback.program_counter);
    assert(frame.state == frame_before_missing_callback.state);
    assert(frame.last_instruction
        == frame_before_missing_callback.last_instruction);
    assert(frame.register_aval
        == frame_before_missing_callback.register_aval);
    assert(frame.register_bval
        == frame_before_missing_callback.register_bval);
    assert(frame.register_initialized
        == frame_before_missing_callback.register_initialized);
    assert(frame.register_logic9_plane2
        == frame_before_missing_callback.register_logic9_plane2);
    assert(frame.register_logic9_plane3
        == frame_before_missing_callback.register_logic9_plane3);
    assert(frame.native_call_depth
        == frame_before_missing_callback.native_call_depth);
    assert(frame.native_call_reserved
        == frame_before_missing_callback.native_call_reserved);
    assert(std::ranges::equal(
        frame.native_return_stack,
        frame_before_missing_callback.native_return_stack));
    assert(register_aval == register_aval_before_missing_callback);
    assert(register_bval == register_bval_before_missing_callback);
    assert(register_initialized
        == register_initialized_before_missing_callback);
    assert(result.abi_version == result_before_missing_callback.abi_version);
    assert(result.struct_size == result_before_missing_callback.struct_size);
    assert(result.status == result_before_missing_callback.status);
    assert(result.instruction == result_before_missing_callback.instruction);
    assert(result.delay == result_before_missing_callback.delay);
  };
  expect_error(
      [&] { (void)jit.resume(handle, old_descriptor, frame, result); },
      "require sample_coverage");
  assert_unchanged_after_callback_rejection();
  const auto process_binding = jit.bind(handle);
  expect_error(
      [&] {
        (void)jit.resume_prevalidated(
            process_binding, old_descriptor, frame, result);
      },
      "require sample_coverage");
  assert_unchanged_after_callback_rejection();

  std::array old_cohort {
      JitProcessCohortResumeEntry {
          process_binding, old_descriptor, frame, result },
      JitProcessCohortResumeEntry {
          process_binding, descriptor, frame, result },
  };
  expect_error(
      [&] { (void)jit.resume_cohort_prevalidated(old_cohort); },
      "require sample_coverage");
  assert_unchanged_after_callback_rejection();

  std::array valid_cohort {
      JitProcessCohortResumeEntry {
          process_binding, descriptor, frame, result },
      JitProcessCohortResumeEntry {
          process_binding, descriptor, frame, result },
  };
  assert(jit.resume_cohort_prevalidated(valid_cohort) == 1U);
  auto bound_cohort = jit.bind_cohort_prevalidated(old_cohort);
  expect_error(
      [&] {
        (void)jit.resume_cohort_prevalidated(bound_cohort, old_cohort);
      },
      "require sample_coverage");

  auto missing_event_callback = descriptor;
  auto missing_event_services = copy_jit_services(missing_event_callback);
  missing_event_services.query_event_triggered = nullptr;
  missing_event_callback.services = &missing_event_services;
  expect_error(
      [&] {
        (void)jit.resume(handle, missing_event_callback, frame, result);
      },
      "require query_event_triggered");
  assert(runtime.coverage_sample_calls == 1U);
  assert(runtime.event_triggered_calls == 1U);

  auto failing_runtime = TestRuntime { };
  failing_runtime.native_service_word_count = layout.register_word_count;
  failing_runtime.coverage_sample_word_offset
      = layout.register_word_offsets[0];
  failing_runtime.coverage_sample_width = 130U;
  failing_runtime.coverage_sample_status = 1U;
  auto failing_descriptor = abi(failing_runtime);
  jit.initialize_frame(
      handle, frame, register_aval, register_bval, register_initialized);
  expect_generated_runtime_error(
      [&] { (void)jit.resume(handle, failing_descriptor, frame, result); },
      1U, JitGeneratedRuntimeErrorReason::native_service_callback_failure,
      "native SimIR service callback failed");
}

[[nodiscard]] Process make_signal_wait_process() {
  Process process;
  process.id = 7;
  process.name = "signal_waits";
  process.static_sensitivity = {
      {0, EdgeKind::posedge},
      {1, EdgeKind::any},
  };
  process.operations = {
      WaitOn{
          {2, 0, 2},
          {EdgeKind::any, EdgeKind::posedge, EdgeKind::any}},
      WaitSensitivity{},
      WaitForever{},
      Halt{},
  };
  return process;
}

void test_signal_waits_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol_prefix) {
  LlvmJit jit{LlvmJitOptions{optimization, {}}};
  const auto process = make_signal_wait_process();
  const std::array<std::uint32_t, 3> widths{1, 128, 257};
  const auto symbol = std::string{symbol_prefix} + "_signal_waits";
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);

  TestRuntime runtime;
  auto descriptor = abi(runtime);
  expect_unsupported(
      [&] { (void)jit.execute(handle, descriptor); },
      "compiled process can suspend");

  const auto layout = jit.frame_layout(handle);
  assert(layout.register_count == 0);
  std::vector<std::uint64_t> register_aval;
  std::vector<std::uint64_t> register_bval;
  std::vector<std::uint8_t> register_initialized;
  fsim_jit_frame_v2 frame{};
  jit.initialize_frame(
      handle, frame, register_aval, register_bval,
      register_initialized);
  auto result = new_resume_result();

  assert(jit.resume(handle, descriptor, frame, result) ==
         JitResumeStatus::wait_on);
  assert(result.status == FSIM_JIT_RESUME_STATUS_WAIT_ON_V2);
  assert(result.instruction == 0);
  assert(result.delay == 0);
  assert(frame.program_counter == 1);
  assert(frame.last_instruction == 0);
  assert(frame.state == FSIM_JIT_FRAME_STATE_READY_V2);
  assert((fsim::runtime::simir::operation_get<WaitOn>(process.operations[0]).signals ==
          std::vector<SignalId>{2, 0, 2}));
  assert((
      fsim::runtime::simir::operation_get<WaitOn>(process.operations[0]).edges
      == std::vector<EdgeKind>{
          EdgeKind::any, EdgeKind::posedge, EdgeKind::any}));

  assert(jit.resume(handle, descriptor, frame, result) ==
         JitResumeStatus::wait_sensitivity);
  assert(result.status == FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY_V2);
  assert(result.instruction == 1);
  assert(result.delay == 0);
  assert(frame.program_counter == 2);
  assert(frame.last_instruction == 1);
  assert(frame.state == FSIM_JIT_FRAME_STATE_READY_V2);
  assert(process.static_sensitivity.size() == 2);
  assert(process.static_sensitivity[0].signal == 0);
  assert(process.static_sensitivity[0].edge == EdgeKind::posedge);
  assert(process.static_sensitivity[1].signal == 1);
  assert(process.static_sensitivity[1].edge == EdgeKind::any);

  assert(jit.resume(handle, descriptor, frame, result) ==
         JitResumeStatus::wait_forever);
  assert(result.status == FSIM_JIT_RESUME_STATUS_WAIT_FOREVER_V2);
  assert(result.instruction == 2);
  assert(result.delay == 0);
  assert(frame.program_counter == 3);
  assert(frame.last_instruction == 2);
  assert(frame.state == FSIM_JIT_FRAME_STATE_READY_V2);

  assert(jit.resume(handle, descriptor, frame, result) ==
         JitResumeStatus::completed);
  assert(result.status == FSIM_JIT_RESUME_STATUS_COMPLETED_V2);
  assert(result.instruction == 3);
  assert(frame.program_counter == process.operations.size());
  assert(frame.last_instruction == 3);
  assert(frame.state == FSIM_JIT_FRAME_STATE_COMPLETED_V2);
  assert(runtime.writes.empty());
  assert(runtime.scheduled_writes.empty());

  Process wait_on_loop;
  wait_on_loop.id = 8;
  wait_on_loop.name = "wait_on_loop";
  wait_on_loop.operations = {
      WaitOn{{0}},
      Jump{0},
  };
  const auto wait_on_symbol = std::string{symbol_prefix} + "_wait_on_loop";
  jit.add_process(wait_on_symbol, wait_on_loop, widths);
  const auto wait_on_handle = jit.lookup(wait_on_symbol);
  fsim_jit_frame_v2 wait_on_frame{};
  jit.initialize_frame(
      wait_on_handle, wait_on_frame, register_aval, register_bval,
      register_initialized);
  auto wait_on_result = new_resume_result();
  assert(jit.resume(
             wait_on_handle, descriptor, wait_on_frame, wait_on_result) ==
         JitResumeStatus::wait_on);
  assert(wait_on_frame.program_counter == 1);
  assert(jit.resume(
             wait_on_handle, descriptor, wait_on_frame, wait_on_result) ==
         JitResumeStatus::wait_on);
  assert(wait_on_result.instruction == 0);
  assert(wait_on_frame.program_counter == 1);

  Process sensitivity_loop;
  sensitivity_loop.id = 9;
  sensitivity_loop.name = "sensitivity_loop";
  sensitivity_loop.static_sensitivity = {{1, EdgeKind::any}};
  sensitivity_loop.operations = {
      WaitSensitivity{},
      Jump{0},
  };
  const auto sensitivity_symbol =
      std::string{symbol_prefix} + "_sensitivity_loop";
  jit.add_process(sensitivity_symbol, sensitivity_loop, widths);
  const auto sensitivity_handle = jit.lookup(sensitivity_symbol);
  fsim_jit_frame_v2 sensitivity_frame{};
  jit.initialize_frame(
      sensitivity_handle, sensitivity_frame, register_aval, register_bval,
      register_initialized);
  auto sensitivity_result = new_resume_result();
  assert(jit.resume(
             sensitivity_handle, descriptor, sensitivity_frame,
             sensitivity_result) ==
         JitResumeStatus::wait_sensitivity);
  assert(sensitivity_frame.program_counter == 1);
  assert(jit.resume(
             sensitivity_handle, descriptor, sensitivity_frame,
             sensitivity_result) ==
         JitResumeStatus::wait_sensitivity);
  assert(sensitivity_result.instruction == 0);
  assert(sensitivity_frame.program_counter == 1);

  Process timed_wait;
  timed_wait.id = 10;
  timed_wait.name = "timed_wait";
  timed_wait.register_count = 1;
  WaitOn timed_boundary;
  timed_boundary.timeout = 7;
  timed_boundary.timeout_result = 0;
  timed_wait.operations = {
      std::move(timed_boundary),
      Halt{},
  };
  const auto timed_symbol =
      std::string{symbol_prefix} + "_timed_wait";
  jit.add_process(timed_symbol, timed_wait, widths);
  const auto timed_handle = jit.lookup(timed_symbol);
  std::vector<std::uint64_t> timed_aval(1);
  std::vector<std::uint64_t> timed_bval(1);
  std::vector<std::uint8_t> timed_initialized(1);
  fsim_jit_frame_v2 timed_frame{};
  jit.initialize_frame(
      timed_handle, timed_frame, timed_aval, timed_bval,
      timed_initialized);
  auto timed_result = new_resume_result();
  assert(
      jit.resume(
          timed_handle,
          descriptor,
          timed_frame,
          timed_result)
      == JitResumeStatus::wait_on);
  assert(timed_result.instruction == 0);
  assert(timed_result.delay == 7);
  assert(timed_frame.program_counter == 1);

  Process fork_process;
  fork_process.id = 11;
  fork_process.name = "fork_boundaries";
  fork_process.operations = {
      Fork{{4}, ForkJoinKind::none},
      WaitFork{},
      DisableFork{},
      Halt{},
      ForkEnd{}};
  const auto fork_symbol =
      std::string{symbol_prefix} + "_fork_boundaries";
  jit.add_process(fork_symbol, fork_process, {});
  const auto fork_handle = jit.lookup(fork_symbol);
  fsim_jit_frame_v2 fork_frame{};
  jit.initialize_frame(
      fork_handle, fork_frame, register_aval, register_bval,
      register_initialized);
  auto fork_result = new_resume_result();
  assert(
      jit.resume(
          fork_handle, descriptor, fork_frame, fork_result)
      == JitResumeStatus::fork);
  assert(
      fork_result.status == FSIM_JIT_RESUME_STATUS_FORK_V2
      && fork_result.instruction == 0
      && fork_frame.program_counter == 1);
  assert(
      jit.resume(
          fork_handle, descriptor, fork_frame, fork_result)
      == JitResumeStatus::wait_fork);
  assert(
      fork_result.status == FSIM_JIT_RESUME_STATUS_WAIT_FORK_V2
      && fork_result.instruction == 1
      && fork_frame.program_counter == 2);
  assert(
      jit.resume(
          fork_handle, descriptor, fork_frame, fork_result)
      == JitResumeStatus::disable_fork);
  assert(
      fork_result.status == FSIM_JIT_RESUME_STATUS_DISABLE_FORK_V2
      && fork_result.instruction == 2
      && fork_frame.program_counter == 3);

  fsim_jit_frame_v2 child_frame{};
  jit.initialize_frame(
      fork_handle, child_frame, register_aval, register_bval,
      register_initialized);
  child_frame.program_counter = 4;
  auto child_result = new_resume_result();
  assert(
      jit.resume(
          fork_handle, descriptor, child_frame, child_result)
      == JitResumeStatus::fork_end);
  assert(
      child_result.status == FSIM_JIT_RESUME_STATUS_FORK_END_V2
      && child_result.instruction == 4
      && child_frame.program_counter == 5);
}

void test_wide_boundary_registers_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol) {
  constexpr std::uint32_t wide_width = 130U;
  std::string input_bits(wide_width, '0');
  input_bits.front() = '1';
  input_bits[64] = 'X';
  input_bits.back() = 'Z';
  const auto input = PackedLogic4::from_msb_string(input_bits);
  auto method_result = input;
  method_result.set(0U, Logic4::x);
  method_result.set(65U, Logic4::one);
  auto static_result = method_result;
  static_result.set(129U, Logic4::z);
  auto pla_result = static_result;
  pla_result.set(63U, Logic4::zero);

  Process process;
  process.id = 0U;
  process.name = std::string { symbol };
  process.register_count = 5U;
  process.operations = {
      LoadConstant { 0U, input },
      ClassAllocate {
          1U, "work::WideBoundaryProbe<130>",
          "work::WideBoundaryProbe", { 0U }, { 0U }, { "payload" } },
      ClassMethodCall {
          2U, 1U, "work::WideBoundaryProbe::transform", { 0U },
          { "value" }, { 0U }, wide_width, false, { 0U }, { } },
      ClassStaticMethodCall {
          3U, "work::WideBoundaryProbe::static_transform", { 2U },
          { "value" }, { 0U }, wide_width, { 0U } },
      PlaEvaluate {
          0U, 3U, 4U, wide_width, wide_width,
          PlaLogicKind::and_logic, false },
      Halt { },
  };
  const std::array<std::uint32_t, 5> widths {
      wide_width, 64U, wide_width, wide_width, wide_width };
  LlvmJit jit { LlvmJitOptions { optimization, { } } };
  assert(jit.supports_process(process, widths));
  jit.add_process(symbol, process, widths);
  const auto handle = jit.lookup(symbol);
  const auto layout = jit.frame_layout(handle);
  std::vector<std::uint64_t> register_aval(layout.register_word_count);
  std::vector<std::uint64_t> register_bval(layout.register_word_count);
  std::vector<std::uint8_t> register_initialized(layout.register_count);
  fsim_jit_frame_v2 frame { };
  jit.initialize_frame(
      handle, frame, register_aval, register_bval, register_initialized);

  const auto read_register = [&](const RegisterId id) {
    const auto width = widths[id];
    const auto word_count = static_cast<std::size_t>((width + 63U) / 64U);
    const auto offset = layout.register_word_offsets[id];
    return PackedLogic4::from_word_planes(
        width,
        std::span<const std::uint64_t> {
            register_aval.data() + offset, word_count },
        std::span<const std::uint64_t> {
            register_bval.data() + offset, word_count });
  };
  const auto write_register = [&](const RegisterId id,
                                  const PackedLogic4& value) {
    assert(value.width() == widths[id]);
    const auto offset = layout.register_word_offsets[id];
    std::ranges::copy(
        value.aval_words(), register_aval.begin() + offset);
    std::ranges::copy(
        value.bval_words(), register_bval.begin() + offset);
    register_initialized[id] = 1U;
  };
  TestRuntime runtime;
  auto descriptor = abi(runtime);
  auto result = new_resume_result();
  const auto resume_boundary = [&](const std::uint32_t instruction) {
    assert(jit.resume(handle, descriptor, frame, result)
           == JitResumeStatus::simir_boundary);
    assert(result.instruction == instruction);
    assert(frame.program_counter == instruction + 1U);
  };

  resume_boundary(1U);
  assert(read_register(0U) == input);
  write_register(1U, PackedLogic4::from_aval_bval(64U, 0x1234U, 0U));

  resume_boundary(2U);
  assert(read_register(0U) == input);
  write_register(2U, method_result);

  resume_boundary(3U);
  assert(read_register(2U) == method_result);
  write_register(3U, static_result);

  resume_boundary(4U);
  assert(read_register(3U) == static_result);
  write_register(4U, pla_result);
  assert(jit.resume(handle, descriptor, frame, result)
         == JitResumeStatus::completed);
  assert(read_register(4U) == pla_result);
}

void test_constant_plane_forwarding()
{
  LlvmJit jit { LlvmJitOptions { JitOptimizationLevel::o2, { } } };
  const std::array<std::uint32_t, 3> widths { 1, 8, 8 };
  const std::array<ValueKind, 3> kinds {
      ValueKind::logic4, ValueKind::logic4, ValueKind::logic9
  };

  Process conditional;
  conditional.id = 1001;
  conditional.name = "constant_plane_conditional_definitions";
  conditional.register_count = 2;
  conditional.operations = {
      ReadSignal { 0, 0 },
      Branch { 0, 2, 4, UnknownBranchPolicy::when_false },
      LoadConstant { 1, PackedLogic4::from_msb_string("00111100") },
      Jump { 5 },
      LoadConstant { 1, PackedLogic4::from_msb_string("10100101") },
      WriteBlocking { 1, 1 },
      Halt { },
  };
  jit.add_process("constant_plane_conditional", conditional, widths, kinds);
  const auto conditional_handle = jit.lookup("constant_plane_conditional");
  for (const auto [condition, expected] :
      { std::pair { 0U, 0xa5U }, std::pair { 1U, 0x3cU } }) {
    TestRuntime runtime;
    runtime.signals[0] = { condition, 0 };
    auto descriptor = abi(runtime);
    assert(jit.execute(conditional_handle, descriptor)
        == JitExecutionStatus::completed);
    assert((runtime.signals[1] == EncodedSignal { expected, 0 }));
  }

  Process backward;
  backward.id = 1002;
  backward.name = "constant_plane_backward_use";
  backward.register_count = 1;
  backward.operations = {
      Jump { 3 },
      WriteBlocking { 1, 0 },
      Halt { },
      LoadConstant { 0, PackedLogic4::from_msb_string("01111110") },
      Jump { 1 },
  };
  jit.add_process("constant_plane_backward", backward, widths, kinds);
  TestRuntime backward_runtime;
  auto backward_descriptor = abi(backward_runtime);
  assert(jit.execute(jit.lookup("constant_plane_backward"),
      backward_descriptor) == JitExecutionStatus::completed);
  assert((backward_runtime.signals[1] == EncodedSignal { 0x7e, 0 }));

  Process logic9;
  logic9.id = 1003;
  logic9.name = "constant_plane_logic9";
  logic9.register_count = 1;
  logic9.register_value_kinds = { ValueKind::logic9 };
  const auto nine_states = PackedLogic4::from_logic9_msb_string(
      "U01ZWLH-");
  logic9.operations = {
      LoadConstant { 0, nine_states },
      WriteBlocking { 2, 0 },
      Halt { },
  };
  jit.add_process("constant_plane_logic9", logic9, widths, kinds);
  TestRuntime logic9_runtime;
  auto logic9_descriptor = abi(logic9_runtime);
  assert(jit.execute(jit.lookup("constant_plane_logic9"),
      logic9_descriptor) == JitExecutionStatus::completed);
  assert(logic9_runtime.logic9_signals[2] == planes(nine_states));
}

void test_bound_literal_binding()
{
  LlvmJitOptions options { JitOptimizationLevel::o2, { } };
  options.debug_instrumentation = false;
  const std::array<std::uint32_t, 1> signal_widths { 8U };

  Process process;
  process.id = 71U;
  process.name = "bound_literal_wait_resume";
  process.register_count = 1U;
  process.operations = {
      LoadConstant { 0U, PackedLogic4::from_aval_bval(8U, 0U, 0U) },
      WaitFor { 3U },
      WriteBlocking { 0U, 0U },
      Halt { },
  };
  const std::array entry {
      JitProcessModuleEntry { "bound_literal_wait_resume", &process, { 0U } }
  };

  LlvmJit jit { options };
  jit.add_process_module("bound-literal-wait-resume", entry, signal_widths);
  const auto handle = jit.lookup("bound_literal_wait_resume");
  const auto layout = jit.frame_layout(handle);
  assert(layout.register_count == 1U);

  for (const auto [literal, expected] :
      { std::pair { EncodedSignal { 0xa5U, 0U }, 0xa5U },
          std::pair { EncodedSignal { 0x3cU, 0U }, 0x3cU } }) {
    TestRuntime runtime;
    runtime.bound_literal_value = literal;
    auto descriptor = abi(runtime);
    auto services = copy_jit_services(descriptor);
    services.container_operation = &bound_literal_operation;
    descriptor.services = &services;
    std::vector<std::uint64_t> register_aval(layout.register_word_count);
    std::vector<std::uint64_t> register_bval(layout.register_word_count);
    std::vector<std::uint8_t> register_initialized(layout.register_count);
    fsim_jit_frame_v2 frame { };
    jit.initialize_frame(
        handle, frame, register_aval, register_bval, register_initialized);
    auto result = new_resume_result();

    assert(jit.resume(handle, descriptor, frame, result)
           == JitResumeStatus::wait_for);
    assert(result.instruction == 1U && frame.program_counter == 2U);
    assert(runtime.bound_literal_calls == 1U);
    assert(runtime.bound_literal_process == process.id);
    assert(runtime.bound_literal_instruction == 0U);

    runtime.bound_literal_value = { 0U, 0U };
    assert(jit.resume(handle, descriptor, frame, result)
           == JitResumeStatus::completed);
    assert(result.instruction == 3U);
    assert(runtime.bound_literal_calls == 1U);
    assert((runtime.signals[0] == EncodedSignal { expected, 0U }));
  }

  const std::array<std::uint32_t, 0> no_signals { };
  const auto reject_mask = [&](const std::string_view identity,
                               Process& rejected_process,
                               std::vector<InstructionIndex> sites,
                               const std::string_view error) {
    LlvmJit rejected { options };
    const std::array rejected_entry { JitProcessModuleEntry {
        identity, &rejected_process, std::move(sites) } };
    expect_fatal_error(
        [&] {
          rejected.add_process_module(identity, rejected_entry, no_signals);
        },
        error);
  };

  const auto make_literal_process = [](const std::string_view name,
                                       const PackedLogic4& value) {
    Process value_process;
    value_process.id = 72U;
    value_process.name = name;
    value_process.register_count = 1U;
    value_process.operations = { LoadConstant { 0U, value }, Halt { } };
    return value_process;
  };

  Process not_a_literal;
  not_a_literal.id = 73U;
  not_a_literal.name = "bound_literal_not_load_constant";
  not_a_literal.operations = { Halt { } };
  reject_mask("bound_literal_not_load_constant", not_a_literal, { 0U },
      "bound literal must be a known narrow Logic4 value");

  Process out_of_range;
  out_of_range.id = 74U;
  out_of_range.name = "bound_literal_out_of_range";
  out_of_range.operations = { Halt { } };
  reject_mask("bound_literal_out_of_range", out_of_range, { 1U },
      "bound-literal instruction is out of range");

  auto unknown = make_literal_process(
      "bound_literal_unknown",
      PackedLogic4::from_aval_bval(8U, 0U, 1U));
  reject_mask("bound_literal_unknown", unknown, { 0U },
      "bound literal must be a known narrow Logic4 value");

  auto logic9 = make_literal_process(
      "bound_literal_logic9", PackedLogic4::from_logic9_msb_string("0"));
  logic9.register_value_kinds = { ValueKind::logic9 };
  reject_mask("bound_literal_logic9", logic9, { 0U },
      "bound literal must be a known narrow Logic4 value");

  auto too_wide = make_literal_process(
      "bound_literal_too_wide",
      PackedLogic4::from_msb_string(std::string(65U, '0')));
  reject_mask("bound_literal_too_wide", too_wide, { 0U },
      "bound literal must be a known narrow Logic4 value");

  auto malformed_kinds = make_literal_process(
      "bound_literal_malformed_kinds",
      PackedLogic4::from_aval_bval(8U, 0U, 0U));
  malformed_kinds.register_count = 2U;
  malformed_kinds.register_value_kinds = { ValueKind::logic4 };
  reject_mask("bound_literal_malformed_kinds", malformed_kinds, { 0U },
      "bound literal must be a known narrow Logic4 value");

  Process unordered;
  unordered.id = 75U;
  unordered.name = "bound_literal_unordered_sites";
  unordered.register_count = 1U;
  unordered.operations = {
      LoadConstant { 0U, PackedLogic4::from_aval_bval(8U, 0U, 0U) },
      LoadConstant { 0U, PackedLogic4::from_aval_bval(8U, 1U, 0U) },
      Halt { },
  };
  reject_mask("bound_literal_unordered_sites", unordered, { 1U, 0U },
      "invalid bound-literal module entry");
  reject_mask("bound_literal_duplicate_sites", unordered, { 0U, 0U },
      "invalid bound-literal module entry");

  const auto cache_serial = std::chrono::steady_clock::now()
      .time_since_epoch().count();
  const auto cache_directory = std::filesystem::temp_directory_path()
      / ("fsim-bound-literal-cache-" + std::to_string(cache_serial));
  Process cache_process;
  cache_process.id = 76U;
  cache_process.name = "bound_literal_cache_identity";
  cache_process.register_count = 1U;
  cache_process.operations = {
      LoadConstant { 0U, PackedLogic4::from_aval_bval(8U, 0x2bU, 0U) },
      WriteBlocking { 0U, 0U },
      Halt { },
  };
  const auto verify_cache_identity = [&](
      const std::filesystem::path& directory,
      const bool immutable_design_identity) {
    const auto set_identity = [&](LlvmJit& cache_jit) {
      if (immutable_design_identity) {
        cache_jit.set_immutable_design_identity(
            "bound-literal-cache-design");
      }
    };
    {
      auto cache_options = options;
      cache_options.cache_directory = directory;
      LlvmJit bound_cached { cache_options };
      set_identity(bound_cached);
      const std::array bound_cache_entry { JitProcessModuleEntry {
          "bound_literal_cache_identity", &cache_process, { 0U } } };
      bound_cached.add_process_module(
          "bound-literal-cache-identity", bound_cache_entry, signal_widths);
      TestRuntime runtime;
      runtime.bound_literal_value = { 0x74U, 0U };
      auto descriptor = abi(runtime);
      auto services = copy_jit_services(descriptor);
      services.container_operation = &bound_literal_operation;
      descriptor.services = &services;
      assert(bound_cached.execute(
                 bound_cached.lookup("bound_literal_cache_identity"),
                 descriptor)
             == JitExecutionStatus::completed);
      assert((runtime.signals[0] == EncodedSignal { 0x74U, 0U }));
    }
    {
      auto cache_options = options;
      cache_options.cache_directory = directory;
      LlvmJit ordinary_cached { cache_options };
      set_identity(ordinary_cached);
      const std::array ordinary_cache_entry { JitProcessModuleEntry {
          "bound_literal_cache_identity", &cache_process, { } } };
      ordinary_cached.add_process_module(
          "bound-literal-cache-identity", ordinary_cache_entry,
          signal_widths);
      TestRuntime runtime;
      auto descriptor = abi(runtime);
      assert(ordinary_cached.execute(
                 ordinary_cached.lookup("bound_literal_cache_identity"),
                 descriptor)
             == JitExecutionStatus::completed);
      assert((runtime.signals[0] == EncodedSignal { 0x2bU, 0U }));
    }
  };
  verify_cache_identity(cache_directory / "ordinary-key", false);
  verify_cache_identity(cache_directory / "immutable-key", true);
  std::error_code cache_error;
  std::filesystem::remove_all(cache_directory, cache_error);
  assert(!cache_error);
}

} // namespace fsim::tests::compiler
