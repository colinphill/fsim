// SPDX-License-Identifier: Apache-2.0
#include "fsim/compiler/llvm_jit_region_kernel.hpp"
#include "fsim/runtime/simir_region_activation.hpp"
#include "fsim/runtime/simir_region_graph.hpp"
#include "allocation_profile.hpp"
#include "simir_region_activation_reference.hpp"
#include "../../src/compiler/llvm_jit_region_activation_internal.hpp"
#include "../../src/compiler/llvm_jit_internal.hpp"
#include "../../src/compiler/llvm_jit_lowering_internal.hpp"
#include "../../src/compiler/llvm_jit_native_body_registry_internal.hpp"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Operator.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;

static_assert(!std::is_copy_constructible_v<
    fsim::compiler::llvm_detail::RegionKernelActivationCertificate>);
static_assert(std::is_nothrow_move_constructible_v<
    fsim::compiler::llvm_detail::RegionKernelActivationCertificate>);
static_assert(std::is_nothrow_move_assignable_v<
    fsim::compiler::llvm_detail::RegionKernelActivationCertificate>);

void require(const bool condition, const std::string_view message)
{
  if (!condition) {
    throw std::runtime_error { std::string { message } };
  }
}

void test_region_activation_guard()
{
  fsim::compiler::llvm_detail::RegionKernelActivationCertificate empty;
  require(!static_cast<bool>(empty),
      "an unbound region activation cannot authorize native entry");
  auto moved_empty = std::move(empty);
  require(!static_cast<bool>(empty) && !static_cast<bool>(moved_empty),
      "moving an empty certificate does not create an activation proof");

  std::atomic_flag in_use = ATOMIC_FLAG_INIT;
  std::uint32_t scratch_generation { };
  {
    fsim::compiler::llvm_detail::RegionKernelInvocationGuard outer { in_use };
    require(static_cast<bool>(outer),
        "the first region activation owns the reusable scratch");
    scratch_generation = 1U;
    {
      fsim::compiler::llvm_detail::RegionKernelInvocationGuard reentrant {
        in_use };
      require(!static_cast<bool>(reentrant),
          "a concurrent or reentrant activation declines before scratch reuse");
      require(scratch_generation == 1U,
          "the rejected activation leaves active invocation scratch untouched");
    }
  }
  {
    fsim::compiler::llvm_detail::RegionKernelInvocationGuard retry { in_use };
    require(static_cast<bool>(retry),
        "the guard releases scratch after the prior activation exits");
    scratch_generation = 2U;
  }
  require(scratch_generation == 2U,
      "a later activation can reuse the released scratch");
}

[[nodiscard]] RegionConeActivationKernel make_logic4_kernel()
{
  RegionConeActivationKernel kernel;
  kernel.program.name = "region_native_two_member_fixture";
  kernel.program.scheduling_domain = ProcessSchedulingDomain::systemverilog;
  kernel.program.register_count = 10U;
  kernel.program.register_value_kinds.assign(10U, ValueKind::logic4);
  kernel.program.operations = {
      Branch { 2U, 1U, 5U, UnknownBranchPolicy::error },
      DebugPoint { DebugPointKind::statement,
          SourceLocation { "region-native.sv", 10U, 2U }, "left.scope" },
      UnaryNot { 4U, 0U },
      CopyRegister { 8U, 4U },
      UnaryNot { 4U, 1U },
      Branch { 3U, 6U, 10U, UnknownBranchPolicy::error },
      DebugPoint { DebugPointKind::statement,
          SourceLocation { "region-native.sv", 20U, 3U }, "right.scope" },
      UnaryNot { 6U, 1U },
      CopyRegister { 9U, 6U },
      UnaryNot { 6U, 0U },
      Halt { },
  };
  kernel.inputs = {
      RegionConeKernelInput { 0U, 0U, 4U, ValueKind::logic4, true },
      RegionConeKernelInput { 1U, 1U, 4U, ValueKind::logic4, false },
  };

  RegionConeKernelMember left;
  left.process = 10U;
  left.readiness_register = 2U;
  left.branch_instruction = 0U;
  left.begin = 1U;
  left.end = 5U;
  left.final_debug_state = RegionConeFinalDebugState {
      SourceLocation { "region-native.sv", 10U, 2U }, "left.scope" };
  left.register_bindings = {
      RegionConeKernelRegisterBinding { 0U, 4U, true, 4U, ValueKind::logic4 },
      RegionConeKernelRegisterBinding { 1U, 5U, false, 0U, ValueKind::logic4 },
  };

  RegionConeKernelMember right;
  right.process = 12U;
  right.readiness_register = 3U;
  right.branch_instruction = 5U;
  right.begin = 6U;
  right.end = 10U;
  right.final_debug_state = RegionConeFinalDebugState {
          SourceLocation { "region-native.sv", 20U, 3U }, "right.scope" };
  right.register_bindings = {
      RegionConeKernelRegisterBinding { 0U, 6U, true, 4U, ValueKind::logic4 },
      RegionConeKernelRegisterBinding { 1U, 7U, false, 0U, ValueKind::logic4 },
  };
  kernel.members = { std::move(left), std::move(right) };
  kernel.outputs = {
      RegionConeOutputBinding { 10U, 20U, 0U, 4U, ValueKind::logic4,
          SignalUpdateDomain::systemverilog_active, 8U, 1U, 1U, 3U },
      RegionConeOutputBinding { 12U, 21U, 0U, 4U, ValueKind::logic4,
          SignalUpdateDomain::systemverilog_active, 9U, 1U, 1U, 8U },
  };
  return kernel;
}

[[nodiscard]] RegionKernelActivationImage make_logic4_image(
    const std::uint64_t generation,
    const std::span<const std::size_t> active_members,
    const PackedLogic4& input_a,
    const PackedLogic4& input_b)
{
  RegionKernelActivationImage image;
  image.generation = generation;
  image.active_member_indices.assign(
      active_members.begin(), active_members.end());
  image.ready_processes.reserve(active_members.size());
  image.requests.reserve(active_members.size());
  image.scheduler_prefix.frontier_generation = generation + 100U;
  image.scheduler_prefix.frontier_cursor = 0U;
  image.scheduler_prefix.frontier_end = active_members.size();
  image.scheduler_prefix.time = 0U;
  image.scheduler_prefix.delta = generation - 1U;
  image.scheduler_prefix.phase = SchedulerPhase::active;
  image.scheduler_prefix.systemverilog_round = generation + 200U;
  image.scheduler_prefix.tasks.reserve(active_members.size());

  const std::array processes { ProcessId { 10U }, ProcessId { 12U } };
  for (std::size_t ordinal = 0U; ordinal < active_members.size(); ++ordinal) {
    const auto member_index = active_members[ordinal];
    require(member_index < processes.size(), "fixture member index is valid");
    const auto process = processes[member_index];
    RegionKernelReadyMember request;
    request.process = process;
    request.origin = RegionKernelActivationOrigin {
        ProcessSchedulingDomain::systemverilog,
        SchedulerPhase::active,
        0U,
        generation - 1U,
        static_cast<StableOrder>(100U + process),
        static_cast<std::uint64_t>(200U + process),
        generation + 200U,
    };
    image.ready_processes.push_back(process);
    image.requests.push_back(request);
    image.scheduler_prefix.tasks.push_back({ ordinal, request });
  }

  image.register_inputs = {
      RegionKernelRegisterInput { 0U, input_a },
      RegionKernelRegisterInput { 1U, input_b },
      RegionKernelRegisterInput { 2U,
          PackedLogic4 { 1U,
              std::ranges::find(active_members, 0U) != active_members.end()
                  ? Logic4::one
                  : Logic4::zero } },
      RegionKernelRegisterInput { 3U,
          PackedLogic4 { 1U,
              std::ranges::find(active_members, 1U) != active_members.end()
                  ? Logic4::one
                  : Logic4::zero } },
  };
  return image;
}

[[nodiscard]] std::pair<RegionConeActivationKernel,
    RegionKernelActivationImage> make_builder_activation_kernel()
{
  std::vector<RegionSignalDescriptor> signals {
      { 4U }, { 4U }, { 4U }, { 4U },
  };
  signals[2U].observations = RegionObservation::current;
  signals[3U].observations = RegionObservation::current;

  Process producer;
  producer.id = 0U;
  producer.name = "builder_region_producer";
  producer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
  producer.register_count = 1U;
  producer.static_sensitivity = { { 0U, EdgeKind::any } };
  producer.driver_regions = {
      { 1U, 0U, 0U, true }, { 2U, 0U, 0U, true },
  };
  producer.operations = {
      ReadSignal { 0U, 0U },
      DebugPoint { DebugPointKind::statement,
          SourceLocation { "builder-region.sv", 10U, 4U },
          "producer.scope" },
      WriteUpdate { 1U, 0U, SignalUpdateDomain::systemverilog_active },
      LoadConstant { 0U, PackedLogic4::from_msb_string("1010") },
      WriteUpdate { 2U, 0U, SignalUpdateDomain::systemverilog_active },
      LoadConstant { 0U, PackedLogic4::from_msb_string("0101") },
      WaitSensitivity { },
      Jump { 0U },
  };

  Process consumer;
  consumer.id = 1U;
  consumer.name = "builder_region_consumer";
  consumer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
  consumer.register_count = 2U;
  consumer.register_value_kinds = {
      ValueKind::logic4, ValueKind::logic4,
  };
  consumer.static_sensitivity = { { 1U, EdgeKind::any } };
  consumer.driver_regions = { { 3U, 0U, 0U, true } };
  consumer.operations = {
      ReadSignal { 0U, 1U },
      UnaryNot { 1U, 0U },
      DebugPoint { DebugPointKind::statement,
          SourceLocation { "builder-region.sv", 20U, 6U },
          "consumer.scope" },
      WriteUpdate { 3U, 1U, SignalUpdateDomain::systemverilog_active },
      LoadConstant { 1U, PackedLogic4::from_msb_string("0011") },
      WaitSensitivity { },
      Jump { 0U },
  };

  std::vector<Process> processes;
  processes.push_back(std::move(producer));
  processes.push_back(std::move(consumer));
  std::vector<const Process*> process_bindings;
  process_bindings.reserve(processes.size());
  for (const auto& process : processes) {
    process_bindings.push_back(&process);
  }
  const auto graph = RegionGraph::build(process_bindings, signals);
  const auto& components = graph.certificate_inventory().components;
  require(components.size() == 1U
          && components.front().members
              == std::vector<ProcessId> { 0U, 1U }
          && components.front().structural_internal_signal_candidates
              == std::vector<SignalId> { 1U },
      "builder fixture has one producer-consumer internal signal");
  const auto program = graph.build_compute_program(0U, process_bindings);
  require(program.has_value(),
      "real RegionGraph construction accepts the native wrapper fixture");

  auto kernel = program->activation_kernel;
  for (const auto& output : kernel.outputs) {
    const auto owner = std::ranges::find(kernel.members, output.owner,
        &RegionConeKernelMember::process);
    require(owner != kernel.members.end()
            && output.kernel_instruction >= owner->begin
            && output.kernel_instruction < owner->end,
        "builder output snapshots belong to their original owner segment");
    for (const auto& member : kernel.members) {
      for (const auto& binding : member.register_bindings) {
        require(binding.activation_register != output.value_register,
            "builder output snapshots use registers distinct from member state");
      }
    }
  }
  RegionKernelActivationImage image;
  image.generation = 1U;
  image.active_member_indices.resize(kernel.members.size());
  for (std::size_t member_index = 0U;
       member_index < kernel.members.size(); ++member_index) {
    image.active_member_indices[member_index] = member_index;
  }
  image.scheduler_prefix.frontier_generation = 1U;
  image.scheduler_prefix.frontier_cursor = 0U;
  image.scheduler_prefix.frontier_end = kernel.members.size();
  image.scheduler_prefix.time = 0U;
  image.scheduler_prefix.delta = 0U;
  image.scheduler_prefix.phase = SchedulerPhase::active;
  image.scheduler_prefix.systemverilog_round = 1U;
  for (std::size_t member_index = 0U;
       member_index < kernel.members.size(); ++member_index) {
    const auto process = kernel.members[member_index].process;
    const RegionKernelReadyMember request {
        process,
        Process::full_static_trigger_mask,
        RegionKernelActivationOrigin {
            ProcessSchedulingDomain::systemverilog,
            SchedulerPhase::active,
            0U,
            0U,
            static_cast<StableOrder>(100U + process),
            static_cast<std::uint64_t>(200U + process),
            1U,
        },
    };
    image.ready_processes.push_back(process);
    image.requests.push_back(request);
    image.scheduler_prefix.tasks.push_back({ member_index, request });
  }

  std::vector<std::pair<RegisterId, PackedLogic4>> inputs;
  inputs.reserve(kernel.inputs.size() + kernel.members.size());
  for (const auto& input : kernel.inputs) {
    require(input.signal <= 1U,
        "builder fixture has an expected kernel signal input");
    auto value = input.signal == 0U
        ? PackedLogic4::from_msb_string("1001")
        : PackedLogic4::from_msb_string("0Z1X");
    inputs.emplace_back(input.value_register, std::move(value));
  }
  for (const auto& member : kernel.members) {
    inputs.emplace_back(member.readiness_register,
        PackedLogic4 { 1U, Logic4::one });
  }
  std::ranges::sort(inputs, std::ranges::less { },
      [](const auto& entry) { return entry.first; });
  image.register_inputs.reserve(inputs.size());
  for (auto& [register_id, value] : inputs) {
    image.register_inputs.push_back({ register_id, std::move(value) });
  }
  return { std::move(kernel), std::move(image) };
}

void remap_region_kernel_physical_ids(
    RegionConeActivationKernel& kernel,
    SignalId signal_delta, ProcessId process_delta);

