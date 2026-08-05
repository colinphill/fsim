// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime {

using SystemVerilogChandle = std::uint64_t;

struct SystemVerilogChandleDescriptor {
  std::string type_identity;
  std::string debug_label;
  // The cleanup closure may capture host state, but that state is never
  // returned through HDL, debugger, callback, trace, or snapshot surfaces.
  std::function<void()> cleanup;
};

struct SystemVerilogChandleSnapshot {
  SystemVerilogChandle handle{};
  std::string type_identity;
  std::string debug_label;
  std::uint64_t alias_transfers{};
};

enum class SystemVerilogChandleEventKind : std::uint8_t {
  Created,
  Aliased,
  Released,
};

struct SystemVerilogChandleEvent {
  SystemVerilogChandleEventKind kind{
      SystemVerilogChandleEventKind::Created};
  SystemVerilogChandleSnapshot value;
};

struct SystemVerilogChandleLimits {
  std::size_t maximum_live_handles{
      std::numeric_limits<std::size_t>::max()};
  std::size_t maximum_storage_bytes{
      std::numeric_limits<std::size_t>::max()};
  std::size_t maximum_observers{
      std::numeric_limits<std::size_t>::max()};
};

/// Scheduler-owned registry for opaque foreign identities.
///
/// Zero is null. Nonzero HDL-visible values encode only a dense registry slot
/// and generation, never a host address. Alias transfer is non-owning and
/// returns the same identity. Explicit release, clear, or registry destruction
/// ends the lifetime and makes every alias stale; cleanup executes once.
class SystemVerilogChandleRegistry final {
 public:
  using Observer = std::function<void(const SystemVerilogChandleEvent&)>;

  explicit SystemVerilogChandleRegistry(
      SystemVerilogChandleLimits limits = {});
  ~SystemVerilogChandleRegistry() noexcept;

  SystemVerilogChandleRegistry(const SystemVerilogChandleRegistry&) = delete;
  SystemVerilogChandleRegistry& operator=(
      const SystemVerilogChandleRegistry&) = delete;
  SystemVerilogChandleRegistry(SystemVerilogChandleRegistry&&) = delete;
  SystemVerilogChandleRegistry& operator=(
      SystemVerilogChandleRegistry&&) = delete;

  [[nodiscard]] SystemVerilogChandle create(
      SystemVerilogChandleDescriptor descriptor);
  [[nodiscard]] SystemVerilogChandle alias(SystemVerilogChandle handle);
  [[nodiscard]] bool release(SystemVerilogChandle handle);
  void clear();

  [[nodiscard]] bool contains(SystemVerilogChandle handle) const noexcept;
  [[nodiscard]] SystemVerilogChandleSnapshot inspect(
      SystemVerilogChandle handle) const;
  [[nodiscard]] std::vector<SystemVerilogChandleSnapshot>
  snapshots() const;
  [[nodiscard]] std::string format(SystemVerilogChandle handle) const;

  [[nodiscard]] std::uint64_t add_observer(Observer observer);
  [[nodiscard]] bool remove_observer(std::uint64_t token) noexcept;

  [[nodiscard]] static bool equal(
      const SystemVerilogChandle left,
      const SystemVerilogChandle right) noexcept {
    return left == right;
  }
  [[nodiscard]] std::size_t live_handles() const noexcept {
    return live_handles_;
  }
  [[nodiscard]] std::size_t storage_bytes() const noexcept {
    return storage_bytes_;
  }
  [[nodiscard]] const SystemVerilogChandleLimits& limits() const noexcept {
    return limits_;
  }

 private:
  struct Slot {
    std::uint32_t generation{1};
    bool occupied{};
    std::string type_identity;
    std::string debug_label;
    std::uint64_t alias_transfers{};
    std::size_t accounted_bytes{};
    std::function<void()> cleanup;
  };

  [[nodiscard]] static SystemVerilogChandle encode(
      std::uint32_t slot,
      std::uint32_t generation) noexcept;
  [[nodiscard]] static std::uint32_t slot_of(
      SystemVerilogChandle handle) noexcept;
  [[nodiscard]] static std::uint32_t generation_of(
      SystemVerilogChandle handle) noexcept;
  [[nodiscard]] const Slot* find(SystemVerilogChandle handle) const noexcept;
  [[nodiscard]] Slot* find(SystemVerilogChandle handle) noexcept;
  [[nodiscard]] SystemVerilogChandleSnapshot snapshot(
      std::uint32_t slot,
      const Slot& value) const;
  void notify(const SystemVerilogChandleEvent& event) noexcept;
  void destroy_without_notification(Slot& slot) noexcept;

  SystemVerilogChandleLimits limits_;
  std::vector<Slot> slots_;
  std::set<std::uint32_t> free_slots_;
  std::size_t live_handles_{};
  std::size_t storage_bytes_{};
  std::map<std::uint64_t, Observer> observers_;
  std::uint64_t next_observer_{1};
};

}  // namespace fsim::runtime
