// SPDX-License-Identifier: Apache-2.0
#include "fsim/compiler/llvm_jit_region_kernel.hpp"
#include "llvm_jit_cache_key_internal.hpp"
#include "llvm_jit_llvm_args.hpp"
#include "llvm_jit_native_body_registry_internal.hpp"
#include "llvm_jit_region_activation_internal.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fsim::compiler {
namespace {

using runtime::PackedLogic4;
using runtime::simir::Branch;
using runtime::simir::Binary;
using runtime::simir::BinaryOperator;
using runtime::simir::Concatenate;
using runtime::simir::ConditionalSelect;
using runtime::simir::CopyRegister;
using runtime::simir::DebugPoint;
using runtime::simir::Extract;
using runtime::simir::Halt;
using runtime::simir::LoadConstant;
using runtime::simir::Process;
using runtime::simir::ReadSignal;
using runtime::simir::RegisterId;
using runtime::simir::Reduction;
using runtime::simir::ReductionOperator;
using runtime::simir::RegionConeActivationKernel;
using runtime::simir::RegionConeConstantInput;
using runtime::simir::RegionConeKernelMember;
using runtime::simir::RegionPreparedOutputBatchV1;
using runtime::simir::RegionPreparedOutputSlotV1;
using runtime::simir::SignalId;
using runtime::simir::SignalReadKind;
using runtime::simir::UnaryNot;
using runtime::simir::ValueKind;
using runtime::simir::WriteUpdate;

constexpr std::string_view region_module_symbol
    = "fsim_region_activation_kernel_v1";
constexpr std::string_view region_known_logic4_module_symbol
    = "fsim_region_activation_kernel_known_logic4_v1";
constexpr std::string_view region_constant_inputs_module_symbol
    = "fsim_region_activation_kernel_constant_inputs_v1";

[[nodiscard]] bool region_creation_diagnostics_enabled() noexcept
{
  return std::getenv("FSIM_PROFILE_SV_WAVES") != nullptr;
}

void report_region_creation_event(
    const RegionConeActivationKernel& kernel,
    const RegionKernelSpecialization specialization,
    const std::string_view event, const std::string_view stage,
    const std::size_t line = 0U,
    const std::string_view detail = { }) noexcept
{
  if (!region_creation_diagnostics_enabled()) {
    return;
  }
  std::fprintf(stderr,
      "[fsim region-native-create] layer=compiler event=%.*s stage=%.*s "
      "members=%zu inputs=%zu outputs=%zu specialization=%u line=%zu",
      static_cast<int>(event.size()), event.data(),
      static_cast<int>(stage.size()), stage.data(), kernel.members.size(),
      kernel.inputs.size(), kernel.outputs.size(),
      static_cast<unsigned>(specialization), line);
  if (!detail.empty()) {
    std::fprintf(stderr, " detail=%.*s", static_cast<int>(detail.size()),
        detail.data());
  }
  std::fputc('\n', stderr);
}

struct InputBinding {
  runtime::simir::RegisterId reg { };
  SignalId synthetic_signal { };
  std::uint32_t width { };
  ValueKind value_kind { ValueKind::logic4 };
  bool readiness { };
  std::size_t member_index { };
  SignalId source_signal { };
  bool internal { };
  std::optional<PackedLogic4> specialization_value;
};

struct SpillBinding {
  std::size_t member_index { };
  runtime::simir::RegisterId activation_register { };
  SignalId synthetic_signal { };
  std::uint32_t width { };
  ValueKind value_kind { ValueKind::logic4 };
  std::size_t prefix_output_index { };
  bool prefix_change_flag { };
};

struct PrefixInputBinding {
  SignalId source_signal { };
  std::optional<runtime::simir::RegisterId> committed_input_register;
  runtime::simir::RegisterId activation_register { };
  SignalId synthetic_signal { };
  std::uint32_t width { };
};

struct PrefixFlagBinding {
  std::size_t output_index { };
  std::size_t member_index { };
  runtime::simir::RegisterId activation_register { };
  runtime::simir::RegisterId current_register { };
  runtime::simir::RegisterId readiness_register { };
  SignalId synthetic_signal { };
};

struct CallbackState {
  bool used { };
};

void mark_unexpected_callback(void* const opaque) noexcept
{
  static_cast<CallbackState*>(opaque)->used = true;
}

[[nodiscard]] std::uint32_t word_count_for_width(
    std::uint32_t width) noexcept;
[[nodiscard]] std::uint64_t word_mask_for_width(
    std::uint32_t width, std::uint32_t word_index) noexcept;

extern "C" std::uint64_t unexpected_read_signal(
    void* const opaque, const std::uint32_t,
    std::uint64_t* const bval) noexcept
{
  static_cast<CallbackState*>(opaque)->used = true;
  if (bval != nullptr) {
    *bval = UINT64_MAX;
  }
  return 0U;
}

extern "C" void unexpected_write_signal(
    void* const opaque, const std::uint32_t,
    const std::uint64_t, const std::uint64_t) noexcept
{
  static_cast<CallbackState*>(opaque)->used = true;
}

extern "C" void unexpected_assert_failed(
    void* const opaque, const std::uint32_t, const std::uint32_t,
    const char*, const std::uint64_t) noexcept
{
  static_cast<CallbackState*>(opaque)->used = true;
}

extern "C" void unexpected_write_update(
    void* const opaque, const std::uint32_t, const std::uint64_t,
    const std::uint64_t, const std::uint32_t) noexcept
{
  static_cast<CallbackState*>(opaque)->used = true;
}

extern "C" std::uint32_t unexpected_read_signal_packed(
    void* const opaque, const std::uint32_t, const std::uint32_t width,
    std::uint64_t* const aval_words, std::uint64_t* const bval_words,
    std::uint64_t* const plane2_words,
    std::uint64_t* const plane3_words) noexcept
{
  mark_unexpected_callback(opaque);
  if (width == 0U) {
    return 0U;
  }
  const auto count = word_count_for_width(width);
  if (aval_words != nullptr && bval_words != nullptr) {
    std::fill_n(aval_words, count, UINT64_MAX);
    std::fill_n(bval_words, count, UINT64_MAX);
    const auto tail_mask = word_mask_for_width(width, count - 1U);
    aval_words[count - 1U] &= tail_mask;
    bval_words[count - 1U] &= tail_mask;
  }
  if (plane2_words != nullptr && plane3_words != nullptr) {
    std::fill_n(plane2_words, count, 0U);
    std::fill_n(plane3_words, count, 0U);
  }
  return 0U;
}

extern "C" std::uint32_t unexpected_write_signal_packed(
    void* const opaque, const std::uint32_t, const std::uint32_t,
    const std::uint32_t, const std::uint32_t, const std::uint64_t,
    const std::uint64_t*, const std::uint64_t*, const std::uint64_t*,
    const std::uint64_t*, const std::uint32_t) noexcept
{
  static_cast<CallbackState*>(opaque)->used = true;
  return 0U;
}

void append_number(std::string& destination, const std::uint64_t value)
{
  std::array<char, 32U> buffer { };
  const auto converted = std::to_chars(
      buffer.data(), buffer.data() + buffer.size(), value);
  if (converted.ec != std::errc { }) {
    throw LlvmJitError("failed to serialize region kernel identity");
  }
  destination.append(buffer.data(), converted.ptr);
  destination.push_back(';');
}

void append_text(std::string& destination, const std::string_view value)
{
  append_number(destination, static_cast<std::uint64_t>(value.size()));
  destination.append(value);
  destination.push_back(';');
}

[[nodiscard]] const fsim_jit_services_v2& region_kernel_services() noexcept
{
  static const fsim_jit_services_v2 services = [] {
    fsim_jit_services_v2 value { };
    value.abi_version = FSIM_JIT_SERVICES_ABI_VERSION_V2;
    value.struct_size = sizeof(value);
    value.read_signal = unexpected_read_signal;
    value.write_signal = unexpected_write_signal;
    value.assert_failed = unexpected_assert_failed;
    value.write_update = unexpected_write_update;
    value.read_signal_packed = unexpected_read_signal_packed;
    value.write_signal_packed = unexpected_write_signal_packed;
    value.read_signal_logic9 = +[](void* const opaque, const std::uint32_t,
                                    fsim_jit_logic9_word_v2* const result) noexcept {
      mark_unexpected_callback(opaque);
      if (result != nullptr) {
        *result = { { UINT64_MAX, 0U, 0U, 0U } };
      }
    };
    value.write_signal_logic9 = +[](void* const opaque, const std::uint32_t,
                                     const fsim_jit_logic9_word_v2*) noexcept {
      mark_unexpected_callback(opaque);
    };
    value.write_update_logic9 = +[](void* const opaque, const std::uint32_t,
                                    const fsim_jit_logic9_word_v2*,
                                    const std::uint32_t) noexcept {
      mark_unexpected_callback(opaque);
    };
    value.write_after_logic9 = +[](void* const opaque, const std::uint32_t,
                                    const fsim_jit_logic9_word_v2*,
                                    const std::uint64_t,
                                    const std::uint32_t) noexcept {
      mark_unexpected_callback(opaque);
    };
    value.write_signal_slice_logic9 = +[](void* const opaque,
        const std::uint32_t, const std::uint32_t, const std::uint32_t,
        const fsim_jit_logic9_word_v2*) noexcept {
      mark_unexpected_callback(opaque);
    };
    value.write_update_slice_logic9 = +[](void* const opaque,
        const std::uint32_t, const std::uint32_t, const std::uint32_t,
        const fsim_jit_logic9_word_v2*, const std::uint32_t) noexcept {
      mark_unexpected_callback(opaque);
    };
    value.write_after_slice_logic9 = +[](void* const opaque,
        const std::uint32_t, const std::uint32_t, const std::uint32_t,
        const fsim_jit_logic9_word_v2*, const std::uint64_t,
        const std::uint32_t) noexcept {
      mark_unexpected_callback(opaque);
    };
    value.signal_last_value_logic9 = +[](void* const opaque,
        const std::uint32_t, fsim_jit_logic9_word_v2* const result) noexcept {
      mark_unexpected_callback(opaque);
      if (result != nullptr) {
        *result = { { UINT64_MAX, 0U, 0U, 0U } };
      }
    };
    value.write_inertial_logic9 = +[](void* const opaque,
        const std::uint32_t, const fsim_jit_logic9_word_v2*,
        const std::uint64_t, const std::uint64_t, const std::uint64_t,
        const std::uint32_t) noexcept {
      mark_unexpected_callback(opaque);
    };
    value.write_inertial_slice_logic9 = +[](void* const opaque,
        const std::uint32_t, const std::uint32_t, const std::uint32_t,
        const fsim_jit_logic9_word_v2*, const std::uint64_t,
        const std::uint64_t, const std::uint64_t,
        const std::uint32_t) noexcept {
      mark_unexpected_callback(opaque);
    };
    value.write_projected_logic9 = +[](void* const opaque,
        const std::uint32_t, const fsim_jit_logic9_word_v2*,
        const std::uint64_t, const std::uint64_t,
        const std::uint32_t) noexcept {
      mark_unexpected_callback(opaque);
    };
    value.write_projected_slice_logic9 = +[](void* const opaque,
        const std::uint32_t, const std::uint32_t, const std::uint32_t,
        const fsim_jit_logic9_word_v2*, const std::uint64_t,
        const std::uint64_t, const std::uint32_t) noexcept {
      mark_unexpected_callback(opaque);
    };
    value.write_projected_waveform_logic9 = +[](void* const opaque,
        const std::uint32_t, const std::uint32_t,
        const fsim_jit_logic9_projected_element_v2*, const std::uint32_t,
        const std::uint64_t, const std::uint32_t) noexcept {
      mark_unexpected_callback(opaque);
    };
    value.write_projected_waveform_slice_logic9 = +[](void* const opaque,
        const std::uint32_t, const std::uint32_t, const std::uint32_t,
        const fsim_jit_logic9_projected_element_v2*, const std::uint32_t,
        const std::uint64_t, const std::uint32_t) noexcept {
      mark_unexpected_callback(opaque);
    };
    value.write_formatted_logic9 = +[](void* const opaque,
        const std::uint32_t, const std::uint32_t, const std::uint32_t,
        const fsim_jit_logic9_word_v2*) noexcept {
      mark_unexpected_callback(opaque);
    };
    return value;
  }();
  return services;
}

[[nodiscard]] std::uint64_t width_mask(const std::uint32_t width) noexcept
{
  return width == 64U ? UINT64_MAX
                      : ((UINT64_C(1) << width) - UINT64_C(1));
}

[[nodiscard]] std::uint32_t word_count_for_width(
    const std::uint32_t width) noexcept
{
  return width / 64U + (width % 64U != 0U ? 1U : 0U);
}

[[nodiscard]] std::uint64_t word_mask_for_width(
    const std::uint32_t width,
    const std::uint32_t word_index) noexcept
{
  const auto word_start = static_cast<std::uint64_t>(word_index) * 64U;
  if (word_start >= width) {
    return 0U;
  }
  const auto remaining = static_cast<std::uint64_t>(width) - word_start;
  return remaining >= 64U
      ? UINT64_MAX
      : (UINT64_C(1) << remaining) - 1U;
}

[[nodiscard]] bool build_flat_word_offsets(
    const std::span<const std::uint32_t> widths,
    std::vector<std::uint32_t>& offsets,
    std::uint32_t& total_words)
{
  offsets.clear();
  if (widths.size() > offsets.max_size()) {
    return false;
  }
  offsets.reserve(widths.size());
  std::uint64_t next_word { };
  constexpr auto max_offset = std::numeric_limits<std::uint32_t>::max();
  for (const auto width : widths) {
    if (width == 0U || next_word > max_offset) {
      return false;
    }
    const auto words = word_count_for_width(width);
    if (words == 0U || words > max_offset - next_word) {
      return false;
    }
    offsets.push_back(static_cast<std::uint32_t>(next_word));
    next_word += words;
  }
  total_words = static_cast<std::uint32_t>(next_word);
  return true;
}

[[nodiscard]] std::string region_cache_identity(
    const RegionConeActivationKernel& kernel,
    const std::string_view immutable_design_identity,
    const std::span<const InputBinding> inputs,
    const std::span<const SpillBinding> spills,
    const bool frame_exports,
    const std::string_view operation_identity,
    const std::string_view identity_domain
        = "fsim-region-cone-mapping-v7")
{
  const auto register_value_kinds
      = runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
          kernel.program.register_value_kinds);
  std::string identity;
  identity.reserve(192U + immutable_design_identity.size()
      + kernel.inputs.size() * 48U + kernel.members.size() * 128U
      + kernel.outputs.size() * 64U + spills.size() * 40U);
  append_text(identity, identity_domain);
  append_number(identity, frame_exports ? 1U : 0U);
  append_text(identity, immutable_design_identity);
  append_text(identity, operation_identity);
  append_number(identity, kernel.program.id);
  append_text(identity, kernel.program.name);
  append_number(identity,
      static_cast<std::uint8_t>(kernel.program.scheduling_domain));
  append_number(identity, kernel.program.register_count);
  append_number(identity, register_value_kinds.size());
  for (const auto kind : register_value_kinds) {
    append_number(identity, static_cast<std::uint8_t>(kind));
  }

  append_number(identity, kernel.inputs.size());
  for (const auto& input : kernel.inputs) {
    append_number(identity, input.signal);
    append_number(identity, input.value_register);
    append_number(identity, input.width);
    append_number(identity, static_cast<std::uint8_t>(input.value_kind));
    append_number(identity, input.internal ? 1U : 0U);
  }

  append_number(identity, kernel.members.size());
  append_number(identity, kernel.member_execution_order.size());
  for (const auto index : kernel.member_execution_order) {
    append_number(identity, index);
  }
  for (const auto& member : kernel.members) {
    append_number(identity, member.process);
    append_number(identity, member.readiness_register);
    append_number(identity, member.branch_instruction);
    append_number(identity, member.begin);
    append_number(identity, member.end);
    append_number(identity, member.initialize ? 1U : 0U);
    append_number(identity, member.sensitivities.size());
    for (const auto& sensitivity : member.sensitivities) {
      append_number(identity, sensitivity.signal);
      append_number(identity, static_cast<std::uint8_t>(sensitivity.edge));
      append_number(identity, sensitivity.offset);
      append_number(identity, sensitivity.width);
    }
    append_number(identity, member.final_debug_state.has_value() ? 1U : 0U);
    if (member.final_debug_state) {
      append_text(identity, member.final_debug_state->source.path.str());
      append_number(identity, member.final_debug_state->source.line);
      append_number(identity, member.final_debug_state->source.column);
      append_text(identity, member.final_debug_state->scope);
    }
    append_number(identity, member.register_bindings.size());
    for (const auto& binding : member.register_bindings) {
      append_number(identity, binding.source_register);
      append_number(identity, binding.activation_register);
      append_number(identity, binding.defined ? 1U : 0U);
      append_number(identity, binding.width);
      append_number(identity, static_cast<std::uint8_t>(binding.value_kind));
    }
  }

  append_number(identity, kernel.outputs.size());
  for (const auto& output : kernel.outputs) {
    append_number(identity, output.owner);
    append_number(identity, output.signal);
    append_number(identity, output.offset);
    append_number(identity, output.width);
    append_number(identity, output.signal_width != 0U
            ? output.signal_width
            : output.offset == 0U ? output.width : 0U);
    append_number(identity, static_cast<std::uint8_t>(output.value_kind));
    append_number(identity, static_cast<std::uint8_t>(output.domain));
    append_number(identity, output.value_register);
    append_number(identity, output.source_instruction);
    append_number(identity, output.compute_instruction);
    append_number(identity, output.kernel_instruction);
    append_number(identity, static_cast<std::uint8_t>(output.update_kind));
    append_number(identity,
        static_cast<std::uint8_t>(output.publication_kind));
    append_number(identity, static_cast<std::uint8_t>(output.projected_mode));
    append_number(identity, output.projected_delay);
    append_number(identity, output.projected_rejection);
  }

  append_number(identity, kernel.internal_signals.size());
  for (const auto signal : kernel.internal_signals) {
    append_number(identity, signal);
  }

  append_number(identity, kernel.constant_inputs.size());
  for (const auto& constant : kernel.constant_inputs) {
    append_number(identity, constant.owner);
    append_number(identity, constant.signal);
    append_number(identity, constant.offset);
    append_number(identity, constant.width);
    append_number(identity, static_cast<std::uint8_t>(constant.value_kind));
    append_number(identity, static_cast<std::uint8_t>(constant.domain));
    append_number(identity, static_cast<std::uint8_t>(constant.update_kind));
    append_number(identity, static_cast<std::uint8_t>(constant.projected_mode));
    append_number(identity, constant.projected_delay);
    append_number(identity, constant.projected_rejection);
    append_number(identity, constant.value.width());
    append_number(identity, constant.value.is_logic9() ? 1U : 0U);
    append_number(identity, constant.value.aval_words().size());
    for (const auto word : constant.value.aval_words()) {
      append_number(identity, word);
    }
    append_number(identity, constant.value.bval_words().size());
    for (const auto word : constant.value.bval_words()) {
      append_number(identity, word);
    }
    if (constant.value.is_logic9()) {
      for (std::size_t plane = 2U; plane < 4U; ++plane) {
        const auto words = constant.value.logic9_plane_words(plane);
        append_number(identity, words.size());
        for (const auto word : words) {
          append_number(identity, word);
        }
      }
    }
  }

  append_number(identity, inputs.size());
  for (const auto& input : inputs) {
    append_number(identity, input.reg);
    append_number(identity, input.synthetic_signal);
    append_number(identity, input.width);
    append_number(identity, static_cast<std::uint8_t>(input.value_kind));
    append_number(identity, input.readiness ? 1U : 0U);
    append_number(identity, input.member_index);
    append_number(identity, input.specialization_value.has_value() ? 1U : 0U);
    if (input.specialization_value) {
      append_number(identity, input.specialization_value->width());
      for (const auto word : input.specialization_value->aval_words()) {
        append_number(identity, word);
      }
      for (const auto word : input.specialization_value->bval_words()) {
        append_number(identity, word);
      }
      if (input.specialization_value->is_logic9()) {
        for (std::size_t plane = 2U; plane < 4U; ++plane) {
          const auto words
              = input.specialization_value->logic9_plane_words(plane);
          append_number(identity, words.size());
          for (const auto word : words) {
            append_number(identity, word);
          }
        }
      }
    }
  }
  append_number(identity, spills.size());
  for (const auto& spill : spills) {
    append_number(identity, spill.member_index);
    append_number(identity, spill.activation_register);
    append_number(identity, spill.synthetic_signal);
    append_number(identity, spill.width);
    append_number(identity, static_cast<std::uint8_t>(spill.value_kind));
  }
  return identity;
}

[[nodiscard]] bool canonicalize_region_kernel_physical_ids(
    RegionConeActivationKernel& kernel)
{
  if (kernel.members.size()
      > static_cast<std::size_t>(
          std::numeric_limits<runtime::simir::ProcessId>::max())) {
    return false;
  }
  std::vector<SignalId> source_signals;
  source_signals.reserve(kernel.inputs.size());
  const auto canonical_signal = [&](const SignalId source)
      -> std::optional<SignalId> {
    const auto found = std::ranges::find(source_signals, source);
    if (found != source_signals.end()) {
      return static_cast<SignalId>(found - source_signals.begin());
    }
    if (source_signals.size()
        >= static_cast<std::size_t>(std::numeric_limits<SignalId>::max())) {
      return std::nullopt;
    }
    const auto canonical = static_cast<SignalId>(source_signals.size());
    source_signals.push_back(source);
    return canonical;
  };

  for (auto& input : kernel.inputs) {
    const auto canonical = canonical_signal(input.signal);
    if (!canonical) {
      return false;
    }
    input.signal = *canonical;
  }
  for (auto& member : kernel.members) {
    for (auto& sensitivity : member.sensitivities) {
      const auto canonical = canonical_signal(sensitivity.signal);
      if (!canonical) {
        return false;
      }
      sensitivity.signal = *canonical;
    }
    // Final source/scope state is kept by the original wrapper for runtime
    // diagnostics; the synthetic native body drops these DebugPoint markers.
    member.final_debug_state.reset();
  }
  for (auto& output : kernel.outputs) {
    const auto owner = std::ranges::find(kernel.members, output.owner,
        &RegionConeKernelMember::process);
    if (owner == kernel.members.end()) {
      return false;
    }
    output.owner = static_cast<runtime::simir::ProcessId>(
        owner - kernel.members.begin());
    const auto canonical = canonical_signal(output.signal);
    if (!canonical) {
      return false;
    }
    output.signal = *canonical;
  }
  for (auto& signal : kernel.internal_signals) {
    const auto canonical = canonical_signal(signal);
    if (!canonical) {
      return false;
    }
    signal = *canonical;
  }
  for (auto& constant : kernel.constant_inputs) {
    const auto owner = std::ranges::find(kernel.members, constant.owner,
        &RegionConeKernelMember::process);
    constant.owner = owner == kernel.members.end()
        ? std::numeric_limits<runtime::simir::ProcessId>::max()
        : static_cast<runtime::simir::ProcessId>(
              owner - kernel.members.begin());
    const auto canonical = canonical_signal(constant.signal);
    if (!canonical) {
      return false;
    }
    constant.signal = *canonical;
  }

  for (std::size_t member_index = 0U;
       member_index < kernel.members.size(); ++member_index) {
    kernel.members[member_index].process
        = static_cast<runtime::simir::ProcessId>(member_index);
  }

  kernel.program.id = 0U;
  kernel.program.name = "simir_region_activation_native";
  return true;
}

[[nodiscard]] bool supported_value(
    const ValueKind kind, const std::uint32_t width) noexcept
{
  return (kind == ValueKind::logic4 || kind == ValueKind::logic9)
      && width != 0U;
}

[[nodiscard]] bool member_execution_order_is_valid(
    const RegionConeActivationKernel& kernel)
{
  if (kernel.member_execution_order.empty()) {
    return true;
  }
  if (kernel.member_execution_order.size() != kernel.members.size()) {
    return false;
  }
  std::vector<std::uint8_t> seen(kernel.members.size(), 0U);
  for (const auto index : kernel.member_execution_order) {
    if (index >= seen.size() || seen[index] != 0U) {
      return false;
    }
    seen[index] = 1U;
  }
  return true;
}

[[nodiscard]] std::vector<std::size_t> member_execution_order(
    const RegionConeActivationKernel& kernel)
{
  if (!kernel.member_execution_order.empty()) {
    return kernel.member_execution_order;
  }
  std::vector<std::size_t> result;
  result.reserve(kernel.members.size());
  for (std::size_t index = 0U; index < kernel.members.size(); ++index) {
    result.push_back(index);
  }
  return result;
}

[[nodiscard]] const RegionConeConstantInput* find_constant_input(
    const RegionConeActivationKernel& kernel, const SignalId signal) noexcept
{
  const auto found = std::ranges::find(kernel.constant_inputs, signal,
      &RegionConeConstantInput::signal);
  return found == kernel.constant_inputs.end() ? nullptr : &*found;
}

