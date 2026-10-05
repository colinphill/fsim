// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <atomic>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace fsim::runtime {

using SimulationTick = std::uint64_t;
using StableOrder = std::uint64_t;
using RuntimeSignalId = std::uint32_t;

struct SchedulerOrderKey {
  StableOrder order { };
  std::uint64_t sequence { };

  friend auto operator<=>(const SchedulerOrderKey &left,
                          const SchedulerOrderKey &right) noexcept
  {
    if (const auto compared = left.order <=> right.order; compared != 0)
      return compared;
    return left.sequence <=> right.sequence;
  }
  friend bool operator==(const SchedulerOrderKey &left,
                         const SchedulerOrderKey &right) noexcept
  {
    return left.order == right.order && left.sequence == right.sequence;
  }

private:
  const void *owner_ { };
  std::uint64_t epoch_ { };
  SchedulerOrderKey(StableOrder task_order, std::uint64_t task_sequence,
                    const void *owner, std::uint64_t epoch) noexcept
      : order(task_order), sequence(task_sequence), owner_(owner), epoch_(epoch)
  {
  }
  friend class Scheduler;
};

enum class SchedulerPhase : std::uint8_t {
    active = 0,
    inactive = 1,
    update = 2,
    observed = 3,
    reactive = 4,
    re_inactive = 5,
    re_update = 6,
    postponed = 7,
};

[[nodiscard]] const char *phase_name(SchedulerPhase phase) noexcept;

// Return the scheduler region owned by an elaborated process declaration.
// Elaboration guarantees that at most one of these region flags is set.
[[nodiscard]] constexpr SchedulerPhase process_execution_phase(
    const bool observed,
    const bool reactive,
    const bool postponed) noexcept
{
    return postponed ? SchedulerPhase::postponed
        : reactive ? SchedulerPhase::reactive
        : observed ? SchedulerPhase::observed
        : SchedulerPhase::active;
}

struct SchedulerOptions {
  std::uint64_t max_delta_cycles = 100'000;
  std::size_t recent_signal_capacity = 32;
};

enum class SchedulerTraceKind : std::uint8_t {
  task_begin,
  task_end,
  task_failure,
  batch_begin,
  batch_end,
  batch_failure,
  signal_transaction,
  signal_change,
  queue_storage_growth,
};

/// Diagnostic records describe scheduler work and queue storage, not HDL
/// observation.
/// Batch begin/end counts are offered/consumed counts; task_end records identify
/// only the consumed prefix. Queue storage growth records report the new vector
/// capacity in count. No task_begin is emitted for individual members of a
/// native batch. Signal records identify publication, without copying values.
struct SchedulerTraceRecord {
  SchedulerTraceKind kind { };
  SimulationTick time { };
  std::uint64_t delta { };
  std::optional<SchedulerPhase> phase;
  StableOrder order { };
  std::uint64_t sequence { };
  std::size_t count { };
  RuntimeSignalId signal { };
  std::uint64_t systemverilog_round { };
  bool systemverilog { };
  bool end_of_time_slot { };
};

enum class RunStatus {
  completed,
  stopped,
  time_limit,
};

struct RunResult {
  RunStatus status = RunStatus::completed;
  SimulationTick time = 0;
  std::uint64_t delta = 0;
  std::uint64_t callbacks_executed = 0;
  /// Exact INTEGER status supplied by a language-level simulator-control
  /// procedure, when one was supplied. External stop requests leave it empty.
  std::optional<std::int64_t> simulator_status;
};

struct SchedulerBatchResult {
  std::size_t executed { };
  std::exception_ptr failure;
};

/// Optional identity used to compact adjacent runtime-owned SystemVerilog
/// batch entries. A zero field disables compaction for the entry.
struct SchedulerBatchGroupKey {
  std::uint64_t generation { };
  std::uint64_t group { };

  [[nodiscard]] explicit operator bool() const noexcept
  {
    return generation != 0U && group != 0U;
  }

  friend bool operator==(const SchedulerBatchGroupKey&,
      const SchedulerBatchGroupKey&) = default;
};

struct SchedulerBatchCompactionStats {
  std::uint64_t tickets { };
  std::uint64_t members { };
  std::uint64_t entries_elided { };
  /// Ticket callbacks dispatched directly from the compact queue node.
  std::uint64_t direct_dispatches { };
  /// Logical member payloads offered by direct ticket callbacks.
  std::uint64_t direct_members { };
  /// Actual SystemVerilog WorkQueue insertions for readiness-mask tickets.
  std::uint64_t readiness_ticket_queue_insertions { };
  /// Logical ready members retained behind readiness-mask ticket entries.
  std::uint64_t readiness_ticket_members { };
  /// Per-member WorkQueue insertions suppressed by ticket membership.
  std::uint64_t readiness_ticket_members_elided { };
  /// Members that used ordinary batchable entries after ticket capacity declined.
  std::uint64_t readiness_ticket_fallback_members { };
  /// Actual Generic WorkQueue insertions for compact readiness tickets.
  std::uint64_t generic_readiness_ticket_queue_insertions { };
  /// Logical Generic ready members retained behind compact ticket entries.
  std::uint64_t generic_readiness_ticket_members { };
  /// Per-member Generic WorkQueue insertions suppressed by compact membership.
  std::uint64_t generic_readiness_ticket_members_elided { };
  /// Members dispatched through their ordinary lazy Generic fallback.
  std::uint64_t generic_readiness_ticket_fallback_members { };
};

