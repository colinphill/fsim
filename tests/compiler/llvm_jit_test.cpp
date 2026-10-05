// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"
#include "llvm_jit_internal.hpp"
#include "native_cache_schema.hpp"

#include <fsim/runtime/logic.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <future>
#include <latch>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::compiler {

namespace {

struct CapturedUpdateWrite {
  std::uint32_t signal { };
  std::uint64_t aval { };
  std::uint64_t bval { };
  std::uint32_t domain { };

  friend bool operator==(CapturedUpdateWrite, CapturedUpdateWrite) = default;
};

extern "C" void capture_update_write(
    void* const opaque,
    const std::uint32_t signal,
    const std::uint64_t aval,
    const std::uint64_t bval,
    const std::uint32_t domain)
{
  auto& writes = *static_cast<std::vector<CapturedUpdateWrite>*>(opaque);
  writes.push_back({ signal, aval, bval, domain });
}

[[nodiscard]] PackedLogic4 read_register_value(
    const fsim::compiler::JitProcessFrameLayout& layout,
    const RegisterId reg,
    const std::vector<std::uint64_t>& register_aval,
    const std::vector<std::uint64_t>& register_bval)
{
  const auto width = layout.register_widths[reg];
  const auto offset = layout.register_word_offsets[reg];
  const auto word_count = (static_cast<std::size_t>(width) + 63U) / 64U;
  return PackedLogic4::from_word_planes(
      width,
      std::span { register_aval }.subspan(offset, word_count),
      std::span { register_bval }.subspan(offset, word_count));
}

[[nodiscard]] Logic4 reduce_logic4_reference(
    const ReductionOperator operation,
    const PackedLogic4& source)
{
  auto result = operation == ReductionOperator::bit_and
      ? Logic4::one
      : Logic4::zero;
  for (std::size_t bit = 0U; bit < source.width(); ++bit) {
    if (operation == ReductionOperator::bit_and) {
      result = fsim::runtime::logic_and(result, source.get(bit));
    } else if (operation == ReductionOperator::bit_or) {
      result = fsim::runtime::logic_or(result, source.get(bit));
    } else {
      result = fsim::runtime::logic_xor(result, source.get(bit));
    }
  }
  return result;
}

[[nodiscard]] std::uint64_t count_ones_reference(
    const PackedLogic4& source)
{
  std::uint64_t count = 0U;
  for (std::size_t bit = 0U; bit < source.width(); ++bit) {
    if (source.get(bit) == Logic4::one) {
      ++count;
    }
  }
  return count;
}

[[nodiscard]] std::uint64_t count_bits_reference(
    const PackedLogic4& source,
    const std::uint8_t state_mask)
{
  std::uint64_t count = 0U;
  for (std::size_t bit = 0U; bit < source.width(); ++bit) {
    const auto state = static_cast<std::uint8_t>(source.get(bit));
    if ((state_mask & (std::uint8_t { 1U } << state)) != 0U) {
      ++count;
    }
  }
  return count;
}

[[nodiscard]] PackedLogic4 logic4_binary_reference(
    const BinaryOperator operation,
    const PackedLogic4& lhs,
    const PackedLogic4& rhs)
{
  assert(lhs.width() == rhs.width());
  auto result = PackedLogic4(lhs.width(), Logic4::zero);
  for (std::size_t bit = 0U; bit < lhs.width(); ++bit) {
    Logic4 value = Logic4::x;
    if (operation == BinaryOperator::bit_and) {
      value = fsim::runtime::logic_and(lhs.get(bit), rhs.get(bit));
    } else if (operation == BinaryOperator::bit_or) {
      value = fsim::runtime::logic_or(lhs.get(bit), rhs.get(bit));
    } else {
      value = fsim::runtime::logic_xor(lhs.get(bit), rhs.get(bit));
    }
    result.set(bit, value);
  }
  return result;
}

[[nodiscard]] std::uint64_t read_known_count(
    const PackedLogic4& value)
{
  const auto count = value.known_unsigned_value();
  assert(count);
  return *count;
}

} // namespace