[[nodiscard]] std::pair<RegionConeActivationKernel,
    RegionKernelActivationImage> make_many_reader_activation_kernel(
    const std::size_t reader_count)
{
  require(reader_count > 0U && reader_count <= 65U,
      "successor-mask fixture covers the compact bound and first overflow");
  constexpr SignalId reader_output_base { 3U };
  std::vector<RegionSignalDescriptor> signals;
  signals.reserve(reader_count + 3U);
  for (std::size_t index = 0U; index < reader_count + 3U; ++index) {
    signals.emplace_back(RegionSignalDescriptor { 1U });
    if (index == 2U || index >= reader_output_base) {
      signals.back().observations = RegionObservation::current;
    }
  }

  Process producer;
  constexpr ProcessId producer_process { 17U };
  constexpr ProcessId reader_process_base { producer_process + 1U };
  // RegionGraph requires dense source identities. Remap the certified kernel
  // below to keep native mask ordinals distinct from physical process IDs.
  producer.id = 0U;
  producer.name = "successor_mask_producer";
  producer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
  producer.register_count = 2U;
  producer.register_value_kinds.assign(2U, ValueKind::logic4);
  producer.static_sensitivity = { { 0U, EdgeKind::any } };
  producer.driver_regions = {
      { 1U, 0U, 0U, true }, { 2U, 0U, 0U, true },
  };
  producer.operations = {
      ReadSignal { 0U, 0U },
      UnaryNot { 1U, 0U },
      WriteUpdate { 1U, 1U, SignalUpdateDomain::systemverilog_active },
      WriteUpdate { 2U, 1U, SignalUpdateDomain::systemverilog_active },
      WaitSensitivity { },
      Jump { 0U },
  };

  std::vector<Process> processes;
  processes.reserve(reader_count + 1U);
  processes.push_back(std::move(producer));
  for (std::size_t index = 0U; index < reader_count; ++index) {
    const auto process_id = static_cast<ProcessId>(index + 1U);
    const auto output_signal = static_cast<SignalId>(
        reader_output_base + static_cast<SignalId>(index));
    Process reader;
    reader.id = process_id;
    reader.name = "successor_mask_reader_" + std::to_string(index);
    reader.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    reader.register_count = 2U;
    reader.register_value_kinds.assign(2U, ValueKind::logic4);
    reader.static_sensitivity = { { 1U, EdgeKind::any } };
    reader.driver_regions = { { output_signal, 0U, 0U, true } };
    reader.operations = {
        ReadSignal { 0U, 1U },
        UnaryNot { 1U, 0U },
        WriteUpdate { output_signal, 1U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };
    processes.push_back(std::move(reader));
  }

  std::vector<const Process*> process_bindings;
  process_bindings.reserve(processes.size());
  for (const auto& process : processes) {
    process_bindings.push_back(&process);
  }
  const auto graph = RegionGraph::build(process_bindings, signals);
  const auto& components = graph.certificate_inventory().components;
  require(components.size() == 1U
          && components.front().members.size() == reader_count + 1U
          && components.front().structural_internal_signal_candidates
              == std::vector<SignalId> { 1U },
      "many-reader graph keeps one certified whole-signal fanout");
  const auto program = graph.build_compute_program(0U, process_bindings);
  require(program.has_value(),
      "many-reader graph produces one activation kernel");

  auto kernel = program->activation_kernel;
  remap_region_kernel_physical_ids(kernel, 0U, producer_process);
  require(kernel.internal_signals == std::vector<SignalId> { 1U }
          && kernel.members.size() == reader_count + 1U,
      "many-reader kernel preserves exact signal and member order");
  require(kernel.members.front().process == producer_process,
      "the output owner has a nonzero process identity");
  for (std::size_t index = 0U; index < reader_count; ++index) {
    require(kernel.members[index + 1U].process
            == reader_process_base + static_cast<ProcessId>(index),
        "reader-mask ordinals follow kernel order instead of physical IDs");
  }
  RegionKernelActivationImage image;
  image.generation = 1U;
  image.active_member_indices.resize(kernel.members.size());
  image.scheduler_prefix.frontier_generation = 1U;
  image.scheduler_prefix.frontier_cursor = 0U;
  image.scheduler_prefix.frontier_end = kernel.members.size();
  image.scheduler_prefix.time = 0U;
  image.scheduler_prefix.delta = 0U;
  image.scheduler_prefix.phase = SchedulerPhase::active;
  image.scheduler_prefix.systemverilog_round = 1U;
  for (std::size_t index = 0U; index < kernel.members.size(); ++index) {
    image.active_member_indices[index] = index;
    const auto process = kernel.members[index].process;
    const RegionKernelReadyMember request {
        process,
        Process::full_static_trigger_mask,
        RegionKernelActivationOrigin {
            ProcessSchedulingDomain::systemverilog,
            SchedulerPhase::active,
            0U,
            0U,
            static_cast<StableOrder>(100U + process),
            static_cast<std::uint64_t>(200U + process),
            1U,
        },
    };
    image.ready_processes.push_back(process);
    image.requests.push_back(request);
    image.scheduler_prefix.tasks.push_back({ index, request });
  }

  std::vector<std::pair<RegisterId, PackedLogic4>> register_inputs;
  register_inputs.reserve(kernel.inputs.size() + kernel.members.size());
  for (const auto& input : kernel.inputs) {
    require(input.signal <= 1U,
        "many-reader kernel has only boundary or committed internal inputs");
    register_inputs.emplace_back(input.value_register,
        PackedLogic4 { 1U, Logic4::zero });
  }
  for (const auto& member : kernel.members) {
    register_inputs.emplace_back(member.readiness_register,
        PackedLogic4 { 1U, Logic4::one });
  }
  std::ranges::sort(register_inputs, std::ranges::less { },
      [](const auto& item) { return item.first; });
  for (auto& [register_id, value] : register_inputs) {
    image.register_inputs.push_back({ register_id, std::move(value) });
  }
  return { std::move(kernel), std::move(image) };
}

[[nodiscard]] RegionConeActivationKernel
make_vhdl_projected_activation_kernel()
{
  std::vector<RegionSignalDescriptor> signals { { 1U }, { 1U }, { 1U } };
  signals[2U].observations = RegionObservation::current;

  Process producer;
  producer.id = 0U;
  producer.name = "vhdl_projected_native_producer";
  producer.language_standard = "2008";
  producer.scheduling_domain = ProcessSchedulingDomain::generic;
  producer.register_count = 1U;
  producer.static_sensitivity = { { 0U, EdgeKind::any } };
  producer.driver_regions = { { 1U, 0U, 0U, true } };
  producer.operations = {
      ReadSignal { 0U, 0U },
      WriteProjected { 1U, 0U, 0U, 0U, ProjectedDelayMode::inertial },
      WaitSensitivity { },
      Jump { 0U },
  };

  Process consumer;
  consumer.id = 1U;
  consumer.name = "vhdl_projected_native_consumer";
  consumer.language_standard = "2008";
  consumer.scheduling_domain = ProcessSchedulingDomain::generic;
  consumer.register_count = 2U;
  consumer.static_sensitivity = { { 1U, EdgeKind::any } };
  consumer.driver_regions = { { 2U, 0U, 0U, true } };
  consumer.operations = {
      ReadSignal { 0U, 1U },
      UnaryNot { 1U, 0U },
      WriteProjected { 2U, 1U, 0U, 0U, ProjectedDelayMode::inertial },
      WaitSensitivity { },
      Jump { 0U },
  };

  std::vector<const Process*> programs { &producer, &consumer };
  const auto graph = RegionGraph::build(programs, signals);
  require(graph.certificate_inventory().components.size() == 1U,
      "VHDL projected fixture forms a separate generic-domain component");
  const auto program = graph.build_compute_program(0U, programs);
  require(program.has_value()
          && program->activation_kernel.program.scheduling_domain
              == ProcessSchedulingDomain::generic,
      "the generic-domain builder produces a native VHDL activation kernel");
  for (const auto& output : program->activation_kernel.outputs) {
    require(output.update_kind == RegionUpdateKind::vhdl_projected
            && output.domain == SignalUpdateDomain::generic
            && output.projected_mode == ProjectedDelayMode::inertial
            && output.projected_delay == 0U
            && output.projected_rejection == 0U,
        "VHDL native output bindings retain the exact projected contract");
  }
  return program->activation_kernel;
}

[[nodiscard]] RegionConeActivationKernel
make_generic_update_slice_activation_kernel(const std::uint32_t width)
{
  const auto slice_offset = width >= 65U ? 63U : width - 2U;
  constexpr std::uint32_t slice_width = 2U;
  std::vector<RegionSignalDescriptor> signals(4U, { width });
  signals[2U].observations = RegionObservation::current;
  signals[3U].observations = RegionObservation::current;

  Process producer;
  producer.id = 0U;
  producer.name = "generic_update_native_producer";
  producer.scheduling_domain = ProcessSchedulingDomain::generic;
  producer.register_count = 1U;
  producer.register_value_kinds = { ValueKind::logic4 };
  producer.static_sensitivity = { { 0U, EdgeKind::any } };
  producer.driver_regions = { { 1U, 0U, width, true } };
  producer.operations = { ReadSignal { 0U, 0U },
      WriteUpdate { 1U, 0U }, WaitSensitivity { }, Jump { 0U } };

  Process consumer;
  consumer.id = 1U;
  consumer.name = "generic_update_slice_native_consumer";
  consumer.scheduling_domain = ProcessSchedulingDomain::generic;
  consumer.register_count = 3U;
  consumer.register_value_kinds = { ValueKind::logic4,
      ValueKind::logic4, ValueKind::logic4 };
  consumer.static_sensitivity = { { 1U, EdgeKind::any } };
  consumer.driver_regions = { { 2U, 0U, width, true },
      { 3U, slice_offset, slice_width, false } };
  consumer.operations = { ReadSignal { 0U, 1U }, UnaryNot { 1U, 0U },
      Extract { 2U, 1U, slice_offset, slice_width },
      WriteUpdate { 2U, 1U },
      WriteUpdateSlice { 3U, 2U, slice_offset },
      WaitSensitivity { }, Jump { 0U } };

  const std::vector<const Process*> processes { &producer, &consumer };
  const auto graph = RegionGraph::build(processes, signals);
  require(graph.certificate_inventory().components.size() == 1U,
      "generic Update/UpdateSlice fixture forms one generic component");
  const auto program = graph.build_compute_program(0U, processes);
  require(program.has_value()
          && program->activation_kernel.program.scheduling_domain
              == ProcessSchedulingDomain::generic
          && program->activation_kernel.outputs.size() == 3U,
      "generic compute-program output bindings retain all three owner writes");
  for (const auto& output : program->activation_kernel.outputs) {
    require(output.update_kind == RegionUpdateKind::generic
            && output.domain == SignalUpdateDomain::generic,
        "generic whole and sliced outputs retain their ordinary Update contract");
  }
  require(std::ranges::any_of(program->activation_kernel.outputs,
              [slice_offset](const RegionConeOutputBinding& output) {
                  return output.signal == 3U
                      && output.offset == slice_offset
                      && output.width == 2U
                      && output.owner == 1U;
              }),
      "generic UpdateSlice keeps its exact owner and bit interval");
  return program->activation_kernel;
}

[[nodiscard]] std::optional<RegionConeActivationKernel>
try_make_generic_conditional_select_activation_kernel(
    const std::uint32_t width, const ValueKind value_kind,
    const std::uint32_t condition_width, const std::uint32_t false_width,
    bool* has_expected_two_member_component = nullptr)
{
  std::vector<RegionSignalDescriptor> signals {
      { width }, { width }, { false_width }, { condition_width }, { width },
  };
  signals[0U].value_kind = value_kind;
  signals[1U].value_kind = value_kind;
  signals[2U].value_kind = value_kind;
  signals[4U].value_kind = value_kind;
  signals[4U].observations = RegionObservation::current;

  Process producer;
  producer.id = 0U;
  producer.name = "generic_conditional_select_producer";
  producer.scheduling_domain = ProcessSchedulingDomain::generic;
  producer.register_count = 1U;
  producer.register_value_kinds = { value_kind };
  producer.static_sensitivity = { { 0U, EdgeKind::any } };
  producer.driver_regions = { { 1U, 0U, width, true } };
  producer.operations = { ReadSignal { 0U, 0U },
      WriteUpdate { 1U, 0U }, WaitSensitivity { }, Jump { 0U } };

  Process consumer;
  consumer.id = 1U;
  consumer.name = "generic_conditional_select_consumer";
  consumer.scheduling_domain = ProcessSchedulingDomain::generic;
  consumer.register_count = 4U;
  consumer.register_value_kinds = {
      value_kind, value_kind, ValueKind::logic4, value_kind };
  consumer.static_sensitivity = {
      { 1U, EdgeKind::any }, { 2U, EdgeKind::any },
      { 3U, EdgeKind::any },
  };
  consumer.driver_regions = { { 4U, 0U, width, true } };
  consumer.operations = { ReadSignal { 0U, 1U },
      ReadSignal { 1U, 2U }, ReadSignal { 2U, 3U },
      ConditionalSelect { 3U, 2U, 0U, 1U }, WriteUpdate { 4U, 3U },
      WaitSensitivity { }, Jump { 0U } };

  const std::vector<const Process*> processes { &producer, &consumer };
  const auto graph = RegionGraph::build(processes, signals);
  const auto& components = graph.certificate_inventory().components;
  const bool expected_two_member_component = components.size() == 1U
      && components.front().members
          == std::vector<ProcessId> { ProcessId { 0U }, ProcessId { 1U } };
  if (has_expected_two_member_component != nullptr) {
    *has_expected_two_member_component = expected_two_member_component;
  }
  if (!expected_two_member_component) {
    return std::nullopt;
  }
  const auto program = graph.build_compute_program(0U, processes);
  if (!program) {
    return std::nullopt;
  }
  return program->activation_kernel;
}

[[nodiscard]] PackedLogic4 make_generic_logic9_update_value(
    const std::uint32_t width, const std::uint32_t phase)
{
  constexpr std::string_view states { "UX01ZWLH-" };
  std::string text;
  text.reserve(width);
  for (std::uint32_t bit = 0U; bit < width; ++bit) {
    text.push_back(states[(bit + phase) % states.size()]);
  }
  return PackedLogic4::from_logic9_msb_string(text);
}

[[nodiscard]] RegionConeActivationKernel
make_generic_logic9_update_activation_kernel()
{
  constexpr std::uint32_t width = 65U;
  constexpr std::uint32_t slice_offset = 63U;
  constexpr std::uint32_t slice_width = 2U;
  std::vector<RegionSignalDescriptor> signals(4U, { width });
  for (auto& signal : signals) {
    signal.value_kind = ValueKind::logic9;
  }
  signals[2U].observations = RegionObservation::current;
  signals[3U].observations = RegionObservation::current;

  Process producer;
  producer.id = 0U;
  producer.name = "generic_logic9_update_producer";
  producer.scheduling_domain = ProcessSchedulingDomain::generic;
  producer.register_count = 1U;
  producer.register_value_kinds = { ValueKind::logic9 };
  producer.static_sensitivity = { { 0U, EdgeKind::any } };
  producer.driver_regions = { { 1U, 0U, width, true } };
  producer.operations = { ReadSignal { 0U, 0U },
      WriteUpdate { 1U, 0U }, WaitSensitivity { }, Jump { 0U } };

  Process consumer;
  consumer.id = 1U;
  consumer.name = "generic_logic9_update_consumer";
  consumer.scheduling_domain = ProcessSchedulingDomain::generic;
  consumer.register_count = 3U;
  consumer.register_value_kinds = {
      ValueKind::logic9, ValueKind::logic9, ValueKind::logic9 };
  consumer.static_sensitivity = { { 1U, EdgeKind::any } };
  consumer.driver_regions = {
      { 2U, 0U, width, true }, { 3U, slice_offset, slice_width, false } };
  consumer.operations = { ReadSignal { 0U, 1U },
      Extract { 1U, 0U, slice_offset, slice_width }, CopyRegister { 2U, 0U },
      WriteUpdate { 2U, 2U }, WriteUpdateSlice { 3U, 1U, slice_offset },
      WaitSensitivity { }, Jump { 0U } };

  const std::vector<const Process*> processes { &producer, &consumer };
  const auto graph = RegionGraph::build(processes, signals);
  require(graph.certificate_inventory().components.size() == 1U,
      "generic Logic9 whole and slice updates form one component");
  const auto program = graph.build_compute_program(0U, processes);
  require(program.has_value()
          && program->activation_kernel.outputs.size() == 3U,
      "generic Logic9 compute program retains all original owner outputs");
  for (const auto& output : program->activation_kernel.outputs) {
    require(output.update_kind == RegionUpdateKind::generic
            && output.domain == SignalUpdateDomain::generic
            && output.value_kind == ValueKind::logic9,
        "Logic9 outputs retain the generic Update kind and value type");
  }
  require(std::ranges::any_of(program->activation_kernel.outputs,
              [=](const RegionConeOutputBinding& output) {
                  return output.signal == 3U && output.owner == 1U
                      && output.offset == slice_offset
                      && output.width == slice_width;
              }),
      "generic Logic9 UpdateSlice retains its cross-word owner range");
  return program->activation_kernel;
}

void compare_defined_member_registers(
    const RegionConeActivationKernel& kernel,
    const RegionKernelActivationImage& image,
    std::span<const PackedLogic4> expected,
    std::span<const PackedLogic4> actual);

[[nodiscard]] std::string check_vhdl_projected_native_matches_reference(
    const fsim::compiler::JitOptimizationLevel optimization)
{
  auto kernel = make_vhdl_projected_activation_kernel();
  fsim::compiler::LlvmJitOptions options;
  options.optimization = optimization;
  options.cache_directory.clear();
  auto executor = fsim::compiler::LlvmRegionKernelExecutor::try_create(
      kernel, options, "region-native-vhdl-projected-fixture-v1");
  require(executor != nullptr,
      "the LLVM region provider accepts the exact generic projected kernel");
  const auto identity = std::string { executor->cache_identity() };

  const std::array internal_seed {
      RegionKernelInternalSeed { 1U, 0U,
          PackedLogic4::from_msb_string("1"),
          PackedLogic4::from_msb_string("1"),
          PackedLogic4::from_msb_string("1") },
  };
  RegionKernelActivationState activation { kernel, internal_seed };
  const auto make_prefix = [](const std::uint64_t generation,
                              const std::uint64_t delta,
                              const std::span<const ProcessId> ready) {
    RegionKernelSchedulerPrefix prefix;
    prefix.frontier_generation = generation;
    prefix.frontier_cursor = 0U;
    prefix.frontier_end = ready.size();
    prefix.time = 1U;
    prefix.delta = delta;
    prefix.phase = SchedulerPhase::active;
    prefix.process_domain = ProcessSchedulingDomain::generic;
    prefix.tasks.reserve(ready.size());
    for (std::size_t index = 0U; index < ready.size(); ++index) {
      prefix.tasks.push_back({ index,
          RegionKernelReadyMember { ready[index],
              Process::full_static_trigger_mask,
              RegionKernelActivationOrigin {
                  ProcessSchedulingDomain::generic,
                  SchedulerPhase::active, 1U, delta,
                  static_cast<StableOrder>(index + 1U),
                  static_cast<std::uint64_t>(generation * 10U + index), 0U } } });
    }
    return prefix;
  };
  const std::array boundary_inputs { PackedLogic4::from_msb_string("0") };
  constexpr std::array both_ready { ProcessId { 0U }, ProcessId { 1U } };
  const auto first_prefix = make_prefix(1U, 0U, both_ready);
  const auto& first_image = activation.begin_direct_wave_reusable(
      first_prefix, boundary_inputs);
  require(first_image.register_inputs.empty(),
      "a direct activation begins without copied input values");
  const auto first_generation = first_image.generation;
  const std::array invalid_boundary_inputs {
      PackedLogic4::from_msb_string("00") };
  bool rejected_shape { };
  try {
    activation.materialize_wave_inputs(invalid_boundary_inputs);
  } catch (const std::invalid_argument&) {
    rejected_shape = true;
  }
  require(rejected_shape && first_image.register_inputs.empty()
          && first_image.generation == first_generation
          && first_image.ready_processes
              == std::vector<ProcessId> { 0U, 1U },
      "failed input materialization clears partial values and retains the active frontier for retry");
  activation.materialize_wave_inputs(boundary_inputs);
  const auto expected_first = evaluate_region_activation_kernel_reference(
      kernel, first_image);
  require(executor->execute(first_image),
      "generic VHDL projected activation executes through the native backend");
  const auto first_registers = executor->activation_registers();
  compare_defined_member_registers(
      kernel, first_image, expected_first, first_registers);
  for (const auto& output : kernel.outputs) {
    require(first_registers[output.value_register]
            == expected_first[output.value_register],
        "native VHDL snapshots match the checked activation reference");
  }
  const auto internal_output = std::ranges::find(kernel.outputs, 1U,
      &RegionConeOutputBinding::signal);
  const auto boundary_output = std::ranges::find(kernel.outputs, 2U,
      &RegionConeOutputBinding::signal);
  require(internal_output != kernel.outputs.end()
          && boundary_output != kernel.outputs.end()
          && first_registers[internal_output->value_register]
              == PackedLogic4::from_msb_string("0")
          && first_registers[boundary_output->value_register]
              == PackedLogic4::from_msb_string("0"),
      "same-cycle native consumers use committed internal input, not producer output");
  activation.stage_kernel_outputs(first_image, first_registers);
  const auto first_publications = activation.pending_publications();
  for (std::size_t index = 0U; index < first_publications.size(); ++index) {
    activation.begin_raw_publication(index);
    activation.publish_current(index, first_publications[index].value);
  }
  activation.complete_wave();

  constexpr std::array consumer_ready { ProcessId { 1U } };
  const auto next_prefix = make_prefix(2U, 1U, consumer_ready);
  const auto& next_image = activation.begin_wave_reusable(
      next_prefix, boundary_inputs);
  const auto expected_next = evaluate_region_activation_kernel_reference(
      kernel, next_image);
  require(executor->execute(next_image),
      "a later generic VHDL frontier reuses the native activation backend");
  const auto next_registers = executor->activation_registers();
  compare_defined_member_registers(
      kernel, next_image, expected_next, next_registers);
  require(next_registers[boundary_output->value_register]
              == PackedLogic4::from_msb_string("1")
          && executor->cache_identity() == identity,
      "next-cycle projected input is visible with stable native mapping identity");
  activation.discard_wave();
  return identity;
}

[[nodiscard]] std::string check_generic_update_slice_native_matches_reference(
    const fsim::compiler::JitOptimizationLevel optimization)
{
  constexpr std::uint32_t width = 65U;
  auto kernel = make_generic_update_slice_activation_kernel(width);
  fsim::compiler::LlvmJitOptions options;
  options.optimization = optimization;
  options.cache_directory.clear();
  auto executor = fsim::compiler::LlvmRegionKernelExecutor::try_create(
      kernel, options, "region-native-generic-update-slice-v1");
  require(executor != nullptr,
      "the LLVM region provider accepts generic Update and UpdateSlice outputs");
  const auto identity = std::string { executor->cache_identity() };

  PackedLogic4 input(width, Logic4::zero);
  input.set(63U, Logic4::one);
  input.set(64U, Logic4::x);
  const auto old_parent = PackedLogic4(width, Logic4::zero);
  const std::array internal_seed {
      RegionKernelInternalSeed { 1U, 0U,
          old_parent, old_parent, old_parent },
  };
  RegionKernelActivationState activation { kernel, internal_seed };
  const auto make_prefix = [](const std::uint64_t generation,
                              const std::uint64_t delta,
                              const std::span<const ProcessId> ready) {
    RegionKernelSchedulerPrefix prefix;
    prefix.frontier_generation = generation;
    prefix.frontier_cursor = 0U;
    prefix.frontier_end = ready.size();
    prefix.time = 1U;
    prefix.delta = delta;
    prefix.phase = SchedulerPhase::active;
    prefix.process_domain = ProcessSchedulingDomain::generic;
    prefix.tasks.reserve(ready.size());
    for (std::size_t index = 0U; index < ready.size(); ++index) {
      prefix.tasks.push_back({ index,
          RegionKernelReadyMember { ready[index],
              Process::full_static_trigger_mask,
              RegionKernelActivationOrigin {
                  ProcessSchedulingDomain::generic,
                  SchedulerPhase::active, 1U, delta,
                  static_cast<StableOrder>(index + 1U),
                  static_cast<std::uint64_t>(generation * 10U + index), 0U } } });
    }
    return prefix;
  };
  const std::array boundary_inputs { input };
  constexpr std::array both_ready { ProcessId { 0U }, ProcessId { 1U } };
  const auto first_prefix = make_prefix(1U, 0U, both_ready);
  const auto& first_image = activation.begin_wave_reusable(
      first_prefix, boundary_inputs);
  const auto expected_first = evaluate_region_activation_kernel_reference(
      kernel, first_image);
  require(executor->execute(first_image),
      "generic Update and UpdateSlice execute through the O0/O2 native backend");
  const auto first_registers = executor->activation_registers();
  compare_defined_member_registers(
      kernel, first_image, expected_first, first_registers);
  const auto parent_binding = std::ranges::find(kernel.outputs, 1U,
      &RegionConeOutputBinding::signal);
  const auto slice_binding = std::ranges::find(kernel.outputs, 3U,
      &RegionConeOutputBinding::signal);
  require(parent_binding != kernel.outputs.end()
          && slice_binding != kernel.outputs.end()
          && first_registers[parent_binding->value_register] == input
          && first_registers[slice_binding->value_register]
              == PackedLogic4::from_msb_string("11"),
      "native generic writes preserve full parent value and the old cross-word slice");
  activation.stage_kernel_outputs(first_image, first_registers);
  const auto first_publications = activation.pending_publications();
  for (std::size_t index = 0U; index < first_publications.size(); ++index) {
    activation.begin_raw_publication(index);
    activation.publish_current(index, first_publications[index].value);
  }
  activation.complete_wave();

  constexpr std::array consumer_ready { ProcessId { 1U } };
  const auto next_prefix = make_prefix(2U, 1U, consumer_ready);
  const auto& next_image = activation.begin_wave_reusable(
      next_prefix, boundary_inputs);
  const auto expected_next = evaluate_region_activation_kernel_reference(
      kernel, next_image);
  require(executor->execute(next_image),
      "a later generic child Update reuses the same native kernel");
  const auto next_registers = executor->activation_registers();
  compare_defined_member_registers(
      kernel, next_image, expected_next, next_registers);
  require(next_registers[slice_binding->value_register]
          == PackedLogic4::from_msb_string("X0")
          && executor->cache_identity() == identity,
      "later generic UpdateSlice observes the committed parent with stable identity");
  activation.discard_wave();
  return identity;
}

[[nodiscard]] std::string check_generic_logic9_update_native_matches_reference(
    const fsim::compiler::JitOptimizationLevel optimization)
{
  constexpr std::uint32_t width = 65U;
  constexpr std::uint32_t slice_offset = 63U;
  auto kernel = make_generic_logic9_update_activation_kernel();
  fsim::compiler::LlvmJitOptions options;
  options.optimization = optimization;
  options.cache_directory.clear();
  const auto old_parent = PackedLogic4::from_logic9_msb_string(
      std::string(width, '0'));
  const std::array internal_seed {
      RegionKernelInternalSeed { 1U, 0U,
          old_parent, old_parent, old_parent },
  };
  RegionKernelActivationState valid_activation { kernel, internal_seed };
  static_cast<void>(valid_activation);
  auto partial_internal_output = kernel;
  const auto internal_output = std::ranges::find_if(
      partial_internal_output.outputs,
      [&](const RegionConeOutputBinding& output) {
        return std::ranges::find(partial_internal_output.internal_signals,
                   output.signal)
            != partial_internal_output.internal_signals.end();
      });
  require(internal_output != partial_internal_output.outputs.end(),
      "generic update fixture has a whole internal output binding");
  internal_output->offset = 1U;
  auto activation_rejected { false };
  try {
    RegionKernelActivationState invalid_activation {
        partial_internal_output, internal_seed };
    static_cast<void>(invalid_activation);
  } catch (const std::invalid_argument&) {
    activation_rejected = true;
  }
  require(activation_rejected,
      "generic internal outputs cannot use a partial activation offset");
  require(fsim::compiler::LlvmRegionKernelExecutor::try_create(
              partial_internal_output, options,
              "region-native-generic-logic9-update-v1") == nullptr,
      "native validation rejects partial internal generic outputs");
  auto executor = fsim::compiler::LlvmRegionKernelExecutor::try_create(
      kernel, options, "region-native-generic-logic9-update-v1");
  require(executor != nullptr,
      "LLVM accepts 65-bit generic Logic9 whole and sliced updates");
  const auto identity = std::string { executor->cache_identity() };

  const auto input = make_generic_logic9_update_value(width, 4U);
  constexpr std::string_view states { "UX01ZWLH-" };
  for (const auto state : states) {
    require(input.to_msb_string().find(state) != std::string::npos,
        "wide generic Logic9 input exercises all nine canonical states");
  }
  require(input.extract_bits(slice_offset, 2U).to_msb_string() == "ZW",
      "the generic Logic9 slice crosses the word boundary with Z/W values");
  RegionKernelActivationState activation { kernel, internal_seed };
  const auto make_prefix = [](const std::uint64_t generation,
                              const std::uint64_t delta,
                              const std::span<const ProcessId> ready) {
    RegionKernelSchedulerPrefix prefix;
    prefix.frontier_generation = generation;
    prefix.frontier_cursor = 0U;
    prefix.frontier_end = ready.size();
    prefix.time = 1U;
    prefix.delta = delta;
    prefix.phase = SchedulerPhase::active;
    prefix.process_domain = ProcessSchedulingDomain::generic;
    prefix.tasks.reserve(ready.size());
    for (std::size_t index = 0U; index < ready.size(); ++index) {
      prefix.tasks.push_back({ index,
          RegionKernelReadyMember { ready[index],
              Process::full_static_trigger_mask,
              RegionKernelActivationOrigin {
                  ProcessSchedulingDomain::generic,
                  SchedulerPhase::active, 1U, delta,
                  static_cast<StableOrder>(index + 1U),
                  static_cast<std::uint64_t>(generation * 10U + index), 0U } } });
    }
    return prefix;
  };
  const std::array boundary_inputs { input };
  constexpr std::array both_ready { ProcessId { 0U }, ProcessId { 1U } };
  const auto first_prefix = make_prefix(1U, 0U, both_ready);
  const auto& first_image = activation.begin_wave_reusable(
      first_prefix, boundary_inputs);
  const auto expected_first = evaluate_region_activation_kernel_reference(
      kernel, first_image);
  require(executor->execute(first_image),
      "O0/O2 executes wide generic Logic9 whole and slice updates");
  const auto first_registers = executor->activation_registers();
  compare_defined_member_registers(
      kernel, first_image, expected_first, first_registers);
  const auto parent_binding = std::ranges::find(kernel.outputs, 1U,
      &RegionConeOutputBinding::signal);
  const auto whole_binding = std::ranges::find(kernel.outputs, 2U,
      &RegionConeOutputBinding::signal);
  const auto slice_binding = std::ranges::find(kernel.outputs, 3U,
      &RegionConeOutputBinding::signal);
  require(parent_binding != kernel.outputs.end()
          && whole_binding != kernel.outputs.end()
          && slice_binding != kernel.outputs.end()
          && first_registers[parent_binding->value_register] == input
          && first_registers[whole_binding->value_register] == old_parent
          && first_registers[slice_binding->value_register]
              == old_parent.extract_bits(slice_offset, 2U),
      "first generic Logic9 wave retains old internal input and full source");
  activation.stage_kernel_outputs(first_image, first_registers);
  const auto first_publications = activation.pending_publications();
  for (std::size_t index = 0U; index < first_publications.size(); ++index) {
    activation.begin_raw_publication(index);
    activation.publish_current(index, first_publications[index].value);
  }
  activation.complete_wave();

  constexpr std::array consumer_ready { ProcessId { 1U } };
  const auto next_prefix = make_prefix(2U, 1U, consumer_ready);
  const auto& next_image = activation.begin_wave_reusable(
      next_prefix, boundary_inputs);
  const auto expected_next = evaluate_region_activation_kernel_reference(
      kernel, next_image);
  require(executor->execute(next_image),
      "the later generic Logic9 child reuses the O0/O2 native entry");
  const auto next_registers = executor->activation_registers();
  compare_defined_member_registers(
      kernel, next_image, expected_next, next_registers);
  require(next_registers[whole_binding->value_register] == input
          && next_registers[slice_binding->value_register]
              == input.extract_bits(slice_offset, 2U)
          && next_registers[slice_binding->value_register].is_logic9()
          && executor->cache_identity() == identity,
      "generic Logic9 whole and cross-word slice preserve all typed planes");
  activation.discard_wave();
  return identity;
}

[[nodiscard]] PackedLogic4 expected_conditional_select(
    const PackedLogic4& condition, const PackedLogic4& when_true,
    const PackedLogic4& when_false)
{
  require(condition.width() == 1U
          && when_true.width() == when_false.width(),
      "the independent conditional-select oracle receives a valid shape");
  if (condition.get(0U) == Logic4::one) {
    return when_true;
  }
  if (condition.get(0U) == Logic4::zero) {
    return when_false;
  }
  PackedLogic4 result(when_true.width(), Logic4::x);
  for (std::uint32_t bit = 0U; bit < when_true.width(); ++bit) {
    if (when_true.get(bit) == when_false.get(bit)) {
      result.set(bit, when_true.get(bit));
    }
  }
  return result;
}

[[nodiscard]] std::pair<PackedLogic4, PackedLogic4>
make_equal_conditional_arms(const std::uint32_t width,
    const std::size_t phase)
{
  constexpr std::array states {
      Logic4::zero, Logic4::one, Logic4::x, Logic4::z,
  };
  PackedLogic4 value(width, Logic4::zero);
  for (std::uint32_t bit = 0U; bit < width; ++bit) {
    value.set(bit, states[(bit + phase) % states.size()]);
  }
  return { value, value };
}

[[nodiscard]] std::pair<PackedLogic4, PackedLogic4>
make_differing_conditional_arms(const std::uint32_t width)
{
  auto [when_true, when_false] = make_equal_conditional_arms(width, 2U);
  when_true.set(width - 1U, Logic4::z);
  when_false.set(width - 1U, Logic4::one);
  if (width > 1U) {
    when_true.set(1U, Logic4::one);
    when_false.set(1U, Logic4::zero);
  }
  if (width > 64U) {
    when_true.set(63U, Logic4::x);
    when_false.set(63U, Logic4::z);
  }
  if (width > 65U) {
    when_true.set(64U, Logic4::one);
    when_false.set(64U, Logic4::zero);
  }
  return { std::move(when_true), std::move(when_false) };
}

[[nodiscard]] RegionKernelSchedulerPrefix
make_conditional_select_prefix(const std::uint64_t generation)
{
  constexpr std::array processes { ProcessId { 0U }, ProcessId { 1U } };
  RegionKernelSchedulerPrefix prefix;
  prefix.frontier_generation = generation;
  prefix.frontier_cursor = 0U;
  prefix.frontier_end = processes.size();
  prefix.time = 1U;
  prefix.delta = generation;
  prefix.phase = SchedulerPhase::active;
  prefix.process_domain = ProcessSchedulingDomain::generic;
  prefix.tasks.reserve(processes.size());
  for (std::size_t index = 0U; index < processes.size(); ++index) {
    const auto process = processes[index];
    prefix.tasks.push_back({ index,
        RegionKernelReadyMember { process,
            Process::full_static_trigger_mask,
            RegionKernelActivationOrigin {
                ProcessSchedulingDomain::generic,
                SchedulerPhase::active,
                prefix.time,
                prefix.delta,
                static_cast<StableOrder>(process) + 1U,
                generation * 10U + index,
                0U } } });
  }
  return prefix;
}

void retag_conditional_select_kernel_logic9(
    RegionConeActivationKernel& kernel)
{
  constexpr SignalId condition_signal = 3U;
  for (auto& input : kernel.inputs) {
    input.value_kind = input.signal == condition_signal
        ? ValueKind::logic4 : ValueKind::logic9;
    kernel.program.register_value_kinds[input.value_register]
        = input.value_kind;
  }
  for (auto& member : kernel.members) {
    for (auto& binding : member.register_bindings) {
      binding.value_kind = member.process == 1U
              && binding.source_register == 2U
          ? ValueKind::logic4 : ValueKind::logic9;
      kernel.program.register_value_kinds[binding.activation_register]
          = binding.value_kind;
    }
  }
  for (auto& output : kernel.outputs) {
    output.value_kind = ValueKind::logic9;
    kernel.program.register_value_kinds[output.value_register]
        = ValueKind::logic9;
  }
}

[[nodiscard]] std::string check_generic_conditional_select_native_matches_reference(
    const fsim::compiler::JitOptimizationLevel optimization)
{
  bool logic9_two_member_component { };
  bool nonscalar_condition_two_member_component { };
  bool mismatched_arm_two_member_component { };
  const auto invalid_logic9_graph
      = try_make_generic_conditional_select_activation_kernel(65U,
          ValueKind::logic9, 1U, 65U, &logic9_two_member_component);
  const auto invalid_condition_graph
      = try_make_generic_conditional_select_activation_kernel(65U,
          ValueKind::logic4, 2U, 65U, &nonscalar_condition_two_member_component);
  const auto invalid_arm_width_graph
      = try_make_generic_conditional_select_activation_kernel(65U,
          ValueKind::logic4, 1U, 64U, &mismatched_arm_two_member_component);
  require(logic9_two_member_component && !invalid_logic9_graph,
      "the structurally valid two-member Logic9 graph declines the unsupported select");
  require(!nonscalar_condition_two_member_component
          && !invalid_condition_graph,
      "a nonscalar condition is rejected before a two-member region is formed");
  require(!mismatched_arm_two_member_component && !invalid_arm_width_graph,
      "mismatched select arms are rejected before a two-member region is formed");

  const auto maybe_kernel = try_make_generic_conditional_select_activation_kernel(
      65U, ValueKind::logic4, 1U, 65U);
  require(maybe_kernel.has_value(),
      "matching four-state ConditionalSelect operands form a generic region");
  auto logic9_kernel = *maybe_kernel;
  retag_conditional_select_kernel_logic9(logic9_kernel);
  fsim::compiler::LlvmJitOptions options;
  options.optimization = optimization;
  options.cache_directory.clear();
  auto logic9_copy_kernel = logic9_kernel;
  bool replaced_select { };
  for (std::size_t index = 0U;
       index < logic9_copy_kernel.program.operations.size(); ++index) {
    const auto operation
        = logic9_copy_kernel.program.operations.expanded(index);
    const auto* const select = operation_get_if<ConditionalSelect>(&operation);
    if (select == nullptr) {
      continue;
    }
    logic9_copy_kernel.program.operations.replace(index,
        CopyRegister { select->destination, select->when_true });
    replaced_select = true;
    break;
  }
  require(replaced_select,
      "the Logic9 control kernel has exactly the select instruction under test");
  const auto logic9_copy_executor
      = fsim::compiler::LlvmRegionKernelExecutor::try_create(
          logic9_copy_kernel, options,
          "region-native-conditional-select-logic9-copy-control-v1");
  require(logic9_copy_executor != nullptr,
      "the same manual Logic9 kernel compiles when only select is replaced by copy");
  require(fsim::compiler::LlvmRegionKernelExecutor::try_create(
              logic9_kernel, options,
              "region-native-conditional-select-logic9-decline-v1")
              == nullptr,
      "the compiler declines a manually assembled Logic9 ConditionalSelect kernel");

  std::string first_identity;
  for (const auto width : { 1U, 65U, 129U }) {
    auto maybe_width_kernel
        = try_make_generic_conditional_select_activation_kernel(width,
            ValueKind::logic4, 1U, width);
    require(maybe_width_kernel.has_value(),
        "matching Logic4 ConditionalSelect operands build at every test width");
    auto kernel = std::move(*maybe_width_kernel);
    bool has_select_operation { };
    for (std::size_t index = 0U;
         index < kernel.program.operations.size(); ++index) {
      const auto operation = kernel.program.operations.expanded(index);
      has_select_operation = has_select_operation
          || operation_holds<ConditionalSelect>(operation);
    }
    const auto selected_output = std::ranges::find(kernel.outputs, 4U,
        &RegionConeOutputBinding::signal);
    require(has_select_operation
            && selected_output != kernel.outputs.end()
            && selected_output->owner == 1U
            && selected_output->offset == 0U
            && selected_output->width == width
            && selected_output->value_kind == ValueKind::logic4
            && selected_output->update_kind == RegionUpdateKind::generic,
        "the admitted kernel retains its select instruction and original output binding");

    const auto condition_input = std::ranges::find(kernel.inputs, 3U,
        &RegionConeKernelInput::signal);
    require(condition_input != kernel.inputs.end()
            && condition_input->width == 1U
            && condition_input->value_kind == ValueKind::logic4
            && !condition_input->internal,
        "ConditionalSelect maps a scalar Logic4 boundary condition");
    auto executor = fsim::compiler::LlvmRegionKernelExecutor::try_create(
        kernel, options,
        "region-native-conditional-select-width-" + std::to_string(width));
    require(executor != nullptr,
        "O0/O2 LLVM region entry accepts Logic4 ConditionalSelect widths");
    if (first_identity.empty()) {
      first_identity = std::string { executor->cache_identity() };
    }

    const auto run_case = [&](const Logic4 condition,
                              const PackedLogic4& when_true,
                              const PackedLogic4& when_false,
                              const std::uint64_t generation) {
      const PackedLogic4 source(width, Logic4::zero);
      const std::array internal_seed {
          RegionKernelInternalSeed { 1U, 0U,
              when_true, when_true, when_true },
      };
      RegionKernelActivationState activation { kernel, internal_seed };
      const auto prefix = make_conditional_select_prefix(generation);
      std::vector<PackedLogic4> boundary_inputs;
      boundary_inputs.reserve(3U);
      for (const auto& input : kernel.inputs) {
        if (input.internal) {
          continue;
        }
        if (input.signal == 0U) {
          boundary_inputs.push_back(source);
        } else if (input.signal == 2U) {
          boundary_inputs.push_back(when_false);
        } else if (input.signal == 3U) {
          boundary_inputs.emplace_back(1U, condition);
        } else {
          require(false,
              "conditional fixture has only the three expected boundary inputs");
        }
      }
      const auto& image = activation.begin_wave_reusable(
          prefix, boundary_inputs);
      const auto expected = evaluate_region_activation_kernel_reference(
          kernel, image);
      require(executor->execute(image),
          "O0/O2 ConditionalSelect completes through the native region entry");
      const auto actual = executor->activation_registers();
      compare_defined_member_registers(kernel, image, expected, actual);
      const auto expected_value = expected_conditional_select(
          PackedLogic4 { 1U, condition }, when_true, when_false);
      require(actual[selected_output->value_register] == expected_value
              && expected[selected_output->value_register] == expected_value,
          "native ConditionalSelect matches both the reference and four-state bit oracle");
      if (width == 1U) {
        const auto known_logic4 = [](const PackedLogic4& value) {
          return !value.is_logic9()
              && std::ranges::all_of(value.bval_words(),
                  [](const std::uint64_t word) { return word == 0U; });
        };
        const bool select_is_known
            = (condition == Logic4::zero || condition == Logic4::one)
            && known_logic4(when_true) && known_logic4(when_false);
        const auto expected_body = select_is_known
            ? fsim::compiler::llvm_detail::RegionKernelBodySelection::known_logic4
            : fsim::compiler::llvm_detail::RegionKernelBodySelection::four_state;
        require(fsim::compiler::llvm_detail::RegionKernelTestAccess::
                    last_body_selection(*executor) == expected_body,
            "the known-L4 select body requires known scalar and arm values");
      }
      activation.discard_wave();
    };

    std::uint64_t generation = 1U;
    constexpr std::array all_conditions {
        Logic4::zero, Logic4::one, Logic4::x, Logic4::z,
    };
    for (const auto condition : all_conditions) {
      for (std::size_t phase = 0U; phase < 4U; ++phase) {
        const auto [when_true, when_false]
            = make_equal_conditional_arms(width, phase);
        run_case(condition, when_true, when_false, generation++);
      }
    }
    const auto [differing_true, differing_false]
        = make_differing_conditional_arms(width);
    for (const auto condition : all_conditions) {
      run_case(condition, differing_true, differing_false, generation++);
    }
  }
  return first_identity;
}

void compare_defined_member_registers(
    const RegionConeActivationKernel& kernel,
    const RegionKernelActivationImage& image,
    const std::span<const PackedLogic4> expected,
    const std::span<const PackedLogic4> actual)
{
  require(actual.size() == kernel.program.register_count,
      "native wrapper retains a stable register vector matching the kernel");
  for (std::size_t member_index = 0U;
       member_index < kernel.members.size(); ++member_index) {
    const auto& member = kernel.members[member_index];
    const bool active = std::ranges::binary_search(
        image.active_member_indices, member_index);
    for (const auto& binding : member.register_bindings) {
      if (!binding.defined) {
        continue;
      }
      const auto& actual_value = actual[binding.activation_register];
      if (active) {
        require(actual_value == expected[binding.activation_register],
            "native member registers match the checked C++ reference");
      } else {
        require(actual_value.width() == binding.width
                && actual_value.to_msb_string()
                    == std::string(binding.width, 'X'),
            "an inactive member does not retain a prior activation's value");
      }
    }
  }
}

struct RegionKernelAllocationCase {
  RegionKernelActivationImage image;
  std::vector<PackedLogic4> expected;
};

struct RegionKernelExecuteMeasurement {
  bool completed { };
  fsim::app::allocation_profile::Snapshot delta;
};

class AllocationProfileCaptureGuard final {
public:
  AllocationProfileCaptureGuard() noexcept
      : previous_enabled_(fsim::app::allocation_profile::exchange_enabled(true))
  {
  }

  ~AllocationProfileCaptureGuard()
  {
    static_cast<void>(
        fsim::app::allocation_profile::exchange_enabled(previous_enabled_));
  }

  AllocationProfileCaptureGuard(const AllocationProfileCaptureGuard&) = delete;
  AllocationProfileCaptureGuard& operator=(
      const AllocationProfileCaptureGuard&) = delete;

private:
  bool previous_enabled_ { };
};

void set_register_input(
    RegionKernelActivationImage& image,
    const RegisterId register_id,
    PackedLogic4 value)
{
  const auto input = std::ranges::find(image.register_inputs, register_id,
      &RegionKernelRegisterInput::register_id);
  require(input != image.register_inputs.end(),
      "steady-state fixture finds the requested input register");
  input->value = std::move(value);
}

[[nodiscard]] RegionKernelAllocationCase make_region_allocation_case(
    const RegionConeActivationKernel& kernel,
    const RegionKernelActivationImage& base_image,
    const std::uint64_t generation,
    const std::span<const std::size_t> active_members,
    const PackedLogic4& boundary_input,
    const PackedLogic4& committed_internal_input)
{
  RegionKernelActivationImage image = base_image;
  image.generation = generation;
  image.active_member_indices.assign(
      active_members.begin(), active_members.end());
  image.ready_processes.clear();
  image.requests.clear();
  image.scheduler_prefix.frontier_generation = generation + 100U;
  image.scheduler_prefix.frontier_cursor = 0U;
  image.scheduler_prefix.frontier_end = active_members.size();
  image.scheduler_prefix.time = 0U;
  image.scheduler_prefix.delta = generation - 1U;
  image.scheduler_prefix.phase = SchedulerPhase::active;
  image.scheduler_prefix.systemverilog_round = generation + 200U;
  image.scheduler_prefix.tasks.clear();

  for (std::size_t ordinal = 0U; ordinal < active_members.size(); ++ordinal) {
    const auto member_index = active_members[ordinal];
    require(member_index < kernel.members.size(),
        "steady-state fixture selects an existing member");
    const auto process = kernel.members[member_index].process;
    const RegionKernelReadyMember request {
        process,
        Process::full_static_trigger_mask,
        RegionKernelActivationOrigin {
            ProcessSchedulingDomain::systemverilog,
            SchedulerPhase::active,
            0U,
            generation - 1U,
            static_cast<StableOrder>(100U + process),
            static_cast<std::uint64_t>(200U + process),
            generation + 200U,
        },
    };
    image.ready_processes.push_back(process);
    image.requests.push_back(request);
    image.scheduler_prefix.tasks.push_back({ ordinal, request });
  }

  for (const auto& input : kernel.inputs) {
    const auto value = input.signal == 0U
        ? boundary_input
        : committed_internal_input;
    set_register_input(image, input.value_register, value);
  }
  for (std::size_t member_index = 0U;
       member_index < kernel.members.size(); ++member_index) {
    const auto ready = std::ranges::find(active_members, member_index)
        != active_members.end();
    set_register_input(image, kernel.members[member_index].readiness_register,
        PackedLogic4 { 1U, ready ? Logic4::one : Logic4::zero });
  }

  auto expected = evaluate_region_activation_kernel_reference(kernel, image);
  return { std::move(image), std::move(expected) };
}

[[nodiscard]] RegionKernelExecuteMeasurement execute_with_allocation_capture(
    fsim::compiler::LlvmRegionKernelExecutor& executor,
    const RegionKernelActivationImage& image) noexcept
{
  AllocationProfileCaptureGuard allocation_capture;
  const auto before = fsim::app::allocation_profile::snapshot();
  const bool completed = executor.execute(image);
  const auto after = fsim::app::allocation_profile::snapshot();
  return {
      completed,
      fsim::app::allocation_profile::Snapshot {
          after.requests - before.requests,
          after.requested_bytes - before.requested_bytes,
          after.failures - before.failures,
      },
  };
}

void require_zero_allocation_delta(
    const RegionKernelExecuteMeasurement& measurement)
{
  require(measurement.delta.requests == 0U
          && measurement.delta.requested_bytes == 0U
          && measurement.delta.failures == 0U,
      "a warmed native region execute makes no intercepted C++ allocation requests");
}

void compare_region_allocation_case(
    const RegionConeActivationKernel& kernel,
    const RegionKernelAllocationCase& activation,
    const fsim::compiler::LlvmRegionKernelExecutor& executor)
{
  const auto actual = executor.activation_registers();
  compare_defined_member_registers(
      kernel, activation.image, activation.expected, actual);
  for (const auto& output : kernel.outputs) {
    if (std::ranges::find(activation.image.ready_processes, output.owner)
        == activation.image.ready_processes.end()) {
      continue;
    }
    require(actual[output.value_register]
            == activation.expected[output.value_register],
        "each warmed native output snapshot matches the checked C++ reference");
  }
}

[[nodiscard]] std::string check_logic4_native_matches_reference(
    const fsim::compiler::JitOptimizationLevel optimization)
{
  auto kernel = make_logic4_kernel();
  fsim::compiler::LlvmJitOptions options;
  options.optimization = optimization;
  options.cache_directory.clear();
  auto executor = fsim::compiler::LlvmRegionKernelExecutor::try_create(
      kernel, options, "region-native-logic4-fixture-v1");
  require(executor != nullptr,
      "supported narrow Logic4 activation kernels create a native entry");
  const auto output_bindings = executor->outputs();
  const auto member_metadata = executor->members();
  require(output_bindings.size() == 2U
          && output_bindings[0U].owner == 10U
          && output_bindings[0U].signal == 20U
          && output_bindings[1U].owner == 12U
          && output_bindings[1U].signal == 21U
          && member_metadata.size() == 2U
          && member_metadata[0U].final_debug_state
          && member_metadata[0U].final_debug_state->scope == "left.scope"
          && member_metadata[1U].final_debug_state
          && member_metadata[1U].final_debug_state->scope == "right.scope",
      "native executor retains original drivers and final source/scope metadata");
  const auto identity = std::string { executor->cache_identity() };
  auto different_body = make_logic4_kernel();
  different_body.program.operations.replace(2U, UnaryNot { 4U, 1U });
  auto different_executor = fsim::compiler::LlvmRegionKernelExecutor::try_create(
      different_body, options, "region-native-logic4-fixture-v1");
  require(different_executor != nullptr
          && different_executor->cache_identity() != identity,
      "a changed operation body cannot alias the same region cache identity");
  const auto* const register_storage = executor->activation_registers().data();

  const auto compare_activation = [&](const std::uint64_t generation,
                                      const std::span<const std::size_t> active,
                                      const PackedLogic4& input_a,
                                      const PackedLogic4& input_b) {
    const auto image = make_logic4_image(
        generation, active, input_a, input_b);
    const auto expected = evaluate_region_activation_kernel_reference(
        kernel, image);
    const auto& first_input = image.register_inputs[0U];
    const std::array aval_words { first_input.value.aval_words()[0U] };
    const std::array bval_words { first_input.value.bval_words()[0U] };
    const RegionKernelLogic4InputPlane borrowed_input {
        kernel.inputs[0U].signal, kernel.inputs[0U].value_register,
        kernel.inputs[0U].width, aval_words, bval_words };
    require(executor->execute_with_logic4_input_planes(image,
                std::span<const RegionKernelLogic4InputPlane> {
                    &borrowed_input, 1U }),
        "native activation completes from borrowed current Logic4 planes");
    compare_defined_member_registers(kernel, image, expected,
        executor->activation_registers());
    for (const auto& output : kernel.outputs) {
      if (std::ranges::find(image.ready_processes, output.owner)
          == image.ready_processes.end()) {
        continue;
      }
      require(executor->activation_registers()[output.value_register]
              == expected[output.value_register],
          "native publication snapshot matches the checked reference at its write");
    }
    require(executor->activation_registers().data() == register_storage
            && executor->cache_identity() == identity,
        "steady activations reuse persistent output storage and identity");
  };

  constexpr std::array both { std::size_t { 0U }, std::size_t { 1U } };
  constexpr std::array left_only { std::size_t { 0U } };
  constexpr std::array right_only { std::size_t { 1U } };
  const auto input_a = PackedLogic4::from_msb_string("0ZX1");
  const auto input_b = PackedLogic4::from_msb_string("1XZ0");
  auto unknown_readiness = make_logic4_image(1U, both, input_a, input_b);
  unknown_readiness.register_inputs[2U].value
      = PackedLogic4 { 1U, Logic4::x };
  require(!executor->execute(unknown_readiness),
      "an unknown readiness bit declines before entering the native frame");

  auto nonexistent_member = make_logic4_image(
      1U, both, input_a, input_b);
  nonexistent_member.ready_processes[1U] = 11U;
  nonexistent_member.requests[1U].process = 11U;
  nonexistent_member.requests[1U].origin.stable_order = 111U;
  nonexistent_member.requests[1U].origin.sequence = 211U;
  nonexistent_member.scheduler_prefix.tasks[1U].member
      = nonexistent_member.requests[1U];
  require(!executor->execute(nonexistent_member),
      "a process ID between real members does not alias the next member");

  auto duplicate_member = make_logic4_image(1U, both, input_a, input_b);
  duplicate_member.ready_processes[1U] = 10U;
  duplicate_member.requests[1U].process = 10U;
  duplicate_member.requests[1U].origin.stable_order = 110U;
  duplicate_member.requests[1U].origin.sequence = 212U;
  duplicate_member.scheduler_prefix.tasks[1U].member
      = duplicate_member.requests[1U];
  require(!executor->execute(duplicate_member),
      "duplicate requests cannot omit a different active member");

  auto rejected_image = make_logic4_image(1U, both, input_a, input_b);
  const std::vector<PackedLogic4> prior_registers(
      executor->activation_registers().begin(),
      executor->activation_registers().end());
  const std::array mismatched_aval {
      rejected_image.register_inputs[0U].value.aval_words()[0U] ^ 1U };
  const std::array matching_bval {
      rejected_image.register_inputs[0U].value.bval_words()[0U] };
  const RegionKernelLogic4InputPlane mismatched_input {
      kernel.inputs[0U].signal, kernel.inputs[0U].value_register,
      kernel.inputs[0U].width, mismatched_aval, matching_bval };
  require(!executor->execute_with_logic4_input_planes(rejected_image,
              std::span<const RegionKernelLogic4InputPlane> {
                  &mismatched_input, 1U })
          && std::ranges::equal(prior_registers,
              executor->activation_registers()),
      "a borrowed plane that differs from the captured image declines before frame mutation");
  const std::array malformed_aval { UINT64_C(1) << 63U };
  const RegionKernelLogic4InputPlane malformed_tail {
      kernel.inputs[0U].signal, kernel.inputs[0U].value_register,
      kernel.inputs[0U].width, malformed_aval, matching_bval };
  require(!executor->execute_with_logic4_input_planes(rejected_image,
              std::span<const RegionKernelLogic4InputPlane> {
                  &malformed_tail, 1U })
          && std::ranges::equal(prior_registers,
              executor->activation_registers()),
      "malformed borrowed plane tails decline without changing persistent registers");
  require(executor->execute(rejected_image),
      "the checked image entry remains available after plane rejection");

  auto moved_executor
      = std::make_unique<fsim::compiler::LlvmRegionKernelExecutor>(
          std::move(*executor));
  executor = std::move(moved_executor);
  require(executor != nullptr,
      "moving the public wrapper preserves its heap-owned certificate");

  compare_activation(1U, both, input_a, input_b);
  // Repeated same-value activations must still set every active direct slot.
  compare_activation(2U, both, input_a, input_b);
  compare_activation(3U, right_only,
      PackedLogic4::from_msb_string("XX01"),
      PackedLogic4::from_msb_string("ZX10"));
  compare_activation(4U, left_only,
      PackedLogic4::from_msb_string("101Z"),
      PackedLogic4::from_msb_string("0X01"));
  return identity;
}

void check_add_unsigned_native_matches_reference(
    const fsim::compiler::JitOptimizationLevel optimization)
{
  auto kernel = make_logic4_kernel();
  kernel.program.operations.replace(2U,
      Binary { BinaryOperator::add_unsigned, 4U, 0U, 1U });
  fsim::compiler::LlvmJitOptions options;
  options.optimization = optimization;
  options.cache_directory.clear();
  auto executor = fsim::compiler::LlvmRegionKernelExecutor::try_create(
      kernel, options, "region-native-logic4-add-fixture-v1");
  require(executor != nullptr,
      "equal-width Logic4 add_unsigned is admitted by the native region compiler");

  constexpr std::array active { std::size_t { 0U } };
  const auto compare_case = [&](const std::uint64_t generation,
                                const std::string_view lhs,
                                const std::string_view rhs) {
    const auto image = make_logic4_image(generation, active,
        PackedLogic4::from_msb_string(lhs),
        PackedLogic4::from_msb_string(rhs));
    const auto expected = evaluate_region_activation_kernel_reference(
        kernel, image);
    require(executor->execute(image),
        "Logic4 add_unsigned activation reaches the generated region body");
    compare_defined_member_registers(kernel, image, expected,
        executor->activation_registers());
  };

  compare_case(1U, "0011", "0101");
  compare_case(2U, "00X1", "0001");
  compare_case(3U, "00Z1", "0001");
}

void check_sparse_direct_ready_physical_ids(
    const fsim::compiler::JitOptimizationLevel optimization)
{
  constexpr SignalId narrow_source { 17U };
  constexpr SignalId wide_source { 83U };
  constexpr SignalId narrow_link { 101U };
  constexpr SignalId wide_link { 607U };
  constexpr SignalId narrow_sink { 1001U };
  constexpr SignalId wide_sink { 1201U };
  std::vector<RegionSignalDescriptor> signals(wide_sink + 1U);
  signals[narrow_source] = { 3U };
  signals[wide_source] = { 5U };
  signals[narrow_link] = { 3U };
  signals[wide_link] = { 5U };
  signals[narrow_sink] = { 3U };
  signals[wide_sink] = { 5U };
  signals[narrow_sink].observations = RegionObservation::current;
  signals[wide_sink].observations = RegionObservation::current;

  std::array<Process, 3U> processes;
  auto& producer = processes[0U];
  producer.id = 0U;
  producer.name = "sparse_direct_ready_producer";
  producer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
  producer.register_count = 2U;
  producer.register_value_kinds.assign(2U, ValueKind::logic4);
  producer.static_sensitivity = {
      { narrow_source, EdgeKind::any },
      { wide_source, EdgeKind::any },
  };
  producer.driver_regions = {
      { narrow_link, 0U, 0U, true },
      { wide_link, 0U, 0U, true },
  };
  producer.operations = {
      ReadSignal { 0U, narrow_source },
      ReadSignal { 1U, wide_source },
      WriteUpdate { narrow_link, 0U,
          SignalUpdateDomain::systemverilog_active },
      WriteUpdate { wide_link, 1U,
          SignalUpdateDomain::systemverilog_active },
      WaitSensitivity { },
      Jump { 0U },
  };

  auto& narrow_consumer = processes[1U];
  narrow_consumer.id = 1U;
  narrow_consumer.name = "sparse_direct_ready_narrow_consumer";
  narrow_consumer.scheduling_domain
      = ProcessSchedulingDomain::systemverilog;
  narrow_consumer.register_count = 1U;
  narrow_consumer.register_value_kinds = { ValueKind::logic4 };
  narrow_consumer.static_sensitivity = {
      { narrow_link, EdgeKind::any },
  };
  narrow_consumer.driver_regions = {
      { narrow_sink, 0U, 0U, true },
  };
  narrow_consumer.operations = {
      ReadSignal { 0U, narrow_link },
      WriteUpdate { narrow_sink, 0U,
          SignalUpdateDomain::systemverilog_active },
      WaitSensitivity { },
      Jump { 0U },
  };

  auto& wide_consumer = processes[2U];
  wide_consumer.id = 2U;
  wide_consumer.name = "sparse_direct_ready_wide_consumer";
  wide_consumer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
  wide_consumer.register_count = 1U;
  wide_consumer.register_value_kinds = { ValueKind::logic4 };
  wide_consumer.static_sensitivity = {
      { wide_link, EdgeKind::any },
  };
  wide_consumer.driver_regions = {
      { wide_sink, 0U, 0U, true },
  };
  wide_consumer.operations = {
      ReadSignal { 0U, wide_link },
      WriteUpdate { wide_sink, 0U,
          SignalUpdateDomain::systemverilog_active },
      WaitSensitivity { },
      Jump { 0U },
  };

  const std::array<const Process*, 3U> bindings {
      &producer, &narrow_consumer, &wide_consumer,
  };
  const auto graph = RegionGraph::build(bindings, signals);
  const auto& components = graph.certificate_inventory().components;
  require(components.size() == 1U
          && components.front().members
              == std::vector<ProcessId> { 0U, 1U, 2U }
          && components.front().structural_internal_signal_candidates
              == std::vector<SignalId> { narrow_link, wide_link },
      "sparse heterogeneous fixture forms one two-link native component");
  const auto program = graph.build_compute_program(0U, bindings);
  require(program.has_value(),
      "RegionGraph constructs the sparse direct-ready fixture");
  const auto& kernel = program->activation_kernel;
  const auto has_input = [&kernel](
      const SignalId signal, const std::uint32_t width) {
    return std::ranges::any_of(kernel.inputs,
        [signal, width](const RegionConeKernelInput& input) {
          return input.signal == signal && input.width == width;
        });
  };
  require(has_input(narrow_source, 3U) && has_input(wide_source, 5U)
          && has_input(narrow_link, 3U) && has_input(wide_link, 5U),
      "compiled bindings retain sparse physical IDs and heterogeneous widths");

  fsim::compiler::LlvmJitOptions options;
  options.optimization = optimization;
  options.cache_directory.clear();
  auto executor = fsim::compiler::LlvmRegionKernelExecutor::try_create(
      kernel, options, "sparse-direct-ready-physical-ids-v1");
  require(executor != nullptr && executor->supports_direct_ready_window(),
      "sparse physical IDs compile through the direct-ready mapping at O0/O2");
}

[[nodiscard]] std::string check_builder_kernel_matches_reference(
    const fsim::compiler::JitOptimizationLevel optimization)
{
  auto [kernel, image] = make_builder_activation_kernel();
  fsim::compiler::LlvmJitOptions options;
  options.optimization = optimization;
  options.cache_directory.clear();
  auto wrong_output_owner = kernel;
  wrong_output_owner.outputs.front().owner = 99U;
  require(fsim::compiler::LlvmRegionKernelExecutor::try_create(
              wrong_output_owner, options,
              "region-native-builder-fixture-v1") == nullptr,
      "an output snapshot cannot be attributed to an unknown member");
  auto wrong_output_segment = kernel;
  wrong_output_segment.outputs.front().kernel_instruction
      = wrong_output_segment.members.back().begin;
  require(fsim::compiler::LlvmRegionKernelExecutor::try_create(
              wrong_output_segment, options,
              "region-native-builder-fixture-v1") == nullptr,
      "an output snapshot instruction must belong to its original member");
  auto aliased_output_register = kernel;
  aliased_output_register.outputs.front().value_register
      = aliased_output_register.members.front()
            .register_bindings.front().activation_register;
  require(fsim::compiler::LlvmRegionKernelExecutor::try_create(
              aliased_output_register, options,
              "region-native-builder-fixture-v1") == nullptr,
      "publication snapshots cannot alias any member register mapping");
  auto wrong_output_kind = kernel;
  wrong_output_kind.outputs.front().value_kind = ValueKind::logic9;
  require(fsim::compiler::LlvmRegionKernelExecutor::try_create(
              wrong_output_kind, options,
              "region-native-builder-fixture-v1") == nullptr,
      "publication snapshot kind must match its authenticated register");
  auto executor = fsim::compiler::LlvmRegionKernelExecutor::try_create(
      kernel, options, "region-native-builder-fixture-v1");
  require(executor != nullptr,
      "the native wrapper accepts a kernel built from RegionGraph processes");
  const auto expected = evaluate_region_activation_kernel_reference(
      kernel, image);
  require(executor->execute(image),
      "the builder-produced activation kernel completes natively");
  const auto actual = executor->activation_registers();
  for (const auto& member : kernel.members) {
    for (const auto& binding : member.register_bindings) {
      if (!binding.defined) {
        continue;
      }
      require(actual[binding.activation_register]
              == expected[binding.activation_register],
          "every defined source member register matches the C++ oracle");
    }
  }
  for (const auto& output : kernel.outputs) {
    require(actual[output.value_register] == expected[output.value_register],
        "every real-builder output snapshot matches the C++ oracle");
  }

  const auto producer = std::ranges::find(kernel.members, ProcessId { 0U },
      &RegionConeKernelMember::process);
  require(producer != kernel.members.end()
          && !producer->register_bindings.empty(),
      "builder fixture retains the producer's defined source register");
  const auto final_source_register = producer->register_bindings.front();
  const auto producer_output = std::ranges::find_if(kernel.outputs,
      [](const RegionConeOutputBinding& output) {
        return output.owner == 0U && output.signal == 2U;
      });
  require(producer_output != kernel.outputs.end()
          && final_source_register.source_register == 0U
          && actual[final_source_register.activation_register]
              == PackedLogic4::from_msb_string("0101")
          && actual[producer_output->value_register]
              == PackedLogic4::from_msb_string("1010"),
      "a dedicated publication snapshot survives later source-register reuse");
  return std::string { executor->cache_identity() };
}


// Count only i64 data loads whose address is rooted at the ABI-v2 input or
// frame bval plane. Pointer-field loads and output-slot bookkeeping remain
// outside this census.
struct KnownPlaneLoadCounts {
  std::size_t direct_input_bval { };
  std::size_t persistent_register_bval { };
};

[[nodiscard]] bool is_struct_field_address(
    const llvm::Value* address,
    const llvm::Value* base,
    const unsigned field) noexcept
{
  const auto* const gep = llvm::dyn_cast<llvm::GetElementPtrInst>(address);
  if (gep == nullptr || gep->getPointerOperand() != base
      || gep->getNumIndices() != 2U) {
    return false;
  }
  const auto* const leading_index
      = llvm::dyn_cast<llvm::ConstantInt>(gep->getOperand(1U));
  const auto* const field_index
      = llvm::dyn_cast<llvm::ConstantInt>(gep->getOperand(2U));
  return leading_index != nullptr && leading_index->isZero()
      && field_index != nullptr
      && field_index->getZExtValue() == field;
}

[[nodiscard]] const llvm::Value* find_pointer_field_load(
    const llvm::Function& function,
    const unsigned argument,
    const unsigned field) noexcept
{
  const auto* const base = function.getArg(argument);
  for (const auto& block : function) {
    for (const auto& instruction : block) {
      const auto* const load = llvm::dyn_cast<llvm::LoadInst>(&instruction);
      if (load != nullptr && load->getType()->isPointerTy()
          && is_struct_field_address(
              load->getPointerOperand(), base, field)) {
        return load;
      }
    }
  }
  return nullptr;
}

[[nodiscard]] const llvm::Value* pointer_storage_root(
    const llvm::Value* pointer) noexcept
{
  auto* current = pointer;
  while (true) {
    current = current->stripPointerCasts();
    const auto* const gep = llvm::dyn_cast<llvm::GEPOperator>(current);
    if (gep == nullptr) {
      return current;
    }
    current = gep->getPointerOperand();
  }
}

[[nodiscard]] KnownPlaneLoadCounts count_known_plane_loads(
    const llvm::Function& function)
{
  using fsim::compiler::llvm_detail::JitRuntimeInstanceField;

  const auto* const direct_bval = find_pointer_field_load(
      function, 0U,
      static_cast<unsigned>(JitRuntimeInstanceField::direct_signal_bval));
  // Frame field 9 is register_bval in fsim_jit_frame_v2 (field 8 is
  // register_aval); this is an ABI layout assertion, not an IR-name check.
  const auto* const register_bval = find_pointer_field_load(
      function, 1U, 9U);
  require(direct_bval != nullptr,
      "raw lowering exposes the direct Logic4 bval plane pointer");
  require(register_bval != nullptr,
      "fixture keeps the persistent register bval plane available");

  KnownPlaneLoadCounts counts;
  for (const auto& block : function) {
    for (const auto& instruction : block) {
      const auto* const load = llvm::dyn_cast<llvm::LoadInst>(&instruction);
      if (load == nullptr || !load->getType()->isIntegerTy(64U)) {
        continue;
      }
      const auto* const root
          = pointer_storage_root(load->getPointerOperand());
      if (root == direct_bval) {
        ++counts.direct_input_bval;
      }
      if (root == register_bval) {
        ++counts.persistent_register_bval;
      }
    }
  }
  return counts;
}

[[nodiscard]] KnownPlaneLoadCounts lower_known_plane_test_body(
    const fsim::compiler::llvm_detail::ProcessLoweringMode mode)
{
  using namespace fsim::compiler::llvm_detail;
  using namespace fsim::runtime::simir;

  Process process;
  process.id = mode == ProcessLoweringMode::four_state ? 810U : 811U;
  process.name = mode == ProcessLoweringMode::four_state
      ? "known-plane-four-state-raw-ir"
      : "known-plane-specialized-raw-ir";
  process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
  process.register_count = 3U;
  process.register_value_kinds.assign(3U, ValueKind::logic4);
  process.debug_locals = {
      { "read_value", "logic", 0U, 4U, { }, { }, { }, ValueKind::logic4,
          { }, SystemVerilogScalarKind::None },
      { "copied_value", "logic", 1U, 4U, { }, { }, { }, ValueKind::logic4,
          { }, SystemVerilogScalarKind::None },
      { "inverted_value", "logic", 2U, 4U, { }, { }, { }, ValueKind::logic4,
          { }, SystemVerilogScalarKind::None },
  };
  // The input bval must flow through CopyRegister and UnaryNot into the
  // update value. Debug locals keep all three register planes frame-backed,
  // making the register-plane read explicit in raw, unoptimized IR.
  process.operations = {
      ReadSignal { 0U, 0U },
      CopyRegister { 1U, 0U },
      UnaryNot { 2U, 1U },
      WriteUpdate { 1U, 2U, SignalUpdateDomain::systemverilog_active },
      Halt { },
  };

  const std::array signal_widths { 4U, 4U };
  const std::array signal_kinds {
      ValueKind::logic4, ValueKind::logic4 };
  const auto validated = validate_process(
      process, signal_widths, signal_kinds);
  const auto plan = make_process_lowering_plan(
      process, validated.register_widths, false);
  const auto read_signals = direct_read_signals(
      process, signal_widths, signal_kinds);
  const auto update_signals = direct_update_signals(
      process, signal_widths, signal_kinds);

  llvm::LLVMContext context;
  llvm::Module module { process.name, context };
  const auto pointer_bits = sizeof(void*) * 8U;
  const auto pointer_alignment_bits = alignof(void*) * 8U;
  const auto i64_alignment_bits = alignof(std::uint64_t) * 8U;
  const auto i32_alignment_bits = alignof(std::uint32_t) * 8U;
  const auto data_layout = std::string { "e-p:" }
      + std::to_string(pointer_bits) + ":"
      + std::to_string(pointer_alignment_bits) + "-i64:"
      + std::to_string(i64_alignment_bits) + "-i32:"
      + std::to_string(i32_alignment_bits) + "-n8:16:32:64-S128";
  module.setDataLayout(data_layout);

  std::vector<std::uint8_t> register_values_persistent;
  // Deliberately inspect lower_process output before optimize_module: the
  // test is about the selected lowering body, not a later DCE optimization.
  lower_process(
      module,
      process.name,
      process,
      signal_widths,
      signal_kinds,
      read_signals,
      update_signals,
      false,
      { },
      0U,
      validated,
      plan,
      fsim::compiler::JitOptimizationLevel::o0,
      false,
      true,
      true,
      mode,
      { },
      { },
      register_values_persistent);
  const auto* const function = module.getFunction(process.name);
  require(function != nullptr,
      "raw lower_process creates the requested process function");
  require(verify_error(module).empty(),
      "raw four-state and known Logic4 lowering modules verify");
  return count_known_plane_loads(*function);
}

void check_known_logic4_raw_ir_shape()
{
  using fsim::compiler::llvm_detail::ProcessLoweringMode;
  const auto four_state
      = lower_known_plane_test_body(ProcessLoweringMode::four_state);
  const auto known
      = lower_known_plane_test_body(ProcessLoweringMode::region_known_logic4);

  require(four_state.direct_input_bval != 0U
          && four_state.persistent_register_bval != 0U,
      "the four-state body loads both input and persistent-register bval data");
  require(known.direct_input_bval == 0U
          && known.persistent_register_bval == 0U,
      "the specialized body has no input or register bval data loads");
}

[[nodiscard]] std::size_t raw_direct_aval_load_count(
    const fsim::runtime::simir::Process& process,
    const RegionConeActivationKernel& kernel)
{
  using namespace fsim::compiler::llvm_detail;
  using namespace fsim::runtime::simir;

  struct InputShape {
    RegisterId reg { };
    std::uint32_t width { };
    ValueKind kind { ValueKind::logic4 };
  };
  std::vector<InputShape> input_shapes;
  input_shapes.reserve(kernel.inputs.size() + kernel.members.size());
  for (const auto& input : kernel.inputs) {
    input_shapes.push_back({ input.value_register, input.width,
        input.value_kind });
  }
  for (const auto& member : kernel.members) {
    input_shapes.push_back({ member.readiness_register, 1U,
        ValueKind::logic4 });
  }
  std::ranges::sort(input_shapes, std::ranges::less { }, &InputShape::reg);
  std::vector<std::uint32_t> signal_widths;
  std::vector<ValueKind> signal_kinds;
  signal_widths.reserve(input_shapes.size());
  signal_kinds.reserve(input_shapes.size());
  for (const auto& input : input_shapes) {
    signal_widths.push_back(input.width);
    signal_kinds.push_back(input.kind);
  }
  const auto register_value_kinds
      = fsim::runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
          kernel.program.register_value_kinds);
  const bool raw_prefix_supported
      = kernel.program.scheduling_domain == ProcessSchedulingDomain::systemverilog
      && std::ranges::all_of(register_value_kinds,
          [](const ValueKind kind) { return kind == ValueKind::logic4; })
      && !kernel.internal_signals.empty()
      && std::ranges::is_sorted(kernel.internal_signals)
      && std::ranges::adjacent_find(kernel.internal_signals)
          == kernel.internal_signals.end()
      && std::ranges::all_of(kernel.internal_signals,
          [&](const SignalId signal) {
            const auto output = std::ranges::find(kernel.outputs, signal,
                &RegionConeOutputBinding::signal);
            return output != kernel.outputs.end()
                && std::ranges::count(kernel.outputs, signal,
                       &RegionConeOutputBinding::signal) == 1U
                && output->value_kind == ValueKind::logic4
                && output->offset == 0U && output->width != 0U
                && output->width <= 64U;
          });
  if (raw_prefix_supported) {
    for (const auto signal : kernel.internal_signals) {
      const auto output = std::ranges::find(kernel.outputs, signal,
          &RegionConeOutputBinding::signal);
      signal_widths.push_back(output->width);
      signal_kinds.push_back(ValueKind::logic4);
    }
  }
  for (const auto& member : kernel.members) {
    for (const auto& binding : member.register_bindings) {
      signal_widths.push_back(binding.width);
      signal_kinds.push_back(binding.value_kind);
    }
  }
  for (const auto& output : kernel.outputs) {
    signal_widths.push_back(output.width);
    signal_kinds.push_back(output.value_kind);
  }
  if (raw_prefix_supported) {
    for (std::size_t index = 0U; index < kernel.internal_signals.size();
         ++index) {
      signal_widths.push_back(1U);
      signal_kinds.push_back(ValueKind::logic4);
    }
  }

  const auto validated = validate_process(
      process, signal_widths, signal_kinds);
  const auto plan = make_process_lowering_plan(
      process, validated.register_widths, false);
  const auto reads = direct_read_signals(
      process, signal_widths, signal_kinds);
  const auto updates = direct_update_signals(
      process, signal_widths, signal_kinds);
  llvm::LLVMContext context;
  llvm::Module module { process.name, context };
  const auto pointer_bits = sizeof(void*) * 8U;
  const auto pointer_alignment_bits = alignof(void*) * 8U;
  const auto i64_alignment_bits = alignof(std::uint64_t) * 8U;
  const auto i32_alignment_bits = alignof(std::uint32_t) * 8U;
  module.setDataLayout(std::string { "e-p:" }
      + std::to_string(pointer_bits) + ":"
      + std::to_string(pointer_alignment_bits) + "-i64:"
      + std::to_string(i64_alignment_bits) + "-i32:"
      + std::to_string(i32_alignment_bits) + "-n8:16:32:64-S128");
  std::vector<std::uint8_t> persistent;
  lower_process(module, process.name, process, signal_widths, signal_kinds,
      reads, updates, false, { }, 0U, validated, plan,
      fsim::compiler::JitOptimizationLevel::o0, false, true, true,
      ProcessLoweringMode::four_state, { }, { }, persistent);
  const auto* const function = module.getFunction(process.name);
  require(function != nullptr && verify_error(module).empty(),
      "both generated constant-input bodies verify before optimization");

  const auto* const direct_aval = find_pointer_field_load(*function, 0U,
      static_cast<unsigned>(JitRuntimeInstanceField::direct_signal_aval));
  if (direct_aval == nullptr) {
    return 0U;
  }
  std::size_t load_count { };
  for (const auto& block : *function) {
    for (const auto& instruction : block) {
      const auto* const load = llvm::dyn_cast<llvm::LoadInst>(&instruction);
      if (load != nullptr && load->getType()->isIntegerTy(64U)
          && pointer_storage_root(load->getPointerOperand()) == direct_aval) {
        ++load_count;
      }
    }
  }
  require(load_count == reads.size(),
      "each raw direct read performs one aval plane load");
  return load_count;
}