[[nodiscard]] bool constant_inputs_are_valid(
    const RegionConeActivationKernel& kernel)
{
  for (std::size_t index = 0U; index < kernel.constant_inputs.size(); ++index) {
    const auto& constant = kernel.constant_inputs[index];
    const bool active_contract
        = constant.domain
                == runtime::simir::SignalUpdateDomain::systemverilog_active
            && constant.update_kind
                == runtime::simir::RegionUpdateKind::systemverilog_active
            && constant.projected_mode
                == runtime::simir::ProjectedDelayMode::inertial
            && constant.projected_delay == 0U
            && constant.projected_rejection == 0U;
    const bool projected_contract
        = constant.domain
                == runtime::simir::SignalUpdateDomain::generic
            && constant.update_kind
                == runtime::simir::RegionUpdateKind::vhdl_projected
            && (constant.projected_mode
                    == runtime::simir::ProjectedDelayMode::transport
                || constant.projected_mode
                    == runtime::simir::ProjectedDelayMode::inertial)
            && (constant.projected_mode
                    != runtime::simir::ProjectedDelayMode::inertial
                || constant.projected_rejection <= constant.projected_delay);
    if (constant.width == 0U || constant.value.width() != constant.width
        || (constant.value_kind != ValueKind::logic4
            && constant.value_kind != ValueKind::logic9)
        || constant.value.is_logic9()
            != (constant.value_kind == ValueKind::logic9)
        || constant.offset != 0U
        || (!active_contract && !projected_contract)
        || std::ranges::find(kernel.members, constant.owner,
            &RegionConeKernelMember::process) != kernel.members.end()
        || (index != 0U
            && kernel.constant_inputs[index - 1U].signal >= constant.signal)) {
      return false;
    }
    const auto input = std::ranges::find(kernel.inputs, constant.signal,
        &runtime::simir::RegionConeKernelInput::signal);
    if (input == kernel.inputs.end() || input->internal
        || input->width != constant.width
        || input->value_kind != constant.value_kind
        || std::ranges::count(kernel.inputs, constant.signal,
            &runtime::simir::RegionConeKernelInput::signal) != 1U) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool supported_compute_operation(
    const runtime::simir::Operation& operation) noexcept
{
  bool supported { };
  runtime::simir::visit_operation([&](const auto& value) {
    using Type = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<Type, LoadConstant>) {
      supported = value.value.width() != 0U
          && value.value.width()
              <= std::numeric_limits<std::uint32_t>::max();
    } else if constexpr (std::is_same_v<Type, Binary>) {
      supported = value.operation == BinaryOperator::bit_and
          || value.operation == BinaryOperator::bit_or
          || value.operation == BinaryOperator::bit_xor
          || value.operation == BinaryOperator::add_unsigned;
    } else if constexpr (std::is_same_v<Type, Reduction>) {
      supported = value.operation == ReductionOperator::bit_and
          || value.operation == ReductionOperator::bit_or
          || value.operation == ReductionOperator::bit_xor;
    } else if constexpr (std::is_same_v<Type, ConditionalSelect>
        || std::is_same_v<Type, CopyRegister>
        || std::is_same_v<Type, UnaryNot>
        || std::is_same_v<Type, Extract>
        || std::is_same_v<Type, Concatenate>
        || std::is_same_v<Type, DebugPoint>) {
      supported = true;
    }
  }, operation);
  return supported;
}

[[nodiscard]] std::optional<std::string>
expanded_member_operation_identity(
    const RegionConeActivationKernel& kernel)
{
  if (!member_execution_order_is_valid(kernel)) {
    return std::nullopt;
  }

  Process expanded_members;
  expanded_members.id = 0U;
  expanded_members.register_count = kernel.program.register_count;
  expanded_members.operations = runtime::simir::OperationList { };
  CacheKeyBuilder operation_key;
  operation_key.add("prepared-region-member-operations", "expanded-v2");
  std::size_t operation_count { };
  std::size_t previous_end { };
  for (const auto member_index : member_execution_order(kernel)) {
    if (member_index >= kernel.members.size()) {
      return std::nullopt;
    }
    const auto& member = kernel.members[member_index];
    if (member.branch_instruction != previous_end
        || member.begin != member.branch_instruction + 1U
        || member.begin >= member.end
        || member.end > kernel.program.operations.size()) {
      return std::nullopt;
    }
    auto branch_operation = kernel.program.operations.expanded(
        member.branch_instruction);
    const auto* const branch
        = runtime::simir::operation_get_if<Branch>(&branch_operation);
    if (branch == nullptr || branch->condition != member.readiness_register
        || branch->when_true != member.begin
        || branch->when_false != member.end
        || branch->unknown_policy != runtime::simir::UnknownBranchPolicy::error) {
      return std::nullopt;
    }
    operation_key.add("member-branch-condition",
        std::to_string(branch->condition));
    operation_key.add("member-branch-true",
        std::to_string(branch->when_true));
    operation_key.add("member-branch-false",
        std::to_string(branch->when_false));
    operation_key.add("member-branch-unknown-policy",
        std::to_string(static_cast<std::uint8_t>(branch->unknown_policy)));
    for (std::size_t index = member.begin; index < member.end; ++index) {
      auto operation = kernel.program.operations.expanded(index);
      if (operation_holds<DebugPoint>(operation)) {
        // The generated template omits source markers; exact debug state is
        // checked and retained by each physical wrapper.
        continue;
      }
      if (!supported_compute_operation(operation)
          || operation_count == expanded_members.operations.max_size()) {
        return std::nullopt;
      }
      expanded_members.operations.push_back(std::move(operation));
      ++operation_count;
    }
    previous_end = member.end;
  }
  if (previous_end + 1U != kernel.program.operations.size()
      || !runtime::simir::operation_holds<Halt>(
          kernel.program.operations.expanded(previous_end))) {
    return std::nullopt;
  }

  llvm_detail::add_operation_cache_keys(
      operation_key, expanded_members, std::span<const std::uint32_t> { });
  return operation_key.finish();
}

[[nodiscard]] std::optional<SignalId> canonical_signal_id(
    const RegionConeActivationKernel& physical,
    const RegionConeActivationKernel& canonical,
    const SignalId physical_signal) noexcept
{
  if (physical.inputs.size() != canonical.inputs.size()
      || physical.members.size() != canonical.members.size()
      || physical.outputs.size() != canonical.outputs.size()
      || physical.internal_signals.size() != canonical.internal_signals.size()
      || physical.constant_inputs.size() != canonical.constant_inputs.size()) {
    return std::nullopt;
  }
  for (std::size_t index = 0U; index < physical.inputs.size(); ++index) {
    if (physical.inputs[index].signal == physical_signal) {
      return canonical.inputs[index].signal;
    }
  }
  for (std::size_t member = 0U; member < physical.members.size(); ++member) {
    if (physical.members[member].sensitivities.size()
        != canonical.members[member].sensitivities.size()) {
      return std::nullopt;
    }
    for (std::size_t index = 0U;
         index < physical.members[member].sensitivities.size(); ++index) {
      if (physical.members[member].sensitivities[index].signal
          == physical_signal) {
        return canonical.members[member].sensitivities[index].signal;
      }
    }
  }
  for (std::size_t index = 0U; index < physical.outputs.size(); ++index) {
    if (physical.outputs[index].signal == physical_signal) {
      return canonical.outputs[index].signal;
    }
  }
  for (std::size_t index = 0U;
       index < physical.internal_signals.size(); ++index) {
    if (physical.internal_signals[index] == physical_signal) {
      return canonical.internal_signals[index];
    }
  }
  for (std::size_t index = 0U;
       index < physical.constant_inputs.size(); ++index) {
    if (physical.constant_inputs[index].signal == physical_signal) {
      return canonical.constant_inputs[index].signal;
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool kernel_extracts_fit_source_registers(
    const RegionConeActivationKernel& kernel,
    const std::span<const std::uint32_t> register_widths)
{
  for (const auto member_index : member_execution_order(kernel)) {
    if (member_index >= kernel.members.size()) {
      return false;
    }
    const auto& member = kernel.members[member_index];
    if (member.begin > member.end
        || member.end > kernel.program.operations.size()) {
      return false;
    }
    for (std::size_t index = member.begin; index < member.end; ++index) {
      const auto operation = kernel.program.operations.expanded(index);
      const auto* const extract
          = runtime::simir::operation_get_if<Extract>(&operation);
      if (extract == nullptr) {
        continue;
      }
      if (extract->source >= register_widths.size()
          || extract->destination >= register_widths.size()) {
        return false;
      }
      const auto source_width = register_widths[extract->source];
      if (source_width == 0U || extract->width == 0U
          || extract->offset >= source_width
          || extract->width > source_width - extract->offset) {
        return false;
      }
    }
  }
  return true;
}

[[nodiscard]] bool known_logic4_constant(const LoadConstant& operation)
{
  const auto& value = operation.value;
  return !value.is_logic9() && value.width() != 0U
      && value.width() <= 64U
      && std::ranges::all_of(value.bval_words(),
          [](const std::uint64_t word) { return word == 0U; });
}

[[nodiscard]] bool known_logic4_operation_preserves_values(
    const runtime::simir::Operation& operation,
    const std::span<const std::uint32_t> register_widths,
    const std::span<const ValueKind> register_value_kinds,
    const std::span<const std::uint8_t> known_before,
    std::span<std::uint8_t> known_after)
{
  bool supported { };
  const auto known_register = [&](const RegisterId reg) {
    return reg < known_before.size() && known_before[reg] != 0U
        && reg < register_widths.size() && register_widths[reg] != 0U
        && register_widths[reg] <= 64U
        && reg < register_value_kinds.size()
        && register_value_kinds[reg] == ValueKind::logic4;
  };
  const auto define_register = [&](const RegisterId reg) {
    if (reg >= known_after.size()
        || reg >= register_value_kinds.size()
        || register_value_kinds[reg] != ValueKind::logic4) {
      return false;
    }
    known_after[reg] = 1U;
    return true;
  };
  runtime::simir::visit_operation([&](const auto& value) {
    using Type = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<Type, LoadConstant>) {
      supported = known_logic4_constant(value)
          && define_register(value.destination);
    } else if constexpr (std::is_same_v<Type, Binary>) {
      const bool bitwise = value.operation == BinaryOperator::bit_and
          || value.operation == BinaryOperator::bit_or
          || value.operation == BinaryOperator::bit_xor;
      supported = bitwise && known_register(value.lhs)
          && known_register(value.rhs)
          && define_register(value.destination);
    } else if constexpr (std::is_same_v<Type, Reduction>) {
      const bool supported_operator
          = value.operation == ReductionOperator::bit_and
          || value.operation == ReductionOperator::bit_or
          || value.operation == ReductionOperator::bit_xor;
      supported = supported_operator && known_register(value.source)
          && define_register(value.destination);
    } else if constexpr (std::is_same_v<Type, Extract>) {
      if (known_register(value.source)) {
        const auto source_width = register_widths[value.source];
        supported = value.width != 0U && value.offset < source_width
            && value.width <= source_width - value.offset
            && define_register(value.destination);
      }
    } else if constexpr (std::is_same_v<Type, CopyRegister>) {
      supported = known_register(value.source)
          && define_register(value.destination);
    } else if constexpr (std::is_same_v<Type, UnaryNot>) {
      supported = known_register(value.source)
          && define_register(value.destination);
    } else if constexpr (std::is_same_v<Type, ConditionalSelect>) {
      const bool shape_matches
          = value.condition < register_widths.size()
          && register_widths[value.condition] == 1U
          && value.when_true < register_widths.size()
          && value.when_false < register_widths.size()
          && value.destination < register_widths.size()
          && register_widths[value.when_true] != 0U
          && register_widths[value.when_true]
              == register_widths[value.when_false]
          && register_widths[value.destination]
              == register_widths[value.when_true];
      supported = shape_matches
          && known_register(value.condition)
          && known_register(value.when_true)
          && known_register(value.when_false)
          && define_register(value.destination);
    } else if constexpr (std::is_same_v<Type, Concatenate>) {
      supported = !value.operands.empty()
          && std::ranges::all_of(value.operands, known_register)
          && define_register(value.destination);
    } else if constexpr (std::is_same_v<Type, DebugPoint>) {
      // This value-only entry is invoked by the region path only after its
      // execution-point observer guard. The exact final source/scope snapshot
      // is checked against each member below; this body never calls a hook.
      supported = true;
    }
  }, operation);
  return supported;
}

[[nodiscard]] bool supports_known_logic4_variant(
    const RegionConeActivationKernel& kernel,
    const std::span<const InputBinding> inputs,
    const std::span<const SpillBinding> spills,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const ValueKind> signal_value_kinds,
    const std::span<const std::uint32_t> register_widths)
{
  const auto register_value_kinds
      = runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
          kernel.program.register_value_kinds);
  if (signal_widths.size() != signal_value_kinds.size()
      || std::ranges::any_of(signal_widths,
          [](const std::uint32_t width) {
            return width == 0U || width > 64U;
          })
      || std::ranges::any_of(signal_value_kinds,
          [](const ValueKind kind) { return kind != ValueKind::logic4; })
      || register_widths.size() != kernel.program.register_count
      || register_value_kinds.size() != kernel.program.register_count
      || !member_execution_order_is_valid(kernel)
      || std::ranges::any_of(register_widths,
          [](const std::uint32_t width) {
            return width == 0U || width > 64U;
          })
      || std::ranges::any_of(register_value_kinds,
          [](const ValueKind kind) { return kind != ValueKind::logic4; })) {
    return false;
  }
  std::vector<std::uint8_t> input_registers(
      kernel.program.register_count, 0U);
  for (const auto& input : inputs) {
    if (input.value_kind != ValueKind::logic4
        || input.reg >= input_registers.size()) {
      return false;
    }
    input_registers[input.reg] = 1U;
  }

  // Forwarding kernels use this validated topological order to pass knownness
  // from each producer's output snapshot into later member CopyRegister ops.
  // The transfer helper still checks every operand before defining its result.
  const bool has_forwarding_order
      = !kernel.member_execution_order.empty();
  auto known_registers = input_registers;
  for (const auto member_index : member_execution_order(kernel)) {
    if (member_index >= kernel.members.size()) {
      return false;
    }
    const auto& member = kernel.members[member_index];
    if (member.begin > member.end
        || member.end > kernel.program.operations.size()) {
      return false;
    }
    std::optional<runtime::simir::RegionConeFinalDebugState>
        final_debug_state;
    if (!has_forwarding_order) {
      // Preserve the existing member-local proof for ordinary activation
      // kernels, whose member bodies do not share register definitions.
      known_registers = input_registers;
    }
    for (std::size_t index = member.begin; index < member.end; ++index) {
      const auto operation = kernel.program.operations.expanded(index);
      if (const auto* const point
          = runtime::simir::operation_get_if<DebugPoint>(&operation);
          point != nullptr) {
        final_debug_state = runtime::simir::RegionConeFinalDebugState {
            point->source, point->scope.str() };
      }
      if (!known_logic4_operation_preserves_values(
              operation, register_widths, register_value_kinds,
              known_registers, known_registers)) {
        return false;
      }
    }
    if (final_debug_state != member.final_debug_state) {
      return false;
    }
    for (const auto& binding : member.register_bindings) {
      if (!binding.defined) {
        continue;
      }
      if (binding.activation_register >= known_registers.size()
          || known_registers[binding.activation_register] == 0U) {
        return false;
      }
    }
    for (const auto& spill : spills) {
      if (spill.member_index == member_index && !spill.prefix_change_flag
          && (spill.activation_register >= known_registers.size()
              || known_registers[spill.activation_register] == 0U)) {
        return false;
      }
    }
  }
  return true;
}

[[nodiscard]] bool operation_defines_register(
    const runtime::simir::Operation& operation,
    const runtime::simir::RegisterId target) noexcept
{
  bool defines { };
  runtime::simir::visit_operation([&](const auto& value) {
    using Type = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<Type, LoadConstant>
        || std::is_same_v<Type, CopyRegister>
        || std::is_same_v<Type, UnaryNot>
        || std::is_same_v<Type, Binary>
        || std::is_same_v<Type, Reduction>
        || std::is_same_v<Type, Extract>
        || std::is_same_v<Type, Concatenate>
        || std::is_same_v<Type, ConditionalSelect>) {
      defines = value.destination == target;
    }
  }, operation);
  return defines;
}

[[nodiscard]] bool build_input_and_spill_bindings(
    const RegionConeActivationKernel& kernel,
    const bool use_constant_inputs,
    std::vector<InputBinding>& inputs,
    std::vector<PrefixInputBinding>& prefix_inputs,
    std::vector<PrefixFlagBinding>& prefix_flags,
    std::vector<SpillBinding>& spills,
    std::vector<std::vector<std::size_t>>& spills_by_member,
    std::vector<std::uint32_t>& signal_widths,
    std::vector<ValueKind>& signal_value_kinds,
    bool& frame_exports,
    bool& prefix_comparison_supported)
{
  const auto register_value_kinds
      = runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
          kernel.program.register_value_kinds);
  const auto register_count = kernel.program.register_count;
  if (register_count == 0U
      || register_value_kinds.size() != register_count
      || (kernel.program.scheduling_domain
              != runtime::simir::ProcessSchedulingDomain::systemverilog
          && kernel.program.scheduling_domain
              != runtime::simir::ProcessSchedulingDomain::generic)
      || std::ranges::any_of(register_value_kinds,
          [](const ValueKind kind) {
            return kind != ValueKind::logic4 && kind != ValueKind::logic9;
          })
      || !member_execution_order_is_valid(kernel)
      || kernel.members.empty() || kernel.outputs.empty()) {
    return false;
  }
  frame_exports = std::ranges::any_of(register_value_kinds,
      [](const ValueKind kind) { return kind == ValueKind::logic9; });
  prefix_inputs.clear();
  prefix_flags.clear();
  prefix_comparison_supported = false;

  struct UnassignedInput {
    runtime::simir::RegisterId reg { };
    SignalId source_signal { };
    std::uint32_t width { };
    ValueKind value_kind { ValueKind::logic4 };
    bool readiness { };
    std::size_t member_index { };
    bool internal { };
  };
  std::vector<UnassignedInput> input_registers;
  input_registers.reserve(kernel.inputs.size() + kernel.members.size());
  std::vector<std::uint8_t> register_roles(register_count, 0U);
  for (const auto& input : kernel.inputs) {
    if (!supported_value(input.value_kind, input.width)
        || input.value_register >= register_count
        || register_value_kinds[input.value_register]
            != input.value_kind
        || register_roles[input.value_register] != 0U) {
      return false;
    }
    register_roles[input.value_register] = 1U;
    input_registers.push_back({ input.value_register, input.signal,
        input.width, input.value_kind, false, 0U, input.internal });
  }
  for (std::size_t member_index = 0U;
       member_index < kernel.members.size(); ++member_index) {
    const auto& member = kernel.members[member_index];
    if (member.readiness_register >= register_count
        || register_value_kinds[member.readiness_register]
            != ValueKind::logic4
        || register_roles[member.readiness_register] != 0U
        || (member_index != 0U
            && kernel.members[member_index - 1U].process >= member.process)) {
      return false;
    }
    register_roles[member.readiness_register] = 2U;
    input_registers.push_back({ member.readiness_register, 0U, 1U,
        ValueKind::logic4, true, member_index, false });
  }
  std::ranges::sort(input_registers,
      std::ranges::less { }, &UnassignedInput::reg);

  if (input_registers.size()
      > std::numeric_limits<SignalId>::max()) {
    return false;
  }
  inputs.clear();
  inputs.reserve(input_registers.size());
  signal_widths.clear();
  signal_value_kinds.clear();
  signal_widths.reserve(input_registers.size());
  signal_value_kinds.reserve(input_registers.size());
  for (std::size_t index = 0U; index < input_registers.size(); ++index) {
    const auto& source = input_registers[index];
    const auto signal = static_cast<SignalId>(index);
    std::optional<PackedLogic4> specialization_value;
    if (use_constant_inputs && !source.readiness) {
      const auto* const constant
          = find_constant_input(kernel, source.source_signal);
      if (constant != nullptr) {
        if (source.internal || source.width != constant->width
            || source.value_kind != constant->value_kind) {
          return false;
        }
        specialization_value = constant->value;
      }
    }
    inputs.push_back({ source.reg, signal, source.width,
        source.value_kind, source.readiness, source.member_index,
        source.source_signal, source.internal,
        std::move(specialization_value) });
    signal_widths.push_back(source.width);
    signal_value_kinds.push_back(source.value_kind);
  }

  const auto can_compare_internal_outputs
      = kernel.program.scheduling_domain
              == runtime::simir::ProcessSchedulingDomain::systemverilog
          && !frame_exports && !kernel.internal_signals.empty()
          && std::ranges::is_sorted(kernel.internal_signals)
          && std::ranges::adjacent_find(kernel.internal_signals)
              == kernel.internal_signals.end();
  if (can_compare_internal_outputs) {
    for (const auto signal : kernel.internal_signals) {
      const auto output = std::ranges::find(kernel.outputs, signal,
          &runtime::simir::RegionConeOutputBinding::signal);
      if (output == kernel.outputs.end()
          || output->value_kind != ValueKind::logic4
          || output->width == 0U || output->width > 64U
          || output->offset != 0U
          || std::ranges::count(kernel.outputs, signal,
              &runtime::simir::RegionConeOutputBinding::signal) != 1U) {
        prefix_inputs.clear();
        prefix_flags.clear();
        break;
      }
      const auto committed_input = std::ranges::find_if(
          kernel.inputs, [signal](const auto& input) {
            return input.signal == signal && input.internal;
          });
      const auto committed_input_count = std::ranges::count_if(
          kernel.inputs, [signal](const auto& input) {
            return input.signal == signal && input.internal;
          });
      if (committed_input_count > 1) {
        prefix_inputs.clear();
        prefix_flags.clear();
        break;
      }
      const auto baseline_reg = static_cast<std::uint64_t>(register_count)
          + prefix_inputs.size();
      const auto synthetic_signal
          = static_cast<std::uint64_t>(inputs.size())
          + prefix_inputs.size();
      if (baseline_reg > std::numeric_limits<RegisterId>::max()
          || synthetic_signal > std::numeric_limits<SignalId>::max()) {
        prefix_inputs.clear();
        prefix_flags.clear();
        break;
      }
      prefix_inputs.push_back({ signal,
          committed_input == kernel.inputs.end()
              ? std::optional<RegisterId> { }
              : std::optional<RegisterId> {
                    committed_input->value_register },
          static_cast<RegisterId>(baseline_reg),
          static_cast<SignalId>(synthetic_signal), output->width });
      signal_widths.push_back(output->width);
      signal_value_kinds.push_back(ValueKind::logic4);
    }
    if (prefix_inputs.size() == kernel.internal_signals.size()) {
      prefix_comparison_supported = true;
    } else {
      prefix_inputs.clear();
      prefix_flags.clear();
      signal_widths.resize(inputs.size());
      signal_value_kinds.resize(inputs.size());
    }
  }

  spills_by_member.clear();
  spills_by_member.resize(kernel.members.size());
  std::vector<std::vector<runtime::simir::RegionConeKernelRegisterBinding>>
      defined_bindings(kernel.members.size());
  std::vector<std::uint8_t> activation_binding_seen(register_count, 0U);
  std::vector<std::uint8_t> activation_spill_seen(register_count, 0U);
  for (std::size_t member_index = 0U;
       member_index < kernel.members.size(); ++member_index) {
    const auto& member = kernel.members[member_index];
    auto& member_bindings = defined_bindings[member_index];
    member_bindings.reserve(member.register_bindings.size());
    std::vector<std::uint8_t> seen_source(register_count, 0U);
    std::vector<std::uint8_t> seen_activation(register_count, 0U);
    for (std::size_t binding_index = 0U;
         binding_index < member.register_bindings.size(); ++binding_index) {
      const auto& binding = member.register_bindings[binding_index];
      if (binding.source_register >= register_count
          || binding.activation_register >= register_count
          || binding.source_register != binding_index
          || register_roles[binding.activation_register] != 0U
          || activation_binding_seen[binding.activation_register] != 0U
          || seen_source[binding.source_register] != 0U
          || seen_activation[binding.activation_register] != 0U) {
        return false;
      }
      activation_binding_seen[binding.activation_register] = 1U;
      seen_source[binding.source_register] = 1U;
      seen_activation[binding.activation_register] = 1U;
      if (!binding.defined) {
        if (binding.width != 0U) {
          return false;
        }
        continue;
      }
      if (!supported_value(binding.value_kind, binding.width)
          || register_roles[binding.activation_register] != 0U
          || activation_spill_seen[binding.activation_register] != 0U
          || register_value_kinds[binding.activation_register]
              != binding.value_kind) {
        return false;
      }
      activation_spill_seen[binding.activation_register] = 1U;
      member_bindings.push_back(binding);
    }
    std::ranges::sort(member_bindings,
        std::ranges::less { },
        &runtime::simir::RegionConeKernelRegisterBinding::activation_register);
  }

  std::vector<std::size_t> output_member_indices;
  output_member_indices.reserve(kernel.outputs.size());
  std::optional<runtime::simir::RegionUpdateKind>
      generic_output_update_kind;
  for (const auto& output : kernel.outputs) {
    const bool systemverilog_output
        = kernel.program.scheduling_domain
                == runtime::simir::ProcessSchedulingDomain::systemverilog
        && output.update_kind
            == runtime::simir::RegionUpdateKind::systemverilog_active
        && output.domain
            == runtime::simir::SignalUpdateDomain::systemverilog_active;
    const bool vhdl_projected_output
        = kernel.program.scheduling_domain
                == runtime::simir::ProcessSchedulingDomain::generic
        && output.update_kind
            == runtime::simir::RegionUpdateKind::vhdl_projected
        && output.domain == runtime::simir::SignalUpdateDomain::generic
        && output.projected_mode
            == runtime::simir::ProjectedDelayMode::inertial
        && output.projected_delay == 0U
        && output.projected_rejection == 0U;
    const bool generic_update_output
        = kernel.program.scheduling_domain
                == runtime::simir::ProcessSchedulingDomain::generic
        && output.update_kind
            == runtime::simir::RegionUpdateKind::generic
        && output.domain == runtime::simir::SignalUpdateDomain::generic
        && output.projected_mode
            == runtime::simir::ProjectedDelayMode::inertial
        && output.projected_delay == 0U
        && output.projected_rejection == 0U;
    const bool output_contract_matches = systemverilog_output
        || vhdl_projected_output || generic_update_output;
    const bool internal_output
        = std::ranges::find(kernel.internal_signals, output.signal)
            != kernel.internal_signals.end();
    const auto output_signal_width = output.signal_width != 0U
        ? output.signal_width
        : output.offset == 0U ? output.width : 0U;
    const bool systemverilog_output_range_valid
        = output_signal_width != 0U
            && output.offset <= output_signal_width
            && output.width <= output_signal_width - output.offset;
    const bool internal_output_shape_valid = !internal_output
        || (output.offset == 0U
            && output.width == output_signal_width);
    const bool systemverilog_boundary_output
        = systemverilog_output && !internal_output;
    if (kernel.program.scheduling_domain
            == runtime::simir::ProcessSchedulingDomain::generic
        && (vhdl_projected_output || generic_update_output)) {
      if (generic_output_update_kind
          && *generic_output_update_kind != output.update_kind) {
        return false;
      }
      generic_output_update_kind = output.update_kind;
    }
    const auto member = std::ranges::find(kernel.members, output.owner,
        &RegionConeKernelMember::process);
    if (member == kernel.members.end() || output.width == 0U
        || output.offset
            > std::numeric_limits<std::uint32_t>::max() - output.width
        || (output.offset != 0U && !generic_update_output
            && !systemverilog_boundary_output)
        || (systemverilog_output && !systemverilog_output_range_valid)
        || !internal_output_shape_valid
        || !output_contract_matches
        || !supported_value(output.value_kind, output.width)
        || output.value_register >= register_count
        || register_roles[output.value_register] != 0U
        || output.kernel_instruction >= kernel.program.operations.size()
        || register_value_kinds[output.value_register]
            != output.value_kind) {
      return false;
    }
    const auto member_index = static_cast<std::size_t>(
        member - kernel.members.begin());
    const auto& member_metadata = kernel.members[member_index];
    if (output.kernel_instruction < member_metadata.begin
        || output.kernel_instruction >= member_metadata.end
        || activation_binding_seen[output.value_register] != 0U
        || activation_spill_seen[output.value_register] != 0U) {
      return false;
    }
    const auto operation
        = kernel.program.operations.expanded(output.kernel_instruction);
    const auto* copy = runtime::simir::operation_get_if<CopyRegister>(
        &operation);
    if (copy == nullptr || copy->destination != output.value_register
        || copy->source >= register_count
        || register_value_kinds[copy->source]
            != output.value_kind) {
      return false;
    }
    std::size_t definition_count { };
    for (std::size_t instruction = 0U;
         instruction < kernel.program.operations.size(); ++instruction) {
      const auto candidate
          = kernel.program.operations.expanded(instruction);
      if (!operation_defines_register(candidate, output.value_register)) {
        continue;
      }
      if (instruction != output.kernel_instruction
          || !runtime::simir::operation_holds<CopyRegister>(candidate)) {
        return false;
      }
      ++definition_count;
    }
    if (definition_count != 1U) {
      return false;
    }
    activation_spill_seen[output.value_register] = 1U;
    output_member_indices.push_back(member_index);
  }

  spills.clear();
  std::size_t required_signal_count = inputs.size();
  if (prefix_inputs.size()
      > std::numeric_limits<std::size_t>::max() - required_signal_count) {
    return false;
  }
  required_signal_count += prefix_inputs.size();
  if (!frame_exports) {
    for (const auto& member_bindings : defined_bindings) {
      if (member_bindings.size()
          > std::numeric_limits<std::size_t>::max() - required_signal_count) {
        return false;
      }
      required_signal_count += member_bindings.size();
    }
    if (kernel.outputs.size()
        > std::numeric_limits<std::size_t>::max() - required_signal_count) {
      return false;
    }
    required_signal_count += kernel.outputs.size();
  }
  if (required_signal_count > std::numeric_limits<SignalId>::max()) {
    return false;
  }
  spills.reserve(required_signal_count - inputs.size()
      - prefix_inputs.size());
  for (std::size_t member_index = 0U;
       member_index < defined_bindings.size(); ++member_index) {
    for (const auto& binding : defined_bindings[member_index]) {
      const auto signal = frame_exports
          ? std::numeric_limits<SignalId>::max()
          : static_cast<SignalId>(inputs.size() + prefix_inputs.size()
              + spills.size());
      const auto spill_index = spills.size();
      spills.push_back({ member_index, binding.activation_register, signal,
          binding.width, binding.value_kind,
          std::numeric_limits<std::size_t>::max(), false });
      spills_by_member[member_index].push_back(spill_index);
      if (!frame_exports) {
        signal_widths.push_back(binding.width);
        signal_value_kinds.push_back(binding.value_kind);
      }
    }
  }
  for (std::size_t output_index = 0U;
       output_index < kernel.outputs.size(); ++output_index) {
    const auto& output = kernel.outputs[output_index];
    const auto member_index = output_member_indices[output_index];
    const auto signal = frame_exports
        ? std::numeric_limits<SignalId>::max()
        : static_cast<SignalId>(inputs.size() + prefix_inputs.size()
            + spills.size());
    const auto spill_index = spills.size();
    spills.push_back({ member_index, output.value_register, signal,
        output.width, output.value_kind, output_index, false });
    spills_by_member[member_index].push_back(spill_index);
    if (!frame_exports) {
      signal_widths.push_back(output.width);
      signal_value_kinds.push_back(output.value_kind);
    }
  }
  if (prefix_comparison_supported) {
    for (std::size_t output_index = 0U;
         output_index < kernel.outputs.size(); ++output_index) {
      const auto& output = kernel.outputs[output_index];
      const auto input = std::ranges::find(prefix_inputs, output.signal,
          &PrefixInputBinding::source_signal);
      if (input == prefix_inputs.end()) {
        continue;
      }
      const auto activation_register
          = static_cast<std::uint64_t>(register_count)
          + prefix_inputs.size() + prefix_flags.size();
      const auto synthetic_signal
          = static_cast<std::uint64_t>(inputs.size())
          + prefix_inputs.size() + spills.size();
      if (activation_register > std::numeric_limits<RegisterId>::max()
          || synthetic_signal > std::numeric_limits<SignalId>::max()) {
        return false;
      }
      const auto member_index = output_member_indices[output_index];
      if (member_index >= kernel.members.size()) {
        return false;
      }
      prefix_flags.push_back({ output_index, member_index,
          static_cast<RegisterId>(activation_register),
          input->activation_register,
          kernel.members[member_index].readiness_register,
          static_cast<SignalId>(synthetic_signal) });
      spills.push_back({ member_index,
          static_cast<RegisterId>(activation_register),
          static_cast<SignalId>(synthetic_signal), 1U,
          ValueKind::logic4, output_index, true });
      signal_widths.push_back(1U);
      signal_value_kinds.push_back(ValueKind::logic4);
    }
    if (prefix_flags.size() != kernel.internal_signals.size()
        || prefix_flags.size()
            > std::numeric_limits<std::size_t>::max()
                - required_signal_count
        || required_signal_count + prefix_flags.size()
            > std::numeric_limits<SignalId>::max()) {
      return false;
    }
    required_signal_count += prefix_flags.size();
  }
  return signal_widths.size() == required_signal_count
      && signal_value_kinds.size() == required_signal_count;
}

