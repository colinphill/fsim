// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

namespace fsim::app::application_detail {

#if defined(FSIM_HAS_LLVM)

LlvmProcessExecutor::LlvmProcessExecutor(
    compiler::LlvmJit& jit,
    const compiler::JitProcessHandle handle,
    const runtime::simir::Process& process,
    std::span<const std::uint32_t> signal_widths,
    std::span<const runtime::simir::ValueKind> signal_value_kinds)
    : jit_(jit),
      handle_(handle),
      process_(process),
      signal_widths_(signal_widths),
      signal_value_kinds_(signal_value_kinds),
      storage_(std::make_shared<FrameStorage>()),
      register_aval_(storage_->register_aval),
      register_bval_(storage_->register_bval),
      register_logic9_plane2_(storage_->register_logic9_plane2),
      register_logic9_plane3_(storage_->register_logic9_plane3),
      register_initialized_(storage_->register_initialized),
      string_registers_(storage_->string_registers),
      container_registers_(storage_->container_registers) {
  const auto layout = jit_.frame_layout(handle_);
  register_aval_.resize(layout.register_count);
  register_bval_.resize(layout.register_count);
  if (layout.uses_logic9) {
    register_logic9_plane2_.resize(layout.register_count);
    register_logic9_plane3_.resize(layout.register_count);
  }
  register_initialized_.resize(layout.register_count);
  string_registers_.resize(layout.string_register_count);
  container_registers_.reserve(process.container_register_types.size());
  for (const auto& type : process.container_register_types) {
    container_registers_.push_back(
        runtime::simir::default_container_value(type));
  }
  jit_.initialize_frame(
      handle_, frame_, register_aval_, register_bval_,
      register_initialized_, register_logic9_plane2_,
      register_logic9_plane3_);
}

LlvmProcessExecutor::LlvmProcessExecutor(
    compiler::LlvmJit& jit,
    const compiler::JitProcessHandle handle,
    const runtime::simir::Process& process,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const runtime::simir::ValueKind> signal_value_kinds,
    std::shared_ptr<FrameStorage> storage,
    const fsim_jit_frame_v1& parent_frame,
    const runtime::simir::InstructionIndex start_instruction)
    : jit_(jit),
      handle_(handle),
      process_(process),
      signal_widths_(signal_widths),
      signal_value_kinds_(signal_value_kinds),
      storage_(std::move(storage)),
      frame_(parent_frame),
      register_aval_(storage_->register_aval),
      register_bval_(storage_->register_bval),
      register_logic9_plane2_(storage_->register_logic9_plane2),
      register_logic9_plane3_(storage_->register_logic9_plane3),
      register_initialized_(storage_->register_initialized),
      string_registers_(storage_->string_registers),
      container_registers_(storage_->container_registers) {
  frame_.program_counter = start_instruction;
  frame_.state = FSIM_JIT_FRAME_STATE_READY;
  frame_.last_instruction = FSIM_JIT_INVALID_INSTRUCTION;
}

[[nodiscard]] std::unique_ptr<runtime::simir::ProcessExecutor>
LlvmProcessExecutor::fork_clone(
    const runtime::simir::InstructionIndex start_instruction) {
  return std::unique_ptr<runtime::simir::ProcessExecutor>{
      new LlvmProcessExecutor{
          jit_, handle_, process_, signal_widths_, signal_value_kinds_,
          storage_, frame_, start_instruction}};
}

#endif

}  // namespace fsim::app::application_detail
