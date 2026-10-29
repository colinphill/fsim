// SPDX-License-Identifier: Apache-2.0

#include "simir_internal.hpp"
#include "simir_static_kernel_state.hpp"

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

struct Interpreter::Impl::ProcessSynchronizationClient final
    : SynchronizationClient {
  ProcessSynchronizationClient(Impl& impl_value, ProcessState& process_value,
      const InstructionIndex instruction_value) noexcept
      : impl{impl_value}, process{process_value},
        instruction{instruction_value} {}
  [[nodiscard]] ProcessId self() const override { return process.id; }
  [[nodiscard]] PackedLogic4 read(const RegisterId id,
      const std::uint32_t width) override {
    return process.executor ? process.executor->read_register(id, width)
                            : impl.get_register(process, id);
  }
  [[nodiscard]] std::string read_string(const StringRegisterId id) override {
    return process.executor ? process.executor->read_string_register(id)
                            : impl.get_string_register(process, id);
  }
  void write(const RegisterId id, PackedLogic4 value) override {
    impl.write_process_register(process, id, std::move(value));
  }
  void write_string(const StringRegisterId id, std::string value) override {
    impl.write_process_string_register(process, id, std::move(value));
  }
  [[noreturn]] void fail(const std::string_view message) override {
    process.pc = instruction;
    impl.fail(process, std::string{message});
  }
  [[nodiscard]] SimulationTick now() const override {
    return impl.scheduler.now();
  }
  void wait(const Wait& wait) override {
    if (wait.mailbox_result) {
      process.cold().pending_mailbox_result = wait.mailbox_result;
    }
    if (wait.mailbox_string_result) {
      process.cold().pending_mailbox_string_result
          = wait.mailbox_string_result;
    }
    if (wait.dynamic_event) {
      process.cold().pending_dynamic_event = wait.dynamic_event;
    }
    process.status = ProcessStatus::waiting;
    impl.notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.cold().current_source);
  }
  [[nodiscard]] bool defer_trigger(Scheduler::Task trigger) override {
    if (impl.processes.program_view(process.id).scheduling_domain()
        != ProcessSchedulingDomain::systemverilog) {
      return false;
    }
    // `->>` triggers the event in the NBA region (IEEE 1800-2017 15.5.1,
    // 4.4).
    const auto origin = impl.capture_signal_change_origin(
        process.id, SignalUpdateDomain::systemverilog_nba);
    impl.scheduler.schedule_systemverilog(
        origin.phase, process.id, std::move(trigger));
    return true;
  }
  Impl& impl;
  ProcessState& process;
  InstructionIndex instruction;
};

[[nodiscard]] bool Interpreter::Impl::handle_synchronization_boundary(
    ProcessState& process,
    const InstructionIndex instruction,
    const Operation& operation) {
  ProcessSynchronizationClient client{*this, process, instruction};
  switch (run_synchronization(client, operation)) {
  case SynchronizationResult::other:
    return false;
  case SynchronizationResult::completed:
    process.status = ProcessStatus::running;
    return true;
  case SynchronizationResult::waiting:
    return true;
  }
  return false;
}

bool Interpreter::Impl::synchronization_halted(const ProcessId id) {
  if (static_kernel && static_kernel->synchronization_thread(id)) {
    return !static_kernel->synchronization_live(id);
  }
  return get_process(id).halted;
}

void Interpreter::Impl::synchronization_deliver(const ProcessId id,
    const RegisterId destination, const PackedLogic4& value) {
  if (static_kernel && static_kernel->synchronization_thread(id)) {
    static_kernel->synchronization_deliver(id, destination, value);
    return;
  }
  auto& target = get_process(id);
  if (!target.halted) {
    write_process_register(target, destination, value);
  }
  if (target.cold().pending_mailbox_result == destination) {
    target.cold().pending_mailbox_result.reset();
  }
}

void Interpreter::Impl::synchronization_deliver(const ProcessId id,
    const StringRegisterId destination, const std::string& value) {
  if (static_kernel && static_kernel->synchronization_thread(id)) {
    static_kernel->synchronization_deliver(id, destination, value);
    return;
  }
  auto& target = get_process(id);
  if (!target.halted) {
    write_process_string_register(target, destination, value);
  }
  if (target.cold().pending_mailbox_string_result == destination) {
    target.cold().pending_mailbox_string_result.reset();
  }
}