[[nodiscard]] bool build_synthetic_process(
    const RegionConeActivationKernel& kernel,
    const std::span<const InputBinding> inputs,
    const std::span<const PrefixInputBinding> prefix_inputs,
    const std::span<const PrefixFlagBinding> prefix_flags,
    const std::span<const SpillBinding> spills,
    const std::vector<std::vector<std::size_t>>& spills_by_member,
    const bool frame_exports,
    Process& synthetic)
{
  const auto& source = kernel.program;
  const auto source_register_value_kinds
      = runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
          source.register_value_kinds);
  if (source.operations.empty() || kernel.members.empty()
      || spills_by_member.size() != kernel.members.size()
      || source_register_value_kinds.size() != source.register_count
      || !runtime::simir::operation_holds<Halt>(
          source.operations.expanded(source.operations.size() - 1U))) {
    return false;
  }

  // A forwarding image is admitted only when every member is ready. Emit
  // that proven contract directly so a child's read of its producer's private
  // snapshot is definitely assigned across member boundaries.
  const bool all_members_required = !kernel.member_execution_order.empty();
  std::size_t operation_count = inputs.size();
  const auto append_operation_count = [&](const std::size_t count) {
    if (count > std::numeric_limits<std::size_t>::max() - operation_count) {
      return false;
    }
    operation_count += count;
    return true;
  };
  if (!append_operation_count(prefix_inputs.size())
      || (!all_members_required
          && !append_operation_count(kernel.members.size()))) {
    return false;
  }
  std::size_t previous_end { };
  for (const auto member_index : member_execution_order(kernel)) {
    if (member_index >= kernel.members.size()) {
      return false;
    }
    const auto& member = kernel.members[member_index];
    if (member.begin < previous_end || member.begin > member.end
        || member.end > source.operations.size()) {
      return false;
    }
    for (std::size_t index = member.begin; index < member.end; ++index) {
      const auto operation = source.operations.expanded(index);
      if (!operation_holds<DebugPoint>(operation)
          && !append_operation_count(1U)) {
        return false;
      }
    }
    previous_end = member.end;
  }
  if (prefix_flags.size() > spills.size()) {
    return false;
  }
  const auto spill_operation_count = frame_exports
      ? 0U : spills.size() - prefix_flags.size();
  if (!append_operation_count(spill_operation_count)
      || prefix_flags.size()
          > std::numeric_limits<std::size_t>::max() / 3U
      || !append_operation_count(prefix_flags.size() * 3U)
      || !append_operation_count(1U)
      || operation_count
          >= std::numeric_limits<runtime::simir::InstructionIndex>::max()) {
    return false;
  }

  if (prefix_inputs.size() > std::numeric_limits<std::uint32_t>::max()
      || prefix_flags.size() > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  const auto synthetic_register_count
      = static_cast<std::uint64_t>(source.register_count)
      + prefix_inputs.size() + prefix_flags.size();
  if (synthetic_register_count
      > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }

  synthetic = source;
  // The synthetic process is owned by the wrapper, not by any physical
  // member. A fixed ID lets the JIT object's process key follow code shape;
  // original ProcessIds remain in the wrapper identity and runtime maps.
  synthetic.id = 0U;
  synthetic.name = "simir_region_activation_native";
  synthetic.language_standard.clear();
  synthetic.compatibility_profile.clear();
  synthetic.register_count
      = static_cast<std::uint32_t>(synthetic_register_count);
  synthetic.register_value_kinds.reserve(synthetic.register_count);
  synthetic.register_value_kinds.assign(
      source_register_value_kinds.begin(), source_register_value_kinds.end());
  synthetic.register_value_kinds.resize(
      synthetic.register_count, ValueKind::logic4);
  synthetic.string_register_count = 0U;
  synthetic.container_register_count = 0U;
  synthetic.debug_locals.clear();
  synthetic.debug_string_locals.clear();
  synthetic.debug_container_locals.clear();
  synthetic.container_register_types.clear();
  synthetic.static_sensitivity.clear();
  synthetic.static_trigger_regions.clear();
  synthetic.driver_regions.clear();
  synthetic.drive_strength = runtime::simir::DriveStrength { };
  synthetic.switch_source.reset();
  synthetic.switch_target.reset();
  synthetic.switch_control.reset();
  synthetic.switch_source_offset = 0U;
  synthetic.switch_target_offset = 0U;
  synthetic.switch_width = 0U;
  synthetic.switch_active_high = true;
  synthetic.switch_bidirectional = false;
  synthetic.switch_resistive = false;
  synthetic.scheduling_domain
      = runtime::simir::ProcessSchedulingDomain::generic;
  synthetic.observed = false;
  synthetic.reactive = false;
  synthetic.postponed = false;
  synthetic.final = false;
  synthetic.program_owner.reset();
  synthetic.expression_profiles = runtime::simir::ExpressionProfileList { };
  synthetic.initialize = true;

  if (frame_exports) {
    synthetic.debug_locals.reserve(spills.size());
    for (const auto& spill : spills) {
      if (spill.member_index >= kernel.members.size()
          || spill.activation_register >= source.register_count
          || spill.value_kind
              != source_register_value_kinds[spill.activation_register]) {
        return false;
      }
      runtime::simir::DebugLocal local;
      local.name = "__fsim_region_export_"
          + std::to_string(spill.member_index)
          + "_" + std::to_string(spill.activation_register);
      local.type_name = spill.value_kind == ValueKind::logic9
          ? "logic9" : "logic4";
      local.register_id = spill.activation_register;
      local.width = spill.width;
      local.value_kind = spill.value_kind;
      synthetic.debug_locals.push_back(std::move(local));
    }
  }

  synthetic.operations = runtime::simir::OperationList { };
  synthetic.operations.reserve(operation_count);
  for (const auto& input : inputs) {
    if (input.specialization_value) {
      synthetic.operations.push_back(LoadConstant {
          input.reg, *input.specialization_value });
    } else {
      synthetic.operations.push_back(ReadSignal {
          input.reg, input.synthetic_signal, SignalReadKind::current, 1U,
          std::nullopt, runtime::simir::SampledClockEdge::any, std::nullopt });
    }
  }
  for (const auto& input : prefix_inputs) {
    synthetic.operations.push_back(ReadSignal {
        input.activation_register, input.synthetic_signal,
        SignalReadKind::current, 1U, std::nullopt,
        runtime::simir::SampledClockEdge::any, std::nullopt });
  }

  std::size_t old_cursor { };
  for (const auto member_index : member_execution_order(kernel)) {
    if (member_index >= kernel.members.size()) {
      return false;
    }
    const auto& member = kernel.members[member_index];
    if (member.branch_instruction != old_cursor
        || member.begin != member.branch_instruction + 1U
        || member.begin >= member.end
        || member.end > source.operations.size()) {
      return false;
    }
    const auto original_branch
        = source.operations.expanded(member.branch_instruction);
    const auto* branch = runtime::simir::operation_get_if<Branch>(
        &original_branch);
    if (branch == nullptr || branch->condition != member.readiness_register
        || branch->when_true != member.begin
        || branch->when_false != member.end
        || branch->unknown_policy != runtime::simir::UnknownBranchPolicy::error) {
      return false;
    }

    const auto new_branch_index = static_cast<runtime::simir::InstructionIndex>(
        synthetic.operations.size());
    const auto new_body_begin = static_cast<runtime::simir::InstructionIndex>(
        synthetic.operations.size() + 1U);
    if (!all_members_required) {
      synthetic.operations.push_back(Branch {
          member.readiness_register, new_body_begin, new_body_begin,
          runtime::simir::UnknownBranchPolicy::error });
    }

    for (std::size_t old_index = member.begin; old_index < member.end;
         ++old_index) {
      const auto operation = source.operations.expanded(old_index);
      if (const auto* select
          = runtime::simir::operation_get_if<ConditionalSelect>(&operation);
          select != nullptr
          && (select->condition >= source_register_value_kinds.size()
              || select->when_true >= source_register_value_kinds.size()
              || select->when_false >= source_register_value_kinds.size()
              || select->destination >= source_register_value_kinds.size()
              || source_register_value_kinds[select->condition]
                  != ValueKind::logic4
              || source_register_value_kinds[select->when_true]
                  != ValueKind::logic4
              || source_register_value_kinds[select->when_false]
                  != ValueKind::logic4
              || source_register_value_kinds[select->destination]
                  != ValueKind::logic4)) {
        // Public kernel descriptors can be constructed without the graph
        // builder, so keep this specialization's Logic4 contract here too.
        return false;
      }
      if (const auto* binary
          = runtime::simir::operation_get_if<Binary>(&operation);
          binary != nullptr
          && binary->operation == BinaryOperator::add_unsigned
          && (binary->lhs >= source_register_value_kinds.size()
              || binary->rhs >= source_register_value_kinds.size()
              || binary->destination >= source_register_value_kinds.size()
              || source_register_value_kinds[binary->lhs]
                  != ValueKind::logic4
              || source_register_value_kinds[binary->rhs]
                  != ValueKind::logic4
              || source_register_value_kinds[binary->destination]
                  != ValueKind::logic4)) {
        // Public kernel descriptors can bypass RegionGraph's Logic4
        // certificate, so keep add_unsigned's type contract here too.
        return false;
      }
      if (!supported_compute_operation(operation)) {
        return false;
      }
      if (operation_holds<DebugPoint>(operation)) {
        // DebugPoint only supplies the final source/scope metadata retained
        // on the original ProcessState. It has no kernel-visible value effect.
        continue;
      }
      synthetic.operations.push_back(operation);
    }
    if (!frame_exports) {
      for (const auto spill_index : spills_by_member[member_index]) {
        if (spill_index >= spills.size()
            || spills[spill_index].member_index != member_index) {
          return false;
        }
        synthetic.operations.push_back(WriteUpdate {
            spills[spill_index].synthetic_signal,
            spills[spill_index].activation_register,
            runtime::simir::SignalUpdateDomain::generic });
      }
    }

    // Keep each comparison under the branch that defines its snapshot.
    // A separate branch on the same readiness value does not establish
    // definite assignment for the generated process validator.
    for (const auto& flag : prefix_flags) {
      if (flag.member_index != member_index) {
        continue;
      }
      if (flag.output_index >= kernel.outputs.size()
          || flag.readiness_register != member.readiness_register) {
        return false;
      }
      const auto& output = kernel.outputs[flag.output_index];
      synthetic.operations.push_back(Binary {
          BinaryOperator::case_equal, flag.activation_register,
          output.value_register, flag.current_register });
      synthetic.operations.push_back(UnaryNot {
          flag.activation_register, flag.activation_register });
      synthetic.operations.push_back(WriteUpdate {
          flag.synthetic_signal, flag.activation_register,
          runtime::simir::SignalUpdateDomain::generic });
    }

    const auto false_target = static_cast<runtime::simir::InstructionIndex>(
        synthetic.operations.size());
    if (!all_members_required) {
      synthetic.operations.replace(new_branch_index, Branch {
          member.readiness_register, new_body_begin, false_target,
          runtime::simir::UnknownBranchPolicy::error });
    }
    old_cursor = member.end;
  }
  if (old_cursor + 1U != source.operations.size()) {
    return false;
  }
  synthetic.operations.push_back(Halt { });
  return synthetic.operations.size() == operation_count;
}

[[nodiscard]] bool is_active_member(
    const runtime::simir::RegionKernelActivationImage& image,
    const std::size_t member_index) noexcept
{
  return std::ranges::binary_search(image.active_member_indices, member_index);
}

[[nodiscard]] bool same_layout_shape(
    const JitProcessFrameLayout& left,
    const JitProcessFrameLayout& right) noexcept
{
  return left.register_count == right.register_count
      && left.register_word_count == right.register_word_count
      && left.string_register_count == right.string_register_count
      && left.uses_logic9 == right.uses_logic9
      && left.tracks_register_initialization
          == right.tracks_register_initialization
      && left.register_widths == right.register_widths
      && left.register_word_offsets == right.register_word_offsets
      && left.direct_read_signals == right.direct_read_signals
      && left.direct_update_signals == right.direct_update_signals
      && left.signal_callback_operands == right.signal_callback_operands
      && left.signal_callback_operand_word_base
          == right.signal_callback_operand_word_base
      && left.signal_callback_ids_are_actual
          == right.signal_callback_ids_are_actual
      && left.register_values_persistent
          == right.register_values_persistent;
}

struct RegionKernelNativeBody final {
  explicit RegionKernelNativeBody(LlvmJitOptions options)
      : jit(std::move(options))
  {
  }

  LlvmJit jit;
  // Process lookup and certificate binding touch the shared ORC/JIT owner.
  // Serialize those construction-time operations; resumptions use immutable
  // function pointers and instance-local frames.
  std::mutex api_mutex;
  JitProcessBinding binding;
  JitProcessFrameLayout layout;
  JitProcessBinding known_binding;
  JitProcessFrameLayout known_layout;
  bool known_variant_ready { };
};

using RegionKernelNativeBodyRegistry
    = llvm_detail::WeakSingleFlightRegistry<RegionKernelNativeBody>;

[[nodiscard]] RegionKernelNativeBodyRegistry& region_native_body_registry()
{
  static RegionKernelNativeBodyRegistry registry;
  return registry;
}

struct RegionKernelPreparedTemplate final {
  Process synthetic;
  std::vector<std::uint32_t> signal_widths;
  std::vector<ValueKind> signal_value_kinds;
  llvm_detail::ValidatedProcess validated;
  bool frame_exports { };
  bool prefix_comparison_supported { };
};

struct RegionKernelPreparedTemplateRegistry final {
  llvm_detail::WeakSingleFlightRegistry<RegionKernelPreparedTemplate> entries;
  std::atomic<std::size_t> template_builds { };
  std::atomic<std::size_t> process_validations { };
};

[[nodiscard]] RegionKernelPreparedTemplateRegistry&
region_prepared_template_registry()
{
  static RegionKernelPreparedTemplateRegistry registry;
  return registry;
}

[[nodiscard]] bool environment_flag_is_enabled(
    const char* const name, const bool empty_is_enabled = false) noexcept
{
  const auto* const value = std::getenv(name);
  return value != nullptr
      && (empty_is_enabled
          || (*value != '\0' && std::string_view { value } != "0"));
}

[[nodiscard]] std::string region_native_body_configuration_identity(
    const LlvmJitOptions& options)
{
  const auto host = LlvmJit::native_host_identity(options.optimization);
  CacheKeyBuilder key;
  key.add("kind", "fsim-region-native-body-registry-v1");
  key.add("host-fingerprint", host.fingerprint);
  key.add("llvm-version", host.llvm_version);
  key.add("llvm-arguments", llvm_detail::initialize_llvm_arguments());
  key.add("optimization", to_string(options.optimization));
  const auto cache_path = options.cache_directory.empty()
      ? std::string { }
      : std::filesystem::absolute(options.cache_directory)
            .lexically_normal().generic_string();
  key.add("cache-directory", cache_path);
  key.add("cache-maximum-bytes", options.cache_maximum_bytes
          ? std::to_string(*options.cache_maximum_bytes) : "disabled");
  key.add("cache-maximum-entries", options.cache_maximum_entries
          ? std::to_string(*options.cache_maximum_entries) : "disabled");
  key.add("cache-maximum-age-seconds", options.cache_maximum_age
          ? std::to_string(options.cache_maximum_age->count()) : "disabled");
  key.add("debug-instrumentation",
      options.debug_instrumentation ? "enabled" : "disabled");
  key.add("direct-update-slots",
      options.require_direct_update_slots ? "required" : "optional");
  key.add("direct-read-signals",
      options.require_direct_read_signals ? "required" : "optional");
  key.add("coverage-identity", options.code_coverage_identity);
#ifndef NDEBUG
  key.add("verify-optimized-modules", "enabled-debug-build");
#else
  key.add("verify-optimized-modules",
      environment_flag_is_enabled("FSIM_VERIFY_LLVM_MODULES")
          ? "enabled" : "disabled");
#endif
  key.add("profile-llvm-modules",
      environment_flag_is_enabled("FSIM_PROFILE_LLVM_MODULES", true)
          ? "enabled" : "disabled");
  key.add("profile-sv-waves",
      environment_flag_is_enabled("FSIM_PROFILE_SV_WAVES", true)
          ? "enabled" : "disabled");
  return key.finish();
}

[[nodiscard]] std::optional<std::string>
region_prepared_template_cache_key(
    const RegionConeActivationKernel& physical_kernel,
    const RegionConeActivationKernel& canonical_kernel,
    const std::string_view design_identity,
    const std::string_view configuration_identity,
    const std::span<const InputBinding> inputs,
    const std::span<const PrefixInputBinding> prefix_inputs,
    const std::span<const PrefixFlagBinding> prefix_flags,
    const std::span<const SpillBinding> spills,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const ValueKind> signal_value_kinds,
    const std::span<const std::uint32_t> prepared_output_signals,
    const std::span<const std::uint64_t> prepared_output_successor_masks,
    const bool prepared_output_successor_masks_representable,
    const std::span<const llvm_detail::RegionDirectReadyLoweringBinding>
        direct_ready_bindings,
    const bool frame_exports,
    const bool prefix_comparison_supported)
{
  const auto member_operations
      = expanded_member_operation_identity(physical_kernel);
  if (!member_operations) {
    return std::nullopt;
  }

  const auto canonical_source_identity = region_cache_identity(
      canonical_kernel, design_identity, inputs, spills, frame_exports,
      *member_operations, "fsim-region-prepared-template-source-v1");
  CacheKeyBuilder key;
  const auto add_number = [&key](const std::string_view label,
                                const std::uint64_t value) {
    key.add(label, std::to_string(value));
  };
  key.add("prepared-template-domain", "fsim-region-prepared-template-v1");
  key.add("canonical-source", canonical_source_identity);
  key.add("effective-jit-configuration", configuration_identity);
  key.add("specialization",
      "dynamic-or-guarded-constant-inputs-v1");
  key.add("member-order-contract",
      canonical_kernel.member_execution_order.empty()
          ? "process-order-masked-v1" : "topological-all-members-v1");
  key.add("all-ready-contract",
      canonical_kernel.member_execution_order.empty()
          ? "masked-readiness-v1" : "exact-full-ready-set-v1");
  key.add("knownness-proof-contract", "cross-member-register-proof-v2");
  add_number("knownness-member-count", canonical_kernel.members.size());
  for (const auto& member : canonical_kernel.members) {
    add_number("knownness-member-definite-definition",
        member.all_registers_definitely_defined ? 1U : 0U);
  }
  key.add("frame-exports", frame_exports ? "enabled" : "disabled");
  key.add("prefix-comparison",
      prefix_comparison_supported ? "enabled" : "disabled");

  const auto add_canonical_signal = [&](const std::string_view label,
                                        const SignalId physical_signal) {
    const auto canonical = canonical_signal_id(
        physical_kernel, canonical_kernel, physical_signal);
    if (!canonical) {
      return false;
    }
    add_number(label, *canonical);
    return true;
  };

  add_number("signal-layout-count", signal_widths.size());
  if (signal_widths.size() != signal_value_kinds.size()) {
    return std::nullopt;
  }
  for (std::size_t index = 0U; index < signal_widths.size(); ++index) {
    add_number("signal-layout-width", signal_widths[index]);
    add_number("signal-layout-kind",
        static_cast<std::uint8_t>(signal_value_kinds[index]));
  }

  add_number("input-bindings", inputs.size());
  for (const auto& input : inputs) {
    add_number("input-register", input.reg);
    add_number("input-synthetic-signal", input.synthetic_signal);
    add_number("input-width", input.width);
    add_number("input-kind", static_cast<std::uint8_t>(input.value_kind));
    add_number("input-readiness", input.readiness ? 1U : 0U);
    add_number("input-member", input.member_index);
    add_number("input-internal", input.internal ? 1U : 0U);
    add_number("input-specialized",
        input.specialization_value.has_value() ? 1U : 0U);
    if (input.readiness) {
      continue;
    }
    if (!add_canonical_signal("input-physical-source", input.source_signal)) {
      return std::nullopt;
    }
    if (input.specialization_value) {
      llvm_detail::add_packed_value_key(key, *input.specialization_value);
    }
  }

  add_number("prefix-inputs", prefix_inputs.size());
  for (const auto& input : prefix_inputs) {
    if (!add_canonical_signal("prefix-physical-source", input.source_signal)) {
      return std::nullopt;
    }
    add_number("prefix-has-committed-register",
        input.committed_input_register.has_value() ? 1U : 0U);
    if (input.committed_input_register) {
      add_number("prefix-committed-register",
          *input.committed_input_register);
    }
    add_number("prefix-activation-register", input.activation_register);
    add_number("prefix-synthetic-signal", input.synthetic_signal);
    add_number("prefix-width", input.width);
  }

  add_number("prefix-flags", prefix_flags.size());
  for (const auto& flag : prefix_flags) {
    add_number("prefix-flag-output", flag.output_index);
    add_number("prefix-flag-member", flag.member_index);
    add_number("prefix-flag-activation-register", flag.activation_register);
    add_number("prefix-flag-current-register", flag.current_register);
    add_number("prefix-flag-readiness-register", flag.readiness_register);
    add_number("prefix-flag-synthetic-signal", flag.synthetic_signal);
  }

  add_number("spills", spills.size());
  for (const auto& spill : spills) {
    add_number("spill-member", spill.member_index);
    add_number("spill-register", spill.activation_register);
    add_number("spill-synthetic-signal", spill.synthetic_signal);
    add_number("spill-width", spill.width);
    add_number("spill-kind", static_cast<std::uint8_t>(spill.value_kind));
    add_number("spill-output-index", spill.prefix_output_index);
    add_number("spill-prefix-flag", spill.prefix_change_flag ? 1U : 0U);
  }

  add_number("prepared-output-signals", prepared_output_signals.size());
  for (const auto signal : prepared_output_signals) {
    add_number("prepared-output-synthetic-signal", signal);
  }
  key.add("prepared-output-successor-mask-contract",
      prepared_output_successor_masks_representable
          ? "representable-v1" : "unrepresentable-v1");
  add_number("prepared-output-successor-mask-count",
      prepared_output_successor_masks.size());
  for (const auto mask : prepared_output_successor_masks) {
    add_number("prepared-output-successor-mask", mask);
  }

  // Bindings retain canonical physical relationships in this template key;
  // the executor's public mapping identity retains the exact physical IDs.
  add_number("direct-ready-bindings", direct_ready_bindings.size());
  const auto direct_input_count = static_cast<std::size_t>(
      std::ranges::count_if(inputs,
          [](const InputBinding& input) { return !input.readiness; }));
  for (const auto& binding : direct_ready_bindings) {
    add_number("direct-ready-kind",
        static_cast<std::uint32_t>(binding.kind));
    add_number("direct-ready-source-index", binding.source_index);
    add_number("direct-ready-register", binding.register_id);
    add_number("direct-ready-synthetic-signal",
        binding.synthetic_signal_id);
    add_number("direct-ready-width", binding.width);
    switch (binding.kind) {
    case llvm_detail::RegionDirectReadyBindingKind::member_ready:
      break;
    case llvm_detail::RegionDirectReadyBindingKind::input_slot: {
      std::size_t slot { };
      const auto found = std::ranges::find_if(inputs,
          [&slot, &binding](const InputBinding& input) {
            if (input.readiness) {
              return false;
            }
            return slot++ == binding.source_index;
          });
      if (found == inputs.end()
          || !add_canonical_signal("direct-ready-physical-source",
              found->source_signal)) {
        return std::nullopt;
      }
      break;
    }
    case llvm_detail::RegionDirectReadyBindingKind::prefix_current: {
      if (binding.source_index < direct_input_count) {
        return std::nullopt;
      }
      const auto prefix_index = binding.source_index - direct_input_count;
      if (prefix_index >= prefix_inputs.size()
          || !add_canonical_signal("direct-ready-physical-source",
              prefix_inputs[prefix_index].source_signal)) {
        return std::nullopt;
      }
      break;
    }
    }
  }

  return key.finish();
}

} // namespace

