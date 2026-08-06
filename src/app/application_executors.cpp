// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "fsim/runtime/systemverilog_string.hpp"

namespace fsim::app::application_detail {

#if defined(FSIM_HAS_LLVM)

[[nodiscard]] runtime::simir::ProcessResumeResult LlvmProcessExecutor::resume(
    runtime::simir::ProcessExecutionContext& context,
    const runtime::simir::InstructionIndex start_instruction)  {
    if (frame_.program_counter != start_instruction) {
      throw compiler::LlvmJitError(
          "compiled process frame PC disagrees with the simulation kernel");
    }

    CallbackState callback_state{
        this,
        &context,
        &process_,
        signal_widths_,
        signal_value_kinds_,
        {}};
    fsim_jit_runtime_v1 runtime{};
    runtime.abi_version = FSIM_JIT_RUNTIME_ABI_VERSION_V1;
    runtime.struct_size = sizeof(runtime);
    runtime.context = &callback_state;
    runtime.read_signal = read_signal;
    runtime.write_signal = write_signal;
    runtime.assert_failed = assert_failed;
    runtime.write_update = write_update;
    runtime.write_after = write_after;
    runtime.flags =
        context.execution_points_enabled()
            ? FSIM_JIT_RUNTIME_FLAG_DEBUG_POINTS
            : 0;
    runtime.write_signal_slice = write_signal_slice;
    runtime.write_update_slice = write_update_slice;
    runtime.write_after_slice = write_after_slice;
    runtime.signal_event = signal_event;
    runtime.signal_last_value = signal_last_value;
    runtime.signal_last_event = signal_last_event;
    runtime.signal_active = signal_active;
    runtime.signal_last_active = signal_last_active;
    runtime.signal_driving = signal_driving;
    runtime.signal_driving_value = signal_driving_value;
    runtime.signal_driving_value_logic9 = signal_driving_value_logic9;
    runtime.read_simulation_time = read_simulation_time;
    runtime.vital_timing_check = vital_timing_check;
    runtime.vital_delay = vital_delay;
    runtime.write_output = write_output;
    runtime.schedule_output = schedule_output;
    runtime.write_report = write_report;
    runtime.write_formatted = write_formatted;
    runtime.write_time = write_time;
    runtime.install_monitor = install_monitor;
    runtime.control_monitor = control_monitor;
    runtime.random_value = random_value;
    runtime.write_inertial = write_inertial;
    runtime.write_inertial_slice = write_inertial_slice;
    runtime.write_projected = write_projected;
    runtime.write_projected_slice = write_projected_slice;
    runtime.write_projected_waveform = write_projected_waveform;
    runtime.write_projected_waveform_slice =
        write_projected_waveform_slice;
    runtime.read_signal_logic9 = read_signal_logic9;
    runtime.write_signal_logic9 = write_signal_logic9;
    runtime.write_update_logic9 = write_update_logic9;
    runtime.write_after_logic9 = write_after_logic9;
    runtime.write_signal_slice_logic9 =
        write_signal_slice_logic9;
    runtime.write_update_slice_logic9 =
        write_update_slice_logic9;
    runtime.write_after_slice_logic9 =
        write_after_slice_logic9;
    runtime.signal_last_value_logic9 =
        signal_last_value_logic9;
    runtime.write_inertial_logic9 = write_inertial_logic9;
    runtime.write_inertial_slice_logic9 =
        write_inertial_slice_logic9;
    runtime.write_projected_logic9 = write_projected_logic9;
    runtime.write_projected_slice_logic9 =
        write_projected_slice_logic9;
    runtime.write_projected_waveform_logic9 =
        write_projected_waveform_logic9;
    runtime.write_projected_waveform_slice_logic9 =
        write_projected_waveform_slice_logic9;
    runtime.write_formatted_logic9 = write_formatted_logic9;
    runtime.force_signal_slice = force_signal_slice;
    runtime.force_signal_slice_logic9 = force_signal_slice_logic9;
    runtime.release_signal_slice = release_signal_slice;
    runtime.load_string = load_string;
    runtime.copy_string = copy_string;
    runtime.read_string_object = read_string_object;
    runtime.write_string_object = write_string_object;
    runtime.concatenate_strings = concatenate_strings;
    runtime.compare_strings = compare_strings;
    runtime.string_length = string_length;
    runtime.string_index = string_index;
    runtime.string_replace_code_point = string_replace_code_point;
    runtime.write_string_output = write_string_output;
    runtime.file_open = file_open;
    runtime.file_close = file_close;
    runtime.file_write = file_write;
    runtime.file_read_line = file_read_line;
    runtime.file_end_of_file = file_end_of_file;
    runtime.file_error = file_error;
    runtime.container_operation = container_operation;

    fsim_jit_resume_result_v1 result{};
    result.abi_version = FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1;
    result.struct_size = sizeof(result);
    const auto status = [&]() -> compiler::JitResumeStatus {
      try {
        return jit_.resume(handle_, runtime, frame_, result);
      } catch (const compiler::LlvmJitGeneratedRuntimeError& error) {
        if (callback_state.failure) {
          std::rethrow_exception(callback_state.failure);
        }
        switch (error.reason()) {
          case compiler::JitGeneratedRuntimeErrorReason::
              unknown_branch_condition:
            throw runtime::simir::InterpreterError(
                process_.id,
                error.instruction(),
                "branch condition is unknown or high impedance");
          case compiler::JitGeneratedRuntimeErrorReason::
              integer_operand_unknown:
            throw runtime::simir::InterpreterError(
                process_.id,
                error.instruction(),
                "VHDL integer operand contains an unknown or "
                "high-impedance value");
          case compiler::JitGeneratedRuntimeErrorReason::
              integer_overflow:
            throw runtime::simir::InterpreterError(
                process_.id,
                error.instruction(),
                "VHDL integer arithmetic overflow");
          case compiler::JitGeneratedRuntimeErrorReason::
              integer_division_by_zero:
            throw runtime::simir::InterpreterError(
                process_.id,
                error.instruction(),
                "VHDL integer division by zero");
          case compiler::JitGeneratedRuntimeErrorReason::
              integer_negative_exponent:
            throw runtime::simir::InterpreterError(
                process_.id,
                error.instruction(),
                "VHDL integer exponent must be nonnegative");
          case compiler::JitGeneratedRuntimeErrorReason::
              integer_subtype_range:
            throw runtime::simir::InterpreterError(
                process_.id,
                error.instruction(),
                "VHDL integer subtype range check failed");
          case compiler::JitGeneratedRuntimeErrorReason::
              dynamic_index_unknown:
            throw runtime::simir::InterpreterError(
                process_.id,
                error.instruction(),
                "dynamic packed index contains an unknown or "
                "high-impedance value");
          case compiler::JitGeneratedRuntimeErrorReason::
              dynamic_index_range:
            throw runtime::simir::InterpreterError(
                process_.id,
                error.instruction(),
                "dynamic packed index is outside the declared range");
          case compiler::JitGeneratedRuntimeErrorReason::
              call_stack_unknown:
            throw runtime::simir::InterpreterError(
                process_.id,
                error.instruction(),
                "call-stack pointer or return target is unknown");
          case compiler::JitGeneratedRuntimeErrorReason::
              call_stack_overflow:
            throw runtime::simir::InterpreterError(
                process_.id,
                error.instruction(),
                "call-stack capacity is exhausted");
          case compiler::JitGeneratedRuntimeErrorReason::
              call_stack_underflow:
            throw runtime::simir::InterpreterError(
                process_.id,
                error.instruction(),
                "call-stack underflow");
          case compiler::JitGeneratedRuntimeErrorReason::
              call_stack_target:
            throw runtime::simir::InterpreterError(
                process_.id,
                error.instruction(),
                "call-stack return target is invalid");
          case compiler::JitGeneratedRuntimeErrorReason::
              string_callback_failure:
            throw runtime::simir::InterpreterError(
                process_.id,
                error.instruction(),
                "mutable string runtime callback failed");
          case compiler::JitGeneratedRuntimeErrorReason::
              file_callback_failure:
            throw runtime::simir::InterpreterError(
                process_.id,
                error.instruction(),
                "text file runtime callback failed");
          case compiler::JitGeneratedRuntimeErrorReason::
              container_callback_failure:
            throw runtime::simir::InterpreterError(
                process_.id,
                error.instruction(),
                "bounded container runtime callback failed");
        }
        throw;
      } catch (...) {
        // A callback failure is the first language/runtime failure observed by
        // generated code and must not be masked by a later adapter status.
        if (callback_state.failure) {
          std::rethrow_exception(callback_state.failure);
        }
        throw;
      }
    }();
    if (callback_state.failure) {
      std::rethrow_exception(callback_state.failure);
    }
    if (result.instruction >= process_.operations.size()) {
      throw compiler::LlvmJitError(
          "compiled process returned an invalid boundary instruction");
    }

    switch (status) {
      case compiler::JitResumeStatus::completed:
        require_boundary<runtime::simir::Halt>(
            result.instruction, "completion");
        break;
      case compiler::JitResumeStatus::assertion_failed: {
        const auto* assertion = fsim::runtime::simir::operation_get_if<runtime::simir::Assert>(
            &process_.operations[result.instruction]);
        if (assertion != nullptr) {
          throw runtime::simir::AssertionError(
              process_.id,
              result.instruction,
              assertion->message.empty()
                  ? "assertion failed"
                  : assertion->message,
              assertion->severity,
              assertion->source);
        }
        const auto* report = fsim::runtime::simir::operation_get_if<runtime::simir::Report>(
            &process_.operations[result.instruction]);
        if (report != nullptr
            && report->severity
                == runtime::simir::AssertionSeverity::failure) {
          throw runtime::simir::AssertionError(
              process_.id,
              result.instruction,
              report->message.empty()
                  ? "report failure"
                  : report->message,
              report->severity,
              report->source,
              true);
        }
        throw compiler::LlvmJitError(
            "compiled process reported an assertion at an incompatible "
            "instruction");
      }
      case compiler::JitResumeStatus::wait_for: {
        const auto* wait = fsim::runtime::simir::operation_get_if<runtime::simir::WaitFor>(
            &process_.operations[result.instruction]);
        if (wait == nullptr) {
          throw compiler::LlvmJitError(
              "compiled process reported WaitFor at a non-wait instruction");
        }
        const auto expected_delay = wait->source ? 0 : wait->delay;
        if (result.delay != expected_delay) {
          throw compiler::LlvmJitError(
              "compiled process returned a WaitFor delay that disagrees "
              "with SimIR");
        }
        break;
      }
      case compiler::JitResumeStatus::wait_on: {
        const auto* wait = fsim::runtime::simir::operation_get_if<
            runtime::simir::WaitOn>(
            &process_.operations[result.instruction]);
        if (wait == nullptr) {
          throw compiler::LlvmJitError(
              "compiled process reported WaitOn at a non-wait instruction");
        }
        if (result.delay != wait->timeout.value_or(0)) {
          throw compiler::LlvmJitError(
              "compiled process returned a WaitOn timeout that disagrees "
              "with SimIR");
        }
        break;
      }
      case compiler::JitResumeStatus::wait_sensitivity:
        require_boundary<runtime::simir::WaitSensitivity>(
            result.instruction, "WaitSensitivity");
        break;
      case compiler::JitResumeStatus::wait_forever:
        require_boundary<runtime::simir::WaitForever>(
            result.instruction, "WaitForever");
        break;
      case compiler::JitResumeStatus::yielded:
        require_boundary<runtime::simir::Yield>(
            result.instruction, "yield");
        break;
      case compiler::JitResumeStatus::fork:
        require_boundary<runtime::simir::Fork>(
            result.instruction, "fork");
        break;
      case compiler::JitResumeStatus::fork_end:
        require_boundary<runtime::simir::ForkEnd>(
            result.instruction, "fork end");
        break;
      case compiler::JitResumeStatus::wait_fork:
        require_boundary<runtime::simir::WaitFork>(
            result.instruction, "wait fork");
        break;
      case compiler::JitResumeStatus::disable_fork:
        require_boundary<runtime::simir::DisableFork>(
            result.instruction, "disable fork");
        break;
      case compiler::JitResumeStatus::debug_point:
        require_boundary<runtime::simir::DebugPoint>(
            result.instruction, "debug point");
        break;
      case compiler::JitResumeStatus::paused:
        require_boundary<runtime::simir::Pause>(
            result.instruction, "pause");
        break;
      case compiler::JitResumeStatus::stopped:
        require_boundary<runtime::simir::Stop>(
            result.instruction, "stop");
        break;
      case compiler::JitResumeStatus::simir_boundary: {
        const auto& operation = process_.operations[result.instruction];
        const auto host_boundary =
            fsim::runtime::simir::operation_holds<
                runtime::simir::ClassAllocate>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ClassPropertyRead>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ClassPropertyWrite>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ClassMethodCall>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ClassStaticPropertyRead>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ClassStaticPropertyWrite>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ClassStaticMethodCall>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ProcessSelf>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ProcessStatusQuery>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ProcessCompleted>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ProcessAwait>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ProcessKill>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::MailboxCreate>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::MailboxPut>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::MailboxGet>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::MailboxNum>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::SemaphoreCreate>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::SemaphoreGet>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::SemaphorePut>(operation);
        if (!host_boundary) {
          throw compiler::LlvmJitError(
              "compiled process reported an unsupported SimIR boundary");
        }
        break;
      }
    }
    if (frame_.program_counter != result.instruction + 1U) {
      throw compiler::LlvmJitError(
          "compiled process returned a non-sequential boundary PC");
    }
    return {result.instruction, frame_.program_counter};
  }

[[nodiscard]] PackedLogic4 LlvmProcessExecutor::read_register(
    const runtime::simir::RegisterId id,
    const std::size_t width) const  {
    if (id >= register_aval_.size() || width > 64) {
      throw compiler::LlvmJitError{
          "compiled process debug-register request is out of range"};
    }
    if (width == 0) {
      return PackedLogic4{};
    }
    if (register_initialized_[id] == 0) {
      throw std::logic_error{
          "compiled process debug local has not been initialized"};
    }
    const auto kind =
        process_.register_value_kinds.empty()
            ? runtime::simir::ValueKind::logic4
            : process_.register_value_kinds[id];
    if (kind == runtime::simir::ValueKind::logic9) {
      return PackedLogic4::from_logic9_word(
          {
              width,
              {
                  register_aval_[id],
                  register_bval_[id],
                  register_logic9_plane2_[id],
                  register_logic9_plane3_[id]}});
    }
    return PackedLogic4::from_aval_bval(
        width, register_aval_[id], register_bval_[id]);
  }

void LlvmProcessExecutor::write_register(
    const runtime::simir::RegisterId id,
    const PackedLogic4& value)  {
    if (id >= register_aval_.size() || value.width() > 64) {
      throw compiler::LlvmJitError{
          "compiled process register write is out of range"};
    }
    if (value.width() == 0) {
      register_initialized_[id] = 1;
      return;
    }
    const auto kind =
        process_.register_value_kinds.empty()
            ? runtime::simir::ValueKind::logic4
            : process_.register_value_kinds[id];
    if (kind == runtime::simir::ValueKind::logic9) {
      const auto word = value.logic9_low_word();
      register_aval_[id] = word.planes[0];
      register_bval_[id] = word.planes[1];
      register_logic9_plane2_[id] = word.planes[2];
      register_logic9_plane3_[id] = word.planes[3];
    } else {
      const auto word = value.low_word();
      register_aval_[id] = word.aval;
      register_bval_[id] = word.bval;
    }
    register_initialized_[id] = 1;
  }

template <typename Boundary>
void LlvmProcessExecutor::require_boundary(
    const runtime::simir::InstructionIndex instruction,
    const std::string_view status) const  {
    if (!fsim::runtime::simir::operation_holds<Boundary>(
            process_.operations[instruction])) {
      throw compiler::LlvmJitError(
          "compiled process reported " + std::string{status}
          + " at the wrong SimIR instruction");
    }
  }

void LlvmProcessExecutor::capture_failure(CallbackState& state) noexcept  {
    if (!state.failure) {
      state.failure = std::current_exception();
    }
  }

std::uint32_t LlvmProcessExecutor::load_string(
    void* context,
    const std::uint32_t destination,
    const char* bytes,
    const std::uint64_t byte_count) noexcept {
  auto& state = *static_cast<CallbackState*>(context);
  if (state.failure) {
    return 1;
  }
  try {
    if (state.executor == nullptr
        || byte_count > runtime::simir::maximum_string_bytes
        || (byte_count != 0 && bytes == nullptr)) {
      throw std::logic_error{"invalid generated string-load callback"};
    }
    (void)runtime::systemverilog_string_length(
        std::string_view{bytes, static_cast<std::size_t>(byte_count)});
    state.executor->write_string_register(
        destination,
        std::string_view{bytes, static_cast<std::size_t>(byte_count)});
    return 0;
  } catch (...) {
    capture_failure(state);
    return 1;
  }
}

std::uint32_t LlvmProcessExecutor::copy_string(
    void* context,
    const std::uint32_t destination,
    const std::uint32_t source) noexcept {
  auto& state = *static_cast<CallbackState*>(context);
  if (state.failure) {
    return 1;
  }
  try {
    state.executor->write_string_register(
        destination,
        state.executor->read_string_register(source));
    return 0;
  } catch (...) {
    capture_failure(state);
    return 1;
  }
}

std::uint32_t LlvmProcessExecutor::read_string_object(
    void* context,
    const std::uint32_t destination,
    const std::uint32_t object) noexcept {
  auto& state = *static_cast<CallbackState*>(context);
  if (state.failure) {
    return 1;
  }
  try {
    state.executor->write_string_register(
        destination, state.context->read_string_object(object));
    return 0;
  } catch (...) {
    capture_failure(state);
    return 1;
  }
}

std::uint32_t LlvmProcessExecutor::write_string_object(
    void* context,
    const std::uint32_t object,
    const std::uint32_t source) noexcept {
  auto& state = *static_cast<CallbackState*>(context);
  if (state.failure) {
    return 1;
  }
  try {
    state.context->write_string_object(
        object, state.executor->read_string_register(source));
    return 0;
  } catch (...) {
    capture_failure(state);
    return 1;
  }
}

std::uint32_t LlvmProcessExecutor::concatenate_strings(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint32_t destination,
    const std::uint32_t* operands,
    const std::uint32_t operand_count) noexcept {
  auto& state = *static_cast<CallbackState*>(context);
  if (state.failure) {
    return 1;
  }
  try {
    if (state.process == nullptr || process != state.process->id
        || (operand_count != 0 && operands == nullptr)) {
      throw std::logic_error{
          "invalid generated string-concatenation callback"};
    }
    std::string result;
    for (std::uint32_t index = 0; index < operand_count; ++index) {
      const auto value =
          state.executor->read_string_register(operands[index]);
      if (value.size()
          > runtime::simir::maximum_string_bytes - result.size()) {
        throw runtime::simir::InterpreterError(
            state.process->id,
            instruction,
            "string concatenation exceeds 4096-byte limit");
      }
      result += value;
    }
    state.executor->write_string_register(destination, result);
    return 0;
  } catch (...) {
    capture_failure(state);
    return 1;
  }
}

std::uint32_t LlvmProcessExecutor::compare_strings(
    void* context,
    const std::uint32_t lhs,
    const std::uint32_t rhs,
    const std::uint32_t not_equal,
    std::uint32_t* result) noexcept {
  auto& state = *static_cast<CallbackState*>(context);
  if (state.failure) {
    return 1;
  }
  try {
    if (result == nullptr || not_equal > 1) {
      throw std::logic_error{"invalid generated string-compare callback"};
    }
    const bool equal = runtime::systemverilog_string_compare(
                           state.executor->read_string_register(lhs),
                           state.executor->read_string_register(rhs))
        == 0;
    *result = equal != (not_equal != 0) ? 1U : 0U;
    return 0;
  } catch (...) {
    capture_failure(state);
    return 1;
  }
}

std::uint32_t LlvmProcessExecutor::string_length(
    void* context,
    const std::uint32_t source,
    std::uint32_t* result) noexcept {
  auto& state = *static_cast<CallbackState*>(context);
  if (state.failure) {
    return 1;
  }
  try {
    if (result == nullptr) {
      throw std::logic_error{"invalid generated string-length callback"};
    }
    *result = static_cast<std::uint32_t>(
        runtime::systemverilog_string_length(
            state.executor->read_string_register(source)));
    return 0;
  } catch (...) {
    capture_failure(state);
    return 1;
  }
}

std::uint32_t LlvmProcessExecutor::string_index(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint32_t source,
    const std::uint64_t index_aval,
    const std::uint64_t index_bval,
    const std::uint32_t signed_index,
    std::uint32_t* result) noexcept {
  auto& state = *static_cast<CallbackState*>(context);
  if (state.failure) {
    return 1;
  }
  try {
    const auto& value = state.executor->string_registers_.at(source);
    if (state.process == nullptr || process != state.process->id
        || result == nullptr || signed_index > 1 || index_bval != 0) {
      throw runtime::simir::InterpreterError(
          state.process->id,
          instruction,
          "string index contains X or Z");
    }
    const auto raw = static_cast<std::uint32_t>(index_aval);
    const auto index = signed_index != 0
        ? static_cast<std::int64_t>(static_cast<std::int32_t>(raw))
        : static_cast<std::int64_t>(raw);
    const auto length = runtime::systemverilog_string_length(value);
    if (index < 0
        || static_cast<std::uint64_t>(index) >= length) {
      throw runtime::simir::InterpreterError(
          state.process->id,
          instruction,
          "string index is outside the current code-point range");
    }
    *result = runtime::systemverilog_string_at(
        value, static_cast<std::size_t>(index));
    return 0;
  } catch (...) {
    capture_failure(state);
    return 1;
  }
}

std::uint32_t LlvmProcessExecutor::string_replace_code_point(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint32_t target,
    const std::uint64_t index_aval,
    const std::uint64_t index_bval,
    const std::uint32_t signed_index,
    const std::uint64_t source_aval,
    const std::uint64_t source_bval) noexcept {
  auto& state = *static_cast<CallbackState*>(context);
  if (state.failure) {
    return 1;
  }
  try {
    auto& value = state.executor->string_registers_.at(target);
    if (state.process == nullptr || process != state.process->id
        || signed_index > 1 || index_bval != 0) {
      throw runtime::simir::InterpreterError(
          state.process->id,
          instruction,
          "string index contains X or Z");
    }
    const auto raw = static_cast<std::uint32_t>(index_aval);
    const auto selected = signed_index != 0
        ? static_cast<std::int64_t>(static_cast<std::int32_t>(raw))
        : static_cast<std::int64_t>(raw);
    const auto length = runtime::systemverilog_string_length(value);
    if (selected < 0
        || static_cast<std::uint64_t>(selected) >= length) {
      throw runtime::simir::InterpreterError(
          state.process->id,
          instruction,
          "string index is outside the current code-point range");
    }
    if (source_bval != 0) {
      throw runtime::simir::InterpreterError(
          state.process->id,
          instruction,
          "string replacement code point contains X or Z");
    }
    runtime::systemverilog_string_replace(
        value,
        static_cast<std::size_t>(selected),
        static_cast<std::uint32_t>(source_aval),
        runtime::simir::maximum_string_bytes);
    return 0;
  } catch (...) {
    capture_failure(state);
    return 1;
  }
}

std::uint32_t LlvmProcessExecutor::write_string_output(
    void* context,
    const std::uint32_t process,
    const std::uint32_t source,
    const char* prefix,
    const std::uint64_t prefix_size,
    const char* suffix,
    const std::uint64_t suffix_size,
    const std::uint32_t newline,
    const std::uint32_t postponed) noexcept {
  auto& state = *static_cast<CallbackState*>(context);
  if (state.failure) {
    return 1;
  }
  try {
    if (state.process == nullptr || process != state.process->id
        || (prefix_size != 0 && prefix == nullptr)
        || (suffix_size != 0 && suffix == nullptr)
        || newline > 1 || postponed > 1) {
      throw std::logic_error{"invalid generated string-output callback"};
    }
    std::string text{prefix, static_cast<std::size_t>(prefix_size)};
    text += state.executor->read_string_register(source);
    text.append(suffix, static_cast<std::size_t>(suffix_size));
    if (postponed != 0) {
      state.context->postpone_display(text, newline != 0);
    } else {
      state.context->display(text, newline != 0);
    }
    return 0;
  } catch (...) {
    capture_failure(state);
    return 1;
  }
}

std::uint64_t LlvmProcessExecutor::read_signal(
    void* context,
    const std::uint32_t signal,
    std::uint64_t* bval) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      if (bval != nullptr) {
        *bval = 0;
      }
      return 0;
    }
    try {
      if (bval == nullptr || state.context == nullptr
          || signal >= state.signal_widths.size()) {
        throw std::logic_error("invalid generated read-signal callback");
      }
      const auto value = state.context->read_signal_word(signal);
      const auto expected_width = state.signal_widths[signal];
      if (value.width != expected_width
          || value.width == 0
          || value.width > 64) {
        throw std::logic_error(
            "generated read-signal callback observed an invalid width");
      }
      *bval = value.bval;
      return value.aval;
    } catch (...) {
      capture_failure(state);
      if (bval != nullptr) {
        *bval = 0;
      }
      return 0;
    }
  }

