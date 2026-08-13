// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "fsim/api.h"

#include "fsim/app/application.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/project/project.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fsim::api::detail {

constexpr std::uint32_t kRootPayload = 1;
constexpr std::uint32_t kScopePayloadBase = 2;
constexpr std::uint32_t kScopeIndexMask = UINT32_C(0x0fffffff);
constexpr std::uint32_t kSystemCObjectPayload = UINT32_C(0x10000000);
constexpr std::uint32_t kSystemCObjectIndexMask = UINT32_C(0x0fffffff);
constexpr std::uint32_t kDriverPayload = UINT32_C(0x20000000);
constexpr std::uint32_t kDriverIndexMask = UINT32_C(0x1fffffff);
constexpr std::uint32_t kSignalPayload = UINT32_C(0x40000000);
constexpr std::uint32_t kProcessPayload = UINT32_C(0x80000000);
constexpr std::uint32_t kVariablePayload = UINT32_C(0xc0000000);
constexpr std::uint32_t kObjectIndexMask = UINT32_C(0x3fffffff);

struct VariableObject {
  std::size_t process{};
  std::size_t local{};
  std::optional<std::size_t> parent_scope;
  std::string full_name;
};

struct DriverObject {
  fsim::runtime::simir::SignalId signal{};
  std::size_t process{};
  std::string full_name;
};

enum class ScopeObjectKind : std::uint8_t {
  instance,
  generate,
  lexical,
};

struct ScopeObject {
  ScopeObjectKind kind{ScopeObjectKind::lexical};
  std::optional<std::size_t> process;
  std::optional<std::size_t> parent_scope;
  std::string full_name;
  std::string type_name;
  fsim::runtime::simir::SourceLocation source;
  std::optional<std::size_t> systemc_object;
};

struct DesignSourceLocation {
  std::string_view path;
  std::uint32_t line{1};
  std::uint32_t column{1};
};

inline bool is_design_root(
    const fsim::semantic::design::DesignIr& design,
    const std::string_view path) {
  return std::ranges::find(design.roots(), path) != design.roots().end();
}

inline bool has_synthetic_root(
    const fsim::semantic::design::DesignIr& design) noexcept {
  return design.roots().size() > 1;
}

struct Session {
  // Callbacks execute synchronously on the simulation thread and may perform
  // read-only API queries, so same-thread re-entry must not deadlock.
  std::recursive_mutex mutex;
  fsim_session_t handle{FSIM_INVALID_SESSION};
  fsim::diagnostic::Engine diagnostics;
  std::optional<fsim::project::Config> project;
  std::unique_ptr<fsim::app::Simulation> simulation;
  fsim_callbacks_t callbacks{};
  std::atomic_bool stop_requested{false};
  std::atomic_bool external_stop_seen{false};
  std::atomic_bool running{false};
  std::atomic_bool destroyed{false};
  std::atomic_uint32_t callback_depth{0};
  std::uint32_t design_generation{1};
  std::uint64_t max_deltas{100'000};
  std::uint64_t seed{1};
  bool seed_override{};
  std::optional<fsim::runtime::simir::ProcessId>
      current_execution_process;
  std::vector<ScopeObject> scopes;
  std::vector<std::string> process_names;
  std::vector<VariableObject> variables;
  std::vector<DriverObject> drivers;
  std::optional<fsim::app::VerilogScopeProvenance> query_provenance;
  std::vector<std::optional<std::size_t>> systemc_scope_by_object;
  std::vector<std::optional<std::size_t>> systemc_object_by_process;
  // Legacy SystemC object ordinals are retained solely as the v1 handle ABI
  // adapter. Public identity and hierarchy metadata come from DesignIR IDs.
  std::vector<std::optional<fsim::semantic::design::ObjectId>>
      systemc_design_object_by_object;
  std::vector<std::optional<fsim::semantic::design::ProcessOccurrenceId>>
      systemc_design_process_by_object;
  bool finished{};
};

struct CallbackGuard {
  explicit CallbackGuard(Session& value);
  ~CallbackGuard();