struct LlvmRegionKernelExecutor::Impl {
  explicit Impl(const RegionConeActivationKernel& source,
      LlvmJitOptions options,
      const RegionKernelSpecialization requested_specialization)
      : kernel(source)
      , specialization(requested_specialization)
      , options(std::move(options))
  {
  }

  [[nodiscard]] bool reject_prepare(
      const std::size_t line,
      const std::string_view detail = { }) const noexcept
  {
    report_region_creation_event(kernel, specialization,
        "prepare-decline", "prepare", line, detail);
    return false;
  }

  [[nodiscard]] LlvmJit& jit() noexcept
  {
    return native_body->jit;
  }

  [[nodiscard]] const LlvmJit& jit() const noexcept
  {
    return native_body->jit;
  }

  [[nodiscard]] bool prepare(const std::string_view design_identity)
  {
    const bool use_constant_inputs
        = specialization
            == RegionKernelSpecialization::guarded_constant_inputs;
    if ((use_constant_inputs
            && (kernel.constant_inputs.empty()
                || !constant_inputs_are_valid(kernel)))
        || !build_input_and_spill_bindings(kernel, use_constant_inputs,
            inputs, prefix_inputs, prefix_flags, spills, spills_by_member,
            signal_widths, signal_value_kinds, frame_exports,
            prefix_comparison_supported)) {
      return reject_prepare(__LINE__);
    }
    const bool has_wide_signal = std::ranges::any_of(
        signal_widths, [](const std::uint32_t width) { return width > 64U; });
    if (has_wide_signal) {
      if (!build_flat_word_offsets(signal_widths,
              direct_wide_signal_offsets, direct_wide_word_count)) {
        return reject_prepare(__LINE__);
      }
    } else {
      direct_wide_signal_offsets.clear();
      direct_wide_word_count = 0U;
    }
    prepared_output_signals.clear();
    prepared_output_successor_masks.clear();
    prepared_output_successor_masks_representable = true;
    if (prefix_comparison_supported && !frame_exports
        && kernel.program.scheduling_domain
            == runtime::simir::ProcessSchedulingDomain::systemverilog) {
      prepared_output_signals.reserve(kernel.internal_signals.size());
      prepared_output_successor_masks.reserve(kernel.internal_signals.size());
      for (const auto signal : kernel.internal_signals) {
        const auto output = std::ranges::find(kernel.outputs, signal,
            &runtime::simir::RegionConeOutputBinding::signal);
        if (output == kernel.outputs.end()) {
          prepared_output_signals.clear();
          prepared_output_successor_masks.clear();
          break;
        }
        if (std::ranges::count(kernel.outputs, signal,
                &runtime::simir::RegionConeOutputBinding::signal) != 1U) {
          // This first prepared-output ABI owns one raw driver record per
          // signal. Multi-owner resolution remains on the checked publisher.
          prepared_output_signals.clear();
          prepared_output_successor_masks.clear();
          break;
        }
        const auto internal_input = std::ranges::find_if(
            kernel.inputs, [signal](const auto& input) {
              return input.signal == signal && input.internal;
            });
        if (internal_input == kernel.inputs.end()
            || output->offset != 0U
            || output->width != internal_input->width
            || output->value_kind != ValueKind::logic4
            || output->domain
                != runtime::simir::SignalUpdateDomain::systemverilog_active
            || output->update_kind
                != runtime::simir::RegionUpdateKind::systemverilog_active) {
          prepared_output_signals.clear();
          prepared_output_successor_masks.clear();
          break;
        }
        const auto spill = std::ranges::find_if(spills,
            [this, &output](const SpillBinding& candidate) {
                return !candidate.prefix_change_flag
                    && candidate.activation_register == output->value_register
                    && candidate.member_index < kernel.members.size()
                    && kernel.members[candidate.member_index].process
                        == output->owner;
            });
        if (spill == spills.end() || spill->width == 0U
            || spill->width > 64U || spill->value_kind != ValueKind::logic4) {
          prepared_output_signals.clear();
          prepared_output_successor_masks.clear();
          break;
        }
        prepared_output_signals.push_back(spill->synthetic_signal);
        std::uint64_t successor_mask { };
        std::size_t successor_index { };
        for (std::size_t member_index = 0U;
             member_index < kernel.members.size(); ++member_index) {
          const auto& member = kernel.members[member_index];
          const bool reads_whole_any = std::ranges::any_of(
              member.sensitivities, [signal](const auto& sensitivity) {
                  return sensitivity.signal == signal
                      && sensitivity.edge == runtime::simir::EdgeKind::any
                      && sensitivity.offset == 0U
                      && sensitivity.width == 0U;
              });
          if (!reads_whole_any) {
            continue;
          }
          if (successor_index >= 64U) {
            successor_mask = 0U;
            prepared_output_successor_masks_representable = false;
            successor_index = 65U;
            break;
          }
          successor_mask |= UINT64_C(1) << successor_index;
          ++successor_index;
        }
        prepared_output_successor_masks.push_back(
            successor_index <= 64U ? successor_mask : 0U);
      }
    }
    direct_ready_bindings.clear();
    if (!use_constant_inputs && !frame_exports
        && prefix_comparison_supported
        && !prepared_output_signals.empty()
        && kernel.program.scheduling_domain
            == runtime::simir::ProcessSchedulingDomain::systemverilog) {
      const bool narrow_logic4_inputs = std::ranges::all_of(
          inputs, [](const InputBinding& input) {
              return input.width != 0U && input.width <= 64U
                  && input.value_kind == ValueKind::logic4
                  && !input.specialization_value.has_value();
          }) && std::ranges::all_of(prefix_inputs,
          [](const PrefixInputBinding& input) {
              return input.width != 0U && input.width <= 64U;
          });
      if (narrow_logic4_inputs
          && inputs.size() + prefix_inputs.size()
              <= std::numeric_limits<std::uint32_t>::max()) {
        std::size_t input_slot { };
        direct_ready_bindings.reserve(
            inputs.size() + prefix_inputs.size());
        for (const auto& input : inputs) {
          if (input.readiness) {
            if (input.member_index
                > std::numeric_limits<std::uint32_t>::max()) {
              direct_ready_bindings.clear();
              break;
            }
            direct_ready_bindings.push_back({
                llvm_detail::RegionDirectReadyBindingKind::member_ready,
                static_cast<std::uint32_t>(input.member_index), 0U,
                input.reg, input.synthetic_signal, 1U });
            continue;
          }
          direct_ready_bindings.push_back({
              llvm_detail::RegionDirectReadyBindingKind::input_slot,
              static_cast<std::uint32_t>(input_slot), input.source_signal,
              input.reg, input.synthetic_signal, input.width });
          ++input_slot;
        }
        for (const auto& input : prefix_inputs) {
          direct_ready_bindings.push_back({
              llvm_detail::RegionDirectReadyBindingKind::prefix_current,
              static_cast<std::uint32_t>(input_slot), input.source_signal,
              input.activation_register, input.synthetic_signal,
              input.width });
          ++input_slot;
        }
        if (direct_ready_bindings.size() != inputs.size()
                + prefix_inputs.size()) {
          direct_ready_bindings.clear();
        }
      }
    }

    auto canonical_kernel = kernel;
    if (!canonicalize_region_kernel_physical_ids(canonical_kernel)) {
      return reject_prepare(__LINE__);
    }
    const auto configuration_identity
        = region_native_body_configuration_identity(options);
    const auto template_key = region_prepared_template_cache_key(
        kernel, canonical_kernel, design_identity, configuration_identity,
        inputs, prefix_inputs, prefix_flags, spills, signal_widths,
        signal_value_kinds, prepared_output_signals,
        prepared_output_successor_masks,
        prepared_output_successor_masks_representable,
        direct_ready_bindings,
        frame_exports, prefix_comparison_supported);
    if (!template_key) {
      return reject_prepare(__LINE__);
    }
    auto& template_registry = region_prepared_template_registry();
    try {
      prepared_template = template_registry.entries.get_or_create(
          *template_key, [&]()
              -> std::shared_ptr<RegionKernelPreparedTemplate> {
            template_registry.template_builds.fetch_add(
                1U, std::memory_order_relaxed);
            auto candidate
                = std::make_shared<RegionKernelPreparedTemplate>();
            candidate->signal_widths = signal_widths;
            candidate->signal_value_kinds = signal_value_kinds;
            candidate->frame_exports = frame_exports;
            candidate->prefix_comparison_supported
                = prefix_comparison_supported;
            if (!build_synthetic_process(kernel, inputs, prefix_inputs,
                    prefix_flags, spills, spills_by_member, frame_exports,
                    candidate->synthetic)) {
              return nullptr;
            }
            template_registry.process_validations.fetch_add(
                1U, std::memory_order_relaxed);
            candidate->validated = llvm_detail::validate_process(
                candidate->synthetic, candidate->signal_widths,
                candidate->signal_value_kinds);
            for (std::size_t index = 0U;
                 index < kernel.program.operations.size(); ++index) {
              const auto operation
                  = kernel.program.operations.expanded(index);
              const auto* const binary
                  = runtime::simir::operation_get_if<Binary>(&operation);
              if (binary == nullptr
                  || binary->operation != BinaryOperator::add_unsigned) {
                continue;
              }
              const auto& widths = candidate->validated.register_widths;
              if (binary->lhs >= widths.size()
                  || binary->rhs >= widths.size()
                  || binary->destination >= widths.size()
                  || widths[binary->lhs] == 0U
                  || widths[binary->lhs] != widths[binary->rhs]
                  || widths[binary->lhs] != widths[binary->destination]) {
                return nullptr;
              }
            }
            return candidate;
          });
    } catch (const LlvmJitError& error) {
      return reject_prepare(__LINE__, error.what());
    }
    if (prepared_template == nullptr
        || prepared_template->frame_exports != frame_exports
        || prepared_template->prefix_comparison_supported
            != prefix_comparison_supported
        || prepared_template->signal_widths != signal_widths
        || prepared_template->signal_value_kinds != signal_value_kinds
        || prepared_template->validated.register_widths.size()
            != prepared_template->synthetic.register_count) {
      return reject_prepare(__LINE__);
    }
    const auto& synthetic = prepared_template->synthetic;
    const auto& validated = prepared_template->validated;
    const auto source_register_value_kinds
        = runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
            synthetic.register_value_kinds);
    activation_register_value_kinds.emplace(source_register_value_kinds);
    if (!prepared_output_signals.empty()
        && prepared_output_successor_masks.size()
            != prepared_output_signals.size()) {
      return reject_prepare(__LINE__);
    }
    const auto make_operation_identity = [&](const bool include_physical_ids) {
      CacheKeyBuilder operation_key;
      operation_key.add("region-kernel-operation-schema",
          include_physical_ids ? "v3" : "v4");
      operation_key.add("region-kernel-output-prefix-case-equal",
          prefix_comparison_supported ? "v1" : "off");
      operation_key.add("region-kernel-prepared-output-entry",
          prepared_output_signals.empty()
              ? "off" : "v2-successor-sidecar-v1");
      for (std::size_t index = 0U;
           index < prepared_output_signals.size(); ++index) {
        operation_key.add("prepared-output-successor-slot",
            std::to_string(index));
        operation_key.add("prepared-output-successor-signal",
            std::to_string(prepared_output_signals[index]));
        operation_key.add("prepared-output-successor-member-mask",
            std::to_string(prepared_output_successor_masks[index]));
      }
      operation_key.add("region-kernel-direct-ready-window-entry",
          direct_ready_bindings.empty() ? "off" : "v1");
      for (const auto& direct_binding : direct_ready_bindings) {
        operation_key.add("direct-ready-binding-kind",
            std::to_string(static_cast<std::uint32_t>(direct_binding.kind)));
        operation_key.add("direct-ready-binding-source",
            std::to_string(direct_binding.source_index));
        if (include_physical_ids) {
          operation_key.add("direct-ready-binding-signal",
              std::to_string(direct_binding.signal_id));
        }
        operation_key.add("direct-ready-binding-register",
            std::to_string(direct_binding.register_id));
        operation_key.add("direct-ready-binding-synthetic",
            std::to_string(direct_binding.synthetic_signal_id));
        operation_key.add("direct-ready-binding-width",
            std::to_string(direct_binding.width));
      }
      operation_key.add("region-kernel-frame-export",
          frame_exports ? "1" : "0");
      operation_key.add("region-kernel-specialization",
          use_constant_inputs
              ? "guarded-constant-inputs-v1" : "dynamic-v1");
      llvm_detail::add_operation_cache_keys(
          operation_key, synthetic, signal_widths);
      return operation_key.finish();
    };
    const auto mapping_operation_identity = make_operation_identity(true);
    const auto native_operation_identity = make_operation_identity(false);
    identity = region_cache_identity(kernel, design_identity, inputs, spills,
        frame_exports, mapping_operation_identity);
    append_text(identity, use_constant_inputs
        ? "guarded-constant-inputs-v1" : "dynamic-inputs-v1");
    append_text(identity, frame_exports
        ? "region-logic9-frame-register-exports-v1"
        : "region-logic4-flat-planes-v1");
    append_number(identity, direct_wide_signal_offsets.size());
    for (const auto offset : direct_wide_signal_offsets) {
      append_number(identity, offset);
    }

    native_code_identity = region_cache_identity(canonical_kernel,
        design_identity, inputs, spills, frame_exports,
        native_operation_identity, "fsim-region-cone-native-code-v2");
    append_text(native_code_identity, use_constant_inputs
        ? "guarded-constant-inputs-v1" : "dynamic-inputs-v1");
    append_text(native_code_identity, frame_exports
        ? "region-logic9-frame-register-exports-v1"
        : "region-logic4-flat-planes-v1");
    append_number(native_code_identity, signal_widths.size());
    for (std::size_t signal = 0U; signal < signal_widths.size(); ++signal) {
      append_number(native_code_identity, signal_widths[signal]);
      append_number(native_code_identity,
          static_cast<std::uint8_t>(signal_value_kinds[signal]));
    }
    append_number(native_code_identity, synthetic.register_count);
    append_number(native_code_identity, source_register_value_kinds.size());
    for (const auto kind : source_register_value_kinds) {
      append_number(native_code_identity, static_cast<std::uint8_t>(kind));
    }
    append_number(native_code_identity, synthetic.debug_locals.size());
    for (const auto& local : synthetic.debug_locals) {
      append_text(native_code_identity, local.name);
      append_text(native_code_identity, local.type_name);
      append_number(native_code_identity, local.register_id);
      append_number(native_code_identity, local.width);
      append_number(native_code_identity,
          static_cast<std::uint8_t>(local.value_kind));
    }
    append_text(native_code_identity,
        prepared_output_signals.empty()
            ? "prepared-output-successor-masks-off"
            : "prepared-output-successor-masks-v1");
    append_number(native_code_identity, prepared_output_signals.size());
    for (std::size_t index = 0U;
         index < prepared_output_signals.size(); ++index) {
      append_number(native_code_identity, prepared_output_signals[index]);
      append_number(native_code_identity,
          prepared_output_successor_masks[index]);
    }
    append_number(native_code_identity, direct_ready_bindings.size());
    for (const auto& direct_binding : direct_ready_bindings) {
      append_number(native_code_identity,
          static_cast<std::uint32_t>(direct_binding.kind));
      append_number(native_code_identity, direct_binding.source_index);
      append_number(native_code_identity, direct_binding.register_id);
      append_number(native_code_identity,
          direct_binding.synthetic_signal_id);
      append_number(native_code_identity, direct_binding.width);
    }
    append_number(native_code_identity, direct_wide_signal_offsets.size());
    for (const auto offset : direct_wide_signal_offsets) {
      append_number(native_code_identity, offset);
    }

    // Every current read is bound to the synthetic direct planes and every
    // Logic4 output is a required direct-update slot. When any Logic9 value is
    // present, persistent synthetic debug-local slots carry all member and
    // write-time snapshot registers instead. Callback stubs remain installed
    // only because ABI v2 validates the complete core service surface.
    if (!kernel_extracts_fit_source_registers(
            kernel, validated.register_widths)) {
      return reject_prepare(__LINE__);
    }
    known_logic4_variant_supported = !use_constant_inputs && !frame_exports
        && supports_known_logic4_variant(kernel, inputs, spills, signal_widths,
            signal_value_kinds,
            std::span<const std::uint32_t> { validated.register_widths }
                .first(kernel.program.register_count));
    append_text(native_code_identity, known_logic4_variant_supported
        ? "known-logic4-variant-v1" : "known-logic4-variant-off");
    const auto module_symbol = use_constant_inputs
        ? region_constant_inputs_module_symbol : region_module_symbol;
    CacheKeyBuilder body_key;
    body_key.add("canonical-native-code", native_code_identity);
    body_key.add("effective-jit-configuration", configuration_identity);
    auto compiled_body = region_native_body_registry().get_or_create(
        body_key.finish(), [this, module_symbol]()
            -> std::shared_ptr<RegionKernelNativeBody> {
          auto body = std::make_shared<RegionKernelNativeBody>(options);
          body->jit.set_immutable_design_identity(native_code_identity);
          if (!body->jit.supports_process(
                  prepared_template->synthetic, signal_widths,
                  signal_value_kinds)) {
            return nullptr;
          }
          if (prepared_output_signals.empty()) {
            body->jit.add_process(module_symbol, prepared_template->synthetic,
                signal_widths, signal_value_kinds,
                JitBackendTierHint::fused_masked_region,
                kernel.members.size());
          } else {
            body->jit.add_region_prepared_output_process(module_symbol,
                prepared_template->synthetic, signal_widths,
                signal_value_kinds,
                prepared_output_signals,
                JitBackendTierHint::fused_masked_region,
                kernel.members.size(), direct_ready_bindings,
                prepared_output_successor_masks);
          }
          const auto handle = body->jit.lookup(module_symbol);
          body->binding = body->jit.bind(handle);
          body->layout = body->jit.frame_layout(body->binding);
          if (known_logic4_variant_supported) {
            body->jit.add_region_known_logic4_process(
                region_known_logic4_module_symbol,
                prepared_template->synthetic,
                signal_widths, signal_value_kinds,
                JitBackendTierHint::fused_masked_region,
                kernel.members.size());
            const auto known_handle
                = body->jit.lookup(region_known_logic4_module_symbol);
            body->known_binding = body->jit.bind(known_handle);
            body->known_layout
                = body->jit.frame_layout(body->known_binding);
            body->known_variant_ready = true;
          }
          return body;
        });
    if (compiled_body == nullptr) {
      return reject_prepare(__LINE__);
    }
    native_body = std::move(compiled_body);
    binding = native_body->binding;
    layout = native_body->layout;
    known_binding = native_body->known_binding;
    known_layout = native_body->known_layout;
    known_variant_ready = native_body->known_variant_ready;
    const auto expected_direct_read_count = static_cast<std::size_t>(
        std::ranges::count_if(inputs, [](const InputBinding& input) {
            return !input.specialization_value.has_value();
        })) + prefix_inputs.size();
    if (layout.register_count != synthetic.register_count
        || layout.register_widths.size() != layout.register_count
        || layout.register_word_offsets.size() != layout.register_count
        || layout.register_values_persistent.size() != layout.register_count
        || layout.uses_logic9 != frame_exports
        || (frame_exports && !layout.tracks_register_initialization)
        || layout.direct_read_signals.size() != expected_direct_read_count
        || layout.direct_update_signals.size()
            != (frame_exports ? 0U : spills.size())
        || layout.direct_read_signals.size()
            > std::numeric_limits<std::uint32_t>::max()
        || layout.direct_update_signals.size()
            > std::numeric_limits<std::uint32_t>::max()
        || signal_widths.size() > std::numeric_limits<std::uint32_t>::max()) {
      return reject_prepare(__LINE__);
    }
    update_slot_by_signal.assign(signal_widths.size(),
        std::numeric_limits<std::uint32_t>::max());
    request_seen.assign(kernel.members.size(), 0U);

    std::size_t read_index { };
    for (const auto& input : inputs) {
      if (layout.register_widths[input.reg] != input.width) {
        return reject_prepare(__LINE__);
      }
      if (input.specialization_value) {
        continue;
      }
      if (read_index >= layout.direct_read_signals.size()
          || layout.direct_read_signals[read_index]
              != input.synthetic_signal) {
        return reject_prepare(__LINE__);
      }
      ++read_index;
    }
    for (const auto& input : prefix_inputs) {
      if (layout.register_widths[input.activation_register] != input.width
          || read_index >= layout.direct_read_signals.size()
          || layout.direct_read_signals[read_index] != input.synthetic_signal) {
        return reject_prepare(__LINE__);
      }
      ++read_index;
    }
    const auto no_slot = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> spill_index_by_signal(
        signal_widths.size(), no_slot);
    if (!frame_exports) {
      for (std::size_t spill_index = 0U;
           spill_index < spills.size(); ++spill_index) {
        const auto& spill = spills[spill_index];
        if (spill.synthetic_signal >= spill_index_by_signal.size()
            || spill.activation_register >= layout.register_widths.size()
            || spill_index >= no_slot
            || spill_index_by_signal[spill.synthetic_signal] != no_slot
            || signal_widths[spill.synthetic_signal] != spill.width
            || signal_value_kinds[spill.synthetic_signal] != spill.value_kind) {
          return reject_prepare(__LINE__);
        }
        spill_index_by_signal[spill.synthetic_signal]
            = static_cast<std::uint32_t>(spill_index);
      }
      for (std::size_t slot_index = 0U;
           slot_index < layout.direct_update_signals.size(); ++slot_index) {
        const auto signal = layout.direct_update_signals[slot_index];
        if (signal >= spill_index_by_signal.size()
            || spill_index_by_signal[signal] == no_slot
            || update_slot_by_signal[signal] != no_slot
            || slot_index >= no_slot) {
          return reject_prepare(__LINE__);
        }
        const auto& spill = spills[spill_index_by_signal[signal]];
        if (layout.register_widths[spill.activation_register] != spill.width) {
          return reject_prepare(__LINE__);
        }
        update_slot_by_signal[signal] = static_cast<std::uint32_t>(slot_index);
      }
      for (const auto& spill : spills) {
        if (spill.synthetic_signal >= update_slot_by_signal.size()
            || update_slot_by_signal[spill.synthetic_signal] == no_slot) {
          return reject_prepare(__LINE__);
        }
      }
    } else if (!layout.direct_update_signals.empty()) {
      return reject_prepare(__LINE__);
    }

    register_aval.resize(layout.register_word_count);
    register_bval.resize(layout.register_word_count);
    register_initialized.resize(layout.register_count);
    register_logic9_plane2.resize(layout.uses_logic9
            ? layout.register_word_count : 0U);
    register_logic9_plane3.resize(layout.uses_logic9
            ? layout.register_word_count : 0U);
    {
      const std::lock_guard lock { native_body->api_mutex };
      jit().initialize_frame(binding, frame, register_aval, register_bval,
          register_initialized, register_logic9_plane2,
          register_logic9_plane3);
    }