/// Exact ordering metadata for one SystemVerilog batch callback. The entry
/// span is borrowed from Scheduler scratch and remains valid only until that
/// callback returns. Every callback receives a new generation, including a
/// retry after a batch executor consumes only a prefix.
struct SchedulerBatchFrontierEntry {
  StableOrder stable_order { };
  std::uint64_t sequence { };
  std::uint64_t payload { };
};

struct SchedulerBatchFrontier {
  std::uint64_t generation { };
  SimulationTick time { };
  std::uint64_t delta { };
  SchedulerPhase phase { SchedulerPhase::active };
  std::uint64_t systemverilog_round { };
  std::size_t cursor { };
  std::size_t end { };
  std::span<const SchedulerBatchFrontierEntry> tasks;
};

/// Scheduler-owned exact keys for the unconsumed members of one Generic
/// compact-readiness ticket. The span is borrowed and valid only during the
/// matching compact-ticket callback; it includes members beyond a foreign-key
/// cut but grants no permission to execute beyond the offered tasks prefix.
struct SchedulerGenericTicketMember {
  StableOrder order { };
  std::uint64_t sequence { };
    std::uint64_t payload { };
};

/// Exact ordering metadata for one generic Active batch callback. This is a
/// generic-delta frontier: it carries no SystemVerilog round and grants no
/// permission to bypass the caller's projected-write or next-delta contract.
struct SchedulerGenericBatchFrontier {
  std::uint64_t generation { };
  SimulationTick time { };
  std::uint64_t delta { };
  SchedulerPhase phase { SchedulerPhase::active };
  std::size_t cursor { };
  std::size_t end { };
  std::span<const SchedulerBatchFrontierEntry> tasks;
  /// Exact compact ticket identity, present only on a compact-ticket callback.
  SchedulerBatchGroupKey compact_group_key { };
  /// Absolute member cursor into that ticket, before this callback consumes.
  std::size_t ticket_member_offset { };
  /// All unconsumed ticket keys, including any retained suffix after a cut.
  std::span<const SchedulerGenericTicketMember> ticket_members;
};

/// Exact scheduler-issued key for one SystemVerilog task. `valid` is
/// set only when the scheduler can authenticate the target round as well as
/// the queue order key. Valid work scheduled before a time slot starts uses
/// round one; unsupported contexts leave the receipt invalid.
struct SchedulerSystemVerilogKeyReceipt {
  bool valid { };
  SimulationTick time { };
  std::uint64_t delta { };
  std::uint64_t systemverilog_round { };
  SchedulerPhase phase { SchedulerPhase::active };
  StableOrder stable_order { };
  std::uint64_t sequence { };
};

/// Exact scheduler-issued key for one Generic Active readiness task.
/// `valid` is explicit because sequence zero is a valid insertion key.
struct SchedulerGenericKeyReceipt {
  bool valid { };
  SimulationTick time { };
  std::uint64_t delta { };
  SchedulerPhase phase { SchedulerPhase::active };
  StableOrder stable_order { };
  std::uint64_t sequence { };
  std::uint64_t payload { };
};

class Scheduler;
class CancellationSlots;
namespace scheduler_detail {
struct Entry;
struct WorkQueue;
}

namespace detail {

inline constexpr std::size_t scheduler_task_payload_size
    = 4U * sizeof(std::uint64_t);

/// Compact, trivially-copyable task payload for runtime-owned scheduler work.
/// Public users should schedule callbacks with Scheduler::Task instead.
struct SchedulerTaskDescriptor {
  using Invoke = void (*)(Scheduler &, const std::byte *);

  Invoke invoke { };
  std::array<std::byte, scheduler_task_payload_size> payload { };

  void operator()(Scheduler &scheduler) const
  {
    if (invoke == nullptr) {
      throw std::logic_error("cannot invoke an empty scheduler task descriptor");
    }
    invoke(scheduler, payload.data());
  }
};

template <typename Payload,
          void (*Dispatch)(Scheduler &, const Payload &)>
[[nodiscard]] SchedulerTaskDescriptor make_scheduler_task_descriptor(
    const Payload &payload)
{
  static_assert(std::is_trivially_copyable_v<Payload>);
  static_assert(std::is_default_constructible_v<Payload>);
  static_assert(sizeof(Payload) <= scheduler_task_payload_size);

  SchedulerTaskDescriptor descriptor;
  descriptor.invoke = +[](Scheduler &scheduler, const std::byte *bytes) {
    Payload decoded { };
    std::memcpy(&decoded, bytes, sizeof(Payload));
    Dispatch(scheduler, decoded);
  };
  std::memcpy(descriptor.payload.data(), &payload, sizeof(Payload));
  return descriptor;
}

} // namespace detail