void test_concurrent_uncached_module_materialization(const bool lower_in_worker)
{
  constexpr std::size_t module_count = 8U;
  LlvmJit jit { LlvmJitOptions { JitOptimizationLevel::o0, { } } };
  std::array<Process, module_count> processes;
  std::array<std::string, module_count> symbols;

  for (std::size_t module = 0U; module < module_count; ++module) {
    auto& process = processes[module];
    process.id = static_cast<std::uint32_t>(module);
    process.name = "concurrent_uncached_module_" + std::to_string(module);
    process.register_count = 1U;
    for (std::size_t operation = 0U; operation < 64U; ++operation) {
      process.operations.emplace_back(LoadConstant {
          0U,
          PackedLogic4::from_aval_bval(
              32U, static_cast<std::uint64_t>(module * 64U + operation), 0U),
      });
    }
    process.operations.emplace_back(Halt { });
    symbols[module] = "fsim_concurrent_uncached_" + std::to_string(module);
    if (!lower_in_worker) {
      jit.add_process(symbols[module], process, { });
    }
  }

  std::latch ready { static_cast<std::ptrdiff_t>(module_count) };
  std::latch start { 1 };
  std::array<std::future<JitProcessHandle>, module_count> lookups;
  for (std::size_t module = 0U; module < module_count; ++module) {
    lookups[module] = std::async(
        std::launch::async,
        [&jit, &ready, &start, &symbols, &processes, module, lower_in_worker] {
          ready.count_down();
          start.wait();
          if (lower_in_worker) {
            jit.add_process(symbols[module], processes[module], { });
          }
          return jit.lookup(symbols[module]);
        });
  }
  ready.wait();
  start.count_down();

  std::array<JitProcessHandle, module_count> handles;
  for (std::size_t module = 0U; module < module_count; ++module) {
    handles[module] = lookups[module].get();
    assert(handles[module]);
  }

  TestRuntime runtime;
  auto descriptor = abi(runtime);
  for (std::size_t module = 0U; module < module_count; ++module) {
    const auto layout = jit.frame_layout(handles[module]);
    std::vector<std::uint64_t> register_aval(layout.register_word_count);
    std::vector<std::uint64_t> register_bval(layout.register_word_count);
    std::vector<std::uint8_t> register_initialized(layout.register_count);
    fsim_jit_frame_v2 frame { };
    jit.initialize_frame(
        handles[module], frame, register_aval, register_bval,
        register_initialized);
    auto result = new_resume_result();
    assert(jit.resume(handles[module], descriptor, frame, result)
        == JitResumeStatus::completed);

    const auto offset = layout.register_word_offsets[0U];
    const auto expected = static_cast<std::uint64_t>(module * 64U + 63U);
    assert(register_initialized[0U] != 0U);
    assert(register_aval[offset] == expected);
    assert(register_bval[offset] == 0U);
  }
}

void test_scheduling_domain_native_at_level(
    const JitOptimizationLevel optimization,
    const std::filesystem::path& cache_directory,
    const std::string_view symbol)
{
  const auto options = LlvmJitOptions { optimization, cache_directory };
  const std::array<std::uint32_t, 1> signal_widths { 8U };
  LlvmJit jit { options };

  Process process;
  process.id = 182U;
  process.name = std::string { symbol } + "_mixed_domains";
  process.register_count = 1U;
  process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
  process.operations = {
      LoadConstant {
          0U, PackedLogic4::from_aval_bval(8U, 0x35U, 0U) },
      WriteUpdate { 0U, 0U, SignalUpdateDomain::generic },
      LoadConstant {
          0U, PackedLogic4::from_aval_bval(8U, 0xa6U, 0U) },
      WriteUpdate {
          0U, 0U, SignalUpdateDomain::systemverilog_active },
      Halt { },
  };
  assert(jit.supports_process(process, signal_widths));
  jit.add_process(process.name, process, signal_widths);
  const auto handle = jit.lookup(process.name);
  assert(handle);

  const auto layout = jit.frame_layout(handle);
  assert(layout.direct_update_signals.empty());
  std::vector<std::uint64_t> register_aval(layout.register_word_count);
  std::vector<std::uint64_t> register_bval(layout.register_word_count);
  std::vector<std::uint8_t> register_initialized(layout.register_count);
  fsim_jit_frame_v2 frame { };
  jit.initialize_frame(
      handle, frame, register_aval, register_bval, register_initialized);

  TestRuntime test_runtime;
  auto runtime = abi(test_runtime);
  auto services = *runtime.services;
  services.write_update = &capture_update_write;
  runtime.services = &services;
  std::vector<CapturedUpdateWrite> writes;
  runtime.context = &writes;

  auto result = new_resume_result();
  assert(jit.resume(handle, runtime, frame, result)
      == JitResumeStatus::completed);
  assert((writes == std::vector<CapturedUpdateWrite> {
      { 0U, 0x35U, 0U,
          static_cast<std::uint32_t>(SignalUpdateDomain::generic) },
      { 0U, 0xa6U, 0U,
          static_cast<std::uint32_t>(
              SignalUpdateDomain::systemverilog_active) },
  }));
}