    register_values.assign(layout.register_count,
        PackedLogic4 { 1U, runtime::Logic4::x });
    for (std::size_t reg = 0U; reg < layout.register_count; ++reg) {
      const auto width = layout.register_widths[reg];
      if (width != 0U) {
        register_values[reg] = PackedLogic4 { width, runtime::Logic4::x };
        if (source_register_value_kinds[reg] == ValueKind::logic9) {
          register_values[reg].fill(runtime::Logic9::x);
        }
      }
    }
    for (const auto& input : inputs) {
      if (input.reg >= register_values.size()
          || register_values[input.reg].width() != input.width) {
        return reject_prepare(__LINE__);
      }
    }
    for (const auto& spill : spills) {
      if (spill.activation_register >= register_values.size()
          || register_values[spill.activation_register].width() != spill.width
          || register_values[spill.activation_register].is_logic9()
              != (spill.value_kind == ValueKind::logic9)) {
        return reject_prepare(__LINE__);
      }
    }
    register_export_mask.assign(layout.register_count, 0U);
    generated_output_changed.assign(kernel.outputs.size(), 0U);
    prefix_signal_seen.assign(kernel.internal_signals.size(), 0U);
    output_prefix_changed.reserve(kernel.outputs.size());
    if (prepared_pointer_spans.max_size() < 66U
        || prepared_output_signals.size()
            > (prepared_pointer_spans.max_size() - 66U) / 16U) {
      return reject_prepare(__LINE__);
    }
    auto pointer_span_capacity
        = 66U + prepared_output_signals.size() * 16U;
    const auto add_span_capacity = [&](const std::size_t count) {
      if (count > (prepared_pointer_spans.max_size()
              - pointer_span_capacity) / 6U) {
        return false;
      }
      pointer_span_capacity += count * 6U;
      return true;
    };
    if (!add_span_capacity(layout.register_count)
        || !add_span_capacity(kernel.internal_signals.size())) {
      return reject_prepare(__LINE__);
    }
    prepared_pointer_spans.reserve(pointer_span_capacity);
    if (frame_exports) {
      for (const auto& spill : spills) {
        const auto reg = spill.activation_register;
        if (reg >= register_export_mask.size()
            || register_export_mask[reg] != 0U
            || layout.register_values_persistent[reg] == 0U
            || source_register_value_kinds[reg] != spill.value_kind) {
          return reject_prepare(__LINE__);
        }
        register_export_mask[reg] = 1U;
      }
    }

    for (std::size_t index = 0U; index < signal_widths.size(); ++index) {
      if (!supported_value(signal_value_kinds[index], signal_widths[index])) {
        return reject_prepare(__LINE__);
      }
    }
    if (signal_widths.size() > direct_signal_aval.max_size()
        || signal_widths.size() > direct_signal_bval.max_size()
        || direct_wide_word_count > direct_wide_signal_aval.max_size()
        || direct_wide_word_count > direct_wide_signal_bval.max_size()) {
      return reject_prepare(__LINE__);
    }
    direct_signal_aval.assign(signal_widths.size(), 0U);
    direct_signal_bval.assign(signal_widths.size(), 0U);
    direct_wide_signal_aval.resize(direct_wide_word_count);
    direct_wide_signal_bval.resize(direct_wide_word_count);
    const bool has_narrow_logic9_signal = [&] {
      for (std::size_t signal = 0U; signal < signal_widths.size(); ++signal) {
        if (signal_value_kinds[signal] == ValueKind::logic9
            && signal_widths[signal] <= 64U) {
          return true;
        }
      }
      return false;
    }();
    const bool has_wide_logic9_signal = [&] {
      for (std::size_t signal = 0U; signal < signal_widths.size(); ++signal) {
        if (signal_value_kinds[signal] == ValueKind::logic9
            && signal_widths[signal] > 64U) {
          return true;
        }
      }
      return false;
    }();
    const auto narrow_logic9_size = has_narrow_logic9_signal
        ? signal_widths.size() : 0U;
    direct_signal_logic9_plane0.assign(narrow_logic9_size, 0U);
    direct_signal_logic9_plane1.assign(narrow_logic9_size, 0U);
    direct_signal_logic9_plane2.assign(narrow_logic9_size, 0U);
    direct_signal_logic9_plane3.assign(narrow_logic9_size, 0U);
    const auto wide_logic9_size = has_wide_logic9_signal
        ? direct_wide_word_count : 0U;
    direct_wide_signal_logic9_plane2.assign(wide_logic9_size, 0U);
    direct_wide_signal_logic9_plane3.assign(wide_logic9_size, 0U);
    direct_update_slots.resize(layout.direct_update_signals.size());
    active_words.resize(direct_update_slots.size() / 64U
        + (direct_update_slots.size() % 64U != 0U ? 1U : 0U));
    direct_update_wide_word_offsets.assign(direct_update_slots.size(),
        std::numeric_limits<std::size_t>::max());
    std::size_t direct_update_wide_word_count { };
    for (std::size_t slot_index = 0U;
         slot_index < layout.direct_update_signals.size(); ++slot_index) {
      const auto signal = layout.direct_update_signals[slot_index];
      if (signal >= signal_widths.size()) {
        return reject_prepare(__LINE__);
      }
      const auto width = signal_widths[signal];
      if (width <= 64U) {
        continue;
      }
      const auto words = word_count_for_width(width);
      if (words == 0U
          || words > std::numeric_limits<std::size_t>::max()
              - direct_update_wide_word_count) {
        return reject_prepare(__LINE__);
      }
      direct_update_wide_word_offsets[slot_index]
          = direct_update_wide_word_count;
      direct_update_wide_word_count += words;
    }
    if (direct_update_wide_word_count
            > direct_update_wide_aval.max_size()
        || direct_update_wide_word_count
            > direct_update_wide_bval.max_size()
        || direct_update_wide_word_count
            > direct_update_wide_mask.max_size()) {
      return reject_prepare(__LINE__);
    }
    direct_update_wide_aval.resize(direct_update_wide_word_count);
    direct_update_wide_bval.resize(direct_update_wide_word_count);
    direct_update_wide_mask.resize(direct_update_wide_word_count);
    reset_direct_updates();

    runtime.abi_version = FSIM_JIT_RUNTIME_ABI_VERSION_V2;
    runtime.struct_size = sizeof(runtime);
    runtime.services = &region_kernel_services();
    runtime.context = &callback_state;
    runtime.direct_update_slots = direct_update_slots.data();
    runtime.direct_update_slot_count
        = static_cast<std::uint32_t>(direct_update_slots.size());
    runtime.direct_signal_aval = direct_signal_aval.data();
    runtime.direct_signal_bval = direct_signal_bval.data();
    runtime.direct_signal_logic9_plane0
        = direct_signal_logic9_plane0.data();
    runtime.direct_signal_logic9_plane1
        = direct_signal_logic9_plane1.data();
    runtime.direct_signal_logic9_plane2
        = direct_signal_logic9_plane2.data();
    runtime.direct_signal_logic9_plane3
        = direct_signal_logic9_plane3.data();
    runtime.direct_read_signals = layout.direct_read_signals.data();
    runtime.direct_read_signal_count
        = static_cast<std::uint32_t>(layout.direct_read_signals.size());
    runtime.direct_signal_count
        = static_cast<std::uint32_t>(signal_widths.size());
    runtime.direct_wide_signal_aval = direct_wide_signal_aval.data();
    runtime.direct_wide_signal_bval = direct_wide_signal_bval.data();
    runtime.direct_wide_signal_logic9_plane2
        = direct_wide_signal_logic9_plane2.data();
    runtime.direct_wide_signal_logic9_plane3
        = direct_wide_signal_logic9_plane3.data();
    runtime.direct_wide_signal_offsets = direct_wide_signal_offsets.data();
    runtime.direct_wide_signal_offset_count
        = static_cast<std::uint32_t>(direct_wide_signal_offsets.size());
    runtime.direct_wide_word_count = direct_wide_word_count;
    runtime.direct_update_active_words = active_words.data();
    runtime.direct_update_active_word_count
        = static_cast<std::uint32_t>(active_words.size());