void LlvmProcessExecutor::read_signal_logic9(
    void* context,
    const std::uint32_t signal,
    fsim_jit_logic9_word_v1* result) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    clear_logic9_word(result);
    if (state.failure) {
      return;
    }
    try {
      require_logic9_signal(state, signal, result);
      const auto value =
          state.context->read_signal_logic9_word(signal);
      if (value.width != state.signal_widths[signal]) {
        throw std::logic_error(
            "generated Logic9 read observed an invalid width");
      }
      result->planes[0] = value.planes[0];
      result->planes[1] = value.planes[1];
      result->planes[2] = value.planes[2];
      result->planes[3] = value.planes[3];
    } catch (...) {
      capture_failure(state);
      clear_logic9_word(result);
    }
  }

void LlvmProcessExecutor::write_signal(
    void* context,
    const std::uint32_t signal,
    const std::uint64_t aval,
    const std::uint64_t bval) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      const auto value =
          checked_write_word(state, signal, aval, bval);
      state.context->write_blocking_word(
          signal, value);
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::write_signal_logic9(
    void* context,
    const std::uint32_t signal,
    const fsim_jit_logic9_word_v1* value) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      state.context->write_blocking(
          signal,
          checked_logic9_value(state, signal, value));
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::write_update(
    void* context,
    const std::uint32_t signal,
    const std::uint64_t aval,
    const std::uint64_t bval) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      const auto value =
          checked_write_word(state, signal, aval, bval);
      state.context->write_update_word(
          signal, value);
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::write_update_logic9(
    void* context,
    const std::uint32_t signal,
    const fsim_jit_logic9_word_v1* value) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      state.context->write_update(
          signal,
          checked_logic9_value(state, signal, value));
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::write_after(
    void* context,
    const std::uint32_t signal,
    const std::uint64_t aval,
    const std::uint64_t bval,
    const std::uint64_t delay) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      const auto value =
          checked_write_word(state, signal, aval, bval);
      state.context->write_after_word(
          signal, value, delay);
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::write_after_logic9(
    void* context,
    const std::uint32_t signal,
    const fsim_jit_logic9_word_v1* value,
    const std::uint64_t delay) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      state.context->write_after(
          signal,
          checked_logic9_value(state, signal, value),
          delay);
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::write_inertial(
    void* context,
    const std::uint32_t signal,
    const std::uint64_t aval,
    const std::uint64_t bval,
    const std::uint64_t rise_delay,
    const std::uint64_t fall_delay,
    const std::uint64_t turnoff_delay) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      const auto value =
          checked_write_word(state, signal, aval, bval);
      state.context->write_inertial_word(
          signal,
          value,
          {rise_delay, fall_delay, turnoff_delay});
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::write_inertial_logic9(
    void* context,
    const std::uint32_t signal,
    const fsim_jit_logic9_word_v1* value,
    const std::uint64_t rise_delay,
    const std::uint64_t fall_delay,
    const std::uint64_t turnoff_delay) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      state.context->write_inertial(
          signal,
          checked_logic9_value(state, signal, value),
          {rise_delay, fall_delay, turnoff_delay});
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::write_projected(
    void* context,
    const std::uint32_t signal,
    const std::uint64_t aval,
    const std::uint64_t bval,
    const std::uint64_t delay,
    const std::uint64_t rejection,
    const std::uint32_t mode) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      const auto value =
          checked_write_word(state, signal, aval, bval);
      state.context->write_projected_word(
          signal,
          value,
          delay,
          rejection,
          projected_delay_mode(mode));
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::write_projected_logic9(
    void* context,
    const std::uint32_t signal,
    const fsim_jit_logic9_word_v1* value,
    const std::uint64_t delay,
    const std::uint64_t rejection,
    const std::uint32_t mode) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      state.context->write_projected(
          signal,
          checked_logic9_value(state, signal, value),
          delay,
          rejection,
          projected_delay_mode(mode));
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::write_signal_slice(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const std::uint64_t aval,
    const std::uint64_t bval) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      const auto value = checked_slice_word(
          state, signal, offset, width, aval, bval);
      state.context->write_blocking_slice_word(
          signal, value, offset);
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::write_signal_slice_logic9(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const fsim_jit_logic9_word_v1* value) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      state.context->write_blocking_slice(
          signal,
          checked_logic9_slice(
              state, signal, offset, width, value),
          offset);
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::write_update_slice(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const std::uint64_t aval,
    const std::uint64_t bval) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      const auto value = checked_slice_word(
          state, signal, offset, width, aval, bval);
      state.context->write_update_slice_word(
          signal, value, offset);
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::write_update_slice_logic9(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const fsim_jit_logic9_word_v1* value) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      state.context->write_update_slice(
          signal,
          checked_logic9_slice(
              state, signal, offset, width, value),
          offset);
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::write_after_slice(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const std::uint64_t aval,
    const std::uint64_t bval,
    const std::uint64_t delay) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      const auto value = checked_slice_word(
          state, signal, offset, width, aval, bval);
      state.context->write_after_slice_word(
          signal, value, offset, delay);
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::write_after_slice_logic9(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const fsim_jit_logic9_word_v1* value,
    const std::uint64_t delay) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      state.context->write_after_slice(
          signal,
          checked_logic9_slice(
              state, signal, offset, width, value),
          offset,
          delay);
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::write_inertial_slice(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const std::uint64_t aval,
    const std::uint64_t bval,
    const std::uint64_t rise_delay,
    const std::uint64_t fall_delay,
    const std::uint64_t turnoff_delay) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      const auto value = checked_slice_word(
          state, signal, offset, width, aval, bval);
      state.context->write_inertial_slice_word(
          signal,
          value,
          offset,
          {rise_delay, fall_delay, turnoff_delay});
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::write_inertial_slice_logic9(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const fsim_jit_logic9_word_v1* value,
    const std::uint64_t rise_delay,
    const std::uint64_t fall_delay,
    const std::uint64_t turnoff_delay) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      state.context->write_inertial_slice(
          signal,
          checked_logic9_slice(
              state, signal, offset, width, value),
          offset,
          {rise_delay, fall_delay, turnoff_delay});
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::write_projected_slice(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const std::uint64_t aval,
    const std::uint64_t bval,
    const std::uint64_t delay,
    const std::uint64_t rejection,
    const std::uint32_t mode) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      const auto value = checked_slice_word(
          state, signal, offset, width, aval, bval);
      state.context->write_projected_slice_word(
          signal,
          value,
          offset,
          delay,
          rejection,
          projected_delay_mode(mode));
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::write_projected_slice_logic9(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const fsim_jit_logic9_word_v1* value,
    const std::uint64_t delay,
    const std::uint64_t rejection,
    const std::uint32_t mode) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      state.context->write_projected_slice(
          signal,
          checked_logic9_slice(
              state, signal, offset, width, value),
          offset,
          delay,
          rejection,
          projected_delay_mode(mode));
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::write_projected_waveform(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t width,
    const fsim_jit_projected_element_v1* elements,
    const std::uint32_t count,
    const std::uint64_t rejection,
    const std::uint32_t mode) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      if (elements == nullptr || count == 0
          || signal >= state.signal_widths.size()
          || width != state.signal_widths[signal]) {
        throw std::logic_error(
            "invalid generated projected-waveform callback");
      }
      std::vector<runtime::simir::ProjectedWaveformValue> values;
      values.reserve(count);
      for (std::uint32_t index = 0; index < count; ++index) {
        const auto word = checked_write_word(
            state,
            signal,
            elements[index].aval,
            elements[index].bval);
        values.push_back({
            PackedLogic4::from_aval_bval(
                word.width, word.aval, word.bval),
            elements[index].delay});
      }
      state.context->write_projected_waveform(
          signal,
          std::move(values),
          rejection,
          projected_delay_mode(mode));
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::write_projected_waveform_logic9(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t width,
    const fsim_jit_logic9_projected_element_v1* elements,
    const std::uint32_t count,
    const std::uint64_t rejection,
    const std::uint32_t mode) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      if (elements == nullptr || count == 0
          || signal >= state.signal_widths.size()
          || width != state.signal_widths[signal]) {
        throw std::logic_error(
            "invalid generated Logic9 projected-waveform callback");
      }
      std::vector<runtime::simir::ProjectedWaveformValue> values;
      values.reserve(count);
      for (std::uint32_t index = 0; index < count; ++index) {
        values.push_back({
            checked_logic9_value(
                state, signal, &elements[index].value),
            elements[index].delay});
      }
      state.context->write_projected_waveform(
          signal,
          std::move(values),
          rejection,
          projected_delay_mode(mode));
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::write_projected_waveform_slice(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const fsim_jit_projected_element_v1* elements,
    const std::uint32_t count,
    const std::uint64_t rejection,
    const std::uint32_t mode) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      if (elements == nullptr || count == 0) {
        throw std::logic_error(
            "invalid generated projected-slice-waveform callback");
      }
      std::vector<runtime::simir::ProjectedWaveformValue> values;
      values.reserve(count);
      for (std::uint32_t index = 0; index < count; ++index) {
        const auto word = checked_slice_word(
            state,
            signal,
            offset,
            width,
            elements[index].aval,
            elements[index].bval);
        values.push_back({
            PackedLogic4::from_aval_bval(
                word.width, word.aval, word.bval),
            elements[index].delay});
      }
      state.context->write_projected_waveform_slice(
          signal,
          std::move(values),
          offset,
          rejection,
          projected_delay_mode(mode));
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::write_projected_waveform_slice_logic9(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const fsim_jit_logic9_projected_element_v1* elements,
    const std::uint32_t count,
    const std::uint64_t rejection,
    const std::uint32_t mode) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      if (elements == nullptr || count == 0) {
        throw std::logic_error(
            "invalid generated Logic9 projected-slice-waveform "
            "callback");
      }
      std::vector<runtime::simir::ProjectedWaveformValue> values;
      values.reserve(count);
      for (std::uint32_t index = 0; index < count; ++index) {
        values.push_back({
            checked_logic9_slice(
                state,
                signal,
                offset,
                width,
                &elements[index].value),
            elements[index].delay});
      }
      state.context->write_projected_waveform_slice(
          signal,
          std::move(values),
          offset,
          rejection,
          projected_delay_mode(mode));
    } catch (...) {
      capture_failure(state);
    }
  }

