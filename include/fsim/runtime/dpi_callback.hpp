// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/dpi_marshalling.hpp"
#include "fsim/runtime/dpi_scope.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fsim::runtime {

class SystemVerilogDpiImportedTaskRegistry;

enum class SystemVerilogDpiCallbackError {
  None,
  InvalidName,
  DuplicateName,
  UnknownName,
  InvalidScope,
  ArityMismatch,
  DirectionMismatch,
  Disabled,
  Exception,
};

class SystemVerilogDpiCallbackContext final {
 public:
  explicit SystemVerilogDpiCallbackContext(
      const SystemVerilogDpiScopeRegistry& scopes) noexcept;

  [[nodiscard]] std::optional<SystemVerilogDpiScopeHandle> current_scope()
      const noexcept;
  [[nodiscard]] bool is_disabled_state() const noexcept;
  [[nodiscard]] bool acknowledge_disabled_state() noexcept;
  void request_disable() noexcept;

 private:
  friend class SystemVerilogDpiCallbackRegistry;
  friend class SystemVerilogDpiImportedTaskRegistry;

  [[nodiscard]] SystemVerilogDpiScopeSetResult set_scope(
      std::optional<SystemVerilogDpiScopeHandle> scope) noexcept;
  void begin_dispatch() noexcept;
  [[nodiscard]] bool finish_dispatch() noexcept;

  SystemVerilogDpiScopeContext scope_context_;
  bool disabled_{};
  bool disable_acknowledged_{};
};

class SystemVerilogDpiCallbackFrame final {
 public:
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] const std::vector<PackedLogic4>* read(
      std::size_t argument) const noexcept;
  [[nodiscard]] std::vector<PackedLogic4>* writable(
      std::size_t argument) noexcept;
  [[nodiscard]] std::optional<SystemVerilogDpiScopeHandle> current_scope()
      const noexcept;
  [[nodiscard]] bool is_disabled_state() const noexcept;
  [[nodiscard]] bool acknowledge_disabled_state() noexcept;

 private:
  friend class SystemVerilogDpiCallbackRegistry;
  friend class SystemVerilogDpiImportedTaskRegistry;

  SystemVerilogDpiCallbackFrame(
      std::vector<SystemVerilogDpiTransferMode> directions,
      std::vector<std::vector<PackedLogic4>> values,
      SystemVerilogDpiCallbackContext& context);

  std::vector<SystemVerilogDpiTransferMode> directions_;
  std::vector<std::vector<PackedLogic4>> values_;
  SystemVerilogDpiCallbackContext* context_{};
  SystemVerilogDpiCallbackError access_error_{
      SystemVerilogDpiCallbackError::None};
};

using SystemVerilogDpiExportedCallback =
    std::function<void(SystemVerilogDpiCallbackFrame&)>;

struct SystemVerilogDpiCallbackResult {
  std::vector<std::vector<PackedLogic4>> values;
  SystemVerilogDpiCallbackError error{
      SystemVerilogDpiCallbackError::None};
  std::string message;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == SystemVerilogDpiCallbackError::None;
  }
};

class SystemVerilogDpiCallbackRegistry final {
 public:
  explicit SystemVerilogDpiCallbackRegistry(
      const SystemVerilogDpiScopeRegistry& scopes) noexcept;

  [[nodiscard]] SystemVerilogDpiCallbackError register_callback(
      std::string linkage_name,
      SystemVerilogDpiScopeHandle scope,
      std::vector<SystemVerilogDpiTransferMode> directions,
      SystemVerilogDpiExportedCallback callback);
  [[nodiscard]] SystemVerilogDpiCallbackResult dispatch(
      std::string_view linkage_name,
      std::vector<std::vector<PackedLogic4>> arguments,
      SystemVerilogDpiCallbackContext& context) const;

 private:
  struct Entry {
    SystemVerilogDpiScopeHandle scope;
    std::vector<SystemVerilogDpiTransferMode> directions;
    SystemVerilogDpiExportedCallback callback;
  };

  const SystemVerilogDpiScopeRegistry* scopes_{};
  std::unordered_map<std::string, Entry> callbacks_;
};

}  // namespace fsim::runtime