void test_exhaustive_logic4_reductions_and_counts_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
  Process process;
  process.id = 180U;
  process.name = std::string { symbol };
  process.register_count = 22U;
  process.operations = {
      ReadSignal { 0U, 0U },
      Reduction { ReductionOperator::bit_and, 1U, 0U },
      Reduction { ReductionOperator::bit_or, 2U, 0U },
      Reduction { ReductionOperator::bit_xor, 3U, 0U },
      Reduction { ReductionOperator::one_hot, 4U, 0U },
      Reduction { ReductionOperator::one_hot_or_zero, 5U, 0U },
      CountOnes { 6U, 0U },
  };
  for (std::uint8_t state_mask = 1U; state_mask <= 15U; ++state_mask) {
    process.operations.emplace_back(CountBits {
        static_cast<RegisterId>(6U + state_mask), 0U, state_mask });
  }
  process.operations.emplace_back(Pause { });
  process.operations.emplace_back(Halt { });

  LlvmJit jit { LlvmJitOptions { optimization, { } } };
  const std::array<std::uint32_t, 1> signal_widths { 4U };
  jit.add_process(symbol, process, signal_widths);
  const auto handle = jit.lookup(symbol);
  const auto layout = jit.frame_layout(handle);
  TestRuntime runtime;
  auto descriptor = abi(runtime);

  for (std::uint32_t encoding = 0U; encoding < 256U; ++encoding) {
    const auto aval = static_cast<std::uint64_t>(encoding & 0x0FU);
    const auto bval = static_cast<std::uint64_t>((encoding >> 4U) & 0x0FU);
    runtime.signals[0] = { aval, bval };
    const auto source = PackedLogic4::from_aval_bval(4U, aval, bval);

    std::vector<std::uint64_t> register_aval(layout.register_word_count);
    std::vector<std::uint64_t> register_bval(layout.register_word_count);
    std::vector<std::uint8_t> register_initialized(layout.register_count);
    fsim_jit_frame_v2 frame { };
    jit.initialize_frame(
        handle, frame, register_aval, register_bval,
        register_initialized);
    auto result = new_resume_result();
    assert(jit.resume(handle, descriptor, frame, result)
        == JitResumeStatus::paused);

    assert(read_register_value(layout, 1U, register_aval, register_bval)
        .get(0U) == reduce_logic4_reference(
            ReductionOperator::bit_and, source));
    assert(read_register_value(layout, 2U, register_aval, register_bval)
        .get(0U) == reduce_logic4_reference(
            ReductionOperator::bit_or, source));
    assert(read_register_value(layout, 3U, register_aval, register_bval)
        .get(0U) == reduce_logic4_reference(
            ReductionOperator::bit_xor, source));

    std::uint64_t exact_one_count = 0U;
    for (std::size_t bit = 0U; bit < source.width(); ++bit) {
      exact_one_count += source.get(bit) == Logic4::one ? 1U : 0U;
    }
    const auto one_hot = exact_one_count == 1U;
    const auto one_hot_or_zero = exact_one_count <= 1U;
    assert(read_register_value(layout, 4U, register_aval, register_bval)
        .get(0U) == (one_hot ? Logic4::one : Logic4::zero));
    assert(read_register_value(layout, 5U, register_aval, register_bval)
        .get(0U) == (one_hot_or_zero ? Logic4::one : Logic4::zero));
    assert(read_known_count(
               read_register_value(
                   layout, 6U, register_aval, register_bval))
        == count_ones_reference(source));
    for (std::uint8_t state_mask = 1U; state_mask <= 15U; ++state_mask) {
      assert(read_known_count(read_register_value(
                 layout,
                 static_cast<RegisterId>(6U + state_mask),
                 register_aval,
                 register_bval))
          == count_bits_reference(source, state_mask));
    }
  }
}