[[nodiscard]] runtime::simir::ProjectedDelayMode
LlvmProcessExecutor::projected_delay_mode(const std::uint32_t mode)  {
    if (mode == FSIM_JIT_PROJECTED_TRANSPORT) {
      return runtime::simir::ProjectedDelayMode::transport;
    }
    if (mode == FSIM_JIT_PROJECTED_INERTIAL) {
      return runtime::simir::ProjectedDelayMode::inertial;
    }
    throw compiler::LlvmJitError(
        "generated process requested an invalid projected delay mode");
  }

[[nodiscard]] runtime::Logic4Word LlvmProcessExecutor::checked_write_word(
    const CallbackState& state,
    const std::uint32_t signal,
    const std::uint64_t aval,
    const std::uint64_t bval)  {
    if (state.context == nullptr
        || signal >= state.signal_widths.size()) {
      throw std::logic_error("invalid generated write-signal callback");
    }
    const auto width = state.signal_widths[signal];
    if (width == 0 || width > 64) {
      throw std::logic_error(
          "generated write-signal callback received an invalid width");
    }
    return {width, aval, bval};
  }

void LlvmProcessExecutor::clear_logic9_word(
    fsim_jit_logic9_word_v1* value) noexcept  {
    if (value == nullptr) {
      return;
    }
    value->planes[0] = 0;
    value->planes[1] = 0;
    value->planes[2] = 0;
    value->planes[3] = 0;
  }

