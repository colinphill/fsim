// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/dpi_marshalling.hpp"
#include "fsim/runtime/dpi_scope.hpp"
#include "fsim/runtime/svdpi_bridge.h"

#include <cstddef>
#include <functional>
#include <memory>
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
  DeclarationMismatch,
  Disabled,
  Exception,
};

enum class SystemVerilogDpiRuntimeQualifier {
  None,
  Pure,
  Context,
};

enum class SystemVerilogDpiRuntimeCallableKind {
  Function,
  Task,
};

struct SystemVerilogDpiRuntimeDeclaration {
  std::string linkage_name;
  SystemVerilogDpiRuntimeCallableKind callable_kind{
      SystemVerilogDpiRuntimeCallableKind::Function};
  SystemVerilogDpiRuntimeQualifier qualifier{
      SystemVerilogDpiRuntimeQualifier::None};
  std::vector<SystemVerilogDpiTransferMode> directions;
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
  void set_call_site(std::string file_name, int line_number);
  void set_simulation_time(
      std::uint64_t ticks, std::int32_t time_unit,
      std::int32_t time_precision) noexcept;

  SystemVerilogDpiCallbackContext(
      const SystemVerilogDpiCallbackContext&) = delete;
  SystemVerilogDpiCallbackContext& operator=(
      const SystemVerilogDpiCallbackContext&) = delete;
  SystemVerilogDpiCallbackContext(
      SystemVerilogDpiCallbackContext&&) = delete;
  SystemVerilogDpiCallbackContext& operator=(
      SystemVerilogDpiCallbackContext&&) = delete;

 private:
  friend class SystemVerilogDpiCallbackRegistry;
  friend class SystemVerilogDpiImportedTaskRegistry;

  [[nodiscard]] SystemVerilogDpiScopeSetResult set_scope(
      std::optional<SystemVerilogDpiScopeHandle> scope) noexcept;
  void begin_dispatch() noexcept;
  [[nodiscard]] bool finish_dispatch() noexcept;
  [[nodiscard]] bool enter_foreign_call() noexcept;
  [[nodiscard]] bool leave_foreign_call() noexcept;

  struct ScopeToken {
    SystemVerilogDpiScopeHandle handle;
    std::string name;
  };
  struct UserDataEntry {
    ScopeToken* scope{};
    void* key{};
    void* value{};
  };

  [[nodiscard]] ScopeToken* token(
      SystemVerilogDpiScopeHandle handle) noexcept;
  [[nodiscard]] ScopeToken* token(svScope scope) noexcept;
  [[nodiscard]] static svScope FSIM_SVDPI_CALL bridge_get_scope(
      void* user_data) noexcept;
  [[nodiscard]] static svScope FSIM_SVDPI_CALL bridge_set_scope(
      void* user_data, svScope scope) noexcept;
  [[nodiscard]] static const char* FSIM_SVDPI_CALL bridge_scope_name(
      void* user_data, svScope scope) noexcept;
  [[nodiscard]] static svScope FSIM_SVDPI_CALL bridge_find_scope(
      void* user_data, const char* name) noexcept;
  [[nodiscard]] static int FSIM_SVDPI_CALL bridge_put_user_data(
      void* user_data, svScope scope, void* key, void* value) noexcept;
  [[nodiscard]] static void* FSIM_SVDPI_CALL bridge_get_user_data(
      void* user_data, svScope scope, void* key) noexcept;
  [[nodiscard]] static int FSIM_SVDPI_CALL bridge_caller_info(
      void* user_data, const char** file_name, int* line_number) noexcept;
  [[nodiscard]] static int FSIM_SVDPI_CALL bridge_disabled(
      void* user_data) noexcept;
  static void FSIM_SVDPI_CALL bridge_ack_disabled(void* user_data) noexcept;
  [[nodiscard]] static int FSIM_SVDPI_CALL bridge_time(
      void* user_data, svScope scope, svTimeVal* time_value) noexcept;
  [[nodiscard]] static int FSIM_SVDPI_CALL bridge_time_unit(
      void* user_data, svScope scope, std::int32_t* exponent) noexcept;
  [[nodiscard]] static int FSIM_SVDPI_CALL bridge_time_precision(
      void* user_data, svScope scope, std::int32_t* exponent) noexcept;

  const SystemVerilogDpiScopeRegistry* scopes_{};
  SystemVerilogDpiScopeContext scope_context_;
  std::vector<std::unique_ptr<ScopeToken>> scope_tokens_;
  std::vector<UserDataEntry> user_data_;
  std::string caller_file_name_;
  int caller_line_number_{};
  std::uint64_t simulation_ticks_{};
  std::int32_t time_unit_{};
  std::int32_t time_precision_{};
  fsim_svdpi_call_context_v3 foreign_context_{};
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
  [[nodiscard]] SystemVerilogDpiCallbackError register_callback(
      SystemVerilogDpiRuntimeDeclaration declaration,
      SystemVerilogDpiScopeHandle scope,
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