void check_guarded_constant_input_specialization(
    const fsim::compiler::JitOptimizationLevel optimization)
{
  using fsim::compiler::RegionKernelSpecialization;
  using fsim::compiler::llvm_detail::RegionKernelBodySelection;

  auto [kernel, matching_image] = make_builder_activation_kernel();
  const auto boundary = std::ranges::find_if(kernel.inputs,
      [](const RegionConeKernelInput& input) { return !input.internal; });
  require(boundary != kernel.inputs.end()
          && boundary->value_kind == ValueKind::logic4,
      "builder fixture exposes a Logic4 boundary input for startup specialization");
  const auto input_value = std::ranges::find(matching_image.register_inputs,
      boundary->value_register, &RegionKernelRegisterInput::register_id);
  require(input_value != matching_image.register_inputs.end(),
      "captured activation image contains the boundary input register");
  const auto expected_constant = input_value->value;
  kernel.constant_inputs.push_back({ 99U, boundary->signal, 0U,
      boundary->width, ValueKind::logic4,
      SignalUpdateDomain::systemverilog_active, expected_constant });

  fsim::compiler::LlvmJitOptions options;
  options.optimization = optimization;
  options.cache_directory.clear();
  auto dynamic_executor = fsim::compiler::LlvmRegionKernelExecutor::try_create(
      kernel, options, "region-constant-dynamic-reference-v1",
      RegionKernelSpecialization::dynamic_inputs);
  auto constant_executor = fsim::compiler::LlvmRegionKernelExecutor::try_create(
      kernel, options, "region-constant-dynamic-reference-v1",
      RegionKernelSpecialization::guarded_constant_inputs);
  require(dynamic_executor != nullptr && constant_executor != nullptr,
      "both the dynamic and guarded constant region entries are built");
  require(dynamic_executor->cache_identity()
          != constant_executor->cache_identity(),
      "native cache identity distinguishes dynamic and guarded bodies");

  const auto* const dynamic_process
      = fsim::compiler::llvm_detail::RegionKernelTestAccess::synthetic_process(
          *dynamic_executor);
  const auto* const constant_process
      = fsim::compiler::llvm_detail::RegionKernelTestAccess::synthetic_process(
          *constant_executor);
  require(dynamic_process != nullptr && constant_process != nullptr,
      "each executor retains its exact synthetic process for the raw-IR proof");
  std::vector<RegisterId> input_registers;
  input_registers.reserve(kernel.inputs.size() + kernel.members.size());
  for (const auto& input : kernel.inputs) {
    input_registers.push_back(input.value_register);
  }
  for (const auto& member : kernel.members) {
    input_registers.push_back(member.readiness_register);
  }
  std::ranges::sort(input_registers);
  const auto input_ordinal = static_cast<std::size_t>(
      std::ranges::find(input_registers, boundary->value_register)
          - input_registers.begin());
  require(input_ordinal < input_registers.size(),
      "the specialized boundary input has a stable synthetic preamble slot");
  const auto dynamic_input_operation
      = dynamic_process->operations.expanded(input_ordinal);
  const auto constant_input_operation
      = constant_process->operations.expanded(input_ordinal);
  const auto* const dynamic_read = operation_get_if<ReadSignal>(
      &dynamic_input_operation);
  const auto* const constant_load = operation_get_if<LoadConstant>(
      &constant_input_operation);
  require(dynamic_read != nullptr && constant_load != nullptr
          && constant_load->destination == boundary->value_register
          && constant_load->value == expected_constant,
      "the specialized body replaces only its certified boundary read with the exact literal");
  require(raw_direct_aval_load_count(*constant_process, kernel) + 1U
          == raw_direct_aval_load_count(*dynamic_process, kernel),
      "unoptimized specialized LLVM IR omits the certified signal aval load");

  const auto compare_to_reference = [&](
      const RegionKernelActivationImage& image,
      fsim::compiler::LlvmRegionKernelExecutor& executor,
      const std::string_view description) {
    const auto expected
        = evaluate_region_activation_kernel_reference(kernel, image);
    require(executor.execute(image), description);
    const auto actual = executor.activation_registers();
    compare_defined_member_registers(kernel, image, expected, actual);
    for (const auto& output : kernel.outputs) {
      require(actual[output.value_register] == expected[output.value_register],
          "constant specialization preserves each original write snapshot");
    }
  };
  compare_to_reference(matching_image, *constant_executor,
      "an exactly matching captured boundary value enters the constant body");
  require(fsim::compiler::llvm_detail::RegionKernelTestAccess::
              last_body_selection(*constant_executor)
              == RegionKernelBodySelection::guarded_constant_inputs,
      "the test route confirms the guarded constant entry actually ran");
  const std::vector<PackedLogic4> retained(
      constant_executor->activation_registers().begin(),
      constant_executor->activation_registers().end());

  auto mismatching_image = matching_image;
  const auto mismatching_input = std::ranges::find(
      mismatching_image.register_inputs, boundary->value_register,
      &RegionKernelRegisterInput::register_id);
  require(mismatching_input != mismatching_image.register_inputs.end(),
      "the mismatch scenario retains its captured boundary slot");
  mismatching_input->value.set(0U,
      mismatching_input->value.get(0U) == Logic4::zero
          ? Logic4::one : Logic4::zero);
  require(!constant_executor->execute(mismatching_image)
          && std::ranges::equal(constant_executor->activation_registers(),
              retained),
      "a changed deposit/force image declines before mutating static workspace or snapshots");
  compare_to_reference(mismatching_image, *dynamic_executor,
      "the paired dynamic body handles a changed or released boundary value");
  require(fsim::compiler::llvm_detail::RegionKernelTestAccess::
              last_body_selection(*dynamic_executor)
              == RegionKernelBodySelection::four_state,
      "the ordinary dynamic entry remains selected for changed inputs");

  auto changed_value_kernel = kernel;
  changed_value_kernel.constant_inputs.front().value.set(0U,
      changed_value_kernel.constant_inputs.front().value.get(0U)
              == Logic4::zero
          ? Logic4::one : Logic4::zero);
  auto changed_value_executor
      = fsim::compiler::LlvmRegionKernelExecutor::try_create(
          changed_value_kernel, options,
          "region-constant-dynamic-reference-v1",
          RegionKernelSpecialization::guarded_constant_inputs);
  require(changed_value_executor != nullptr
          && changed_value_executor->cache_identity()
              != constant_executor->cache_identity(),
      "changing only the certified constant value changes its native identity");
}

void check_known_logic4_dispatch(
    const fsim::compiler::JitOptimizationLevel optimization)
{
  auto [kernel, four_state_image] = make_builder_activation_kernel();
  require(std::ranges::all_of(kernel.members,
              [](const RegionConeKernelMember& member) {
                return member.final_debug_state.has_value();
              }),
      "the graph-built marker case retains final source/scope metadata");
  fsim::compiler::LlvmJitOptions options;
  options.optimization = optimization;
  options.cache_directory.clear();
  auto executor = fsim::compiler::LlvmRegionKernelExecutor::try_create(
      kernel, options, "region-known-logic4-builder-fixture-v1");
  require(executor != nullptr,
      "the real graph-built Logic4 kernel accepts the private native bodies");
  const auto selection = [](const auto& value) {
    return fsim::compiler::llvm_detail::RegionKernelTestAccess::
        last_body_selection(value);
  };
  using fsim::compiler::llvm_detail::RegionKernelBodySelection;
  require(selection(*executor) == RegionKernelBodySelection::never_entered,
      "the test route starts unset until a generated entry is invoked");

  const auto compare = [&](const RegionKernelActivationImage& image,
                           const RegionKernelBodySelection expected_body,
                           const std::string_view context) {
    const auto expected = evaluate_region_activation_kernel_reference(
        kernel, image);
    require(executor->execute(image), context);
    require(selection(*executor) == expected_body,
        "the selected private lowering body matches the validated input class");
    const auto actual = executor->activation_registers();
    compare_defined_member_registers(kernel, image, expected, actual);
    for (const auto& output : kernel.outputs) {
      require(actual[output.value_register] == expected[output.value_register],
          "native lowering bodies preserve each distinct publication snapshot");
    }
  };

  compare(four_state_image, RegionKernelBodySelection::four_state,
      "X/Z input values execute through the checked four-state body");

  auto known_image = four_state_image;
  for (auto& input : known_image.register_inputs) {
    const bool readiness = std::ranges::any_of(kernel.members,
        [&](const auto& member) {
          return member.readiness_register == input.register_id;
        });
    PackedLogic4 known { input.value.width(), Logic4::zero };
    if (readiness) {
      known.fill(Logic4::one);
    } else {
      for (std::size_t bit = 0U; bit < known.width(); ++bit) {
        known.set(bit, ((bit + input.register_id) % 2U) == 0U
                ? Logic4::zero : Logic4::one);
      }
    }
    input.value = std::move(known);
  }
  compare(known_image, RegionKernelBodySelection::known_logic4,
      "a fully known active image enters the specialized two-state body");

  auto x_image = known_image;
  const auto boundary_input = kernel.inputs.front().value_register;
  const auto x_input = std::ranges::find(x_image.register_inputs,
      boundary_input, &RegionKernelRegisterInput::register_id);
  require(x_input != x_image.register_inputs.end(),
      "the fallback image contains the first bound input register");
  x_input->value.set(0U, Logic4::x);
  compare(x_image, RegionKernelBodySelection::four_state,
      "one X bit declines the two-state body before entry");

  auto z_image = known_image;
  const auto z_input = std::ranges::find(z_image.register_inputs,
      boundary_input, &RegionKernelRegisterInput::register_id);
  require(z_input != z_image.register_inputs.end(),
      "the fallback image contains the first bound input register");
  z_input->value.set(0U, Logic4::z);
  compare(z_image, RegionKernelBodySelection::four_state,
      "one Z bit declines the two-state body before entry");

  auto unknown_constant_kernel = kernel;
  bool changed_constant { };
  for (auto& operation : unknown_constant_kernel.program.operations) {
    if (auto* constant
            = fsim::runtime::simir::operation_get_if<LoadConstant>(
            &operation);
        constant != nullptr) {
      constant->value = PackedLogic4::from_msb_string("10X0");
      changed_constant = true;
      break;
    }
  }
  require(changed_constant,
      "the graph-built fallback fixture includes a constant assignment");
  auto unknown_constant_executor
      = fsim::compiler::LlvmRegionKernelExecutor::try_create(
          unknown_constant_kernel, options,
          "region-known-logic4-unknown-constant-v1");
  require(unknown_constant_executor != nullptr,
      "a valid unknown-valued constant retains the four-state entry");
  const auto unknown_constant_expected
      = evaluate_region_activation_kernel_reference(
          unknown_constant_kernel, known_image);
  require(unknown_constant_executor->execute(known_image)
          && fsim::compiler::llvm_detail::RegionKernelTestAccess::
              last_body_selection(*unknown_constant_executor)
              == RegionKernelBodySelection::four_state,
      "an X-valued constant cannot enter the known Logic4 variant");
  const auto unknown_constant_actual
      = unknown_constant_executor->activation_registers();
  compare_defined_member_registers(unknown_constant_kernel, known_image,
      unknown_constant_expected, unknown_constant_actual);

  auto out_of_range_extract = make_logic4_kernel();
  out_of_range_extract.program.operations.replace(
      2U, Extract { 4U, 0U, 3U, 4U });
  auto out_of_range_executor
      = fsim::compiler::LlvmRegionKernelExecutor::try_create(
          out_of_range_extract, options,
          "region-known-logic4-out-of-range-extract-v1");
  require(out_of_range_executor == nullptr,
      "out-of-range extraction is rejected before any native body is entered");

  auto read_before_definition = make_logic4_kernel();
  read_before_definition.program.operations.replace(
      2U, UnaryNot { 4U, 7U });
  read_before_definition.program.operations.replace(
      4U, LoadConstant { 7U, PackedLogic4::from_msb_string("0000") });
  require(fsim::compiler::LlvmRegionKernelExecutor::try_create(
              read_before_definition, options,
              "region-known-logic4-read-before-definition-v1") == nullptr,
      "an intermediate read before its definition fails closed");
}

[[nodiscard]] Logic4 wide_logic4_state(
    const std::uint32_t bit, const std::uint32_t seed) noexcept
{
  constexpr std::array states {
      Logic4::zero, Logic4::one, Logic4::x, Logic4::z };
  return states[(bit + seed) % states.size()];
}

[[nodiscard]] PackedLogic4 make_wide_logic4_value(
    const std::uint32_t width, const std::uint32_t seed)
{
  PackedLogic4 value { width, Logic4::zero };
  for (std::uint32_t bit = 0U; bit < width; ++bit) {
    value.set(bit, wide_logic4_state(bit, seed));
  }
  return value;
}

[[nodiscard]] PackedLogic4 make_wide_logic4_not(const PackedLogic4& value)
{
  PackedLogic4 result { value.width(), Logic4::x };
  for (std::uint32_t bit = 0U; bit < value.width(); ++bit) {
    switch (value.get(bit)) {
    case Logic4::zero:
      result.set(bit, Logic4::one);
      break;
    case Logic4::one:
      result.set(bit, Logic4::zero);
      break;
    case Logic4::x:
    case Logic4::z:
      break;
    }
  }
  return result;
}

[[nodiscard]] std::pair<RegionConeActivationKernel,
    RegionKernelActivationImage> make_wide_builder_activation_kernel(
    const std::uint32_t width)
{
  std::vector<RegionSignalDescriptor> signals {
      { width }, { 4U }, { width }, { width }, { width },
  };
  signals[2U].observations = RegionObservation::current;
  signals[3U].observations = RegionObservation::current;

  Process producer;
  producer.id = 0U;
  producer.name = "builder_wide_region_producer";
  producer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
  producer.register_count = 3U;
  producer.register_value_kinds.assign(3U, ValueKind::logic4);
  producer.static_sensitivity = { { 0U, EdgeKind::any } };
  producer.driver_regions = {
      { 1U, 0U, 0U, true }, { 2U, 0U, 0U, true },
  };
  producer.operations = {
      ReadSignal { 0U, 0U },
      UnaryNot { 1U, 0U },
      WriteUpdate { 2U, 1U, SignalUpdateDomain::systemverilog_active },
      LoadConstant { 1U, make_wide_logic4_value(width, 3U) },
      LoadConstant { 2U, PackedLogic4::from_msb_string("10XZ") },
      WriteUpdate { 1U, 2U, SignalUpdateDomain::systemverilog_active },
      WaitSensitivity { },
      Jump { 0U },
  };

  Process consumer;
  consumer.id = 1U;
  consumer.name = "builder_wide_region_consumer";
  consumer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
  consumer.register_count = 4U;
  consumer.register_value_kinds.assign(4U, ValueKind::logic4);
  consumer.static_sensitivity = {
      { 1U, EdgeKind::any }, { 4U, EdgeKind::any },
  };
  consumer.driver_regions = { { 3U, 0U, 0U, true } };
  consumer.operations = {
      ReadSignal { 0U, 1U },
      UnaryNot { 1U, 0U },
      ReadSignal { 2U, 4U },
      UnaryNot { 3U, 2U },
      WriteUpdate { 3U, 3U, SignalUpdateDomain::systemverilog_active },
      LoadConstant { 3U, make_wide_logic4_value(width, 1U) },
      WaitSensitivity { },
      Jump { 0U },
  };

  std::vector<Process> processes;
  processes.push_back(std::move(producer));
  processes.push_back(std::move(consumer));
  std::vector<const Process*> process_bindings;
  process_bindings.reserve(processes.size());
  for (const auto& process : processes) {
    process_bindings.push_back(&process);
  }

  const auto graph = RegionGraph::build(process_bindings, signals);
  const auto& components = graph.certificate_inventory().components;
  require(components.size() == 1U
          && components.front().members
              == std::vector<ProcessId> { 0U, 1U }
          && components.front().structural_internal_signal_candidates
              == std::vector<SignalId> { 1U },
      "wide boundary fixture keeps only its four-bit link internal");
  const auto program = graph.build_compute_program(0U, process_bindings);
  require(program.has_value(),
      "RegionGraph builds wide boundary reads and publication snapshots");

  auto kernel = program->activation_kernel;
  require(kernel.inputs.size() == 3U && kernel.outputs.size() == 3U,
      "wide fixture has two boundary inputs, one internal input and three writes");
  for (const auto& output : kernel.outputs) {
    const auto owner = std::ranges::find(kernel.members, output.owner,
        &RegionConeKernelMember::process);
    require(owner != kernel.members.end()
            && output.kernel_instruction >= owner->begin
            && output.kernel_instruction < owner->end,
        "each wide output snapshot retains its source member segment");
    for (const auto& member : kernel.members) {
      for (const auto& binding : member.register_bindings) {
        require(binding.activation_register != output.value_register,
            "wide output snapshots are distinct from source member registers");
      }
    }
  }

  RegionKernelActivationImage image;
  image.generation = 1U;
  image.active_member_indices.resize(kernel.members.size());
  for (std::size_t member = 0U; member < kernel.members.size(); ++member) {
    image.active_member_indices[member] = member;
  }
  image.scheduler_prefix.frontier_generation = 101U;
  image.scheduler_prefix.frontier_cursor = 0U;
  image.scheduler_prefix.frontier_end = kernel.members.size();
  image.scheduler_prefix.time = 0U;
  image.scheduler_prefix.delta = 0U;
  image.scheduler_prefix.phase = SchedulerPhase::active;
  image.scheduler_prefix.systemverilog_round = 201U;
  for (std::size_t member = 0U; member < kernel.members.size(); ++member) {
    const auto process = kernel.members[member].process;
    const RegionKernelReadyMember request {
        process,
        Process::full_static_trigger_mask,
        RegionKernelActivationOrigin {
            ProcessSchedulingDomain::systemverilog,
            SchedulerPhase::active,
            0U,
            0U,
            static_cast<StableOrder>(100U + process),
            static_cast<std::uint64_t>(200U + process),
            201U,
        },
    };
    image.ready_processes.push_back(process);
    image.requests.push_back(request);
    image.scheduler_prefix.tasks.push_back({ member, request });
  }

  std::vector<std::pair<RegisterId, PackedLogic4>> register_inputs;
  register_inputs.reserve(kernel.inputs.size() + kernel.members.size());
  const auto wide_a = make_wide_logic4_value(width, 0U);
  const auto wide_b = make_wide_logic4_value(width, 2U);
  const auto internal = PackedLogic4::from_msb_string("10XZ");
  for (const auto& input : kernel.inputs) {
    if (input.signal == 0U) {
      register_inputs.emplace_back(input.value_register, wide_a);
    } else if (input.signal == 1U) {
      register_inputs.emplace_back(input.value_register, internal);
    } else if (input.signal == 4U) {
      register_inputs.emplace_back(input.value_register, wide_b);
    } else {
      throw std::runtime_error {
          "wide builder fixture contains an unexpected signal input" };
    }
  }
  for (const auto& member : kernel.members) {
    register_inputs.emplace_back(member.readiness_register,
        PackedLogic4 { 1U, Logic4::one });
  }
  std::ranges::sort(register_inputs, std::ranges::less { },
      [](const auto& entry) { return entry.first; });
  image.register_inputs.reserve(register_inputs.size());
  for (auto& [register_id, value] : register_inputs) {
    image.register_inputs.push_back({ register_id, std::move(value) });
  }
  return { std::move(kernel), std::move(image) };
}