void LlvmProcessExecutor::require_logic9_signal(
    const CallbackState& state,
    const std::uint32_t signal,
    const fsim_jit_logic9_word_v1* value)  {
    if (value == nullptr
        || state.context == nullptr
        || signal >= state.signal_widths.size()
        || signal >= state.signal_value_kinds.size()
        || state.signal_value_kinds[signal]
            != runtime::simir::ValueKind::logic9
        || state.signal_widths[signal] == 0
        || state.signal_widths[signal] > 64) {
      throw std::logic_error(
          "invalid generated Logic9 signal callback");
    }
  }

[[nodiscard]] PackedLogic4 LlvmProcessExecutor::checked_logic9_value(
    const CallbackState& state,
    const std::uint32_t signal,
    const fsim_jit_logic9_word_v1* value)  {
    require_logic9_signal(state, signal, value);
    return PackedLogic4::from_logic9_word(
        {
            state.signal_widths[signal],
            {
                value->planes[0],
                value->planes[1],
                value->planes[2],
                value->planes[3]}});
  }

[[nodiscard]] PackedLogic4 LlvmProcessExecutor::checked_logic9_slice(
    const CallbackState& state,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const fsim_jit_logic9_word_v1* value)  {
    require_logic9_signal(state, signal, value);
    const auto target_width = state.signal_widths[signal];
    if (width == 0 || width > 64
        || offset > target_width
        || width > target_width - offset) {
      throw std::logic_error(
          "invalid generated Logic9 partial-write callback");
    }
    return PackedLogic4::from_logic9_word(
        {
            width,
            {
                value->planes[0],
                value->planes[1],
                value->planes[2],
                value->planes[3]}});
  }

