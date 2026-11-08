// SPDX-License-Identifier: Apache-2.0
#pragma once

// Scheduler event-kind registry for run-state save/restart (#128; IEEE
// 1800-2023 D.9 $save/$restart, IEEE 1076-2019 20.7-20.9).
//
// A pending scheduler event is a compact descriptor: an `invoke` function
// pointer plus a 32-byte trivially copyable payload (scheduler.hpp). That
// representation cannot be written to a file as is, because the pointer and the
// owner pointers in the payload change from run to run. A kind names one
// (Payload, Dispatch) pair with a stable string and says how its payload is
// encoded without pointers. The registry is consulted only by save_future and
// restore_future: scheduling, dispatch and cancellation never touch it, and a
// descriptor carries nothing extra per event.
//
// Closure events (`Scheduler::Task`) are saved only when they hold a
// registered descriptor (`Task{descriptor}`); any other closure refuses the
// save with a message that names its time, region, phase and order.

#include "fsim/runtime/scheduler.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace fsim::runtime {

/// What a saved event of the kind means to a restart.
enum class SchedulerEventClass : std::uint8_t {
  /// Plain data: the payload is saved and the event is re-created.
  data,
  /// Re-created by the owner's own re-arm (kernel doorbells and the like), so
  /// it is not saved.
  derived,
  /// A user callback of a foreign interface. IEEE 1800-2023 38.9 and IEEE
  /// 1076-2019 20.9 release these at a restart, so they are dropped and
  /// counted.
  foreign_callback,
  /// Embedder state that cannot be written to a file. A pending event of this
  /// kind refuses the save.
  host_closure,
};

[[nodiscard]] const char *scheduler_event_class_name(
    SchedulerEventClass klass) noexcept;

/// The objects a payload may point to, by domain name (for example
/// "interpreter"). A payload pointer is saved as "the owner of this domain"
/// and re-bound to the owner of the restarted run.
class SchedulerEventOwners {
public:
  void bind(std::string_view domain, void *owner);
  [[nodiscard]] void *find(std::string_view domain) const noexcept;

private:
  std::vector<std::pair<std::string, void *>> owners_;
};

/// Little-endian payload writer. After a failure ok() is false and error()
/// says why; later calls do nothing.
class SchedulerEventWriter {
public:
  SchedulerEventWriter(std::vector<std::byte> &out,
                       const SchedulerEventOwners &owners,
                       std::string_view domain) noexcept
      : out_(&out), owners_(&owners), domain_(domain) {}

  void u8(std::uint8_t value) { put(value, 1U); }
  void u32(std::uint32_t value) { put(value, 4U); }
  void u64(std::uint64_t value) { put(value, 8U); }
  void i64(std::int64_t value) { put(static_cast<std::uint64_t>(value), 8U); }
  void boolean(bool value) { put(value ? 1U : 0U, 1U); }
  /// A pointer to the kind's owner. It must be that owner (or null).
  void owner(const void *pointer);
  void fail(std::string message);
  [[nodiscard]] bool ok() const noexcept { return error_.empty(); }
  [[nodiscard]] const std::string &error() const noexcept { return error_; }

private:
  void put(std::uint64_t value, unsigned bytes);

  std::vector<std::byte> *out_;
  const SchedulerEventOwners *owners_;
  std::string_view domain_;
  std::string error_;
};

class SchedulerEventReader {
public:
  SchedulerEventReader(std::span<const std::byte> in,
                       const SchedulerEventOwners &owners,
                       std::string_view domain) noexcept
      : in_(in), owners_(&owners), domain_(domain) {}

  [[nodiscard]] std::uint8_t u8() { return static_cast<std::uint8_t>(get(1U)); }
  [[nodiscard]] std::uint32_t u32() {
    return static_cast<std::uint32_t>(get(4U));
  }
  [[nodiscard]] std::uint64_t u64() { return get(8U); }
  [[nodiscard]] std::int64_t i64() {
    return static_cast<std::int64_t>(get(8U));
  }
  [[nodiscard]] bool boolean() { return get(1U) != 0U; }
  template <typename T> [[nodiscard]] T *owner() {
    return static_cast<T *>(find_owner());
  }
  void fail(std::string message);
  [[nodiscard]] bool ok() const noexcept { return error_.empty(); }
  /// True when every byte was consumed, so a payload with trailing bytes (a
  /// schema mismatch) is rejected.
  [[nodiscard]] bool exhausted() const noexcept {
    return position_ == in_.size();
  }
  [[nodiscard]] const std::string &error() const noexcept { return error_; }

private:
  [[nodiscard]] std::uint64_t get(unsigned bytes);
  [[nodiscard]] void *find_owner();

  std::span<const std::byte> in_;
  std::size_t position_ { };
  const SchedulerEventOwners *owners_;
  std::string_view domain_;
  std::string error_;
};