/// Reusable storage for one bounded internal SystemVerilog ordered ticket.
/// The scheduler marks it unavailable from reservation through completion of
/// the final member callback, including exceptional completion. Callers must
/// keep it alive until the ticket drains; the queue retains owner_lifetime to
/// enforce that lifetime when storage belongs to a component snapshot.
struct InternalSystemVerilogOrderedTicketMember {
  StableOrder order { };
  std::uint64_t sequence { };
  detail::SchedulerTaskDescriptor task;
};

struct InternalSystemVerilogOrderedTicketStorage {
  [[nodiscard]] bool available() const noexcept { return !active; }
  [[nodiscard]] bool in_use() const noexcept { return active; }
  [[nodiscard]] std::size_t capacity() const noexcept
  {
    return members.capacity();
  }
  /// Scheduler lifecycle hook; external callers must not retire queued storage.
  void retire() noexcept
  {
    members.clear();
    cursor = 0U;
    active = false;
  }

private:
  friend class Scheduler;
  friend struct scheduler_detail::Entry;
  friend struct scheduler_detail::WorkQueue;

  std::vector<InternalSystemVerilogOrderedTicketMember> members;
  std::size_t cursor { };
  bool active { };
};

/// Compact input for a caller-owned ordered scheduler batch. Members are
/// supplied in nondecreasing stable-order sequence; equal keys retain input
/// order. The scheduler assigns insertion sequences during the atomic commit.
struct SystemVerilogCompactBatchMember {
  StableOrder stable_order { };
  std::uint64_t payload { };
};

/// Stable, caller-owned executor for adjacent opt-in scheduler tasks.
/// Implementations consume only a leading payload prefix and contain any
/// exception in the returned result. The executor must outlive queued tasks.
class SchedulerBatchTask {
public:
  virtual ~SchedulerBatchTask() = default;
  [[nodiscard]] virtual SchedulerBatchResult execute(
      Scheduler&, std::span<const std::uint64_t> payloads) = 0;
};

/// Ordered batch whose ordinary fallback descriptor is created only when a
/// scheduler-owned key is actually dispatched outside the batch path.
class SchedulerOrderedBatchTask : public SchedulerBatchTask {
public:
  [[nodiscard]] virtual detail::SchedulerTaskDescriptor
  make_fallback_descriptor(std::uint64_t payload) noexcept = 0;
};

class ScheduledTaskHandle {
public:
  ScheduledTaskHandle() = default;

  [[nodiscard]] explicit operator bool() const noexcept;

private:
  friend class Scheduler;

  explicit ScheduledTaskHandle(
      std::weak_ptr<const CancellationSlots> owner,
      std::size_t slot,
      std::uint64_t generation)
      : owner_(std::move(owner)), slot_(slot), generation_(generation) {}

  std::weak_ptr<const CancellationSlots> owner_;
  std::size_t slot_ { };
  std::uint64_t generation_ { };
};

/// Raised before executing a delta cycle beyond max_delta_cycles.
class DeltaCycleLimitError final : public std::runtime_error {
public:
  DeltaCycleLimitError(SimulationTick time, std::uint64_t limit,
                       std::vector<StableOrder> pending_orders,
                       std::vector<RuntimeSignalId> recent_signals);

  [[nodiscard]] SimulationTick time() const noexcept { return time_; }
  [[nodiscard]] std::uint64_t limit() const noexcept { return limit_; }
  [[nodiscard]] const std::vector<StableOrder> &pending_orders() const noexcept {
    return pending_orders_;
  }
  [[nodiscard]] const std::vector<RuntimeSignalId> &
  recent_signals() const noexcept {
    return recent_signals_;
  }

private:
  SimulationTick time_{};
  std::uint64_t limit_{};
  std::vector<StableOrder> pending_orders_;
  std::vector<RuntimeSignalId> recent_signals_;
};

/// A deterministic, single-thread event scheduler.
///
/// Tasks are ordered first by SchedulerPhase, then by StableOrder, and finally
/// by insertion sequence. A task scheduled into an already-completed phase at
/// the current timestamp is deferred to the next delta cycle. Explicit
/// schedule_next_delta calls always defer by one delta.
class Scheduler {
public:
  using Task = std::function<void(Scheduler &)>;

  /// One ordered task retained as the semantic fallback for a grouped
  /// SystemVerilog ticket member. Reservation leaves it untouched on decline.
  struct SystemVerilogGroupBatchMember {
    StableOrder stable_order { };
    std::uint64_t payload { };
    Task fallback_task;
    detail::SchedulerTaskDescriptor fallback_descriptor;
    /// Optional owner for the batch task and any payload storage it reads.
    /// Kept until this member is consumed, falls back, or is discarded.
    std::shared_ptr<void> batch_owner_lifetime;
  };
  using ReadinessBatchMember = SystemVerilogGroupBatchMember;