void test_wide_logic4_reductions_and_counts_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
  constexpr std::array<std::uint32_t, 4> widths {
      65U, 129U, 256U, 1024U
  };
  constexpr std::array states {
      Logic4::zero, Logic4::one, Logic4::x, Logic4::z
  };

  LlvmJit jit { LlvmJitOptions { optimization, { } } };
  for (const auto width : widths) {
    auto mixed = PackedLogic4(width, Logic4::zero);
    for (std::uint32_t bit = 0U; bit < width; ++bit) {
      mixed.set(bit, states[bit % states.size()]);
    }
    auto tail_one = PackedLogic4(width, Logic4::zero);
    tail_one.set(width - 1U, Logic4::one);
    auto tail_unknown = PackedLogic4(width, Logic4::zero);
    tail_unknown.set(width - 1U, Logic4::x);
    auto tail_z = PackedLogic4(width, Logic4::zero);
    tail_z.set(width - 1U, Logic4::z);

    Process process;
    process.id = 181U;
    process.name = std::string { symbol } + "_" + std::to_string(width);
    process.register_count = 19U;
    process.operations = {
        ReadSignal { 0U, 0U },
        ReadSignal { 1U, 1U },
        ReadSignal { 2U, 2U },
        ReadSignal { 17U, 3U },
        Binary { BinaryOperator::bit_and, 3U, 0U, 1U },
        Binary { BinaryOperator::bit_or, 4U, 0U, 1U },
        Binary { BinaryOperator::bit_xor, 5U, 0U, 1U },
        Reduction { ReductionOperator::bit_and, 6U, 0U },
        Reduction { ReductionOperator::bit_or, 7U, 0U },
        Reduction { ReductionOperator::bit_xor, 8U, 0U },
        Reduction { ReductionOperator::one_hot, 9U, 1U },
        Reduction { ReductionOperator::one_hot_or_zero, 10U, 1U },
        Reduction { ReductionOperator::one_hot, 11U, 2U },
        Reduction { ReductionOperator::one_hot_or_zero, 12U, 2U },
        CountOnes { 13U, 0U },
        CountBits { 14U, 0U, 0x0CU },
        CountOnes { 15U, 1U },
        CountBits { 16U, 2U, 0x0CU },
        CountBits { 18U, 17U, 0x08U },
        Pause { },
        Halt { },
    };

    const auto process_symbol =
        std::string { symbol } + "_" + std::to_string(width);
    const std::array<std::uint32_t, 4> signal_widths {
        width, width, width, width
    };
    jit.add_process(process_symbol, process, signal_widths);
    const auto handle = jit.lookup(process_symbol);
    const auto layout = jit.frame_layout(handle);
    std::vector<std::uint64_t> register_aval(layout.register_word_count);
    std::vector<std::uint64_t> register_bval(layout.register_word_count);
    std::vector<std::uint8_t> register_initialized(layout.register_count);
    fsim_jit_frame_v2 frame { };
    jit.initialize_frame(
        handle, frame, register_aval, register_bval,
        register_initialized);
    TestRuntime runtime;
    const auto store_wide_signal = [&](const std::size_t signal,
                                       const PackedLogic4& value) {
      runtime.wide_signal_aval[signal].assign(
          value.aval_words().begin(), value.aval_words().end());
      runtime.wide_signal_bval[signal].assign(
          value.bval_words().begin(), value.bval_words().end());
    };
    store_wide_signal(0U, mixed);
    store_wide_signal(1U, tail_one);
    store_wide_signal(2U, tail_unknown);
    store_wide_signal(3U, tail_z);
    auto descriptor = abi(runtime);
    auto result = new_resume_result();
    assert(jit.resume(handle, descriptor, frame, result)
        == JitResumeStatus::paused);

    const auto read = [&](const RegisterId reg) {
      return read_register_value(
          layout, reg, register_aval, register_bval);
    };
    assert(read(3U).to_msb_string()
        == logic4_binary_reference(
               BinaryOperator::bit_and, mixed, tail_one)
               .to_msb_string());
    assert(read(4U).to_msb_string()
        == logic4_binary_reference(
               BinaryOperator::bit_or, mixed, tail_one)
               .to_msb_string());
    assert(read(5U).to_msb_string()
        == logic4_binary_reference(
               BinaryOperator::bit_xor, mixed, tail_one)
               .to_msb_string());
    assert(read(6U).get(0U)
        == reduce_logic4_reference(ReductionOperator::bit_and, mixed));
    assert(read(7U).get(0U)
        == reduce_logic4_reference(ReductionOperator::bit_or, mixed));
    assert(read(8U).get(0U)
        == reduce_logic4_reference(ReductionOperator::bit_xor, mixed));
    assert(read(9U).get(0U) == Logic4::one);
    assert(read(10U).get(0U) == Logic4::one);
    assert(read(11U).get(0U) == Logic4::zero);
    assert(read(12U).get(0U) == Logic4::one);
    assert(read_known_count(read(13U)) == count_ones_reference(mixed));
    assert(read_known_count(read(14U))
        == count_bits_reference(mixed, 0x0CU));
    assert(read_known_count(read(15U)) == 1U);
    assert(read_known_count(read(16U)) == 1U);
    assert(read_known_count(read(18U)) == 1U);
  }
}

} // namespace fsim::tests::compiler