/// One registered kind, with its codec reduced to byte-level functions.
struct SchedulerEventKind {
  std::string name;
  SchedulerEventClass klass { SchedulerEventClass::data };
  detail::SchedulerTaskDescriptor::Invoke invoke { };
  /// Owner domain of the pointers in the payload ("" when it has none).
  std::string owner_domain;
  bool (*encode)(const std::byte *payload, SchedulerEventWriter &) { };
  bool (*decode)(SchedulerEventReader &, std::byte *payload) { };
  /// Non-null for a cancelable kind: the key an owner uses to find the
  /// handle again (IEEE 1800-2023 9.6.2 disable cancels such events).
  bool (*cancel_key)(const std::byte *payload, std::uint64_t &key) { };
};

/// Adds a kind. A duplicate name or invoke pointer is a programming error and
/// aborts at start-up with a message.
void add_scheduler_event_kind(SchedulerEventKind kind);
[[nodiscard]] const SchedulerEventKind *
find_scheduler_event_kind(std::string_view name) noexcept;
[[nodiscard]] const SchedulerEventKind *find_scheduler_event_kind(
    detail::SchedulerTaskDescriptor::Invoke invoke) noexcept;
/// Every registered kind, in registration order (for tests and audits).
[[nodiscard]] std::vector<const SchedulerEventKind *>
scheduler_event_kinds();

/// A scheduler closure that belongs to the embedding program (a testbench
/// hook, a native callback). It cannot be written to a file, so a pending one
/// refuses the save with FSIM-SAVE-0004 and `what` names it. Use it for a
/// closure that can wait in the queue of a future time; a closure that only
/// lives inside one time slot needs no wrapper, because the save waits for the
/// slot to drain (SchedulerSaveResult::deferrable).
struct SchedulerHostClosure {
  const char *what { "embedder closure" };
  Scheduler::Task inner;
  void operator()(Scheduler &scheduler) const { inner(scheduler); }
};

/// A user callback of a foreign interface (VPI, VHPI, PLI). IEEE 1800-2023
/// 38.9 and IEEE 1076-2019 20.9 release these at a restart, so a save drops
/// the pending event and counts it (SchedulerSaveResult::foreign_dropped).
struct SchedulerForeignCallback {
  Scheduler::Task inner;
  void operator()(Scheduler &scheduler) const { inner(scheduler); }
};

template <typename Function>
[[nodiscard]] Scheduler::Task host_closure_task(const char *what,
                                                Function &&function) {
  return Scheduler::Task { SchedulerHostClosure {
      what, Scheduler::Task { std::forward<Function>(function) } } };
}

template <typename Function>
[[nodiscard]] Scheduler::Task foreign_callback_task(Function &&function) {
  return Scheduler::Task { SchedulerForeignCallback {
      Scheduler::Task { std::forward<Function>(function) } } };
}

/// Registers `invoke` as a derived kind the first time it is called (the
/// owner re-arms the event on restore, so a save leaves it out). For kinds
/// whose dispatch is a local lambda at the scheduling site: the site calls
/// this once with the descriptor it is about to schedule.
void register_derived_scheduler_event_kind(
    std::string_view name, detail::SchedulerTaskDescriptor::Invoke invoke);

struct SchedulerEventKindRegistration { };

/// A codec for a payload with no pointers: the bytes are saved as they are.
template <typename Payload> struct PlainSchedulerEventCodec {
  static constexpr std::string_view owner_domain { };
  static void encode(const Payload &payload, SchedulerEventWriter &writer) {
    for (const auto byte :
         std::as_bytes(std::span<const Payload, 1>(&payload, 1U))) {
      writer.u8(static_cast<std::uint8_t>(byte));
    }
  }
  static void decode(SchedulerEventReader &reader, Payload &payload) {
    std::array<std::byte, sizeof(Payload)> bytes { };
    for (auto &byte : bytes) {
      byte = static_cast<std::byte>(reader.u8());
    }
    std::memcpy(&payload, bytes.data(), sizeof(Payload));
  }
};

namespace detail {

template <typename T>
void put_scheduler_event_field(SchedulerEventWriter &writer, const T &value) {
  if constexpr (std::is_same_v<T, bool>) {
    writer.boolean(value);
  } else if constexpr (std::is_enum_v<T>) {
    writer.u64(static_cast<std::uint64_t>(
        static_cast<std::underlying_type_t<T>>(value)));
  } else if constexpr (std::is_signed_v<T>) {
    writer.i64(static_cast<std::int64_t>(value));
  } else {
    static_assert(std::is_unsigned_v<T>, "unsupported event payload field");
    writer.u64(static_cast<std::uint64_t>(value));
  }
}

template <typename T>
[[nodiscard]] T get_scheduler_event_field(SchedulerEventReader &reader) {
  if constexpr (std::is_same_v<T, bool>) {
    return reader.boolean();
  } else if constexpr (std::is_enum_v<T>) {
    return static_cast<T>(static_cast<std::underlying_type_t<T>>(reader.u64()));
  } else if constexpr (std::is_signed_v<T>) {
    return static_cast<T>(reader.i64());
  } else {
    return static_cast<T>(reader.u64());
  }
}

} // namespace detail