bool Interpreter::Impl::synchronization_wake(const ProcessId id) {
  if (static_kernel && static_kernel->synchronization_thread(id)) {
    return static_kernel->synchronization_wake(id);
  }
  auto& waiter = get_process(id);
  if (waiter.halted) return false;
  waiter.status = ProcessStatus::running;
  queue_active_current(id);
  return true;
}

void Interpreter::Impl::synchronization_event_wake(const ProcessId id,
    const std::uint64_t handle) {
  if (static_kernel && static_kernel->synchronization_thread(id)) {
    static_kernel->synchronization_event_wake(id, handle);
    return;
  }
  auto& target = get_process(id);
  if (target.halted || target.status != ProcessStatus::waiting
      || target.cold().pending_dynamic_event != handle) {
    return;
  }
  target.cold().pending_dynamic_event.reset();
  target.status = ProcessStatus::running;
  queue_active_current(id);
}

Interpreter::Impl::SynchronizationResult Interpreter::Impl::run_synchronization(
    SynchronizationClient& client, const Operation& operation) {
  using Result = SynchronizationResult;
  const auto read_string_register = [&](const StringRegisterId id) {
    auto value = client.read_string(id);
    if (value.size() > maximum_string_bytes) {
      client.fail("mailbox string value exceeds the byte limit");
    }
    (void)systemverilog_string_length(value);
    return value;
  };
  const auto known_u32 = [&](const RegisterId id,
                             const std::string_view purpose) {
    const auto value = client.read(id, 32U).low_word();
    if (value.bval != 0 || value.aval > UINT32_MAX) {
      client.fail(std::string{purpose} + " must be a known 32-bit value");
    }
    return static_cast<std::uint32_t>(value.aval);
  };
  const auto known_i32 = [&](const RegisterId id,
                             const std::string_view purpose) {
    return std::bit_cast<std::int32_t>(known_u32(id, purpose));
  };
  const auto receiver_handle = [&](const RegisterId id) {
    const auto value = client.read(id, 64U).low_word();
    if (value.bval != 0) {
      client.fail("synchronization handle contains X or Z");
    }
    return value.aval;
  };
  const auto mailbox_from_handle = [&](const RegisterId id) -> MailboxState& {
    const auto handle = receiver_handle(id);
    if ((handle & synchronization_handle_tag_mask) != mailbox_handle_tag) {
      client.fail("mailbox handle is null or has the wrong kind");
    }
    const auto encoded = handle & synchronization_handle_payload_mask;
    if (encoded == 0 || encoded > mailboxes.size()) {
      client.fail("mailbox handle is outside the live object range");
    }
    return mailboxes[static_cast<std::size_t>(encoded - 1U)];
  };
  const auto semaphore_from_handle =
      [&](const RegisterId id) -> SemaphoreState& {
    const auto handle = receiver_handle(id);
    if ((handle & synchronization_handle_tag_mask)
        != semaphore_handle_tag) {
      client.fail("semaphore handle is null or has the wrong kind");
    }
    const auto encoded = handle & synchronization_handle_payload_mask;
    if (encoded == 0 || encoded > semaphores.size()) {
      client.fail("semaphore handle is outside the live object range");
    }
    return semaphores[static_cast<std::size_t>(encoded - 1U)];
  };
  const auto write_bool = [&](const RegisterId destination,
                              const bool value) {
    client.write(destination,
        PackedLogic4::from_aval_bval(32, value ? 1U : 0U, 0));
  };

  if (const auto* create = operation_get_if<MailboxCreate>(&operation)) {
    if (!valid_mailbox_element_kind(create->element_kind)
        || (create->element_kind == MailboxElementKind::packed
            ? create->element_width == 0U
            : create->element_width != 0U)) {
      client.fail("mailbox has an invalid element kind or width");
    }
    if (mailboxes.size() >= maximum_synchronization_objects) {
      client.fail("mailbox object limit is exhausted");
    }
    const auto requested = known_u32(create->capacity, "mailbox capacity");
    const auto maximum = maximum_container_storage_bytes
        / (create->element_kind == MailboxElementKind::string
                ? mailbox_string_entry_storage_bytes()
                : mailbox_entry_storage_bytes(create->element_width));
    if (maximum == 0 || (requested != 0 && requested > maximum)) {
      client.fail("mailbox capacity exceeds the bounded storage limit");
    }
    mailboxes.push_back(
        MailboxState{create->element_kind, create->element_width,
                     requested == 0 ? maximum : requested,
                     {}, {}, 0U, {}, {}});
    const auto handle = mailbox_handle_tag | mailboxes.size();
    client.write(create->destination,
        PackedLogic4::from_aval_bval(64, handle, 0));
    return Result::completed;
  }

  if (const auto* put = operation_get_if<MailboxPut>(&operation)) {
    auto& mailbox = mailbox_from_handle(put->receiver);
    if (!valid_mailbox_element_kind(put->element_kind)
        || put->element_kind != mailbox.element_kind
        || put->element_width != mailbox.element_width) {
      client.fail("mailbox put operation has the wrong element type");
    }
    PackedLogic4 packed_value;
    std::string string_value;
    if (mailbox.element_kind == MailboxElementKind::packed) {
      packed_value = client.read(put->source, put->element_width);
      if (packed_value.width() != put->element_width) {
        client.fail("mailbox put value has the wrong element width");
      }
    } else {
      string_value = read_string_register(put->string_source);
      if (string_value.size() > maximum_container_storage_bytes
              - std::min(mailbox.string_bytes,
                  maximum_container_storage_bytes)) {
        client.fail("mailbox string storage exceeds the bounded storage limit");
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
        if (string) {
          if constexpr (std::is_same_v<std::decay_t<decltype(delivered)>,
                            std::string>) {
            synchronization_deliver(
                reader.process, reader.string_destination, delivered);
          }
        } else {
          if constexpr (!std::is_same_v<std::decay_t<decltype(delivered)>,
                            std::string>) {
            synchronization_deliver(
                reader.process, reader.destination, delivered);
          }
        }
        if (synchronization_halted(reader.process)) {
          continue;
        }
        (void)synchronization_wake(reader.process);
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
      return Result::completed;
    }
    if (put->result) {
      write_bool(*put->result, false);
      return Result::completed;
    }
    mailbox.writers.push_back({
        client.self(), std::move(packed_value), std::move(string_value)});
    client.wait({});
    return Result::waiting;
  }

  if (const auto* get = operation_get_if<MailboxGet>(&operation)) {
    auto& mailbox = mailbox_from_handle(get->receiver);
    if (!valid_mailbox_element_kind(get->element_kind)
        || get->element_kind != mailbox.element_kind
        || get->element_width != mailbox.element_width) {
      client.fail("mailbox get operation has the wrong element type");
    }
    const auto entry_count = mailbox.element_kind == MailboxElementKind::string
        ? mailbox.string_entries.size()
        : mailbox.entries.size();
    if (entry_count != 0U) {
      if (mailbox.element_kind == MailboxElementKind::string) {
        client.write_string(
            get->string_destination, mailbox.string_entries.front());
      } else {
        client.write(get->destination, mailbox.entries.front());
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
          if (!synchronization_wake(writer.process)) continue;
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
      return Result::completed;
    }
    if (get->result) {
      write_bool(*get->result, false);
      return Result::completed;
    }
    SynchronizationClient::Wait wait;
    if (mailbox.element_kind == MailboxElementKind::string) {
      wait.mailbox_string_result = get->string_destination;
    } else {
      wait.mailbox_result = get->destination;
    }
    mailbox.readers.push_back(
        {client.self(), get->destination, get->string_destination,
            get->peek});
    client.wait(wait);
    return Result::waiting;
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
        client.fail("event handle has the wrong kind");
      }
      const auto encoded = handle & synchronization_handle_payload_mask;
      if (encoded == 0 || encoded > dynamic_events.size()) {
        client.fail("event handle is outside the live object range");
      }
      return &dynamic_events[static_cast<std::size_t>(encoded - 1U)];
    };
    switch (event->operation) {
    case DynamicEventOperation::create: {
      if (dynamic_events.size() >= maximum_synchronization_objects) {
        client.fail("event object limit is exhausted");
      }
      dynamic_events.emplace_back();
      client.write(event->destination,
          PackedLogic4::from_aval_bval(
              64, event_handle_tag | dynamic_events.size(), 0));
      return Result::completed;
    }
    case DynamicEventOperation::trigger: {
      const auto handle = receiver_handle(event->receiver);
      if (event_from_handle(event->receiver) != nullptr) {
        const auto index = static_cast<std::size_t>(
            (handle & synchronization_handle_payload_mask) - 1U);
        const auto trigger = [this, handle, index](
                                 const SimulationTick now) {
          auto& state = dynamic_events[index];
          state.last_trigger = now;
          auto waiters = std::move(state.waiters);
          state.waiters.clear();
          for (const auto waiter : waiters) {
            synchronization_event_wake(waiter, handle);
          }
        };
        if (!event->nonblocking
            || !client.defer_trigger([this, trigger](Scheduler&) {
                 trigger(scheduler.now());
               })) {
          trigger(client.now());
        }
      }
      return Result::completed;
    }
    case DynamicEventOperation::wait: {
      const auto handle = receiver_handle(event->receiver);
      if (auto* state = event_from_handle(event->receiver)) {
        state->waiters.push_back(client.self());
      }
      SynchronizationClient::Wait wait;
      wait.dynamic_event = handle;
      client.wait(wait);
      return Result::waiting;
    }
    case DynamicEventOperation::triggered: {
      const auto* state = event_from_handle(event->receiver);
      const bool triggered = state != nullptr && state->last_trigger
          && *state->last_trigger == client.now();
      client.write(event->destination,
          PackedLogic4::from_aval_bval(1, triggered ? 1U : 0U, 0));
      return Result::completed;
    }
    }
  }

  if (const auto* num = operation_get_if<MailboxNum>(&operation)) {
    const auto& mailbox = mailbox_from_handle(num->receiver);
    client.write(num->destination,
        PackedLogic4::from_aval_bval(
            32,
            static_cast<std::uint32_t>(
                mailbox.element_kind == MailboxElementKind::string
                    ? mailbox.string_entries.size()
                    : mailbox.entries.size()),
            0));
    return Result::completed;
  }

  if (const auto* create = operation_get_if<SemaphoreCreate>(&operation)) {
    if (semaphores.size() >= maximum_synchronization_objects) {
      client.fail("semaphore object limit is exhausted");
    }
    semaphores.push_back(
        SemaphoreState{known_i32(create->keys, "semaphore key count"), {}});
    const auto handle = semaphore_handle_tag | semaphores.size();
    client.write(create->destination,
        PackedLogic4::from_aval_bval(64, handle, 0));
    return Result::completed;
  }

  if (const auto* get = operation_get_if<SemaphoreGet>(&operation)) {
    auto& semaphore = semaphore_from_handle(get->receiver);
    const auto requested = known_i32(get->keys, "semaphore get count");
    if (requested < 0) {
      client.fail("semaphore get count cannot be negative");
    }
    const auto available = semaphore.waiters.empty()
        && semaphore.keys >= requested;
    if (available) {
      semaphore.keys -= requested;
      if (get->result) write_bool(*get->result, true);
      return Result::completed;
    }
    if (get->result) {
      write_bool(*get->result, false);
      return Result::completed;
    }
    semaphore.waiters.push_back({client.self(), requested});
    client.wait({});
    return Result::waiting;
  }

  if (const auto* put = operation_get_if<SemaphorePut>(&operation)) {
    auto& semaphore = semaphore_from_handle(put->receiver);
    const auto returned = known_i32(put->keys, "semaphore put count");
    if (returned < 0) {
      client.fail("semaphore put count cannot be negative");
    }
    if (semaphore.keys
        > std::numeric_limits<std::int64_t>::max()
            - static_cast<std::int64_t>(returned)) {
      client.fail("semaphore key count overflow");
    }
    semaphore.keys += returned;
    while (!semaphore.waiters.empty()) {
      const auto waiter = semaphore.waiters.front();
      if (synchronization_halted(waiter.process)) {
        semaphore.waiters.pop_front();
        continue;
      }
      if (waiter.keys > semaphore.keys) break;
      semaphore.waiters.pop_front();
      semaphore.keys -= waiter.keys;
      (void)synchronization_wake(waiter.process);
    }
    return Result::completed;
  }

  return Result::other;
}

}  // namespace fsim::runtime::simir