  /// A temporary reservation for an internal SystemVerilog Active batch.
  /// Capacity is held without creating visible queue entries or consuming
  /// sequence IDs. Destroying an uncommitted ticket cancels it. The ticket is
  /// scoped to the matching scheduler batch callback and must not outlive or
  /// outmove its Scheduler.
  class InternalSystemVerilogBatchReservation {
  public:
    InternalSystemVerilogBatchReservation() noexcept = default;
    ~InternalSystemVerilogBatchReservation();
    InternalSystemVerilogBatchReservation(
        InternalSystemVerilogBatchReservation&&) noexcept;
    InternalSystemVerilogBatchReservation& operator=(
        InternalSystemVerilogBatchReservation&&) noexcept;
    InternalSystemVerilogBatchReservation(
        const InternalSystemVerilogBatchReservation&) = delete;
    InternalSystemVerilogBatchReservation& operator=(
        const InternalSystemVerilogBatchReservation&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept
    {
      return owner_ != nullptr;
    }

    [[nodiscard]] bool commit(
        std::span<const StableOrder> stable_orders,
        std::span<const detail::SchedulerTaskDescriptor> tasks) noexcept;
    /// Commit the reserved callbacks as one ordered queue entry while
    /// retaining every logical member's original order and sequence. The
    /// ticket keeps owner_lifetime until its last member is consumed or
    /// discarded.
    [[nodiscard]] bool commit_ordered_ticket() noexcept;
    void cancel() noexcept;

  private:
    friend class Scheduler;
    InternalSystemVerilogBatchReservation(
        Scheduler* owner, std::uint64_t reservation_id,
        std::size_t count) noexcept
        : owner_(owner), reservation_id_(reservation_id), count_(count)
    {
    }

    Scheduler* owner_ { };
    std::uint64_t reservation_id_ { };
    std::size_t count_ { };
  };

  /// Invisible preflight for one component-keyed batch ticket. A caller may
  /// hold the reservation while preparing its values, then atomically append
  /// the ordered member entries. Cancellation consumes no sequence IDs. The
  /// reservation borrows its Scheduler: that Scheduler must remain alive and
  /// unmoved until the reservation is committed, canceled, or destroyed.
  /// In-tree callers keep the token within the synchronous scheduler callback.
  class SystemVerilogGroupBatchReservation {
  public:
    SystemVerilogGroupBatchReservation() noexcept = default;
    ~SystemVerilogGroupBatchReservation();
    SystemVerilogGroupBatchReservation(
        SystemVerilogGroupBatchReservation&&) noexcept;
    SystemVerilogGroupBatchReservation& operator=(
        SystemVerilogGroupBatchReservation&&) noexcept;
    SystemVerilogGroupBatchReservation(
        const SystemVerilogGroupBatchReservation&) = delete;
    SystemVerilogGroupBatchReservation& operator=(
        const SystemVerilogGroupBatchReservation&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept
    {
      return owner_ != nullptr;
    }

    /// Return the exact Active round selected for this reserved output batch.
    /// The value is available only while the reservation remains active and
    /// still names the current slot's pending Active queue.
    [[nodiscard]] std::optional<std::uint64_t>
    target_systemverilog_round() const noexcept;

    /// Commit up to the reserved member capacity. An empty commit releases
    /// the invisible reservation without issuing a key or ticket.
    /// Receipts are written in input-member order only after a successful
    /// atomic commit; failed commits clear every output receipt.
    [[nodiscard]] bool commit(
        std::span<SystemVerilogGroupBatchMember> members,
        std::span<SchedulerSystemVerilogKeyReceipt> receipts = { }) noexcept;
    /// Commit key/payload members without constructing per-member scheduler
    /// descriptors. The ordered task and owner stay pinned until retirement.
    /// Optional sequence output receives one fresh sequence per input member
    /// only after a successful commit. It must be empty or match the actual
    /// member count and is untouched on failure. Optional receipts follow
    /// input-member order and are cleared on failure.
    [[nodiscard]] bool commit_compact(
        std::span<const SystemVerilogCompactBatchMember> members,
        SchedulerOrderedBatchTask& ordered_task,
        std::shared_ptr<void> owner_lifetime,
        std::span<std::uint64_t> issued_sequences = { },
        std::span<SchedulerSystemVerilogKeyReceipt> receipts = { }) noexcept;
    void cancel() noexcept;

  private:
    friend class Scheduler;
    SystemVerilogGroupBatchReservation(
        Scheduler* owner, std::uint64_t reservation_id,
        std::size_t count,
        SchedulerOrderedBatchTask* ordered_task,
        std::uint64_t target_systemverilog_round) noexcept
        : owner_(owner), reservation_id_(reservation_id), count_(count),
          ordered_task_(ordered_task),
          target_systemverilog_round_(target_systemverilog_round)
    {
    }

    Scheduler* owner_ { };
    std::uint64_t reservation_id_ { };
    std::size_t count_ { };
    SchedulerOrderedBatchTask* ordered_task_ { };
    std::uint64_t target_systemverilog_round_ { };
  };