[[nodiscard]] runtime::Logic4Word LlvmProcessExecutor::checked_slice_word(
    const CallbackState& state,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const std::uint64_t aval,
    const std::uint64_t bval)  {
    if (state.context == nullptr
        || signal >= state.signal_widths.size()
        || width == 0 || width > 64) {
      throw std::logic_error(
          "invalid generated partial-write callback");
    }
    const auto target_width = state.signal_widths[signal];
    if (offset > target_width
        || width > target_width - offset) {
      throw std::logic_error(
          "generated partial-write range is outside its target");
    }
    return {width, aval, bval};
  }

void LlvmProcessExecutor::assert_failed(
    void* context,
    std::uint32_t,
    std::uint32_t,
    const char*,
    std::uint64_t) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    // The generated status and the immutable SimIR assertion carry all data
    // needed after the C ABI returns. No C++ allocation or exception is
    // permitted in this thunk.
  }

std::uint32_t LlvmProcessExecutor::signal_event(
    void* context,
    const std::uint32_t signal) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure || state.context == nullptr
        || signal >= state.signal_widths.size()) {
      return 0;
    }
    try {
      return state.context->signal_event(signal) ? 1U : 0U;
    } catch (...) {
      capture_failure(state);
      return 0;
    }
  }

std::uint64_t LlvmProcessExecutor::signal_last_value(
    void* context,
    const std::uint32_t signal,
    std::uint64_t* bval) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      if (bval != nullptr) {
        *bval = 0;
      }
      return 0;
    }
    try {
      if (bval == nullptr || state.context == nullptr
          || signal >= state.signal_widths.size()) {
        throw std::logic_error(
            "invalid generated signal-last-value callback");
      }
      const auto value =
          state.context->signal_last_value_word(signal);
      if (value.width != state.signal_widths[signal]
          || value.width == 0 || value.width > 64) {
        throw std::logic_error(
            "generated signal-last-value callback observed an invalid width");
      }
      *bval = value.bval;
      return value.aval;
    } catch (...) {
      capture_failure(state);
      if (bval != nullptr) {
        *bval = 0;
      }
      return 0;
    }
  }

