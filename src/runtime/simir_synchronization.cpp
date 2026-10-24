// SPDX-License-Identifier: Apache-2.0

#include "simir_internal.hpp"

#include "fsim/runtime/systemverilog_string.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace fsim::runtime::simir {
namespace {

constexpr std::uint64_t synchronization_handle_tag_mask =
    UINT64_C(0xffff000000000000);
constexpr std::uint64_t mailbox_handle_tag = UINT64_C(0x4d42000000000000);
constexpr std::uint64_t semaphore_handle_tag = UINT64_C(0x5345000000000000);
constexpr std::uint64_t event_handle_tag = UINT64_C(0x4556000000000000);
constexpr std::uint64_t synchronization_handle_payload_mask =
    UINT64_C(0x0000ffffffffffff);
constexpr std::size_t maximum_synchronization_objects = 1U << 20U;

[[nodiscard]] bool valid_mailbox_element_kind(
    const MailboxElementKind kind) noexcept {
  return kind == MailboxElementKind::packed
      || kind == MailboxElementKind::string;
}

[[nodiscard]] std::size_t mailbox_entry_storage_bytes(
    const std::uint32_t width) {
  const auto words = (static_cast<std::size_t>(width) + 63U) / 64U;
  return std::max<std::size_t>(1U, words * sizeof(std::uint64_t) * 2U);
}

// A string entry is charged its bytes when it enters the mailbox queue, so
// the capacity bound counts only the fixed part of each entry.
[[nodiscard]] constexpr std::size_t mailbox_string_entry_storage_bytes() {
  return sizeof(std::string);
}

}  // namespace