/// A codec for a payload that is one owner pointer plus integer, enum and bool
/// fields. `Domain::name` names the owner domain; the fields are saved in the
/// order given.
template <typename Payload, typename Owner, typename Domain,
          Owner *Payload::*OwnerMember, auto... Fields>
struct OwnedFieldsSchedulerEventCodec {
  static constexpr std::string_view owner_domain { Domain::name };
  static void encode(const Payload &payload, SchedulerEventWriter &writer) {
    writer.owner(payload.*OwnerMember);
    (detail::put_scheduler_event_field(writer, payload.*Fields), ...);
  }
  static void decode(SchedulerEventReader &reader, Payload &payload) {
    payload.*OwnerMember = reader.template owner<Owner>();
    ((payload.*Fields = detail::get_scheduler_event_field<
          std::remove_cvref_t<decltype(payload.*Fields)>>(reader)),
     ...);
  }
};

/// Registers the kind of the descriptor
/// `detail::make_scheduler_task_descriptor<Payload, Dispatch>`.
///
/// `Codec` provides `static constexpr std::string_view owner_domain`,
/// `static void encode(const Payload&, SchedulerEventWriter&)` and
/// `static void decode(SchedulerEventReader&, Payload&)`; a cancelable kind
/// also provides `static std::uint64_t cancel_key(const Payload&)`.
template <typename Payload,
          void (*Dispatch)(Scheduler &, const Payload &), typename Codec>
SchedulerEventKindRegistration register_scheduler_event_kind(
    std::string_view name, SchedulerEventClass klass) {
  SchedulerEventKind kind;
  kind.name = std::string(name);
  kind.klass = klass;
  kind.invoke =
      detail::make_scheduler_task_descriptor<Payload, Dispatch>(Payload { })
          .invoke;
  kind.owner_domain = std::string(Codec::owner_domain);
  kind.encode = +[](const std::byte *bytes, SchedulerEventWriter &writer) {
    Payload payload { };
    std::memcpy(&payload, bytes, sizeof(Payload));
    Codec::encode(payload, writer);
    return writer.ok();
  };
  kind.decode = +[](SchedulerEventReader &reader, std::byte *bytes) {
    Payload payload { };
    Codec::decode(reader, payload);
    if (!reader.ok()) {
      return false;
    }
    std::memcpy(bytes, &payload, sizeof(Payload));
    return true;
  };
  if constexpr (requires(const Payload &payload) {
                  { Codec::cancel_key(payload) }
                  -> std::convertible_to<std::uint64_t>;
                }) {
    kind.cancel_key = +[](const std::byte *bytes, std::uint64_t &key) {
      Payload payload { };
      std::memcpy(&payload, bytes, sizeof(Payload));
      key = Codec::cancel_key(payload);
      return true;
    };
  }
  add_scheduler_event_kind(std::move(kind));
  return { };
}

/// Which queue of a time slot an event waits in.
enum class SchedulerSavedRegion : std::uint8_t {
  generic = 0,
  systemverilog = 1,
  end_of_slot = 2,
};

struct SchedulerSavedEvent {
  SimulationTick time { };
  SchedulerSavedRegion region { SchedulerSavedRegion::generic };
  SchedulerPhase phase { SchedulerPhase::active };
  StableOrder order { };
  std::uint64_t sequence { };
  /// Kind name (SchedulerEventKind::name).
  std::string kind;
  bool cancelable { };
  std::uint64_t cancel_key { };
  std::vector<std::byte> payload;
};

struct SchedulerSavedState {
  SimulationTick now { };
  std::uint64_t cycles { };
  std::uint64_t callbacks { };
  std::uint64_t next_sequence { };
  /// The delta cycle of a cycle-end save. Informational: a restart resumes the
  /// time slot at its first delta cycle.
  std::uint64_t delta { };
  std::vector<SchedulerSavedEvent> events;
};

struct SchedulerSaveResult {
  bool ok { };
  SchedulerSavedState state;
  /// Why the save was refused (one entry per offending event).
  std::vector<std::string> refusals;
  /// Events of `derived` kinds left out (the owner re-arms them).
  std::size_t derived { };
  /// Foreign callbacks dropped (IEEE 1800-2023 38.9, IEEE 1076-2019 20.9).
  std::size_t foreign_dropped { };
  /// True when every refusal is about work of the current time slot (FSIM-SAVE
  /// codes in the message). That work runs before the slot ends, so the
  /// request waits for the next quiet point instead of failing (the owner's
  /// rule: a save waits until nothing is pending at the first point of the
  /// slot).
  bool deferrable { };
};

struct SchedulerRestoreResult {
  bool ok { };
  std::vector<std::string> errors;
  std::size_t restored { };
};

/// Byte form of a saved state, for the run-state image sections.
[[nodiscard]] std::vector<std::byte>
serialize_scheduler_state(const SchedulerSavedState &state);
/// Fails (empty result, `error` set) on a bad magic, truncation or trailing
/// bytes.
[[nodiscard]] bool deserialize_scheduler_state(
    std::span<const std::byte> bytes, SchedulerSavedState &state,
    std::string &error);

} // namespace fsim::runtime