  Session& session;
};

extern std::mutex registry_mutex;
extern std::unordered_map<fsim_session_t, std::shared_ptr<Session>> sessions;
extern std::atomic_uint64_t next_session;

std::shared_ptr<Session> find_session(const fsim_session_t handle);

fsim_string_view_t view(const std::string_view value) noexcept;

fsim_severity_t convert_severity(
    const fsim::diagnostic::Severity severity) noexcept;

fsim_safe_point_kind_t convert_safe_point_kind(
    const fsim::runtime::simir::ExecutionPointKind kind) noexcept;

fsim_scheduler_phase_t convert_scheduler_phase(
    const fsim::runtime::SchedulerPhase phase) noexcept;

bool valid_struct_header(
    const std::uint32_t struct_size,
    const std::uint32_t api_version,
    const std::size_t required_size) noexcept;

bool struct_contains(
    const std::uint32_t struct_size,
    const std::size_t offset,
    const std::size_t field_size) noexcept;

#define FSIM_STRUCT_CONTAINS(struct_size, type, member) \
  struct_contains(                                      \
      (struct_size), offsetof(type, member),            \
      sizeof((static_cast<type*>(nullptr))->member))

fsim_status_t with_session(
    const fsim_session_t handle,
    const std::function<fsim_status_t(Session&)>& function) noexcept;

bool ready(Session& session, const std::string_view operation);

void advance_design_generation(Session& session) noexcept;

fsim_object_t object_handle(
    const Session& session,
    const std::uint32_t payload) noexcept;

bool current_object(
    const Session& session,
    const fsim_object_t object) noexcept;

std::uint32_t object_payload(const fsim_object_t object) noexcept;

fsim_object_t systemc_object_handle(
    const Session& session, std::size_t object);

std::optional<std::size_t> object_systemc(
    const Session& session, fsim_object_t object);

const fsim::semantic::design::Object* design_systemc_object(
    const Session& session, std::size_t object) noexcept;

const fsim::semantic::design::ProcessOccurrence* design_systemc_process(
    const Session& session, std::size_t object) noexcept;

std::optional<std::size_t> systemc_adapter_for_object(
    const Session& session,
    fsim::semantic::design::ObjectId object) noexcept;

std::optional<std::size_t> systemc_adapter_for_process(
    const Session& session,
    fsim::semantic::design::ProcessOccurrenceId process) noexcept;

const fsim::semantic::design::Object* design_signal(
    const Session& session,
    fsim::runtime::simir::SignalId signal) noexcept;

const fsim::semantic::design::ProcessOccurrence* design_process(
    const Session& session, std::size_t process) noexcept;

DesignSourceLocation design_source(
    const Session& session,
    std::optional<fsim::semantic::SourceSpanId> source);

fsim_object_t debug_systemc_object_handle(
    const Session& session, std::size_t object);

fsim_object_t root_handle(const Session& session) noexcept;

fsim_object_t signal_handle(
    const Session& session,
    const fsim::runtime::simir::SignalId signal);

std::optional<fsim::runtime::simir::SignalId> object_signal(
    const Session& session,
    const fsim_object_t object);

fsim_object_t process_handle(const Session& session, const std::size_t process);

std::optional<std::size_t> object_process(
    const Session& session,
    const fsim_object_t object);

fsim_object_t variable_handle(
    const Session& session, const std::size_t variable);

std::optional<std::size_t> object_variable(
    const Session& session,
    const fsim_object_t object);

fsim_object_t scope_handle(
    const Session& session, const std::size_t scope);

std::optional<std::size_t> object_scope(
    const Session& session,
    const fsim_object_t object);

fsim_object_t driver_handle(
    const Session& session, const std::size_t driver);

std::optional<std::size_t> object_driver(
    const Session& session,
    const fsim_object_t object);

std::optional<std::size_t> ensure_scope(
    Session& session,
    const std::size_t process,
    const std::optional<std::size_t> parent_scope,
    const std::string& full_name,
    const fsim::runtime::simir::SourceLocation& source);

bool path_is_within(
    const std::string_view path,
    const std::string_view scope) noexcept;

std::optional<std::size_t> owning_design_scope(
    const Session& session, const std::string_view path);

std::optional<std::size_t> append_design_scope(
    Session& session,
    const ScopeObjectKind kind,
    const std::optional<std::size_t> parent_scope,
    std::string full_name,
    std::string type_name,
    const std::string& source);

bool rebuild_debug_objects(Session& session);

std::string_view leaf_name(const std::string_view name);

bool variable_initialized(
    Session& session, const VariableObject& variable);

bool scope_entered(Session& session, const ScopeObject& scope);

void lifecycle(Session& session, const fsim_lifecycle_event_t event);

void invoke_safe_point(
    Session& session,
    fsim::runtime::Scheduler& scheduler,
    const fsim_object_t process = FSIM_INVALID_OBJECT,
    const fsim::runtime::simir::ExecutionPoint* point = nullptr,
    const std::optional<fsim::runtime::SchedulerPhase> phase =
        std::nullopt);

void attach_callbacks(Session& session);

fsim_status_t runtime_failure(Session& session, const std::exception& error);

fsim::diagnostic::Severity assertion_severity(
    const fsim::runtime::simir::AssertionSeverity severity) noexcept;

fsim_status_t assertion_failure(
    Session& session,
    const fsim::runtime::simir::AssertionError& error);

bool mutation_forbidden(const Session& session) noexcept;

fsim_status_t run_session(
    Session& session,
    const std::optional<fsim::runtime::SimulationTick> until);


} // namespace fsim::api::detail