void LlvmProcessExecutor::signal_last_value_logic9(
    void* context,
    const std::uint32_t signal,
    fsim_jit_logic9_word_v1* result) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    clear_logic9_word(result);
    if (state.failure) {
      return;
    }
    try {
      require_logic9_signal(state, signal, result);
      const auto value =
          state.context->signal_last_value_logic9_word(signal);
      if (value.width != state.signal_widths[signal]) {
        throw std::logic_error(
            "generated Logic9 last-value read observed an invalid width");
      }
      result->planes[0] = value.planes[0];
      result->planes[1] = value.planes[1];
      result->planes[2] = value.planes[2];
      result->planes[3] = value.planes[3];
    } catch (...) {
      capture_failure(state);
      clear_logic9_word(result);
    }
  }

std::uint64_t LlvmProcessExecutor::signal_last_event(
    void* context,
    const std::uint32_t signal) noexcept {
  auto& state = *static_cast<CallbackState*>(context);
  if (state.failure || state.context == nullptr
      || signal >= state.signal_widths.size()) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  try {
    return state.context->signal_last_event(signal);
  } catch (...) {
    capture_failure(state);
    return std::numeric_limits<std::uint64_t>::max();
  }
}

std::uint64_t LlvmProcessExecutor::read_simulation_time(
    void* context) noexcept {
  auto& state = *static_cast<CallbackState*>(context);
  if (state.failure || state.context == nullptr) {
    return 0;
  }
  return state.context->current_time();
}