int main(int argc, char** argv) {
  using namespace fsim::tests::compiler;
  if (argc == 2
      && std::string_view { argv[1] } == "--fast-isel-profile") {
    test_fast_isel_census_native_fallback();
    return 0;
  }
  if (argc == 2
      && std::string_view { argv[1] } == "--wide-bitwise-profile") {
    test_wide_bitwise_profile();
    return 0;
  }
  assert(argc == 1);
  // FSIM-CONFORMANCE CF-COMMON-LLVM-001 source=SRC-LLVM expectation=execute
  // FSIM-CONFORMANCE CF-COMMON-CACHE-001 source=SRC-LLVM expectation=execute
  // FSIM-CONFORMANCE CF-COMMON-LLVM-N01 source=SRC-LLVM expectation=reject
  assert(!LlvmJit::llvm_version().empty());
  const auto native_o0 =
      LlvmJit::native_host_identity(JitOptimizationLevel::o0);
  const auto native_o1 =
      LlvmJit::native_host_identity(JitOptimizationLevel::o1);
  const auto native_o2 =
      LlvmJit::native_host_identity(JitOptimizationLevel::o2);
  assert(native_o0.fingerprint.size() == 64);
  assert(native_o1.fingerprint.size() == 64);
  assert(native_o2.fingerprint.size() == 64);
  assert(native_o2.fingerprint
      == fsim::compiler::llvm_detail::make_native_host_identity_fingerprint(
          native_o2, JitOptimizationLevel::o2,
          fsim::compiler::llvm_detail::kNativeObjectCacheSchema));
  assert(native_o2.fingerprint
      != fsim::compiler::llvm_detail::make_native_host_identity_fingerprint(
          native_o2, JitOptimizationLevel::o2,
          "fsim-llvm-native-object-v183"));
  assert(native_o0.fingerprint != native_o1.fingerprint);
  assert(native_o1.fingerprint != native_o2.fingerprint);
  assert(native_o0.fingerprint != native_o2.fingerprint);
  assert(!native_o2.target.empty());
  assert(!native_o2.data_layout.empty());
  assert(!native_o2.cpu.empty());
  assert(
      LlvmJit::native_host_identity(JitOptimizationLevel::o2)
      == native_o2);
  test_backend_tier_and_fast_isel_census();
  test_fast_isel_codegen_preparation();
  test_concurrent_uncached_module_materialization(false);
  test_concurrent_uncached_module_materialization(true);
  test_compilation_context_reuse();
  const auto scheduling_cache_root
      = std::filesystem::temp_directory_path()
      / ("fsim-jit-scheduling-contract-"
          + std::to_string(
              std::chrono::steady_clock::now()
                  .time_since_epoch().count()));
  test_scheduling_domain_native_at_level(
      JitOptimizationLevel::o0,
      scheduling_cache_root / "o0",
      "scheduling_domain_native_o0");
  test_scheduling_domain_native_at_level(
      JitOptimizationLevel::o2,
      scheduling_cache_root / "o2",
      "scheduling_domain_native_o2");
  std::error_code scheduling_cache_error;
  std::filesystem::remove_all(
      scheduling_cache_root, scheduling_cache_error);
  assert(!scheduling_cache_error);
  test_exhaustive_logic4_reductions_and_counts_at_level(
      JitOptimizationLevel::o0, "exhaustive_logic4_reductions_o0");
  test_exhaustive_logic4_reductions_and_counts_at_level(
      JitOptimizationLevel::o2, "exhaustive_logic4_reductions_o2");
  test_wide_logic4_reductions_and_counts_at_level(
      JitOptimizationLevel::o0, "wide_logic4_reductions_o0");
  test_wide_logic4_reductions_and_counts_at_level(
      JitOptimizationLevel::o2, "wide_logic4_reductions_o2");
  run_at_level(JitOptimizationLevel::o0, "arithmetic_o0");
  run_at_level(JitOptimizationLevel::o1, "arithmetic_o1");
  run_at_level(JitOptimizationLevel::o2, "arithmetic_o2");
  test_code_coverage_at_level(
      JitOptimizationLevel::o0, "code_coverage_o0");
  test_code_coverage_at_level(
      JitOptimizationLevel::o1, "code_coverage_o1");
  // Application O2 and O3 both select this optimized native profile.
  test_code_coverage_at_level(
      JitOptimizationLevel::o2, "code_coverage_o2_o3");
  test_code_coverage_cache_identity();
  test_scalar_truth_tables_and_64_bits();
  test_systemverilog_scalar_transport_at_level(
      JitOptimizationLevel::o0, "systemverilog_scalar_o0");
  test_systemverilog_scalar_transport_at_level(
      JitOptimizationLevel::o2, "systemverilog_scalar_o2");
  test_wide_register_frame_at_level(
      JitOptimizationLevel::o0, "wide_register_frame_o0");
  test_wide_register_frame_at_level(
      JitOptimizationLevel::o2, "wide_register_frame_o2");
  test_wide_transient_register_frame_at_level(
      JitOptimizationLevel::o0, "wide_transient_register_frame_o0");
  test_wide_transient_register_frame_at_level(
      JitOptimizationLevel::o2, "wide_transient_register_frame_o2");
  test_optimized_frame_initialization_elision_at_level(
      JitOptimizationLevel::o0, "optimized_frame_initialization_o0");
  test_optimized_frame_initialization_elision_at_level(
      JitOptimizationLevel::o2, "optimized_frame_initialization_o2");
  test_wide_signal_read_at_level(
      JitOptimizationLevel::o0, "wide_signal_read_o0");
  test_wide_signal_read_at_level(
      JitOptimizationLevel::o2, "wide_signal_read_o2");
  test_element_alias_wide_direct_read_at_level(
      JitOptimizationLevel::o0, "element_alias_wide_direct_read_o0");
  test_element_alias_wide_direct_read_at_level(
      JitOptimizationLevel::o2, "element_alias_wide_direct_read_o2");
  test_wide_signal_attributes_at_level(
      JitOptimizationLevel::o0, "wide_signal_attributes_o0");
  test_wide_signal_attributes_at_level(
      JitOptimizationLevel::o2, "wide_signal_attributes_o2");
  test_wide_signal_write_at_level(
      JitOptimizationLevel::o0, "wide_signal_write_o0");
  test_wide_signal_write_at_level(
      JitOptimizationLevel::o2, "wide_signal_write_o2");
  test_wide_container_operations_at_level(
      JitOptimizationLevel::o0, "wide_container_operations_o0");
  test_wide_container_operations_at_level(
      JitOptimizationLevel::o2, "wide_container_operations_o2");
  test_fused_container_object_read_at_level(
      JitOptimizationLevel::o0, "fused_container_object_read_o0");
  test_fused_container_object_read_at_level(
      JitOptimizationLevel::o2, "fused_container_object_read_o2");
  test_fused_container_object_read_at_level(
      JitOptimizationLevel::o0,
      "generic_container_object_read_wide_o0", 64U);
  test_fused_container_object_read_at_level(
      JitOptimizationLevel::o2,
      "generic_container_object_read_wide_o2", 64U);
  test_fused_container_object_read_at_level(
      JitOptimizationLevel::o0,
      "generic_container_object_read_logic9_o0", 32U, true);
  test_fused_container_object_read_at_level(
      JitOptimizationLevel::o2,
      "generic_container_object_read_logic9_o2", 32U, true);
  test_fused_container_object_native_frame_at_level(
      JitOptimizationLevel::o0,
      "native_callable_container_read_o0", true);
  test_fused_container_object_native_frame_at_level(
      JitOptimizationLevel::o2,
      "native_callable_container_read_o2", true);
  test_fused_container_object_native_frame_at_level(
      JitOptimizationLevel::o0,
      "checked_callable_container_read_o0", false);
  test_fused_container_object_native_frame_at_level(
      JitOptimizationLevel::o2,
      "checked_callable_container_read_o2", false);
  test_wide_value_operations_at_level(
      JitOptimizationLevel::o0, "wide_value_operations_o0");
  test_wide_value_operations_at_level(
      JitOptimizationLevel::o2, "wide_value_operations_o2");
  test_constant_dynamic_part_select_at_level(
      JitOptimizationLevel::o0, "constant_dynamic_part_select_o0");
  test_constant_dynamic_part_select_at_level(
      JitOptimizationLevel::o2, "constant_dynamic_part_select_o2");
  test_logic4_constant_dynamic_part_select_at_level(
      JitOptimizationLevel::o0, "logic4_constant_dynamic_part_select_o0");
  test_logic4_constant_dynamic_part_select_at_level(
      JitOptimizationLevel::o2, "logic4_constant_dynamic_part_select_o2");
  test_affine_dynamic_extract_fusion_at_level(
      JitOptimizationLevel::o0, "affine_dynamic_extract_fusion_o0");
  test_affine_dynamic_extract_fusion_at_level(
      JitOptimizationLevel::o2, "affine_dynamic_extract_fusion_o2");
  test_fused_dynamic_part_signal_read_at_level(
      JitOptimizationLevel::o0, "fused_dynamic_part_signal_read_o0");
  test_fused_dynamic_part_signal_read_at_level(
      JitOptimizationLevel::o2, "fused_dynamic_part_signal_read_o2");
  test_wide_single_bit_dynamic_part_select_at_level(
      JitOptimizationLevel::o0, "wide_single_bit_dynamic_part_select_o0");
  test_wide_single_bit_dynamic_part_select_at_level(
      JitOptimizationLevel::o2, "wide_single_bit_dynamic_part_select_o2");
  test_wide_dynamic_part_select_at_level(
      JitOptimizationLevel::o0, "wide_dynamic_part_select_o0");
  test_wide_dynamic_part_select_at_level(
      JitOptimizationLevel::o2, "wide_dynamic_part_select_o2");
  test_dynamic_part_interval_operations_at_level(
      JitOptimizationLevel::o0, "dynamic_part_interval_operations_o0");
  test_dynamic_part_interval_operations_at_level(
      JitOptimizationLevel::o2, "dynamic_part_interval_operations_o2");
  test_dynamic_part_write_closed_form_at_level(
      JitOptimizationLevel::o0, "dynamic_part_write_closed_form_o0");
  test_dynamic_part_write_closed_form_at_level(
      JitOptimizationLevel::o2, "dynamic_part_write_closed_form_o2");
  test_wide_dynamic_single_bit_operations_at_level(
      JitOptimizationLevel::o0, "wide_dynamic_single_bit_operations_o0");
  test_wide_dynamic_single_bit_operations_at_level(
      JitOptimizationLevel::o2, "wide_dynamic_single_bit_operations_o2");
  test_conditional_select_at_level(
      JitOptimizationLevel::o0, "conditional_select_o0");
  test_conditional_select_at_level(
      JitOptimizationLevel::o2, "conditional_select_o2");
  test_wildcard_case_matching_at_level(
      JitOptimizationLevel::o0, "wildcard_case_o0");
  test_wildcard_case_matching_at_level(
      JitOptimizationLevel::o2, "wildcard_case_o2");
  test_comparisons_at_level(
      JitOptimizationLevel::o0, "comparisons_o0");
  test_comparisons_at_level(
      JitOptimizationLevel::o2, "comparisons_o2");
  test_logical_binary_at_level(
      JitOptimizationLevel::o0, "logical_binary_o0");
  test_logical_binary_at_level(
      JitOptimizationLevel::o2, "logical_binary_o2");
  test_reduction_and_shift_at_level(
      JitOptimizationLevel::o0, "reduction_shift_o0");
  test_reduction_and_shift_at_level(
      JitOptimizationLevel::o2, "reduction_shift_o2");
  test_signed_shift_counts_at_level(
      JitOptimizationLevel::o0, "signed_shift_counts_o0");
  test_signed_shift_counts_at_level(
      JitOptimizationLevel::o2, "signed_shift_counts_o2");
  test_unsigned_arithmetic_at_level(
      JitOptimizationLevel::o0, "unsigned_arithmetic_o0");
  test_unsigned_arithmetic_at_level(
      JitOptimizationLevel::o2, "unsigned_arithmetic_o2");
  test_signed_arithmetic_at_level(
      JitOptimizationLevel::o0, "signed_arithmetic_o0");
  test_signed_arithmetic_at_level(
      JitOptimizationLevel::o2, "signed_arithmetic_o2");
  test_extract_and_concatenate_at_level(
      JitOptimizationLevel::o0, "extract_concatenate_o0");
  test_extract_and_concatenate_at_level(
      JitOptimizationLevel::o2, "extract_concatenate_o2");
  test_insert_and_partial_writes_at_level(
      JitOptimizationLevel::o0, "insert_partial_writes_o0");
  test_insert_and_partial_writes_at_level(
      JitOptimizationLevel::o2, "insert_partial_writes_o2");
  test_dynamic_packed_indices_at_level(
      JitOptimizationLevel::o0, "dynamic_packed_indices_o0");
  test_dynamic_packed_indices_at_level(
      JitOptimizationLevel::o2, "dynamic_packed_indices_o2");
  test_initialized_bval_slot(JitOptimizationLevel::o0, "initialized_bval_o0");
  test_initialized_bval_slot(JitOptimizationLevel::o2, "initialized_bval_o2");
  test_direct_signal_read_at_level(
      JitOptimizationLevel::o0, "direct_signal_read_o0");
  test_direct_signal_read_at_level(
      JitOptimizationLevel::o2, "direct_signal_read_o2");
  test_required_direct_signal_reads_at_level(
      JitOptimizationLevel::o0, "required_direct_signal_read_o0");
  test_required_direct_signal_reads_at_level(
      JitOptimizationLevel::o2, "required_direct_signal_read_o2");
  test_required_direct_read_cache_identity();
  test_required_direct_read_lease_at_level(
      JitOptimizationLevel::o0, "required_direct_read_lease_o0");
  test_required_direct_read_lease_at_level(
      JitOptimizationLevel::o2, "required_direct_read_lease_o2");
  test_direct_update_accumulator_at_level(
      JitOptimizationLevel::o0, "direct_update_accumulator_o0");
  test_direct_update_accumulator_at_level(
      JitOptimizationLevel::o2, "direct_update_accumulator_o2");
  test_static_trigger_regions_at_level(
      JitOptimizationLevel::o0, "static_trigger_regions_o0");
  test_static_trigger_regions_at_level(
      JitOptimizationLevel::o2, "static_trigger_regions_o2");
  test_debug_point_instrumentation();
  test_control_flow_at_level(JitOptimizationLevel::o0, "control_flow_o0");
  test_control_flow_at_level(JitOptimizationLevel::o2, "control_flow_o2");
  test_native_callable_regions();
  test_constant_plane_forwarding();
  test_bound_literal_binding();
  test_checked_integer_at_level(
      JitOptimizationLevel::o0, "checked_integer_o0");
  test_checked_integer_at_level(
      JitOptimizationLevel::o2, "checked_integer_o2");
  test_scheduled_callbacks_at_level(
      JitOptimizationLevel::o0, "scheduled_o0");
  test_scheduled_callbacks_at_level(
      JitOptimizationLevel::o2, "scheduled_o2");
  test_direct_word_write_order_at_level(
      JitOptimizationLevel::o0, "direct_word_write_order_o0");
  test_direct_word_write_order_at_level(
      JitOptimizationLevel::o2, "direct_word_write_order_o2");
  test_inertial_callbacks_at_level(
      JitOptimizationLevel::o0, "inertial_o0");
  test_inertial_callbacks_at_level(
      JitOptimizationLevel::o2, "inertial_o2");
  test_projected_callbacks_at_level(
      JitOptimizationLevel::o0, "projected_o0");
  test_projected_callbacks_at_level(
      JitOptimizationLevel::o2, "projected_o2");
  test_scheduling_differential_at_level(
      JitOptimizationLevel::o0, "scheduled_diff_o0");
  test_scheduling_differential_at_level(
      JitOptimizationLevel::o2, "scheduled_diff_o2");
  test_resumable_at_level(JitOptimizationLevel::o0, "resume_o0");
  test_resumable_at_level(JitOptimizationLevel::o2, "resume_o2");
  test_process_cohort_resume_at_level(
      JitOptimizationLevel::o0, "cohort_resume_o0");
  test_process_cohort_resume_at_level(
      JitOptimizationLevel::o2, "cohort_resume_o2");
  test_ordered_cohort_cache_budget();
  test_fused_masked_all_active_process_at_level(JitOptimizationLevel::o0);
  test_fused_masked_all_active_process_at_level(JitOptimizationLevel::o2);
  test_fused_masked_process_at_level(JitOptimizationLevel::o0);
  test_fused_masked_process_at_level(JitOptimizationLevel::o2);
  test_class_service_boundaries_at_level(
      JitOptimizationLevel::o0, "class_boundary_o0");
  test_class_service_boundaries_at_level(
      JitOptimizationLevel::o2, "class_boundary_o2");
  test_wide_boundary_registers_at_level(
      JitOptimizationLevel::o0, "wide_boundary_o0");
  test_wide_boundary_registers_at_level(
      JitOptimizationLevel::o2, "wide_boundary_o2");
  test_native_service_callbacks_at_level(
      JitOptimizationLevel::o0, "native_services_o0");
  test_native_service_callbacks_at_level(
      JitOptimizationLevel::o2, "native_services_o2");
  test_signal_waits_at_level(
      JitOptimizationLevel::o0, "signal_wait_o0");
  test_signal_waits_at_level(
      JitOptimizationLevel::o2, "signal_wait_o2");
  test_display_at_level(
      JitOptimizationLevel::o0, "display_o0");
  test_display_at_level(
      JitOptimizationLevel::o2, "display_o2");
  test_strings_at_level(
      JitOptimizationLevel::o0, "strings_o0");
  test_strings_at_level(
      JitOptimizationLevel::o2, "strings_o2");
  test_logic9_at_level(
      JitOptimizationLevel::o0, "logic9_o0");
  test_logic9_at_level(
      JitOptimizationLevel::o2, "logic9_o2");
  test_wide_bitwise_at_level(
      JitOptimizationLevel::o0, "wide_bitwise_o0");
  test_wide_bitwise_at_level(
      JitOptimizationLevel::o2, "wide_bitwise_o2");
  test_vital_timing_at_level(
      JitOptimizationLevel::o0, "vital_timing_o0");
  test_vital_timing_at_level(
      JitOptimizationLevel::o2, "vital_timing_o2");
  test_vital_delay_at_level(
      JitOptimizationLevel::o0, "vital_delay_o0");
  test_vital_delay_at_level(
      JitOptimizationLevel::o2, "vital_delay_o2");
  test_persistent_object_cache();
  test_process_control_cache_identity();
  test_jit_nounwind_contract();
  test_rejections();
  std::cout << "LLVM JIT tests passed with LLVM " << LlvmJit::llvm_version()
            << '\n';
}