  /// A split-phase ticket for output callbacks discovered in one generic
  /// Active batch. Reservation may allocate Update-queue capacity, but creates
  /// no work and consumes no sequence IDs; commit is non-allocating and
  /// publishes the complete callback prefix atomically. An installed scheduler
  /// trace hook declines this ticket so the ordinary fully traced route wins.
  /// Like the SystemVerilog ticket, it is scoped to this callback and must not
  /// outlive or outmove its Scheduler.
  class InternalGenericUpdateBatchReservation {
  public:
    InternalGenericUpdateBatchReservation() noexcept = default;
    ~InternalGenericUpdateBatchReservation();
    InternalGenericUpdateBatchReservation(
        InternalGenericUpdateBatchReservation&&) noexcept;
    InternalGenericUpdateBatchReservation& operator=(
        InternalGenericUpdateBatchReservation&&) noexcept;
    InternalGenericUpdateBatchReservation(
        const InternalGenericUpdateBatchReservation&) = delete;
    InternalGenericUpdateBatchReservation& operator=(
        const InternalGenericUpdateBatchReservation&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept
    {
      return owner_ != nullptr;
    }

    [[nodiscard]] bool commit(
        std::span<const StableOrder> stable_orders,
        std::span<const detail::SchedulerTaskDescriptor> tasks) noexcept;
    void cancel() noexcept;

  private:
    friend class Scheduler;
    InternalGenericUpdateBatchReservation(
        Scheduler* owner, std::uint64_t reservation_id,
        std::size_t count) noexcept
        : owner_(owner), reservation_id_(reservation_id), count_(count)
    {
    }

    Scheduler* owner_ { };
    std::uint64_t reservation_id_ { };
    std::size_t count_ { };
  };
  using SlotStartHook = std::function<void(Scheduler &)>;
  /// Internal runtime hook at a fully drained time-slot boundary. It must not
  /// schedule or inspect a partially executing slot; the context must outlive
  /// this hook registration.
  using RuntimeSlotQuietHook = void (*)(void *) noexcept;
  using SafePointHook = std::function<void(Scheduler &, SchedulerPhase)>;
  using SafePointHookToken = std::uint64_t;
  using DiscardHook = void (*)(void *) noexcept;
  using DiscardHookToken = std::uint64_t;

  explicit Scheduler(SchedulerOptions options = {});
  ~Scheduler();
  /// Moving from a running or releasing scheduler throws std::logic_error.
  Scheduler(Scheduler &&);
  /// An assignment during either scheduler's run or cleanup has no effect.
  Scheduler &operator=(Scheduler &&) noexcept;
  Scheduler(const Scheduler &) = delete;
  Scheduler &operator=(const Scheduler &) = delete;

  void schedule_at(SimulationTick time, SchedulerPhase phase,
                   StableOrder stable_order, Task task);
  void schedule_after(SimulationTick delay, SchedulerPhase phase,
                      StableOrder stable_order, Task task);
  [[nodiscard]] ScheduledTaskHandle
  schedule_after_cancelable(SimulationTick delay, SchedulerPhase phase,
                            StableOrder stable_order, Task task);
  void cancel(const ScheduledTaskHandle &handle) noexcept;
  void schedule(SchedulerPhase phase, StableOrder stable_order, Task task);
  void schedule_next_delta(SchedulerPhase phase, StableOrder stable_order,
                           Task task);
  void schedule_next_delta_batchable(
      SchedulerPhase phase, StableOrder stable_order,
      SchedulerBatchTask& batch_task, std::uint64_t batch_payload,
      Task fallback_task);
  /// Queue one Generic next-delta Active member behind a compact component
  /// ticket. The component count is an admission bound shared by all members;
  /// the scheduler reserves its full compact slab before the first member is
  /// visible. False clears the receipt and consumes no sequence or queue work.
  [[nodiscard]] bool schedule_generic_next_delta_readiness_member(
      SchedulerOrderedBatchTask& ordered_task,
      SchedulerBatchGroupKey group_key,
      std::size_t component_member_count,
      StableOrder stable_order,
      std::uint64_t payload,
      std::shared_ptr<void> owner_lifetime,
      SchedulerGenericKeyReceipt* receipt = nullptr);