[[nodiscard]] RegionKernelActivationImage make_wide_builder_image(
    const RegionConeActivationKernel& kernel,
    const std::uint64_t generation,
    const std::span<const std::size_t> active_members,
    const PackedLogic4& wide_a,
    const PackedLogic4& wide_b,
    const PackedLogic4& internal)
{
  RegionKernelActivationImage image;
  image.generation = generation;
  image.active_member_indices.assign(
      active_members.begin(), active_members.end());
  image.scheduler_prefix.frontier_generation = generation + 100U;
  image.scheduler_prefix.frontier_cursor = 0U;
  image.scheduler_prefix.frontier_end = active_members.size();
  image.scheduler_prefix.time = 0U;
  image.scheduler_prefix.delta = generation - 1U;
  image.scheduler_prefix.phase = SchedulerPhase::active;
  image.scheduler_prefix.systemverilog_round = generation + 200U;
  for (std::size_t ordinal = 0U; ordinal < active_members.size(); ++ordinal) {
    const auto member_index = active_members[ordinal];
    require(member_index < kernel.members.size(),
        "wide image selects a real graph-built member");
    const auto process = kernel.members[member_index].process;
    const RegionKernelReadyMember request {
        process,
        Process::full_static_trigger_mask,
        RegionKernelActivationOrigin {
            ProcessSchedulingDomain::systemverilog,
            SchedulerPhase::active,
            0U,
            generation - 1U,
            static_cast<StableOrder>(100U + process),
            static_cast<std::uint64_t>(200U + process),
            generation + 200U,
        },
    };
    image.ready_processes.push_back(process);
    image.requests.push_back(request);
    image.scheduler_prefix.tasks.push_back({ ordinal, request });
  }

  std::vector<std::pair<RegisterId, PackedLogic4>> register_inputs;
  register_inputs.reserve(kernel.inputs.size() + kernel.members.size());
  for (const auto& input : kernel.inputs) {
    if (input.signal == 0U) {
      register_inputs.emplace_back(input.value_register, wide_a);
    } else if (input.signal == 1U) {
      register_inputs.emplace_back(input.value_register, internal);
    } else if (input.signal == 4U) {
      register_inputs.emplace_back(input.value_register, wide_b);
    } else {
      throw std::runtime_error {
          "wide builder image contains an unexpected signal input" };
    }
  }
  for (std::size_t member_index = 0U;
       member_index < kernel.members.size(); ++member_index) {
    const bool ready = std::ranges::binary_search(
        active_members, member_index);
    register_inputs.emplace_back(
        kernel.members[member_index].readiness_register,
        PackedLogic4 { 1U, ready ? Logic4::one : Logic4::zero });
  }
  std::ranges::sort(register_inputs, std::ranges::less { },
      [](const auto& entry) { return entry.first; });
  image.register_inputs.reserve(register_inputs.size());
  for (auto& [register_id, value] : register_inputs) {
    image.register_inputs.push_back({ register_id, std::move(value) });
  }
  return image;
}


[[nodiscard]] std::string check_wide_builder_kernel_matches_reference(
    const fsim::compiler::JitOptimizationLevel optimization,
    const std::uint32_t width)
{
  auto [kernel, unused_image] = make_wide_builder_activation_kernel(width);
  static_cast<void>(unused_image);
  fsim::compiler::LlvmJitOptions options;
  options.optimization = optimization;
  options.cache_directory.clear();
  auto executor = fsim::compiler::LlvmRegionKernelExecutor::try_create(
      kernel, options, "region-native-wide-logic4-fixture-v1");
  require(executor != nullptr,
      "the native wrapper accepts graph-built wide Logic4 activations");
  const auto identity = std::string { executor->cache_identity() };
  const auto* const register_storage = executor->activation_registers().data();

  const auto compare_activation = [&](const std::uint64_t generation,
                                      const std::span<const std::size_t> active,
                                      const PackedLogic4& wide_a,
                                      const PackedLogic4& wide_b,
                                      const PackedLogic4& internal) {
    const auto image = make_wide_builder_image(
        kernel, generation, active, wide_a, wide_b, internal);
    const auto expected = evaluate_region_activation_kernel_reference(
        kernel, image);
    require(executor->execute(image),
        "wide Logic4 reads and outputs complete through direct ABI planes");
    const auto actual = executor->activation_registers();
    compare_defined_member_registers(kernel, image, expected, actual);
    for (const auto& output : kernel.outputs) {
      // Inactive owners have no publication obligation; their register state
      // is checked separately by compare_defined_member_registers.
      const bool owner_active = std::ranges::any_of(
          active, [&](const std::size_t member_index) {
            return kernel.members[member_index].process == output.owner;
          });
      if (!owner_active) {
        continue;
      }
      require(actual[output.value_register]
              == expected[output.value_register],
          "every wide graph output snapshot matches the checked C++ reference");
      require(actual[output.value_register].width() == output.width,
          "wide output snapshots retain the exact source width");
    }
    require(actual.data() == register_storage
            && executor->cache_identity() == identity,
        "wide activations reuse register storage and immutable identity");
    if (generation == 1U) {
      const auto producer = std::ranges::find(kernel.members, ProcessId { 0U },
          &RegionConeKernelMember::process);
      const auto consumer = std::ranges::find(kernel.members, ProcessId { 1U },
          &RegionConeKernelMember::process);
      require(producer != kernel.members.end()
              && consumer != kernel.members.end(),
          "wide snapshot fixture retains both member mappings");
      const auto producer_source = std::ranges::find(
          producer->register_bindings, RegisterId { 1U },
          &RegionConeKernelRegisterBinding::source_register);
      const auto consumer_source = std::ranges::find(
          consumer->register_bindings, RegisterId { 3U },
          &RegionConeKernelRegisterBinding::source_register);
      const auto producer_output = std::ranges::find_if(kernel.outputs,
          [](const RegionConeOutputBinding& output) {
            return output.owner == 0U && output.signal == 2U;
          });
      const auto consumer_output = std::ranges::find_if(kernel.outputs,
          [](const RegionConeOutputBinding& output) {
            return output.owner == 1U && output.signal == 3U;
          });
      require(producer_source != producer->register_bindings.end()
              && consumer_source != consumer->register_bindings.end()
              && producer_output != kernel.outputs.end()
              && consumer_output != kernel.outputs.end()
              && actual[producer_source->activation_register]
                  == make_wide_logic4_value(width, 3U)
              && actual[consumer_source->activation_register]
                  == make_wide_logic4_value(width, 1U)
              && actual[producer_source->activation_register]
                  != actual[producer_output->value_register]
              && actual[consumer_source->activation_register]
                  != actual[consumer_output->value_register],
          "wide publication snapshots survive later source-register reuse");
    }
  };

  constexpr std::array both { std::size_t { 0U }, std::size_t { 1U } };
  constexpr std::array producer_only { std::size_t { 0U } };
  constexpr std::array consumer_only { std::size_t { 1U } };
  const auto wide_a0 = make_wide_logic4_value(width, 0U);
  const auto wide_b2 = make_wide_logic4_value(width, 2U);
  const auto wide_a1 = make_wide_logic4_value(width, 1U);
  const auto wide_b3 = make_wide_logic4_value(width, 3U);
  const std::array tail_states {
      make_wide_logic4_value(width, 0U).get(width - 1U),
      make_wide_logic4_value(width, 1U).get(width - 1U),
      make_wide_logic4_value(width, 2U).get(width - 1U),
      make_wide_logic4_value(width, 3U).get(width - 1U),
  };
  require(tail_states[0U] != tail_states[1U]
          && tail_states[0U] != tail_states[2U]
          && tail_states[0U] != tail_states[3U]
          && tail_states[1U] != tail_states[2U]
          && tail_states[1U] != tail_states[3U]
          && tail_states[2U] != tail_states[3U],
      "wide fixture exercises 0/1/X/Z in the partial or final word");
  require(wide_a1.get(width - 1U) != wide_a0.get(width - 1U),
      "wide fixture varies the partial final word across activations");

  const auto producer_output = std::ranges::find_if(kernel.outputs,
      [](const RegionConeOutputBinding& output) {
        return output.owner == 0U && output.signal == 2U;
      });
  require(producer_output != kernel.outputs.end(),
      "wide retained-copy fixture finds the changing producer output");
  compare_activation(1U, both, wide_a0, wide_b2,
      PackedLogic4::from_msb_string("10XZ"));
  const PackedLogic4 retained_output
      = executor->activation_registers()[producer_output->value_register];
  const auto expected_retained_output
      = make_wide_logic4_not(make_wide_logic4_value(width, 0U));
  require(retained_output == expected_retained_output,
      "the retained producer snapshot has the first activation value");
  // Repeated values still require each wide direct update slot to be active.
  compare_activation(2U, both, wide_a0, wide_b2,
      PackedLogic4::from_msb_string("10XZ"));
  require(retained_output
              == executor->activation_registers()[producer_output->value_register],
      "a retained wide output copy remains stable after a repeated activation");
  compare_activation(3U, both, wide_a1, wide_b3,
      PackedLogic4::from_msb_string("ZX01"));
  require(retained_output == expected_retained_output
          && retained_output
              != executor->activation_registers()[producer_output->value_register],
      "wide COW output storage preserves old copies when the next activation changes");
  compare_activation(4U, producer_only, wide_b3, wide_a0,
      PackedLogic4::from_msb_string("X0Z1"));
  compare_activation(5U, consumer_only, wide_a0, wide_a1,
      PackedLogic4::from_msb_string("1ZX0"));

  auto valid_retry = make_wide_builder_image(kernel, 6U, both,
      wide_b3, wide_a1, PackedLogic4::from_msb_string("0XZ1"));
  const std::vector<PackedLogic4> prior_registers {
      executor->activation_registers().begin(),
      executor->activation_registers().end() };
  auto malformed = valid_retry;
  const auto wide_input = std::ranges::find_if(malformed.register_inputs,
      [&](const RegionKernelRegisterInput& value) {
        return value.value.width() == width;
      });
  require(wide_input != malformed.register_inputs.end(),
      "wide malformed-image fixture finds a boundary input");
  wide_input->value = PackedLogic4 { width - 1U, Logic4::x };
  require(!executor->execute(malformed),
      "a mismatched wide input width declines before native entry");
  require(std::ranges::equal(prior_registers,
              executor->activation_registers()),
      "a rejected wide image leaves the previous activation values intact");
  compare_activation(6U, both, wide_b3, wide_a1,
      PackedLogic4::from_msb_string("0XZ1"));

  // The test-only callback stubs mark any use. Every successful activation
  // above therefore proves the wide paths used their authenticated direct
  // planes and update slots; the wrapper returns false if a callback fires.
  return identity;
}

void check_builder_kernel_steady_state_no_allocations(
    const fsim::compiler::JitOptimizationLevel optimization)
{
  auto [kernel, base_image] = make_builder_activation_kernel();
  fsim::compiler::LlvmJitOptions options;
  options.optimization = optimization;
  options.cache_directory.clear();
  auto executor = fsim::compiler::LlvmRegionKernelExecutor::try_create(
      kernel, options, "region-native-allocation-fixture-v1");
  require(executor != nullptr,
      "the allocation witness uses a native kernel built by RegionGraph");

  constexpr std::array both { std::size_t { 0U }, std::size_t { 1U } };
  constexpr std::array producer_only { std::size_t { 0U } };
  constexpr std::array consumer_only { std::size_t { 1U } };
  auto full_first = make_region_allocation_case(kernel, base_image, 10U, both,
      PackedLogic4::from_msb_string("1001"),
      PackedLogic4::from_msb_string("0Z1X"));
  auto full_second = make_region_allocation_case(kernel, base_image, 11U, both,
      PackedLogic4::from_msb_string("0110"),
      PackedLogic4::from_msb_string("101Z"));
  auto producer = make_region_allocation_case(kernel, base_image, 12U,
      producer_only, PackedLogic4::from_msb_string("X01Z"),
      PackedLogic4::from_msb_string("1100"));
  auto consumer = make_region_allocation_case(kernel, base_image, 13U,
      consumer_only, PackedLogic4::from_msb_string("ZX10"),
      PackedLogic4::from_msb_string("01X1"));
  auto full_retry = make_region_allocation_case(kernel, base_image, 14U, both,
      PackedLogic4::from_msb_string("1X0Z"),
      PackedLogic4::from_msb_string("ZZ01"));

  auto rejected_image = full_first.image;
  rejected_image.ready_processes[1U] = 99U;
  rejected_image.requests[1U].process = 99U;
  rejected_image.requests[1U].origin.stable_order = 999U;
  rejected_image.requests[1U].origin.sequence = 999U;
  rejected_image.scheduler_prefix.tasks[1U].member
      = rejected_image.requests[1U];

  const auto warm_case = [&](const RegionKernelAllocationCase& activation) {
    require(executor->execute(activation.image),
        "native region allocation entry is warmed before measurement");
    compare_region_allocation_case(kernel, activation, *executor);
  };
  warm_case(full_first);
  warm_case(full_second);
  warm_case(producer);
  warm_case(consumer);
  warm_case(full_retry);
  const auto* const stable_registers = executor->activation_registers().data();

  // This isolated test executable links the complete global-new interceptor:
  // ordinary, nothrow, aligned and aligned+nothrow scalar/array APIs feed these
  // counters. Direct malloc-family calls and custom allocators that bypass
  // replaceable global new are outside this C++ allocation-request witness.
  // The malformed-image retry below is a normal validation decline, not an
  // injected allocation-failure or recovery test.
  const auto rejected = execute_with_allocation_capture(
      *executor, rejected_image);
  require(!rejected.completed,
      "a malformed image is rejected before native execution");
  require_zero_allocation_delta(rejected);

  const auto run_case = [&](const RegionKernelAllocationCase& activation) {
    const auto measurement = execute_with_allocation_capture(
        *executor, activation.image);
    require(measurement.completed,
        "a prebuilt valid image completes after the rejected image");
    require_zero_allocation_delta(measurement);
    compare_region_allocation_case(kernel, activation, *executor);
    require(executor->activation_registers().data() == stable_registers,
        "steady activations retain the same output vector storage");
  };

  run_case(full_first);
  run_case(full_second);
  run_case(producer);
  run_case(consumer);
  run_case(full_retry);
}

[[nodiscard]] PackedLogic4 make_logic9_value(
    const std::uint32_t width, const std::uint32_t seed)
{
  constexpr std::string_view states { "UX01ZWLH-" };
  std::string pattern;
  pattern.reserve(width);
  for (std::uint32_t bit = 0U; bit < width; ++bit) {
    pattern.push_back(states[(bit + seed) % states.size()]);
  }
  return PackedLogic4::from_logic9_msb_string(pattern);
}

class TemporaryRegionCacheDirectory final {
public:
  TemporaryRegionCacheDirectory()
  {
    static std::atomic<std::uint64_t> next_directory { 0U };
    const auto timestamp = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path()
        / ("fsim-region-logic9-cache-" + std::to_string(timestamp) + "-"
            + std::to_string(next_directory.fetch_add(
                1U, std::memory_order_relaxed)));
    std::error_code error;
    const bool created = std::filesystem::create_directories(path_, error);
    require(created && !error,
        "persistent Logic9 cache fixture creates a unique directory");
  }

