// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

namespace fsim::app::application_detail {

SystemCProcessExecutor::SystemCProcessExecutor(
    std::shared_ptr<systemc::HierarchyRegistry> hierarchy,
    const std::uint64_t process)
    : hierarchy_(std::move(hierarchy)), process_(process) {
  if (!hierarchy_) {
    throw std::invalid_argument{
        "SystemC process executor requires a hierarchy registry"};
  }
}

runtime::simir::ProcessResumeResult SystemCProcessExecutor::resume(
    runtime::simir::ProcessExecutionContext& context,
    runtime::simir::InstructionIndex) {
  const auto suspension = hierarchy_->invoke_process(process_, context);
  runtime::simir::ProcessResumeResult result{0, 1};
  switch (suspension.kind) {
  case systemc::MethodSuspendKind::halt:
    result.external.kind = runtime::simir::ExternalSuspendKind::halt;
    break;
  case systemc::MethodSuspendKind::static_sensitivity:
    result.external.kind =
        runtime::simir::ExternalSuspendKind::wait_sensitivity;
    break;
  case systemc::MethodSuspendKind::wait_for:
    result.external.kind =
        suspension.delay_ticks == 0
            ? runtime::simir::ExternalSuspendKind::yield
            : runtime::simir::ExternalSuspendKind::wait_for;
    result.external.delay = suspension.delay_ticks;
    break;
  case systemc::MethodSuspendKind::wait_event:
    result.external.kind = runtime::simir::ExternalSuspendKind::wait_on;
    result.external.wait_all = suspension.wait_all;
    result.external.sensitivity.reserve(
        suspension.event_signals.size());
    for (const auto event : suspension.event_signals) {
      result.external.sensitivity.push_back(
          {event, runtime::simir::EdgeKind::any});
    }
    break;
  }
  return result;
}

void SystemCProcessExecutor::update_channel(
    const std::uint64_t channel,
    runtime::simir::ProcessExecutionContext& context) {
  hierarchy_->invoke_primitive_channel(channel, context);
}

std::string_view report_severity_name(
    const runtime::simir::AssertionSeverity severity) noexcept {
  switch (severity) {
  case runtime::simir::AssertionSeverity::note: return "note";
  case runtime::simir::AssertionSeverity::warning: return "warning";
  case runtime::simir::AssertionSeverity::error: return "error";
  case runtime::simir::AssertionSeverity::failure: return "failure";
  }
  return "error";
}

std::uint64_t entropy_seed() {
  std::random_device source;
  const auto high = static_cast<std::uint64_t>(source());
  const auto low = static_cast<std::uint64_t>(source());
  return (high << 32U) ^ low;
}

#if defined(FSIM_HAS_LLVM)

runtime::simir::FileHandle LlvmProcessExecutor::checked_file_handle(
    const std::uint64_t aval,
    const std::uint64_t bval) {
  if (bval != 0
      || aval
          > std::numeric_limits<
                runtime::simir::FileHandle>::max()) {
    throw std::runtime_error{
        "file handle must be a known 32-bit integral value"};
  }
  return static_cast<runtime::simir::FileHandle>(aval);
}

void LlvmProcessExecutor::capture_file_failure(
    CallbackState& state,
    const std::uint32_t process,
    const std::uint32_t instruction) noexcept {
  if (state.failure) {
    return;
  }
  try {
    throw;
  } catch (const runtime::simir::InterpreterError&) {
    state.failure = std::current_exception();
  } catch (const std::exception& error) {
    state.failure = std::make_exception_ptr(
        runtime::simir::InterpreterError{
            process, instruction, error.what()});
  } catch (...) {
    state.failure = std::current_exception();
  }
}

const runtime::simir::Operation&
LlvmProcessExecutor::callback_operation(
    const CallbackState& state,
    const std::uint32_t process,
    const std::uint32_t instruction) {
  if (state.process == nullptr
      || process != state.process->id
      || instruction >= state.process->operations.size()) {
    throw compiler::LlvmJitError{
        "compiled file callback metadata is out of range"};
  }
  return state.process->operations[instruction];
}

std::uint32_t LlvmProcessExecutor::file_open(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    std::uint32_t* result) noexcept {
  auto& state = *static_cast<CallbackState*>(context);
  if (state.failure) {
    return 1;
  }
  try {
    if (result == nullptr) {
      throw compiler::LlvmJitError{
          "compiled file-open result pointer is null"};
    }
    const auto* operation =
        std::get_if<runtime::simir::FileOpen>(
            &callback_operation(state, process, instruction));
    if (operation == nullptr) {
      throw compiler::LlvmJitError{
          "compiled file-open callback has the wrong operation"};
    }
    *result = state.context->open_file(
        state.executor->string_registers_.at(operation->path),
        state.executor->string_registers_.at(operation->mode));
    return 0;
  } catch (...) {
    capture_file_failure(state, process, instruction);
    return 1;
  }
}

std::uint32_t LlvmProcessExecutor::file_close(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint64_t handle_aval,
    const std::uint64_t handle_bval) noexcept {
  auto& state = *static_cast<CallbackState*>(context);
  if (state.failure) {
    return 1;
  }
  try {
    if (!std::holds_alternative<runtime::simir::FileClose>(
            callback_operation(state, process, instruction))) {
      throw compiler::LlvmJitError{
          "compiled file-close callback has the wrong operation"};
    }
    state.context->close_file(
        checked_file_handle(handle_aval, handle_bval));
    return 0;
  } catch (...) {
    capture_file_failure(state, process, instruction);
    return 1;
  }
}

std::uint32_t LlvmProcessExecutor::file_write(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint64_t handle_aval,
    const std::uint64_t handle_bval) noexcept {
  auto& state = *static_cast<CallbackState*>(context);
  if (state.failure) {
    return 1;
  }
  try {
    const auto handle =
        checked_file_handle(handle_aval, handle_bval);
    const auto& operation =
        callback_operation(state, process, instruction);
    if (const auto* literal =
            std::get_if<runtime::simir::FileWriteLiteral>(
                &operation)) {
      state.context->write_file(
          handle, literal->text, literal->newline);
      return 0;
    }
    if (const auto* formatted =
            std::get_if<runtime::simir::FileWriteFormatted>(
                &operation)) {
      const auto value = runtime::PackedLogic4::from_aval_bval(
          formatted->width,
          state.executor->register_aval_.at(formatted->source),
          state.executor->register_bval_.at(formatted->source));
      state.context->write_file_formatted(
          handle,
          formatted->prefix,
          formatted->suffix,
          formatted->format,
          value,
          formatted->signed_decimal,
          formatted->suppress_leading_zero,
          formatted->minimum_width,
          formatted->left_justify,
          formatted->zero_pad);
      if (formatted->newline) {
        state.context->write_file(handle, {}, true);
      }
      return 0;
    }
    if (const auto* string =
            std::get_if<runtime::simir::FileWriteString>(
                &operation)) {
      state.context->write_file(
          handle,
          string->prefix
              + state.executor->string_registers_.at(string->source)
              + string->suffix,
          string->newline);
      return 0;
    }
    throw compiler::LlvmJitError{
        "compiled file-write callback has the wrong operation"};
  } catch (...) {
    capture_file_failure(state, process, instruction);
    return 1;
  }
}

std::uint32_t LlvmProcessExecutor::file_read_line(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint64_t handle_aval,
    const std::uint64_t handle_bval,
    std::uint32_t* result) noexcept {
  auto& state = *static_cast<CallbackState*>(context);
  if (state.failure) {
    return 1;
  }
  try {
    if (result == nullptr) {
      throw compiler::LlvmJitError{
          "compiled file-read result pointer is null"};
    }
    const auto* operation =
        std::get_if<runtime::simir::FileReadLine>(
            &callback_operation(state, process, instruction));
    if (operation == nullptr) {
      throw compiler::LlvmJitError{
          "compiled file-read callback has the wrong operation"};
    }
    auto line = state.context->read_file_line(
        checked_file_handle(handle_aval, handle_bval), *result);
    state.executor->write_string_register(
        operation->target, line);
    return 0;
  } catch (...) {
    capture_file_failure(state, process, instruction);
    return 1;
  }
}

std::uint32_t LlvmProcessExecutor::file_end_of_file(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint64_t handle_aval,
    const std::uint64_t handle_bval,
    std::uint32_t* result) noexcept {
  auto& state = *static_cast<CallbackState*>(context);
  if (state.failure) {
    return 1;
  }
  try {
    if (result == nullptr
        || !std::holds_alternative<
            runtime::simir::FileEndOfFile>(
            callback_operation(state, process, instruction))) {
      throw compiler::LlvmJitError{
          "compiled file-eof callback metadata is invalid"};
    }
    *result = state.context->file_end_of_file(
        checked_file_handle(handle_aval, handle_bval))
        ? 1U : 0U;
    return 0;
  } catch (...) {
    capture_file_failure(state, process, instruction);
    return 1;
  }
}

std::uint32_t LlvmProcessExecutor::file_error(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint64_t handle_aval,
    const std::uint64_t handle_bval,
    std::uint32_t* result) noexcept {
  auto& state = *static_cast<CallbackState*>(context);
  if (state.failure) {
    return 1;
  }
  try {
    if (result == nullptr) {
      throw compiler::LlvmJitError{
          "compiled file-error result pointer is null"};
    }
    const auto* operation =
        std::get_if<runtime::simir::FileErrorStatus>(
            &callback_operation(state, process, instruction));
    if (operation == nullptr) {
      throw compiler::LlvmJitError{
          "compiled file-error callback has the wrong operation"};
    }
    bool has_error{};
    auto message = state.context->file_error(
        checked_file_handle(handle_aval, handle_bval),
        has_error);
    state.executor->write_string_register(
        operation->target, message);
    *result = has_error ? 1U : 0U;
    return 0;
  } catch (...) {
    capture_file_failure(state, process, instruction);
    return 1;
  }
}

compiler::JitOptimizationLevel jit_optimization(
    const project::Optimization optimization) noexcept {
  return optimization == project::Optimization::o0
      ? compiler::JitOptimizationLevel::o0
      : compiler::JitOptimizationLevel::o2;
}

#endif

}  // namespace fsim::app::application_detail