  /// IEEE SystemVerilog same-time regions. Each Active, Inactive or NBA batch
  /// is frozen while it executes. Newly enqueued work is considered after that
  /// batch, and Active/Inactive work settles before the next NBA batch.
  /// These rounds do not advance the generic/VHDL delta cycle. Cross-language
  /// callers must explicitly use the generic next-delta boundary first.
  void schedule_systemverilog_at(SimulationTick time, SchedulerPhase phase,
      StableOrder stable_order, Task task);
  void schedule_systemverilog(SchedulerPhase phase,
      StableOrder stable_order, Task task);
  /// Offer only contiguous entries from the same frozen SV region batch.
  /// Declined or partially consumed work keeps its original order and round.
  void schedule_systemverilog_batchable(SchedulerPhase phase,
      StableOrder stable_order, SchedulerBatchTask& batch_task,
      std::uint64_t batch_payload, Task fallback_task);
  /// Runtime-owned opt-in form. Adjacent entries with the same nonempty key
  /// may share one bounded queue ticket while retaining every original key.
  /// A supplied receipt is published only after enqueue succeeds.
  void schedule_systemverilog_group_batchable(SchedulerPhase phase,
      StableOrder stable_order, SchedulerBatchTask& batch_task,
      std::uint64_t batch_payload, Task fallback_task,
      SchedulerBatchGroupKey group_key,
      SchedulerSystemVerilogKeyReceipt* receipt = nullptr);
  /// Queue one logical ready member behind a component ticket when a pending
  /// ticket for this scheduler round exists or can be reserved.
  /// Returns true only when the member was placed in that one-node ticket;
  /// otherwise it enqueues the equivalent ordinary batchable entry. A
  /// supplied receipt identifies whichever task was successfully enqueued.
  [[nodiscard]] bool schedule_systemverilog_readiness_member(
      SchedulerPhase phase, StableOrder stable_order,
      SchedulerBatchTask& batch_task, std::uint64_t batch_payload,
      Task fallback_task, SchedulerBatchGroupKey group_key,
      SchedulerSystemVerilogKeyReceipt* receipt = nullptr);
  /// Atomically place an ordered set of ready members behind one
  /// component ticket. On false, no member task or scheduler sequence is
  /// consumed, all supplied receipts are cleared, and the caller may take its
  /// ordinary per-member route. Successful receipts follow input-member order.
  [[nodiscard]] bool schedule_systemverilog_readiness_group(
      SchedulerPhase phase, SchedulerBatchTask& batch_task,
      SchedulerBatchGroupKey group_key,
      std::span<ReadinessBatchMember> members,
      std::span<SchedulerSystemVerilogKeyReceipt> receipts = { });
  /// Prepare stable backing slots for runtime-certified component tickets.
  /// Scheduler instances retain the historical 64-slot default. A caller may
  /// request a larger pool during fixed-topology setup; false means growth
  /// would relocate storage that may still be referenced by a queue or
  /// reservation. Allocation failure throws without changing the old pool.
  [[nodiscard]] bool prepare_systemverilog_readiness_ticket_capacity(
      std::size_t required_capacity);
  /// Prepare stable ticket slots and reusable queue-entry buffers for
  /// runtime-certified Generic components. Queue buffers cover the requested
  /// capacity and the scheduler's fixed batch-dispatch chunk. Growth is
  /// allowed only when no ticket reservation references the existing pool.
  /// Allocation failure preserves the previous ticket and queue capacity.
  [[nodiscard]] bool prepare_generic_readiness_ticket_capacity(
      std::size_t required_capacity);
  /// Reserve an invisible active-region component ticket with capacity for
  /// member_count entries before result preparation. Commit may publish fewer
  /// actual entries after native execution, without allocating queue storage.
  [[nodiscard]] SystemVerilogGroupBatchReservation
  reserve_systemverilog_group_batch(
      SchedulerPhase phase, SchedulerBatchTask& batch_task,
      SchedulerBatchGroupKey group_key, std::size_t member_count);
  [[nodiscard]] SystemVerilogGroupBatchReservation
  reserve_systemverilog_compact_group_batch(
      SchedulerPhase phase, SchedulerOrderedBatchTask& batch_task,
      SchedulerBatchGroupKey group_key, std::size_t member_count);
  [[nodiscard]] SchedulerBatchCompactionStats
  systemverilog_batch_compaction_stats() const noexcept;
  [[nodiscard]] SchedulerBatchCompactionStats
  generic_batch_compaction_stats() const noexcept;
  void schedule_systemverilog_next_delta(SchedulerPhase phase,
      StableOrder stable_order, Task task);
  void schedule_systemverilog_after(SimulationTick delay, SchedulerPhase phase,
      StableOrder stable_order, Task task);
  [[nodiscard]] ScheduledTaskHandle schedule_systemverilog_after_cancelable(
      SimulationTick delay, SchedulerPhase phase, StableOrder stable_order, Task task);
  void schedule_internal_systemverilog_at(SimulationTick time, SchedulerPhase phase,
      StableOrder stable_order, detail::SchedulerTaskDescriptor task);
  /// Atomically append runtime-owned tasks to the Active queue while handling
  /// the exact scheduler-issued batch frontier. All entry storage and sequence
  /// capacity is reserved before the first task becomes visible. A stale or
  /// non-Active frontier is rejected without consuming task identities.
  void schedule_internal_systemverilog_batch_from_frontier(
      std::uint64_t frontier_generation,
      std::span<const StableOrder> stable_orders,
      std::span<const detail::SchedulerTaskDescriptor> tasks);
  /// Native-only split-phase form. reserve() can allocate but is invisible
  /// and consumes no order/sequence IDs; commit() is non-allocating and
  /// atomically makes the prepared tasks visible. An installed trace hook
  /// returns an empty ticket so the caller can use the fully traced path.
  [[nodiscard]] InternalSystemVerilogBatchReservation
  reserve_internal_systemverilog_batch_from_frontier(
      std::uint64_t frontier_generation, std::size_t task_count);
  /// Reserve one Active WorkQueue slot for a bounded ticket of runtime-owned
  /// tasks. The member spans are copied before returning; owner_lifetime is
  /// retained until the ticket drains. Tickets preserve each member's
  /// insertion sequence and may interleave with foreign queue entries.
  [[nodiscard]] InternalSystemVerilogBatchReservation
  reserve_internal_systemverilog_ordered_ticket_from_frontier(
      std::uint64_t frontier_generation,
      std::span<const StableOrder> stable_orders,
      std::span<const detail::SchedulerTaskDescriptor> tasks,
      InternalSystemVerilogOrderedTicketStorage& storage,
      std::shared_ptr<void> owner_lifetime);
  /// Reserve generic Update callbacks while handling the exact current
  /// generic Active batch. The ticket only reserves queue insertion; callbacks
  /// must retain the original owner and projected-write path so downstream
  /// sensitivity still enters the next generic delta.
  [[nodiscard]] InternalGenericUpdateBatchReservation
  reserve_internal_generic_update_batch_from_frontier(
      std::uint64_t frontier_generation, std::size_t task_count);
  [[nodiscard]] bool trace_hook_installed() const noexcept;
  void schedule_internal_systemverilog(SchedulerPhase phase,
      StableOrder stable_order, detail::SchedulerTaskDescriptor task);
  void schedule_internal_systemverilog_next_delta(SchedulerPhase phase,
      StableOrder stable_order, detail::SchedulerTaskDescriptor task);
  /// Run only after all same-time SV work and generic cycles are quiescent.
  void schedule_end_of_time_slot(StableOrder stable_order, Task task);
  /// Per-slot SV iteration count, separate from delta(); a return to an earlier
  /// or equal region starts another bounded iteration.
  [[nodiscard]] std::uint64_t systemverilog_round() const noexcept;