[[nodiscard]] bool Interpreter::Impl::handle_synchronization_boundary(
    ProcessState& process,
    const InstructionIndex instruction,
    const Operation& operation) {
  const auto read_register = [&](const RegisterId id,
                                 const std::uint32_t width) {
    return process.executor
               ? process.executor->read_register(id, width)
               : get_register(process, id);
  };
  const auto read_string_register = [&](const StringRegisterId id) {
    auto value = process.executor
        ? process.executor->read_string_register(id)
        : get_string_register(process, id);
    if (value.size() > maximum_string_bytes) {
      process.pc = instruction;
      fail(process, "mailbox string value exceeds the byte limit");
    }
    (void)systemverilog_string_length(value);
    return value;
  };
  const auto known_u32 = [&](const RegisterId id,
                             const std::string_view purpose) {
    const auto value = read_register(id, 32U).low_word();
    if (value.bval != 0 || value.aval > UINT32_MAX) {
      process.pc = instruction;
      fail(process, std::string{purpose} + " must be a known 32-bit value");
    }
    return static_cast<std::uint32_t>(value.aval);
  };
  const auto known_i32 = [&](const RegisterId id,
                             const std::string_view purpose) {
    const auto value = read_register(id, 32U).low_word();
    if (value.bval != 0 || value.aval > UINT32_MAX) {
      process.pc = instruction;
      fail(process, std::string{purpose} + " must be a known 32-bit value");
    }
    return std::bit_cast<std::int32_t>(
        static_cast<std::uint32_t>(value.aval));
  };
  const auto receiver_handle = [&](const RegisterId id) {
    const auto value = read_register(id, 64U).low_word();
    if (value.bval != 0) {
      process.pc = instruction;
      fail(process, "synchronization handle contains X or Z");
    }
    return value.aval;
  };
  const auto mailbox_from_handle = [&](const RegisterId id) -> MailboxState& {
    const auto handle = receiver_handle(id);
    if ((handle & synchronization_handle_tag_mask) != mailbox_handle_tag) {
      process.pc = instruction;
      fail(process, "mailbox handle is null or has the wrong kind");
    }
    const auto encoded = handle & synchronization_handle_payload_mask;
    if (encoded == 0 || encoded > mailboxes.size()) {
      process.pc = instruction;
      fail(process, "mailbox handle is outside the live object range");
    }
    return mailboxes[static_cast<std::size_t>(encoded - 1U)];
  };
  const auto semaphore_from_handle =
      [&](const RegisterId id) -> SemaphoreState& {
    const auto handle = receiver_handle(id);
    if ((handle & synchronization_handle_tag_mask)
        != semaphore_handle_tag) {
      process.pc = instruction;
      fail(process, "semaphore handle is null or has the wrong kind");
    }
    const auto encoded = handle & synchronization_handle_payload_mask;
    if (encoded == 0 || encoded > semaphores.size()) {
      process.pc = instruction;
      fail(process, "semaphore handle is outside the live object range");
    }
    return semaphores[static_cast<std::size_t>(encoded - 1U)];
  };
  const auto wake = [&](const ProcessId id) {
    auto& waiter = get_process(id);
    if (waiter.halted) return false;
    waiter.status = ProcessStatus::running;
    queue_active_current(id);
    return true;
  };
  const auto write_bool = [&](const RegisterId destination,
                              const bool value) {
    write_process_register(
        process,
        destination,
        PackedLogic4::from_aval_bval(32, value ? 1U : 0U, 0));
  };

  if (const auto* create = operation_get_if<MailboxCreate>(&operation)) {
    if (!valid_mailbox_element_kind(create->element_kind)
        || (create->element_kind == MailboxElementKind::packed
            ? create->element_width == 0U
            : create->element_width != 0U)) {
      process.pc = instruction;
      fail(process, "mailbox has an invalid element kind or width");
    }
    if (mailboxes.size() >= maximum_synchronization_objects) {
      process.pc = instruction;
      fail(process, "mailbox object limit is exhausted");
    }
    const auto requested = known_u32(create->capacity, "mailbox capacity");
    const auto maximum = maximum_container_storage_bytes
        / (create->element_kind == MailboxElementKind::string
                ? mailbox_string_entry_storage_bytes()
                : mailbox_entry_storage_bytes(create->element_width));
    if (maximum == 0 || (requested != 0 && requested > maximum)) {
      process.pc = instruction;
      fail(process, "mailbox capacity exceeds the bounded storage limit");
    }
    mailboxes.push_back(
        MailboxState{create->element_kind, create->element_width,
                     requested == 0 ? maximum : requested,
                     {}, {}, 0U, {}, {}});
    const auto handle = mailbox_handle_tag | mailboxes.size();
    write_process_register(
        process,
        create->destination,
        PackedLogic4::from_aval_bval(64, handle, 0));
    process.status = ProcessStatus::running;
    return true;
  }

  if (const auto* put = operation_get_if<MailboxPut>(&operation)) {
    auto& mailbox = mailbox_from_handle(put->receiver);
    if (!valid_mailbox_element_kind(put->element_kind)
        || put->element_kind != mailbox.element_kind
        || put->element_width != mailbox.element_width) {
      process.pc = instruction;
      fail(process, "mailbox put operation has the wrong element type");
    }
    PackedLogic4 packed_value;
    std::string string_value;
    if (mailbox.element_kind == MailboxElementKind::packed) {
      packed_value = read_register(put->source, put->element_width);
      if (packed_value.width() != put->element_width) {
        process.pc = instruction;
        fail(process, "mailbox put value has the wrong element width");
      }
    } else {
      string_value = read_string_register(put->string_source);
      if (string_value.size() > maximum_container_storage_bytes
              - std::min(mailbox.string_bytes,
                  maximum_container_storage_bytes)) {
        process.pc = instruction;
        fail(process, "mailbox string storage exceeds the bounded storage limit");
      }
    }
    // Hand a put value to the waiting readers in order; a get consumes it,
    // a peek leaves it for the next reader or the mailbox queue.
    const auto deliver = [&](const auto& delivered, auto& entries,
                             const bool string) {
      bool consumed{};
      while (!mailbox.readers.empty()) {
        const auto reader = mailbox.readers.front();
        mailbox.readers.pop_front();
        auto& target = get_process(reader.process);
        auto& pending = string ? target.cold().pending_mailbox_string_result
                               : target.cold().pending_mailbox_result;
        const auto destination
            = string ? reader.string_destination : reader.destination;
        if (!target.halted) {
          if constexpr (std::is_same_v<std::decay_t<decltype(delivered)>,
                            std::string>) {
            write_process_string_register(target, destination, delivered);
          } else {
            write_process_register(target, destination, delivered);
          }
        }
        if (pending == destination) {
          pending.reset();
        }
        if (target.halted) {
          continue;
        }
        (void)wake(reader.process);
        if (!reader.peek) {
          consumed = true;
          break;
        }
      }
      if (!consumed) {
        if constexpr (std::is_same_v<std::decay_t<decltype(delivered)>,
                          std::string>) {
          mailbox.string_bytes += delivered.size();
        }
        entries.push_back(delivered);
      }
    };
    const auto entry_count = mailbox.element_kind == MailboxElementKind::string
        ? mailbox.string_entries.size()
        : mailbox.entries.size();
    const auto available = mailbox.writers.empty()
        && (!mailbox.readers.empty()
            || entry_count < mailbox.capacity);
    if (available) {
      if (mailbox.element_kind == MailboxElementKind::string) {
        deliver(string_value, mailbox.string_entries, true);
      } else {
        deliver(packed_value, mailbox.entries, false);
      }
      if (put->result) write_bool(*put->result, true);
      process.status = ProcessStatus::running;
    } else if (put->result) {
      write_bool(*put->result, false);
      process.status = ProcessStatus::running;
    } else {
      mailbox.writers.push_back({
          process.id, std::move(packed_value), std::move(string_value)});
      process.status = ProcessStatus::waiting;
      notify_execution_point(
          process, instruction, ExecutionPointKind::process_suspend,
          process.cold().current_source);
    }
    return true;
  }

  if (const auto* get = operation_get_if<MailboxGet>(&operation)) {
    auto& mailbox = mailbox_from_handle(get->receiver);
    if (!valid_mailbox_element_kind(get->element_kind)
        || get->element_kind != mailbox.element_kind
        || get->element_width != mailbox.element_width) {
      process.pc = instruction;
      fail(process, "mailbox get operation has the wrong element type");
    }
    const auto entry_count = mailbox.element_kind == MailboxElementKind::string
        ? mailbox.string_entries.size()
        : mailbox.entries.size();
    if (entry_count != 0U) {
      if (mailbox.element_kind == MailboxElementKind::string) {
        write_process_string_register(
            process, get->string_destination,
            mailbox.string_entries.front());
      } else {
        write_process_register(
            process, get->destination, mailbox.entries.front());
      }
      if (!get->peek) {
        if (mailbox.element_kind == MailboxElementKind::string) {
          mailbox.string_bytes -= mailbox.string_entries.front().size();
          mailbox.string_entries.pop_front();
        } else {
          mailbox.entries.pop_front();
        }
        while (!mailbox.writers.empty()) {
          auto writer = std::move(mailbox.writers.front());
          mailbox.writers.pop_front();
          if (!wake(writer.process)) continue;
          if (mailbox.element_kind == MailboxElementKind::string) {
            mailbox.string_bytes += writer.string_value.size();
            mailbox.string_entries.push_back(
                std::move(writer.string_value));
          } else {
            mailbox.entries.push_back(std::move(writer.value));
          }
          break;
        }
      }
      if (get->result) write_bool(*get->result, true);
      process.status = ProcessStatus::running;
    } else if (get->result) {
      write_bool(*get->result, false);
      process.status = ProcessStatus::running;
    } else {
      if (mailbox.element_kind == MailboxElementKind::string) {
        process.cold().pending_mailbox_string_result
            = get->string_destination;
      } else {
        process.cold().pending_mailbox_result = get->destination;
      }
      mailbox.readers.push_back(
          {process.id, get->destination, get->string_destination,
              get->peek});
      process.status = ProcessStatus::waiting;
      notify_execution_point(
          process, instruction, ExecutionPointKind::process_suspend,
          process.cold().current_source);
    }
    return true;
  }

  if (const auto* event = operation_get_if<DynamicEvent>(&operation)) {
    // A null handle names no event: a trigger has no effect and a wait
    // never resumes (IEEE 1800-2017 15.5.5).
    const auto event_from_handle = [&](const RegisterId id)
        -> DynamicEventState* {
      const auto handle = receiver_handle(id);
      if (handle == 0U) {
        return nullptr;
      }
      if ((handle & synchronization_handle_tag_mask) != event_handle_tag) {
        process.pc = instruction;
        fail(process, "event handle has the wrong kind");
      }
      const auto encoded = handle & synchronization_handle_payload_mask;
      if (encoded == 0 || encoded > dynamic_events.size()) {
        process.pc = instruction;
        fail(process, "event handle is outside the live object range");
      }
      return &dynamic_events[static_cast<std::size_t>(encoded - 1U)];
    };
    switch (event->operation) {
    case DynamicEventOperation::create: {
      if (dynamic_events.size() >= maximum_synchronization_objects) {
        process.pc = instruction;
        fail(process, "event object limit is exhausted");
      }
      dynamic_events.emplace_back();
      write_process_register(process, event->destination,
          PackedLogic4::from_aval_bval(
              64, event_handle_tag | dynamic_events.size(), 0));
      process.status = ProcessStatus::running;
      return true;
    }
    case DynamicEventOperation::trigger: {
      const auto handle = receiver_handle(event->receiver);
      if (event_from_handle(event->receiver) != nullptr) {
        const auto index = static_cast<std::size_t>(
            (handle & synchronization_handle_payload_mask) - 1U);
        const auto trigger = [this, handle, index](Scheduler&) {
          auto& state = dynamic_events[index];
          state.last_trigger = scheduler.now();
          auto waiters = std::move(state.waiters);
          state.waiters.clear();
          for (const auto waiter : waiters) {
            auto& target = get_process(waiter);
            if (target.halted || target.status != ProcessStatus::waiting
                || target.cold().pending_dynamic_event != handle) {
              continue;
            }
            target.cold().pending_dynamic_event.reset();
            target.status = ProcessStatus::running;
            queue_active_current(waiter);
          }
        };
        if (event->nonblocking
            && processes.program_view(process.id).scheduling_domain()
                == ProcessSchedulingDomain::systemverilog) {
          // `->>` triggers the event in the NBA region (IEEE 1800-2017
          // 15.5.1, 4.4).
          const auto origin = capture_signal_change_origin(
              process.id, SignalUpdateDomain::systemverilog_nba);
          scheduler.schedule_systemverilog(origin.phase, process.id, trigger);
        } else {
          trigger(scheduler);
        }
      }
      process.status = ProcessStatus::running;
      return true;
    }
    case DynamicEventOperation::wait: {
      const auto handle = receiver_handle(event->receiver);
      if (auto* state = event_from_handle(event->receiver)) {
        state->waiters.push_back(process.id);
      }
      process.cold().pending_dynamic_event = handle;
      process.status = ProcessStatus::waiting;
      notify_execution_point(
          process, instruction, ExecutionPointKind::process_suspend,
          process.cold().current_source);
      return true;
    }
    case DynamicEventOperation::triggered: {
      const auto* state = event_from_handle(event->receiver);
      const bool triggered = state != nullptr && state->last_trigger
          && *state->last_trigger == scheduler.now();
      write_process_register(process, event->destination,
          PackedLogic4::from_aval_bval(1, triggered ? 1U : 0U, 0));
      process.status = ProcessStatus::running;
      return true;
    }
    }
  }

  if (const auto* num = operation_get_if<MailboxNum>(&operation)) {
    const auto& mailbox = mailbox_from_handle(num->receiver);
    write_process_register(
        process,
        num->destination,
        PackedLogic4::from_aval_bval(
            32,
            static_cast<std::uint32_t>(
                mailbox.element_kind == MailboxElementKind::string
                    ? mailbox.string_entries.size()
                    : mailbox.entries.size()),
            0));
    process.status = ProcessStatus::running;
    return true;
  }

  if (const auto* create = operation_get_if<SemaphoreCreate>(&operation)) {
    if (semaphores.size() >= maximum_synchronization_objects) {
      process.pc = instruction;
      fail(process, "semaphore object limit is exhausted");
    }
    semaphores.push_back(
        SemaphoreState{known_i32(create->keys, "semaphore key count"), {}});
    const auto handle = semaphore_handle_tag | semaphores.size();
    write_process_register(
        process,
        create->destination,
        PackedLogic4::from_aval_bval(64, handle, 0));
    process.status = ProcessStatus::running;
    return true;
  }

  if (const auto* get = operation_get_if<SemaphoreGet>(&operation)) {
    auto& semaphore = semaphore_from_handle(get->receiver);
    const auto requested = known_i32(get->keys, "semaphore get count");
    if (requested < 0) {
      process.pc = instruction;
      fail(process, "semaphore get count cannot be negative");
    }
    const auto available = semaphore.waiters.empty()
        && semaphore.keys >= requested;
    if (available) {
      semaphore.keys -= requested;
      if (get->result) write_bool(*get->result, true);
      process.status = ProcessStatus::running;
    } else if (get->result) {
      write_bool(*get->result, false);
      process.status = ProcessStatus::running;
    } else {
      semaphore.waiters.push_back({process.id, requested});
      process.status = ProcessStatus::waiting;
      notify_execution_point(
          process, instruction, ExecutionPointKind::process_suspend,
          process.cold().current_source);
    }
    return true;
  }

  if (const auto* put = operation_get_if<SemaphorePut>(&operation)) {
    auto& semaphore = semaphore_from_handle(put->receiver);
    const auto returned = known_i32(put->keys, "semaphore put count");
    if (returned < 0) {
      process.pc = instruction;
      fail(process, "semaphore put count cannot be negative");
    }
    if (semaphore.keys
        > std::numeric_limits<std::int64_t>::max()
            - static_cast<std::int64_t>(returned)) {
      process.pc = instruction;
      fail(process, "semaphore key count overflow");
    }
    semaphore.keys += returned;
    while (!semaphore.waiters.empty()) {
      const auto waiter = semaphore.waiters.front();
      if (get_process(waiter.process).halted) {
        semaphore.waiters.pop_front();
        continue;
      }
      if (waiter.keys > semaphore.keys) break;
      semaphore.waiters.pop_front();
      semaphore.keys -= waiter.keys;
      (void)wake(waiter.process);
    }
    process.status = ProcessStatus::running;
    return true;
  }

  return false;
}

}  // namespace fsim::runtime::simir