    result.abi_version = FSIM_JIT_RESUME_RESULT_ABI_VERSION_V2;
    result.struct_size = sizeof(result);
    return true;
  }

  void prepare_known_logic4_frame()
  {
    if (!same_layout_shape(layout, known_layout)) {
      throw LlvmJitError(
          "known Logic4 region entry has a different frame shape");
    }

    known_runtime = runtime;
    known_runtime.direct_read_signals
        = known_layout.direct_read_signals.data();
    known_runtime.direct_read_signal_count
        = static_cast<std::uint32_t>(known_layout.direct_read_signals.size());
    {
      const std::lock_guard lock { native_body->api_mutex };
      jit().initialize_frame(known_binding, known_frame, register_aval,
          register_bval, register_initialized, register_logic9_plane2,
          register_logic9_plane3);
    }
    known_result.abi_version = FSIM_JIT_RESUME_RESULT_ABI_VERSION_V2;
    known_result.struct_size = sizeof(known_result);
    known_variant_ready = true;
  }

  [[nodiscard]] llvm_detail::RegionKernelActivationBacking
  activation_backing(const JitProcessFrameLayout& selected_layout) noexcept
  {
    const auto& register_value_kinds
        = *activation_register_value_kinds;
    return {
        &region_kernel_services(),
        &callback_state,
        std::span<const std::uint32_t> { signal_widths },
        std::span<const ValueKind> { signal_value_kinds },
        std::span<const std::uint32_t> {
            selected_layout.direct_read_signals },
        std::span<const std::uint32_t> {
            selected_layout.direct_update_signals },
        std::span<const std::uint32_t> { selected_layout.register_widths },
        std::span<const std::uint32_t> {
            selected_layout.register_word_offsets },
        std::span<std::uint64_t> { direct_signal_aval },
        std::span<std::uint64_t> { direct_signal_bval },
        std::span<std::uint64_t> { direct_signal_logic9_plane0 },
        std::span<std::uint64_t> { direct_signal_logic9_plane1 },
        std::span<std::uint64_t> { direct_signal_logic9_plane2 },
        std::span<std::uint64_t> { direct_signal_logic9_plane3 },
        std::span<const std::uint32_t> { direct_wide_signal_offsets },
        std::span<std::uint64_t> { direct_wide_signal_aval },
        std::span<std::uint64_t> { direct_wide_signal_bval },
        std::span<std::uint64_t> { direct_wide_signal_logic9_plane2 },
        std::span<std::uint64_t> { direct_wide_signal_logic9_plane3 },
        std::span<std::uint64_t> { register_aval },
        std::span<std::uint64_t> { register_bval },
        std::span<std::uint64_t> { register_logic9_plane2 },
        std::span<std::uint64_t> { register_logic9_plane3 },
        std::span<std::uint8_t> { register_initialized },
        std::span<const ValueKind> {
            register_value_kinds.data(), register_value_kinds.size() },
        std::span<const std::uint8_t> {
            selected_layout.register_values_persistent },
        std::span<const std::uint8_t> { register_export_mask },
        std::span<fsim_jit_update_slot_v2> { direct_update_slots },
        std::span<const std::size_t> { direct_update_wide_word_offsets },
        std::span<std::uint64_t> { direct_update_wide_aval },
        std::span<std::uint64_t> { direct_update_wide_bval },
        std::span<std::uint64_t> { direct_update_wide_mask },
        std::span<std::uint64_t> { active_words },
    };
  }

  void reset_direct_updates() noexcept
  {
    std::fill(active_words.begin(), active_words.end(), UINT64_C(0));
    std::fill(direct_update_wide_aval.begin(),
        direct_update_wide_aval.end(), UINT64_C(0));
    std::fill(direct_update_wide_bval.begin(),
        direct_update_wide_bval.end(), UINT64_C(0));
    std::fill(direct_update_wide_mask.begin(),
        direct_update_wide_mask.end(), UINT64_C(0));
    for (std::size_t index = 0U; index < direct_update_slots.size(); ++index) {
      auto& slot = direct_update_slots[index];
      slot = { };
      const auto width = signal_widths[layout.direct_update_signals[index]];
      slot.width = width;
      slot.word_count = word_count_for_width(width);
      if (width > 64U) {
        const auto offset = direct_update_wide_word_offsets[index];
        slot.wide_aval = direct_update_wide_aval.data() + offset;
        slot.wide_bval = direct_update_wide_bval.data() + offset;
        slot.wide_mask = direct_update_wide_mask.data() + offset;
      }
      // reserved bit zero is the generated Logic4 slot's shadow-valid flag.
      // Clear it each activation so identical values are still published.
      slot.reserved = 0U;
    }
  }

  [[nodiscard]] std::size_t active_member_index(
      const runtime::simir::ProcessId process) const noexcept
  {
    const auto member = std::ranges::lower_bound(kernel.members, process,
        std::ranges::less { }, &RegionConeKernelMember::process);
    return member == kernel.members.end() || member->process != process
        ? kernel.members.size()
        : static_cast<std::size_t>(member - kernel.members.begin());
  }

  [[nodiscard]] bool validate_image(
      const runtime::simir::RegionKernelActivationImage& image,
      const bool direct_ready_window = false) noexcept
  {
    const auto& prefix = image.scheduler_prefix;
    if (image.generation == 0U || prefix.frontier_generation == 0U
        || prefix.phase != runtime::SchedulerPhase::active
        || prefix.process_domain != kernel.program.scheduling_domain
        || (prefix.process_domain
                == runtime::simir::ProcessSchedulingDomain::generic
            && prefix.systemverilog_round != 0U)
        || prefix.frontier_cursor >= prefix.frontier_end
        || prefix.tasks.empty()
        || prefix.tasks.size() > prefix.frontier_end - prefix.frontier_cursor
        || image.requests.size() != image.ready_processes.size()
        || image.requests.size() != prefix.tasks.size()
        || image.requests.size() != image.active_member_indices.size()
        || (!kernel.member_execution_order.empty()
            && image.active_member_indices.size() != kernel.members.size())
        || (direct_ready_window
            ? !image.register_inputs.empty()
            : image.register_inputs.size() != inputs.size())
        || request_seen.size() != kernel.members.size()) {
      return false;
    }
    for (std::size_t index = 0U; index < image.active_member_indices.size();
         ++index) {
      if (image.active_member_indices[index] >= kernel.members.size()
          || (index != 0U
              && image.active_member_indices[index - 1U]
                  >= image.active_member_indices[index])) {
        return false;
      }
      request_seen[image.active_member_indices[index]] = 0U;
    }

    std::optional<std::tuple<runtime::StableOrder, std::uint64_t>>
        previous_key;
    for (std::size_t index = 0U; index < image.requests.size(); ++index) {
      const auto& request = image.requests[index];
      const auto& task = prefix.tasks[index];
      if (request.process != image.ready_processes[index]
          || task.member != request
          || task.task_ordinal != prefix.frontier_cursor + index
          || request.origin.process_domain
              != prefix.process_domain
          || request.origin.phase != runtime::SchedulerPhase::active
          || request.origin.time != prefix.time
          || request.origin.delta != prefix.delta
          || request.origin.phase != prefix.phase
          || request.origin.systemverilog_round
              != prefix.systemverilog_round
          || (prefix.process_domain
                  == runtime::simir::ProcessSchedulingDomain::generic
              && request.origin.systemverilog_round != 0U)
          || request.trigger_mask == 0U) {
        return false;
      }
      const auto key = std::tuple {
          request.origin.stable_order, request.origin.sequence };
      if (previous_key && !(*previous_key < key)) {
        return false;
      }
      previous_key = key;
      const auto member_index = active_member_index(request.process);
      if (member_index == kernel.members.size()
          || !is_active_member(image, member_index)
          || request_seen[member_index] != 0U) {
        return false;
      }
      request_seen[member_index] = 1U;
    }
    for (const auto member_index : image.active_member_indices) {
      if (request_seen[member_index] == 0U) {
        return false;
      }
    }

    if (direct_ready_window) {
      return !frame_exports && specialization
              == RegionKernelSpecialization::dynamic_inputs
          && !direct_ready_bindings.empty();
    }

    for (std::size_t index = 0U; index < inputs.size(); ++index) {
      const auto& expected = inputs[index];
      const auto& actual = image.register_inputs[index];
      if (actual.register_id != expected.reg
          || actual.value.width() != expected.width
          || actual.value.is_logic9()
              != (expected.value_kind == ValueKind::logic9)) {
        return false;
      }
      const auto word_count = word_count_for_width(expected.width);
      if (word_count == 0U) {
        return false;
      }
      if (expected.value_kind == ValueKind::logic9) {
        for (std::size_t plane = 0U; plane < 4U; ++plane) {
          const auto words = actual.value.logic9_plane_words(plane);
          if (words.size() != word_count) {
            return false;
          }
          for (std::size_t word = 0U; word < words.size(); ++word) {
            if (plane == 3U) {
              const auto p0 = actual.value.logic9_plane_words(0U)[word];
              const auto p1 = actual.value.logic9_plane_words(1U)[word];
              const auto p2 = actual.value.logic9_plane_words(2U)[word];
              if ((words[word] & (p2 | p1 | p0)) != 0U) {
                return false;
              }
            }
            if (word + 1U == words.size() && expected.width % 64U != 0U) {
              const auto valid_mask = word_mask_for_width(
                  expected.width, static_cast<std::uint32_t>(word));
              if ((words[word] & ~valid_mask) != 0U) {
                return false;
              }
            }
          }
        }
      } else if (actual.value.aval_words().size() != word_count
          || actual.value.bval_words().size() != word_count) {
        return false;
      }
      if (expected.width > 64U) {
        const auto signal = expected.synthetic_signal;
        if (signal >= direct_wide_signal_offsets.size()) {
          return false;
        }
        const auto offset = direct_wide_signal_offsets[signal];
        if (offset > direct_wide_signal_aval.size()
            || word_count > direct_wide_signal_aval.size() - offset
            || offset > direct_wide_signal_bval.size()
            || word_count > direct_wide_signal_bval.size() - offset) {
          return false;
        }
      }
      if (expected.readiness) {
        const auto word = actual.value.low_word();
        const bool ready = word.aval == 1U && word.bval == 0U;
        const bool not_ready = word.aval == 0U && word.bval == 0U;
        if (!ready && !not_ready) {
          return false;
        }
        if (ready != is_active_member(image, expected.member_index)) {
          return false;
        }
      }
    }
    return true;
  }

  [[nodiscard]] bool validate_internal_output_prefix(
      const runtime::simir::RegionKernelActivationImage& image,
      const std::span<const PackedLogic4> current_internal_values,
      const std::span<const runtime::simir::RegionConeOutputBinding>
          ordered_prefix) noexcept
  {
    if (!prefix_comparison_supported || ordered_prefix.empty()
        || current_internal_values.size() != prefix_inputs.size()
        || !validate_image(image)) {
      return false;
    }
    for (std::size_t index = 0U; index < prefix_inputs.size(); ++index) {
      const auto& input = prefix_inputs[index];
      const auto& value = current_internal_values[index];
      if (input.source_signal != kernel.internal_signals[index]
          || value.width() != input.width || value.is_logic9()) {
        return false;
      }
    }
    for (std::size_t input_index = 0U;
         input_index < inputs.size(); ++input_index) {
      const auto& input = inputs[input_index];
      if (input.readiness) {
        continue;
      }
      const auto internal = std::ranges::lower_bound(
          kernel.internal_signals, input.source_signal);
      const bool is_internal = internal != kernel.internal_signals.end()
          && *internal == input.source_signal;
      if (input.internal != is_internal) {
        return false;
      }
      if (!is_internal) {
        continue;
      }
      const auto internal_index = static_cast<std::size_t>(
          internal - kernel.internal_signals.begin());
      if (internal_index >= current_internal_values.size()
          || current_internal_values[internal_index]
              != image.register_inputs[input_index].value) {
        return false;
      }
    }

    std::ranges::fill(prefix_signal_seen, 0U);
    std::size_t prefix_index { };
    for (const auto& request : image.requests) {
      for (const auto& output : kernel.outputs) {
        if (output.owner != request.process) {
          continue;
        }
        if (prefix_index == ordered_prefix.size()) {
          return true;
        }
        const auto internal = std::ranges::lower_bound(
            kernel.internal_signals, output.signal);
        if (internal == kernel.internal_signals.end()
            || *internal != output.signal
            || ordered_prefix[prefix_index] != output
            || output.value_kind != ValueKind::logic4
            || output.width == 0U || output.width > 64U) {
          return false;
        }
        const auto internal_index = static_cast<std::size_t>(
            internal - kernel.internal_signals.begin());
        if (internal_index >= prefix_signal_seen.size()
            || prefix_signal_seen[internal_index] != 0U) {
          return false;
        }
        prefix_signal_seen[internal_index] = 1U;
        ++prefix_index;
      }
    }
    return prefix_index == ordered_prefix.size();
  }

  [[nodiscard]] bool validate_direct_ready_window(
      const runtime::simir::RegionKernelActivationImage& image,
      const runtime::simir::RegionDirectReadyWindowV1& window,
      const std::span<const PackedLogic4> current_internal_values,
      const std::span<const runtime::simir::RegionConeOutputBinding>
          ordered_prefix,
      const RegionPreparedOutputBatchV1& outputs,
      const runtime::simir::RegionPreparedOutputSuccessorMasksV1*
          successors = nullptr) noexcept
  {
    using BindingKind = llvm_detail::RegionDirectReadyBindingKind;
    using InputSlot = runtime::simir::RegionDirectReadyInputSlotV1;
    if (direct_ready_bindings.empty()
        || !validate_image(image, true)
        || window.abi_version
            != runtime::simir::kRegionDirectReadyWindowAbiVersionV1
        || window.struct_size != sizeof(window)
        || window.activation_generation != image.generation
        || window.frontier_generation
            != image.scheduler_prefix.frontier_generation
        || window.member_count != kernel.members.size()
        || window.readiness_word_count
            != (kernel.members.size() / 64U
                + (kernel.members.size() % 64U != 0U ? 1U : 0U))
        || window.reserved != 0U || window.readiness_mask == nullptr
        || window.input_slots == nullptr
        || reinterpret_cast<std::uintptr_t>(window.readiness_mask)
            % alignof(std::uint64_t) != 0U
        || reinterpret_cast<std::uintptr_t>(window.input_slots)
            % alignof(InputSlot) != 0U
        || current_internal_values.size() != prefix_inputs.size()
        || current_internal_values.size() != kernel.internal_signals.size()
        || ordered_prefix.empty() || !prefix_comparison_supported) {
      return false;
    }

    std::size_t expected_slot_count { };
    std::size_t ready_binding_count { };
    for (const auto& direct_binding : direct_ready_bindings) {
      if (direct_binding.kind == BindingKind::member_ready) {
        ++ready_binding_count;
      } else if (direct_binding.kind == BindingKind::input_slot
          || direct_binding.kind == BindingKind::prefix_current) {
        ++expected_slot_count;
      } else {
        return false;
      }
    }
    if (ready_binding_count != kernel.members.size()
        || expected_slot_count != window.input_slot_count) {
      return false;
    }

    const auto valid_span_extent = [](
        const void* const pointer,
        const std::size_t count,
        const std::size_t element_size) noexcept {
      const auto address
          = reinterpret_cast<std::uintptr_t>(pointer);
      const auto maximum = std::numeric_limits<std::uintptr_t>::max();
      if (element_size == 0U
          || count > std::numeric_limits<std::size_t>::max() / element_size) {
        return false;
      }
      const auto bytes = count * element_size;
      return address <= maximum - bytes;
    };
    if (!valid_span_extent(window.input_slots, window.input_slot_count,
            sizeof(InputSlot))
        || !valid_span_extent(window.readiness_mask,
            window.readiness_word_count, sizeof(std::uint64_t))) {
      return false;
    }

    const std::span<const std::uint64_t> readiness_words {
        window.readiness_mask, window.readiness_word_count };
    if (!kernel.members.empty() && readiness_words.empty()) {
      return false;
    }
    if (kernel.members.size() % 64U != 0U
        && (readiness_words.back()
            & (~UINT64_C(0) << (kernel.members.size() % 64U))) != 0U) {
      return false;
    }
    for (std::size_t member = 0U; member < kernel.members.size(); ++member) {
      const bool expected_ready = is_active_member(image, member);
      const bool actual_ready = (readiness_words[member / 64U]
          & (UINT64_C(1) << (member % 64U))) != 0U;
      if (actual_ready != expected_ready) {
        return false;
      }
      const auto ready_binding = std::ranges::find_if(
          direct_ready_bindings, [member](const auto& direct_binding) {
              return direct_binding.kind == BindingKind::member_ready
                  && direct_binding.source_index == member;
          });
      if (ready_binding == direct_ready_bindings.end()
          || ready_binding->register_id
              != kernel.members[member].readiness_register
          || ready_binding->width != 1U
          || ready_binding->synthetic_signal_id >= signal_widths.size()
          || signal_widths[ready_binding->synthetic_signal_id] != 1U) {
        return false;
      }
    }

    for (const auto& direct_binding : direct_ready_bindings) {
      if (direct_binding.kind == BindingKind::member_ready) {
        continue;
      }
      if (direct_binding.source_index >= window.input_slot_count) {
        return false;
      }
      const auto duplicate_count = std::ranges::count_if(
          direct_ready_bindings, [&direct_binding](const auto& candidate) {
              return candidate.kind != BindingKind::member_ready
                  && candidate.source_index == direct_binding.source_index;
          });
      if (duplicate_count != 1U) {
        return false;
      }
      const auto& slot = window.input_slots[direct_binding.source_index];
      if (slot.struct_size != sizeof(InputSlot)
          || slot.signal_id != direct_binding.signal_id
          || slot.register_id != direct_binding.register_id
          || slot.width != direct_binding.width || slot.word_count != 1U
          || slot.reserved != 0U || slot.aval == nullptr || slot.bval == nullptr
          || reinterpret_cast<std::uintptr_t>(slot.aval)
              % alignof(std::uint64_t) != 0U
          || reinterpret_cast<std::uintptr_t>(slot.bval)
              % alignof(std::uint64_t) != 0U
          || !valid_span_extent(slot.aval, 1U, sizeof(std::uint64_t))
          || !valid_span_extent(slot.bval, 1U, sizeof(std::uint64_t))
          || direct_binding.synthetic_signal_id >= signal_widths.size()
          || direct_binding.synthetic_signal_id >= signal_value_kinds.size()
          || signal_widths[direct_binding.synthetic_signal_id] != direct_binding.width
          || signal_value_kinds[direct_binding.synthetic_signal_id]
              != ValueKind::logic4) {
        return false;
      }
      if (direct_binding.width < 64U) {
        const auto valid_mask
            = (UINT64_C(1) << direct_binding.width) - 1U;
        if (((*slot.aval | *slot.bval) & ~valid_mask) != 0U) {
          return false;
        }
      }

      const auto internal = std::ranges::lower_bound(
          kernel.internal_signals, direct_binding.signal_id);
      if (internal != kernel.internal_signals.end()
          && *internal == direct_binding.signal_id) {
        const auto internal_index = static_cast<std::size_t>(
            internal - kernel.internal_signals.begin());
        const auto& current = current_internal_values[internal_index];
        if (current.width() != direct_binding.width || current.width() == 0U
            || current.width() > 64U || current.is_logic9()) {
          return false;
        }
        const auto word = current.unchecked_low_word();
        if (*slot.aval != word.aval || *slot.bval != word.bval) {
          return false;
        }
      } else if (direct_binding.kind == BindingKind::prefix_current) {
        return false;
      }
    }

    for (std::size_t index = 0U; index < prefix_inputs.size(); ++index) {
      const auto& input = prefix_inputs[index];
      const auto& value = current_internal_values[index];
      if (input.source_signal != kernel.internal_signals[index]
          || value.width() != input.width || value.is_logic9()) {
        return false;
      }
    }

    std::ranges::fill(prefix_signal_seen, 0U);
    std::size_t prefix_index { };
    for (const auto& request : image.requests) {
      for (const auto& output : kernel.outputs) {
        if (output.owner != request.process) {
          continue;
        }
        if (prefix_index == ordered_prefix.size()) {
          return validate_prepared_output_batch(
              image, outputs, current_internal_values, &window, successors);
        }
        const auto internal = std::ranges::lower_bound(
            kernel.internal_signals, output.signal);
        if (internal == kernel.internal_signals.end()
            || *internal != output.signal
            || ordered_prefix[prefix_index] != output
            || output.value_kind != ValueKind::logic4
            || output.width == 0U || output.width > 64U) {
          return false;
        }
        const auto internal_index = static_cast<std::size_t>(
            internal - kernel.internal_signals.begin());
        if (internal_index >= prefix_signal_seen.size()
            || prefix_signal_seen[internal_index] != 0U) {
          return false;
        }
        prefix_signal_seen[internal_index] = 1U;
        ++prefix_index;
      }
    }
    return prefix_index == ordered_prefix.size()
        && validate_prepared_output_batch(
            image, outputs, current_internal_values, &window, successors);
  }

  [[nodiscard]] bool validate_prepared_output_batch(
      const runtime::simir::RegionKernelActivationImage& image,
      const RegionPreparedOutputBatchV1& batch,
      const std::span<const PackedLogic4> current_internal_values,
      const runtime::simir::RegionDirectReadyWindowV1* const
          direct_window = nullptr,
      const runtime::simir::RegionPreparedOutputSuccessorMasksV1* const
          successors = nullptr) noexcept
  {
    using Slot = RegionPreparedOutputSlotV1;
    if (prepared_output_signals.empty()
        || batch.abi_version
            != runtime::simir::kRegionPreparedOutputBatchAbiVersionV1
        || batch.struct_size != sizeof(RegionPreparedOutputBatchV1)
        || batch.slot_count != kernel.internal_signals.size()
        || batch.reserved != 0U || batch.slots == nullptr
        || reinterpret_cast<std::uintptr_t>(batch.slots) % alignof(Slot) != 0U
        || current_internal_values.size() != kernel.internal_signals.size()
        || prefix_signal_seen.size() != kernel.internal_signals.size()) {
      return false;
    }
    if (successors != nullptr
        && reinterpret_cast<std::uintptr_t>(successors)
            % alignof(runtime::simir::
                RegionPreparedOutputSuccessorMasksV1) != 0U) {
      return false;
    }
    std::size_t successor_mask_bytes { };
    if (successors != nullptr) {
      const std::size_t successor_count = successors->slot_count;
      if (successors->abi_version
              != runtime::simir::
                  kRegionPreparedOutputSuccessorMasksAbiVersionV1
          || successors->struct_size != sizeof(*successors)
          || successors->slot_count != batch.slot_count
          || successors->reserved != 0U
          || successors->member_masks == nullptr
          || reinterpret_cast<std::uintptr_t>(successors->member_masks)
                  % alignof(std::uint64_t) != 0U
          || successor_count
              > std::numeric_limits<std::size_t>::max()
                  / sizeof(std::uint64_t)) {
        return false;
      }
      successor_mask_bytes = successor_count * sizeof(std::uint64_t);
      const auto masks_begin = reinterpret_cast<std::uintptr_t>(
          successors->member_masks);
      if (masks_begin
              > std::numeric_limits<std::uintptr_t>::max()
                  - successor_mask_bytes) {
        return false;
      }
    }
    prepared_pointer_spans.clear();
    const auto add_span = [&](const void* const pointer,
                              const std::size_t size,
                              const bool writable) {
      if (pointer == nullptr || size == 0U
          || prepared_pointer_spans.size()
              == prepared_pointer_spans.capacity()) {
        return false;
      }
      const auto begin = reinterpret_cast<std::uintptr_t>(pointer);
      const auto maximum = std::numeric_limits<std::uintptr_t>::max();
      if (begin > maximum - size) {
        return false;
      }
      prepared_pointer_spans.push_back(
          { begin, begin + size, writable });
      return true;
    };
    const auto add_vector_span = [&](const auto& values,
                                     const bool writable = false) {
      return values.empty()
          || (values.size()
                  <= std::numeric_limits<std::size_t>::max()
                      / sizeof(values.front())
              && add_span(values.data(),
                  values.size() * sizeof(values.front()), writable));
    };
    const auto add_packed_value_spans = [&](const PackedLogic4& value) {
      const auto add_words = [&](const std::span<const std::uint64_t> words) {
        return words.empty()
            || (words.size()
                    <= std::numeric_limits<std::size_t>::max()
                        / sizeof(std::uint64_t)
                && add_span(words.data(),
                    words.size() * sizeof(std::uint64_t), false));
      };
      if (!add_words(value.aval_words()) || !add_words(value.bval_words())) {
        return false;
      }
      return !value.is_logic9()
          || (add_words(value.logic9_plane_words(2U))
              && add_words(value.logic9_plane_words(3U)));
    };
    if (!add_span(&batch, sizeof(batch), false)
        || static_cast<std::size_t>(batch.slot_count)
            > std::numeric_limits<std::size_t>::max() / sizeof(Slot)
        || !add_span(batch.slots,
            static_cast<std::size_t>(batch.slot_count) * sizeof(Slot), false)
        || !add_span(&image, sizeof(image), false)
        || !add_span(&runtime, sizeof(runtime), false)
        || !add_span(&frame, sizeof(frame), false)
        || !add_span(&result, sizeof(result), false)
        || !add_vector_span(image.scheduler_prefix.tasks)
        || !add_vector_span(image.ready_processes)
        || !add_vector_span(image.requests)
        || !add_vector_span(image.active_member_indices)
        || (!direct_window && !add_vector_span(image.register_inputs))
        || !add_vector_span(current_internal_values)
        || !add_vector_span(register_values)
        || !add_vector_span(direct_update_slots)
        || !add_vector_span(active_words)
        || !add_vector_span(register_aval)
        || !add_vector_span(register_bval)
        || !add_vector_span(register_logic9_plane2)
        || !add_vector_span(register_logic9_plane3)
        || !add_vector_span(register_initialized)
        || !add_vector_span(direct_signal_aval, direct_window != nullptr)
        || !add_vector_span(direct_signal_bval, direct_window != nullptr)
        || !add_vector_span(direct_signal_logic9_plane0)
        || !add_vector_span(direct_signal_logic9_plane1)
        || !add_vector_span(direct_signal_logic9_plane2)
        || !add_vector_span(direct_signal_logic9_plane3)
        || !add_vector_span(direct_wide_signal_aval)
        || !add_vector_span(direct_wide_signal_bval)
        || !add_vector_span(direct_wide_signal_logic9_plane2)
        || !add_vector_span(direct_wide_signal_logic9_plane3)
        || !add_vector_span(direct_update_wide_aval)
        || !add_vector_span(direct_update_wide_bval)
        || !add_vector_span(direct_update_wide_mask)
        || !add_vector_span(prefix_signal_seen)
        || !add_vector_span(generated_output_changed)
        || !add_vector_span(output_prefix_changed)
        || (successors != nullptr
            && (!add_span(successors, sizeof(*successors), false)
                || !add_span(successors->member_masks,
                    successor_mask_bytes, true)))
        || (direct_window != nullptr
            && (!add_span(direct_window, sizeof(*direct_window), false)
                || static_cast<std::size_t>(
                       direct_window->input_slot_count)
                    > std::numeric_limits<std::size_t>::max()
                        / sizeof(runtime::simir::RegionDirectReadyInputSlotV1)
                || !add_span(direct_window->input_slots,
                    static_cast<std::size_t>(direct_window->input_slot_count)
                        * sizeof(runtime::simir::RegionDirectReadyInputSlotV1),
                    false)
                || static_cast<std::size_t>(
                       direct_window->readiness_word_count)
                    > std::numeric_limits<std::size_t>::max()
                        / sizeof(std::uint64_t)
                || !add_span(direct_window->readiness_mask,
                    static_cast<std::size_t>(
                        direct_window->readiness_word_count)
                        * sizeof(std::uint64_t), false)))) {
      return false;
    }
    if (direct_window != nullptr) {
      for (std::size_t index = 0U;
           index < direct_window->input_slot_count; ++index) {
        const auto& slot = direct_window->input_slots[index];
        if (reinterpret_cast<std::uintptr_t>(slot.aval)
                % alignof(std::uint64_t) != 0U
            || reinterpret_cast<std::uintptr_t>(slot.bval)
                % alignof(std::uint64_t) != 0U
            || !add_span(slot.aval, sizeof(std::uint64_t), false)
            || !add_span(slot.bval, sizeof(std::uint64_t), false)) {
          return false;
        }
      }
    } else {
      for (const auto& input : image.register_inputs) {
        if (!add_packed_value_spans(input.value)) {
          return false;
        }
      }
    }
    for (const auto& value : current_internal_values) {
      if (!add_packed_value_spans(value)) {
        return false;
      }
    }
    for (const auto& value : register_values) {
      if (!add_packed_value_spans(value)) {
        return false;
      }
    }

    const auto is_all_null = [](const Slot& slot) {
      return slot.owner_mask == nullptr
          && slot.old_current_aval == nullptr
          && slot.old_current_bval == nullptr
          && slot.old_owner_aval == nullptr
          && slot.old_owner_bval == nullptr
          && slot.next_current_aval == nullptr
          && slot.next_current_bval == nullptr
          && slot.next_last_aval == nullptr
          && slot.next_last_bval == nullptr
          && slot.next_stored_aval == nullptr
          && slot.next_stored_bval == nullptr
          && slot.next_owner_aval == nullptr
          && slot.next_owner_bval == nullptr
          && slot.changed == nullptr
          && slot.value_ready == nullptr
          && slot.transaction_ready == nullptr;
    };
    for (std::size_t index = 0U; index < batch.slot_count; ++index) {
      const auto& slot = batch.slots[index];
      const auto signal = kernel.internal_signals[index];
      const auto output = std::ranges::find(kernel.outputs, signal,
          &runtime::simir::RegionConeOutputBinding::signal);
      if (output == kernel.outputs.end()
          || index >= current_internal_values.size()
          || slot.struct_size != sizeof(Slot)
          || slot.signal_id != signal || slot.owner_id != output->owner
          || slot.width != output->width || slot.word_count != 1U
          || slot.value_kind
              != runtime::simir::RegionPreparedOutputValueKindV1::logic4
          || slot.selected > 1U || slot.reserved != 0U
          || slot.selected != (prefix_signal_seen[index] != 0U ? 1U : 0U)) {
        return false;
      }
      if (slot.selected == 0U) {
        if (!is_all_null(slot)) {
          return false;
        }
        continue;
      }
      const auto& current = current_internal_values[index];
      if (current.width() != slot.width || current.is_logic9()
          || slot.owner_mask == nullptr
          || slot.old_current_aval == nullptr
          || slot.old_current_bval == nullptr
          || slot.old_owner_aval == nullptr
          || slot.old_owner_bval == nullptr
          || slot.next_current_aval == nullptr
          || slot.next_current_bval == nullptr
          || slot.next_last_aval == nullptr
          || slot.next_last_bval == nullptr
          || slot.next_stored_aval == nullptr
          || slot.next_stored_bval == nullptr
          || slot.next_owner_aval == nullptr
          || slot.next_owner_bval == nullptr
          || slot.changed == nullptr || slot.value_ready == nullptr
          || slot.transaction_ready == nullptr) {
        return false;
      }
      if (slot.width == 0U || slot.width > 64U) {
        return false;
      }
      const std::array read_words {
          slot.owner_mask,
          slot.old_current_aval,
          slot.old_current_bval,
          slot.old_owner_aval,
          slot.old_owner_bval,
      };
      const std::array write_words {
          slot.next_current_aval,
          slot.next_current_bval,
          slot.next_last_aval,
          slot.next_last_bval,
          slot.next_stored_aval,
          slot.next_stored_bval,
          slot.next_owner_aval,
          slot.next_owner_bval,
      };
      const std::array write_bytes {
          slot.changed,
          slot.value_ready,
          slot.transaction_ready,
      };
      for (const auto* const pointer : read_words) {
        if ((reinterpret_cast<std::uintptr_t>(pointer)
                % alignof(std::uint64_t)) != 0U
            || !add_span(pointer, sizeof(std::uint64_t), false)) {
          return false;
        }
      }
      for (const auto* const pointer : write_words) {
        if ((reinterpret_cast<std::uintptr_t>(pointer)
                % alignof(std::uint64_t)) != 0U
            || !add_span(pointer, sizeof(std::uint64_t), true)) {
          return false;
        }
      }
      for (const auto* const pointer : write_bytes) {
        if (!add_span(pointer, sizeof(std::uint8_t), true)) {
          return false;
        }
      }
    }

    std::ranges::sort(prepared_pointer_spans,
        std::ranges::less { }, &PreparedPointerSpan::begin);
    std::uintptr_t greatest_read_end { };
    std::uintptr_t greatest_write_end { };
    for (const auto& span : prepared_pointer_spans) {
      if (span.writable) {
        if (span.begin < greatest_read_end
            || span.begin < greatest_write_end) {
          return false;
        }
        greatest_write_end = span.end;
      } else {
        if (span.begin < greatest_write_end) {
          return false;
        }
        greatest_read_end = std::max(greatest_read_end, span.end);
      }
    }

    if (direct_window != nullptr) {
      for (std::size_t index = 0U;
           index < direct_window->input_slot_count; ++index) {
        const auto& slot = direct_window->input_slots[index];
        const auto valid_mask = width_mask(slot.width);
        if ((*slot.aval & ~valid_mask) != 0U
            || (*slot.bval & ~valid_mask) != 0U) {
          return false;
        }
      }
    }

    // Do not inspect descriptor storage until every span has passed its
    // alignment, extent-overflow, and alias checks. In particular, malformed
    // readable pointers must decline without touching their pointees.
    for (std::size_t index = 0U; index < batch.slot_count; ++index) {
      const auto& slot = batch.slots[index];
      if (slot.selected == 0U) {
        continue;
      }
      const auto& current = current_internal_values[index];
      const auto mask = width_mask(slot.width);
      if (*slot.owner_mask != mask
          || *slot.old_current_aval != current.aval_words().front()
          || *slot.old_current_bval != current.bval_words().front()
          || *slot.old_owner_aval != *slot.old_current_aval
          || *slot.old_owner_bval != *slot.old_current_bval
          || *slot.changed != 0U || *slot.value_ready != 0U
          || *slot.transaction_ready != 0U) {
        return false;
      }
    }
    return true;
  }

  // Call only after validate_image and any supplied-plane validation. The
  // latter proves that the borrowed planes exactly represent these values.
  [[nodiscard]] bool activation_inputs_are_known_logic4(
      const runtime::simir::RegionKernelActivationImage& image) const
  {
    if (!known_logic4_variant_supported
        || image.register_inputs.size() != inputs.size()) {
      return false;
    }
    for (std::size_t index = 0U; index < inputs.size(); ++index) {
      const auto& input_binding = inputs[index];
      const auto& value = image.register_inputs[index].value;
      if (input_binding.value_kind != ValueKind::logic4 || value.is_logic9()
          || std::ranges::any_of(value.bval_words(),
              [](const std::uint64_t word) { return word != 0U; })) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] const runtime::simir::RegionKernelLogic4InputPlane*
  find_logic4_input_plane(
      const std::span<const runtime::simir::RegionKernelLogic4InputPlane>
          planes,
      const SignalId signal) const noexcept
  {
    const auto found = std::ranges::lower_bound(planes, signal,
        std::ranges::less { },
        &runtime::simir::RegionKernelLogic4InputPlane::signal);
    return found != planes.end() && found->signal == signal
        ? std::addressof(*found) : nullptr;
  }

  [[nodiscard]] const runtime::simir::RegionKernelInputPlane*
  find_input_plane(
      const std::span<const runtime::simir::RegionKernelInputPlane> planes,
      const SignalId signal) const noexcept
  {
    const auto found = std::ranges::lower_bound(planes, signal,
        std::ranges::less { },
        &runtime::simir::RegionKernelInputPlane::signal);
    return found != planes.end() && found->signal == signal
        ? std::addressof(*found) : nullptr;
  }

  [[nodiscard]] bool validate_logic4_input_planes(
      const runtime::simir::RegionKernelActivationImage& image,
      const std::span<const runtime::simir::RegionKernelLogic4InputPlane>
          planes) const noexcept
  {
    std::size_t expected_plane_count { };
    for (const auto& input : inputs) {
      if (input.internal) {
        if (input.readiness || input.value_kind != ValueKind::logic4) {
          return false;
        }
        ++expected_plane_count;
      }
    }
    if (expected_plane_count == 0U || planes.size() != expected_plane_count) {
      return false;
    }
    for (std::size_t index = 0U; index < planes.size(); ++index) {
      const auto& plane = planes[index];
      if (plane.width == 0U || plane.width > 64U || plane.aval.empty()
          || plane.aval.size() != plane.bval.size()
          || (index != 0U && planes[index - 1U].signal >= plane.signal)) {
        return false;
      }
      const auto expected_words = word_count_for_width(plane.width);
      if (plane.aval.size() != expected_words) {
        return false;
      }
      if (plane.width % 64U != 0U) {
        const auto remainder = plane.width % 64U;
        const auto valid_mask = (UINT64_C(1) << remainder) - 1U;
        if (((plane.aval.back() | plane.bval.back()) & ~valid_mask) != 0U) {
          return false;
        }
      }
      const auto input_binding = std::ranges::find_if(inputs,
          [&plane](const InputBinding& input) {
              return input.internal && input.source_signal == plane.signal;
          });
      if (input_binding == inputs.end()
          || input_binding->value_kind != ValueKind::logic4
          || input_binding->readiness
          || input_binding->reg != plane.register_id
          || input_binding->width != plane.width) {
        return false;
      }
      const auto binding_index = static_cast<std::size_t>(
          input_binding - inputs.begin());
      if (binding_index >= image.register_inputs.size()) {
        return false;
      }
      const auto& image_input = image.register_inputs[binding_index];
      if (image_input.register_id != input_binding->reg
          || image_input.value.width() != plane.width
          || image_input.value.is_logic9()
          || !std::ranges::equal(plane.aval, image_input.value.aval_words())
          || !std::ranges::equal(plane.bval, image_input.value.bval_words())) {
        return false;
      }
    }
    for (const auto& input : inputs) {
      if (!input.internal) {
        continue;
      }
      const auto* const plane
          = find_logic4_input_plane(planes, input.source_signal);
      if (plane == nullptr || plane->register_id != input.reg
          || plane->width != input.width) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool validate_input_planes(
      const runtime::simir::RegionKernelActivationImage& image,
      const std::span<const runtime::simir::RegionKernelInputPlane>
          planes) const noexcept
  {
    std::size_t expected_plane_count { };
    for (const auto& input : inputs) {
      if (input.internal) {
        if (input.readiness
            || (input.value_kind != ValueKind::logic4
                && input.value_kind != ValueKind::logic9)) {
          return false;
        }
        ++expected_plane_count;
      }
    }
    if (expected_plane_count == 0U
        || planes.size() != expected_plane_count) {
      return false;
    }

    for (std::size_t plane_index = 0U;
         plane_index < planes.size(); ++plane_index) {
      const auto& plane = planes[plane_index];
      if (plane.width == 0U
          || (plane.value_kind != ValueKind::logic4
              && plane.value_kind != ValueKind::logic9)
          || (plane_index != 0U
              && planes[plane_index - 1U].signal >= plane.signal)) {
        return false;
      }
      const auto expected_words = word_count_for_width(plane.width);
      if (expected_words == 0U || plane.planes[0U].size() != expected_words
          || plane.planes[1U].size() != expected_words) {
        return false;
      }
      if (plane.value_kind == ValueKind::logic4) {
        if (!plane.planes[2U].empty() || !plane.planes[3U].empty()) {
          return false;
        }
      } else if (plane.planes[2U].size() != expected_words
          || plane.planes[3U].size() != expected_words) {
        return false;
      }

      for (std::uint32_t word = 0U; word < expected_words; ++word) {
        const auto valid_mask = word_mask_for_width(plane.width, word);
        const auto p0 = plane.planes[0U][word];
        const auto p1 = plane.planes[1U][word];
        if ((p0 & ~valid_mask) != 0U || (p1 & ~valid_mask) != 0U) {
          return false;
        }
        if (plane.value_kind == ValueKind::logic9) {
          const auto p2 = plane.planes[2U][word];
          const auto p3 = plane.planes[3U][word];
          if ((p2 & ~valid_mask) != 0U || (p3 & ~valid_mask) != 0U
              || (p3 & (p2 | p1 | p0)) != 0U) {
            return false;
          }
        }
      }

      const auto input_binding = std::ranges::find_if(inputs,
          [&plane](const InputBinding& input) {
              return input.internal && input.source_signal == plane.signal;
          });
      if (input_binding == inputs.end() || input_binding->readiness
          || input_binding->value_kind != plane.value_kind
          || input_binding->reg != plane.register_id
          || input_binding->width != plane.width) {
        return false;
      }
      const auto binding_index = static_cast<std::size_t>(
          input_binding - inputs.begin());
      if (binding_index >= image.register_inputs.size()) {
        return false;
      }
      const auto& image_input = image.register_inputs[binding_index];
      if (image_input.register_id != input_binding->reg
          || image_input.value.width() != plane.width
          || image_input.value.is_logic9()
              != (plane.value_kind == ValueKind::logic9)) {
        return false;
      }
      if (plane.value_kind == ValueKind::logic4) {
        if (!std::ranges::equal(plane.planes[0U],
                image_input.value.aval_words())
            || !std::ranges::equal(plane.planes[1U],
                image_input.value.bval_words())) {
          return false;
        }
      } else {
        for (std::size_t ordinal_plane = 0U;
             ordinal_plane < 4U; ++ordinal_plane) {
          if (!std::ranges::equal(plane.planes[ordinal_plane],
                  image_input.value.logic9_plane_words(ordinal_plane))) {
            return false;
          }
        }
      }
    }

    for (const auto& input : inputs) {
      if (!input.internal) {
        continue;
      }
      const auto* const plane = find_input_plane(planes, input.source_signal);
      if (plane == nullptr || plane->register_id != input.reg
          || plane->width != input.width
          || plane->value_kind != input.value_kind) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool prepare_activation(
      const runtime::simir::RegionKernelActivationImage& image,
      const std::span<const runtime::simir::RegionKernelLogic4InputPlane>
          logic4_input_planes,
      const std::span<const runtime::simir::RegionKernelInputPlane>
          input_planes,
      bool& selected_known_logic4,
      const std::span<const PackedLogic4> prefix_current_values = { },
      const bool allow_known_logic4 = true,
      const bool direct_ready_window = false)
  {
    const auto source_register_value_kinds
        = runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
            prepared_template->synthetic.register_value_kinds);
    if (!validate_image(image, direct_ready_window)
        || (!logic4_input_planes.empty()
            && (!input_planes.empty()
                || !validate_logic4_input_planes(
                    image, logic4_input_planes)))
        || (!input_planes.empty()
            && !validate_input_planes(image, input_planes))) {
      return false;
    }
    if (!direct_ready_window && !prefix_current_values.empty()
        && (!prefix_comparison_supported
            || prefix_current_values.size() != prefix_inputs.size())) {
      return false;
    }
    for (std::size_t index = 0U;
         !direct_ready_window && index < prefix_current_values.size();
         ++index) {
      const auto& input = prefix_inputs[index];
      const auto& value = prefix_current_values[index];
      if (value.width() != input.width || value.is_logic9()) {
        return false;
      }
    }
    if (!direct_ready_window && prefix_current_values.empty()) {
      for (const auto& input : prefix_inputs) {
        if (!input.committed_input_register) {
          continue;
        }
        const auto image_input = std::ranges::lower_bound(
            image.register_inputs, *input.committed_input_register,
            std::ranges::less { },
            &runtime::simir::RegionKernelRegisterInput::register_id);
        if (image_input == image.register_inputs.end()
            || image_input->register_id != *input.committed_input_register
            || image_input->value.width() != input.width
            || image_input->value.is_logic9()) {
          return false;
        }
      }
    }
    if (direct_ready_window
        && (specialization
                != RegionKernelSpecialization::dynamic_inputs
            || direct_ready_bindings.empty()
            || !logic4_input_planes.empty() || !input_planes.empty())) {
      return false;
    }
    if (!direct_ready_window && specialization
        == RegionKernelSpecialization::guarded_constant_inputs) {
      for (std::size_t input_index = 0U;
           input_index < inputs.size(); ++input_index) {
        const auto& expected = inputs[input_index].specialization_value;
        if (expected
            && image.register_inputs[input_index].value != *expected) {
          // This is an ordinary decline. It occurs before touching reusable
          // signal planes, result values, frames, or direct-update slots.
          return false;
        }
      }
    }
    selected_known_logic4 = !direct_ready_window && allow_known_logic4 && specialization
            != RegionKernelSpecialization::guarded_constant_inputs
        && prefix_current_values.empty()
        && activation_inputs_are_known_logic4(image);
    auto& selected_runtime = selected_known_logic4
        ? known_runtime : runtime;
    auto& selected_frame = selected_known_logic4
        ? known_frame : frame;
    auto& selected_result = selected_known_logic4
        ? known_result : result;
    callback_state.used = false;
    std::fill(direct_signal_aval.begin(), direct_signal_aval.end(), 0U);
    std::fill(direct_signal_bval.begin(), direct_signal_bval.end(), 0U);
    std::fill(direct_signal_logic9_plane0.begin(),
        direct_signal_logic9_plane0.end(), 0U);
    std::fill(direct_signal_logic9_plane1.begin(),
        direct_signal_logic9_plane1.end(), 0U);
    std::fill(direct_signal_logic9_plane2.begin(),
        direct_signal_logic9_plane2.end(), 0U);
    std::fill(direct_signal_logic9_plane3.begin(),
        direct_signal_logic9_plane3.end(), 0U);
    std::fill(direct_wide_signal_aval.begin(),
        direct_wide_signal_aval.end(), 0U);
    std::fill(direct_wide_signal_bval.begin(),
        direct_wide_signal_bval.end(), 0U);
    std::fill(direct_wide_signal_logic9_plane2.begin(),
        direct_wide_signal_logic9_plane2.end(), 0U);
    std::fill(direct_wide_signal_logic9_plane3.begin(),
        direct_wide_signal_logic9_plane3.end(), 0U);
    // Detach retained wide values before native entry. The post-entry output
    // chunk inserts then write into unique, preallocated PackedLogic4 planes.
    for (std::size_t reg = 0U; reg < register_values.size(); ++reg) {
      auto& value = register_values[reg];
      if (source_register_value_kinds[reg] == ValueKind::logic9) {
        value.fill(runtime::Logic9::x);
      } else {
        value.fill(runtime::Logic4::x);
      }
    }
    if (frame_exports) {
      std::fill(register_aval.begin(), register_aval.end(), 0U);
      std::fill(register_bval.begin(), register_bval.end(), 0U);
      std::fill(register_logic9_plane2.begin(),
          register_logic9_plane2.end(), 0U);
      std::fill(register_logic9_plane3.begin(),
          register_logic9_plane3.end(), 0U);
      std::fill(register_initialized.begin(),
          register_initialized.end(), 0U);
    }
    for (std::size_t input_index = 0U;
         input_index < inputs.size(); ++input_index) {
      if (direct_ready_window) {
        break;
      }
      const auto& input = inputs[input_index];
      const auto& value = image.register_inputs[input_index].value;
      const auto* const input_plane = input_planes.empty() || !input.internal
          ? nullptr : find_input_plane(input_planes, input.source_signal);
      const auto* const direct_plane = logic4_input_planes.empty()
          || !input.internal ? nullptr
          : find_logic4_input_plane(
                logic4_input_planes, input.source_signal);
      if (input_plane != nullptr) {
        const auto signal = input.synthetic_signal;
        if (input.width <= 64U) {
          direct_signal_aval[signal] = input_plane->planes[0U][0U];
          direct_signal_bval[signal] = input_plane->planes[1U][0U];
          if (input.value_kind == ValueKind::logic9) {
            for (std::size_t plane = 0U; plane < 4U; ++plane) {
              const auto word = input_plane->planes[plane][0U];
              if (plane == 0U) {
                direct_signal_logic9_plane0[signal] = word;
              } else if (plane == 1U) {
                direct_signal_logic9_plane1[signal] = word;
              } else if (plane == 2U) {
                direct_signal_logic9_plane2[signal] = word;
              } else {
                direct_signal_logic9_plane3[signal] = word;
              }
            }
          }
        } else {
          const auto offset = direct_wide_signal_offsets[signal];
          std::copy(input_plane->planes[0U].begin(),
              input_plane->planes[0U].end(),
              direct_wide_signal_aval.begin() + offset);
          std::copy(input_plane->planes[1U].begin(),
              input_plane->planes[1U].end(),
              direct_wide_signal_bval.begin() + offset);
          if (input.value_kind == ValueKind::logic9) {
            std::copy(input_plane->planes[2U].begin(),
                input_plane->planes[2U].end(),
                direct_wide_signal_logic9_plane2.begin() + offset);
            std::copy(input_plane->planes[3U].begin(),
                input_plane->planes[3U].end(),
                direct_wide_signal_logic9_plane3.begin() + offset);
          }
        }

        if (input.value_kind == ValueKind::logic4) {
          for (std::uint32_t word = 0U;
               word < word_count_for_width(input.width); ++word) {
            const auto word_start = static_cast<std::uint64_t>(word) * 64U;
            const auto word_width = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(
                    64U, static_cast<std::uint64_t>(input.width) - word_start));
            register_values[input.reg].insert_word(
                { word_width, input_plane->planes[0U][word],
                    input_plane->planes[1U][word] },
                static_cast<std::size_t>(word_start));
          }
        } else {
          for (std::uint32_t bit = 0U; bit < input.width; ++bit) {
            const auto word = static_cast<std::size_t>(bit / 64U);
            const auto mask = UINT64_C(1) << (bit % 64U);
            const auto ordinal
                = static_cast<std::uint8_t>(
                    (input_plane->planes[0U][word] & mask) != 0U)
                | static_cast<std::uint8_t>(
                    (input_plane->planes[1U][word] & mask) != 0U) << 1U
                | static_cast<std::uint8_t>(
                    (input_plane->planes[2U][word] & mask) != 0U) << 2U
                | static_cast<std::uint8_t>(
                    (input_plane->planes[3U][word] & mask) != 0U) << 3U;
            register_values[input.reg].set_logic9(
                bit, static_cast<runtime::Logic9>(ordinal));
          }
        }
      } else if (direct_plane != nullptr) {
        const runtime::Logic4Word word {
            input.width, direct_plane->aval[0U], direct_plane->bval[0U] };
        direct_signal_aval[input.synthetic_signal] = word.aval;
        direct_signal_bval[input.synthetic_signal] = word.bval;
        register_values[input.reg].assign_word(word);
      } else if (input.value_kind == ValueKind::logic9) {
        if (input.width <= 64U) {
          const auto word = value.logic9_low_word();
          direct_signal_aval[input.synthetic_signal] = word.planes[0];
          direct_signal_bval[input.synthetic_signal] = word.planes[1];
          direct_signal_logic9_plane0[input.synthetic_signal]
              = word.planes[0];
          direct_signal_logic9_plane1[input.synthetic_signal]
              = word.planes[1];
          direct_signal_logic9_plane2[input.synthetic_signal]
              = word.planes[2];
          direct_signal_logic9_plane3[input.synthetic_signal]
              = word.planes[3];
        } else {
          const auto offset
              = direct_wide_signal_offsets[input.synthetic_signal];
          for (std::size_t plane = 0U; plane < 2U; ++plane) {
            const auto words = value.logic9_plane_words(plane);
            std::copy(words.begin(), words.end(),
                (plane == 0U ? direct_wide_signal_aval
                             : direct_wide_signal_bval).begin() + offset);
          }
          std::copy(value.logic9_plane_words(2U).begin(),
              value.logic9_plane_words(2U).end(),
              direct_wide_signal_logic9_plane2.begin() + offset);
          std::copy(value.logic9_plane_words(3U).begin(),
              value.logic9_plane_words(3U).end(),
              direct_wide_signal_logic9_plane3.begin() + offset);
        }
        register_values[input.reg].insert_bits(value, 0U);
      } else if (input.width <= 64U) {
        const auto word = value.low_word();
        direct_signal_aval[input.synthetic_signal] = word.aval;
        direct_signal_bval[input.synthetic_signal] = word.bval;
        register_values[input.reg].assign_word(word);
      } else {
        const auto offset
            = direct_wide_signal_offsets[input.synthetic_signal];
        const auto aval_words = value.aval_words();
        const auto bval_words = value.bval_words();
        std::copy(aval_words.begin(), aval_words.end(),
            direct_wide_signal_aval.begin() + offset);
        std::copy(bval_words.begin(), bval_words.end(),
            direct_wide_signal_bval.begin() + offset);
        register_values[input.reg].insert_bits(value, 0U);
      }
    }
    for (std::size_t input_index = 0U;
         input_index < prefix_inputs.size(); ++input_index) {
      if (direct_ready_window) {
        break;
      }
      const auto& input = prefix_inputs[input_index];
      auto word = runtime::Logic4Word { input.width, 0U, 0U };
      if (!prefix_current_values.empty()) {
        word = prefix_current_values[input_index].low_word();
      } else if (input.committed_input_register) {
        const auto image_input = std::ranges::lower_bound(
            image.register_inputs, *input.committed_input_register,
            std::ranges::less { },
            &runtime::simir::RegionKernelRegisterInput::register_id);
        word = image_input->value.low_word();
      }
      direct_signal_aval[input.synthetic_signal] = word.aval;
      direct_signal_bval[input.synthetic_signal] = word.bval;
    }
    reset_direct_updates();

    selected_frame.program_counter = 0U;
    selected_frame.state = FSIM_JIT_FRAME_STATE_READY_V2;
    selected_frame.last_instruction = FSIM_JIT_INVALID_INSTRUCTION_V2;
    selected_frame.native_call_depth = 0U;
    selected_frame.native_call_reserved = 0U;
    std::fill(std::begin(selected_frame.native_return_stack),
        std::end(selected_frame.native_return_stack), 0U);
    selected_result.status = FSIM_JIT_RESUME_STATUS_COMPLETED_V2;
    selected_result.instruction = FSIM_JIT_INVALID_INSTRUCTION_V2;
    selected_result.delay = 0U;

    return selected_runtime.flags == 0U
        && selected_frame.state == FSIM_JIT_FRAME_STATE_READY_V2
        && selected_frame.program_counter == 0U
        && selected_frame.native_call_depth == 0U
        && selected_frame.native_call_reserved == 0U
        && selected_result.abi_version
            == FSIM_JIT_RESUME_RESULT_ABI_VERSION_V2
        && selected_result.struct_size >= sizeof(fsim_jit_resume_result_v2);
  }

  [[nodiscard]] bool finish_activation(
      const runtime::simir::RegionKernelActivationImage& image,
      const bool selected_known_logic4,
      const std::uint32_t raw_status)
  {
    const auto& selected_frame = selected_known_logic4
        ? known_frame : frame;
    const auto& selected_result = selected_known_logic4
        ? known_result : result;
    const auto& selected_layout = selected_known_logic4
        ? known_layout : layout;
    if (callback_state.used
        || raw_status != selected_result.status
        || raw_status != FSIM_JIT_RESUME_STATUS_COMPLETED_V2
        || selected_result.status != FSIM_JIT_RESUME_STATUS_COMPLETED_V2
        || selected_frame.state != FSIM_JIT_FRAME_STATE_COMPLETED_V2) {
      return false;
    }

    if (frame_exports) {
      return finish_frame_export_activation(image);
    }

    for (std::size_t spill_index = 0U;
         spill_index < spills.size(); ++spill_index) {
      const auto& spill = spills[spill_index];
      const auto slot_index = update_slot_by_signal[spill.synthetic_signal];
      if (slot_index >= direct_update_slots.size()) {
        return false;
      }
      const auto& slot = direct_update_slots[slot_index];
      if (selected_layout.direct_update_signals[slot_index]
          != spill.synthetic_signal) {
        return false;
      }
      const bool expected_active
          = is_active_member(image, spill.member_index);
      const bool active_bit
          = (active_words[slot_index / 64U]
                & (UINT64_C(1) << (slot_index % 64U))) != 0U;
      if (slot.active != (expected_active ? 1U : 0U)
          || active_bit != expected_active || slot.width != spill.width
          || slot.word_count != word_count_for_width(spill.width)
          || slot.reserved != 0U || slot.logic9_plane2 != 0U
          || slot.logic9_plane3 != 0U) {
        return false;
      }

      if (spill.width <= 64U) {
        const auto mask = width_mask(spill.width);
        if (slot.wide_aval != nullptr || slot.wide_bval != nullptr
            || slot.wide_mask != nullptr
            || direct_update_wide_word_offsets[slot_index]
                != std::numeric_limits<std::size_t>::max()
            || slot.mask != (expected_active ? mask : 0U)
            || (slot.aval & ~mask) != 0U || (slot.bval & ~mask) != 0U
            || (!expected_active
                && (slot.aval != 0U || slot.bval != 0U))) {
          return false;
        }
        if (expected_active) {
          register_values[spill.activation_register].insert_word(
              { spill.width, slot.aval, slot.bval }, 0U);
        }
        if (spill.prefix_change_flag) {
          if (spill.prefix_output_index >= generated_output_changed.size()
              || slot.aval > 1U || slot.bval != 0U) {
            return false;
          }
          generated_output_changed[spill.prefix_output_index]
              = static_cast<std::uint8_t>(slot.aval);
        }
        continue;
      }

      const auto expected_offset
          = direct_update_wide_word_offsets[slot_index];
      const auto word_count = word_count_for_width(spill.width);
      if (expected_offset == std::numeric_limits<std::size_t>::max()
          || slot.wide_aval
              != direct_update_wide_aval.data() + expected_offset
          || slot.wide_bval
              != direct_update_wide_bval.data() + expected_offset
          || slot.wide_mask
              != direct_update_wide_mask.data() + expected_offset
          || slot.aval != 0U || slot.bval != 0U || slot.mask != 0U) {
        return false;
      }
      for (std::uint32_t word = 0U; word < word_count; ++word) {
        const auto mask = word_mask_for_width(spill.width, word);
        const auto expected_mask = expected_active ? mask : 0U;
        if (slot.wide_mask[word] != expected_mask
            || (slot.wide_aval[word] & ~mask) != 0U
            || (slot.wide_bval[word] & ~mask) != 0U
            || (!expected_active
                && (slot.wide_aval[word] != 0U
                    || slot.wide_bval[word] != 0U))) {
          return false;
        }
        if (expected_active) {
          const auto word_start
              = static_cast<std::uint64_t>(word) * 64U;
          const auto chunk_width = static_cast<std::uint32_t>(
              std::min<std::uint64_t>(
                  64U, static_cast<std::uint64_t>(spill.width) - word_start));
          register_values[spill.activation_register].insert_word(
              { chunk_width, slot.wide_aval[word], slot.wide_bval[word] },
              static_cast<std::size_t>(word_start));
        }
      }
    }
    if (!active_words.empty() && direct_update_slots.size() % 64U != 0U) {
      const auto valid_bits = direct_update_slots.size() % 64U;
      const auto valid_mask = (UINT64_C(1) << valid_bits) - 1U;
      if ((active_words.back() & ~valid_mask) != 0U) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool finish_frame_export_activation(
      const runtime::simir::RegionKernelActivationImage& image)
  {
    const auto source_register_value_kinds
        = runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
            prepared_template->synthetic.register_value_kinds);
    if (!direct_update_slots.empty() || !active_words.empty()
        || direct_update_wide_aval.size() != 0U
        || direct_update_wide_bval.size() != 0U
        || direct_update_wide_mask.size() != 0U) {
      return false;
    }

    // Validate the entire exported frame before changing any public result
    // register. A malformed native value therefore declines atomically.
    for (const auto& spill : spills) {
      const auto reg = static_cast<std::size_t>(spill.activation_register);
      if (reg >= register_export_mask.size()
          || register_export_mask[reg] != 1U
          || layout.register_values_persistent[reg] == 0U
          || register_initialized[reg] > 1U
          || layout.register_widths[reg] != spill.width
          || source_register_value_kinds[reg] != spill.value_kind
          || register_values[reg].width() != spill.width
          || register_values[reg].is_logic9()
              != (spill.value_kind == ValueKind::logic9)) {
        return false;
      }
      const bool expected_active
          = is_active_member(image, spill.member_index);
      if (register_initialized[reg] != (expected_active ? 1U : 0U)) {
        return false;
      }
      const auto word_count = word_count_for_width(spill.width);
      const auto offset = layout.register_word_offsets[reg];
      if (word_count == 0U || offset > register_aval.size()
          || word_count > register_aval.size() - offset
          || offset > register_bval.size()
          || word_count > register_bval.size() - offset) {
        return false;
      }
      if (spill.value_kind == ValueKind::logic9
          && (offset > register_logic9_plane2.size()
              || word_count > register_logic9_plane2.size() - offset
              || offset > register_logic9_plane3.size()
              || word_count > register_logic9_plane3.size() - offset)) {
        return false;
      }
      if (spill.value_kind == ValueKind::logic4
          && layout.uses_logic9
          && (offset > register_logic9_plane2.size()
              || word_count > register_logic9_plane2.size() - offset
              || offset > register_logic9_plane3.size()
              || word_count > register_logic9_plane3.size() - offset)) {
        return false;
      }

      for (std::uint32_t word = 0U; word < word_count; ++word) {
        const auto index = static_cast<std::size_t>(offset) + word;
        const auto valid_mask = word_mask_for_width(spill.width, word);
        const auto aval = register_aval[index];
        const auto bval = register_bval[index];
        if ((aval & ~valid_mask) != 0U || (bval & ~valid_mask) != 0U
            || (!expected_active && (aval != 0U || bval != 0U))) {
          return false;
        }
        if (spill.value_kind == ValueKind::logic9) {
          const auto plane2 = register_logic9_plane2[index];
          const auto plane3 = register_logic9_plane3[index];
          if ((plane2 & ~valid_mask) != 0U
              || (plane3 & ~valid_mask) != 0U
              || (plane3 & (plane2 | bval | aval)) != 0U
              || (!expected_active && (plane2 != 0U || plane3 != 0U))) {
            return false;
          }
        } else if (layout.uses_logic9
            && (register_logic9_plane2[index] != 0U
                || register_logic9_plane3[index] != 0U)) {
          return false;
        }
      }
    }

    for (const auto& spill : spills) {
      if (!is_active_member(image, spill.member_index)) {
        continue;
      }
      const auto reg = static_cast<std::size_t>(spill.activation_register);
      const auto word_count = word_count_for_width(spill.width);
      const auto offset = layout.register_word_offsets[reg];
      auto& result_value = register_values[reg];
      if (spill.value_kind == ValueKind::logic9) {
        for (std::uint32_t word = 0U; word < word_count; ++word) {
          const auto index = static_cast<std::size_t>(offset) + word;
          const auto word_start = static_cast<std::uint64_t>(word) * 64U;
          const auto width = static_cast<std::uint32_t>(
              std::min<std::uint64_t>(
                  64U, static_cast<std::uint64_t>(spill.width) - word_start));
          for (std::uint32_t bit = 0U; bit < width; ++bit) {
            const auto mask = UINT64_C(1) << bit;
            const auto code
                = static_cast<std::uint8_t>((register_aval[index] & mask) != 0U)
                | static_cast<std::uint8_t>((register_bval[index] & mask) != 0U)
                    << 1U
                | static_cast<std::uint8_t>(
                    (register_logic9_plane2[index] & mask) != 0U)
                    << 2U
                | static_cast<std::uint8_t>(
                    (register_logic9_plane3[index] & mask) != 0U)
                    << 3U;
            result_value.set_logic9(
                static_cast<std::size_t>(word_start + bit),
                static_cast<runtime::Logic9>(code));
          }
        }
      } else {
        for (std::uint32_t word = 0U; word < word_count; ++word) {
          const auto index = static_cast<std::size_t>(offset) + word;
          const auto word_start = static_cast<std::uint64_t>(word) * 64U;
          const auto width = static_cast<std::uint32_t>(
              std::min<std::uint64_t>(
                  64U, static_cast<std::uint64_t>(spill.width) - word_start));
          result_value.insert_word(
              { width, register_aval[index], register_bval[index] },
              static_cast<std::size_t>(word_start));
        }
      }
    }
    return true;
  }

  RegionConeActivationKernel kernel;
  RegionKernelSpecialization specialization
      { RegionKernelSpecialization::dynamic_inputs };
  // Keep the shared native owner alive until every instance certificate and
  // binding that points into its LlvmJit has been destroyed.
  std::shared_ptr<RegionKernelNativeBody> native_body;
  LlvmJitOptions options;
  std::vector<InputBinding> inputs;
  std::vector<PrefixInputBinding> prefix_inputs;
  std::vector<PrefixFlagBinding> prefix_flags;
  std::vector<SpillBinding> spills;
  std::vector<std::vector<std::size_t>> spills_by_member;
  std::vector<std::uint32_t> signal_widths;
  std::vector<ValueKind> signal_value_kinds;
  std::vector<std::uint32_t> prepared_output_signals;
  std::shared_ptr<const RegionKernelPreparedTemplate> prepared_template;
  std::vector<std::uint64_t> prepared_output_successor_masks;
  bool prepared_output_successor_masks_representable { true };
  std::vector<llvm_detail::RegionDirectReadyLoweringBinding>
      direct_ready_bindings;
  struct PreparedPointerSpan {
    std::uintptr_t begin { };
    std::uintptr_t end { };
    bool writable { };
  };
  std::vector<PreparedPointerSpan> prepared_pointer_spans;
  std::vector<std::uint32_t> direct_wide_signal_offsets;
  std::uint32_t direct_wide_word_count { };
  std::vector<std::uint32_t> update_slot_by_signal;
  // The bound activation keeps this metadata span for the executor lifetime.
  std::optional<runtime::simir::process_layout_detail::ProcessLayoutReadView<ValueKind>>
      activation_register_value_kinds;
  std::string identity;
  // Physical wrapper mappings stay instance-local. Shared code owners key on
  // this separately canonicalized synthetic program identity.
  std::string native_code_identity;
  JitProcessBinding binding;
  JitProcessFrameLayout layout;
  JitProcessBinding known_binding;
  JitProcessFrameLayout known_layout;
  CallbackState callback_state;
  fsim_jit_runtime_instance_v2 runtime { };
  fsim_jit_frame_v2 frame { };
  fsim_jit_resume_result_v2 result { };
  fsim_jit_runtime_instance_v2 known_runtime { };
  fsim_jit_frame_v2 known_frame { };
  fsim_jit_resume_result_v2 known_result { };
  std::vector<std::uint64_t> register_aval;
  std::vector<std::uint64_t> register_bval;
  std::vector<std::uint64_t> register_logic9_plane2;
  std::vector<std::uint64_t> register_logic9_plane3;
  std::vector<std::uint8_t> register_initialized;
  std::vector<std::uint64_t> direct_signal_aval;
  std::vector<std::uint64_t> direct_signal_bval;
  std::vector<std::uint64_t> direct_signal_logic9_plane0;
  std::vector<std::uint64_t> direct_signal_logic9_plane1;
  std::vector<std::uint64_t> direct_signal_logic9_plane2;
  std::vector<std::uint64_t> direct_signal_logic9_plane3;
  std::vector<std::uint64_t> direct_wide_signal_aval;
  std::vector<std::uint64_t> direct_wide_signal_bval;
  std::vector<std::uint64_t> direct_wide_signal_logic9_plane2;
  std::vector<std::uint64_t> direct_wide_signal_logic9_plane3;
  std::vector<fsim_jit_update_slot_v2> direct_update_slots;
  std::vector<std::size_t> direct_update_wide_word_offsets;
  std::vector<std::uint64_t> direct_update_wide_aval;
  std::vector<std::uint64_t> direct_update_wide_bval;
  std::vector<std::uint64_t> direct_update_wide_mask;
  std::vector<std::uint64_t> active_words;
  std::vector<std::uint8_t> register_export_mask;
  std::vector<std::uint8_t> generated_output_changed;
  std::vector<std::size_t> prefix_signal_seen;
  std::vector<std::uint8_t> output_prefix_changed;
  bool prefix_comparison_supported { };
  bool output_prefix_result_valid { };
  std::vector<std::uint8_t> request_seen;
  std::vector<PackedLogic4> register_values;
  bool frame_exports { };
  bool known_logic4_variant_supported { };
  bool known_variant_ready { };
  llvm_detail::RegionKernelBodySelection last_body_selection
      { llvm_detail::RegionKernelBodySelection::never_entered };
  std::atomic_flag in_use = ATOMIC_FLAG_INIT;
  std::exception_ptr last_failure;
  llvm_detail::RegionKernelActivationCertificate activation;
  llvm_detail::RegionKernelActivationCertificate known_activation;
  llvm_detail::RegionPreparedOutputCertificate prepared_output_activation;
  llvm_detail::RegionDirectReadyOutputCertificate
      direct_ready_output_activation;
};

std::unique_ptr<LlvmRegionKernelExecutor> LlvmRegionKernelExecutor::try_create(
    const RegionConeActivationKernel& kernel, LlvmJitOptions options,
    const std::string_view immutable_design_identity,
    const RegionKernelSpecialization specialization)
{
  options.debug_instrumentation = false;
  options.require_direct_update_slots = true;
  options.require_direct_read_signals = true;
  std::string_view stage { "allocate-impl" };
  try {
    auto impl = std::make_unique<Impl>(
        kernel, std::move(options), specialization);
    stage = "prepare";
    if (!impl->prepare(immutable_design_identity)) {
      report_region_creation_event(kernel, specialization,
          "null-return", "prepare-returned-false");
      return nullptr;
    }
    if (impl->known_variant_ready) {
      stage = "known-logic4-frame-init";
      impl->prepare_known_logic4_frame();
    }
    stage = "activation-certificate-bind";
    auto executor = std::unique_ptr<LlvmRegionKernelExecutor> {
        new LlvmRegionKernelExecutor(std::move(impl)) };
    auto& state = *executor->impl_;
    const std::lock_guard native_api_lock {
        state.native_body->api_mutex };
    state.activation = state.jit().bind_region_activation(
        state.binding, state.runtime, state.frame, state.result, state.layout,
        state.activation_backing(state.layout));
    if (!state.prepared_output_signals.empty()) {
      stage = "prepared-output-certificate-bind";
      state.prepared_output_activation
          = state.jit().bind_region_prepared_output(
              state.binding, state.runtime, state.frame, state.result,
              state.layout, state.activation_backing(state.layout));
      if (!state.direct_ready_bindings.empty()) {
        stage = "direct-ready-certificate-bind";
        state.direct_ready_output_activation
            = state.jit().bind_region_direct_ready_output(
                state.binding, state.runtime, state.frame, state.result,
                state.layout, state.activation_backing(state.layout));
      }
    }
    if (state.known_variant_ready) {
      stage = "known-logic4-certificate-bind";
      state.known_activation = state.jit().bind_region_activation(
          state.known_binding, state.known_runtime, state.known_frame,
          state.known_result, state.known_layout,
          state.activation_backing(state.known_layout));
    }
    return executor;
  } catch (const std::exception& error) {
    report_region_creation_event(kernel, specialization,
        "exception", stage, 0U, error.what());
    throw;
  } catch (...) {
    report_region_creation_event(kernel, specialization,
        "exception", stage, 0U, "non-standard exception");
    throw;
  }
}

LlvmRegionKernelExecutor::LlvmRegionKernelExecutor(
    std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{
}

LlvmRegionKernelExecutor::~LlvmRegionKernelExecutor() = default;
LlvmRegionKernelExecutor::LlvmRegionKernelExecutor(
    LlvmRegionKernelExecutor&&) noexcept = default;
LlvmRegionKernelExecutor& LlvmRegionKernelExecutor::operator=(
    LlvmRegionKernelExecutor&&) noexcept = default;

std::exception_ptr LlvmRegionKernelExecutor::take_failure() noexcept
{
  if (impl_ == nullptr) {
    return { };
  }
  auto failure = std::move(impl_->last_failure);
  impl_->last_failure = { };
  return failure;
}

llvm_detail::RegionKernelBodySelection
llvm_detail::RegionKernelTestAccess::last_body_selection(
    const LlvmRegionKernelExecutor& executor) noexcept
{
  return executor.impl_
      ? executor.impl_->last_body_selection
      : llvm_detail::RegionKernelBodySelection::never_entered;
}

std::string_view
llvm_detail::RegionKernelTestAccess::native_code_identity(
    const LlvmRegionKernelExecutor& executor) noexcept
{
  return executor.impl_ != nullptr
      ? std::string_view { executor.impl_->native_code_identity }
      : std::string_view { };
}

const void* llvm_detail::RegionKernelTestAccess::native_body_identity(
    const LlvmRegionKernelExecutor& executor) noexcept
{
  return executor.impl_ != nullptr
      ? executor.impl_->native_body.get() : nullptr;
}

const void* llvm_detail::RegionKernelTestAccess::prepared_template_identity(
    const LlvmRegionKernelExecutor& executor) noexcept
{
  return executor.impl_ != nullptr
      ? executor.impl_->prepared_template.get() : nullptr;
}

std::size_t
llvm_detail::RegionKernelTestAccess::prepared_template_build_count() noexcept
{
  return region_prepared_template_registry().template_builds.load(
      std::memory_order_relaxed);
}

std::size_t llvm_detail::RegionKernelTestAccess::
    prepared_template_validation_count() noexcept
{
  return region_prepared_template_registry().process_validations.load(
      std::memory_order_relaxed);
}

const void* llvm_detail::RegionKernelTestAccess::frame_identity(
    const LlvmRegionKernelExecutor& executor) noexcept
{
  return executor.impl_ != nullptr
      ? std::addressof(executor.impl_->frame) : nullptr;
}

const void* llvm_detail::RegionKernelTestAccess::register_storage_identity(
    const LlvmRegionKernelExecutor& executor) noexcept
{
  return executor.impl_ != nullptr
          && !executor.impl_->register_aval.empty()
      ? executor.impl_->register_aval.data() : nullptr;
}

const runtime::simir::RegionConeActivationKernel*
llvm_detail::RegionKernelTestAccess::source_kernel(
    const LlvmRegionKernelExecutor& executor) noexcept
{
  return executor.impl_ != nullptr ? &executor.impl_->kernel : nullptr;
}

const runtime::simir::Process*
llvm_detail::RegionKernelTestAccess::synthetic_process(
    const LlvmRegionKernelExecutor& executor) noexcept
{
  return executor.impl_ != nullptr
          && executor.impl_->prepared_template != nullptr
      ? std::addressof(executor.impl_->prepared_template->synthetic) : nullptr;
}

std::span<const std::uint64_t>
llvm_detail::RegionKernelTestAccess::direct_signal_aval(
    const LlvmRegionKernelExecutor& executor) noexcept
{
  return executor.impl_ != nullptr
      ? std::span<const std::uint64_t> { executor.impl_->direct_signal_aval }
      : std::span<const std::uint64_t> { };
}

std::span<const std::uint64_t>
llvm_detail::RegionKernelTestAccess::direct_signal_bval(
    const LlvmRegionKernelExecutor& executor) noexcept
{
  return executor.impl_ != nullptr
      ? std::span<const std::uint64_t> { executor.impl_->direct_signal_bval }
      : std::span<const std::uint64_t> { };
}

bool LlvmRegionKernelExecutor::execute(
    const runtime::simir::RegionKernelActivationImage& image) noexcept
{
  if (!impl_) {
    return false;
  }

  auto& state = *impl_;
  llvm_detail::RegionKernelInvocationGuard invocation { state.in_use };
  if (!invocation) {
    return false;
  }
  state.last_failure = { };
  if (!state.jit().matches_region_activation(state.activation,
          state.binding, state.runtime, state.frame, state.result)
      || (state.known_variant_ready
          && !state.jit().matches_region_activation(
              state.known_activation, state.known_binding,
              state.known_runtime, state.known_frame,
              state.known_result))) {
    return false;
  }

  try {
    bool selected_known_logic4 { };
    if (!state.prepare_activation(
            image, { }, { }, selected_known_logic4)) {
      return false;
    }
    auto& selected_binding = selected_known_logic4
        ? state.known_binding : state.binding;
    auto& selected_runtime = selected_known_logic4
        ? state.known_runtime : state.runtime;
    auto& selected_frame = selected_known_logic4
        ? state.known_frame : state.frame;
    auto& selected_result = selected_known_logic4
        ? state.known_result : state.result;
    const auto& selected_activation = selected_known_logic4
        ? state.known_activation : state.activation;
    std::uint32_t raw_status { };
    std::exception_ptr invocation_failure;
    if (!state.jit().resume_region_activation(selected_activation,
            selected_binding, selected_runtime, selected_frame, selected_result,
            raw_status, invocation_failure)) {
      state.last_failure = std::move(invocation_failure);
      return false;
    }
    state.last_body_selection
        = state.specialization
                == RegionKernelSpecialization::guarded_constant_inputs
            ? llvm_detail::RegionKernelBodySelection::guarded_constant_inputs
            : selected_known_logic4
                ? llvm_detail::RegionKernelBodySelection::known_logic4
                : llvm_detail::RegionKernelBodySelection::four_state;
    return state.finish_activation(image, selected_known_logic4, raw_status);
  } catch (...) {
    state.last_failure = std::current_exception();
    return false;
  }
}

bool LlvmRegionKernelExecutor::execute_with_logic4_input_planes(
    const runtime::simir::RegionKernelActivationImage& image,
    const std::span<const runtime::simir::RegionKernelLogic4InputPlane> planes)
    noexcept
{
  if (!impl_) {
    return false;
  }

  auto& state = *impl_;
  llvm_detail::RegionKernelInvocationGuard invocation { state.in_use };
  if (!invocation) {
    return false;
  }
  state.last_failure = { };
  if (!state.jit().matches_region_activation(state.activation,
          state.binding, state.runtime, state.frame, state.result)
      || (state.known_variant_ready
          && !state.jit().matches_region_activation(
              state.known_activation, state.known_binding,
              state.known_runtime, state.known_frame,
              state.known_result))) {
    return false;
  }

  try {
    bool selected_known_logic4 { };
    if (!state.validate_image(image)
        || !state.validate_logic4_input_planes(image, planes)
        || !state.prepare_activation(
            image, planes, { }, selected_known_logic4)) {
      return false;
    }
    auto& selected_binding = selected_known_logic4
        ? state.known_binding : state.binding;
    auto& selected_runtime = selected_known_logic4
        ? state.known_runtime : state.runtime;
    auto& selected_frame = selected_known_logic4
        ? state.known_frame : state.frame;
    auto& selected_result = selected_known_logic4
        ? state.known_result : state.result;
    const auto& selected_activation = selected_known_logic4
        ? state.known_activation : state.activation;
    std::uint32_t raw_status { };
    std::exception_ptr invocation_failure;
    if (!state.jit().resume_region_activation(selected_activation,
            selected_binding, selected_runtime, selected_frame, selected_result,
            raw_status, invocation_failure)) {
      state.last_failure = std::move(invocation_failure);
      return false;
    }
    state.last_body_selection
        = state.specialization
                == RegionKernelSpecialization::guarded_constant_inputs
            ? llvm_detail::RegionKernelBodySelection::guarded_constant_inputs
            : selected_known_logic4
                ? llvm_detail::RegionKernelBodySelection::known_logic4
                : llvm_detail::RegionKernelBodySelection::four_state;
    return state.finish_activation(image, selected_known_logic4, raw_status);
  } catch (...) {
    state.last_failure = std::current_exception();
    return false;
  }
}

bool LlvmRegionKernelExecutor::execute_with_input_planes(
    const runtime::simir::RegionKernelActivationImage& image,
    const std::span<const runtime::simir::RegionKernelInputPlane> planes)
    noexcept
{
  if (!impl_) {
    return false;
  }

  auto& state = *impl_;
  llvm_detail::RegionKernelInvocationGuard invocation { state.in_use };
  if (!invocation) {
    return false;
  }
  state.last_failure = { };
  if (!state.jit().matches_region_activation(state.activation,
          state.binding, state.runtime, state.frame, state.result)
      || (state.known_variant_ready
          && !state.jit().matches_region_activation(
              state.known_activation, state.known_binding,
              state.known_runtime, state.known_frame,
              state.known_result))) {
    return false;
  }

  try {
    bool selected_known_logic4 { };
    if (!state.validate_image(image)
        || !state.validate_input_planes(image, planes)
        || !state.prepare_activation(
            image, { }, planes, selected_known_logic4)) {
      return false;
    }
    auto& selected_binding = selected_known_logic4
        ? state.known_binding : state.binding;
    auto& selected_runtime = selected_known_logic4
        ? state.known_runtime : state.runtime;
    auto& selected_frame = selected_known_logic4
        ? state.known_frame : state.frame;
    auto& selected_result = selected_known_logic4
        ? state.known_result : state.result;
    const auto& selected_activation = selected_known_logic4
        ? state.known_activation : state.activation;
    std::uint32_t raw_status { };
    std::exception_ptr invocation_failure;
    if (!state.jit().resume_region_activation(selected_activation,
            selected_binding, selected_runtime, selected_frame, selected_result,
            raw_status, invocation_failure)) {
      state.last_failure = std::move(invocation_failure);
      return false;
    }
    state.last_body_selection
        = state.specialization
                == RegionKernelSpecialization::guarded_constant_inputs
            ? llvm_detail::RegionKernelBodySelection::guarded_constant_inputs
            : selected_known_logic4
                ? llvm_detail::RegionKernelBodySelection::known_logic4
                : llvm_detail::RegionKernelBodySelection::four_state;
    return state.finish_activation(image, selected_known_logic4, raw_status);
  } catch (...) {
    state.last_failure = std::current_exception();
    return false;
  }
}

bool LlvmRegionKernelExecutor::execute_internal_output_prefix(
    const runtime::simir::RegionKernelActivationImage& image,
    const std::span<const runtime::PackedLogic4> current_internal_values,
    const std::span<const runtime::simir::RegionConeOutputBinding>
        ordered_prefix) noexcept
{
  if (!impl_) {
    return false;
  }

  auto& state = *impl_;
  llvm_detail::RegionKernelInvocationGuard invocation { state.in_use };
  if (!invocation) {
    return false;
  }
  state.last_failure = { };
  if (!state.jit().matches_region_activation(state.activation,
          state.binding, state.runtime, state.frame, state.result)
      || (state.known_variant_ready
          && !state.jit().matches_region_activation(
              state.known_activation, state.known_binding,
              state.known_runtime, state.known_frame,
              state.known_result))) {
    return false;
  }

  try {
    if (!state.validate_internal_output_prefix(
            image, current_internal_values, ordered_prefix)) {
      return false;
    }

    // Prefix result storage describes the last completed prefix call. Retire
    // it immediately before activation preparation can mutate native scratch.
    state.output_prefix_result_valid = false;
    state.output_prefix_changed.clear();
    bool selected_known_logic4 { };
    if (!state.prepare_activation(image, { }, { }, selected_known_logic4,
            current_internal_values)) {
      return false;
    }
    auto& selected_binding = selected_known_logic4
        ? state.known_binding : state.binding;
    auto& selected_runtime = selected_known_logic4
        ? state.known_runtime : state.runtime;
    auto& selected_frame = selected_known_logic4
        ? state.known_frame : state.frame;
    auto& selected_result = selected_known_logic4
        ? state.known_result : state.result;
    const auto& selected_activation = selected_known_logic4
        ? state.known_activation : state.activation;
    std::uint32_t raw_status { };
    std::exception_ptr invocation_failure;
    if (!state.jit().resume_region_activation(selected_activation,
            selected_binding, selected_runtime, selected_frame, selected_result,
            raw_status, invocation_failure)) {
      state.last_failure = std::move(invocation_failure);
      return false;
    }
    state.last_body_selection
        = state.specialization
                == RegionKernelSpecialization::guarded_constant_inputs
            ? llvm_detail::RegionKernelBodySelection::guarded_constant_inputs
            : llvm_detail::RegionKernelBodySelection::four_state;
    if (!state.finish_activation(image, selected_known_logic4, raw_status)) {
      return false;
    }

    for (const auto& binding : ordered_prefix) {
      const auto output = std::ranges::find(state.kernel.outputs, binding);
      if (output == state.kernel.outputs.end()) {
        state.output_prefix_changed.clear();
        return false;
      }
      const auto output_index = static_cast<std::size_t>(
          output - state.kernel.outputs.begin());
      if (output_index >= state.generated_output_changed.size()) {
        state.output_prefix_changed.clear();
        return false;
      }
      state.output_prefix_changed.push_back(
          state.generated_output_changed[output_index]);
    }
    state.output_prefix_result_valid = true;
    return true;
  } catch (...) {
    state.output_prefix_result_valid = false;
    state.output_prefix_changed.clear();
    state.last_failure = std::current_exception();
    return false;
  }
}

bool LlvmRegionKernelExecutor::execute_internal_output_prefix_prepared(
    const runtime::simir::RegionKernelActivationImage& image,
    const std::span<const runtime::PackedLogic4> current_internal_values,
    const std::span<const runtime::simir::RegionConeOutputBinding>
        ordered_prefix,
    runtime::simir::RegionPreparedOutputBatchV1& outputs,
    runtime::simir::RegionPreparedOutputSuccessorMasksV1* const successors)
    noexcept
{
  if (!impl_) {
    return false;
  }

  auto& state = *impl_;
  llvm_detail::RegionKernelInvocationGuard invocation { state.in_use };
  if (!invocation) {
    return false;
  }
  state.last_failure = { };
  if (!state.prepared_output_activation
      || !state.jit().matches_region_prepared_output(
          state.prepared_output_activation, state.binding, state.runtime,
          state.frame, state.result)
      || !state.jit().matches_region_activation(state.activation,
          state.binding, state.runtime, state.frame, state.result)
      || (state.known_variant_ready
          && !state.jit().matches_region_activation(
              state.known_activation, state.known_binding,
              state.known_runtime, state.known_frame,
              state.known_result))
      || (successors != nullptr
          && !state.prepared_output_successor_masks_representable)) {
    return false;
  }

  try {
    if (!state.validate_internal_output_prefix(
            image, current_internal_values, ordered_prefix)
        || !state.validate_prepared_output_batch(
            image, outputs, current_internal_values, nullptr, successors)) {
      return false;
    }

    state.output_prefix_result_valid = false;
    state.output_prefix_changed.clear();
    bool selected_known_logic4 { };
    // The generated private output entry is paired with the four-state
    // activation body. Keep known-valued cuts on that body too; selecting a
    // different body and declining afterward would mutate the reusable frame
    // without completing the caller's requested publication.
    if (!state.prepare_activation(image, { }, { }, selected_known_logic4,
            current_internal_values, false)) {
      return false;
    }

    std::uint32_t raw_status { };
    std::exception_ptr invocation_failure;
    if (!state.jit().resume_region_prepared_output(
            state.prepared_output_activation, state.binding,
            state.runtime, state.frame, state.result, outputs, raw_status,
            successors, invocation_failure)) {
      state.last_failure = std::move(invocation_failure);
      return false;
    }
    state.last_body_selection
        = llvm_detail::RegionKernelBodySelection::four_state;
    if (!state.finish_activation(image, false, raw_status)) {
      return false;
    }

    for (const auto& binding : ordered_prefix) {
      const auto output = std::ranges::find(state.kernel.outputs, binding);
      if (output == state.kernel.outputs.end()) {
        state.output_prefix_changed.clear();
        return false;
      }
      const auto output_index = static_cast<std::size_t>(
          output - state.kernel.outputs.begin());
      const auto internal = std::ranges::lower_bound(
          state.kernel.internal_signals, binding.signal);
      if (output_index >= state.generated_output_changed.size()
          || internal == state.kernel.internal_signals.end()
          || *internal != binding.signal) {
        state.output_prefix_changed.clear();
        return false;
      }
      const auto internal_index = static_cast<std::size_t>(
          internal - state.kernel.internal_signals.begin());
      const auto& slot = outputs.slots[internal_index];
      if (slot.changed == nullptr || slot.value_ready == nullptr
          || slot.transaction_ready == nullptr
          || *slot.changed != state.generated_output_changed[output_index]
          || *slot.value_ready != *slot.changed
          || *slot.transaction_ready != 1U) {
        state.output_prefix_changed.clear();
        return false;
      }
      state.output_prefix_changed.push_back(*slot.changed);
    }
    if (successors != nullptr) {
      if (successors->slot_count != state.kernel.internal_signals.size()
          || state.prepared_output_successor_masks.size()
              != state.kernel.internal_signals.size()
          || successors->member_masks == nullptr) {
        state.output_prefix_changed.clear();
        return false;
      }
      for (std::size_t index = 0U;
           index < successors->slot_count; ++index) {
        const auto& descriptor = outputs.slots[index];
        std::uint8_t changed { };
        if (descriptor.selected > 1U
            || (descriptor.selected != 0U
                && (descriptor.changed == nullptr
                    || descriptor.value_ready == nullptr
                    || descriptor.transaction_ready == nullptr))) {
          state.output_prefix_changed.clear();
          return false;
        }
        if (descriptor.selected != 0U) {
          changed = *descriptor.changed;
          if (changed > 1U
              || *descriptor.value_ready != changed
              || *descriptor.transaction_ready != 1U) {
            state.output_prefix_changed.clear();
            return false;
          }
        }
        const auto expected = changed != 0U
            ? state.prepared_output_successor_masks[index] : 0U;
        if (successors->member_masks[index] != expected) {
          state.output_prefix_changed.clear();
          return false;
        }
      }
    }
    state.output_prefix_result_valid = true;
    return true;
  } catch (...) {
    state.output_prefix_result_valid = false;
    state.output_prefix_changed.clear();
    state.last_failure = std::current_exception();
    return false;
  }
}

bool LlvmRegionKernelExecutor::execute_direct_ready_window_prepared(
    const runtime::simir::RegionKernelActivationImage& image,
    const runtime::simir::RegionDirectReadyWindowV1& input_window,
    const std::span<const runtime::PackedLogic4> current_internal_values,
    const std::span<const runtime::simir::RegionConeOutputBinding>
        ordered_prefix,
    runtime::simir::RegionPreparedOutputBatchV1& outputs,
    runtime::simir::RegionPreparedOutputSuccessorMasksV1* const successors)
    noexcept
{
  if (!impl_) {
    return false;
  }

  auto& state = *impl_;
  llvm_detail::RegionKernelInvocationGuard invocation { state.in_use };
  if (!invocation) {
    return false;
  }
  state.last_failure = { };
  if (!state.direct_ready_output_activation
      || !state.prepared_output_activation
      || (successors != nullptr
          && (!state.prepared_output_successor_masks_representable
              || !state.direct_ready_output_activation.has_successor_masks()))
      || !state.jit().matches_region_direct_ready_output(
          state.direct_ready_output_activation, state.binding,
          state.runtime, state.frame, state.result)
      || !state.jit().matches_region_prepared_output(
          state.prepared_output_activation, state.binding, state.runtime,
          state.frame, state.result)
      || !state.jit().matches_region_activation(state.activation,
          state.binding, state.runtime, state.frame, state.result)) {
    return false;
  }

  try {
    if (!state.validate_direct_ready_window(image, input_window,
            current_internal_values, ordered_prefix, outputs, successors)) {
      return false;
    }

    state.output_prefix_result_valid = false;
    state.output_prefix_changed.clear();
    bool selected_known_logic4 { };
    if (!state.prepare_activation(image, { }, { }, selected_known_logic4,
            { }, false, true)) {
      return false;
    }

    std::uint32_t raw_status { };
    std::exception_ptr invocation_failure;
    if (!state.jit().resume_region_direct_ready_output(
            state.direct_ready_output_activation, state.binding,
            state.runtime, state.frame, state.result, input_window,
            outputs, successors, raw_status, invocation_failure)) {
      state.last_failure = std::move(invocation_failure);
      return false;
    }
    state.last_body_selection
        = llvm_detail::RegionKernelBodySelection::four_state;
    if (!state.finish_activation(image, false, raw_status)) {
      return false;
    }

    for (const auto& binding : ordered_prefix) {
      const auto output = std::ranges::find(state.kernel.outputs, binding);
      if (output == state.kernel.outputs.end()) {
        state.output_prefix_changed.clear();
        return false;
      }
      const auto output_index = static_cast<std::size_t>(
          output - state.kernel.outputs.begin());
      const auto internal = std::ranges::lower_bound(
          state.kernel.internal_signals, binding.signal);
      if (output_index >= state.generated_output_changed.size()
          || internal == state.kernel.internal_signals.end()
          || *internal != binding.signal) {
        state.output_prefix_changed.clear();
        return false;
      }
      const auto internal_index = static_cast<std::size_t>(
          internal - state.kernel.internal_signals.begin());
      const auto& slot = outputs.slots[internal_index];
      if (slot.changed == nullptr || slot.value_ready == nullptr
          || slot.transaction_ready == nullptr
          || *slot.changed != state.generated_output_changed[output_index]
          || *slot.value_ready != *slot.changed
          || *slot.transaction_ready != 1U) {
        state.output_prefix_changed.clear();
        return false;
      }
      state.output_prefix_changed.push_back(*slot.changed);
    }
    if (successors != nullptr) {
      if (successors->slot_count != state.kernel.internal_signals.size()
          || state.prepared_output_successor_masks.size()
              != state.kernel.internal_signals.size()
          || successors->member_masks == nullptr) {
        state.output_prefix_changed.clear();
        return false;
      }
      for (std::size_t index = 0U;
           index < successors->slot_count; ++index) {
        const auto& descriptor = outputs.slots[index];
        std::uint8_t changed { };
        if (descriptor.selected > 1U
            || (descriptor.selected != 0U
                && (descriptor.changed == nullptr
                    || descriptor.value_ready == nullptr
                    || descriptor.transaction_ready == nullptr))) {
          state.output_prefix_changed.clear();
          return false;
        }
        if (descriptor.selected != 0U) {
          changed = *descriptor.changed;
          if (changed > 1U
              || *descriptor.value_ready != changed
              || *descriptor.transaction_ready != 1U) {
            state.output_prefix_changed.clear();
            return false;
          }
        }
        const auto expected = changed != 0U
            ? state.prepared_output_successor_masks[index] : 0U;
        if (successors->member_masks[index] != expected) {
          state.output_prefix_changed.clear();
          return false;
        }
      }
    }
    state.output_prefix_result_valid = true;
    return true;
  } catch (...) {
    state.output_prefix_result_valid = false;
    state.output_prefix_changed.clear();
    state.last_failure = std::current_exception();
    return false;
  }
}

std::span<const std::uint8_t>
LlvmRegionKernelExecutor::internal_output_prefix_changed() const noexcept
{
  return impl_ != nullptr && impl_->output_prefix_result_valid
      ? std::span<const std::uint8_t> { impl_->output_prefix_changed }
      : std::span<const std::uint8_t> { };
}

bool LlvmRegionKernelExecutor::supports_direct_ready_window() const noexcept
{
  return impl_ != nullptr
      && static_cast<bool>(impl_->direct_ready_output_activation);
}

bool LlvmRegionKernelExecutor::
supports_direct_ready_window_successor_masks() const noexcept
{
  return impl_ != nullptr
      && impl_->prepared_output_successor_masks_representable
      && static_cast<bool>(impl_->direct_ready_output_activation)
      && impl_->direct_ready_output_activation.has_successor_masks();
}

std::span<const runtime::PackedLogic4>
LlvmRegionKernelExecutor::activation_registers() const noexcept
{
  return impl_ != nullptr
      ? std::span<const runtime::PackedLogic4> { impl_->register_values.data(),
            impl_->kernel.program.register_count }
      : std::span<const runtime::PackedLogic4> { };
}

std::span<const runtime::simir::RegionConeKernelMember>
LlvmRegionKernelExecutor::members() const noexcept
{
  return impl_
      ? std::span<const runtime::simir::RegionConeKernelMember> {
            impl_->kernel.members }
      : std::span<const runtime::simir::RegionConeKernelMember> { };
}

std::span<const runtime::simir::RegionConeOutputBinding>
LlvmRegionKernelExecutor::outputs() const noexcept
{
  return impl_
      ? std::span<const runtime::simir::RegionConeOutputBinding> {
            impl_->kernel.outputs }
      : std::span<const runtime::simir::RegionConeOutputBinding> { };
}

std::string_view LlvmRegionKernelExecutor::cache_identity() const noexcept
{
  return impl_ ? std::string_view { impl_->identity } : std::string_view { };
}

} // namespace fsim::compiler