  /// Reserve the ordering identity of a virtual next-delta task without
  /// placing an entry in the queue. Runtime-owned region frontiers use this
  /// to preserve the order of callbacks they coalesce. A key belongs to this
  /// scheduler and reset epoch. The trusted caller must keep at most one live
  /// entry per key; after canceling that entry, it may reuse the key for a
  /// continuation. Discard/reset also ends all unqueued virtual work.
  [[nodiscard]] SchedulerOrderKey reserve_order_key(StableOrder stable_order);
  void schedule_reserved_next_delta(
      SchedulerPhase phase, SchedulerOrderKey key, Task task);
  [[nodiscard]] ScheduledTaskHandle schedule_reserved_next_delta_cancelable(
      SchedulerPhase phase, SchedulerOrderKey key, Task task);
  void schedule_reserved_current(
      SchedulerPhase phase, SchedulerOrderKey key, Task task);
  [[nodiscard]] ScheduledTaskHandle schedule_reserved_current_cancelable(
      SchedulerPhase phase, SchedulerOrderKey key, Task task);
  /// During an ordinary callback, return the next queued task in this phase.
  /// A batch callback cannot use this because its entries were prefetched.
  [[nodiscard]] std::optional<SchedulerOrderKey>
  next_current_order_key() const;

  /// Schedule compact runtime-owned work without a per-entry callback object.
  /// The descriptor payload must not outlive its owning runtime context.
  void schedule_internal_at(
      SimulationTick time, SchedulerPhase phase, StableOrder stable_order,
      detail::SchedulerTaskDescriptor task);
  void schedule_internal(
      SchedulerPhase phase, StableOrder stable_order,
      detail::SchedulerTaskDescriptor task);
  void schedule_internal_next_delta(
      SchedulerPhase phase, StableOrder stable_order,
      detail::SchedulerTaskDescriptor task);

  /// Record a changed signal for a possible delta-limit diagnostic.
  void note_signal_change(RuntimeSignalId signal);
  /// Record a signal publication, including a transaction with no value change.
  void note_signal_transaction(RuntimeSignalId signal) noexcept;
  /// Borrow a non-throwing diagnostic sink. The context must outlive the hook;
  /// it must not mutate or reenter the scheduler. Passing nullptr disables it.
  /// Queue-storage events may be delivered during a scheduling call. This hook
  /// does not register an HDL observer or demote compiled execution.
  using TraceHook = void (*)(void*, const SchedulerTraceRecord&) noexcept;
  void set_trace_hook(void* context, TraceHook hook) noexcept;

  [[nodiscard]] RunResult
  run(std::optional<SimulationTick> until = std::nullopt);

  void request_stop() noexcept;
  void clear_stop() noexcept;
  [[nodiscard]] bool stop_requested() const noexcept;

  /// Discard all queued work without changing the current simulation time.
  /// This is used after a terminal design stop before scheduling final-only
  /// lifecycle work.
  void discard_pending();
  /// Discard queued work, rewind time/delta identity, and clear a stop request.
  /// Reset is valid only while the scheduler is not running.
  void reset();

