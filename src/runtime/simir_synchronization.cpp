// SPDX-License-Identifier: Apache-2.0

#include "simir_internal.hpp"

#include <limits>

namespace fsim::runtime::simir {
namespace {

constexpr std::uint64_t synchronization_handle_tag_mask =
    UINT64_C(0xffff000000000000);
constexpr std::uint64_t mailbox_handle_tag = UINT64_C(0x4d42000000000000);
constexpr std::uint64_t semaphore_handle_tag = UINT64_C(0x5345000000000000);
constexpr std::uint64_t synchronization_handle_payload_mask =
    UINT64_C(0x0000ffffffffffff);
constexpr std::size_t maximum_synchronization_objects = 1U << 20U;

[[nodiscard]] std::size_t mailbox_entry_storage_bytes(
    const std::uint32_t width) {
  const auto words = (static_cast<std::size_t>(width) + 63U) / 64U;
  return std::max<std::size_t>(1U, words * sizeof(std::uint64_t) * 2U);
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
  const auto known_u32 = [&](const RegisterId id,
                             const std::string_view purpose) {
    const auto value = read_register(id, 32U).low_word();
    if (value.bval != 0 || value.aval > UINT32_MAX) {
      process.pc = instruction;
      fail(process, std::string{purpose} + " must be a known 32-bit value");
    }
    return static_cast<std::uint32_t>(value.aval);
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
    if (create->element_width == 0) {
      process.pc = instruction;
      fail(process, "mailbox element width must be positive");
    }
    if (mailboxes.size() >= maximum_synchronization_objects) {
      process.pc = instruction;
      fail(process, "mailbox object limit is exhausted");
    }
    const auto requested = known_u32(create->capacity, "mailbox capacity");
    const auto maximum = maximum_container_storage_bytes
        / mailbox_entry_storage_bytes(create->element_width);
    if (maximum == 0 || (requested != 0 && requested > maximum)) {
      process.pc = instruction;
      fail(process, "mailbox capacity exceeds the bounded storage limit");
    }
    mailboxes.push_back(
        MailboxState{create->element_width,
                     requested == 0 ? maximum : requested,
                     {}, {}, {}});
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
    if (put->element_width != mailbox.element_width) {
      process.pc = instruction;
      fail(process, "mailbox put operation has the wrong element type");
    }
    auto value = read_register(put->source, put->element_width);
    if (value.width() != put->element_width) {
      process.pc = instruction;
      fail(process, "mailbox put value has the wrong element width");
    }
    const auto deliver = [&](const PackedLogic4& delivered) {
      bool consumed{};
      while (!mailbox.readers.empty()) {
        const auto reader = mailbox.readers.front();
        mailbox.readers.pop_front();
        auto& target = get_process(reader.process);
        if (target.halted) continue;
        write_process_register(target, reader.destination, delivered);
        (void)wake(reader.process);
        if (!reader.peek) {
          consumed = true;
          break;
        }
      }
      if (!consumed) mailbox.entries.push_back(delivered);
    };
    const auto available = mailbox.writers.empty()
        && (!mailbox.readers.empty()
            || mailbox.entries.size() < mailbox.capacity);
    if (available) {
      deliver(value);
      if (put->result) write_bool(*put->result, true);
      process.status = ProcessStatus::running;
    } else if (put->result) {
      write_bool(*put->result, false);
      process.status = ProcessStatus::running;
    } else {
      mailbox.writers.push_back({process.program.id, std::move(value)});
      process.status = ProcessStatus::waiting;
      notify_execution_point(
          process, instruction, ExecutionPointKind::process_suspend,
          process.current_source);
    }
    return true;
  }

  if (const auto* get = operation_get_if<MailboxGet>(&operation)) {
    auto& mailbox = mailbox_from_handle(get->receiver);
    if (get->element_width != mailbox.element_width) {
      process.pc = instruction;
      fail(process, "mailbox get operation has the wrong element type");
    }
    if (!mailbox.entries.empty()) {
      write_process_register(process, get->destination, mailbox.entries.front());
      if (!get->peek) {
        mailbox.entries.pop_front();
        while (!mailbox.writers.empty()) {
          auto writer = std::move(mailbox.writers.front());
          mailbox.writers.pop_front();
          if (!wake(writer.process)) continue;
          mailbox.entries.push_back(std::move(writer.value));
          break;
        }
      }
      if (get->result) write_bool(*get->result, true);
      process.status = ProcessStatus::running;
    } else if (get->result) {
      write_bool(*get->result, false);
      process.status = ProcessStatus::running;
    } else {
      mailbox.readers.push_back(
          {process.program.id, get->destination, get->peek});
      process.status = ProcessStatus::waiting;
      notify_execution_point(
          process, instruction, ExecutionPointKind::process_suspend,
          process.current_source);
    }
    return true;
  }

  if (const auto* num = operation_get_if<MailboxNum>(&operation)) {
    const auto& mailbox = mailbox_from_handle(num->receiver);
    write_process_register(
        process,
        num->destination,
        PackedLogic4::from_aval_bval(
            32, static_cast<std::uint32_t>(mailbox.entries.size()), 0));
    process.status = ProcessStatus::running;
    return true;
  }

  if (const auto* create = operation_get_if<SemaphoreCreate>(&operation)) {
    if (semaphores.size() >= maximum_synchronization_objects) {
      process.pc = instruction;
      fail(process, "semaphore object limit is exhausted");
    }
    semaphores.push_back(
        SemaphoreState{known_u32(create->keys, "semaphore key count"), {}});
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
    const auto requested = known_u32(get->keys, "semaphore get count");
    if (requested == 0) {
      process.pc = instruction;
      fail(process, "semaphore get count must be positive");
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
      semaphore.waiters.push_back({process.program.id, requested});
      process.status = ProcessStatus::waiting;
      notify_execution_point(
          process, instruction, ExecutionPointKind::process_suspend,
          process.current_source);
    }
    return true;
  }

  if (const auto* put = operation_get_if<SemaphorePut>(&operation)) {
    auto& semaphore = semaphore_from_handle(put->receiver);
    const auto returned = known_u32(put->keys, "semaphore put count");
    if (returned == 0) {
      process.pc = instruction;
      fail(process, "semaphore put count must be positive");
    }
    if (returned > UINT32_MAX - semaphore.keys) {
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
