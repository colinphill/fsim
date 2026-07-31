// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"

namespace fsim::tests::compiler {

void test_debug_point_instrumentation() {
  const std::array<std::uint32_t, 0> no_signals{};
  const auto check_kind =
      [&](const DebugPointKind kind,
          const std::string_view kind_name) {
    Process process;
    process.id = 0;
    process.name = "debug_point";
    process.operations = {
        DebugPoint{
            kind,
            SourceLocation{"debug_point.sv", 7, 3}},
        Halt{},
    };
    const auto run =
        [&](const JitOptimizationLevel optimization,
            const std::string& symbol) {
        LlvmJit jit{LlvmJitOptions{optimization, {}}};
        jit.add_process(symbol, process, no_signals);
        const auto handle = jit.lookup(symbol);
        std::array<std::uint64_t, 0> aval{};
        std::array<std::uint64_t, 0> bval{};
        std::array<std::uint8_t, 0> initialized{};
        fsim_jit_frame_v1 frame{};
        jit.initialize_frame(
            handle, frame, aval, bval, initialized);
        fsim_jit_resume_result_v1 result{
            FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1,
            static_cast<std::uint32_t>(
                sizeof(fsim_jit_resume_result_v1)),
            0,
            FSIM_JIT_INVALID_INSTRUCTION,
            0,
        };
        TestRuntime runtime;
        auto descriptor = abi(runtime);
        auto short_descriptor = descriptor;
        short_descriptor.struct_size =
            static_cast<std::uint32_t>(
                offsetof(fsim_jit_runtime_v1, flags));
        expect_error(
            [&] {
              (void)jit.resume(
                  handle, short_descriptor, frame, result);
            },
            "does not include debug-point flags");
        if (optimization == JitOptimizationLevel::o0) {
          assert(
              jit.resume(handle, descriptor, frame, result)
              == JitResumeStatus::debug_point);
          assert(result.instruction == 0);
          assert(frame.program_counter == 1);
          assert(frame.state == FSIM_JIT_FRAME_STATE_READY);
        }
        assert(
            jit.resume(handle, descriptor, frame, result)
            == JitResumeStatus::completed);
        assert(result.instruction == 1);
        if (optimization == JitOptimizationLevel::o2) {
          jit.initialize_frame(
              handle, frame, aval, bval, initialized);
          descriptor.flags = FSIM_JIT_RUNTIME_FLAG_DEBUG_POINTS;
          assert(
              jit.resume(handle, descriptor, frame, result)
              == JitResumeStatus::debug_point);
          assert(result.instruction == 0);
          assert(frame.program_counter == 1);
          assert(
              jit.resume(handle, descriptor, frame, result)
              == JitResumeStatus::completed);
        }
        };
    run(
        JitOptimizationLevel::o0,
        "debug_point_" + std::string{kind_name} + "_o0");
    run(
        JitOptimizationLevel::o2,
        "debug_point_" + std::string{kind_name} + "_o2");
  };
  check_kind(DebugPointKind::statement, "statement");
  check_kind(DebugPointKind::call, "call");
}

[[nodiscard]] Process make_cached_process(const std::string_view value) {
  Process process;
  process.id = 11;
  process.name = "cached_process";
  process.register_count = 1;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string(value)},
      WriteBlocking{0, 0},
      Halt{},
  };
  return process;
}

[[nodiscard]] Process
make_cached_signal_process(const SignalId signal) {
  Process process;
  process.id = 12;
  process.name = "cached_signal_process";
  process.register_count = 1;
  process.operations = {
      ReadSignal{0, signal},
      Halt{},
  };
  return process;
}

[[nodiscard]] Process
make_cached_scheduled_process(const bool delayed,
                              const std::uint64_t delay) {
  Process process;
  process.id = 13;
  process.name = "cached_scheduled_process";
  process.register_count = 1;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("10100101")},
      delayed ? Operation{WriteAfter{0, 0, delay}}
              : Operation{WriteUpdate{0, 0}},
      Halt{},
  };
  return process;
}

[[nodiscard]] Process make_cached_inertial_process(
    const TransitionDelays delays) {
  Process process;
  process.id = 16;
  process.name = "cached_inertial_process";
  process.register_count = 1;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("10100101")},
      WriteInertial{0, 0, delays},
      Halt{},
  };
  return process;
}

[[nodiscard]] Process make_cached_projected_process(
    const std::uint64_t delay,
    const std::uint64_t rejection,
    const ProjectedDelayMode mode) {
  Process process;
  process.id = 17;
  process.name = "cached_projected_process";
  process.register_count = 1;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("10100101")},
      WriteProjected{0, 0, delay, rejection, mode},
      Halt{},
  };
  return process;
}

[[nodiscard]] Process make_cached_projected_waveform_process(
    const std::uint64_t second_delay,
    const std::uint64_t rejection,
    const ProjectedDelayMode mode) {
  Process process;
  process.id = 18;
  process.name = "cached_projected_waveform_process";
  process.register_count = 2;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("10100101")},
      LoadConstant{1, PackedLogic4::from_msb_string("01011010")},
      WriteProjectedWaveform{
          0, {{0, 5}, {1, second_delay}}, rejection, mode},
      Halt{},
  };
  return process;
}

[[nodiscard]] Process
make_cached_wait_process(const bool static_wait,
                         std::vector<SignalId> signals,
                         std::vector<EdgeKind> edges = {}) {
  Process process;
  process.id = 14;
  process.name = "cached_wait_process";
  for (const auto signal : signals) {
    process.static_sensitivity.push_back({signal, EdgeKind::any});
  }
  if (static_wait) {
    process.operations = {WaitSensitivity{}, Halt{}};
  } else {
    process.operations = {
        WaitOn{std::move(signals), std::move(edges)}, Halt{}};
  }
  return process;
}

[[nodiscard]] Process make_cached_assertion_process(
    const AssertionSeverity severity,
    const std::uint32_t source_line) {
  Process process;
  process.id = 15;
  process.name = "cached_assertion_process";
  process.register_count = 1;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      Assert{
          0,
          "cached assertion",
          severity,
          SourceLocation{"cache_assertion.sv", source_line, 3}},
      Halt{},
  };
  return process;
}

[[nodiscard]] Process make_cached_debug_point_process(
    const DebugPointKind kind,
    const std::uint32_t source_line) {
  Process process;
  process.id = 16;
  process.name = "cached_debug_point_process";
  process.operations = {
      DebugPoint{
          kind,
          SourceLocation{"cache_debug_point.sv", source_line, 5}},
      Halt{},
  };
  return process;
}

void expect_cache_statistics(const LlvmJit &jit, const std::uint64_t hits,
                             const std::uint64_t misses,
                             const std::uint64_t stores,
                             const std::uint64_t rejected_entries = 0) {
  const auto statistics = jit.cache_statistics();
  assert(statistics.hits == hits);
  assert(statistics.misses == misses);
  assert(statistics.stores == stores);
  assert(statistics.rejected_entries == rejected_entries);
  assert(statistics.load_failures == 0);
  assert(statistics.store_failures == 0);
  assert(statistics.prune_failures == 0);
}