std::uint32_t LlvmProcessExecutor::signal_active(
    void* context,
    const std::uint32_t signal) noexcept {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure || state.context == nullptr
        || signal >= state.signal_widths.size()) {
      return 0;
    }
    try {
      return state.context->signal_active(signal) ? 1U : 0U;
    } catch (...) {
      capture_failure(state);
      return 0;
    }
  }

void LlvmProcessExecutor::write_output(
    void* context,
    const std::uint32_t,
    const char* text,
    const std::uint64_t text_size,
    const std::uint32_t newline) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      if (state.context == nullptr
          || (text == nullptr && text_size != 0)
          || newline > 1
          || text_size
              > static_cast<std::uint64_t>(
                  std::numeric_limits<std::size_t>::max())) {
        throw std::logic_error(
            "invalid generated language-output callback");
      }
      state.context->display(
          std::string_view{
              text == nullptr ? "" : text,
              static_cast<std::size_t>(text_size)},
          newline != 0);
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::schedule_output(
    void* context,
    const std::uint32_t,
    const char* text,
    const std::uint64_t text_size,
    const std::uint32_t newline) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      if (state.context == nullptr
          || (text == nullptr && text_size != 0)
          || newline > 1
          || text_size
              > static_cast<std::uint64_t>(
                  std::numeric_limits<std::size_t>::max())) {
        throw std::logic_error{
            "invalid generated postponed-output callback"};
      }
      state.context->postpone_display(
          std::string_view{
              text == nullptr ? "" : text,
              static_cast<std::size_t>(text_size)},
          newline != 0);
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::write_formatted(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint32_t width,
    const std::uint64_t aval,
    const std::uint64_t bval) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      if (state.context == nullptr
          || state.process == nullptr
          || state.process->id != process
          || instruction >= state.process->operations.size()
          || width == 0
          || width > 64) {
        throw std::logic_error{
            "invalid generated formatted-output callback"};
      }
      const auto* operation =
          fsim::runtime::simir::operation_get_if<runtime::simir::FormatDisplay>(
              &state.process->operations[instruction]);
      if (operation == nullptr) {
        throw std::logic_error{
            "generated formatted-output callback references a "
            "different operation"};
      }
      const auto value = PackedLogic4::from_aval_bval(
          width, aval, bval);
      state.context->display_formatted(
          operation->prefix,
          operation->suffix,
          operation->format,
          value,
          operation->newline,
          operation->postponed,
          operation->signed_decimal,
          operation->suppress_leading_zero,
          operation->minimum_width,
          operation->left_justify,
          operation->zero_pad,
          operation->scalar_kind);
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::write_formatted_logic9(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint32_t width,
    const fsim_jit_logic9_word_v1* value) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      if (state.context == nullptr
          || state.process == nullptr
          || state.process->id != process
          || instruction >= state.process->operations.size()
          || width == 0 || width > 64
          || value == nullptr) {
        throw std::logic_error{
            "invalid generated Logic9 formatted-output callback"};
      }
      const auto* operation =
          fsim::runtime::simir::operation_get_if<runtime::simir::FormatDisplay>(
              &state.process->operations[instruction]);
      if (operation == nullptr) {
        throw std::logic_error{
            "generated Logic9 formatted-output callback references a "
            "different operation"};
      }
      const auto packed = PackedLogic4::from_logic9_word(
          {
              width,
              {
                  value->planes[0],
                  value->planes[1],
                  value->planes[2],
                  value->planes[3]}});
      state.context->display_formatted(
          operation->prefix,
          operation->suffix,
          operation->format,
          packed,
          operation->newline,
          operation->postponed,
          operation->signed_decimal,
          operation->suppress_leading_zero,
          operation->minimum_width,
          operation->left_justify,
          operation->zero_pad,
          operation->scalar_kind);
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::write_time(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      if (state.context == nullptr
          || state.process == nullptr
          || state.process->id != process
          || instruction >= state.process->operations.size()) {
        throw std::logic_error{
            "invalid generated time-output callback"};
      }
      const auto* operation =
          fsim::runtime::simir::operation_get_if<runtime::simir::TimeDisplay>(
              &state.process->operations[instruction]);
      if (operation == nullptr) {
        throw std::logic_error{
            "generated time-output callback references a "
            "different operation"};
      }
      state.context->display_time(
          operation->prefix,
          operation->suffix,
          operation->newline,
          operation->postponed,
          operation->minimum_width,
          operation->left_justify,
          operation->zero_pad);
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::install_monitor(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      if (state.context == nullptr
          || state.process == nullptr
          || state.process->id != process
          || instruction >= state.process->operations.size()) {
        throw std::logic_error{
            "invalid generated monitor-install callback"};
      }
      const auto* operation =
          fsim::runtime::simir::operation_get_if<runtime::simir::MonitorInstall>(
              &state.process->operations[instruction]);
      if (operation == nullptr) {
        throw std::logic_error{
            "generated monitor-install callback references a "
            "different operation"};
      }
      state.context->install_monitor(*operation);
    } catch (...) {
      capture_failure(state);
    }
  }

