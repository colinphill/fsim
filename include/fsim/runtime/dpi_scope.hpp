// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fsim::runtime {

enum class SystemVerilogDpiScopeError {
  None,
  InvalidSimulation,
  InvalidName,
  DuplicateName,
  InvalidParent,
  StaleScope,
};

struct SystemVerilogDpiScopeHandle {
  std::uint64_t simulation{};
  std::uint32_t slot{};
  std::uint32_t epoch{};

  friend bool operator==(
      const SystemVerilogDpiScopeHandle&,
      const SystemVerilogDpiScopeHandle&) = default;
};

struct SystemVerilogDpiScopeResult {
  SystemVerilogDpiScopeHandle value;
  SystemVerilogDpiScopeError error{SystemVerilogDpiScopeError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == SystemVerilogDpiScopeError::None;
  }
};

class SystemVerilogDpiScopeRegistry final {
 public:
  explicit SystemVerilogDpiScopeRegistry(std::uint64_t simulation_identity);

  [[nodiscard]] SystemVerilogDpiScopeResult define(
      std::string full_name,
      std::optional<SystemVerilogDpiScopeHandle> parent = std::nullopt);
  [[nodiscard]] bool contains(
      SystemVerilogDpiScopeHandle handle) const noexcept;
  [[nodiscard]] std::optional<SystemVerilogDpiScopeHandle> find(
      std::string_view full_name) const;
  [[nodiscard]] std::optional<std::string> name(
      SystemVerilogDpiScopeHandle handle) const;
  [[nodiscard]] std::optional<SystemVerilogDpiScopeHandle> parent(
      SystemVerilogDpiScopeHandle handle) const noexcept;
  [[nodiscard]] std::uint64_t simulation_identity() const noexcept;

 private:
  struct Scope {
    std::uint32_t epoch{1};
    std::string full_name;
    std::optional<std::uint32_t> parent;
  };

  [[nodiscard]] const Scope* scope(
      SystemVerilogDpiScopeHandle handle) const noexcept;
  [[nodiscard]] SystemVerilogDpiScopeHandle handle(
      std::size_t slot) const noexcept;

  std::uint64_t simulation_identity_{};
  std::vector<Scope> scopes_;
  std::unordered_map<std::string, std::uint32_t> scopes_by_name_;
};

struct SystemVerilogDpiScopeSetResult {
  std::optional<SystemVerilogDpiScopeHandle> previous;
  SystemVerilogDpiScopeError error{SystemVerilogDpiScopeError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == SystemVerilogDpiScopeError::None;
  }
};

// Owned by one scheduler execution context. No process-global or thread-local
// scope is used, so concurrent simulations and callbacks cannot alias state.
class SystemVerilogDpiScopeContext final {
 public:
  explicit SystemVerilogDpiScopeContext(
      const SystemVerilogDpiScopeRegistry& registry) noexcept;

  [[nodiscard]] std::optional<SystemVerilogDpiScopeHandle> current()
      const noexcept;
  [[nodiscard]] SystemVerilogDpiScopeSetResult set(
      std::optional<SystemVerilogDpiScopeHandle> scope) noexcept;

 private:
  const SystemVerilogDpiScopeRegistry* registry_{};
  std::optional<SystemVerilogDpiScopeHandle> current_;
};

}  // namespace fsim::runtime