void run_cached_process(LlvmJit &jit, const std::string_view symbol,
                        const EncodedSignal expected) {
  const auto handle = jit.lookup(symbol);
  TestRuntime runtime;
  auto descriptor = abi(runtime);
  assert(jit.execute(handle, descriptor) == JitExecutionStatus::completed);
  assert(runtime.signals[0] == expected);
}

void run_cached_signal_process(LlvmJit &jit,
                               const std::string_view symbol) {
  const auto handle = jit.lookup(symbol);
  TestRuntime runtime;
  runtime.signals[0] = {UINT64_C(0xa5), 0};
  runtime.signals[1] = {UINT64_C(0x3c), 0};
  auto descriptor = abi(runtime);
  assert(jit.execute(handle, descriptor) == JitExecutionStatus::completed);
}

void run_cached_assertion_process(
    LlvmJit& jit, const std::string_view symbol) {
  TestRuntime runtime;
  auto descriptor = abi(runtime);
  assert(
      jit.execute(jit.lookup(symbol), descriptor)
      == JitExecutionStatus::completed);
  assert(runtime.assertion_count == 0);
}

void run_cached_scheduled_process(
    LlvmJit &jit, const std::string_view symbol,
    const ScheduledWriteKind expected_kind,
    const std::uint64_t expected_delay) {
  const auto handle = jit.lookup(symbol);
  TestRuntime runtime;
  auto descriptor = abi(runtime);
  assert(jit.execute(handle, descriptor) == JitExecutionStatus::completed);
  assert(runtime.scheduled_writes.size() == 1);
  assert(runtime.scheduled_writes.front().kind == expected_kind);
  assert(runtime.scheduled_writes.front().delay == expected_delay);
}

void materialize_cached_wait_process(
    LlvmJit &jit, const std::string_view symbol) {
  assert(jit.lookup(symbol));
}