void LlvmProcessExecutor::control_monitor(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
      return;
    }
    try {
      if (state.context == nullptr
          || state.process == nullptr
          || state.process->id != process
          || instruction >= state.process->operations.size()) {
        throw std::logic_error{
            "invalid generated monitor-control callback"};
      }
      const auto* operation =
          fsim::runtime::simir::operation_get_if<runtime::simir::MonitorControl>(
              &state.process->operations[instruction]);
      if (operation == nullptr) {
        throw std::logic_error{
            "generated monitor-control callback references a "
            "different operation"};
      }
      state.context->set_monitor_enabled(operation->enabled);
    } catch (...) {
      capture_failure(state);
    }
  }

std::uint64_t LlvmProcessExecutor::random_value(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint64_t maximum_aval,
    const std::uint64_t maximum_bval,
    const std::uint64_t minimum_aval,
    const std::uint64_t minimum_bval,
    std::uint64_t* result_bval) noexcept  {
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure || result_bval == nullptr) {
      return 0;
    }
    try {
      if (state.context == nullptr
          || state.process == nullptr
          || state.process->id != process
          || instruction >= state.process->operations.size()) {
        throw std::logic_error{
            "invalid generated random-value callback"};
      }
      const auto* operation =
          fsim::runtime::simir::operation_get_if<runtime::simir::RandomValue>(
              &state.process->operations[instruction]);
      if (operation == nullptr) {
        throw std::logic_error{
            "generated random-value callback references a "
            "different operation"};
      }
      const auto maximum =
          operation->maximum
              ? std::optional<PackedLogic4>{
                    PackedLogic4::from_aval_bval(
                        32, maximum_aval, maximum_bval)}
              : std::nullopt;
      const auto minimum =
          operation->minimum
              ? std::optional<PackedLogic4>{
                    PackedLogic4::from_aval_bval(
                        32, minimum_aval, minimum_bval)}
              : std::nullopt;
      const auto result =
          state.context->random_value(
              operation->kind, maximum, minimum);
      const auto encoded = result.low_word();
      *result_bval = encoded.bval;
      return encoded.aval;
    } catch (...) {
      capture_failure(state);
      *result_bval = std::numeric_limits<std::uint64_t>::max();
      return std::numeric_limits<std::uint64_t>::max();
    }
  }

#endif

} // namespace fsim::app::application_detail