  ~TemporaryRegionCacheDirectory() noexcept
  {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  TemporaryRegionCacheDirectory(const TemporaryRegionCacheDirectory&) = delete;
  TemporaryRegionCacheDirectory& operator=(
      const TemporaryRegionCacheDirectory&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const noexcept
  {
    return path_;
  }

private:
  std::filesystem::path path_;
};

using RegionCacheFile = std::pair<std::filesystem::path, std::uintmax_t>;

[[nodiscard]] std::vector<RegionCacheFile> region_cache_files(
    const std::filesystem::path& root)
{
  std::vector<RegionCacheFile> result;
  if (!std::filesystem::exists(root)) {
    return result;
  }
  for (const auto& entry : std::filesystem::recursive_directory_iterator { root }) {
    if (entry.is_regular_file() && entry.path().extension() == ".fobj") {
      result.emplace_back(std::filesystem::relative(entry.path(), root),
          entry.file_size());
    }
  }
  std::ranges::sort(result, std::ranges::less { },
      [](const RegionCacheFile& file) -> const std::filesystem::path& {
        return file.first;
      });
  return result;
}

[[nodiscard]] RegionConeActivationKernel make_logic9_builder_kernel(
    const std::uint32_t boundary_width)
{
  std::vector<RegionSignalDescriptor> signals {
      { boundary_width }, { 4U }, { boundary_width },
      { boundary_width }, { 4U }, { boundary_width },
  };
  signals[0U].value_kind = ValueKind::logic9;
  signals[2U].value_kind = ValueKind::logic9;
  signals[3U].value_kind = ValueKind::logic9;
  signals[5U].value_kind = ValueKind::logic9;
  signals[2U].observations = RegionObservation::current;
  signals[4U].observations = RegionObservation::current;
  signals[5U].observations = RegionObservation::current;

  Process producer;
  producer.id = 0U;
  producer.name = "builder_logic9_region_producer";
  producer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
  producer.register_count = 3U;
  producer.register_value_kinds = {
      ValueKind::logic9, ValueKind::logic9, ValueKind::logic4 };
  producer.static_sensitivity = { { 0U, EdgeKind::any } };
  producer.driver_regions = {
      { 1U, 0U, 0U, true }, { 2U, 0U, 0U, true },
  };
  producer.operations = {
      ReadSignal { 0U, 0U },
      UnaryNot { 1U, 0U },
      WriteUpdate { 2U, 1U, SignalUpdateDomain::systemverilog_active },
      LoadConstant { 2U, PackedLogic4::from_msb_string("10XZ") },
      WriteUpdate { 1U, 2U, SignalUpdateDomain::systemverilog_active },
      LoadConstant { 2U, PackedLogic4::from_msb_string("0Z1X") },
      WaitSensitivity { },
      Jump { 0U },
  };

  Process consumer;
  consumer.id = 1U;
  consumer.name = "builder_logic9_region_consumer";
  consumer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
  consumer.register_count = 4U;
  consumer.register_value_kinds = {
      ValueKind::logic4, ValueKind::logic4,
      ValueKind::logic9, ValueKind::logic9,
  };
  consumer.static_sensitivity = {
      { 1U, EdgeKind::any }, { 3U, EdgeKind::any },
  };
  consumer.driver_regions = {
      { 4U, 0U, 0U, true }, { 5U, 0U, 0U, true },
  };
  consumer.operations = {
      ReadSignal { 0U, 1U },
      UnaryNot { 1U, 0U },
      WriteUpdate { 4U, 1U, SignalUpdateDomain::systemverilog_active },
      LoadConstant { 1U, PackedLogic4::from_msb_string("0XZ1") },
      ReadSignal { 2U, 3U },
      UnaryNot { 3U, 2U },
      WriteUpdate { 5U, 3U, SignalUpdateDomain::systemverilog_active },
      CopyRegister { 3U, 2U },
      WaitSensitivity { },
      Jump { 0U },
  };

  std::vector<Process> processes;
  processes.push_back(std::move(producer));
  processes.push_back(std::move(consumer));
  std::vector<const Process*> process_bindings;
  process_bindings.reserve(processes.size());
  for (const auto& process : processes) {
    process_bindings.push_back(&process);
  }

  const auto graph = RegionGraph::build(process_bindings, signals);
  const auto& components = graph.certificate_inventory().components;
  require(components.size() == 1U
          && components.front().members
              == std::vector<ProcessId> { 0U, 1U }
          && components.front().structural_internal_signal_candidates
              == std::vector<SignalId> { 1U },
      "Logic9 builder fixture keeps only its four-bit Logic4 link internal");
  const auto program = graph.build_compute_program(0U, process_bindings);
  require(program.has_value(),
      "RegionGraph builds mixed Logic4/Logic9 activation programs");
  auto kernel = program->activation_kernel;
  require(kernel.inputs.size() == 3U && kernel.outputs.size() >= 4U,
      "mixed fixture carries two Logic9 boundaries and a Logic4 link");
  require(std::ranges::any_of(kernel.program.register_value_kinds,
              [](const ValueKind kind) { return kind == ValueKind::logic9; })
          && std::ranges::any_of(kernel.program.register_value_kinds,
              [](const ValueKind kind) { return kind == ValueKind::logic4; }),
      "one activation frame contains both Logic4 and Logic9 registers");
  for (const auto& output : kernel.outputs) {
    const auto owner = std::ranges::find(kernel.members, output.owner,
        &RegionConeKernelMember::process);
    require(owner != kernel.members.end()
            && output.kernel_instruction >= owner->begin
            && output.kernel_instruction < owner->end,
        "Logic9 output snapshots retain their source member segment");
    for (const auto& member : kernel.members) {
      for (const auto& binding : member.register_bindings) {
        require(binding.activation_register != output.value_register,
            "Logic9 output snapshots remain distinct from source registers");
      }
    }
  }
  return kernel;
}

[[nodiscard]] RegionKernelActivationImage make_logic9_builder_image(
    const RegionConeActivationKernel& kernel,
    const std::uint64_t generation,
    const std::span<const std::size_t> active_members,
    const PackedLogic4& producer_input,
    const PackedLogic4& consumer_input,
    const PackedLogic4& internal_input)
{
  RegionKernelActivationImage image;
  image.generation = generation;
  image.active_member_indices.assign(
      active_members.begin(), active_members.end());
  image.scheduler_prefix.frontier_generation = generation + 100U;
  image.scheduler_prefix.frontier_cursor = 0U;
  image.scheduler_prefix.frontier_end = active_members.size();
  image.scheduler_prefix.time = 0U;
  image.scheduler_prefix.delta = generation - 1U;
  image.scheduler_prefix.phase = SchedulerPhase::active;
  image.scheduler_prefix.systemverilog_round = generation + 200U;
  for (std::size_t ordinal = 0U; ordinal < active_members.size(); ++ordinal) {
    const auto member_index = active_members[ordinal];
    require(member_index < kernel.members.size(),
        "Logic9 image selects a real graph-built member");
    const auto process = kernel.members[member_index].process;
    const RegionKernelReadyMember request {
        process,
        Process::full_static_trigger_mask,
        RegionKernelActivationOrigin {
            ProcessSchedulingDomain::systemverilog,
            SchedulerPhase::active,
            0U,
            generation - 1U,
            static_cast<StableOrder>(100U + process),
            static_cast<std::uint64_t>(200U + process),
            generation + 200U,
        },
    };
    image.ready_processes.push_back(process);
    image.requests.push_back(request);
    image.scheduler_prefix.tasks.push_back({ ordinal, request });
  }

  std::vector<std::pair<RegisterId, PackedLogic4>> register_inputs;
  register_inputs.reserve(kernel.inputs.size() + kernel.members.size());
  for (const auto& input : kernel.inputs) {
    if (input.internal) {
      register_inputs.emplace_back(input.value_register, internal_input);
    } else if (input.signal == 3U) {
      register_inputs.emplace_back(input.value_register, consumer_input);
    } else if (input.signal == 0U || input.signal == 1U) {
      register_inputs.emplace_back(input.value_register, producer_input);
    } else {
      throw std::runtime_error {
          "Logic9 builder image contains an unexpected signal input" };
    }
  }
  for (std::size_t member_index = 0U;
       member_index < kernel.members.size(); ++member_index) {
    const bool ready = std::ranges::binary_search(
        active_members, member_index);
    register_inputs.emplace_back(
        kernel.members[member_index].readiness_register,
        PackedLogic4 { 1U, ready ? Logic4::one : Logic4::zero });
  }
  std::ranges::sort(register_inputs, std::ranges::less { },
      [](const auto& entry) { return entry.first; });
  image.register_inputs.reserve(register_inputs.size());
  for (auto& [register_id, value] : register_inputs) {
    image.register_inputs.push_back({ register_id, std::move(value) });
  }
  return image;
}

[[nodiscard]] PackedLogic4 logic9_cycle_for_test(
    const std::uint32_t width, const std::size_t start = 0U)
{
  constexpr std::string_view states { "UX01ZWLH-" };
  std::string text;
  text.reserve(width);
  for (std::size_t index = 0U; index < width; ++index) {
    text.push_back(states[(index + start) % states.size()]);
  }
  return PackedLogic4::from_logic9_msb_string(text);
}

void check_guarded_logic9_constant_input_specialization(
    const fsim::compiler::JitOptimizationLevel optimization,
    const std::uint32_t width)
{
  using fsim::compiler::RegionKernelSpecialization;
  using fsim::compiler::llvm_detail::RegionKernelBodySelection;

  auto kernel = make_logic9_builder_kernel(width);
  const auto boundary = std::ranges::find_if(kernel.inputs,
      [](const RegionConeKernelInput& input) {
        return !input.internal && input.signal == 3U
            && input.value_kind == ValueKind::logic9;
      });
  require(boundary != kernel.inputs.end(),
      "the graph-built fixture exposes its Logic9 consumer boundary");
  const auto expected_constant = logic9_cycle_for_test(width);
  kernel.constant_inputs.push_back({ 99U, boundary->signal, 0U,
      width, ValueKind::logic9,
      SignalUpdateDomain::systemverilog_active, expected_constant });

  fsim::compiler::LlvmJitOptions options;
  options.optimization = optimization;
  options.cache_directory.clear();
  auto dynamic_kernel = kernel;
  dynamic_kernel.constant_inputs.clear();
  auto dynamic_executor = fsim::compiler::LlvmRegionKernelExecutor::try_create(
      dynamic_kernel, options, "region-logic9-constant-input-v1",
      RegionKernelSpecialization::dynamic_inputs);
  auto constant_executor = fsim::compiler::LlvmRegionKernelExecutor::try_create(
      kernel, options, "region-logic9-constant-input-v1",
      RegionKernelSpecialization::guarded_constant_inputs);
  auto projected_kernel = kernel;
  auto& projected_constant = projected_kernel.constant_inputs.front();
  projected_constant.domain = SignalUpdateDomain::generic;
  projected_constant.update_kind = RegionUpdateKind::vhdl_projected;
  projected_constant.projected_mode = width == 65U
      ? ProjectedDelayMode::transport : ProjectedDelayMode::inertial;
  projected_constant.projected_delay = 3U;
  projected_constant.projected_rejection
      = projected_constant.projected_mode == ProjectedDelayMode::transport
      ? 5U : 2U;
  auto projected_executor
      = fsim::compiler::LlvmRegionKernelExecutor::try_create(
          projected_kernel, options, "region-logic9-constant-input-v1",
          RegionKernelSpecialization::guarded_constant_inputs);
  require(dynamic_executor != nullptr && constant_executor != nullptr
          && projected_executor != nullptr,
      "Logic9 dynamic, active-constant, and VHDL projected-constant bodies are admitted");
  require(dynamic_executor->cache_identity()
          != constant_executor->cache_identity(),
      "Logic9 constant specialization has its own native identity");
  require(projected_executor->cache_identity()
              != constant_executor->cache_identity()
          && fsim::compiler::llvm_detail::RegionKernelTestAccess::
                 native_code_identity(*projected_executor)
              != fsim::compiler::llvm_detail::RegionKernelTestAccess::
                 native_code_identity(*constant_executor),
      "VHDL projected scheduling metadata has a separate guarded code identity");
  const auto* const projected_template
      = fsim::compiler::llvm_detail::RegionKernelTestAccess::
          prepared_template_identity(*projected_executor);
  require(projected_template != nullptr
          && projected_template
              != fsim::compiler::llvm_detail::RegionKernelTestAccess::
                  prepared_template_identity(*constant_executor),
      "projected domain and timing fields distinguish prepared templates");

  const std::array<std::size_t, 2U> active_members { 0U, 1U };
  const auto matching_image = make_logic9_builder_image(kernel, 1U,
      active_members, logic9_cycle_for_test(width, 1U), expected_constant,
      PackedLogic4::from_msb_string("10XZ"));
  const auto compare_with_reference = [&](
      const RegionKernelActivationImage& image,
      fsim::compiler::LlvmRegionKernelExecutor& executor) {
    const auto expected = evaluate_region_activation_kernel_reference(
        kernel, image);
    require(executor.execute(image),
        "Logic9 constant and dynamic bodies execute the captured image");
    const auto actual = executor.activation_registers();
    compare_defined_member_registers(kernel, image, expected, actual);
    for (const auto& output : kernel.outputs) {
      require(actual[output.value_register] == expected[output.value_register],
          "Logic9 constants preserve every original output snapshot");
    }
  };
  compare_with_reference(matching_image, *constant_executor);
  require(fsim::compiler::llvm_detail::RegionKernelTestAccess::
              last_body_selection(*constant_executor)
              == RegionKernelBodySelection::guarded_constant_inputs,
      "the exact nine-state input selects the guarded native body");
  compare_with_reference(matching_image, *projected_executor);
  require(fsim::compiler::llvm_detail::RegionKernelTestAccess::
              last_body_selection(*projected_executor)
              == RegionKernelBodySelection::guarded_constant_inputs,
      "the exact nine-state input selects the guarded VHDL projected body");
  compare_with_reference(matching_image, *dynamic_executor);

  const std::vector<PackedLogic4> active_retained(
      constant_executor->activation_registers().begin(),
      constant_executor->activation_registers().end());
  const std::vector<PackedLogic4> projected_retained(
      projected_executor->activation_registers().begin(),
      projected_executor->activation_registers().end());
  auto changed_image = matching_image;
  const auto changed_input = std::ranges::find(changed_image.register_inputs,
      boundary->value_register, &RegionKernelRegisterInput::register_id);
  require(changed_input != changed_image.register_inputs.end(),
      "the Logic9 mismatch image retains the guarded input register");
  auto forced_value = expected_constant;
  forced_value.set_logic9(width - 1U, Logic9::z);
  changed_input->value = forced_value;
  require(!constant_executor->execute(changed_image)
          && std::ranges::equal(constant_executor->activation_registers(),
              active_retained),
      "a changed Logic9 force or deposit declines before changing active exports");
  require(!projected_executor->execute(changed_image)
          && std::ranges::equal(projected_executor->activation_registers(),
              projected_retained),
      "a changed Logic9 force or deposit declines before changing projected exports");
  compare_with_reference(changed_image, *dynamic_executor);
  require(fsim::compiler::llvm_detail::RegionKernelTestAccess::
              last_body_selection(*dynamic_executor)
              == RegionKernelBodySelection::four_state,
      "the dynamic Logic9 entry remains the checked fallback");

  auto invalid_projected_kernel = projected_kernel;
  invalid_projected_kernel.constant_inputs.front().domain
      = SignalUpdateDomain::systemverilog_active;
  require(fsim::compiler::LlvmRegionKernelExecutor::try_create(
              invalid_projected_kernel, options,
              "region-logic9-constant-input-v1",
              RegionKernelSpecialization::guarded_constant_inputs)
              == nullptr,
      "mixed SystemVerilog and VHDL projected constant metadata is rejected");
  invalid_projected_kernel = projected_kernel;
  invalid_projected_kernel.constant_inputs.front().projected_mode
      = static_cast<ProjectedDelayMode>(255U);
  require(fsim::compiler::LlvmRegionKernelExecutor::try_create(
              invalid_projected_kernel, options,
              "region-logic9-constant-input-v1",
              RegionKernelSpecialization::guarded_constant_inputs)
      == nullptr,
      "an unknown projected delay mode cannot enter a guarded body");
  invalid_projected_kernel = projected_kernel;
  invalid_projected_kernel.constant_inputs.front().projected_mode
      = ProjectedDelayMode::inertial;
  invalid_projected_kernel.constant_inputs.front().projected_rejection
      = invalid_projected_kernel.constant_inputs.front().projected_delay + 1U;
  require(fsim::compiler::LlvmRegionKernelExecutor::try_create(
              invalid_projected_kernel, options,
              "region-logic9-constant-input-v1",
              RegionKernelSpecialization::guarded_constant_inputs)
              == nullptr,
      "inertial rejection beyond the projected delay is rejected");

  if (optimization == fsim::compiler::JitOptimizationLevel::o0
      && width == 9U) {
    auto changed_mode_kernel = projected_kernel;
    changed_mode_kernel.constant_inputs.front().projected_mode
        = ProjectedDelayMode::transport;
    auto changed_mode
        = fsim::compiler::LlvmRegionKernelExecutor::try_create(
            changed_mode_kernel, options,
            "region-logic9-constant-input-v1",
            RegionKernelSpecialization::guarded_constant_inputs);
    require(changed_mode != nullptr
            && changed_mode->cache_identity()
                != projected_executor->cache_identity()
            && fsim::compiler::llvm_detail::RegionKernelTestAccess::
                   native_code_identity(*changed_mode)
                != fsim::compiler::llvm_detail::RegionKernelTestAccess::
                   native_code_identity(*projected_executor)
            && fsim::compiler::llvm_detail::RegionKernelTestAccess::
                   prepared_template_identity(*changed_mode)
                != projected_template,
        "changing projected delay mode changes guarded identity");

    auto changed_timing_kernel = projected_kernel;
    changed_timing_kernel.constant_inputs.front().projected_delay += 1U;
    auto changed_timing
        = fsim::compiler::LlvmRegionKernelExecutor::try_create(
            changed_timing_kernel, options,
            "region-logic9-constant-input-v1",
            RegionKernelSpecialization::guarded_constant_inputs);
    require(changed_timing != nullptr
            && changed_timing->cache_identity()
                != projected_executor->cache_identity()
            && fsim::compiler::llvm_detail::RegionKernelTestAccess::
                   native_code_identity(*changed_timing)
                != fsim::compiler::llvm_detail::RegionKernelTestAccess::
                   native_code_identity(*projected_executor)
            && fsim::compiler::llvm_detail::RegionKernelTestAccess::
                   prepared_template_identity(*changed_timing)
                != projected_template,
        "changing projected delay changes guarded template identity");

    auto changed_rejection_kernel = projected_kernel;
    changed_rejection_kernel.constant_inputs.front().projected_rejection -= 1U;
    auto changed_rejection
        = fsim::compiler::LlvmRegionKernelExecutor::try_create(
            changed_rejection_kernel, options,
            "region-logic9-constant-input-v1",
            RegionKernelSpecialization::guarded_constant_inputs);
    require(changed_rejection != nullptr
            && changed_rejection->cache_identity()
                != projected_executor->cache_identity()
            && fsim::compiler::llvm_detail::RegionKernelTestAccess::
                   native_code_identity(*changed_rejection)
                != fsim::compiler::llvm_detail::RegionKernelTestAccess::
                   native_code_identity(*projected_executor)
            && fsim::compiler::llvm_detail::RegionKernelTestAccess::
                   prepared_template_identity(*changed_rejection)
                != projected_template,
        "changing projected rejection time changes guarded template identity");
  }

  if (width == 9U
      && optimization == fsim::compiler::JitOptimizationLevel::o0) {
    const auto original_low_aval = expected_constant.aval_words();
    const auto original_low_bval = expected_constant.bval_words();
    const auto original_plane2 = expected_constant.logic9_plane_words(2U);
    const auto original_plane3 = expected_constant.logic9_plane_words(3U);
    for (const auto upper_state : { Logic9::z, Logic9::dont_care }) {
      auto changed_kernel = kernel;
      auto changed_constant = expected_constant;
      changed_constant.set_logic9(width - 1U, upper_state);
      require(std::ranges::equal(
                  changed_constant.aval_words(), original_low_aval)
              && std::ranges::equal(
                  changed_constant.bval_words(), original_low_bval)
              && ((upper_state == Logic9::z
                      && !std::ranges::equal(
                          changed_constant.logic9_plane_words(2U),
                          original_plane2))
                  || (upper_state == Logic9::dont_care
                      && !std::ranges::equal(
                          changed_constant.logic9_plane_words(3U),
                          original_plane3))),
          "the identity cases differ only in Logic9 upper planes");
      changed_kernel.constant_inputs.front().value = changed_constant;
      auto changed_executor
          = fsim::compiler::LlvmRegionKernelExecutor::try_create(
              changed_kernel, options, "region-logic9-constant-input-v1",
              RegionKernelSpecialization::guarded_constant_inputs);
      require(changed_executor != nullptr
              && changed_executor->cache_identity()
                  != constant_executor->cache_identity()
              && fsim::compiler::llvm_detail::RegionKernelTestAccess::
                     native_code_identity(*changed_executor)
                  != fsim::compiler::llvm_detail::RegionKernelTestAccess::
                     native_code_identity(*constant_executor),
          "Logic9 upper-plane changes produce distinct mapping and "
          "native-code identities");
    }
  }
}

[[nodiscard]] std::string check_logic9_builder_kernel_matches_reference(
    const fsim::compiler::JitOptimizationLevel optimization,
    const std::uint32_t width,
    std::filesystem::path cache_directory = {})
{
  const auto kernel = make_logic9_builder_kernel(width);
  fsim::compiler::LlvmJitOptions options;
  options.optimization = optimization;
  options.cache_directory = std::move(cache_directory);
  auto executor = fsim::compiler::LlvmRegionKernelExecutor::try_create(
      kernel, options, "region-native-logic9-builder-fixture-v1");
  require(executor != nullptr,
      "native frame exports accept builder-produced Logic9 regions");
  const auto identity = std::string { executor->cache_identity() };
  const auto* const register_storage = executor->activation_registers().data();
  PackedLogic4 expected_retained_output;

  const auto compare_activation = [&](const std::uint64_t generation,
                                      const std::span<const std::size_t> active,
                                      const PackedLogic4& producer_input,
                                      const PackedLogic4& consumer_input,
                                      const PackedLogic4& internal_input) {
    const auto image = make_logic9_builder_image(kernel, generation, active,
        producer_input, consumer_input, internal_input);
    const auto expected = evaluate_region_activation_kernel_reference(
        kernel, image);
    require(executor->execute(image),
        "Logic9 inputs and mixed-kind registers complete through frame planes");
    const auto actual = executor->activation_registers();
    compare_defined_member_registers(kernel, image, expected, actual);
    for (const auto& output : kernel.outputs) {
      const bool owner_active = std::ranges::any_of(
          active, [&](const std::size_t member_index) {
            return kernel.members[member_index].process == output.owner;
          });
      if (!owner_active) {
        continue;
      }
      require(actual[output.value_register]
              == expected[output.value_register]
              && actual[output.value_register].width() == output.width
              && actual[output.value_register].is_logic9()
                  == (output.value_kind == ValueKind::logic9),
          "each mixed-kind output snapshot matches the checked reference");
    }
    if (generation == 1U) {
      const auto output = std::ranges::find_if(kernel.outputs,
          [](const RegionConeOutputBinding& binding) {
            return binding.owner == 0U && binding.signal == 2U;
          });
      require(output != kernel.outputs.end(),
          "first Logic9 activation has a producer snapshot");
      expected_retained_output = expected[output->value_register];
    }
    require(actual.data() == register_storage
            && executor->cache_identity() == identity,
        "Logic9 activations reuse their bound result storage and identity");
  };

  constexpr std::array both { std::size_t { 0U }, std::size_t { 1U } };
  constexpr std::array producer_only { std::size_t { 0U } };
  constexpr std::array consumer_only { std::size_t { 1U } };
  const auto producer_first = make_logic9_value(width, 0U);
  const auto consumer_first = make_logic9_value(width, 3U);
  constexpr std::string_view states { "UX01ZWLH-" };
  for (const auto state : states) {
    require(producer_first.to_msb_string().find(state)
                != std::string::npos,
        "each Logic9 input fixture contains all nine canonical states");
  }

  const auto producer_output = std::ranges::find_if(kernel.outputs,
      [](const RegionConeOutputBinding& output) {
        return output.owner == 0U && output.signal == 2U;
      });
  require(producer_output != kernel.outputs.end(),
      "Logic9 retained-copy fixture finds the producer boundary output");
  compare_activation(1U, both, producer_first, consumer_first,
      PackedLogic4::from_msb_string("10XZ"));
  const PackedLogic4 retained_output
      = executor->activation_registers()[producer_output->value_register];
  require(retained_output == expected_retained_output,
      "the Logic9 publication snapshot is retained after native return");
  compare_activation(2U, both, make_logic9_value(width, 1U),
      make_logic9_value(width, 5U), PackedLogic4::from_msb_string("01ZX"));
  require(retained_output
              != executor->activation_registers()[producer_output->value_register]
          && retained_output == expected_retained_output,
      "the previous Logic9 result copy stays alive across a changed activation");
  compare_activation(3U, producer_only, make_logic9_value(width, 6U),
      make_logic9_value(width, 2U), PackedLogic4::from_msb_string("XZ10"));
  compare_activation(4U, consumer_only, make_logic9_value(width, 4U),
      make_logic9_value(width, 7U), PackedLogic4::from_msb_string("Z10X"));

  auto valid_retry = make_logic9_builder_image(kernel, 5U, both,
      make_logic9_value(width, 2U), make_logic9_value(width, 8U),
      PackedLogic4::from_msb_string("X01Z"));
  const std::vector<PackedLogic4> prior_registers {
      executor->activation_registers().begin(),
      executor->activation_registers().end() };
  auto malformed = valid_retry;
  const auto bad_input = std::ranges::find_if(malformed.register_inputs,
      [&](const RegionKernelRegisterInput& value) {
        return value.value.width() == width && value.value.is_logic9();
      });
  require(bad_input != malformed.register_inputs.end(),
      "Logic9 malformed image identifies a boundary input");
  bad_input->value = PackedLogic4 { width, Logic4::x };
  require(!executor->execute(malformed),
      "a Logic4 value cannot enter a Logic9 signal binding");
  require(std::ranges::equal(prior_registers,
              executor->activation_registers()),
      "rejected Logic9 images leave all prior result registers unchanged");
  compare_activation(5U, both, make_logic9_value(width, 2U),
      make_logic9_value(width, 8U), PackedLogic4::from_msb_string("X01Z"));
  return identity;
}

void check_logic9_native_cache_cold_warm_reload(
    const fsim::compiler::JitOptimizationLevel optimization,
    const std::uint32_t width)
{
  TemporaryRegionCacheDirectory cache_directory;
  require(region_cache_files(cache_directory.path()).empty(),
      "cold Logic9 cache fixture starts without native objects");
  const auto cold_identity = check_logic9_builder_kernel_matches_reference(
      optimization, width, cache_directory.path());
  const auto cold_objects = region_cache_files(cache_directory.path());
  require(!cold_objects.empty()
          && std::ranges::all_of(cold_objects,
              [](const RegionCacheFile& file) { return file.second != 0U; }),
      "cold Logic9 native construction writes nonempty persistent objects");

  // The second call constructs a new wrapper and a new LlvmJit instance against
  // the same disk cache. Replaying full and subset activations checks the
  // reloaded kernel against the same graph-built reference program.
  const auto warm_identity = check_logic9_builder_kernel_matches_reference(
      optimization, width, cache_directory.path());
  const auto warm_objects = region_cache_files(cache_directory.path());
  require(warm_identity == cold_identity,
      "cold and warm Logic9 wrappers use the same native cache identity");
  require(warm_objects == cold_objects,
      "warm Logic9 construction reuses the persistent native object inventory");
}

void check_wide_internal_activation(
    const fsim::compiler::JitOptimizationLevel optimization,
    const std::uint32_t width,
    const ValueKind kind)
{
  const auto value = [&](const std::size_t rotation) {
    const std::string_view states = kind == ValueKind::logic9
        ? "UX01ZWLH-" : "01XZ";
    std::string digits;
    digits.reserve(width);
    for (std::size_t bit = 0U; bit < width; ++bit) {
      digits.push_back(states[(bit + rotation) % states.size()]);
    }
    return kind == ValueKind::logic9
        ? PackedLogic4::from_logic9_msb_string(digits)
        : PackedLogic4::from_msb_string(digits);
  };
  std::vector<RegionSignalDescriptor> descriptors(
      3U, RegionSignalDescriptor { width, ResolutionKind::none, kind });
  descriptors[2U].observations = RegionObservation::current;
  std::array<Process, 2U> processes;
  for (ProcessId id = 0U; id < processes.size(); ++id) {
    auto& process = processes[id];
    const SignalId source_signal = id == 0U ? 1U : 0U;
    const SignalId output_signal = id == 0U ? 0U : 2U;
    process.id = id;
    process.name = "wide_internal_member_" + std::to_string(id);
    process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    process.register_count = 2U;
    process.register_value_kinds.assign(2U, kind);
    process.static_sensitivity = { { source_signal, EdgeKind::any } };
    process.driver_regions = { { output_signal, 0U, 0U, true } };
    process.operations = {
        ReadSignal { 0U, source_signal },
        UnaryNot { 1U, 0U },
        WriteUpdate { output_signal, 1U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };
  }
  const std::array<const Process*, 2U> bindings {
      &processes[0U], &processes[1U]
  };
  const auto graph = RegionGraph::build(bindings, descriptors);
  const auto& components = graph.certificate_inventory().components;
  require(components.size() == 1U
          && components[0U].structural_internal_signal_candidates
              == std::vector<SignalId> { 0U },
      "signal-zero internal link is certified while output stays a boundary");
  const auto program = graph.build_compute_program(0U, bindings);
  require(program.has_value(),
      "graph builds exact activation for narrow and wide Logic4/Logic9 links");
  const auto& kernel = program->activation_kernel;
  require(kernel.internal_signals == std::vector<SignalId> { 0U },
      "signal-zero link remains an explicitly captured committed input");
  fsim::compiler::LlvmJitOptions options;
  options.optimization = optimization;
  auto executor = fsim::compiler::LlvmRegionKernelExecutor::try_create(
      kernel, options, "wide-internal-exact-activation-v1");
  require(executor != nullptr,
      "wide internal graph binds to the native backend");
  const auto input = value(0U);
  const auto initial_link = value(3U);
  const auto find_output = [&](const SignalId signal) {
    const auto found = std::ranges::find(kernel.outputs, signal,
        &RegionConeOutputBinding::signal);
    require(found != kernel.outputs.end(),
        "each original owner retains a publication snapshot");
    return found->value_register;
  };
  const auto link_register = find_output(0U);
  const auto output_register = find_output(2U);
  const auto internal_input = std::ranges::find_if(kernel.inputs,
      [](const RegionConeKernelInput& input) { return input.internal; });
  require(internal_input != kernel.inputs.end(),
      "wide internal activation has a captured component-plane input");
  const auto check_activation = [&](const std::uint64_t generation,
                                    const std::span<const std::size_t> active,
                                    const PackedLogic4& committed_link) {
    const auto image = make_logic9_builder_image(kernel, generation,
        active, input, input, committed_link);
    const auto expected = evaluate_region_activation_kernel_reference(kernel, image);
    const auto image_input = std::ranges::lower_bound(image.register_inputs,
        internal_input->value_register, std::ranges::less { },
        &RegionKernelRegisterInput::register_id);
    require(image_input != image.register_inputs.end()
            && image_input->register_id == internal_input->value_register,
        "captured internal input has a matching packed register");
    std::array<std::span<const std::uint64_t>, 4U> planes;
    if (kind == ValueKind::logic9) {
      for (std::size_t plane = 0U; plane < planes.size(); ++plane) {
        planes[plane] = image_input->value.logic9_plane_words(plane);
      }
    } else {
      planes[0U] = image_input->value.aval_words();
      planes[1U] = image_input->value.bval_words();
    }
    const RegionKernelInputPlane borrowed_input {
        internal_input->signal, internal_input->value_register,
        internal_input->width, kind, planes };
    require(executor->execute(image),
        "the checked activation API still completes from a packed image");
    const std::vector<PackedLogic4> checked_actual {
        executor->activation_registers().begin(),
        executor->activation_registers().end() };
    compare_defined_member_registers(kernel, image, expected, checked_actual);
    require(executor->execute_with_input_planes(image,
                std::span<const RegionKernelInputPlane> {
                    &borrowed_input, 1U }),
        "wide internal activation executes from borrowed Logic4 or Logic9 planes");
    const auto actual = executor->activation_registers();
    compare_defined_member_registers(kernel, image, expected, actual);
    for (const auto& output : kernel.outputs) {
      if (std::ranges::any_of(active, [&](const std::size_t member) {
            return kernel.members[member].process == output.owner;
          })) {
        require(actual[output.value_register] == expected[output.value_register],
            "wide owner snapshots match the exact committed-input reference");
      }
    }

    if (kind == ValueKind::logic4 && width <= 64U
        && std::ranges::binary_search(active, std::size_t { 0U })) {
      using fsim::compiler::llvm_detail::RegionKernelBodySelection;
      using fsim::compiler::llvm_detail::RegionKernelTestAccess;
      const auto internal_output = std::ranges::find(kernel.outputs, 0U,
          &RegionConeOutputBinding::signal);
      const auto boundary_output = std::ranges::find(kernel.outputs, 2U,
          &RegionConeOutputBinding::signal);
      require(internal_output != kernel.outputs.end()
              && boundary_output != kernel.outputs.end(),
          "prefix fixture identifies the internal and boundary snapshots");
      const std::array ordered_internal_prefix { *internal_output };
      const std::array current_values { committed_link };
      require(executor->execute_internal_output_prefix(
                  image, current_values, ordered_internal_prefix),
          "native prefix entry compares a generated internal output to the committed cut");
      auto changed = executor->internal_output_prefix_changed();
      require(changed.size() == 1U
              && changed.front()
                  == static_cast<std::uint8_t>(
                      expected[link_register] != committed_link),
          "generated case equality returns the exact internal change bit");
      const auto prefix_registers = executor->activation_registers();
      compare_defined_member_registers(
          kernel, image, expected, prefix_registers);

      const auto execute_prepared_case = [&](
          const RegionKernelActivationImage& prepared_image,
          const std::span<const PackedLogic4> prepared_current,
          const std::vector<PackedLogic4>& prepared_expected,
          const std::string_view label) {
        const auto count = kernel.internal_signals.size();
        std::vector<RegionPreparedOutputSlotV1> slots(count);
        std::vector<std::uint64_t> owner_masks(count, 0U);
        std::vector<std::uint64_t> old_current_aval(count, 0U);
        std::vector<std::uint64_t> old_current_bval(count, 0U);
        std::vector<std::uint64_t> old_owner_aval(count, 0U);
        std::vector<std::uint64_t> old_owner_bval(count, 0U);
        std::vector<std::uint64_t> next_current_aval(count, UINT64_MAX);
        std::vector<std::uint64_t> next_current_bval(count, UINT64_MAX);
        std::vector<std::uint64_t> next_last_aval(count, UINT64_C(0x13579));
        std::vector<std::uint64_t> next_last_bval(count, UINT64_C(0x2468a));
        std::vector<std::uint64_t> next_stored_aval(count, UINT64_MAX);
        std::vector<std::uint64_t> next_stored_bval(count, UINT64_MAX);
        std::vector<std::uint64_t> next_owner_aval(count, UINT64_MAX);
        std::vector<std::uint64_t> next_owner_bval(count, UINT64_MAX);
        std::vector<std::uint8_t> changed(count, 0U);
        std::vector<std::uint8_t> value_ready(count, 0U);
        std::vector<std::uint8_t> transaction_ready(count, 0U);
        std::size_t selected_index { count };
        const auto selected_output = std::ranges::find(kernel.outputs,
            internal_output->signal, &RegionConeOutputBinding::signal);
        require(selected_output != kernel.outputs.end(),
            "prepared prefix retains its selected internal owner");
        for (std::size_t index = 0U; index < count; ++index) {
          auto& slot = slots[index];
          const auto signal = kernel.internal_signals[index];
          const auto output = std::ranges::find(kernel.outputs, signal,
              &RegionConeOutputBinding::signal);
          require(output != kernel.outputs.end(),
              "prepared slots map every internal signal to its output");
          slot.struct_size = sizeof(RegionPreparedOutputSlotV1);
          slot.signal_id = signal;
          slot.owner_id = output->owner;
          slot.width = output->width;
          slot.word_count = 1U;
          slot.value_kind = RegionPreparedOutputValueKindV1::logic4;
          if (signal != selected_output->signal) {
            continue;
          }
          selected_index = index;
          slot.selected = 1U;
          owner_masks[index] = output->width == 64U ? UINT64_MAX
              : (UINT64_C(1) << output->width) - 1U;
          next_last_aval[index] &= owner_masks[index];
          next_last_bval[index] &= owner_masks[index];
          const auto old_word = prepared_current[index].low_word();
          old_current_aval[index] = old_word.aval;
          old_current_bval[index] = old_word.bval;
          old_owner_aval[index] = old_word.aval;
          old_owner_bval[index] = old_word.bval;
          slot.owner_mask = &owner_masks[index];
          slot.old_current_aval = &old_current_aval[index];
          slot.old_current_bval = &old_current_bval[index];
          slot.old_owner_aval = &old_owner_aval[index];
          slot.old_owner_bval = &old_owner_bval[index];
          slot.next_current_aval = &next_current_aval[index];
          slot.next_current_bval = &next_current_bval[index];
          slot.next_last_aval = &next_last_aval[index];
          slot.next_last_bval = &next_last_bval[index];
          slot.next_stored_aval = &next_stored_aval[index];
          slot.next_stored_bval = &next_stored_bval[index];
          slot.next_owner_aval = &next_owner_aval[index];
          slot.next_owner_bval = &next_owner_bval[index];
          slot.changed = &changed[index];
          slot.value_ready = &value_ready[index];
          slot.transaction_ready = &transaction_ready[index];
        }
        require(selected_index < count,
            "prepared descriptor includes the selected signal");
        RegionPreparedOutputBatchV1 batch {
            kRegionPreparedOutputBatchAbiVersionV1,
            sizeof(RegionPreparedOutputBatchV1),
            static_cast<std::uint32_t>(slots.size()), 0U, slots.data() };
        std::vector<std::uint64_t> successor_masks(count, UINT64_MAX);
        RegionPreparedOutputSuccessorMasksV1 successors {
            kRegionPreparedOutputSuccessorMasksAbiVersionV1,
            sizeof(RegionPreparedOutputSuccessorMasksV1),
            static_cast<std::uint32_t>(successor_masks.size()), 0U,
            successor_masks.data() };

        const std::vector<PackedLogic4> prior_registers {
            executor->activation_registers().begin(),
            executor->activation_registers().end() };
        const auto prior_read_words = std::array {
            owner_masks, old_current_aval, old_current_bval,
            old_owner_aval, old_owner_bval };
        const auto prior_replacement_words = std::array {
            next_current_aval, next_current_bval,
            next_last_aval, next_last_bval,
            next_stored_aval, next_stored_bval,
            next_owner_aval, next_owner_bval };
        const auto read_words_unchanged = [&] {
          return prior_read_words == std::array {
              owner_masks, old_current_aval, old_current_bval,
              old_owner_aval, old_owner_bval };
        };
        const auto replacement_words_unchanged = [&] {
          return prior_replacement_words == std::array {
              next_current_aval, next_current_bval,
              next_last_aval, next_last_bval,
              next_stored_aval, next_stored_bval,
              next_owner_aval, next_owner_bval };
        };
        const auto prior_ready = std::array {
            changed, value_ready, transaction_ready };
        const auto ready_flags_unchanged = [&] {
          return prior_ready == std::array {
              changed, value_ready, transaction_ready };
        };
        const auto successor_masks_unchanged = [&] {
          return std::ranges::all_of(successor_masks,
              [](const std::uint64_t mask) { return mask == UINT64_MAX; });
        };
        auto bad_version = batch;
        ++bad_version.abi_version;
        require(!executor->execute_internal_output_prefix_prepared(
                    prepared_image, prepared_current, ordered_internal_prefix,
                    bad_version)
                && std::ranges::equal(prior_registers,
                    executor->activation_registers())
                && read_words_unchanged()
                && replacement_words_unchanged()
                && ready_flags_unchanged()
                && changed[selected_index] == 0U
                && value_ready[selected_index] == 0U
                && transaction_ready[selected_index] == 0U,
            "invalid prepared-output version declines before frame or role mutation");

        auto bad_successor_version = successors;
        ++bad_successor_version.abi_version;
        require(!executor->execute_internal_output_prefix_prepared(
                    prepared_image, prepared_current, ordered_internal_prefix,
                    batch, &bad_successor_version)
                && std::ranges::equal(prior_registers,
                    executor->activation_registers())
                && read_words_unchanged()
                && replacement_words_unchanged()
                && ready_flags_unchanged()
                && successor_masks_unchanged(),
            "unknown successor-mask ABI declines before frame, output, or sidecar mutation");
        auto short_successor = successors;
        --short_successor.struct_size;
        require(!executor->execute_internal_output_prefix_prepared(
                    prepared_image, prepared_current, ordered_internal_prefix,
                    batch, &short_successor)
                && std::ranges::equal(prior_registers,
                    executor->activation_registers())
                && read_words_unchanged()
                && replacement_words_unchanged()
                && ready_flags_unchanged()
                && successor_masks_unchanged(),
            "wrong successor-mask ABI size declines before any output mutation");
        auto reserved_successor = successors;
        reserved_successor.reserved = 1U;
        require(!executor->execute_internal_output_prefix_prepared(
                    prepared_image, prepared_current, ordered_internal_prefix,
                    batch, &reserved_successor)
                && std::ranges::equal(prior_registers,
                    executor->activation_registers())
                && read_words_unchanged()
                && replacement_words_unchanged()
                && ready_flags_unchanged()
                && successor_masks_unchanged(),
            "unknown successor-mask capability bits decline atomically");
        auto null_successor = successors;
        null_successor.member_masks = nullptr;
        require(!executor->execute_internal_output_prefix_prepared(
                    prepared_image, prepared_current, ordered_internal_prefix,
                    batch, &null_successor)
                && std::ranges::equal(prior_registers,
                    executor->activation_registers())
                && read_words_unchanged()
                && replacement_words_unchanged()
                && ready_flags_unchanged()
                && successor_masks_unchanged(),
            "null successor storage declines before dereference or output mutation");
        alignas(std::uint64_t)
            std::array<std::byte, sizeof(std::uint64_t) + alignof(std::uint64_t)>
                misaligned_successor_storage { };
        auto misaligned_successor = successors;
        misaligned_successor.member_masks
            = reinterpret_cast<std::uint64_t*>(
                misaligned_successor_storage.data() + 1U);
        require(!executor->execute_internal_output_prefix_prepared(
                    prepared_image, prepared_current, ordered_internal_prefix,
                    batch, &misaligned_successor)
                && std::ranges::equal(prior_registers,
                    executor->activation_registers())
                && read_words_unchanged()
                && replacement_words_unchanged()
                && ready_flags_unchanged()
                && successor_masks_unchanged(),
            "misaligned successor storage declines before dereference or output mutation");
        alignas(RegionPreparedOutputSuccessorMasksV1)
            std::array<std::byte,
                sizeof(RegionPreparedOutputSuccessorMasksV1)
                    + alignof(RegionPreparedOutputSuccessorMasksV1)>
                misaligned_descriptor_storage { };
        std::memcpy(misaligned_descriptor_storage.data() + 1U,
            &successors, sizeof(successors));
        auto* const misaligned_descriptor
            = reinterpret_cast<RegionPreparedOutputSuccessorMasksV1*>(
                misaligned_descriptor_storage.data() + 1U);
        require(!executor->execute_internal_output_prefix_prepared(
                    prepared_image, prepared_current, ordered_internal_prefix,
                    batch, misaligned_descriptor)
                && std::ranges::equal(prior_registers,
                    executor->activation_registers())
                && read_words_unchanged()
                && replacement_words_unchanged()
                && ready_flags_unchanged()
                && successor_masks_unchanged(),
            "misaligned successor descriptor declines before field reads or output mutation");

        auto overlapping_slots = slots;
        overlapping_slots[selected_index].next_current_aval
            = &old_current_aval[selected_index];
        RegionPreparedOutputBatchV1 overlapping_batch {
            kRegionPreparedOutputBatchAbiVersionV1,
            sizeof(RegionPreparedOutputBatchV1),
            static_cast<std::uint32_t>(overlapping_slots.size()), 0U,
            overlapping_slots.data() };
        require(!executor->execute_internal_output_prefix_prepared(
                    prepared_image, prepared_current, ordered_internal_prefix,
                    overlapping_batch)
                && std::ranges::equal(prior_registers,
                    executor->activation_registers())
                && read_words_unchanged()
                && replacement_words_unchanged()
                && ready_flags_unchanged()
                && changed[selected_index] == 0U
                && value_ready[selected_index] == 0U
                && transaction_ready[selected_index] == 0U,
            "aliased prepared destinations decline before frame or role mutation");

        alignas(std::uint64_t)
            std::array<std::byte, sizeof(std::uint64_t) + alignof(std::uint64_t)>
                misaligned_storage { };
        auto misaligned_slots = slots;
        misaligned_slots[selected_index].owner_mask
            = reinterpret_cast<const std::uint64_t*>(
                misaligned_storage.data() + 1U);
        RegionPreparedOutputBatchV1 misaligned_batch {
            kRegionPreparedOutputBatchAbiVersionV1,
            sizeof(RegionPreparedOutputBatchV1),
            static_cast<std::uint32_t>(misaligned_slots.size()), 0U,
            misaligned_slots.data() };
        require(!executor->execute_internal_output_prefix_prepared(
                    prepared_image, prepared_current, ordered_internal_prefix,
                    misaligned_batch)
                && std::ranges::equal(prior_registers,
                    executor->activation_registers())
                && read_words_unchanged()
                && replacement_words_unchanged()
                && ready_flags_unchanged()
                && changed[selected_index] == 0U
                && value_ready[selected_index] == 0U
                && transaction_ready[selected_index] == 0U,
            "misaligned prepared readable planes decline before dereference or frame mutation");

        require(executor->supports_direct_ready_window(),
            "the narrow Logic4 fixture binds the separate direct-window entry");
        auto direct_image = prepared_image;
        direct_image.register_inputs.clear();
        std::vector<std::uint64_t> direct_readiness(
            (kernel.members.size() + 63U) / 64U, 0U);
        for (const auto member : prepared_image.active_member_indices) {
          require(member < kernel.members.size(),
              "direct-window readiness refers to a certified member");
          direct_readiness[member / 64U]
              |= UINT64_C(1) << (member % 64U);
        }
        std::vector<const RegionConeKernelInput*> ordered_inputs;
        ordered_inputs.reserve(kernel.inputs.size());
        for (const auto& input : kernel.inputs) {
          ordered_inputs.push_back(&input);
        }
        std::ranges::sort(ordered_inputs, std::ranges::less { },
            [](const RegionConeKernelInput* const input) {
              return input->value_register;
            });
        std::vector<RegionDirectReadyInputSlotV1> direct_slots;
        direct_slots.reserve(
            kernel.inputs.size() + kernel.internal_signals.size());
        const auto append_direct_slot = [&direct_slots](
            const SignalId signal,
            const RegisterId register_id,
            const std::uint32_t slot_width,
            const PackedLogic4& source) {
          require(source.width() == slot_width && !source.is_logic9(),
              "direct-window slots retain the exact narrow Logic4 shape");
          const auto aval = source.aval_words();
          const auto bval = source.bval_words();
          require(aval.size() == 1U && bval.size() == 1U,
              "direct-window slots borrow one complete word per plane");
          direct_slots.push_back({ sizeof(RegionDirectReadyInputSlotV1),
              signal, register_id, slot_width, 1U, 0U,
              aval.data(), bval.data() });
        };
        for (const auto* const input : ordered_inputs) {
          const PackedLogic4* source { };
          if (input->internal) {
            const auto internal = std::ranges::lower_bound(
                kernel.internal_signals, input->signal);
            require(internal != kernel.internal_signals.end()
                    && *internal == input->signal,
                "direct-window internal input has a current-plane index");
            const auto index = static_cast<std::size_t>(
                internal - kernel.internal_signals.begin());
            require(index < prepared_current.size(),
                "direct-window internal input remains in the committed cut");
            source = &prepared_current[index];
          } else {
            const auto image_input = std::ranges::find_if(
                prepared_image.register_inputs,
                [input](const RegionKernelRegisterInput& value) {
                    return value.register_id == input->value_register;
                });
            require(image_input != prepared_image.register_inputs.end(),
                "direct-window boundary input has its same-call captured value");
            source = &image_input->value;
          }
          append_direct_slot(input->signal, input->value_register,
              input->width, *source);
        }
        for (std::size_t index = 0U;
             index < kernel.internal_signals.size(); ++index) {
          const auto signal = kernel.internal_signals[index];
          const auto output = std::ranges::find(kernel.outputs, signal,
              &RegionConeOutputBinding::signal);
          require(output != kernel.outputs.end(),
              "direct-window committed inputs have a matching output shape");
          const auto register_id = static_cast<std::uint64_t>(
              kernel.program.register_count) + index;
          require(register_id <= std::numeric_limits<RegisterId>::max(),
              "direct-window committed input register fits its ABI field");
          append_direct_slot(signal,
              static_cast<RegisterId>(register_id), output->width,
              prepared_current[index]);
        }
        RegionDirectReadyWindowV1 direct_window;
        direct_window.abi_version = kRegionDirectReadyWindowAbiVersionV1;
        direct_window.struct_size = sizeof(RegionDirectReadyWindowV1);
        direct_window.activation_generation = direct_image.generation;
        direct_window.frontier_generation
            = direct_image.scheduler_prefix.frontier_generation;
        direct_window.member_count
            = static_cast<std::uint32_t>(kernel.members.size());
        direct_window.readiness_word_count
            = static_cast<std::uint32_t>(direct_readiness.size());
        direct_window.input_slot_count
            = static_cast<std::uint32_t>(direct_slots.size());
        direct_window.readiness_mask = direct_readiness.data();
        direct_window.input_slots = direct_slots.data();
        const auto direct_declines_without_mutation = [&](
            const RegionDirectReadyWindowV1& malformed,
            const std::string_view message) {
          require(!executor->execute_direct_ready_window_prepared(
                      direct_image, malformed, prepared_current,
                      ordered_internal_prefix, batch)
                  && std::ranges::equal(prior_registers,
                      executor->activation_registers())
                  && read_words_unchanged()
                  && replacement_words_unchanged()
                  && ready_flags_unchanged(), message);
        };
        const auto malformed_current_declines_without_mutation = [&](
            const std::span<const PackedLogic4> malformed,
            const std::string_view message) {
          require(!executor->execute_direct_ready_window_prepared(
                      direct_image, direct_window, malformed,
                      ordered_internal_prefix, batch)
                  && std::ranges::equal(prior_registers,
                      executor->activation_registers())
                  && read_words_unchanged()
                  && replacement_words_unchanged()
                  && ready_flags_unchanged(), message);
        };
        auto malformed_window = direct_window;
        ++malformed_window.frontier_generation;
        direct_declines_without_mutation(malformed_window,
            "a stale direct-window frontier declines before frame or output mutation");
        auto wrong_abi_version = direct_window;
        ++wrong_abi_version.abi_version;
        direct_declines_without_mutation(wrong_abi_version,
            "an unknown direct-window ABI version declines without mutation");
        auto wrong_input_count = direct_window;
        --wrong_input_count.input_slot_count;
        direct_declines_without_mutation(wrong_input_count,
            "a truncated direct input table declines before frame mutation");
        auto malformed_slots = direct_slots;
        ++malformed_slots.front().width;
        auto malformed_slot_window = direct_window;
        malformed_slot_window.input_slots = malformed_slots.data();
        direct_declines_without_mutation(malformed_slot_window,
            "a mismatched direct input width declines without mutation");
        auto wrong_signal_slots = direct_slots;
        ++wrong_signal_slots.front().signal_id;
        auto wrong_signal_window = direct_window;
        wrong_signal_window.input_slots = wrong_signal_slots.data();
        direct_declines_without_mutation(wrong_signal_window,
            "a direct descriptor with the wrong signal ID declines atomically");
        auto wrong_register_slots = direct_slots;
        ++wrong_register_slots.front().register_id;
        auto wrong_register_window = direct_window;
        wrong_register_window.input_slots = wrong_register_slots.data();
        direct_declines_without_mutation(wrong_register_window,
            "a direct descriptor with the wrong source register declines atomically");
        if (width < 64U) {
          const auto valid_mask
              = (UINT64_C(1) << width) - UINT64_C(1);
          const auto invalid_aval = *direct_slots.front().aval | ~valid_mask;
          auto invalid_tail_slots = direct_slots;
          invalid_tail_slots.front().aval = &invalid_aval;
          auto invalid_tail_window = direct_window;
          invalid_tail_window.input_slots = invalid_tail_slots.data();
          direct_declines_without_mutation(invalid_tail_window,
              "nonzero direct input tail bits decline before frame mutation");
        }
        auto overflowing_slots = direct_slots;
        const auto last_aligned_address
            = std::numeric_limits<std::uintptr_t>::max()
            - (std::numeric_limits<std::uintptr_t>::max()
                % alignof(std::uint64_t));
        overflowing_slots.front().aval
            = reinterpret_cast<const std::uint64_t*>(last_aligned_address);
        auto overflowing_window = direct_window;
        overflowing_window.input_slots = overflowing_slots.data();
        direct_declines_without_mutation(overflowing_window,
            "an overflowing direct source span declines before dereference");
        if (kernel.members.size() % 64U != 0U) {
          auto malformed_readiness = direct_readiness;
          malformed_readiness.back() |= UINT64_C(1) << 63U;
          auto malformed_readiness_window = direct_window;
          malformed_readiness_window.readiness_mask
              = malformed_readiness.data();
          direct_declines_without_mutation(malformed_readiness_window,
              "unused direct readiness tail bits decline before frame mutation");
        }
        if (!prepared_current.empty()) {
          auto wrong_width_current = std::vector<PackedLogic4>(
              prepared_current.begin(), prepared_current.end());
          wrong_width_current.front() = PackedLogic4 { 65U };
          malformed_current_declines_without_mutation(
              std::span<const PackedLogic4> { wrong_width_current },
              "a malformed current-plane width declines without unwinding or mutation");

          auto wrong_kind_current = std::vector<PackedLogic4>(
              prepared_current.begin(), prepared_current.end());
          wrong_kind_current.front() = make_logic9_value(
              static_cast<std::uint32_t>(prepared_current.front().width()),
              0U);
          malformed_current_declines_without_mutation(
              std::span<const PackedLogic4> { wrong_kind_current },
              "a Logic9 current-plane value declines before narrow-word access");
        }

        require(executor->execute_direct_ready_window_prepared(
                    direct_image, direct_window, prepared_current,
                    ordered_internal_prefix, batch),
            "direct signal planes execute the prepared narrow Logic4 window");
        require(RegionKernelTestAccess::last_body_selection(*executor)
                == RegionKernelBodySelection::four_state,
            "direct-window execution selects the certified four-state body");
        const auto direct_expected_value
            = prepared_expected[selected_output->value_register];
        const auto direct_expected_word = direct_expected_value.low_word();
        const bool direct_expected_changed
            = direct_expected_value != prepared_current[selected_index];
        const auto direct_expected_previous = direct_expected_changed
            ? prepared_current[selected_index].low_word()
            : Logic4Word { width,
                UINT64_C(0x13579) & owner_masks[selected_index],
                UINT64_C(0x2468a) & owner_masks[selected_index] };
        require(changed[selected_index]
                    == static_cast<std::uint8_t>(direct_expected_changed)
                && value_ready[selected_index]
                    == static_cast<std::uint8_t>(direct_expected_changed)
                && transaction_ready[selected_index] == 1U
                && next_current_aval[selected_index]
                    == direct_expected_word.aval
                && next_current_bval[selected_index]
                    == direct_expected_word.bval
                && next_stored_aval[selected_index]
                    == direct_expected_word.aval
                && next_stored_bval[selected_index]
                    == direct_expected_word.bval
                && next_owner_aval[selected_index]
                    == direct_expected_word.aval
                && next_owner_bval[selected_index]
                    == direct_expected_word.bval
                && next_last_aval[selected_index]
                    == direct_expected_previous.aval
                && next_last_bval[selected_index]
                    == direct_expected_previous.bval
                && executor->internal_output_prefix_changed().size() == 1U
                && executor->internal_output_prefix_changed().front()
                    == static_cast<std::uint8_t>(direct_expected_changed),
            "direct-window native execution fills the preflighted output planes");
        compare_defined_member_registers(kernel, direct_image,
            prepared_expected, executor->activation_registers());
        std::ranges::copy(prior_replacement_words[0U],
            next_current_aval.begin());
        std::ranges::copy(prior_replacement_words[1U],
            next_current_bval.begin());
        std::ranges::copy(prior_replacement_words[2U],
            next_last_aval.begin());
        std::ranges::copy(prior_replacement_words[3U],
            next_last_bval.begin());
        std::ranges::copy(prior_replacement_words[4U],
            next_stored_aval.begin());
        std::ranges::copy(prior_replacement_words[5U],
            next_stored_bval.begin());
        std::ranges::copy(prior_replacement_words[6U],
            next_owner_aval.begin());
        std::ranges::copy(prior_replacement_words[7U],
            next_owner_bval.begin());
        std::ranges::fill(changed, 0U);
        std::ranges::fill(value_ready, 0U);
        std::ranges::fill(transaction_ready, 0U);

        require(executor->execute_internal_output_prefix_prepared(
                    prepared_image, prepared_current, ordered_internal_prefix,
                    batch), label);
        require(RegionKernelTestAccess::last_body_selection(*executor)
                == RegionKernelBodySelection::four_state,
            "private prepared outputs always use their matching four-state activation body");
        const auto expected_value
            = prepared_expected[selected_output->value_register];
        const auto expected_word = expected_value.low_word();
        const bool expected_changed
            = expected_value != prepared_current[selected_index];
        require(changed[selected_index]
                    == static_cast<std::uint8_t>(expected_changed)
                && value_ready[selected_index]
                    == static_cast<std::uint8_t>(expected_changed)
                && transaction_ready[selected_index] == 1U,
            "prepared prefix keeps value readiness distinct from transaction readiness");
        require(next_current_aval[selected_index] == expected_word.aval
                && next_current_bval[selected_index] == expected_word.bval
                && next_stored_aval[selected_index] == expected_word.aval
                && next_stored_bval[selected_index] == expected_word.bval
                && next_owner_aval[selected_index] == expected_word.aval
                && next_owner_bval[selected_index] == expected_word.bval,
            "generated prepared stores produce current, stored, and raw-owner words");
        const auto expected_previous = expected_changed
            ? prepared_current[selected_index].low_word()
            : Logic4Word { width,
                UINT64_C(0x13579) & owner_masks[selected_index],
                UINT64_C(0x2468a) & owner_masks[selected_index] };
        require(next_last_aval[selected_index] == expected_previous.aval
                && next_last_bval[selected_index] == expected_previous.bval,
            "generated prepared stores update LAST only when current changes");
        require(executor->internal_output_prefix_changed().size() == 1U
                && executor->internal_output_prefix_changed().front()
                    == static_cast<std::uint8_t>(expected_changed),
            "prepared stores retain the generated prefix change result");
        compare_defined_member_registers(kernel, prepared_image,
            prepared_expected, executor->activation_registers());

        std::ranges::copy(prior_replacement_words[0U],
            next_current_aval.begin());
        std::ranges::copy(prior_replacement_words[1U],
            next_current_bval.begin());
        std::ranges::copy(prior_replacement_words[2U],
            next_last_aval.begin());
        std::ranges::copy(prior_replacement_words[3U],
            next_last_bval.begin());
        std::ranges::copy(prior_replacement_words[4U],
            next_stored_aval.begin());
        std::ranges::copy(prior_replacement_words[5U],
            next_stored_bval.begin());
        std::ranges::copy(prior_replacement_words[6U],
            next_owner_aval.begin());
        std::ranges::copy(prior_replacement_words[7U],
            next_owner_bval.begin());
        std::ranges::fill(changed, 0U);
        std::ranges::fill(value_ready, 0U);
        std::ranges::fill(transaction_ready, 0U);
        std::ranges::fill(successor_masks, UINT64_MAX);
        require(executor->execute_internal_output_prefix_prepared(
                    prepared_image, prepared_current, ordered_internal_prefix,
                    batch, &successors),
            "typed V2 entry commits output and generated local successor mask");
        std::uint64_t expected_mask { };
        std::size_t reader_ordinal { };
        for (const auto& member : kernel.members) {
          if (std::ranges::any_of(member.sensitivities,
                  [&selected_output](const auto& sensitivity) {
                    return sensitivity.signal == selected_output->signal
                        && sensitivity.edge == EdgeKind::any
                        && sensitivity.offset == 0U
                        && sensitivity.width == 0U;
                  })) {
            require(reader_ordinal < 64U,
                "fixture successor ordinal fits the sidecar word");
            expected_mask |= UINT64_C(1) << reader_ordinal;
            ++reader_ordinal;
          }
        }
        const auto expected_successor_mask = expected_changed
            ? expected_mask : 0U;
        bool every_sidecar_slot_matches = true;
        for (std::size_t index = 0U; index < successor_masks.size(); ++index) {
          const auto expected_slot_mask = index == selected_index
              ? expected_successor_mask : 0U;
          every_sidecar_slot_matches
              = every_sidecar_slot_matches
                  && successor_masks[index] == expected_slot_mask;
        }
        require(reader_ordinal == 1U
                && successor_masks[selected_index]
                    == expected_successor_mask
                && every_sidecar_slot_matches,
            "V2 sidecar emits the exact local whole-any reader bit and clears unselected slots");
        require(changed[selected_index]
                    == static_cast<std::uint8_t>(expected_changed)
                && value_ready[selected_index]
                    == static_cast<std::uint8_t>(expected_changed)
                && transaction_ready[selected_index] == 1U
                && next_current_aval[selected_index] == expected_word.aval
                && next_current_bval[selected_index] == expected_word.bval
                && next_last_aval[selected_index] == expected_previous.aval
                && next_last_bval[selected_index] == expected_previous.bval
                && next_stored_aval[selected_index] == expected_word.aval
                && next_stored_bval[selected_index] == expected_word.bval
                && next_owner_aval[selected_index] == expected_word.aval
                && next_owner_bval[selected_index] == expected_word.bval,
            "V2 sidecar preserves the exact prepared output transaction planes");
        compare_defined_member_registers(kernel, prepared_image,
            prepared_expected, executor->activation_registers());
      };

      execute_prepared_case(image, current_values, expected,
          "typed private entry applies a changed Logic4 output to unpublished role planes");

      const auto external_input = std::ranges::find_if(kernel.inputs,
          [](const RegionConeKernelInput& candidate) {
            return candidate.signal == 1U && !candidate.internal;
          });
      require(external_input != kernel.inputs.end(),
          "prepared output state matrix identifies its boundary input");
      constexpr std::array logic4_states {
          Logic4::zero, Logic4::one, Logic4::x, Logic4::z };
      for (const auto state_value : logic4_states) {
        auto state_image = image;
        const auto state_input = std::ranges::lower_bound(
            state_image.register_inputs, external_input->value_register,
            std::ranges::less { },
            &RegionKernelRegisterInput::register_id);
        require(state_input != state_image.register_inputs.end()
                && state_input->register_id == external_input->value_register,
            "prepared state matrix locates its boundary input register");
        state_input->value = PackedLogic4 { width, state_value };
        const auto state_expected
            = evaluate_region_activation_kernel_reference(kernel, state_image);
        execute_prepared_case(state_image, current_values, state_expected,
            "generated prepared stores preserve known and four-state Logic4 values");
      }

      auto equal_cut_image = image;
      const auto equal_cut_input = std::ranges::find(
          equal_cut_image.register_inputs, internal_input->value_register,
          &RegionKernelRegisterInput::register_id);
      require(equal_cut_input != equal_cut_image.register_inputs.end(),
          "prefix fixture locates the committed internal input register");
      equal_cut_input->value = expected[link_register];
      const std::array equal_current_values { expected[link_register] };
      const auto equal_expected = evaluate_region_activation_kernel_reference(
          kernel, equal_cut_image);
      if (kind == ValueKind::logic4) {
        auto unchanged_image = image;
        const auto unchanged_input = std::ranges::find(
            unchanged_image.register_inputs, internal_input->value_register,
            &RegionKernelRegisterInput::register_id);
        require(unchanged_input != unchanged_image.register_inputs.end(),
            "unchanged prepared case locates its internal input");
        const PackedLogic4 unknown_input { width, Logic4::x };
        unchanged_input->value = unknown_input;
        const auto unknown_external_input = std::ranges::lower_bound(
            unchanged_image.register_inputs, external_input->value_register,
            std::ranges::less { },
            &RegionKernelRegisterInput::register_id);
        require(unknown_external_input != unchanged_image.register_inputs.end()
                && unknown_external_input->register_id
                    == external_input->value_register,
            "equal prepared case locates its boundary source register");
        unknown_external_input->value = unknown_input;
        std::vector<PackedLogic4> unchanged_current {
            current_values.begin(), current_values.end() };
        unchanged_current[0U] = unknown_input;
        const auto unchanged_expected
            = evaluate_region_activation_kernel_reference(
                kernel, unchanged_image);
        execute_prepared_case(unchanged_image, unchanged_current,
            unchanged_expected,
            "typed private entry records an equal transaction without advancing LAST");
      }
      require(executor->execute_internal_output_prefix(
                  equal_cut_image, equal_current_values,
                  ordered_internal_prefix)
              && executor->internal_output_prefix_changed().size() == 1U
              && executor->internal_output_prefix_changed().front() == 0U,
          "case-equal internal writes remain in the ordered prefix with a clear change bit");
      compare_defined_member_registers(kernel, equal_cut_image,
          equal_expected, executor->activation_registers());

      const std::vector<PackedLogic4> equal_registers {
          executor->activation_registers().begin(),
          executor->activation_registers().end() };
      auto mismatched_current_values = equal_current_values;
      mismatched_current_values.front().set(0U,
          mismatched_current_values.front().get(0U) == Logic4::zero
              ? Logic4::one : Logic4::zero);
      require(!executor->execute_internal_output_prefix(
                  equal_cut_image, mismatched_current_values,
                  ordered_internal_prefix)
              && std::ranges::equal(equal_registers,
                  executor->activation_registers()),
          "baseline planes that disagree with the committed activation image decline before frame mutation");
      const std::array boundary_only_prefix { *boundary_output };
      require(!executor->execute_internal_output_prefix(
                  equal_cut_image, equal_current_values, boundary_only_prefix)
              && std::ranges::equal(equal_registers,
                  executor->activation_registers())
              && executor->internal_output_prefix_changed().size() == 1U,
          "a boundary publication cannot be mislabeled as a private internal prefix");
      const std::array crosses_boundary_prefix {
          *internal_output, *boundary_output };
      require(!executor->execute_internal_output_prefix(
                  equal_cut_image, equal_current_values,
                  crosses_boundary_prefix)
              && std::ranges::equal(equal_registers,
                  executor->activation_registers()),
          "prefix validation declines before frame mutation when it crosses a boundary output");
    }

    if (kind == ValueKind::logic4 && width <= 64U
        && !std::ranges::binary_search(active, std::size_t { 0U })) {
      const auto internal_output = std::ranges::find(kernel.outputs, 0U,
          &RegionConeOutputBinding::signal);
      const std::array inactive_prefix { *internal_output };
      const std::array current_values { committed_link };
      require(!executor->execute_internal_output_prefix(
                  image, current_values, inactive_prefix),
          "an inactive producer cannot publish an internal output prefix");
    }

    if (generation == 1U) {
      const std::vector<PackedLogic4> prior_registers(actual.begin(),
          actual.end());
      const auto require_rejection_without_mutation =
          [&](const RegionKernelInputPlane& malformed,
              const std::string_view message) {
              require(!executor->execute_with_input_planes(image,
                          std::span<const RegionKernelInputPlane> {
                              &malformed, 1U })
                      && std::ranges::equal(prior_registers,
                          executor->activation_registers()),
                  message);
          };

      if (width % 64U != 0U) {
        std::array<std::vector<std::uint64_t>, 4U> bad_tail_words;
        for (std::size_t plane = 0U; plane < planes.size(); ++plane) {
          bad_tail_words[plane].assign(
              planes[plane].begin(), planes[plane].end());
        }
        const auto tail_bit = width % 64U;
        bad_tail_words[0U].back() |= UINT64_C(1) << tail_bit;
        std::array<std::span<const std::uint64_t>, 4U> bad_tail_planes;
        for (std::size_t plane = 0U; plane < bad_tail_planes.size(); ++plane) {
          bad_tail_planes[plane] = bad_tail_words[plane];
        }
        const RegionKernelInputPlane bad_tail {
            internal_input->signal, internal_input->value_register,
            internal_input->width, kind, bad_tail_planes };
        require_rejection_without_mutation(bad_tail,
            "borrowed component planes reject nonzero unused tail bits before frame mutation");
      }

      if (kind == ValueKind::logic9) {
        for (std::uint8_t invalid_code = 9U; invalid_code < 16U;
             ++invalid_code) {
          std::array<std::vector<std::uint64_t>, 4U> invalid_words;
          for (std::size_t plane = 0U; plane < planes.size(); ++plane) {
            invalid_words[plane].assign(
                planes[plane].begin(), planes[plane].end());
            invalid_words[plane][0U] &= ~UINT64_C(1);
            if (((invalid_code >> plane) & 1U) != 0U) {
              invalid_words[plane][0U] |= UINT64_C(1);
            }
          }
          std::array<std::span<const std::uint64_t>, 4U> invalid_planes;
          for (std::size_t plane = 0U; plane < invalid_planes.size(); ++plane) {
            invalid_planes[plane] = invalid_words[plane];
          }
          const RegionKernelInputPlane invalid_logic9 {
              internal_input->signal, internal_input->value_register,
              internal_input->width, kind, invalid_planes };
          require_rejection_without_mutation(invalid_logic9,
              "reserved Logic9 ordinals are rejected before native frame mutation");
        }
      }
    }
    // Nested prefix probes deliberately execute different committed cuts.
    // Return the original activation snapshot for the two-wave oracle.
    return checked_actual;
  };
  constexpr std::array both { std::size_t { 0U }, std::size_t { 1U } };
  const auto first_registers = check_activation(1U, both, initial_link);
  const auto invert = [](const PackedLogic4& source) {
    auto result = source;
    for (std::size_t bit = 0U; bit < source.width(); ++bit) {
      if (source.is_logic9()) {
        result.set_logic9(bit, logic_not(source.get_logic9(bit)));
      } else {
        result.set(bit, logic_not(source.get(bit)));
      }
    }
    return result;
  };
  const auto first_link = first_registers[link_register];
  const auto first_output = first_registers[output_register];
  if (kind == ValueKind::logic4 && width <= 64U) {
    auto known_external = PackedLogic4 { width, Logic4::zero };
    auto known_committed = PackedLogic4 { width, Logic4::zero };
    for (std::size_t bit = 0U; bit < width; ++bit) {
      if (bit % 2U != 0U) {
        known_external.set(bit, Logic4::one);
      }
      if (bit % 3U == 0U) {
        known_committed.set(bit, Logic4::one);
      }
    }
    const auto known_image = make_logic9_builder_image(kernel, 7U, both,
        known_external, known_external, known_committed);
    const auto known_expected = evaluate_region_activation_kernel_reference(
        kernel, known_image);
    require(executor->execute(known_image),
        "ordinary activation accepts a known Logic4 committed cut");
    compare_defined_member_registers(kernel, known_image, known_expected,
        executor->activation_registers());
    const auto synthetic_signal = kernel.inputs.size() + kernel.members.size();
    const auto direct_aval
        = fsim::compiler::llvm_detail::RegionKernelTestAccess::
            direct_signal_aval(*executor);
    const auto direct_bval
        = fsim::compiler::llvm_detail::RegionKernelTestAccess::
            direct_signal_bval(*executor);
    const auto committed_word = known_committed.low_word();
    require(synthetic_signal < direct_aval.size()
            && synthetic_signal < direct_bval.size()
            && direct_aval[synthetic_signal] == committed_word.aval
            && direct_bval[synthetic_signal] == committed_word.bval
            && committed_word.bval == 0U,
        "ordinary activation initializes its hidden internal baseline from the known committed image");
  }
  require(first_output == invert(initial_link),
      "same-activation consumer reads the old wide link, without forwarding");
  constexpr std::array consumer_only { std::size_t { 1U } };
  const auto second_registers
      = check_activation(2U, consumer_only, first_link);
  require(first_output == invert(initial_link),
      "a retained first-wave output survives later native activation");

  Interpreter reference;
  static_cast<void>(reference.add_signal(
      Signal { "link", initial_link, ResolutionKind::none, kind }));
  static_cast<void>(reference.add_signal(
      Signal { "input", input, ResolutionKind::none, kind }));
  static_cast<void>(reference.add_signal(
      Signal { "output", value(1U), ResolutionKind::none, kind }));
  for (const auto& process : processes) {
    static_cast<void>(reference.add_process(process));
  }
    require(reference.run().status == RunStatus::completed
            && reference.signal_value(0U) == first_link
            && reference.signal_value(2U)
                == second_registers[output_register],
      "two exact native activation waves settle like ordinary interpretation");
  if (width == 1U) {
    const std::string_view scalar_states = kind == ValueKind::logic9
        ? "UX01ZWLH-" : "01XZ";
    for (std::size_t rotation = 0U;
         rotation < scalar_states.size(); ++rotation) {
      check_activation(static_cast<std::uint64_t>(10U + rotation), both,
          value(rotation));
    }
  }
}

void check_prepared_output_successor_mask_capacity()
{
  const auto run = [](const std::size_t reader_count,
                      const bool expect_successor_mask) {
    auto [kernel, image]
        = make_many_reader_activation_kernel(reader_count);
    fsim::compiler::LlvmJitOptions options;
    options.optimization = fsim::compiler::JitOptimizationLevel::o0;
    options.cache_directory.clear();
    auto executor = fsim::compiler::LlvmRegionKernelExecutor::try_create(
        kernel, options,
        "region-prepared-successor-mask-capacity-"
            + std::to_string(reader_count));
    require(executor != nullptr,
        "capacity fixture creates a checked compiler kernel");

    const auto internal_output = std::ranges::find(kernel.outputs, 1U,
        &RegionConeOutputBinding::signal);
    require(internal_output != kernel.outputs.end()
            && kernel.internal_signals.size() == 1U,
        "capacity fixture has one internal owner output");
    const std::array ordered_prefix { *internal_output };
    const std::array current_values {
        PackedLogic4 { 1U, Logic4::zero },
    };

    std::vector<RegionPreparedOutputSlotV1> slots(1U);
    std::vector<std::uint64_t> owner_masks(1U, UINT64_C(1));
    std::vector<std::uint64_t> old_current_aval(1U, 0U);
    std::vector<std::uint64_t> old_current_bval(1U, 0U);
    std::vector<std::uint64_t> old_owner_aval(1U, 0U);
    std::vector<std::uint64_t> old_owner_bval(1U, 0U);
    std::vector<std::uint64_t> next_current_aval(1U, 0U);
    std::vector<std::uint64_t> next_current_bval(1U, 0U);
    std::vector<std::uint64_t> next_last_aval(1U, 0U);
    std::vector<std::uint64_t> next_last_bval(1U, 0U);
    std::vector<std::uint64_t> next_stored_aval(1U, 0U);
    std::vector<std::uint64_t> next_stored_bval(1U, 0U);
    std::vector<std::uint64_t> next_owner_aval(1U, 0U);
    std::vector<std::uint64_t> next_owner_bval(1U, 0U);
    std::vector<std::uint8_t> changed(1U, 0U);
    std::vector<std::uint8_t> value_ready(1U, 0U);
    std::vector<std::uint8_t> transaction_ready(1U, 0U);
    auto& slot = slots.front();
    slot.struct_size = sizeof(RegionPreparedOutputSlotV1);
    slot.signal_id = internal_output->signal;
    slot.owner_id = internal_output->owner;
    slot.width = internal_output->width;
    slot.word_count = 1U;
    slot.value_kind = RegionPreparedOutputValueKindV1::logic4;
    slot.selected = 1U;
    slot.owner_mask = owner_masks.data();
    slot.old_current_aval = old_current_aval.data();
    slot.old_current_bval = old_current_bval.data();
    slot.old_owner_aval = old_owner_aval.data();
    slot.old_owner_bval = old_owner_bval.data();
    slot.next_current_aval = next_current_aval.data();
    slot.next_current_bval = next_current_bval.data();
    slot.next_last_aval = next_last_aval.data();
    slot.next_last_bval = next_last_bval.data();
    slot.next_stored_aval = next_stored_aval.data();
    slot.next_stored_bval = next_stored_bval.data();
    slot.next_owner_aval = next_owner_aval.data();
    slot.next_owner_bval = next_owner_bval.data();
    slot.changed = changed.data();
    slot.value_ready = value_ready.data();
    slot.transaction_ready = transaction_ready.data();
    RegionPreparedOutputBatchV1 batch {
        kRegionPreparedOutputBatchAbiVersionV1,
        sizeof(RegionPreparedOutputBatchV1), 1U, 0U, slots.data() };
    constexpr std::uint64_t untouched_mask = UINT64_C(0xA55AA55AA55AA55A);
    std::vector<std::uint64_t> masks(1U, untouched_mask);
    RegionPreparedOutputSuccessorMasksV1 successors {
        kRegionPreparedOutputSuccessorMasksAbiVersionV1,
        sizeof(RegionPreparedOutputSuccessorMasksV1), 1U, 0U,
        masks.data() };
    const std::vector<PackedLogic4> prior_registers {
        executor->activation_registers().begin(),
        executor->activation_registers().end() };

    const bool completed
        = executor->execute_internal_output_prefix_prepared(
            image, current_values, ordered_prefix, batch, &successors);
    if (expect_successor_mask) {
      require(completed && masks.front() == UINT64_MAX
              && changed.front() == 1U
              && value_ready.front() == 1U
              && transaction_ready.front() == 1U,
          "64 whole-any readers use bits 0 through 63, including bit 63");
      return;
    }

    const auto output_planes_unchanged = [&] {
      return owner_masks.front() == UINT64_C(1)
          && old_current_aval.front() == 0U
          && old_current_bval.front() == 0U
          && old_owner_aval.front() == 0U
          && old_owner_bval.front() == 0U
          && next_current_aval.front() == 0U
          && next_current_bval.front() == 0U
          && next_last_aval.front() == 0U
          && next_last_bval.front() == 0U
          && next_stored_aval.front() == 0U
          && next_stored_bval.front() == 0U
          && next_owner_aval.front() == 0U
          && next_owner_bval.front() == 0U;
    };
    require(!completed
            && masks.front() == untouched_mask
            && changed.front() == 0U
            && value_ready.front() == 0U
            && transaction_ready.front() == 0U
            && output_planes_unchanged()
            && std::ranges::equal(prior_registers,
                executor->activation_registers()),
        "65 readers decline the V2 sidecar before frame or output mutation");
    require(executor->execute_internal_output_prefix_prepared(
                image, current_values, ordered_prefix, batch)
            && changed.front() == 1U,
        "65-reader capacity decline retains the ordinary V1 prepared-output path");
    require(masks.front() == untouched_mask,
        "V1 fallback leaves the unavailable successor sidecar untouched");
  };

  run(64U, true);
  run(65U, false);
}

void check_unsupported_region_programs_still_decline()
{
  auto projected_contract_mismatch = make_vhdl_projected_activation_kernel();
  projected_contract_mismatch.outputs.front().projected_rejection = 1U;
  require(fsim::compiler::LlvmRegionKernelExecutor::try_create(
              projected_contract_mismatch) == nullptr,
      "the native compiler rejects unsupported projected output provenance");

  auto unsupported_control = make_logic4_kernel();
  unsupported_control.program.operations.replace(2U,
      Branch { 0U, 3U, 4U, UnknownBranchPolicy::when_false });
  require(fsim::compiler::LlvmRegionKernelExecutor::try_create(
              unsupported_control) == nullptr,
      "nested control flow declines until the kernel instruction map handles it");

  auto unsupported_arithmetic = make_logic4_kernel();
  unsupported_arithmetic.program.operations.replace(2U,
      Binary { BinaryOperator::subtract_unsigned, 4U, 0U, 1U });
  require(fsim::compiler::LlvmRegionKernelExecutor::try_create(
              unsupported_arithmetic) == nullptr,
      "unsupported subtract arithmetic declines before JIT compilation");

  auto unequal_width_add = make_logic4_kernel();
  unequal_width_add.program.operations.replace(2U,
      Binary { BinaryOperator::add_unsigned, 4U, 0U, 1U });
  unequal_width_add.inputs[1U].width = 3U;
  require(fsim::compiler::LlvmRegionKernelExecutor::try_create(
              unequal_width_add) == nullptr,
      "direct kernel admission rejects add_unsigned with unequal operand widths");

  auto logic9_add = make_logic9_builder_kernel(65U);
  bool replaced_logic9_unary = false;
  for (std::size_t index = 0U;
       index < logic9_add.program.operations.size(); ++index) {
    const auto operation = logic9_add.program.operations.expanded(index);
    const auto* const unary = operation_get_if<UnaryNot>(&operation);
    if (unary == nullptr
        || unary->source >= logic9_add.program.register_value_kinds.size()
        || unary->destination
            >= logic9_add.program.register_value_kinds.size()
        || logic9_add.program.register_value_kinds[unary->source]
            != ValueKind::logic9
        || logic9_add.program.register_value_kinds[unary->destination]
            != ValueKind::logic9) {
      continue;
    }
    logic9_add.program.operations.replace(index,
        Binary { BinaryOperator::add_unsigned, unary->destination,
            unary->source, unary->source });
    replaced_logic9_unary = true;
    break;
  }
  require(replaced_logic9_unary,
      "Logic9 direct-kernel fixture has a value-preserving unary site to replace");
  require(fsim::compiler::LlvmRegionKernelExecutor::try_create(logic9_add)
              == nullptr,
      "direct kernel admission keeps add_unsigned restricted to Logic4");

  auto unsupported_reduction = make_logic4_kernel();
  unsupported_reduction.program.operations.replace(2U,
      Reduction { ReductionOperator::one_hot, 4U, 0U });
  require(fsim::compiler::LlvmRegionKernelExecutor::try_create(
              unsupported_reduction) == nullptr,
      "unsupported reductions decline before JIT compilation");

  auto mismatched_input_width = make_logic4_kernel();
  mismatched_input_width.inputs[0U].width = 65U;
  require(fsim::compiler::LlvmRegionKernelExecutor::try_create(
              mismatched_input_width) == nullptr,
      "inconsistent input and register widths decline before JIT compilation");
}


std::uint32_t dag_random(std::uint32_t& state) noexcept
{
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

void check_random_dag(const std::uint32_t width, const ValueKind kind,
    const std::uint32_t seed)
{
    auto random = seed;
    const auto count = static_cast<ProcessId>(4U + dag_random(random) % 5U);
    std::vector<Process> processes(count);
    std::vector<RegionSignalDescriptor> descriptors(count + 2U,
        RegionSignalDescriptor { width, ResolutionKind::none, kind });
    descriptors.back().observations = RegionObservation::current;
    for (ProcessId id = 0U; id < count; ++id) {
        auto& process = processes[id];
        const auto output = id + 2U;
        const auto first = id == 0U ? 0U : output - 1U;
        const auto second = dag_random(random) % output;
        process.id = id;
        process.name = "random_dag_member_" + std::to_string(id);
        process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        process.register_count = 3U;
        process.register_value_kinds.assign(3U, kind);
        process.static_sensitivity = { { first, EdgeKind::any } };
        if (second != first) {
            process.static_sensitivity.push_back({ second, EdgeKind::any });
        }
        process.driver_regions = { { output, 0U, 0U, true } };
        process.operations = { ReadSignal { 0U, first }, ReadSignal { 1U, second } };
        constexpr std::array operations {
            BinaryOperator::bit_and, BinaryOperator::bit_or, BinaryOperator::bit_xor
        };
        const auto operation = dag_random(random) % 4U;
        if (operation == 3U) {
            process.operations.push_back(UnaryNot { 2U, 0U });
        } else {
            process.operations.push_back(Binary { operations[operation], 2U, 0U, 1U });
        }
        process.operations.push_back(WriteUpdate {
            output, 2U, SignalUpdateDomain::systemverilog_active });
        process.operations.push_back(WaitSensitivity { });
        process.operations.push_back(Jump { 0U });
    }
    std::vector<const Process*> bindings;
    for (const auto& process : processes) {
        bindings.push_back(&process);
    }
    const auto graph = RegionGraph::build(bindings, descriptors);
    require(graph.certificate_inventory().components.size() == 1U,
        "random acyclic dependency chains and reconvergence form one region");
    const auto program = graph.build_compute_program(0U, bindings);
    require(program.has_value(), "random pure DAG has an exact activation kernel");
    const auto& kernel = program->activation_kernel;
    require(kernel.members.size() == count,
        "random graph construction retains every original process");
    for (const auto optimization : { fsim::compiler::JitOptimizationLevel::o0,
             fsim::compiler::JitOptimizationLevel::o2 }) {
        fsim::compiler::LlvmJitOptions options;
        options.optimization = optimization;
        options.cache_directory.clear();
        auto executor = fsim::compiler::LlvmRegionKernelExecutor::try_create(
            kernel, options, "random-dag-committed-cut-v1");
        require(executor != nullptr, "random DAG binds to both LLVM modes");
        std::uint64_t generation = 0U;
        for (const bool known : { true, false }) {
            Interpreter interpreter;
            std::vector<PackedLogic4> committed;
            auto value_random = seed;
            for (SignalId signal = 0U; signal < count + 2U; ++signal) {
                const auto rotation = dag_random(value_random);
                const std::string_view digits = known ? "01"
                    : kind == ValueKind::logic9 ? "UX01ZWLH-" : "01XZ";
                std::string text;
                text.reserve(width);
                for (std::uint32_t bit = 0U; bit < width; ++bit) {
                    text.push_back(digits[(static_cast<std::uint64_t>(bit)
                        + rotation) % digits.size()]);
                }
                auto value = kind == ValueKind::logic9
                    ? PackedLogic4::from_logic9_msb_string(text)
                    : PackedLogic4::from_msb_string(text);
                require(interpreter.add_signal({ "random_signal_" + std::to_string(signal),
                            value, ResolutionKind::none, kind }) == signal,
                    "random reference preserves signal identity");
                committed.push_back(std::move(value));
            }
            for (const auto& process : processes) {
                static_cast<void>(interpreter.add_process(process));
            }
            require(interpreter.run().status == RunStatus::completed,
                "ordinary interpreter settles the random acyclic graph");
            std::vector<std::size_t> active;
            for (std::size_t member = 0U; member < kernel.members.size(); ++member) {
                active.push_back(member);
            }
            std::size_t rounds = 0U;
            while (!active.empty()) {
                require(++rounds <= count + 2U,
                    "random DAG converges within its acyclic depth bound");
                RegionKernelActivationImage image;
                image.generation = ++generation;
                image.active_member_indices = active;
                auto& prefix = image.scheduler_prefix;
                prefix.frontier_generation = generation;
                prefix.frontier_end = active.size();
                prefix.phase = SchedulerPhase::active;
                prefix.systemverilog_round = 2U * rounds;
                for (std::size_t ordinal = 0U; ordinal < active.size(); ++ordinal) {
                    const auto process = kernel.members[active[ordinal]].process;
                    const RegionKernelReadyMember request {
                        process, Process::full_static_trigger_mask,
                        RegionKernelActivationOrigin {
                            ProcessSchedulingDomain::systemverilog,
                            SchedulerPhase::active, 0U, 0U,
                            static_cast<StableOrder>(process),
                            static_cast<std::uint64_t>(ordinal),
                            prefix.systemverilog_round } };
                    image.ready_processes.push_back(process);
                    image.requests.push_back(request);
                    prefix.tasks.push_back({ ordinal, request });
                }
                for (const auto& input : kernel.inputs) {
                    image.register_inputs.push_back({ input.value_register,
                        committed[input.signal] });
                }
                for (std::size_t member = 0U; member < kernel.members.size(); ++member) {
                    image.register_inputs.push_back({ kernel.members[member].readiness_register,
                        PackedLogic4 { 1U, std::ranges::binary_search(active, member)
                                ? Logic4::one : Logic4::zero } });
                }
                std::ranges::sort(image.register_inputs, std::ranges::less { },
                    &RegionKernelRegisterInput::register_id);
                const auto expected = evaluate_region_activation_kernel_reference(kernel, image);
                require(executor->execute(image),
                    "random committed-cut activation executes natively");
                const auto actual = executor->activation_registers();
                compare_defined_member_registers(kernel, image, expected, actual);
                std::vector<std::uint8_t> changed(committed.size());
                for (const auto& output : kernel.outputs) {
                    if (std::ranges::find(image.ready_processes, output.owner)
                        == image.ready_processes.end()) {
                        continue;
                    }
                    const auto& value = actual[output.value_register];
                    require(value == expected[output.value_register],
                        "every active random output matches the checked reference");
                    changed[output.signal] = static_cast<std::uint8_t>(
                        committed[output.signal] != value);
                    committed[output.signal] = value;
                }
                active.clear();
                for (std::size_t member = 0U; member < kernel.members.size(); ++member) {
                    const auto process = kernel.members[member].process;
                    if (std::ranges::any_of(processes[process].static_sensitivity,
                            [&](const Sensitivity& sensitivity) {
                                return changed[sensitivity.signal] != 0U;
                            })) {
                        active.push_back(member);
                    }
                }
            }
            for (SignalId signal = 0U; signal < committed.size(); ++signal) {
                require(committed[signal] == interpreter.signal_value(signal),
                    "native random graph settles to the ordinary interpreter value on every net");
            }
        }
    }
}

void check_random_dag_matrix()
{
    auto seed = UINT32_C(0x6d2b79f5);
    for (const auto width : { 1U, 65U, 129U, 256U, 1024U }) {
        for (const auto kind : { ValueKind::logic4, ValueKind::logic9 }) {
            const auto graph_seed = dag_random(seed);
            try {
                check_random_dag(width, kind, graph_seed);
            } catch (const std::exception& error) {
                throw std::runtime_error {
                    "random DAG seed=" + std::to_string(graph_seed)
                    + " width=" + std::to_string(width)
                    + " kind=" + std::to_string(static_cast<unsigned>(kind))
                    + ": " + error.what() };
            }
        }
    }
}

[[nodiscard]] RegionConeProgram make_nontrivial_region_program()
{
  const std::vector<RegionSignalDescriptor> signals {
      { 1U }, { 1U },
      { 1U, ResolutionKind::none, ValueKind::logic4, false, false,
          false, RegionObservation::current },
  };

  Process consumer;
  consumer.id = 0U;
  consumer.name = "forwarding_child_before_parent";
  consumer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
  consumer.register_count = 2U;
  consumer.register_value_kinds = {
      ValueKind::logic4, ValueKind::logic4,
  };
  consumer.static_sensitivity = {
      Sensitivity { 1U, EdgeKind::any, 0U, 1U },
  };
  consumer.driver_regions = { { 2U, 0U, 1U, true } };
  consumer.operations = {
      ReadSignal { 0U, 1U },
      UnaryNot { 1U, 0U },
      DebugPoint { DebugPointKind::statement,
          SourceLocation { "forwarding-chain.sv", 4U, 3U },
          "child.scope" },
      WriteUpdate { 2U, 1U,
          SignalUpdateDomain::systemverilog_active },
      WaitSensitivity { },
      Jump { 0U },
  };

  Process producer;
  producer.id = 1U;
  producer.name = "forwarding_root_after_child";
  producer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
  producer.register_count = 1U;
  producer.register_value_kinds = { ValueKind::logic4 };
  producer.static_sensitivity = {
      Sensitivity { 0U, EdgeKind::any, 0U, 1U },
  };
  producer.driver_regions = { { 1U, 0U, 1U, true } };
  producer.operations = {
      ReadSignal { 0U, 0U },
      DebugPoint { DebugPointKind::statement,
          SourceLocation { "forwarding-chain.sv", 9U, 3U },
          "root.scope" },
      WriteUpdate { 1U, 0U,
          SignalUpdateDomain::systemverilog_active },
      WaitSensitivity { },
      Jump { 0U },
  };

  const std::array<const Process*, 2U> process_bindings {
      &consumer, &producer,
  };
  const auto graph = RegionGraph::build(process_bindings, signals);
  const auto& components = graph.certificate_inventory().components;
  const auto component = std::ranges::find_if(components,
      [](const RegionComponentCertificate& candidate) {
        return candidate.members == std::vector<ProcessId> { 0U, 1U }
            && candidate.structural_internal_signal_candidates
                == std::vector<SignalId> { 1U };
      });
  require(component != components.end(),
      "reversed-ID chain has one certified internal signal");
  const auto component_index = static_cast<std::size_t>(
      component - components.begin());
  auto compute = graph.build_compute_program(
      component_index, process_bindings);
  require(compute.has_value() && compute->forwarding_kernel.has_value(),
      "the source-order child/parent graph builds a forwarding kernel");

  auto forwarding = *compute->forwarding_kernel;
  require(forwarding.execution_kernel.member_execution_order
          == std::vector<std::size_t> { 1U, 0U },
      "native body order follows the producer before the lower ProcessId child");
  require(forwarding.dependencies.size() == 1U
          && forwarding.dependencies.front().signal == 1U
          && forwarding.dependencies.front().edge == EdgeKind::any
          && forwarding.dependencies.front().offset == 0U
          && forwarding.dependencies.front().width == 1U,
      "the dependency certificate preserves the normalized sensitivity range");
  require(std::ranges::any_of(consumer.operations,
              [](const Operation& operation) {
                return operation_holds<DebugPoint>(operation);
              })
          && std::ranges::any_of(producer.operations,
              [](const Operation& operation) {
                return operation_holds<DebugPoint>(operation);
              }),
      "normal member source DebugPoints remain in the admitted body");
  return std::move(*compute);
}

[[nodiscard]] RegionConeForwardingKernel
make_nontrivial_forwarding_kernel()
{
  auto program = make_nontrivial_region_program();
  require(program.forwarding_kernel.has_value(),
      "the shared nontrivial graph program retains its forwarding form");
  return std::move(*program.forwarding_kernel);
}

[[nodiscard]] RegionKernelActivationImage make_all_ready_forwarding_image(
    const RegionConeActivationKernel& kernel)
{
  RegionKernelActivationImage image;
  image.generation = 1U;
  image.active_member_indices.resize(kernel.members.size());
  image.scheduler_prefix.frontier_generation = 1U;
  image.scheduler_prefix.frontier_cursor = 0U;
  image.scheduler_prefix.frontier_end = kernel.members.size();
  image.scheduler_prefix.phase = SchedulerPhase::active;
  image.scheduler_prefix.process_domain
      = ProcessSchedulingDomain::systemverilog;
  image.scheduler_prefix.systemverilog_round = 1U;

  for (std::size_t index = 0U; index < kernel.members.size(); ++index) {
    const auto& member = kernel.members[index];
    image.active_member_indices[index] = index;
    const RegionKernelReadyMember request {
        member.process,
        Process::full_static_trigger_mask,
        RegionKernelActivationOrigin {
            ProcessSchedulingDomain::systemverilog,
            SchedulerPhase::active,
            0U,
            0U,
            static_cast<StableOrder>(member.process + 1U),
            static_cast<std::uint64_t>(member.process + 1U),
            1U,
        },
    };
    image.ready_processes.push_back(member.process);
    image.requests.push_back(request);
    image.scheduler_prefix.tasks.push_back({ index, request });
  }
  for (const auto& input : kernel.inputs) {
    image.register_inputs.push_back({ input.value_register,
        PackedLogic4 { input.width, Logic4::zero } });
  }
  for (const auto& member : kernel.members) {
    image.register_inputs.push_back({ member.readiness_register,
        PackedLogic4 { 1U, Logic4::one } });
  }
  std::ranges::sort(image.register_inputs, std::ranges::less { },
      &RegionKernelRegisterInput::register_id);
  return image;
}

struct DirectReadySharingFixture final {
  RegionKernelActivationImage checked_image;
  RegionKernelActivationImage direct_image;
  std::vector<PackedLogic4> current_internal_values;
  std::vector<RegionConeOutputBinding> ordered_internal_prefix;
  std::vector<std::uint64_t> readiness_mask;
  std::vector<std::uint64_t> input_aval;
  std::vector<std::uint64_t> input_bval;
  std::vector<RegionDirectReadyInputSlotV1> input_slots;
  std::vector<RegionPreparedOutputSlotV1> output_slots;
  std::vector<std::uint64_t> owner_masks;
  std::vector<std::uint64_t> old_current_aval;
  std::vector<std::uint64_t> old_current_bval;
  std::vector<std::uint64_t> old_owner_aval;
  std::vector<std::uint64_t> old_owner_bval;
  std::vector<std::uint64_t> next_current_aval;
  std::vector<std::uint64_t> next_current_bval;
  std::vector<std::uint64_t> next_last_aval;
  std::vector<std::uint64_t> next_last_bval;
  std::vector<std::uint64_t> next_stored_aval;
  std::vector<std::uint64_t> next_stored_bval;
  std::vector<std::uint64_t> next_owner_aval;
  std::vector<std::uint64_t> next_owner_bval;
  std::vector<std::uint8_t> changed;
  std::vector<std::uint8_t> value_ready;
  std::vector<std::uint8_t> transaction_ready;
  RegionDirectReadyWindowV1 input_window;
  RegionPreparedOutputBatchV1 output_batch;
  std::vector<std::uint64_t> successor_masks;
  RegionPreparedOutputSuccessorMasksV1 successor_abi;

  [[nodiscard]] std::vector<std::uint64_t> replacement_snapshot() const
  {
    std::vector<std::uint64_t> result;
    const auto append = [&result](const auto& values) {
      result.insert(result.end(), values.begin(), values.end());
    };
    append(next_current_aval);
    append(next_current_bval);
    append(next_last_aval);
    append(next_last_bval);
    append(next_stored_aval);
    append(next_stored_bval);
    append(next_owner_aval);
    append(next_owner_bval);
    for (const auto value : changed) {
      result.push_back(value);
    }
    for (const auto value : value_ready) {
      result.push_back(value);
    }
    for (const auto value : transaction_ready) {
      result.push_back(value);
    }
    result.insert(result.end(), successor_masks.begin(), successor_masks.end());
    return result;
  }
};

[[nodiscard]] DirectReadySharingFixture make_direct_ready_sharing_fixture(
    const RegionConeActivationKernel& kernel,
    const Logic4 boundary_value, const Logic4 internal_value)
{
  DirectReadySharingFixture fixture;
  fixture.checked_image = make_all_ready_forwarding_image(kernel);
  require(kernel.members.size() >= 2U
          && kernel.internal_signals.size() == 1U,
      "the direct-ready witness uses one internal signal and multiple members");

  const auto internal_signal = kernel.internal_signals.front();
  const auto internal_output = std::ranges::find(kernel.outputs,
      internal_signal, &RegionConeOutputBinding::signal);
  require(internal_output != kernel.outputs.end()
          && internal_output->value_kind == ValueKind::logic4
          && internal_output->width == 1U,
      "the shared-body direct window has one narrow internal output");
  const auto root_process = internal_output->owner;

  auto& checked_image = fixture.checked_image;
  for (const auto& input : kernel.inputs) {
    const auto image_input = std::ranges::find(checked_image.register_inputs,
        input.value_register, &RegionKernelRegisterInput::register_id);
    require(image_input != checked_image.register_inputs.end(),
        "the direct-ready fixture retains every checked input value");
    if (input.internal) {
      require(input.signal == internal_signal,
          "the chain has no additional internal input signal");
      image_input->value.set(0U, internal_value);
    } else {
      image_input->value.set(0U, boundary_value);
    }
  }

  std::ranges::stable_sort(checked_image.requests,
      [root_process](const RegionKernelReadyMember& left,
                     const RegionKernelReadyMember& right) {
        return left.process == root_process && right.process != root_process;
      });
  checked_image.ready_processes.clear();
  checked_image.scheduler_prefix.tasks.clear();
  for (std::size_t index = 0U; index < checked_image.requests.size(); ++index) {
    auto& request = checked_image.requests[index];
    request.origin.stable_order = static_cast<StableOrder>(index + 1U);
    request.origin.sequence = static_cast<std::uint64_t>(index + 1U);
    checked_image.ready_processes.push_back(request.process);
    checked_image.scheduler_prefix.tasks.push_back({ index, request });
  }

  for (const auto& request : checked_image.requests) {
    for (const auto& output : kernel.outputs) {
      if (output.owner != request.process) {
        continue;
      }
      const auto internal = std::ranges::binary_search(
          kernel.internal_signals, output.signal);
      if (!internal) {
        break;
      }
      fixture.ordered_internal_prefix.push_back(output);
    }
  }
  require(!fixture.ordered_internal_prefix.empty()
          && fixture.ordered_internal_prefix.front() == *internal_output,
      "the direct prepared prefix begins at the producer's internal output");

  fixture.current_internal_values.emplace_back(1U, internal_value);
  fixture.direct_image = checked_image;
  fixture.direct_image.register_inputs.clear();

  fixture.readiness_mask.resize(
      (kernel.members.size() + 63U) / 64U, 0U);
  for (const auto member : checked_image.active_member_indices) {
    fixture.readiness_mask[member / 64U]
        |= UINT64_C(1) << (member % 64U);
  }

  std::vector<const RegionConeKernelInput*> ordered_inputs;
  ordered_inputs.reserve(kernel.inputs.size());
  for (const auto& input : kernel.inputs) {
    ordered_inputs.push_back(&input);
  }
  std::ranges::sort(ordered_inputs, std::ranges::less { },
      [](const RegionConeKernelInput* input) {
        return input->value_register;
      });
  const auto input_slot_count
      = ordered_inputs.size() + kernel.internal_signals.size();
  fixture.input_aval.reserve(input_slot_count);
  fixture.input_bval.reserve(input_slot_count);
  fixture.input_slots.reserve(input_slot_count);
  const auto append_input_slot = [&fixture](
      const SignalId signal, const RegisterId reg,
      const std::uint32_t width, const PackedLogic4& value) {
    require(width != 0U && width <= 64U && value.width() == width
            && !value.is_logic9(),
        "direct-ready slots have one-word Logic4 values");
    const auto word = value.low_word();
    fixture.input_aval.push_back(word.aval);
    fixture.input_bval.push_back(word.bval);
    const auto index = fixture.input_aval.size() - 1U;
    fixture.input_slots.push_back({ sizeof(RegionDirectReadyInputSlotV1),
        signal, reg, width, 1U, 0U,
        &fixture.input_aval[index], &fixture.input_bval[index] });
  };
  for (const auto* const input : ordered_inputs) {
    const PackedLogic4* value { };
    if (input->internal) {
      value = &fixture.current_internal_values.front();
    } else {
      const auto image_input = std::ranges::find(
          checked_image.register_inputs, input->value_register,
          &RegionKernelRegisterInput::register_id);
      require(image_input != checked_image.register_inputs.end(),
          "the direct slot maps to its checked captured boundary input");
      value = &image_input->value;
    }
    append_input_slot(input->signal, input->value_register,
        input->width, *value);
  }
  for (std::size_t index = 0U;
       index < kernel.internal_signals.size(); ++index) {
    const auto register_id = static_cast<std::uint64_t>(
        kernel.program.register_count) + index;
    require(register_id <= std::numeric_limits<RegisterId>::max(),
        "the internal prefix register fits the direct ABI");
    append_input_slot(kernel.internal_signals[index],
        static_cast<RegisterId>(register_id), internal_output->width,
        fixture.current_internal_values[index]);
  }

  fixture.input_window.abi_version
      = kRegionDirectReadyWindowAbiVersionV1;
  fixture.input_window.struct_size = sizeof(RegionDirectReadyWindowV1);
  fixture.input_window.activation_generation = fixture.direct_image.generation;
  fixture.input_window.frontier_generation
      = fixture.direct_image.scheduler_prefix.frontier_generation;
  fixture.input_window.member_count
      = static_cast<std::uint32_t>(kernel.members.size());
  fixture.input_window.readiness_word_count
      = static_cast<std::uint32_t>(fixture.readiness_mask.size());
  fixture.input_window.input_slot_count
      = static_cast<std::uint32_t>(fixture.input_slots.size());
  fixture.input_window.readiness_mask = fixture.readiness_mask.data();
  fixture.input_window.input_slots = fixture.input_slots.data();

  const auto output_count = kernel.internal_signals.size();
  fixture.output_slots.resize(output_count);
  fixture.owner_masks.resize(output_count);
  fixture.old_current_aval.resize(output_count);
  fixture.old_current_bval.resize(output_count);
  fixture.old_owner_aval.resize(output_count);
  fixture.old_owner_bval.resize(output_count);
  fixture.next_current_aval.resize(output_count);
  fixture.next_current_bval.resize(output_count);
  fixture.next_last_aval.resize(output_count);
  fixture.next_last_bval.resize(output_count);
  fixture.next_stored_aval.resize(output_count);
  fixture.next_stored_bval.resize(output_count);
  fixture.next_owner_aval.resize(output_count);
  fixture.next_owner_bval.resize(output_count);
  fixture.changed.resize(output_count, 0U);
  fixture.value_ready.resize(output_count, 0U);
  fixture.transaction_ready.resize(output_count, 0U);
  fixture.successor_masks.resize(output_count, UINT64_MAX);
  for (std::size_t index = 0U; index < output_count; ++index) {
    const auto signal = kernel.internal_signals[index];
    const auto output = std::ranges::find(kernel.outputs, signal,
        &RegionConeOutputBinding::signal);
    require(output != kernel.outputs.end(),
        "every private internal signal has one output binding");
    const auto current = fixture.current_internal_values[index].low_word();
    fixture.owner_masks[index] = output->width == 64U ? UINT64_MAX
        : (UINT64_C(1) << output->width) - UINT64_C(1);
    fixture.old_current_aval[index] = current.aval;
    fixture.old_current_bval[index] = current.bval;
    fixture.old_owner_aval[index] = current.aval;
    fixture.old_owner_bval[index] = current.bval;

    auto& slot = fixture.output_slots[index];
    slot.struct_size = sizeof(RegionPreparedOutputSlotV1);
    slot.signal_id = signal;
    slot.owner_id = output->owner;
    slot.width = output->width;
    slot.word_count = 1U;
    slot.value_kind = RegionPreparedOutputValueKindV1::logic4;
    slot.selected = std::ranges::find(fixture.ordered_internal_prefix,
        signal, &RegionConeOutputBinding::signal)
            != fixture.ordered_internal_prefix.end() ? 1U : 0U;
    slot.owner_mask = &fixture.owner_masks[index];
    slot.old_current_aval = &fixture.old_current_aval[index];
    slot.old_current_bval = &fixture.old_current_bval[index];
    slot.old_owner_aval = &fixture.old_owner_aval[index];
    slot.old_owner_bval = &fixture.old_owner_bval[index];
    slot.next_current_aval = &fixture.next_current_aval[index];
    slot.next_current_bval = &fixture.next_current_bval[index];
    slot.next_last_aval = &fixture.next_last_aval[index];
    slot.next_last_bval = &fixture.next_last_bval[index];
    slot.next_stored_aval = &fixture.next_stored_aval[index];
    slot.next_stored_bval = &fixture.next_stored_bval[index];
    slot.next_owner_aval = &fixture.next_owner_aval[index];
    slot.next_owner_bval = &fixture.next_owner_bval[index];
    slot.changed = &fixture.changed[index];
    slot.value_ready = &fixture.value_ready[index];
    slot.transaction_ready = &fixture.transaction_ready[index];
  }
  fixture.output_batch.abi_version
      = kRegionPreparedOutputBatchAbiVersionV1;
  fixture.output_batch.struct_size = sizeof(RegionPreparedOutputBatchV1);
  fixture.output_batch.slot_count
      = static_cast<std::uint32_t>(fixture.output_slots.size());
  fixture.output_batch.slots = fixture.output_slots.data();
  fixture.successor_abi = {
      kRegionPreparedOutputSuccessorMasksAbiVersionV1,
      sizeof(RegionPreparedOutputSuccessorMasksV1),
      static_cast<std::uint32_t>(fixture.successor_masks.size()), 0U,
      fixture.successor_masks.data() };
  return fixture;
}

void check_direct_ready_successor_mask_capacity()
{
  const auto run = [](const std::size_t reader_count,
                      const bool expect_combined_entry) {
    auto [kernel, ignored_image]
        = make_many_reader_activation_kernel(reader_count);
    static_cast<void>(ignored_image);
    auto fixture = make_direct_ready_sharing_fixture(
        kernel, Logic4::zero, Logic4::zero);

    fsim::compiler::LlvmJitOptions options;
    options.optimization = fsim::compiler::JitOptimizationLevel::o0;
    options.cache_directory.clear();
    auto executor = fsim::compiler::LlvmRegionKernelExecutor::try_create(
        kernel, options,
        "region-direct-ready-successor-mask-capacity-"
            + std::to_string(reader_count));
    require(executor != nullptr
            && executor->supports_direct_ready_window(),
        "reader-capacity fixture retains its V1 direct-ready entry");
    require(executor->supports_direct_ready_window_successor_masks()
                == expect_combined_entry,
        "combined capability reflects the compact 64-reader mask capacity");

    const auto expected = evaluate_region_activation_kernel_reference(
        kernel, fixture.checked_image);
    if (expect_combined_entry) {
      require(executor->execute_direct_ready_window_prepared(
                  fixture.direct_image, fixture.input_window,
                  fixture.current_internal_values,
                  fixture.ordered_internal_prefix, fixture.output_batch,
                  &fixture.successor_abi),
          "64-reader direct-ready output and successor mask execute together");
      require(fixture.changed.size() == 1U
              && fixture.changed.front() == 1U
              && fixture.successor_masks.size() == 1U
              && fixture.successor_masks.front() == UINT64_MAX,
          "the 64-reader combined entry preserves the highest bit, bit 63");
    } else {
      const std::vector<PackedLogic4> registers_before_decline {
          executor->activation_registers().begin(),
          executor->activation_registers().end() };
      const auto outputs_before_decline = fixture.replacement_snapshot();
      require(!executor->execute_direct_ready_window_prepared(
                  fixture.direct_image, fixture.input_window,
                  fixture.current_internal_values,
                  fixture.ordered_internal_prefix, fixture.output_batch,
                  &fixture.successor_abi)
              && std::ranges::equal(registers_before_decline,
                  executor->activation_registers())
              && fixture.replacement_snapshot() == outputs_before_decline,
          "65-reader combined entry declines before frame, output, or mask mutation");
      require(executor->execute_direct_ready_window_prepared(
                  fixture.direct_image, fixture.input_window,
                  fixture.current_internal_values,
                  fixture.ordered_internal_prefix, fixture.output_batch),
          "65-reader kernels retain the compatible V1 direct-ready entry");
      require(fixture.changed.size() == 1U
              && fixture.changed.front() == 1U
              && fixture.successor_masks.size() == 1U
              && fixture.successor_masks.front() == UINT64_MAX,
          "V1 execution does not overwrite an unsupported successor sidecar");
    }

    const auto actual = executor->activation_registers();
    for (const auto& output : kernel.outputs) {
      require(actual[output.value_register] == expected[output.value_register],
          "direct-ready execution preserves every many-reader output");
    }
  };

  run(64U, true);
  run(65U, false);
}

void check_forwarding_permutation_and_all_ready(
    const fsim::compiler::JitOptimizationLevel optimization)
{
  auto forwarding = make_nontrivial_forwarding_kernel();
  auto& kernel = forwarding.execution_kernel;
  fsim::compiler::LlvmJitOptions options;
  options.optimization = optimization;
  options.cache_directory.clear();
  auto executor = fsim::compiler::LlvmRegionKernelExecutor::try_create(
      kernel, options, "region-forwarding-permutation-v1");
  require(executor != nullptr,
      "LLVM accepts a valid nontrivial ProcessId-to-body permutation");

  const auto parent_output = std::ranges::find(kernel.outputs, 1U,
      &RegionConeOutputBinding::signal);
  const auto child_output = std::ranges::find(kernel.outputs, 2U,
      &RegionConeOutputBinding::signal);
  require(parent_output != kernel.outputs.end()
          && child_output != kernel.outputs.end(),
      "the reversed-ID chain retains both internal and boundary outputs");

  const auto parent_member = std::ranges::find(kernel.members, 1U,
      &RegionConeKernelMember::process);
  const auto child_member = std::ranges::find(kernel.members, 0U,
      &RegionConeKernelMember::process);
  require(parent_member != kernel.members.end()
          && child_member != kernel.members.end(),
      "the reversed-ID chain retains producer and consumer member records");
  std::vector<RegisterId> parent_copy_destinations;
  for (std::size_t index = parent_member->begin;
       index < parent_member->end; ++index) {
    const auto operation = kernel.program.operations.expanded(index);
    if (const auto* const copy
        = operation_get_if<CopyRegister>(&operation);
        copy != nullptr) {
      parent_copy_destinations.push_back(copy->destination);
    }
  }
  const bool child_reads_parent_snapshot = [&] {
    for (std::size_t index = child_member->begin;
         index < child_member->end; ++index) {
      const auto operation = kernel.program.operations.expanded(index);
      const auto* const copy = operation_get_if<CopyRegister>(&operation);
      const bool parent_member_register = copy != nullptr
          && std::ranges::any_of(parent_member->register_bindings,
              [copy](const auto& binding) {
                return binding.activation_register == copy->source;
              });
      if (copy != nullptr && !parent_member_register
          && std::ranges::find(parent_copy_destinations, copy->source)
              != parent_copy_destinations.end()) {
        return true;
      }
    }
    return false;
  }();
  require(child_reads_parent_snapshot,
      "the child CopyRegister reads the producer's separate output snapshot");

  const auto compare = [&](const RegionKernelActivationImage& image,
                           const fsim::compiler::llvm_detail::
                               RegionKernelBodySelection expected_body,
                           const std::string_view message) {
    const auto expected = evaluate_region_activation_kernel_reference(
        kernel, image);
    require(executor->execute(image), message);
    require(fsim::compiler::llvm_detail::RegionKernelTestAccess::
                last_body_selection(*executor) == expected_body,
        "forwarding selects the known or four-state body from current inputs");
    const auto actual = executor->activation_registers();
    compare_defined_member_registers(kernel, image, expected, actual);
    for (const auto& output : kernel.outputs) {
      require(actual[output.value_register]
              == expected[output.value_register],
          "forwarded snapshots match the four-state reference result");
    }
  };
  using fsim::compiler::llvm_detail::RegionKernelBodySelection;

  auto image = make_all_ready_forwarding_image(kernel);
  compare(image, RegionKernelBodySelection::known_logic4,
      "the complete root/child forwarding body executes at O0 and O2");
  require(executor->activation_registers()[parent_output->value_register]
              == PackedLogic4 { 1U, Logic4::zero }
          && executor->activation_registers()[child_output->value_register]
              == PackedLogic4 { 1U, Logic4::one },
      "known zero input forwards known parent and child values");

  auto known_one = image;
  const auto boundary_input_register = kernel.inputs.front().value_register;
  const auto input_register = std::ranges::find(known_one.register_inputs,
      boundary_input_register, &RegionKernelRegisterInput::register_id);
  require(input_register != known_one.register_inputs.end(),
      "the forwarding fixture exposes its boundary input register");
  input_register->value.set(0U, Logic4::one);
  compare(known_one, RegionKernelBodySelection::known_logic4,
      "known one input also enters the cross-member known body");
  require(executor->activation_registers()[parent_output->value_register]
              == PackedLogic4 { 1U, Logic4::one }
          && executor->activation_registers()[child_output->value_register]
              == PackedLogic4 { 1U, Logic4::zero },
      "known one input forwards through the internal CopyRegister");

  auto unknown_x = known_one;
  const auto x_input = std::ranges::find(unknown_x.register_inputs,
      boundary_input_register, &RegionKernelRegisterInput::register_id);
  require(x_input != unknown_x.register_inputs.end(),
      "the X fallback case retains its boundary input register");
  x_input->value.set(0U, Logic4::x);
  compare(unknown_x, RegionKernelBodySelection::four_state,
      "an X boundary value declines specialization to the four-state body");
  require(executor->activation_registers()[parent_output->value_register]
              == PackedLogic4 { 1U, Logic4::x }
          && executor->activation_registers()[child_output->value_register]
              == PackedLogic4 { 1U, Logic4::x },
      "X propagates through producer and consumer forwarding");

  auto unknown_z = known_one;
  const auto z_input = std::ranges::find(unknown_z.register_inputs,
      boundary_input_register, &RegionKernelRegisterInput::register_id);
  require(z_input != unknown_z.register_inputs.end(),
      "the Z fallback case retains its boundary input register");
  z_input->value.set(0U, Logic4::z);
  compare(unknown_z, RegionKernelBodySelection::four_state,
      "a Z boundary value also declines to the four-state body");
  require(executor->activation_registers()[parent_output->value_register]
              == PackedLogic4 { 1U, Logic4::z }
          && executor->activation_registers()[child_output->value_register]
              == PackedLogic4 { 1U, Logic4::x },
      "internal CopyRegister preserves Z before the child's X result");

  compare(known_one, RegionKernelBodySelection::known_logic4,
      "known specialization recovers after X/Z four-state activations");

  auto partial = image;
  partial.active_member_indices = { 0U };
  partial.ready_processes = { kernel.members[0U].process };
  partial.requests = { image.requests[0U] };
  partial.generation = image.generation + 1U;
  partial.scheduler_prefix.frontier_generation = partial.generation;
  partial.scheduler_prefix.frontier_end = 1U;
  partial.scheduler_prefix.tasks = { image.scheduler_prefix.tasks[0U] };
  const auto omitted_parent = std::ranges::find(kernel.members, 1U,
      &RegionConeKernelMember::process);
  require(omitted_parent != kernel.members.end(),
      "partial-readiness fixture has a distinct omitted producer");
  const auto omitted_ready = std::ranges::find(partial.register_inputs,
      omitted_parent->readiness_register,
      &RegionKernelRegisterInput::register_id);
  require(omitted_ready != partial.register_inputs.end(),
      "partial-readiness fixture has the producer readiness input");
  omitted_ready->value.assign_word({ 1U, 0U, 0U });
  const auto boundary_input = std::ranges::find(partial.register_inputs,
      kernel.inputs.front().value_register,
      &RegionKernelRegisterInput::register_id);
  require(boundary_input != partial.register_inputs.end(),
      "partial-readiness fixture has its boundary input register");
  boundary_input->value.assign_word({ 1U, 0U, 0U });
  const std::vector<PackedLogic4> before {
      executor->activation_registers().begin(),
      executor->activation_registers().end(),
  };
  require(!executor->execute(partial),
      "a forwarding body declines unless every member is ready");
  require(std::ranges::equal(before, executor->activation_registers()),
      "partial readiness declines before reusable native registers change");

  image.generation = partial.generation;
  image.scheduler_prefix.frontier_generation = image.generation;
  require(executor->execute(image),
      "a declined partial call leaves the executor available for full readiness");

  for (const auto& bad_order : {
           std::vector<std::size_t> { 1U },
           std::vector<std::size_t> { 0U, 0U },
           std::vector<std::size_t> { 0U, 2U },
       }) {
    auto malformed = kernel;
    malformed.member_execution_order = bad_order;
    require(fsim::compiler::LlvmRegionKernelExecutor::try_create(
                malformed, options,
                "region-forwarding-invalid-permutation-v1") == nullptr,
        "missing, duplicate, and out-of-range member orders decline");
  }
}

void remap_region_kernel_physical_ids(
    RegionConeActivationKernel& kernel,
    const SignalId signal_delta,
    const ProcessId process_delta)
{
  for (auto& input : kernel.inputs) {
    input.signal += signal_delta;
  }
  for (auto& member : kernel.members) {
    member.process += process_delta;
    for (auto& sensitivity : member.sensitivities) {
      sensitivity.signal += signal_delta;
    }
  }
  for (auto& output : kernel.outputs) {
    output.owner += process_delta;
    output.signal += signal_delta;
  }
  for (auto& signal : kernel.internal_signals) {
    signal += signal_delta;
  }
  for (auto& constant : kernel.constant_inputs) {
    constant.owner += process_delta;
    constant.signal += signal_delta;
  }
  kernel.program.id += process_delta;
  kernel.program.name += "_physical_remap";
}

void remap_region_image_process_ids(
    RegionKernelActivationImage& image, const ProcessId process_delta)
{
  for (auto& process : image.ready_processes) {
    process += process_delta;
  }
  for (auto& request : image.requests) {
    request.process += process_delta;
  }
  for (auto& task : image.scheduler_prefix.tasks) {
    task.member.process += process_delta;
  }
}

void compare_region_executor_with_reference(
    const RegionConeActivationKernel& kernel,
    const RegionKernelActivationImage& image,
    fsim::compiler::LlvmRegionKernelExecutor& executor,
    const std::string_view description)
{
  const auto expected = evaluate_region_activation_kernel_reference(
      kernel, image);
  require(executor.execute(image), description);
  const auto actual = executor.activation_registers();
  compare_defined_member_registers(kernel, image, expected, actual);
  for (const auto& output : kernel.outputs) {
    require(actual[output.value_register] == expected[output.value_register],
        "canonical object reuse preserves every mapped output snapshot");
  }
}

void check_native_body_registry_single_flight()
{
  using fsim::compiler::llvm_detail::WeakSingleFlightRegistry;
  struct RegistryProbe final { };
  struct FlightResult final {
    std::array<std::shared_ptr<RegistryProbe>, 2U> bodies;
    std::array<std::exception_ptr, 2U> errors;
    std::size_t factory_calls { };
    bool factory_entered { };
    bool waiter_joined { };
  };

  WeakSingleFlightRegistry<RegistryProbe> registry;
  const auto run_flight = [&](const std::string& key, const auto& factory) {
    std::mutex gate_mutex;
    std::condition_variable gate_changed;
    bool factory_entered { };
    bool release_factory { };
    std::atomic<std::size_t> factory_calls { };
    FlightResult result;
    const auto gated_factory = [&]() -> std::shared_ptr<RegistryProbe> {
      ++factory_calls;
      {
        std::unique_lock lock { gate_mutex };
        factory_entered = true;
        gate_changed.notify_all();
        gate_changed.wait(lock, [&] { return release_factory; });
      }
      return factory();
    };
    const auto invoke = [&](const std::size_t slot) {
      try {
        result.bodies[slot] = registry.get_or_create(key, gated_factory);
      } catch (...) {
        result.errors[slot] = std::current_exception();
      }
    };

    std::thread creator { invoke, 0U };
    {
      std::unique_lock lock { gate_mutex };
      result.factory_entered = gate_changed.wait_for(lock,
          std::chrono::seconds { 5 }, [&] { return factory_entered; });
    }
    if (!result.factory_entered) {
      {
        const std::lock_guard lock { gate_mutex };
        release_factory = true;
      }
      gate_changed.notify_all();
      creator.join();
      result.factory_calls = factory_calls.load();
      return result;
    }

    std::thread waiter;
    try {
      waiter = std::thread { invoke, 1U };
    } catch (...) {
      {
        const std::lock_guard lock { gate_mutex };
        release_factory = true;
      }
      gate_changed.notify_all();
      creator.join();
      result.factory_calls = factory_calls.load();
      return result;
    }
    result.waiter_joined = registry.wait_for_test_waiters(
        key, 1U, std::chrono::seconds { 5 });
    {
      const std::lock_guard lock { gate_mutex };
      release_factory = true;
    }
    gate_changed.notify_all();
    creator.join();
    waiter.join();
    result.factory_calls = factory_calls.load();
    return result;
  };

  const std::string success_key {
      "fsim-region-native-body-registry-single-flight-success-test-v1" };
  const auto success = run_flight(success_key, [] {
    return std::make_shared<RegistryProbe>();
  });
  require(success.factory_entered && success.waiter_joined
          && success.factory_calls == 1U
          && !success.errors[0U] && !success.errors[1U]
          && success.bodies[0U] != nullptr
          && success.bodies[0U] == success.bodies[1U],
      "concurrent registry callers share one successful cold body factory");

  const std::string failure_key {
      "fsim-region-native-body-registry-single-flight-failure-test-v1" };
  const auto failure = run_flight(failure_key, []()
      -> std::shared_ptr<RegistryProbe> {
    throw std::runtime_error { "injected native body factory failure" };
  });
  require(failure.factory_entered && failure.waiter_joined
          && failure.factory_calls == 1U
          && !failure.bodies[0U] && !failure.bodies[1U]
          && failure.errors[0U] && failure.errors[1U],
      "a failed single-flight factory wakes all attached waiters");

  std::size_t retry_calls { };
  auto retried = registry.get_or_create(failure_key, [&] {
    ++retry_calls;
    return std::make_shared<RegistryProbe>();
  });
  auto reused = registry.get_or_create(failure_key, [&] {
    ++retry_calls;
    return std::make_shared<RegistryProbe>();
  });
  require(retried != nullptr && retried == reused && retry_calls == 1U,
      "failed body creation leaves the key available for retry and reuse");
}

void check_concurrent_native_body_creation(
    const fsim::compiler::JitOptimizationLevel optimization)
{
  using fsim::compiler::LlvmRegionKernelExecutor;
  using fsim::compiler::llvm_detail::RegionKernelTestAccess;

  TemporaryRegionCacheDirectory cache_directory;
  fsim::compiler::LlvmJitOptions options;
  options.optimization = optimization;
  options.cache_directory = cache_directory.path();
  static_cast<void>(fsim::compiler::LlvmJit::native_host_identity(
      optimization));

  const auto template_builds_before
      = RegionKernelTestAccess::prepared_template_build_count();
  const auto validations_before
      = RegionKernelTestAccess::prepared_template_validation_count();

  const auto forwarding = make_nontrivial_forwarding_kernel();
  std::array<RegionConeActivationKernel, 2U> kernels {
      forwarding.execution_kernel, forwarding.execution_kernel };
  constexpr SignalId signal_delta { 90U };
  constexpr ProcessId process_delta { 50U };
  remap_region_kernel_physical_ids(
      kernels[1U], signal_delta, process_delta);
  const std::array images {
      make_all_ready_forwarding_image(kernels[0U]),
      make_all_ready_forwarding_image(kernels[1U]),
  };

  std::mutex start_mutex;
  std::condition_variable start_changed;
  std::size_t waiting_threads { };
  bool release_threads { };
  std::array<std::unique_ptr<LlvmRegionKernelExecutor>, 2U> executors;
  std::array<std::exception_ptr, 2U> errors;
  const auto create = [&](const std::size_t index) {
    {
      std::unique_lock lock { start_mutex };
      ++waiting_threads;
      start_changed.notify_all();
      start_changed.wait(lock, [&] { return release_threads; });
    }
    try {
      executors[index] = LlvmRegionKernelExecutor::try_create(
          kernels[index], options,
          "region-concurrent-native-body-fixture-v1");
    } catch (...) {
      errors[index] = std::current_exception();
    }
  };
  std::thread first { create, 0U };
  std::thread second;
  try {
    second = std::thread { create, 1U };
  } catch (...) {
    {
      const std::lock_guard lock { start_mutex };
      release_threads = true;
    }
    start_changed.notify_all();
    first.join();
    throw;
  }
  bool both_waiting { };
  {
    std::unique_lock lock { start_mutex };
    both_waiting = start_changed.wait_for(lock, std::chrono::seconds { 5 },
        [&] { return waiting_threads == 2U; });
    release_threads = true;
  }
  start_changed.notify_all();
  first.join();
  second.join();

  require(both_waiting && !errors[0U] && !errors[1U]
          && executors[0U] != nullptr && executors[1U] != nullptr,
      "concurrent cold wrappers both finish native-body creation");
  require(RegionKernelTestAccess::prepared_template_build_count()
              == template_builds_before + 1U
          && RegionKernelTestAccess::prepared_template_validation_count()
              == validations_before + 1U
          && RegionKernelTestAccess::prepared_template_identity(
                 *executors[0U])
              == RegionKernelTestAccess::prepared_template_identity(
                  *executors[1U]),
      "concurrent remapped wrappers build and validate one shared template");
  const auto* const shared_body
      = RegionKernelTestAccess::native_body_identity(*executors[0U]);
  require(shared_body != nullptr
          && shared_body
              == RegionKernelTestAccess::native_body_identity(*executors[1U]),
      "concurrent isomorphic wrappers retain one compiled native body");
  require(executors[0U]->cache_identity()
              != executors[1U]->cache_identity()
          && RegionKernelTestAccess::frame_identity(*executors[0U])
              != RegionKernelTestAccess::frame_identity(*executors[1U])
          && RegionKernelTestAccess::register_storage_identity(*executors[0U])
              != RegionKernelTestAccess::register_storage_identity(
                  *executors[1U]),
      "shared code keeps exact mappings and mutable frames instance-local");
  compare_region_executor_with_reference(kernels[0U], images[0U],
      *executors[0U], "the first concurrent instance executes independently");
  compare_region_executor_with_reference(kernels[1U], images[1U],
      *executors[1U], "the second concurrent instance executes independently");

  executors[1U].reset();
  require(RegionKernelTestAccess::native_body_identity(*executors[0U])
              == shared_body,
      "destroying one wrapper leaves the shared body owned by its peer");
  compare_region_executor_with_reference(kernels[0U], images[0U],
      *executors[0U], "the surviving wrapper remains executable after peer destruction");
}

void check_prepared_template_signal_alias_identity(
    const fsim::compiler::JitOptimizationLevel optimization)
{
  using fsim::compiler::LlvmRegionKernelExecutor;
  using fsim::compiler::llvm_detail::RegionKernelTestAccess;

  TemporaryRegionCacheDirectory cache_directory;
  fsim::compiler::LlvmJitOptions options;
  options.optimization = optimization;
  options.cache_directory = cache_directory.path();

  auto distinct_signals = make_logic4_kernel();
  distinct_signals.inputs.front().internal = false;
  auto aliased_signals = distinct_signals;
  aliased_signals.inputs[1U].signal = aliased_signals.inputs.front().signal;

  auto distinct = LlvmRegionKernelExecutor::try_create(distinct_signals,
      options, "region-prepared-template-alias-fixture-v1");
  auto aliased = LlvmRegionKernelExecutor::try_create(aliased_signals,
      options, "region-prepared-template-alias-fixture-v1");
  require(distinct != nullptr && aliased != nullptr,
      "both distinct and aliased input maps have valid private templates");
  require(RegionKernelTestAccess::prepared_template_identity(*distinct)
              != RegionKernelTestAccess::prepared_template_identity(*aliased),
      "canonical preparation identity preserves physical signal alias relationships");
}

void check_canonical_native_code_identity(
    const fsim::compiler::JitOptimizationLevel optimization,
    const bool verify_distinct_codegen_inputs)
{
  using fsim::compiler::RegionKernelSpecialization;
  using fsim::compiler::LlvmRegionKernelExecutor;
  using fsim::compiler::llvm_detail::RegionKernelTestAccess;

  TemporaryRegionCacheDirectory cache_directory;
  fsim::compiler::LlvmJitOptions options;
  options.optimization = optimization;
  options.cache_directory = cache_directory.path();

  auto program = make_nontrivial_region_program();
  require(program.forwarding_kernel.has_value(),
      "the graph-built component retains its optional forwarding form");
  const auto& forwarding = *program.forwarding_kernel;
  auto kernel = program.activation_kernel;
  require(kernel.internal_signals == forwarding.internal_signals
          && std::ranges::all_of(kernel.internal_signals,
              [&kernel](const SignalId signal) {
                const auto input = std::ranges::find_if(kernel.inputs,
                    [signal](const RegionConeKernelInput& candidate) {
                      return candidate.signal == signal
                          && candidate.internal;
                    });
                const auto output = std::ranges::find(kernel.outputs,
                    signal, &RegionConeOutputBinding::signal);
                return input != kernel.inputs.end()
                    && output != kernel.outputs.end()
                    && input->width == output->width
                    && input->value_kind == output->value_kind;
              }),
      "the activation body binds exact internal current-value slots");
  require(forwarding.execution_kernel.internal_signals.empty(),
      "the forwarding body keeps internal values in private registers");
  fsim::compiler::LlvmJitOptions forwarding_options = options;
  forwarding_options.cache_directory.clear();
  auto forwarding_executor = LlvmRegionKernelExecutor::try_create(
      forwarding.execution_kernel, forwarding_options,
      "region-forwarding-without-direct-ready-inputs-v1");
  require(forwarding_executor != nullptr
          && !forwarding_executor->supports_direct_ready_window(),
      "flattened forwarding remains checked-only without internal input slots");
  auto image = make_all_ready_forwarding_image(kernel);
  const auto template_builds_before
      = RegionKernelTestAccess::prepared_template_build_count();
  const auto validations_before
      = RegionKernelTestAccess::prepared_template_validation_count();
  auto original = LlvmRegionKernelExecutor::try_create(
      kernel, options, "region-canonical-native-code-fixture-v1");
  require(original != nullptr && original->supports_direct_ready_window()
          && original->supports_direct_ready_window_successor_masks(),
      "the graph-built activation body has native direct-ready and "
      "successor-mask entries");
  const auto* const original_template
      = RegionKernelTestAccess::prepared_template_identity(*original);
  require(original_template != nullptr
          && RegionKernelTestAccess::prepared_template_build_count()
              == template_builds_before + 1U
          && RegionKernelTestAccess::prepared_template_validation_count()
              == validations_before + 1U,
      "the first wrapper builds and validates one immutable template");
  const auto mapping_identity
      = std::string { original->cache_identity() };
  const auto native_code_identity
      = std::string { RegionKernelTestAccess::native_code_identity(*original) };
  const auto* const original_synthetic
      = RegionKernelTestAccess::synthetic_process(*original);
  require(original_synthetic != nullptr && original_synthetic->id == 0U,
      "the private JIT process uses its canonical identity, not a member ID");
  require(!native_code_identity.empty(),
      "the native body has an internal canonical code identity");
  auto malformed_mapping = kernel;
  malformed_mapping.members.front().readiness_register
      = static_cast<std::uint32_t>(malformed_mapping.program.register_count);
  require(LlvmRegionKernelExecutor::try_create(malformed_mapping, options,
              "region-canonical-native-code-fixture-v1") == nullptr
          && RegionKernelTestAccess::prepared_template_build_count()
              == template_builds_before + 1U
          && RegionKernelTestAccess::prepared_template_validation_count()
              == validations_before + 1U,
      "a malformed physical member map declines before template lookup");
  const auto first_objects = region_cache_files(cache_directory.path());
  require(!first_objects.empty(),
      "the first direct-ready body writes a persistent native object");

  auto remapped_kernel = kernel;
  constexpr SignalId signal_delta { 70U };
  constexpr ProcessId process_delta { 40U };
  remap_region_kernel_physical_ids(
      remapped_kernel, signal_delta, process_delta);
  auto remapped_image = image;
  remap_region_image_process_ids(remapped_image, process_delta);
  auto remapped = LlvmRegionKernelExecutor::try_create(
      remapped_kernel, options, "region-canonical-native-code-fixture-v1");
  require(remapped != nullptr && remapped->supports_direct_ready_window()
          && remapped->supports_direct_ready_window_successor_masks(),
      "an isomorphic physical mapping keeps both direct-ready entries");
  require(remapped->cache_identity() != mapping_identity,
      "the executor identity still distinguishes physical signal and member bindings");
  require(RegionKernelTestAccess::native_code_identity(*remapped)
              == native_code_identity,
      "the synthetic native code identity canonicalizes physical IDs");
  require(RegionKernelTestAccess::prepared_template_identity(*remapped)
              == original_template
          && RegionKernelTestAccess::prepared_template_build_count()
              == template_builds_before + 1U
          && RegionKernelTestAccess::prepared_template_validation_count()
              == validations_before + 1U
          && RegionKernelTestAccess::synthetic_process(*remapped)
              == original_synthetic,
      "remapped instances reuse the same prepared process and validator result");
  const auto* const shared_native_body
      = RegionKernelTestAccess::native_body_identity(*original);
  require(shared_native_body != nullptr
          && shared_native_body
              == RegionKernelTestAccess::native_body_identity(*remapped),
      "physical mappings share the actual compiled LlvmJit owner");
  require(RegionKernelTestAccess::frame_identity(*original)
              != RegionKernelTestAccess::frame_identity(*remapped)
          && RegionKernelTestAccess::register_storage_identity(*original)
              != RegionKernelTestAccess::register_storage_identity(*remapped),
      "physical mappings retain independent native frames and register planes");
  const auto* const remapped_synthetic
      = RegionKernelTestAccess::synthetic_process(*remapped);
  require(remapped_synthetic != nullptr
          && remapped_synthetic->id == original_synthetic->id
          && remapped_synthetic->name == original_synthetic->name,
      "the compiled process key is stable across physical member IDs");
  require(remapped->outputs().size() == kernel.outputs.size()
          && remapped->outputs().front().owner
              == kernel.outputs.front().owner + process_delta
          && remapped->outputs().front().signal
              == kernel.outputs.front().signal + signal_delta,
      "the mapped wrapper retains its exact physical output bindings");
  require(region_cache_files(cache_directory.path()) == first_objects,
      "the isomorphic mapping reuses the existing persistent native artifact");
  compare_region_executor_with_reference(
      kernel, image, *original,
      "the original mapped kernel executes independently");
  compare_region_executor_with_reference(
      remapped_kernel, remapped_image, *remapped,
      "the remapped kernel executes against its own physical bindings");

  auto original_direct = make_direct_ready_sharing_fixture(
      kernel, Logic4::zero, Logic4::one);
  auto remapped_direct = make_direct_ready_sharing_fixture(
      remapped_kernel, Logic4::one, Logic4::zero);
  const std::vector<PackedLogic4> remapped_registers_before_decline {
      remapped->activation_registers().begin(),
      remapped->activation_registers().end() };
  const auto remapped_replacements_before_decline
      = remapped_direct.replacement_snapshot();
  require(!remapped->execute_direct_ready_window_prepared(
              remapped_direct.direct_image, original_direct.input_window,
              remapped_direct.current_internal_values,
              remapped_direct.ordered_internal_prefix,
              remapped_direct.output_batch)
          && std::ranges::equal(remapped_registers_before_decline,
              remapped->activation_registers())
          && remapped_direct.replacement_snapshot()
              == remapped_replacements_before_decline,
      "a shared body rejects a peer's physical input mapping before mutation");

  auto invalid_successor = original_direct.successor_abi;
  --invalid_successor.struct_size;
  std::ranges::fill(original_direct.successor_masks, UINT64_MAX);
  const std::vector<PackedLogic4> original_registers_before_sidecar_decline {
      original->activation_registers().begin(),
      original->activation_registers().end() };
  const auto original_replacements_before_sidecar_decline
      = original_direct.replacement_snapshot();
  require(!original->execute_direct_ready_window_prepared(
              original_direct.direct_image, original_direct.input_window,
              original_direct.current_internal_values,
              original_direct.ordered_internal_prefix,
              original_direct.output_batch, &invalid_successor)
          && std::ranges::equal(original_registers_before_sidecar_decline,
              original->activation_registers())
          && original_direct.replacement_snapshot()
              == original_replacements_before_sidecar_decline,
      "a malformed direct-ready successor sidecar declines before frame, output, or mask mutation");

  const auto execute_direct = [](
      const RegionConeActivationKernel& selected_kernel,
      DirectReadySharingFixture& fixture,
      LlvmRegionKernelExecutor& selected_executor,
      const bool expect_changed,
      const std::string_view description) {
    const auto expected = evaluate_region_activation_kernel_reference(
        selected_kernel, fixture.checked_image);
    require(selected_executor.execute_direct_ready_window_prepared(
                fixture.direct_image, fixture.input_window,
                fixture.current_internal_values,
                fixture.ordered_internal_prefix, fixture.output_batch,
                &fixture.successor_abi),
        description);
    require(RegionKernelTestAccess::last_body_selection(selected_executor)
            == fsim::compiler::llvm_detail::RegionKernelBodySelection::four_state,
        "shared physical mappings invoke the direct-ready native entry");
    const auto actual = selected_executor.activation_registers();
    for (const auto& output : selected_kernel.outputs) {
      require(actual[output.value_register]
                  == expected[output.value_register],
          "the direct-ready shared body preserves each mapped output");
    }
    const auto internal = std::ranges::find(selected_kernel.internal_signals,
        fixture.output_slots.front().signal_id);
    require(internal != selected_kernel.internal_signals.end(),
        "the direct prepared role belongs to this instance's internal signal");
    const auto internal_index = static_cast<std::size_t>(
        internal - selected_kernel.internal_signals.begin());
    const auto output = std::ranges::find(selected_kernel.outputs,
        *internal, &RegionConeOutputBinding::signal);
    require(output != selected_kernel.outputs.end(),
        "the prepared direct role has an exact output binding");
    const auto expected_word = expected[output->value_register].low_word();
    require(fixture.changed[internal_index]
                    == (expect_changed ? 1U : 0U)
            && fixture.value_ready[internal_index]
                == (expect_changed ? 1U : 0U)
            && fixture.transaction_ready[internal_index] == 1U
            && fixture.next_current_aval[internal_index] == expected_word.aval
            && fixture.next_current_bval[internal_index] == expected_word.bval
            && fixture.next_stored_aval[internal_index] == expected_word.aval
            && fixture.next_stored_bval[internal_index] == expected_word.bval
            && fixture.next_owner_aval[internal_index] == expected_word.aval
            && fixture.next_owner_bval[internal_index] == expected_word.bval,
        "the shared entry writes only the selected instance's prepared output");
    std::uint64_t expected_mask { };
    if (expect_changed) {
      std::size_t ordinal { };
      for (const auto& member : selected_kernel.members) {
        const bool reads_whole_any = std::ranges::any_of(
            member.sensitivities, [internal = *internal](const auto& sensitivity) {
              return sensitivity.signal == internal
              && sensitivity.edge == fsim::runtime::simir::EdgeKind::any
                  && sensitivity.offset == 0U
                  && sensitivity.width == 0U;
            });
        if (reads_whole_any) {
          require(ordinal < 64U,
              "direct-ready successor witness fits one canonical member mask");
          expected_mask |= UINT64_C(1) << ordinal;
          ++ordinal;
        }
      }
    }
    require(fixture.successor_masks[internal_index] == expected_mask,
        "direct-ready masks are emitted only for changed outputs and use member ordinals");
  };
  execute_direct(kernel, original_direct, *original,
      true,
      "the original physical mapping executes the direct-ready entry");
  execute_direct(remapped_kernel, remapped_direct, *remapped,
      true,
      "the remapped physical mapping executes the same direct-ready body");

  auto unchanged_direct = make_direct_ready_sharing_fixture(
      kernel, Logic4::zero, Logic4::zero);
  auto unchanged_executor = LlvmRegionKernelExecutor::try_create(
      kernel, options, "region-canonical-native-code-unchanged-v1");
  require(unchanged_executor != nullptr
          && unchanged_executor->supports_direct_ready_window()
          && unchanged_executor->supports_direct_ready_window_successor_masks(),
      "the unchanged-output case retains the combined native entry");
  execute_direct(kernel, unchanged_direct, *unchanged_executor, false,
      "the direct-ready entry completes with unchanged output values");

  auto config_variant_options = options;
  config_variant_options.cache_maximum_entries
      = options.cache_maximum_entries.value_or(0U) + 1U;
  auto config_variant = LlvmRegionKernelExecutor::try_create(
      kernel, config_variant_options,
      "region-canonical-native-code-fixture-v1");
  require(config_variant != nullptr
          && RegionKernelTestAccess::native_code_identity(*config_variant)
              == native_code_identity
          && RegionKernelTestAccess::native_body_identity(*config_variant)
              != shared_native_body
          && RegionKernelTestAccess::prepared_template_identity(
                 *config_variant)
              != original_template,
      "a distinct effective JIT configuration gets a separate body owner");

  if (verify_distinct_codegen_inputs) {
    auto tier_variant_options = options;
    tier_variant_options.optimization
        = optimization == fsim::compiler::JitOptimizationLevel::o0
            ? fsim::compiler::JitOptimizationLevel::o2
            : fsim::compiler::JitOptimizationLevel::o0;
    auto tier_variant = LlvmRegionKernelExecutor::try_create(
        kernel, tier_variant_options,
        "region-canonical-native-code-fixture-v1");
    require(tier_variant != nullptr
            && RegionKernelTestAccess::native_code_identity(*tier_variant)
                == native_code_identity
            && RegionKernelTestAccess::native_body_identity(*tier_variant)
                != shared_native_body
            && RegionKernelTestAccess::prepared_template_identity(
                   *tier_variant)
                != original_template,
        "different LLVM optimization tiers use different compiled bodies");
  }

  auto scope_variant_kernel = kernel;
  std::size_t changed_debug_members { };
  for (auto& member : scope_variant_kernel.members) {
    if (!member.final_debug_state) {
      continue;
    }
    std::optional<InstructionIndex> final_debug_instruction;
    for (std::size_t index = member.begin; index < member.end; ++index) {
      const auto operation
          = scope_variant_kernel.program.operations.expanded(index);
      if (operation_holds<DebugPoint>(operation)) {
        final_debug_instruction
            = static_cast<InstructionIndex>(index);
      }
    }
    require(final_debug_instruction.has_value(),
        "each remapped final source state has its original DebugPoint");
    auto operation = scope_variant_kernel.program.operations.expanded(
        *final_debug_instruction);
    const auto* const original_debug
        = operation_get_if<DebugPoint>(&operation);
    require(original_debug != nullptr,
        "the final source marker is a DebugPoint");
    auto remapped_debug = *original_debug;
    remapped_debug.source = SourceLocation {
        "forwarding-other-instance.sv", original_debug->source.line,
        original_debug->source.column };
    remapped_debug.scope = original_debug->scope.str() + ".other_instance";
    scope_variant_kernel.program.operations.replace(
        *final_debug_instruction, remapped_debug);
    member.final_debug_state->source = remapped_debug.source;
    member.final_debug_state->scope = remapped_debug.scope.str();
    ++changed_debug_members;
  }
  require(changed_debug_members == kernel.members.size(),
      "the scope witness changes every wrapper member's final source state");
  const auto debug_variant_objects
      = region_cache_files(cache_directory.path());
  auto debug_variant = LlvmRegionKernelExecutor::try_create(
      scope_variant_kernel, options,
      "region-canonical-native-code-fixture-v1");
  require(debug_variant != nullptr
          && debug_variant->cache_identity() != mapping_identity
          && RegionKernelTestAccess::native_code_identity(*debug_variant)
              == native_code_identity
          && RegionKernelTestAccess::native_body_identity(*debug_variant)
              == shared_native_body
          && RegionKernelTestAccess::prepared_template_identity(
                 *debug_variant)
              == original_template
          && region_cache_files(cache_directory.path()) == debug_variant_objects,
      "instance DebugPoint paths and scopes stay outside the shared code identity");
  const auto* const retained_debug_kernel
      = RegionKernelTestAccess::source_kernel(*debug_variant);
  require(retained_debug_kernel != nullptr
          && retained_debug_kernel->members == scope_variant_kernel.members,
      "the executor retains each exact original member debug state");
  for (const auto& member : scope_variant_kernel.members) {
    for (std::size_t index = member.begin; index < member.end; ++index) {
      const auto expected_operation
          = scope_variant_kernel.program.operations.expanded(index);
      if (!operation_holds<DebugPoint>(expected_operation)) {
        continue;
      }
      const auto actual_operation
          = retained_debug_kernel->program.operations.expanded(index);
      const auto* const expected_debug
          = operation_get_if<DebugPoint>(&expected_operation);
      const auto* const actual_debug
          = operation_get_if<DebugPoint>(&actual_operation);
      require(expected_debug != nullptr && actual_debug != nullptr,
          "the retained source operation stays a DebugPoint");
      require(actual_debug->scope == expected_debug->scope
              && actual_debug->source.path.str()
                  == expected_debug->source.path.str()
              && actual_debug->source.line == expected_debug->source.line
              && actual_debug->source.column == expected_debug->source.column,
          "the executor retains each exact source DebugPoint");
    }
  }
  compare_region_executor_with_reference(scope_variant_kernel, image,
      *debug_variant,
      "the debug-remapped instance executes its independently mapped kernel");

  auto logic9_kernel = make_logic9_builder_kernel(65U);
  auto logic9_image = make_logic9_builder_image(logic9_kernel, 1U,
      std::array { std::size_t { 0U }, std::size_t { 1U } },
      make_logic9_value(65U, 0U), make_logic9_value(65U, 3U),
      PackedLogic4::from_msb_string("10XZ"));
  auto logic9 = LlvmRegionKernelExecutor::try_create(
      logic9_kernel, options, "region-canonical-native-code-fixture-v1");
  require(logic9 != nullptr,
      "the mixed Logic9 frame-export body has a native entry");
  const auto logic9_mapping_identity
      = std::string { logic9->cache_identity() };
  const auto logic9_native_identity = std::string {
      RegionKernelTestAccess::native_code_identity(*logic9) };
  const auto* const logic9_process
      = RegionKernelTestAccess::synthetic_process(*logic9);
  require(logic9_process != nullptr
          && !logic9_process->debug_locals.empty()
          && logic9_process->debug_locals.front().name.starts_with(
              "__fsim_region_export_0_"),
      "private Logic9 export locals use member ordinals");
  const auto logic9_objects = region_cache_files(cache_directory.path());
  require(logic9_objects.size() > first_objects.size(),
      "the distinct Logic9 frame-export shape gets its own native object");

  auto remapped_logic9_kernel = logic9_kernel;
  constexpr SignalId logic9_signal_delta { 110U };
  constexpr ProcessId logic9_process_delta { 80U };
  remap_region_kernel_physical_ids(remapped_logic9_kernel,
      logic9_signal_delta, logic9_process_delta);
  auto remapped_logic9_image = logic9_image;
  remap_region_image_process_ids(
      remapped_logic9_image, logic9_process_delta);
  auto remapped_logic9 = LlvmRegionKernelExecutor::try_create(
      remapped_logic9_kernel, options,
      "region-canonical-native-code-fixture-v1");
  require(remapped_logic9 != nullptr
          && remapped_logic9->cache_identity() != logic9_mapping_identity
          && RegionKernelTestAccess::native_code_identity(*remapped_logic9)
              == logic9_native_identity
          && RegionKernelTestAccess::native_body_identity(*remapped_logic9)
              == RegionKernelTestAccess::native_body_identity(*logic9)
          && RegionKernelTestAccess::prepared_template_identity(
                 *remapped_logic9)
              == RegionKernelTestAccess::prepared_template_identity(*logic9),
      "Logic9 process IDs remap while the frame-export code identity stays canonical");
  const auto* const remapped_logic9_process
      = RegionKernelTestAccess::synthetic_process(*remapped_logic9);
  require(remapped_logic9_process != nullptr
          && remapped_logic9_process->debug_locals.size()
              == logic9_process->debug_locals.size(),
      "mapped Logic9 frame exports retain the same private local shape");
  for (std::size_t index = 0U;
       index < logic9_process->debug_locals.size(); ++index) {
    require(remapped_logic9_process->debug_locals[index].name
                == logic9_process->debug_locals[index].name,
        "private Logic9 local names do not retain physical ProcessIds");
  }
  require(region_cache_files(cache_directory.path()) == logic9_objects,
      "isomorphic Logic9 mappings reuse the persistent frame-export artifact");
  compare_region_executor_with_reference(logic9_kernel, logic9_image,
      *logic9, "the original Logic9 wrapper exports its own frame values");
  compare_region_executor_with_reference(remapped_logic9_kernel,
      remapped_logic9_image, *remapped_logic9,
      "the remapped Logic9 wrapper exports values through its own frame");

  if (!verify_distinct_codegen_inputs) {
    return;
  }

  const auto require_new_artifact = [&](
      const RegionConeActivationKernel& candidate,
      const RegionKernelSpecialization specialization,
      const std::string_view description) {
    const auto before = region_cache_files(cache_directory.path());
    auto candidate_executor = LlvmRegionKernelExecutor::try_create(
        candidate, options, "region-canonical-native-code-fixture-v1",
        specialization);
    require(candidate_executor != nullptr, description);
    require(RegionKernelTestAccess::native_code_identity(
                *candidate_executor)
            != native_code_identity,
        "a codegen-relevant change gets a distinct shared-body identity");
    const auto after = region_cache_files(cache_directory.path());
    require(after.size() > before.size(),
        "a codegen-relevant change gets a distinct persistent artifact");
    return candidate_executor;
  };

  auto changed_opcode = kernel;
  bool changed_operation { };
  for (std::size_t index = 0U;
       index < changed_opcode.program.operations.size(); ++index) {
    const auto operation = changed_opcode.program.operations.expanded(index);
    const auto* const unary = operation_get_if<UnaryNot>(&operation);
    if (unary == nullptr) {
      continue;
    }
    changed_opcode.program.operations.replace(index,
        CopyRegister { unary->destination, unary->source });
    changed_operation = true;
    break;
  }
  require(changed_operation,
      "the opcode identity witness finds a unary compute instruction");
  auto changed_opcode_executor = require_new_artifact(changed_opcode,
      RegionKernelSpecialization::dynamic_inputs,
      "the changed-opcode native entry remains admissible");
  require(RegionKernelTestAccess::native_body_identity(
              *changed_opcode_executor) != shared_native_body,
      "different code shapes do not reuse the compiled body");

  for (const auto width : { std::uint32_t { 65U }, std::uint32_t { 129U } }) {
    auto [wide_kernel, unused_image]
        = make_wide_builder_activation_kernel(width);
    static_cast<void>(unused_image);
    require_new_artifact(wide_kernel,
        RegionKernelSpecialization::dynamic_inputs,
        "a wide width-shape native entry remains admissible");
  }

  auto [logic4_kernel, logic4_image] = make_builder_activation_kernel();
  const auto boundary = std::ranges::find_if(logic4_kernel.inputs,
      [](const RegionConeKernelInput& input) { return !input.internal; });
  require(boundary != logic4_kernel.inputs.end(),
      "the entry-mode witness identifies a certified boundary input");
  const auto boundary_value = std::ranges::find(logic4_image.register_inputs,
      boundary->value_register, &RegionKernelRegisterInput::register_id);
  require(boundary_value != logic4_image.register_inputs.end(),
      "the entry-mode witness captures its boundary value");
  logic4_kernel.constant_inputs.push_back({ 99U, boundary->signal, 0U,
      boundary->width, ValueKind::logic4,
      SignalUpdateDomain::systemverilog_active, boundary_value->value });
  auto dynamic_logic4 = require_new_artifact(logic4_kernel,
      RegionKernelSpecialization::dynamic_inputs,
      "the dynamic-input entry remains admissible in the artifact matrix");
  auto constant_logic4 = require_new_artifact(logic4_kernel,
      RegionKernelSpecialization::guarded_constant_inputs,
      "the guarded-constant entry remains admissible in the artifact matrix");
  require(RegionKernelTestAccess::native_body_identity(*dynamic_logic4)
              != RegionKernelTestAccess::native_body_identity(*constant_logic4),
      "dynamic and guarded-constant specializations own separate code bodies");
  auto constant_owner_executor = LlvmRegionKernelExecutor::try_create(
      logic4_kernel, options, "region-canonical-native-code-fixture-v1",
      RegionKernelSpecialization::guarded_constant_inputs);
  require(constant_owner_executor != nullptr,
      "an external-owner guarded-constant entry compiles");
  const auto constant_owner_identity = std::string {
      RegionKernelTestAccess::native_code_identity(*constant_owner_executor) };
  const auto constant_owner_mapping_identity
      = std::string { constant_owner_executor->cache_identity() };
  const auto constant_owner_objects
      = region_cache_files(cache_directory.path());
  auto moved_constant_owner_kernel = logic4_kernel;
  moved_constant_owner_kernel.constant_inputs.front().owner += 1000U;
  auto moved_constant_owner = LlvmRegionKernelExecutor::try_create(
      moved_constant_owner_kernel, options,
      "region-canonical-native-code-fixture-v1",
      RegionKernelSpecialization::guarded_constant_inputs);
  require(moved_constant_owner != nullptr
          && moved_constant_owner->cache_identity()
              != constant_owner_mapping_identity
          && RegionKernelTestAccess::native_code_identity(*moved_constant_owner)
              == constant_owner_identity
          && RegionKernelTestAccess::native_body_identity(*moved_constant_owner)
              == RegionKernelTestAccess::native_body_identity(
                  *constant_owner_executor)
          && region_cache_files(cache_directory.path()) == constant_owner_objects,
      "external constant-owner IDs stay exact in the wrapper but canonical in native code");
  compare_region_executor_with_reference(logic4_kernel, logic4_image,
      *constant_owner_executor,
      "the constant-owner wrapper retains its original input binding");
  compare_region_executor_with_reference(moved_constant_owner_kernel,
      logic4_image, *moved_constant_owner,
      "the remapped external owner uses the same code with its own wrapper");
  logic4_kernel.constant_inputs.front().value.set(0U,
      logic4_kernel.constant_inputs.front().value.get(0U) == Logic4::zero
          ? Logic4::one : Logic4::zero);
  auto changed_constant = require_new_artifact(logic4_kernel,
      RegionKernelSpecialization::guarded_constant_inputs,
      "a changed guarded constant gets a distinct native artifact");
  require(RegionKernelTestAccess::native_body_identity(*changed_constant)
              != RegionKernelTestAccess::native_body_identity(*constant_logic4),
      "different guarded constant values use distinct compiled bodies");

  auto typed_kernel = logic9_kernel;
  typed_kernel.program.register_value_kinds.assign(
      typed_kernel.program.register_count, ValueKind::logic4);
  for (auto& input : typed_kernel.inputs) {
    input.value_kind = ValueKind::logic4;
  }
  for (auto& output : typed_kernel.outputs) {
    output.value_kind = ValueKind::logic4;
  }
  for (auto& member : typed_kernel.members) {
    for (auto& binding : member.register_bindings) {
      binding.value_kind = ValueKind::logic4;
    }
  }
  const auto logic9_code_identity = logic9_native_identity;
  const auto before_type_change = region_cache_files(cache_directory.path());
  auto typed_executor = LlvmRegionKernelExecutor::try_create(
      typed_kernel, options, "region-canonical-native-code-fixture-v1");
  require(typed_executor != nullptr
          && RegionKernelTestAccess::native_code_identity(*typed_executor)
              != logic9_code_identity,
      "a Logic9-to-Logic4 layout change gets a distinct native code identity");
  require(RegionKernelTestAccess::native_body_identity(*typed_executor)
              != RegionKernelTestAccess::native_body_identity(*logic9),
      "different value-kind layouts use distinct compiled bodies");
  require(region_cache_files(cache_directory.path()).size()
              > before_type_change.size(),
      "a Logic9-to-Logic4 layout change gets a distinct native artifact");
}

[[nodiscard]] RegionConeForwardingKernel
make_xor_reduction_forwarding_kernel(const std::uint32_t width)
{
  std::vector<RegionSignalDescriptor> signals {
      { width }, { 1U }, { 1U },
  };
  signals[2U].observations = RegionObservation::current;

  Process consumer;
  consumer.id = 0U;
  consumer.name = "xor_forwarding_child_before_parent";
  consumer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
  consumer.register_count = 2U;
  consumer.register_value_kinds = {
      ValueKind::logic4, ValueKind::logic4,
  };
  consumer.static_sensitivity = {
      Sensitivity { 1U, EdgeKind::any, 0U, 1U },
  };
  consumer.driver_regions = { { 2U, 0U, 1U, true } };
  consumer.operations = {
      ReadSignal { 0U, 1U },
      UnaryNot { 1U, 0U },
      DebugPoint { DebugPointKind::statement,
          SourceLocation { "xor-forwarding-chain.sv", 4U, 3U },
          "child.scope" },
      WriteUpdate { 2U, 1U,
          SignalUpdateDomain::systemverilog_active },
      WaitSensitivity { },
      Jump { 0U },
  };

  Process producer;
  producer.id = 1U;
  producer.name = "xor_forwarding_root_after_child";
  producer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
  producer.register_count = 2U;
  producer.register_value_kinds = {
      ValueKind::logic4, ValueKind::logic4,
  };
  producer.static_sensitivity = {
      Sensitivity { 0U, EdgeKind::any, 0U, width },
  };
  producer.driver_regions = { { 1U, 0U, 1U, true } };
  producer.operations = {
      ReadSignal { 0U, 0U },
      Reduction { ReductionOperator::bit_xor, 1U, 0U },
      DebugPoint { DebugPointKind::statement,
          SourceLocation { "xor-forwarding-chain.sv", 9U, 3U },
          "root.scope" },
      WriteUpdate { 1U, 1U,
          SignalUpdateDomain::systemverilog_active },
      WaitSensitivity { },
      Jump { 0U },
  };

  const std::array<const Process*, 2U> process_bindings {
      &consumer, &producer,
  };
  const auto graph = RegionGraph::build(process_bindings, signals);
  const auto& components = graph.certificate_inventory().components;
  const auto component = std::ranges::find_if(components,
      [](const RegionComponentCertificate& candidate) {
        return candidate.members == std::vector<ProcessId> { 0U, 1U }
            && candidate.structural_internal_signal_candidates
                == std::vector<SignalId> { 1U };
      });
  require(component != components.end(),
      "XOR producer and reversed-ID child form one certified component");
  const auto component_index = static_cast<std::size_t>(
      component - components.begin());
  const auto compute = graph.build_compute_program(
      component_index, process_bindings);
  require(compute.has_value() && compute->forwarding_kernel.has_value(),
      "the XOR producer graph builds a topologically ordered forwarding kernel");
  auto forwarding = *compute->forwarding_kernel;
  require(forwarding.execution_kernel.member_execution_order
          == std::vector<std::size_t> { 1U, 0U },
      "the XOR producer precedes its lower-ProcessId consumer");
  return forwarding;
}

void check_xor_reduction_forwarding_knownness(
    const fsim::compiler::JitOptimizationLevel optimization)
{
  using fsim::compiler::llvm_detail::RegionKernelBodySelection;
  for (const auto width : { 1U, 8U, 32U, 64U }) {
    auto forwarding = make_xor_reduction_forwarding_kernel(width);
    auto& kernel = forwarding.execution_kernel;
    fsim::compiler::LlvmJitOptions options;
    options.optimization = optimization;
    options.cache_directory.clear();
    auto executor = fsim::compiler::LlvmRegionKernelExecutor::try_create(
        kernel, options,
        "region-forwarding-xor-reduction-width-" + std::to_string(width));
    require(executor != nullptr,
        "LLVM accepts a supported Logic4 XOR reduction in a forwarding body");

    const auto parent_output = std::ranges::find(kernel.outputs, 1U,
        &RegionConeOutputBinding::signal);
    const auto child_output = std::ranges::find(kernel.outputs, 2U,
        &RegionConeOutputBinding::signal);
    const auto boundary_binding = std::ranges::find(kernel.inputs, 0U,
        &RegionConeKernelInput::signal);
    require(parent_output != kernel.outputs.end()
            && child_output != kernel.outputs.end()
            && boundary_binding != kernel.inputs.end(),
        "the XOR forwarding kernel binds its input and both outputs");

    const auto compare = [&](const RegionKernelActivationImage& image,
                             const RegionKernelBodySelection expected_body,
                             const std::string_view message) {
      const auto expected = evaluate_region_activation_kernel_reference(
          kernel, image);
      require(executor->execute(image), message);
      require(fsim::compiler::llvm_detail::RegionKernelTestAccess::
                  last_body_selection(*executor) == expected_body,
          "XOR forwarding chooses known or four-state code from current input");
      const auto actual = executor->activation_registers();
      compare_defined_member_registers(kernel, image, expected, actual);
      for (const auto& output : kernel.outputs) {
        require(actual[output.value_register]
                == expected[output.value_register],
            "XOR forwarding outputs match the exact four-state reference");
      }
    };

    auto known_image = make_all_ready_forwarding_image(kernel);
    auto boundary_input = std::ranges::find(known_image.register_inputs,
        boundary_binding->value_register,
        &RegionKernelRegisterInput::register_id);
    require(boundary_input != known_image.register_inputs.end(),
        "the XOR forwarding image contains its packed boundary input");
    compare(known_image, RegionKernelBodySelection::known_logic4,
        "all-zero 1/8/32/64-bit XOR reductions select the known body");
    require(executor->activation_registers()[parent_output->value_register]
                == PackedLogic4 { 1U, Logic4::zero }
            && executor->activation_registers()[child_output->value_register]
                == PackedLogic4 { 1U, Logic4::one },
        "a zero reduction forwards zero and its child complement");

    auto highest_bit_image = known_image;
    auto highest_bit_input = std::ranges::find(
        highest_bit_image.register_inputs, boundary_binding->value_register,
        &RegionKernelRegisterInput::register_id);
    require(highest_bit_input != highest_bit_image.register_inputs.end(),
        "the highest-bit parity image retains its boundary input");
    highest_bit_input->value.set(width - 1U, Logic4::one);
    compare(highest_bit_image, RegionKernelBodySelection::known_logic4,
        "a known one in the highest operand bit selects the known body");
    require(executor->activation_registers()[parent_output->value_register]
                == PackedLogic4 { 1U, Logic4::one }
            && executor->activation_registers()[child_output->value_register]
                == PackedLogic4 { 1U, Logic4::zero },
        "the highest operand bit contributes odd parity at every width");

    PackedLogic4 known_value(width, Logic4::zero);
    bool expected_parity { };
    for (std::uint32_t bit = 0U; bit < width; ++bit) {
      if (bit % 3U == 0U) {
        known_value.set(bit, Logic4::one);
        expected_parity = !expected_parity;
      }
    }
    boundary_input->value = known_value;
    compare(known_image, RegionKernelBodySelection::known_logic4,
        "known 1/8/32/64-bit XOR reductions select the known body at O0/O2");
    require(executor->activation_registers()[parent_output->value_register]
                == PackedLogic4 { 1U,
                    expected_parity ? Logic4::one : Logic4::zero }
            && executor->activation_registers()[child_output->value_register]
                == PackedLogic4 { 1U,
                    expected_parity ? Logic4::zero : Logic4::one },
        "known reduction parity forwards through the producer snapshot");

    auto unknown_x = known_image;
    auto x_input = std::ranges::find(unknown_x.register_inputs,
        boundary_binding->value_register,
        &RegionKernelRegisterInput::register_id);
    require(x_input != unknown_x.register_inputs.end(),
        "the X fallback image retains its boundary input");
    x_input->value.set(width - 1U, Logic4::x);
    compare(unknown_x, RegionKernelBodySelection::four_state,
        "an X input declines XOR knownness specialization at every width");
    require(executor->activation_registers()[parent_output->value_register]
                == PackedLogic4 { 1U, Logic4::x }
            && executor->activation_registers()[child_output->value_register]
                == PackedLogic4 { 1U, Logic4::x },
        "X in the reduction operand produces the reference X result");

    auto unknown_z = known_image;
    auto z_input = std::ranges::find(unknown_z.register_inputs,
        boundary_binding->value_register,
        &RegionKernelRegisterInput::register_id);
    require(z_input != unknown_z.register_inputs.end(),
        "the Z fallback image retains its boundary input");
    z_input->value.set(width - 1U, Logic4::z);
    compare(unknown_z, RegionKernelBodySelection::four_state,
        "a Z input also declines XOR knownness specialization");
    require(executor->activation_registers()[parent_output->value_register]
                == PackedLogic4 { 1U, Logic4::x }
            && executor->activation_registers()[child_output->value_register]
                == PackedLogic4 { 1U, Logic4::x },
        "Z in the reduction operand produces the reference X result");

    compare(known_image, RegionKernelBodySelection::known_logic4,
        "known XOR specialization returns after X/Z fallback activations");
  }
}

} // namespace

int main()
{
  try {
    test_region_activation_guard();
    const auto o0_identity = check_logic4_native_matches_reference(
        fsim::compiler::JitOptimizationLevel::o0);
    const auto o2_identity = check_logic4_native_matches_reference(
        fsim::compiler::JitOptimizationLevel::o2);
    require(o0_identity == o2_identity,
        "native optimization level does not change canonical mapping identity");
    const auto vhdl_o0_identity
        = check_vhdl_projected_native_matches_reference(
            fsim::compiler::JitOptimizationLevel::o0);
    const auto vhdl_o2_identity
        = check_vhdl_projected_native_matches_reference(
            fsim::compiler::JitOptimizationLevel::o2);
    require(vhdl_o0_identity == vhdl_o2_identity,
        "VHDL projected mapping identity is stable across optimization levels");
    const auto generic_update_o0_identity
        = check_generic_update_slice_native_matches_reference(
            fsim::compiler::JitOptimizationLevel::o0);
    const auto generic_update_o2_identity
        = check_generic_update_slice_native_matches_reference(
            fsim::compiler::JitOptimizationLevel::o2);
    require(generic_update_o0_identity == generic_update_o2_identity,
        "generic Update/UpdateSlice mapping identity is stable across optimization levels");
    const auto conditional_select_o0_identity
        = check_generic_conditional_select_native_matches_reference(
            fsim::compiler::JitOptimizationLevel::o0);
    const auto conditional_select_o2_identity
        = check_generic_conditional_select_native_matches_reference(
            fsim::compiler::JitOptimizationLevel::o2);
    require(conditional_select_o0_identity
            == conditional_select_o2_identity,
        "generic ConditionalSelect mapping identity is stable across O0 and O2");
    const auto generic_logic9_update_o0_identity
        = check_generic_logic9_update_native_matches_reference(
            fsim::compiler::JitOptimizationLevel::o0);
    const auto generic_logic9_update_o2_identity
        = check_generic_logic9_update_native_matches_reference(
            fsim::compiler::JitOptimizationLevel::o2);
    require(generic_logic9_update_o0_identity
            == generic_logic9_update_o2_identity,
        "generic Logic9 mapping identity is stable across O0 and O2");
    const auto builder_o0_identity = check_builder_kernel_matches_reference(
        fsim::compiler::JitOptimizationLevel::o0);
    const auto builder_o2_identity = check_builder_kernel_matches_reference(
        fsim::compiler::JitOptimizationLevel::o2);
    require(builder_o0_identity == builder_o2_identity,
        "builder-produced mapping identity is stable across optimization levels");
    check_sparse_direct_ready_physical_ids(
        fsim::compiler::JitOptimizationLevel::o0);
    check_sparse_direct_ready_physical_ids(
        fsim::compiler::JitOptimizationLevel::o2);
    check_known_logic4_raw_ir_shape();
    check_known_logic4_dispatch(
        fsim::compiler::JitOptimizationLevel::o0);
    check_known_logic4_dispatch(
        fsim::compiler::JitOptimizationLevel::o2);
    check_guarded_constant_input_specialization(
        fsim::compiler::JitOptimizationLevel::o0);
    check_guarded_constant_input_specialization(
        fsim::compiler::JitOptimizationLevel::o2);
    constexpr std::array wide_widths {
        std::uint32_t { 65U }, std::uint32_t { 129U },
        std::uint32_t { 256U }, std::uint32_t { 1024U },
    };
    for (const auto width : wide_widths) {
      const auto o0_wide_identity =
          check_wide_builder_kernel_matches_reference(
              fsim::compiler::JitOptimizationLevel::o0, width);
      const auto o2_wide_identity =
          check_wide_builder_kernel_matches_reference(
              fsim::compiler::JitOptimizationLevel::o2, width);
      require(o0_wide_identity == o2_wide_identity,
          "wide builder mapping identity is stable across optimization levels");
    }
    check_builder_kernel_steady_state_no_allocations(
        fsim::compiler::JitOptimizationLevel::o0);
    check_builder_kernel_steady_state_no_allocations(
        fsim::compiler::JitOptimizationLevel::o2);
    constexpr std::array logic9_widths {
        std::uint32_t { 9U }, std::uint32_t { 65U },
        std::uint32_t { 129U }, std::uint32_t { 256U },
        std::uint32_t { 1024U },
    };
    for (const auto width : logic9_widths) {
      check_guarded_logic9_constant_input_specialization(
          fsim::compiler::JitOptimizationLevel::o0, width);
      check_guarded_logic9_constant_input_specialization(
          fsim::compiler::JitOptimizationLevel::o2, width);
      const auto o0_logic9_identity
          = check_logic9_builder_kernel_matches_reference(
              fsim::compiler::JitOptimizationLevel::o0, width);
      const auto o2_logic9_identity
          = check_logic9_builder_kernel_matches_reference(
              fsim::compiler::JitOptimizationLevel::o2, width);
      require(o0_logic9_identity == o2_logic9_identity,
          "Logic9 builder identity is stable across O0 and O2");
    }
    check_logic9_native_cache_cold_warm_reload(
        fsim::compiler::JitOptimizationLevel::o0, 1024U);
    check_logic9_native_cache_cold_warm_reload(
        fsim::compiler::JitOptimizationLevel::o2, 1024U);
    for (const auto width : { 1U, 4U, 65U, 129U, 256U, 1024U }) {
      for (const auto kind : { ValueKind::logic4, ValueKind::logic9 }) {
        check_wide_internal_activation(
            fsim::compiler::JitOptimizationLevel::o0, width, kind);
        check_wide_internal_activation(
            fsim::compiler::JitOptimizationLevel::o2, width, kind);
      }
    }
    check_random_dag_matrix();
    check_forwarding_permutation_and_all_ready(
        fsim::compiler::JitOptimizationLevel::o0);
    check_forwarding_permutation_and_all_ready(
        fsim::compiler::JitOptimizationLevel::o2);
    check_xor_reduction_forwarding_knownness(
        fsim::compiler::JitOptimizationLevel::o0);
    check_xor_reduction_forwarding_knownness(
        fsim::compiler::JitOptimizationLevel::o2);
    check_native_body_registry_single_flight();
    check_concurrent_native_body_creation(
        fsim::compiler::JitOptimizationLevel::o0);
    check_prepared_template_signal_alias_identity(
        fsim::compiler::JitOptimizationLevel::o0);
    check_prepared_template_signal_alias_identity(
        fsim::compiler::JitOptimizationLevel::o2);
    check_canonical_native_code_identity(
        fsim::compiler::JitOptimizationLevel::o0, true);
    check_canonical_native_code_identity(
        fsim::compiler::JitOptimizationLevel::o2, false);
    check_prepared_output_successor_mask_capacity();
    check_direct_ready_successor_mask_capacity();
    check_add_unsigned_native_matches_reference(
        fsim::compiler::JitOptimizationLevel::o0);
    check_add_unsigned_native_matches_reference(
        fsim::compiler::JitOptimizationLevel::o2);
    check_unsupported_region_programs_still_decline();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "region LLVM kernel test failure: " << error.what() << '\n';
    return 1;
  }
}