[[nodiscard]] std::vector<std::filesystem::path>
cached_object_paths(const std::filesystem::path &root) {
  std::vector<std::filesystem::path> result;
  if (!std::filesystem::exists(root)) {
    return result;
  }
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator{root}) {
    if (entry.is_regular_file() && entry.path().extension() == ".fobj") {
      result.push_back(entry.path());
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

void test_process_module_grouping_at_level(
    const JitOptimizationLevel optimization,
    const std::filesystem::path& cache_directory) {
  const auto make_process =
      [](const ProcessId id, const SignalId signal,
         const std::string_view value) {
        Process process;
        process.id = id;
        process.name =
            "grouped_process_" + std::to_string(id);
        process.register_count = 1;
        process.operations = {
            LoadConstant{
                0, PackedLogic4::from_msb_string(value)},
            WriteBlocking{signal, 0},
            Halt{},
        };
        return process;
      };
  const std::array<std::uint32_t, 2> widths{8, 8};
  const auto first = make_process(20, 0, "10100101");
  const auto second = make_process(21, 1, "01011010");
  const auto add_group =
      [&](LlvmJit& jit, const Process& left,
          const Process& right) {
        const std::array entries{
            JitProcessModuleEntry{"grouped_first", &left},
            JitProcessModuleEntry{"grouped_second", &right},
        };
        jit.add_process_module(
            "work.grouped@top", entries, widths);
      };
  const auto execute_group =
      [](LlvmJit& jit) {
        TestRuntime runtime;
        auto descriptor = abi(runtime);
        const auto first_handle = jit.lookup("grouped_first");
        const auto second_handle = jit.lookup("grouped_second");
        assert(first_handle != second_handle);
        assert(
            jit.execute(first_handle, descriptor)
            == JitExecutionStatus::completed);
        assert(
            jit.execute(second_handle, descriptor)
            == JitExecutionStatus::completed);
        assert((
            runtime.signals[0]
            == EncodedSignal{UINT64_C(0xa5), 0}));
        assert((
            runtime.signals[1]
            == EncodedSignal{UINT64_C(0x5a), 0}));
        return std::array{
            jit.frame_layout(first_handle),
            jit.frame_layout(second_handle),
        };
      };

  std::array<fsim::compiler::JitProcessFrameLayout, 2>
      original_layouts;
  {
    LlvmJit cold{
        LlvmJitOptions{optimization, cache_directory}};
    assert(cold.supports_process(first, widths));
    assert(cold.supports_process(second, widths));
    add_group(cold, first, second);
    original_layouts = execute_group(cold);
    expect_cache_statistics(cold, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 1);

  {
    LlvmJit warm{
        LlvmJitOptions{optimization, cache_directory}};
    add_group(warm, first, second);
    const auto warm_layouts = execute_group(warm);
    assert(warm_layouts == original_layouts);
    expect_cache_statistics(warm, 1, 0, 0);
  }
  assert(cached_object_paths(cache_directory).size() == 1);

  // Any changed member invalidates the specialization object, while an
  // unchanged member retains its process-local frame identity.
  {
    LlvmJit changed{
        LlvmJitOptions{optimization, cache_directory}};
    const auto changed_second =
        make_process(21, 1, "00111100");
    add_group(changed, first, changed_second);
    const auto first_handle = changed.lookup("grouped_first");
    const auto second_handle = changed.lookup("grouped_second");
    assert(
        changed.frame_layout(first_handle)
        == original_layouts[0]);
    assert(
        changed.frame_layout(second_handle)
        != original_layouts[1]);
    expect_cache_statistics(changed, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 2);

  {
    LlvmJit rejected;
    const std::array<JitProcessModuleEntry, 0> empty{};
    expect_fatal_error(
        [&] {
          rejected.add_process_module(
              "empty", empty, widths);
        },
        "module cannot be empty");
    const std::array null_entry{
        JitProcessModuleEntry{"missing", nullptr}};
    expect_fatal_error(
        [&] {
          rejected.add_process_module(
              "null", null_entry, widths);
        },
        "has no SimIR process");
    const std::array duplicate_entries{
        JitProcessModuleEntry{"duplicate", &first},
        JitProcessModuleEntry{"duplicate", &second},
    };
    expect_fatal_error(
        [&] {
          rejected.add_process_module(
              "duplicates", duplicate_entries, widths);
        },
        "duplicate LLVM process symbol");

    Process supported;
    supported.id = 22;
    supported.name = "supported";
    supported.operations = {Halt{}};
    Process too_wide;
    too_wide.id = 23;
    too_wide.name = "too_wide";
    too_wide.register_count = 1;
    too_wide.operations = {ReadSignal{0, 0}, Halt{}};
    const std::array<std::uint32_t, 1> wide_widths{65};
    assert(rejected.supports_process(supported, wide_widths));
    assert(!rejected.supports_process(too_wide, wide_widths));
    Process exact_register;
    exact_register.id = 24;
    exact_register.name = "exact_register";
    exact_register.register_count = 1;
    exact_register.register_value_kinds = {
        ValueKind::logic9};
    exact_register.operations = {
        LoadConstant{
            0,
            PackedLogic4::from_logic9_msb_string("W")},
        Halt{}};
    assert(rejected.supports_process(
        exact_register, std::array<std::uint32_t, 0>{}));
    Process exact_signal_access;
    exact_signal_access.id = 25;
    exact_signal_access.name = "exact_signal_access";
    exact_signal_access.register_count = 1;
    exact_signal_access.operations = {
        ReadSignal{0, 0}, Halt{}};
    const std::array<std::uint32_t, 1> scalar_widths{1};
    const std::array<ValueKind, 1> exact_signal_kinds{
        ValueKind::logic9};
    assert(rejected.supports_process(
        exact_signal_access,
        scalar_widths,
        exact_signal_kinds));
    const std::array unsupported_entries{
        JitProcessModuleEntry{"eligible", &supported},
        JitProcessModuleEntry{"unsupported", &too_wide},
    };
    expect_unsupported(
        [&] {
          rejected.add_process_module(
              "unsupported-member", unsupported_entries,
              wide_widths);
        },
        "widths in [1, 64]");
    expect_error(
        [&] { (void)rejected.lookup("eligible"); },
        "was not added");

    const std::array first_entry{
        JitProcessModuleEntry{"registered", &first}};
    rejected.add_process_module(
        "same-module", first_entry, widths);
    const std::array second_entry{
        JitProcessModuleEntry{"new_symbol", &second}};
    expect_fatal_error(
        [&] {
          rejected.add_process_module(
              "same-module", second_entry, widths);
        },
        "duplicate LLVM process module identity");
    expect_error(
        [&] { (void)rejected.lookup("new_symbol"); },
        "was not added");
  }
}

void test_object_cache_at_level(const JitOptimizationLevel optimization,
                                const std::filesystem::path &cache_directory) {
  const std::array<std::uint32_t, 2> widths{8, 1};
  constexpr std::string_view symbol = "persistent_cache_process";
  const auto options = LlvmJitOptions{optimization, cache_directory};

  {
    LlvmJit cold{options};
    cold.add_process(symbol, make_cached_process("10100101"), widths);
    expect_cache_statistics(cold, 0, 0, 0);
    run_cached_process(cold, symbol,
                       EncodedSignal{UINT64_C(0xa5), 0});
    expect_cache_statistics(cold, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 1);

  {
    LlvmJit warm{options};
    warm.add_process(symbol, make_cached_process("10100101"), widths);
    run_cached_process(warm, symbol,
                       EncodedSignal{UINT64_C(0xa5), 0});
    expect_cache_statistics(warm, 1, 0, 0);
  }

  const auto cached_objects = cached_object_paths(cache_directory);
  assert(cached_objects.size() == 1);
  {
    std::ofstream stream{cached_objects.front(),
                         std::ios::binary | std::ios::trunc};
    assert(stream);
    stream << "corrupt";
    stream.flush();
    assert(stream);
  }
  {
    LlvmJit recovered{options};
    recovered.add_process(symbol, make_cached_process("10100101"), widths);
    run_cached_process(recovered, symbol,
                       EncodedSignal{UINT64_C(0xa5), 0});
    expect_cache_statistics(recovered, 0, 1, 1, 1);
  }
  {
    LlvmJit healed{options};
    healed.add_process(symbol, make_cached_process("10100101"), widths);
    run_cached_process(healed, symbol,
                       EncodedSignal{UINT64_C(0xa5), 0});
    expect_cache_statistics(healed, 1, 0, 0);
  }

  // A checksum-valid payload can still be an incompatible native object.
  // The LLVM adapter validates its object structure before handing it to ORC.
  {
    const std::array incompatible{
        std::byte{0xde}, std::byte{0xad}, std::byte{0xbe}, std::byte{0xef}};
    fsim::compiler::ObjectCache storage{
        cache_directory / "llvm" / "objects"};
    std::error_code error;
    assert(storage.store(cached_objects.front().stem().string(),
                         incompatible, error));
    assert(!error);
  }
  {
    LlvmJit recovered{options};
    recovered.add_process(symbol, make_cached_process("10100101"), widths);
    run_cached_process(recovered, symbol,
                       EncodedSignal{UINT64_C(0xa5), 0});
    expect_cache_statistics(recovered, 0, 1, 1, 1);
  }

  // Executable SimIR contents participate in the key.
  {
    LlvmJit changed_process{options};
    changed_process.add_process(
        symbol, make_cached_process("00111100"), widths);
    run_cached_process(changed_process, symbol,
                       EncodedSignal{UINT64_C(0x3c), 0});
    expect_cache_statistics(changed_process, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 2);

  // Unrelated elaborated signals do not invalidate a process-local object.
  {
    LlvmJit changed_widths{options};
    const std::array<std::uint32_t, 2> other_widths{8, 2};
    changed_widths.add_process(
        symbol, make_cached_process("10100101"), other_widths);
    run_cached_process(changed_widths, symbol,
                       EncodedSignal{UINT64_C(0xa5), 0});
    expect_cache_statistics(changed_widths, 1, 0, 0);
  }
  assert(cached_object_paths(cache_directory).size() == 2);

  constexpr std::string_view signal_symbol =
      "persistent_cache_signal_process";
  const std::array<std::uint32_t, 3> signal_widths{8, 8, 1};
  {
    LlvmJit cold{options};
    cold.add_process(
        signal_symbol, make_cached_signal_process(0), signal_widths);
    run_cached_signal_process(cold, signal_symbol);
    expect_cache_statistics(cold, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 3);

  // Changing only an unreferenced signal width reuses the cached object.
  {
    LlvmJit unrelated_width{options};
    const std::array<std::uint32_t, 3> widths_with_unrelated_change{8, 8, 2};
    unrelated_width.add_process(
        signal_symbol, make_cached_signal_process(0),
        widths_with_unrelated_change);
    run_cached_signal_process(unrelated_width, signal_symbol);
    expect_cache_statistics(unrelated_width, 1, 0, 0);
  }
  assert(cached_object_paths(cache_directory).size() == 3);

  // A referenced signal's width remains part of the process cache key.
  {
    LlvmJit referenced_width{options};
    const std::array<std::uint32_t, 3> widths_with_referenced_change{9, 8, 1};
    referenced_width.add_process(
        signal_symbol, make_cached_signal_process(0),
        widths_with_referenced_change);
    run_cached_signal_process(referenced_width, signal_symbol);
    expect_cache_statistics(referenced_width, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 4);

  // Signal IDs remain process inputs even when the referenced widths match.
  {
    LlvmJit changed_signal{options};
    changed_signal.add_process(
        signal_symbol, make_cached_signal_process(1), signal_widths);
    run_cached_signal_process(changed_signal, signal_symbol);
    expect_cache_statistics(changed_signal, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 5);

  constexpr std::string_view scheduled_symbol =
      "persistent_cache_scheduled_process";
  const std::array<std::uint32_t, 1> scheduled_widths{8};
  {
    LlvmJit cold{options};
    cold.add_process(
        scheduled_symbol, make_cached_scheduled_process(true, 3),
        scheduled_widths);
    run_cached_scheduled_process(
        cold, scheduled_symbol, ScheduledWriteKind::after, 3);
    expect_cache_statistics(cold, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 6);
  {
    LlvmJit warm{options};
    warm.add_process(
        scheduled_symbol, make_cached_scheduled_process(true, 3),
        scheduled_widths);
    run_cached_scheduled_process(
        warm, scheduled_symbol, ScheduledWriteKind::after, 3);
    expect_cache_statistics(warm, 1, 0, 0);
  }

  // Delayed-write delay and operation kind both participate in identity.
  {
    LlvmJit changed_delay{options};
    changed_delay.add_process(
        scheduled_symbol, make_cached_scheduled_process(true, 4),
        scheduled_widths);
    run_cached_scheduled_process(
        changed_delay, scheduled_symbol, ScheduledWriteKind::after, 4);
    expect_cache_statistics(changed_delay, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 7);
  {
    LlvmJit changed_kind{options};
    changed_kind.add_process(
        scheduled_symbol, make_cached_scheduled_process(false, 0),
        scheduled_widths);
    run_cached_scheduled_process(
        changed_kind, scheduled_symbol, ScheduledWriteKind::update, 0);
    expect_cache_statistics(changed_kind, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 8);

  constexpr std::string_view wait_symbol =
      "persistent_cache_wait_process";
  const std::array<std::uint32_t, 2> wait_widths{1, 1};
  {
    LlvmJit cold{options};
    cold.add_process(
        wait_symbol, make_cached_wait_process(false, {0, 1}),
        wait_widths);
    materialize_cached_wait_process(cold, wait_symbol);
    expect_cache_statistics(cold, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 9);
  {
    LlvmJit warm{options};
    warm.add_process(
        wait_symbol, make_cached_wait_process(false, {0, 1}),
        wait_widths);
    materialize_cached_wait_process(warm, wait_symbol);
    expect_cache_statistics(warm, 1, 0, 0);
  }

  // Wait operands, referenced widths, operation kind, and static edge rules
  // participate in native cache identity.
  {
    LlvmJit changed_operands{options};
    changed_operands.add_process(
        wait_symbol, make_cached_wait_process(false, {1, 0}),
        wait_widths);
    materialize_cached_wait_process(changed_operands, wait_symbol);
    expect_cache_statistics(changed_operands, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 10);
  {
    LlvmJit changed_kind{options};
    changed_kind.add_process(
        wait_symbol, make_cached_wait_process(true, {0, 1}),
        wait_widths);
    materialize_cached_wait_process(changed_kind, wait_symbol);
    expect_cache_statistics(changed_kind, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 11);
  {
    LlvmJit changed_width{options};
    const std::array<std::uint32_t, 2> wider_wait_signal{2, 1};
    changed_width.add_process(
        wait_symbol, make_cached_wait_process(false, {0, 1}),
        wider_wait_signal);
    materialize_cached_wait_process(changed_width, wait_symbol);
    expect_cache_statistics(changed_width, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 12);
  {
    LlvmJit changed_dynamic_edge{options};
    changed_dynamic_edge.add_process(
        wait_symbol,
        make_cached_wait_process(
            false, {0, 1},
            {EdgeKind::posedge, EdgeKind::any}),
        wait_widths);
    materialize_cached_wait_process(
        changed_dynamic_edge, wait_symbol);
    expect_cache_statistics(changed_dynamic_edge, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 13);
  {
    LlvmJit changed_edge{options};
    auto edge_process =
        make_cached_wait_process(true, {0, 1});
    edge_process.static_sensitivity.front().edge =
        EdgeKind::posedge;
    changed_edge.add_process(
        wait_symbol, edge_process, wait_widths);
    materialize_cached_wait_process(changed_edge, wait_symbol);
    expect_cache_statistics(changed_edge, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 14);

  constexpr std::string_view assertion_symbol =
      "persistent_cache_assertion_process";
  const std::array<std::uint32_t, 0> no_signals{};
  {
    LlvmJit cold{options};
    cold.add_process(
        assertion_symbol,
        make_cached_assertion_process(AssertionSeverity::error, 7),
        no_signals);
    run_cached_assertion_process(cold, assertion_symbol);
    expect_cache_statistics(cold, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 15);
  {
    LlvmJit warm{options};
    warm.add_process(
        assertion_symbol,
        make_cached_assertion_process(AssertionSeverity::error, 7),
        no_signals);
    run_cached_assertion_process(warm, assertion_symbol);
    expect_cache_statistics(warm, 1, 0, 0);
  }

  // Assertion diagnostic metadata is immutable generated behavior and must
  // therefore participate in native object identity.
  {
    LlvmJit changed_metadata{options};
    changed_metadata.add_process(
        assertion_symbol,
        make_cached_assertion_process(AssertionSeverity::failure, 8),
        no_signals);
    run_cached_assertion_process(changed_metadata, assertion_symbol);
    expect_cache_statistics(changed_metadata, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 16);

  constexpr std::string_view debug_symbol =
      "persistent_cache_debug_point_process";
  {
    LlvmJit cold{options};
    cold.add_process(
        debug_symbol,
        make_cached_debug_point_process(DebugPointKind::statement, 11),
        no_signals);
    assert(cold.lookup(debug_symbol));
    expect_cache_statistics(cold, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 17);
  {
    LlvmJit warm{options};
    warm.add_process(
        debug_symbol,
        make_cached_debug_point_process(DebugPointKind::statement, 11),
        no_signals);
    assert(warm.lookup(debug_symbol));
    expect_cache_statistics(warm, 1, 0, 0);
  }

  // Debug-point kind and source location are generated behavior because the
  // returned instruction indexes immutable SimIR metadata.
  {
    LlvmJit changed_metadata{options};
    changed_metadata.add_process(
        debug_symbol,
        make_cached_debug_point_process(DebugPointKind::wait, 12),
        no_signals);
    assert(changed_metadata.lookup(debug_symbol));
    expect_cache_statistics(changed_metadata, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 18);

  // A WaitOn timeout changes generated boundary metadata and cache identity.
  {
    LlvmJit changed_timeout{options};
    auto timed_wait =
        make_cached_wait_process(false, {0, 1});
    std::get<WaitOn>(timed_wait.operations.front()).timeout = 5;
    changed_timeout.add_process(
        wait_symbol, timed_wait, wait_widths);
    materialize_cached_wait_process(
        changed_timeout, wait_symbol);
    expect_cache_statistics(changed_timeout, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 19);

  constexpr std::string_view wildcard_symbol =
      "persistent_cache_wildcard_case";
  const std::array<std::uint32_t, 1> wildcard_widths{1};
  const auto make_wildcard_process =
      [](const BinaryOperator operation) {
        Process process;
        process.id = 0;
        process.name = "cached_wildcard_case";
        process.register_count = 3;
        process.operations = {
            LoadConstant{
                0, PackedLogic4::from_msb_string("10X1")},
            LoadConstant{
                1, PackedLogic4::from_msb_string("1011")},
            Binary{operation, 2, 0, 1},
            WriteBlocking{0, 2},
            Halt{},
        };
        return process;
      };
  const auto run_wildcard_process =
      [&](LlvmJit& jit, const Logic4 expected) {
        TestRuntime runtime;
        auto descriptor = abi(runtime);
        assert(
            jit.execute(jit.lookup(wildcard_symbol), descriptor)
            == JitExecutionStatus::completed);
        assert(runtime.signals[0] == encode(expected));
      };
  {
    LlvmJit cold{options};
    cold.add_process(
        wildcard_symbol,
        make_wildcard_process(BinaryOperator::casez_equal),
        wildcard_widths);
    run_wildcard_process(cold, Logic4::zero);
    expect_cache_statistics(cold, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 20);
  {
    LlvmJit warm{options};
    warm.add_process(
        wildcard_symbol,
        make_wildcard_process(BinaryOperator::casez_equal),
        wildcard_widths);
    run_wildcard_process(warm, Logic4::zero);
    expect_cache_statistics(warm, 1, 0, 0);
  }
  // The wildcard matching policy is generated behavior and participates in
  // native object identity.
  {
    LlvmJit changed_operator{options};
    changed_operator.add_process(
        wildcard_symbol,
        make_wildcard_process(BinaryOperator::casex_equal),
        wildcard_widths);
    run_wildcard_process(changed_operator, Logic4::one);
    expect_cache_statistics(changed_operator, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 21);
  {
    LlvmJit one_sided_wildcard{options};
    one_sided_wildcard.add_process(
        wildcard_symbol,
        make_wildcard_process(BinaryOperator::wildcard_equal),
        wildcard_widths);
    run_wildcard_process(one_sided_wildcard, Logic4::x);
    expect_cache_statistics(one_sided_wildcard, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 22);

  constexpr std::string_view power_symbol =
      "persistent_cache_power_operator";
  const std::array<std::uint32_t, 1> power_widths{8};
  const auto make_power_process =
      [](const BinaryOperator operation) {
        Process process;
        process.id = 0;
        process.name = "cached_power_operator";
        process.register_count = 3;
        process.operations = {
            LoadConstant{
                0, PackedLogic4::from_msb_string("00000011")},
            LoadConstant{
                1, PackedLogic4::from_msb_string("00000100")},
            Binary{operation, 2, 0, 1},
            WriteBlocking{0, 2},
            Halt{},
        };
        return process;
      };
  const auto run_power_process =
      [&](LlvmJit& jit, const std::string_view expected) {
        TestRuntime runtime;
        auto descriptor = abi(runtime);
        assert(
            jit.execute(jit.lookup(power_symbol), descriptor)
            == JitExecutionStatus::completed);
        const auto encoded =
            PackedLogic4::from_msb_string(expected).low_word();
        assert((
            runtime.signals[0]
            == EncodedSignal{encoded.aval, encoded.bval}));
      };
  {
    LlvmJit multiply{options};
    multiply.add_process(
        power_symbol,
        make_power_process(BinaryOperator::multiply_unsigned),
        power_widths);
    run_power_process(multiply, "00001100");
    expect_cache_statistics(multiply, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 23);
  {
    LlvmJit power{options};
    power.add_process(
        power_symbol,
        make_power_process(BinaryOperator::power_unsigned),
        power_widths);
    run_power_process(power, "01010001");
    expect_cache_statistics(power, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 24);
}

void test_optimization_cache_invalidation(
    const std::filesystem::path &cache_directory) {
  const std::array<std::uint32_t, 2> widths{8, 1};
  constexpr std::string_view symbol = "optimization_cache_process";
  const auto process = make_cached_process("10100101");

  {
    LlvmJit o0{
        LlvmJitOptions{JitOptimizationLevel::o0, cache_directory}};
    o0.add_process(symbol, process, widths);
    run_cached_process(o0, symbol, EncodedSignal{UINT64_C(0xa5), 0});
    expect_cache_statistics(o0, 0, 1, 1);
  }
  {
    LlvmJit o2{
        LlvmJitOptions{JitOptimizationLevel::o2, cache_directory}};
    o2.add_process(symbol, process, widths);
    run_cached_process(o2, symbol, EncodedSignal{UINT64_C(0xa5), 0});
    expect_cache_statistics(o2, 0, 1, 1);
  }
  assert(cached_object_paths(cache_directory).size() == 2);
  {
    LlvmJit warm_o2{
        LlvmJitOptions{JitOptimizationLevel::o2, cache_directory}};
    warm_o2.add_process(symbol, process, widths);
    run_cached_process(warm_o2, symbol,
                       EncodedSignal{UINT64_C(0xa5), 0});
    expect_cache_statistics(warm_o2, 1, 0, 0);
  }
}

void test_cache_pruning_integration(
    const std::filesystem::path& cache_directory) {
  fsim::compiler::ObjectCache storage{
      cache_directory / "llvm" / "objects"};
  fsim::compiler::CacheKeyBuilder builder;
  const auto key = builder.add("seed", "prune-me").finish();
  const std::array payload{
      std::byte{0xde}, std::byte{0xad}, std::byte{0xbe}, std::byte{0xef}};
  std::error_code error;
  assert(storage.store(key, payload, error));
  const auto encoded_size =
      std::filesystem::file_size(storage.path_for(key), error);
  assert(!error);

  LlvmJitOptions options{JitOptimizationLevel::o2, cache_directory};
  options.cache_maximum_bytes.reset();
  options.cache_maximum_entries = 0;
  options.cache_maximum_age.reset();
  LlvmJit pruned{options};
  const auto statistics = pruned.cache_statistics();
  assert(statistics.pruned_entries == 1);
  assert(statistics.pruned_bytes == encoded_size);
  assert(statistics.prune_failures == 0);
  assert(!std::filesystem::exists(storage.path_for(key)));

  // Cache maintenance is best-effort: an unusable cache root is reflected in
  // telemetry but never prevents construction of a valid JIT.
  const auto broken_directory = cache_directory.parent_path() / "broken";
  std::filesystem::create_directories(broken_directory / "llvm", error);
  assert(!error);
  {
    std::ofstream file{
        broken_directory / "llvm" / "objects", std::ios::binary};
    file << "not a directory";
  }
  LlvmJit broken{
      LlvmJitOptions{JitOptimizationLevel::o2, broken_directory}};
  assert(broken.cache_statistics().prune_failures == 1);
}

void test_inertial_cache_identity(
    const std::filesystem::path& cache_directory) {
  constexpr std::string_view symbol{"cached_inertial"};
  const std::array<std::uint32_t, 1> widths{8};
  const auto run =
      [&](const TransitionDelays delays,
          const std::size_t hits,
          const std::size_t misses) {
        LlvmJit jit{
            LlvmJitOptions{
                JitOptimizationLevel::o2, cache_directory}};
        jit.add_process(
            symbol, make_cached_inertial_process(delays), widths);
        TestRuntime runtime;
        auto descriptor = abi(runtime);
        assert(
            jit.execute(jit.lookup(symbol), descriptor)
            == JitExecutionStatus::completed);
        assert((
            runtime.inertial_writes
            == std::vector<InertialWrite>{
                {
                    0,
                    encode(PackedLogic4::from_msb_string("10100101")),
                    0,
                    0,
                    delays.rise,
                    delays.fall,
                    delays.turnoff}}));
        expect_cache_statistics(jit, hits, misses, misses);
      };
  run({2, 3, 4}, 0, 1);
  run({2, 3, 4}, 1, 0);
  run({5, 3, 4}, 0, 1);
  run({2, 6, 4}, 0, 1);
  run({2, 3, 7}, 0, 1);
}

void test_projected_cache_identity(
    const std::filesystem::path& cache_directory) {
  constexpr std::string_view symbol{"cached_projected"};
  const std::array<std::uint32_t, 1> widths{8};
  const auto run =
      [&](const std::uint64_t delay,
          const std::uint64_t rejection,
          const ProjectedDelayMode mode,
          const std::size_t hits,
          const std::size_t misses) {
        LlvmJit jit{
            LlvmJitOptions{
                JitOptimizationLevel::o2, cache_directory}};
        jit.add_process(
            symbol,
            make_cached_projected_process(
                delay, rejection, mode),
            widths);
        TestRuntime runtime;
        auto descriptor = abi(runtime);
        assert(
            jit.execute(jit.lookup(symbol), descriptor)
            == JitExecutionStatus::completed);
        assert((
            runtime.projected_writes
            == std::vector<ProjectedWrite>{
                {
                    0,
                    encode(PackedLogic4::from_msb_string("10100101")),
                    0,
                    0,
                    delay,
                    rejection,
                    static_cast<std::uint32_t>(mode)}}));
        expect_cache_statistics(jit, hits, misses, misses);
      };
  run(5, 2, ProjectedDelayMode::inertial, 0, 1);
  run(5, 2, ProjectedDelayMode::inertial, 1, 0);
  run(6, 2, ProjectedDelayMode::inertial, 0, 1);
  run(5, 1, ProjectedDelayMode::inertial, 0, 1);
  run(5, 0, ProjectedDelayMode::transport, 0, 1);
}

void test_projected_waveform_cache_identity(
    const std::filesystem::path& cache_directory) {
  constexpr std::string_view symbol{"cached_projected_waveform"};
  const std::array<std::uint32_t, 1> widths{8};
  const auto run =
      [&](const std::uint64_t second_delay,
          const std::uint64_t rejection,
          const ProjectedDelayMode mode,
          const std::size_t hits,
          const std::size_t misses) {
        LlvmJit jit{
            LlvmJitOptions{
                JitOptimizationLevel::o2, cache_directory}};
        jit.add_process(
            symbol,
            make_cached_projected_waveform_process(
                second_delay, rejection, mode),
            widths);
        TestRuntime runtime;
        auto descriptor = abi(runtime);
        assert(
            jit.execute(jit.lookup(symbol), descriptor)
            == JitExecutionStatus::completed);
        assert(runtime.projected_writes.size() == 2);
        assert(runtime.projected_writes[0].delay == 5);
        assert(runtime.projected_writes[1].delay == second_delay);
        assert(runtime.projected_writes[0].rejection == rejection);
        assert(runtime.projected_writes[0].mode
               == static_cast<std::uint32_t>(mode));
        expect_cache_statistics(jit, hits, misses, misses);
      };
  run(9, 2, ProjectedDelayMode::inertial, 0, 1);
  run(9, 2, ProjectedDelayMode::inertial, 1, 0);
  run(10, 2, ProjectedDelayMode::inertial, 0, 1);
  run(9, 1, ProjectedDelayMode::inertial, 0, 1);
  run(9, 0, ProjectedDelayMode::transport, 0, 1);
}

void test_signed_shift_cache_identity(
    const std::filesystem::path& cache_directory) {
  constexpr std::string_view symbol{"cached_signed_shift"};
  const std::array<std::uint32_t, 1> widths{4};
  const auto run =
      [&](const bool signed_amount,
          const std::uint64_t hits,
          const std::uint64_t misses,
          const std::string_view expected) {
        Process process;
        process.id = 22;
        process.name = std::string{symbol};
        process.register_count = 3;
        process.operations = {
            LoadConstant{
                0,
                PackedLogic4::from_msb_string("1001")},
            LoadConstant{
                1,
                PackedLogic4::from_msb_string("1111")},
            Shift{
                ShiftOperator::logical_left,
                2,
                0,
                1,
                signed_amount},
            WriteBlocking{0, 2},
            Halt{},
        };
        LlvmJit jit{
            LlvmJitOptions{
                JitOptimizationLevel::o2,
                cache_directory}};
        jit.add_process(symbol, process, widths);
        TestRuntime runtime;
        auto descriptor = abi(runtime);
        assert(
            jit.execute(jit.lookup(symbol), descriptor)
            == JitExecutionStatus::completed);
        const auto encoded =
            PackedLogic4::from_msb_string(expected).low_word();
        assert((
            runtime.signals[0]
            == EncodedSignal{encoded.aval, encoded.bval}));
        expect_cache_statistics(jit, hits, misses, misses);
      };

  run(false, 0, 1, "0000");
  run(true, 0, 1, "0100");
  run(true, 1, 0, "0100");
  assert(cached_object_paths(cache_directory).size() == 2);
}

void test_integer_cache_identity(
    const std::filesystem::path& cache_directory) {
  constexpr std::string_view symbol{"cached_integer"};
  const std::array<std::uint32_t, 1> widths{32};
  const auto integer = [](const std::int32_t value) {
    return PackedLogic4::from_aval_bval(
        32, static_cast<std::uint32_t>(value), 0);
  };
  const auto run =
      [&](const IntegerBinaryOperator operation,
          const std::int32_t lower,
          const std::int32_t upper,
          const std::int32_t expected,
          const std::size_t hits,
          const std::size_t misses) {
        Process process;
        process.id = 33;
        process.name = "cached_integer";
        process.register_count = 3;
        process.operations = {
            LoadConstant{0, integer(3)},
            LoadConstant{1, integer(2)},
            IntegerBinary{operation, 2, 0, 1},
            IntegerCheck{2, lower, upper},
            WriteBlocking{0, 2},
            Halt{}};
        LlvmJit jit{
            LlvmJitOptions{
                JitOptimizationLevel::o2, cache_directory}};
        jit.add_process(symbol, process, widths);
        TestRuntime runtime;
        auto descriptor = abi(runtime);
        assert(
            jit.execute(jit.lookup(symbol), descriptor)
            == JitExecutionStatus::completed);
        assert((
            runtime.signals[0]
            == EncodedSignal{
                static_cast<std::uint32_t>(expected), 0}));
        expect_cache_statistics(jit, hits, misses, misses);
      };
  run(IntegerBinaryOperator::add, -5, 7, 5, 0, 1);
  run(IntegerBinaryOperator::add, -5, 7, 5, 1, 0);
  run(IntegerBinaryOperator::subtract, -5, 7, 1, 0, 1);
  run(IntegerBinaryOperator::add, -5, 8, 5, 0, 1);
  assert(cached_object_paths(cache_directory).size() == 3);
}

void test_signal_kind_cache_identity(
    const std::filesystem::path& cache_directory) {
  constexpr std::string_view symbol = "cached_signal_kind";
  const std::array<std::uint32_t, 2> widths{8, 8};
  const auto process = make_cached_signal_process(0);
  const auto options = LlvmJitOptions{
      JitOptimizationLevel::o2, cache_directory};

  {
    LlvmJit cold{options};
    const std::array<ValueKind, 2> kinds{
        ValueKind::logic4, ValueKind::logic4};
    cold.add_process(symbol, process, widths, kinds);
    run_cached_signal_process(cold, symbol);
    expect_cache_statistics(cold, 0, 1, 1);
  }
  {
    LlvmJit unrelated_change{options};
    const std::array<ValueKind, 2> kinds{
        ValueKind::logic4, ValueKind::logic9};
    unrelated_change.add_process(
        symbol, process, widths, kinds);
    run_cached_signal_process(unrelated_change, symbol);
    expect_cache_statistics(unrelated_change, 1, 0, 0);
  }
  {
    LlvmJit referenced_change{options};
    const std::array<ValueKind, 2> kinds{
        ValueKind::logic9, ValueKind::logic4};
    referenced_change.add_process(
        symbol, process, widths, kinds);
    run_cached_signal_process(referenced_change, symbol);
    expect_cache_statistics(referenced_change, 0, 1, 1);
  }
  {
    LlvmJit exact_warm{options};
    const std::array<ValueKind, 2> kinds{
        ValueKind::logic9, ValueKind::logic4};
    exact_warm.add_process(symbol, process, widths, kinds);
    run_cached_signal_process(exact_warm, symbol);
    expect_cache_statistics(exact_warm, 1, 0, 0);
  }
  assert(cached_object_paths(cache_directory).size() == 2);
}

void test_container_predicate_cache_identity(
    const std::filesystem::path& cache_directory) {
  const auto make_process =
      [](const std::uint8_t constant,
         const ContainerPredicateOperator comparison,
         const ContainerPredicateValueKind value_kind =
             ContainerPredicateValueKind::element) {
        ContainerType queue;
        queue.element_width = 8;
        queue.queue = true;
        Process process;
        process.id = 31;
        process.name = "cached_container_predicate";
        process.container_register_count = 2;
        process.container_register_types = {queue, queue};
        process.operations = {
            LocateContainer{
                ContainerLocatorOperator::find,
                0,
                1,
                {
                    {value_kind
                             == ContainerPredicateValueKind::index
                         ? ContainerPredicateOperator::index
                         : ContainerPredicateOperator::item,
                     0, 0, PackedLogic4{}, value_kind},
                    {ContainerPredicateOperator::constant, 0, 0,
                     PackedLogic4::from_aval_bval(
                         value_kind
                                 == ContainerPredicateValueKind::index
                             ? 32
                             : 8,
                         constant, 0),
                     value_kind},
                    {comparison, 0, 1, PackedLogic4{},
                     ContainerPredicateValueKind::logical},
                },
                {}},
            Halt{},
        };
        return process;
      };
  constexpr std::string_view symbol =
      "cached_container_predicate";
  const std::array<std::uint32_t, 0> no_signals{};
  const auto options = LlvmJitOptions{
      JitOptimizationLevel::o2, cache_directory};
  const auto materialize =
      [&](const Process& process,
          const std::uint64_t hits,
          const std::uint64_t misses) {
        LlvmJit jit{options};
        jit.add_process(symbol, process, no_signals);
        assert(jit.lookup(symbol));
        expect_cache_statistics(
            jit, hits, misses, misses);
      };
  materialize(
      make_process(5, ContainerPredicateOperator::greater),
      0, 1);
  materialize(
      make_process(5, ContainerPredicateOperator::greater),
      1, 0);
  materialize(
      make_process(6, ContainerPredicateOperator::greater),
      0, 1);
  materialize(
      make_process(5, ContainerPredicateOperator::less),
      0, 1);
  materialize(
      make_process(
          5, ContainerPredicateOperator::greater,
          ContainerPredicateValueKind::index),
      0, 1);
  materialize(
      make_process(
          5, ContainerPredicateOperator::greater,
          ContainerPredicateValueKind::index),
      1, 0);
  assert(cached_object_paths(cache_directory).size() == 4);
}

void test_container_reduction_cache_identity(
    const std::filesystem::path& cache_directory) {
  const auto make_process =
      [](const bool with_transformation,
         const std::uint32_t threshold,
         const ContainerPredicateValueKind comparison_kind,
         const bool swap_branches = false) {
        ContainerType queue;
        queue.element_width = 32;
        queue.queue = true;
        Process process;
        process.id = 32;
        process.name = "cached_container_reduction";
        process.register_count = 1;
        process.container_register_count = 1;
        process.container_register_types = {queue};
        std::vector<ContainerPredicateNode> transformation;
        if (with_transformation) {
          transformation = {
              {ContainerPredicateOperator::item, 0, 0,
               PackedLogic4{},
               ContainerPredicateValueKind::element},
              {comparison_kind
                       == ContainerPredicateValueKind::index
                   ? ContainerPredicateOperator::index
                   : ContainerPredicateOperator::item,
               0, 0, PackedLogic4{}, comparison_kind},
              {ContainerPredicateOperator::constant, 0, 0,
               PackedLogic4::from_aval_bval(
                   32, threshold, 0),
               comparison_kind},
              {ContainerPredicateOperator::greater, 1, 2,
               PackedLogic4{},
               ContainerPredicateValueKind::logical},
              {ContainerPredicateOperator::constant, 0, 0,
               PackedLogic4::from_aval_bval(32, 0, 0),
               ContainerPredicateValueKind::element},
              {ContainerPredicateOperator::conditional, 3,
               swap_branches ? 4U : 0U,
               PackedLogic4{},
               ContainerPredicateValueKind::element,
               swap_branches ? 0U : 4U},
          };
        }
        process.operations = {
            ContainerReduction{
                ContainerReductionOperator::sum,
                0,
                0,
                std::move(transformation)},
            Halt{},
        };
        return process;
      };
  constexpr std::string_view symbol =
      "cached_container_reduction";
  const std::array<std::uint32_t, 0> no_signals{};
  const auto options = LlvmJitOptions{
      JitOptimizationLevel::o2, cache_directory};
  const auto materialize =
      [&](const Process& process,
          const std::uint64_t hits,
          const std::uint64_t misses) {
        LlvmJit jit{options};
        jit.add_process(symbol, process, no_signals);
        assert(jit.lookup(symbol));
        expect_cache_statistics(jit, hits, misses, misses);
      };
  materialize(
      make_process(
          false, 0, ContainerPredicateValueKind::index),
      0, 1);
  materialize(
      make_process(
          false, 0, ContainerPredicateValueKind::index),
      1, 0);
  materialize(
      make_process(
          true, 0, ContainerPredicateValueKind::index),
      0, 1);
  materialize(
      make_process(
          true, 0, ContainerPredicateValueKind::index),
      1, 0);
  materialize(
      make_process(
          true, 1, ContainerPredicateValueKind::index),
      0, 1);
  materialize(
      make_process(
          true, 1, ContainerPredicateValueKind::element),
      0, 1);
  materialize(
      make_process(
          true, 1, ContainerPredicateValueKind::element,
          true),
      0, 1);
  assert(cached_object_paths(cache_directory).size() == 5);
}

void test_container_ordering_cache_identity(
    const std::filesystem::path& cache_directory) {
  const auto make_process =
      [](const bool with_key,
         const std::uint32_t threshold,
         const ContainerPredicateValueKind comparison_kind,
         const bool swap_branches = false,
         const ContainerOrderingOperator ordering =
             ContainerOrderingOperator::ascending) {
        ContainerType queue;
        queue.element_width = 32;
        queue.queue = true;
        Process process;
        process.id = 33;
        process.name = "cached_container_ordering";
        process.container_register_count = 1;
        process.container_register_types = {queue};
        std::vector<ContainerPredicateNode> key;
        if (with_key) {
          key = {
              {ContainerPredicateOperator::item, 0, 0,
               PackedLogic4{},
               ContainerPredicateValueKind::element},
              {comparison_kind
                       == ContainerPredicateValueKind::index
                   ? ContainerPredicateOperator::index
                   : ContainerPredicateOperator::item,
               0, 0, PackedLogic4{}, comparison_kind},
              {ContainerPredicateOperator::constant, 0, 0,
               PackedLogic4::from_aval_bval(
                   32, threshold, 0),
               comparison_kind},
              {ContainerPredicateOperator::greater, 1, 2,
               PackedLogic4{},
               ContainerPredicateValueKind::logical},
              {ContainerPredicateOperator::constant, 0, 0,
               PackedLogic4::from_aval_bval(32, 0, 0),
               ContainerPredicateValueKind::element},
              {ContainerPredicateOperator::conditional, 3,
               swap_branches ? 4U : 0U,
               PackedLogic4{},
               ContainerPredicateValueKind::element,
               swap_branches ? 0U : 4U},
          };
        }
        process.operations = {
            OrderContainer{
                ordering, 0, std::move(key)},
            Halt{},
        };
        return process;
      };
  constexpr std::string_view symbol =
      "cached_container_ordering";
  const std::array<std::uint32_t, 0> no_signals{};
  const auto options = LlvmJitOptions{
      JitOptimizationLevel::o2, cache_directory};
  const auto materialize =
      [&](const Process& process,
          const std::uint64_t hits,
          const std::uint64_t misses) {
        LlvmJit jit{options};
        jit.add_process(symbol, process, no_signals);
        assert(jit.lookup(symbol));
        expect_cache_statistics(jit, hits, misses, misses);
      };
  materialize(
      make_process(
          false, 0, ContainerPredicateValueKind::index),
      0, 1);
  materialize(
      make_process(
          false, 0, ContainerPredicateValueKind::index),
      1, 0);
  materialize(
      make_process(
          true, 0, ContainerPredicateValueKind::index),
      0, 1);
  materialize(
      make_process(
          true, 0, ContainerPredicateValueKind::index),
      1, 0);
  materialize(
      make_process(
          true, 1, ContainerPredicateValueKind::index),
      0, 1);
  materialize(
      make_process(
          true, 1, ContainerPredicateValueKind::element),
      0, 1);
  materialize(
      make_process(
          true, 1, ContainerPredicateValueKind::element,
          true),
      0, 1);
  materialize(
      make_process(
          true, 1, ContainerPredicateValueKind::element,
          false, ContainerOrderingOperator::descending),
      0, 1);
  assert(cached_object_paths(cache_directory).size() == 6);
}

void test_container_locator_transformation_cache_identity(
    const std::filesystem::path& cache_directory) {
  const auto make_process =
      [](const bool with_transformation,
         const std::uint32_t threshold,
         const ContainerPredicateValueKind comparison_kind,
         const bool swap_branches = false,
         const ContainerLocatorOperator locator =
             ContainerLocatorOperator::minimum) {
        ContainerType queue;
        queue.element_width = 32;
        queue.queue = true;
        Process process;
        process.id = 34;
        process.name = "cached_locator_transformation";
        process.container_register_count = 2;
        process.container_register_types = {queue, queue};
        std::vector<ContainerPredicateNode> transformation;
        if (with_transformation) {
          transformation = {
              {ContainerPredicateOperator::item, 0, 0,
               PackedLogic4{},
               ContainerPredicateValueKind::element},
              {comparison_kind
                       == ContainerPredicateValueKind::index
                   ? ContainerPredicateOperator::index
                   : ContainerPredicateOperator::item,
               0, 0, PackedLogic4{}, comparison_kind},
              {ContainerPredicateOperator::constant, 0, 0,
               PackedLogic4::from_aval_bval(
                   32, threshold, 0),
               comparison_kind},
              {ContainerPredicateOperator::greater, 1, 2,
               PackedLogic4{},
               ContainerPredicateValueKind::logical},
              {ContainerPredicateOperator::constant, 0, 0,
               PackedLogic4::from_aval_bval(32, 0, 0),
               ContainerPredicateValueKind::element},
              {ContainerPredicateOperator::conditional, 3,
               swap_branches ? 4U : 0U,
               PackedLogic4{},
               ContainerPredicateValueKind::element,
               swap_branches ? 0U : 4U},
          };
        }
        process.operations = {
            LocateContainer{
                locator, 0, 1, {},
                std::move(transformation)},
            Halt{},
        };
        return process;
      };
  constexpr std::string_view symbol =
      "cached_locator_transformation";
  const std::array<std::uint32_t, 0> no_signals{};
  const auto options = LlvmJitOptions{
      JitOptimizationLevel::o2, cache_directory};
  const auto materialize =
      [&](const Process& process,
          const std::uint64_t hits,
          const std::uint64_t misses) {
        LlvmJit jit{options};
        jit.add_process(symbol, process, no_signals);
        assert(jit.lookup(symbol));
        expect_cache_statistics(jit, hits, misses, misses);
      };
  materialize(
      make_process(
          false, 0, ContainerPredicateValueKind::index),
      0, 1);
  materialize(
      make_process(
          false, 0, ContainerPredicateValueKind::index),
      1, 0);
  materialize(
      make_process(
          true, 0, ContainerPredicateValueKind::index),
      0, 1);
  materialize(
      make_process(
          true, 0, ContainerPredicateValueKind::index),
      1, 0);
  materialize(
      make_process(
          true, 1, ContainerPredicateValueKind::index),
      0, 1);
  materialize(
      make_process(
          true, 1, ContainerPredicateValueKind::element),
      0, 1);
  materialize(
      make_process(
          true, 1, ContainerPredicateValueKind::element,
          true),
      0, 1);
  materialize(
      make_process(
          true, 1, ContainerPredicateValueKind::element,
          false, ContainerLocatorOperator::maximum),
      0, 1);
  assert(cached_object_paths(cache_directory).size() == 6);
}

void test_persistent_object_cache() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("fsim-llvm-object-cache-" + std::to_string(serial));
  std::error_code error;
  std::filesystem::remove_all(root, error);
  assert(!error);

  test_object_cache_at_level(JitOptimizationLevel::o0, root / "o0");
  test_object_cache_at_level(JitOptimizationLevel::o2, root / "o2");
  test_optimization_cache_invalidation(root / "optimization");
  test_process_module_grouping_at_level(
      JitOptimizationLevel::o0, root / "group-o0");
  test_process_module_grouping_at_level(
      JitOptimizationLevel::o2, root / "group-o2");
  test_cache_pruning_integration(root / "pruning");
  test_inertial_cache_identity(root / "inertial");
  test_projected_cache_identity(root / "projected");
  test_projected_waveform_cache_identity(
      root / "projected-waveform");
  test_signed_shift_cache_identity(root / "signed-shift");
  test_integer_cache_identity(root / "integer");
  test_signal_kind_cache_identity(root / "signal-kind");
  test_container_predicate_cache_identity(
      root / "container-predicate");
  test_container_reduction_cache_identity(
      root / "container-reduction");
  test_container_ordering_cache_identity(
      root / "container-ordering");
  test_container_locator_transformation_cache_identity(
      root / "locator-transformation");

  std::filesystem::remove_all(root, error);
  assert(!error);
}

} // namespace fsim::tests::compiler
