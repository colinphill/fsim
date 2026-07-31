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

[[nodiscard]] std::string LlvmProcessExecutor::read_string_register(
    const runtime::simir::StringRegisterId id) const {
  if (id >= string_registers_.size()) {
    throw compiler::LlvmJitError{
        "compiled process string-register request is out of range"};
  }
  return string_registers_[id];
}

void LlvmProcessExecutor::write_string_register(
    const runtime::simir::StringRegisterId id,
    const std::string_view value) {
  if (id >= string_registers_.size()
      || value.size() > runtime::simir::maximum_string_bytes) {
    throw compiler::LlvmJitError{
        "compiled process string-register write is out of range"};
  }
  string_registers_[id] = value;
}

runtime::simir::ContainerValue
LlvmProcessExecutor::read_container_register(
    const runtime::simir::ContainerRegisterId id) const {
  return container_registers_.at(id);
}

void LlvmProcessExecutor::write_container_register(
    const runtime::simir::ContainerRegisterId id,
    const runtime::simir::ContainerValue& value) {
  if (id >= container_registers_.size()
      || container_registers_[id].type != value.type) {
    throw compiler::LlvmJitError{
        "compiled process container-register write is out of range"};
  }
  container_registers_[id] = value;
}

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

std::uint32_t LlvmProcessExecutor::container_operation(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint64_t input0_aval,
    const std::uint64_t input0_bval,
    const std::uint64_t input1_aval,
    const std::uint64_t input1_bval,
    std::uint64_t* result_aval,
    std::uint64_t* result_bval) noexcept {
  auto& state = *static_cast<CallbackState*>(context);
  if (state.failure) {
    return 1;
  }
  try {
    if (state.executor == nullptr || state.context == nullptr
        || result_aval == nullptr || result_bval == nullptr) {
      throw compiler::LlvmJitError{
          "invalid generated container callback"};
    }
    *result_aval = 0;
    *result_bval = 0;
    auto& registers = state.executor->container_registers_;
    const auto& operation =
        callback_operation(state, process, instruction);
    const auto index =
        [&](const std::uint64_t aval,
            const std::uint64_t bval,
            const bool signed_index,
            const std::string_view role) {
          if (bval != 0
              || (signed_index
                  && static_cast<std::int32_t>(
                         static_cast<std::uint32_t>(aval))
                      < 0)) {
            throw runtime::simir::InterpreterError{
                process, instruction,
                std::string{role}
                    + (bval != 0
                           ? " must be a known integral value"
                           : " cannot be negative")};
          }
          return static_cast<std::size_t>(aval);
        };
    const auto fixed_offset =
        [&](const runtime::simir::ContainerValue& target,
            const std::uint64_t aval,
            const std::uint64_t bval) {
          if (bval != 0) {
            throw runtime::simir::InterpreterError{
                process, instruction,
                "static-array index must be a known 32-bit integral value"};
          }
          const auto sought = static_cast<std::int32_t>(
              static_cast<std::uint32_t>(aval));
          const auto low = std::min(
              target.type.index_left, target.type.index_right);
          const auto high = std::max(
              target.type.index_left, target.type.index_right);
          if (sought < low || sought > high) {
            throw runtime::simir::InterpreterError{
                process, instruction,
                "static-array index is out of range"};
          }
          return static_cast<std::size_t>(
              target.type.index_left >= target.type.index_right
                  ? static_cast<std::int64_t>(
                        target.type.index_left) - sought
                  : static_cast<std::int64_t>(sought)
                        - target.type.index_left);
        };
    const auto require_same =
        [&](const runtime::simir::ContainerValue& left,
            const runtime::simir::ContainerValue& right) {
          if (left.type != right.type) {
            throw runtime::simir::InterpreterError{
                process, instruction,
                "container value type mismatch"};
          }
        };
    const auto element =
        [&](const runtime::simir::ContainerValue& target,
            const std::uint64_t aval,
            const std::uint64_t bval) {
          if (target.type.two_state && bval != 0) {
            throw runtime::simir::InterpreterError{
                process, instruction,
                "container element write type mismatch"};
          }
          return PackedLogic4::from_aval_bval(
              target.type.element_width, aval, bval);
        };
    const auto key =
        [&](const runtime::simir::ContainerValue& target,
            const std::uint64_t aval,
            const std::uint64_t bval) {
          if (!target.type.associative) {
            throw runtime::simir::InterpreterError{
                process, instruction,
                "associative-array method used on another container"};
          }
          if (bval != 0) {
            throw runtime::simir::InterpreterError{
                process, instruction,
                "associative-array index must be a known integral value"};
          }
          return PackedLogic4::from_aval_bval(
              target.type.index_width, aval, 0);
        };
    const auto key_less =
        [](const runtime::simir::ContainerValue& target,
           const PackedLogic4& left,
           const PackedLogic4& right) {
          const auto lhs = left.low_word().aval;
          const auto rhs = right.low_word().aval;
          if (target.type.signed_indices) {
            const auto sign =
                UINT64_C(1)
                << (target.type.index_width - 1U);
            const auto lhs_negative = (lhs & sign) != 0;
            const auto rhs_negative = (rhs & sign) != 0;
            if (lhs_negative != rhs_negative) {
              return lhs_negative;
            }
          }
          return lhs < rhs;
        };
    const auto lower_key =
        [&](const runtime::simir::ContainerValue& target,
            const PackedLogic4& sought) {
          return static_cast<std::size_t>(
              std::lower_bound(
                  target.keys.begin(), target.keys.end(), sought,
                  [&](const PackedLogic4& candidate,
                      const PackedLogic4& value) {
                    return key_less(target, candidate, value);
                  })
              - target.keys.begin());
        };
    const auto key_equal =
        [](const PackedLogic4& left,
           const PackedLogic4& right) {
          return left.low_word().aval
              == right.low_word().aval;
        };
    if (const auto* resize =
            std::get_if<runtime::simir::ResizeContainer>(
                &operation)) {
      auto& target = registers.at(resize->target);
      if (target.type.queue || target.type.associative
          || target.type.fixed) {
        throw runtime::simir::InterpreterError{
            process, instruction,
            target.type.queue
                ? "new[size] cannot resize a queue"
                : target.type.associative
                      ? "new[size] cannot resize an associative array"
                      : "new[size] cannot resize a static array"};
      }
      const auto size = index(
          input0_aval, input0_bval, false,
          "dynamic-array size");
      if (size > runtime::simir::maximum_container_elements) {
        throw runtime::simir::InterpreterError{
            process, instruction,
            "dynamic-array size exceeds the 4096-element limit"};
      }
      target.elements.assign(
          size,
          PackedLogic4::from_aval_bval(
              target.type.element_width, 0, 0));
    } else if (const auto* copy =
                   std::get_if<
                       runtime::simir::CopyContainerRegister>(
                       &operation)) {
      auto& target = registers.at(copy->destination);
      const auto& source = registers.at(copy->source);
      require_same(target, source);
      target.elements = source.elements;
      target.keys = source.keys;
    } else if (const auto* read_object =
                   std::get_if<
                       runtime::simir::ReadContainerObject>(
                       &operation)) {
      auto& target = registers.at(read_object->destination);
      const auto source =
          state.context->read_container_object(read_object->object);
      require_same(target, source);
      target.elements = source.elements;
      target.keys = source.keys;
    } else if (const auto* write_object =
                   std::get_if<
                       runtime::simir::WriteContainerObject>(
                       &operation)) {
      state.context->write_container_object(
          write_object->object,
          registers.at(write_object->source));
    } else if (const auto* size =
                   std::get_if<runtime::simir::ContainerSize>(
                       &operation)) {
      *result_aval = registers.at(size->source).elements.size();
    } else if (const auto* reduction =
                   std::get_if<runtime::simir::ContainerReduction>(
                       &operation)) {
      const auto result = runtime::simir::reduce_container_value(
          registers.at(reduction->source),
          reduction->operation,
          reduction->transformation);
      const auto word = result.low_word();
      *result_aval = word.aval;
      *result_bval = word.bval;
    } else if (const auto* ordering =
                   std::get_if<runtime::simir::OrderContainer>(
                       &operation)) {
      runtime::simir::order_container_value(
          registers.at(ordering->target),
          ordering->operation);
    } else if (const auto* locator =
                   std::get_if<runtime::simir::LocateContainer>(
                       &operation)) {
      runtime::simir::locate_container_values(
          registers.at(locator->destination),
          registers.at(locator->source),
          locator->operation,
          locator->predicate);
    } else if (const auto* read =
                   std::get_if<runtime::simir::ContainerRead>(
                       &operation)) {
      const auto& source = registers.at(read->source);
      if (source.type.associative) {
        const auto sought =
            key(source, input0_aval, input0_bval);
        const auto at = lower_key(source, sought);
        if (at < source.keys.size()
            && key_equal(source.keys[at], sought)) {
          const auto word = source.elements[at].low_word();
          *result_aval = word.aval;
          *result_bval = word.bval;
        }
        return 0;
      }
      const auto at =
          source.type.fixed
              ? fixed_offset(source, input0_aval, input0_bval)
              : index(
                    input0_aval, input0_bval, read->signed_index,
                    "container index");
      if (at >= source.elements.size()) {
        throw runtime::simir::InterpreterError{
            process, instruction,
            "container index is out of range"};
      }
      const auto word = source.elements[at].low_word();
      *result_aval = word.aval;
      *result_bval = word.bval;
    } else if (const auto* write =
                   std::get_if<runtime::simir::ContainerWrite>(
                       &operation)) {
      auto& target = registers.at(write->target);
      if (target.type.associative) {
        const auto sought =
            key(target, input0_aval, input0_bval);
        const auto source =
            element(target, input1_aval, input1_bval);
        const auto at = lower_key(target, sought);
        if (at < target.keys.size()
            && key_equal(target.keys[at], sought)) {
          target.elements[at] = source;
        } else {
          if (target.elements.size()
              >= runtime::simir::maximum_container_elements) {
            throw runtime::simir::InterpreterError{
                process, instruction,
                "associative array exceeds the 4096-entry limit"};
          }
          target.keys.insert(target.keys.begin() + at, sought);
          target.elements.insert(
              target.elements.begin() + at, source);
        }
        return 0;
      }
      const auto at =
          target.type.fixed
              ? fixed_offset(target, input0_aval, input0_bval)
              : index(
                    input0_aval, input0_bval, write->signed_index,
                    "container index");
      if (at >= target.elements.size()) {
        throw runtime::simir::InterpreterError{
            process, instruction,
            "container index is out of range"};
      }
      target.elements[at] =
          element(target, input1_aval, input1_bval);
    } else if (const auto* erase =
                   std::get_if<runtime::simir::DeleteContainer>(
                       &operation)) {
      auto& target = registers.at(erase->target);
      if (erase->index) {
        const auto sought =
            key(target, input0_aval, input0_bval);
        const auto at = lower_key(target, sought);
        if (at < target.keys.size()
            && key_equal(target.keys[at], sought)) {
          target.keys.erase(target.keys.begin() + at);
          target.elements.erase(target.elements.begin() + at);
        }
      } else {
        if (target.type.fixed) {
          throw runtime::simir::InterpreterError{
              process, instruction,
              "delete() cannot clear a static array"};
        }
        target.elements.clear();
        target.keys.clear();
      }
    } else if (const auto* load =
                   std::get_if<runtime::simir::LoadMemory>(
                       &operation)) {
      const auto known_optional =
          [&](const std::optional<runtime::simir::RegisterId> source,
              const std::uint64_t aval,
              const std::uint64_t bval,
              const std::string_view role)
              -> std::optional<std::int32_t> {
            if (!source) {
              return std::nullopt;
            }
            if (bval != 0) {
              throw runtime::simir::InterpreterError{
                  process, instruction,
                  std::string{role}
                      + " must be a known 32-bit integral value"};
            }
            return static_cast<std::int32_t>(
                static_cast<std::uint32_t>(aval));
          };
      const auto handle = state.context->open_file(
          state.executor->string_registers_.at(load->path), "r");
      std::string text;
      try {
        while (!state.context->file_end_of_file(handle)) {
          std::uint32_t count{};
          auto line =
              state.context->read_file_line(handle, count);
          if (text.size() + line.size()
              > runtime::simir::maximum_memory_file_bytes) {
            throw std::length_error{
                "read-memory file exceeds the 1 MiB limit"};
          }
          text += line;
        }
        state.context->close_file(handle);
      } catch (...) {
        try {
          state.context->close_file(handle);
        } catch (...) {
        }
        throw;
      }
      runtime::simir::load_memory_text(
          registers.at(load->target), text, load->hexadecimal,
          known_optional(
              load->start, input0_aval, input0_bval,
              "read-memory start"),
          known_optional(
              load->finish, input1_aval, input1_bval,
              "read-memory finish"));
    } else if (const auto* exists =
                   std::get_if<runtime::simir::ContainerExists>(
                       &operation)) {
      const auto& source = registers.at(exists->source);
      const auto sought =
          key(source, input0_aval, input0_bval);
      const auto at = lower_key(source, sought);
      *result_aval =
          at < source.keys.size()
              && key_equal(source.keys[at], sought)
          ? 1U
          : 0U;
    } else if (const auto* traverse =
                   std::get_if<runtime::simir::TraverseContainer>(
                       &operation)) {
      const auto& source = registers.at(traverse->source);
      if (!source.type.associative) {
        throw runtime::simir::InterpreterError{
            process, instruction,
            "first/last/next/prev require an associative array"};
      }
      std::optional<std::size_t> selected;
      if (!source.keys.empty()) {
        if (traverse->traversal
            == runtime::simir::ContainerTraversal::first) {
          selected = 0;
        } else if (
            traverse->traversal
            == runtime::simir::ContainerTraversal::last) {
          selected = source.keys.size() - 1U;
        } else {
          const auto sought =
              key(source, input0_aval, input0_bval);
          const auto at = lower_key(source, sought);
          if (traverse->traversal
              == runtime::simir::ContainerTraversal::next) {
            const auto next =
                at < source.keys.size()
                        && key_equal(source.keys[at], sought)
                    ? at + 1U
                    : at;
            if (next < source.keys.size()) {
              selected = next;
            }
          } else if (at != 0) {
            selected = at - 1U;
          }
        }
      }
      if (input1_aval == 0) {
        if (selected) {
          const auto word = source.keys[*selected].low_word();
          *result_aval = word.aval;
          *result_bval = word.bval;
        } else {
          *result_aval = input0_aval;
          *result_bval = input0_bval;
        }
      } else {
        *result_aval = selected ? 1U : 0U;
      }
    } else if (const auto* push =
                   std::get_if<runtime::simir::PushContainer>(
                       &operation)) {
      auto& target = registers.at(push->target);
      if (!target.type.queue) {
        throw runtime::simir::InterpreterError{
            process, instruction,
            target.type.associative
                ? "queue method used on an associative array"
                : "queue method used on a dynamic array"};
      }
      const auto source =
          element(target, input0_aval, input0_bval);
      if (push->front) {
        target.elements.insert(target.elements.begin(), source);
      } else {
        target.elements.push_back(source);
      }
      const auto maximum =
          target.type.maximum_elements.value_or(
              static_cast<std::uint32_t>(
                  runtime::simir::maximum_container_elements));
      if (target.elements.size() > maximum) {
        target.elements.pop_back();
      }
    } else if (const auto* pop =
                   std::get_if<runtime::simir::PopContainer>(
                       &operation)) {
      auto& target = registers.at(pop->target);
      if (!target.type.queue || target.elements.empty()) {
        throw runtime::simir::InterpreterError{
            process, instruction,
            target.type.queue
                ? "cannot pop an empty queue"
                : (target.type.associative
                       ? "queue method used on an associative array"
                       : "queue method used on a dynamic array")};
      }
      const auto source = pop->front
          ? target.elements.front()
          : target.elements.back();
      const auto word = source.low_word();
      *result_aval = word.aval;
      *result_bval = word.bval;
      if (pop->front) {
        target.elements.erase(target.elements.begin());
      } else {
        target.elements.pop_back();
      }
    } else {
      throw compiler::LlvmJitError{
          "compiled container callback has the wrong operation"};
    }
    return 0;
  } catch (...) {
    capture_failure(state);
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