  [[nodiscard]] bool has_pending() const noexcept;
  [[nodiscard]] std::optional<SimulationTick>
  next_pending_time() const noexcept;
  [[nodiscard]] bool running() const noexcept;
  /// True only while invoking the hook between two completed scheduler phases.
  [[nodiscard]] bool at_safe_point() const noexcept;
  /// True only while invoking the runtime hook after generic, SV, and
  /// end-of-slot work for the current timestamp has drained.
  [[nodiscard]] bool at_runtime_slot_quiet_point() const noexcept;
  [[nodiscard]] SimulationTick now() const noexcept;
  [[nodiscard]] std::uint64_t delta() const noexcept;
  [[nodiscard]] std::optional<SchedulerPhase> current_phase() const noexcept;
  /// Return the scheduler-owned frozen batch metadata only while executing a
  /// SystemVerilog SchedulerBatchTask callback. The returned task span is
  /// invalidated when that callback returns.
  [[nodiscard]] std::optional<SchedulerBatchFrontier>
  current_batch_frontier() const noexcept;
  /// Return the generic Active batch frontier only while its SchedulerBatchTask
  /// callback is executing. The borrowed task span ends with that callback.
  [[nodiscard]] std::optional<SchedulerGenericBatchFrontier>
  current_generic_batch_frontier() const noexcept;
  /// Monotonic identity changed whenever work enters the phase currently
  /// being executed. Batch executors use it to stop before crossing newly
  /// inserted canonical work.
  [[nodiscard]] std::uint64_t current_phase_revision() const noexcept;

  /// Observe a newly loaded simulation time slot before any phase executes.
  void set_slot_start_hook(SlotStartHook hook);
  /// Install an allocation-free runtime callback for a fully drained slot.
  /// The callback cannot throw and must not schedule or reenter this scheduler.
  void set_runtime_slot_quiet_hook(
      void *context, RuntimeSlotQuietHook hook);
  /// Replace the legacy safe-point hook without changing its dispatch order.
  void set_safe_point_hook(SafePointHook hook);
  /// Add an observer after the legacy hook. Registry changes during dispatch
  /// take effect at the next safe point.
  [[nodiscard]] SafePointHookToken add_safe_point_hook(SafePointHook hook);
  void remove_safe_point_hook(SafePointHookToken token) noexcept;
  /// Observe discarded work after its task objects have been released.
  /// The callback must not change this hook registry while it is invoked.
  [[nodiscard]] DiscardHookToken add_discard_hook(
      void *context, DiscardHook hook);
  void remove_discard_hook(DiscardHookToken token) noexcept;

private:
  [[nodiscard]] bool commit_internal_systemverilog_batch_reservation(
      std::uint64_t reservation_id, std::size_t count,
      std::span<const StableOrder> stable_orders,
      std::span<const detail::SchedulerTaskDescriptor> tasks) noexcept;
  [[nodiscard]] bool commit_internal_systemverilog_ordered_ticket_reservation(
      std::uint64_t reservation_id) noexcept;
  void cancel_internal_systemverilog_batch_reservation(
      std::uint64_t reservation_id) noexcept;
  [[nodiscard]] bool commit_systemverilog_group_batch_reservation(
      std::uint64_t reservation_id,
      std::span<SystemVerilogGroupBatchMember> members,
      std::uint64_t target_systemverilog_round,
      std::span<SchedulerSystemVerilogKeyReceipt> receipts) noexcept;
  [[nodiscard]] bool commit_systemverilog_compact_group_batch_reservation(
      std::uint64_t reservation_id,
      std::span<const SystemVerilogCompactBatchMember> members,
      SchedulerOrderedBatchTask& ordered_task,
      std::shared_ptr<void> owner_lifetime,
      std::span<std::uint64_t> issued_sequences,
      std::uint64_t target_systemverilog_round,
      std::span<SchedulerSystemVerilogKeyReceipt> receipts) noexcept;
  [[nodiscard]] SystemVerilogGroupBatchReservation
  reserve_systemverilog_group_batch_impl(
      SchedulerPhase phase, SchedulerBatchTask& batch_task,
      SchedulerBatchGroupKey group_key, std::size_t member_count,
      SchedulerOrderedBatchTask* ordered_task);
  void cancel_systemverilog_group_batch_reservation(
      std::uint64_t reservation_id) noexcept;
  [[nodiscard]] std::optional<std::uint64_t>
  systemverilog_group_batch_target_round(
      std::uint64_t reservation_id,
      std::uint64_t target_systemverilog_round) const noexcept;
  [[nodiscard]] bool commit_internal_generic_update_batch_reservation(
      std::uint64_t reservation_id, std::size_t count,
      std::span<const StableOrder> stable_orders,
      std::span<const detail::SchedulerTaskDescriptor> tasks) noexcept;
  void cancel_internal_generic_update_batch_reservation(
      std::uint64_t reservation_id) noexcept;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace fsim::runtime
