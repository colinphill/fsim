// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/uvm_object.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace fsim::runtime {

class SystemVerilogUvmComponentService;

enum class SystemVerilogUvmReportSeverity : std::uint8_t {
  Info,
  Warning,
  Error,
  Fatal,
};

enum class SystemVerilogUvmReportAction : std::uint32_t {
  None = 0,
  Display = 1U << 0U,
  Log = 1U << 1U,
  Count = 1U << 2U,
  Exit = 1U << 3U,
  CallHook = 1U << 4U,
  Stop = 1U << 5U,
  Record = 1U << 6U,
};

[[nodiscard]] constexpr SystemVerilogUvmReportAction operator|(
    const SystemVerilogUvmReportAction left,
    const SystemVerilogUvmReportAction right) noexcept {
  return static_cast<SystemVerilogUvmReportAction>(
      static_cast<std::uint32_t>(left)
      | static_cast<std::uint32_t>(right));
}

[[nodiscard]] constexpr bool has_action(
    const SystemVerilogUvmReportAction value,
    const SystemVerilogUvmReportAction action) noexcept {
  return (static_cast<std::uint32_t>(value)
          & static_cast<std::uint32_t>(action))
      != 0;
}

inline constexpr SystemVerilogUvmReportAction
    kSystemVerilogUvmReportElementDefaultAction =
        SystemVerilogUvmReportAction::Log
        | SystemVerilogUvmReportAction::Record;

enum class SystemVerilogUvmReportElementKind : std::uint8_t {
  Integer,
  String,
  Object,
};

enum class SystemVerilogUvmReportRadix : std::uint8_t {
  Binary,
  Octal,
  Decimal,
  Unsigned,
  Hexadecimal,
};

struct SystemVerilogUvmReportElementLimits {
  std::size_t max_elements{1'024};
  std::size_t max_name_bytes{4'096};
  std::size_t max_string_bytes{1U * 1'024U * 1'024U};
  std::size_t max_packed_width{4'096};
  std::size_t max_total_bytes{16U * 1'024U * 1'024U};
};

using SystemVerilogUvmReportElementValue = std::variant<
    PackedLogic4,
    std::string,
    SystemVerilogClassHandle>;

struct SystemVerilogUvmReportElement {
  SystemVerilogUvmReportElementKind kind{
      SystemVerilogUvmReportElementKind::Integer};
  std::string name;
  SystemVerilogUvmReportElementValue value{PackedLogic4{}};
  std::size_t size{};
  SystemVerilogUvmReportRadix radix{
      SystemVerilogUvmReportRadix::Binary};
  SystemVerilogUvmReportAction action{
      kSystemVerilogUvmReportElementDefaultAction};
};

class SystemVerilogUvmReportElementContainer final {
 public:
  explicit SystemVerilogUvmReportElementContainer(
      SystemVerilogUvmReportElementLimits limits = {});

  void add_integer(
      std::string name,
      PackedLogic4 value,
      std::size_t size,
      SystemVerilogUvmReportRadix radix,
      SystemVerilogUvmReportAction action =
          kSystemVerilogUvmReportElementDefaultAction);
  void add_string(
      std::string name,
      std::string value,
      SystemVerilogUvmReportAction action =
          kSystemVerilogUvmReportElementDefaultAction);
  void add_object(
      std::string name,
      SystemVerilogClassHandle object,
      SystemVerilogUvmReportAction action =
          kSystemVerilogUvmReportElementDefaultAction);
  void erase(std::size_t index);
  void clear() noexcept;

  [[nodiscard]] const std::vector<SystemVerilogUvmReportElement>&
  elements() const noexcept { return elements_; }
  [[nodiscard]] std::size_t size() const noexcept {
    return elements_.size();
  }
  [[nodiscard]] std::size_t total_bytes() const noexcept {
    return total_bytes_;
  }
  [[nodiscard]] const SystemVerilogUvmReportElementLimits&
  limits() const noexcept { return limits_; }

 private:
  void append(SystemVerilogUvmReportElement element);
  [[nodiscard]] std::size_t storage_bytes(
      const SystemVerilogUvmReportElement& element) const;
  void validate_action(SystemVerilogUvmReportAction action) const;

  SystemVerilogUvmReportElementLimits limits_;
  std::vector<SystemVerilogUvmReportElement> elements_;
  std::size_t total_bytes_{};
};

struct SystemVerilogUvmReportRequest {
  SystemVerilogClassHandle report_object{};
  SystemVerilogUvmReportSeverity severity{
      SystemVerilogUvmReportSeverity::Info};
  std::string id;
  std::string message;
  std::optional<std::int32_t> verbosity;
  std::string filename;
  std::uint32_t line{};
  std::uint64_t timestamp{};
  std::string context;
  bool report_enabled_checked{};
  SystemVerilogUvmReportElementContainer elements;
};

struct SystemVerilogUvmReportMessage {
  std::uint64_t sequence{};
  SystemVerilogClassHandle report_object{};
  std::string report_object_name;
  SystemVerilogUvmReportSeverity severity{
      SystemVerilogUvmReportSeverity::Info};
  std::string id;
  std::string message;
  std::int32_t verbosity{};
  std::string filename;
  std::uint32_t line{};
  std::uint64_t timestamp{};
  std::string context;
  SystemVerilogUvmReportAction action{
      SystemVerilogUvmReportAction::None};
  std::uint64_t file{};
  SystemVerilogUvmReportElementContainer elements;
};

enum class SystemVerilogUvmReportCatcherResult : std::uint8_t {
  Throw,
  Caught,
};

enum class SystemVerilogUvmReportCatcherOrdering : std::uint8_t {
  Append,
  Prepend,
};

using SystemVerilogUvmReportCatcherHandle = std::uint64_t;

/// Mutable view of the current message. Later catchers observe every retained
/// modification made by earlier catchers.
class SystemVerilogUvmReportCatcherContext final {
 public:
  [[nodiscard]] const SystemVerilogUvmReportMessage& message() const noexcept {
    return *message_;
  }
  [[nodiscard]] SystemVerilogUvmReportElementContainer& elements() noexcept {
    return message_->elements;
  }
  void set_severity(SystemVerilogUvmReportSeverity severity) noexcept {
    message_->severity = severity;
  }
  void set_verbosity(std::int32_t verbosity) noexcept {
    message_->verbosity = verbosity;
  }
  void set_id(std::string id) { message_->id = std::move(id); }
  void set_message(std::string message) {
    message_->message = std::move(message);
  }
  void set_action(SystemVerilogUvmReportAction action) noexcept {
    message_->action = action;
    action_set_ = true;
  }
  void set_context(std::string context) {
    message_->context = std::move(context);
  }

 private:
  friend class SystemVerilogUvmReportService;
  explicit SystemVerilogUvmReportCatcherContext(
      SystemVerilogUvmReportMessage& message) noexcept
      : message_{&message} {}
  [[nodiscard]] bool action_set() const noexcept { return action_set_; }

  SystemVerilogUvmReportMessage* message_{};
  bool action_set_{};
};

struct SystemVerilogUvmReportPolicy {
  SystemVerilogUvmReportSeverity severity{
      SystemVerilogUvmReportSeverity::Info};
  SystemVerilogUvmReportAction action{
      SystemVerilogUvmReportAction::Display};
  std::uint64_t file{};
  std::int32_t verbosity{200};
};

struct SystemVerilogUvmReportServerLimits {
  std::size_t max_ids{1U * 1'024U * 1'024U};
  std::size_t max_output_bytes{32U * 1'024U * 1'024U};
};

struct SystemVerilogUvmReportExecution {
  std::string composed;
  SystemVerilogUvmReportAction action{
      SystemVerilogUvmReportAction::None};
  std::uint64_t file{};
  bool displayed{};
  bool logged{};
  bool recorded{};
  bool exit_requested{};
  bool stop_requested{};
};

/// Simulation-owned UVM report-server formatting, accounting, and action
/// execution. Sinks receive exactly one newline-terminated record.
class SystemVerilogUvmReportServer final {
 public:
  using DisplaySink = std::function<void(std::string_view)>;
  using FileSink =
      std::function<void(std::uint64_t, std::string_view)>;
  using RecordSink =
      std::function<void(const SystemVerilogUvmReportMessage&)>;
  using ControlSink =
      std::function<void(const SystemVerilogUvmReportExecution&)>;

  explicit SystemVerilogUvmReportServer(
      SystemVerilogUvmReportServerLimits limits = {});

  [[nodiscard]] std::string compose(
      const SystemVerilogUvmReportMessage& message,
      std::string_view payload) const;
  [[nodiscard]] bool process(
      SystemVerilogUvmReportMessage& message,
      std::string_view payload,
      SystemVerilogUvmReportExecution* execution = nullptr);
  [[nodiscard]] std::string summarize() const;

  bool set_max_quit_count(std::uint64_t count, bool overridable = true);
  void set_quit_count(std::uint64_t count) noexcept { quit_count_ = count; }
  void set_severity_count(
      SystemVerilogUvmReportSeverity severity, std::uint64_t count);
  void set_id_count(std::string id, std::uint64_t count);
  void reset_counts() noexcept;
  void set_display_sink(DisplaySink sink) { display_sink_ = std::move(sink); }
  void set_file_sink(FileSink sink) { file_sink_ = std::move(sink); }
  void set_record_sink(RecordSink sink) { record_sink_ = std::move(sink); }
  void set_control_sink(ControlSink sink) { control_sink_ = std::move(sink); }
  void set_show_verbosity(const bool value) noexcept {
    show_verbosity_ = value;
  }
  void set_show_terminator(const bool value) noexcept {
    show_terminator_ = value;
  }
  void set_enable_id_summary(const bool value) noexcept {
    enable_id_summary_ = value;
  }
  void set_record_all_messages(const bool value) noexcept {
    record_all_messages_ = value;
  }

  [[nodiscard]] std::uint64_t severity_count(
      SystemVerilogUvmReportSeverity severity) const;
  [[nodiscard]] std::uint64_t id_count(std::string_view id) const noexcept;
  [[nodiscard]] std::uint64_t quit_count() const noexcept {
    return quit_count_;
  }
  [[nodiscard]] std::uint64_t max_quit_count() const noexcept {
    return max_quit_count_;
  }
  [[nodiscard]] bool max_quit_overridable() const noexcept {
    return max_quit_overridable_;
  }
  [[nodiscard]] std::uint64_t sink_failures() const noexcept {
    return sink_failures_;
  }
  [[nodiscard]] const SystemVerilogUvmReportServerLimits& limits()
      const noexcept { return limits_; }

 private:
  void validate_severity(SystemVerilogUvmReportSeverity severity) const;
  [[nodiscard]] static std::size_t severity_index(
      SystemVerilogUvmReportSeverity severity);

  SystemVerilogUvmReportServerLimits limits_;
  std::array<std::uint64_t, 4> severity_counts_{};
  std::map<std::string, std::uint64_t, std::less<>> id_counts_;
  std::uint64_t quit_count_{};
  std::uint64_t max_quit_count_{};
  bool max_quit_overridable_{true};
  bool show_verbosity_{};
  bool show_terminator_{};
  bool enable_id_summary_{true};
  bool record_all_messages_{};
  DisplaySink display_sink_;
  FileSink file_sink_;
  RecordSink record_sink_;
  ControlSink control_sink_;
  std::uint64_t sink_failures_{};
};

struct SystemVerilogUvmReportLimits {
  std::size_t max_id_bytes{4'096};
  std::size_t max_message_bytes{16U * 1'024U * 1'024U};
  std::size_t max_filename_bytes{16'384};
  std::size_t max_context_bytes{16'384};
  std::size_t max_report_object_name_bytes{16'384};
  std::size_t max_composed_bytes{32U * 1'024U * 1'024U};
  std::size_t max_handlers{65'536};
  std::size_t max_settings{1U * 1'024U * 1'024U};
  std::size_t max_catchers{65'536};
  std::size_t max_catcher_name_bytes{4'096};
  std::size_t max_catcher_dispatches{65'536};
  SystemVerilogUvmReportElementLimits elements;
};

/// Simulation-owned UVM report-message construction and report-object routing.
/// Handler policy and final report-server formatting are deliberately separate.
class SystemVerilogUvmReportService final {
 public:
  using RouteHook =
      std::function<void(const SystemVerilogUvmReportMessage&)>;
  using ReportHook =
      std::function<bool(const SystemVerilogUvmReportMessage&)>;
  using ReportCatcher = std::function<SystemVerilogUvmReportCatcherResult(
      SystemVerilogUvmReportCatcherContext&)>;

  explicit SystemVerilogUvmReportService(
      SystemVerilogUvmObjectService& objects,
      SystemVerilogUvmReportLimits limits = {});
  SystemVerilogUvmReportService(
      SystemVerilogUvmObjectService& objects,
      SystemVerilogUvmComponentService& components,
      SystemVerilogUvmReportLimits limits = {});

  [[nodiscard]] bool enabled(
      SystemVerilogClassHandle report_object,
      std::int32_t verbosity,
      SystemVerilogUvmReportSeverity severity =
          SystemVerilogUvmReportSeverity::Info,
      std::string_view id = {}) const;
  [[nodiscard]] SystemVerilogUvmReportMessage construct(
      const SystemVerilogUvmReportRequest& request) const;
  [[nodiscard]] bool report(
      const SystemVerilogUvmReportRequest& request);
  [[nodiscard]] std::string compose_payload(
      const SystemVerilogUvmReportMessage& message) const;
  [[nodiscard]] SystemVerilogUvmReportServer& server() noexcept {
    return server_;
  }
  [[nodiscard]] const SystemVerilogUvmReportServer& server() const noexcept {
    return server_;
  }

  [[nodiscard]] SystemVerilogUvmReportPolicy policy(
      SystemVerilogClassHandle report_object,
      SystemVerilogUvmReportSeverity severity,
      std::string_view id = {}) const;
  void set_verbosity(
      SystemVerilogClassHandle report_object, std::int32_t verbosity);
  void set_id_verbosity(
      SystemVerilogClassHandle report_object,
      std::string id,
      std::int32_t verbosity);
  void set_severity_id_verbosity(
      SystemVerilogClassHandle report_object,
      SystemVerilogUvmReportSeverity severity,
      std::string id,
      std::int32_t verbosity);
  void set_severity_action(
      SystemVerilogClassHandle report_object,
      SystemVerilogUvmReportSeverity severity,
      SystemVerilogUvmReportAction action);
  void set_id_action(
      SystemVerilogClassHandle report_object,
      std::string id,
      SystemVerilogUvmReportAction action);
  void set_severity_id_action(
      SystemVerilogClassHandle report_object,
      SystemVerilogUvmReportSeverity severity,
      std::string id,
      SystemVerilogUvmReportAction action);
  void set_default_file(
      SystemVerilogClassHandle report_object, std::uint64_t file);
  void set_severity_file(
      SystemVerilogClassHandle report_object,
      SystemVerilogUvmReportSeverity severity,
      std::uint64_t file);
  void set_id_file(
      SystemVerilogClassHandle report_object,
      std::string id,
      std::uint64_t file);
  void set_severity_id_file(
      SystemVerilogClassHandle report_object,
      SystemVerilogUvmReportSeverity severity,
      std::string id,
      std::uint64_t file);
  void set_severity_override(
      SystemVerilogClassHandle report_object,
      SystemVerilogUvmReportSeverity current,
      SystemVerilogUvmReportSeverity replacement);
  void set_severity_id_override(
      SystemVerilogClassHandle report_object,
      SystemVerilogUvmReportSeverity current,
      std::string id,
      SystemVerilogUvmReportSeverity replacement);
  void set_report_hook(
      SystemVerilogClassHandle report_object, ReportHook hook);
  void set_severity_hook(
      SystemVerilogClassHandle report_object,
      SystemVerilogUvmReportSeverity severity,
      ReportHook hook);

  [[nodiscard]] SystemVerilogUvmReportCatcherHandle add_catcher(
      SystemVerilogClassHandle report_object,
      std::string name,
      ReportCatcher catcher,
      SystemVerilogUvmReportCatcherOrdering ordering =
          SystemVerilogUvmReportCatcherOrdering::Append);
  [[nodiscard]] bool remove_catcher(
      SystemVerilogUvmReportCatcherHandle catcher) noexcept;
  void set_catcher_enabled(
      SystemVerilogUvmReportCatcherHandle catcher, bool enabled);
  [[nodiscard]] bool catcher_enabled(
      SystemVerilogUvmReportCatcherHandle catcher) const;
  [[nodiscard]] std::size_t catcher_count() const noexcept {
    return catchers_.size();
  }
  [[nodiscard]] std::uint64_t catcher_invocations() const noexcept {
    return catcher_invocations_;
  }
  [[nodiscard]] std::uint64_t catcher_failures() const noexcept {
    return catcher_failures_;
  }
  [[nodiscard]] std::uint64_t catcher_reentry_bypasses() const noexcept {
    return catcher_reentry_bypasses_;
  }
  [[nodiscard]] std::uint64_t caught_count(
      SystemVerilogUvmReportSeverity severity) const;
  [[nodiscard]] std::uint64_t demoted_count(
      SystemVerilogUvmReportSeverity severity) const;
  [[nodiscard]] std::string catcher_summary() const;

  void set_verbosity_hier(
      SystemVerilogClassHandle component, std::int32_t verbosity);
  void set_id_verbosity_hier(
      SystemVerilogClassHandle component,
      std::string id,
      std::int32_t verbosity);
  void set_severity_id_verbosity_hier(
      SystemVerilogClassHandle component,
      SystemVerilogUvmReportSeverity severity,
      std::string id,
      std::int32_t verbosity);
  void set_severity_action_hier(
      SystemVerilogClassHandle component,
      SystemVerilogUvmReportSeverity severity,
      SystemVerilogUvmReportAction action);
  void set_id_action_hier(
      SystemVerilogClassHandle component,
      std::string id,
      SystemVerilogUvmReportAction action);
  void set_severity_id_action_hier(
      SystemVerilogClassHandle component,
      SystemVerilogUvmReportSeverity severity,
      std::string id,
      SystemVerilogUvmReportAction action);
  void set_default_file_hier(
      SystemVerilogClassHandle component, std::uint64_t file);
  void set_severity_file_hier(
      SystemVerilogClassHandle component,
      SystemVerilogUvmReportSeverity severity,
      std::uint64_t file);
  void set_id_file_hier(
      SystemVerilogClassHandle component,
      std::string id,
      std::uint64_t file);
  void set_severity_id_file_hier(
      SystemVerilogClassHandle component,
      SystemVerilogUvmReportSeverity severity,
      std::string id,
      std::uint64_t file);

  void set_route_hook(RouteHook hook) { route_hook_ = std::move(hook); }
  void set_default_verbosity(std::int32_t verbosity) noexcept;
  [[nodiscard]] std::int32_t default_verbosity() const noexcept {
    return default_verbosity_;
  }
  [[nodiscard]] std::uint64_t routed_count() const noexcept {
    return routed_count_;
  }
  [[nodiscard]] std::uint64_t filtered_count() const noexcept {
    return filtered_count_;
  }
  [[nodiscard]] std::uint64_t route_failures() const noexcept {
    return route_failures_;
  }
  [[nodiscard]] std::uint64_t hook_filtered_count() const noexcept {
    return hook_filtered_count_;
  }
  [[nodiscard]] std::uint64_t hook_failures() const noexcept {
    return hook_failures_;
  }
  [[nodiscard]] std::uint64_t action_filtered_count() const noexcept {
    return action_filtered_count_;
  }
  [[nodiscard]] std::size_t handler_count() const noexcept {
    return handlers_.size();
  }
  [[nodiscard]] std::size_t setting_count() const noexcept {
    return setting_count_;
  }
  [[nodiscard]] const SystemVerilogUvmReportLimits& limits() const noexcept {
    return limits_;
  }

 private:
  struct HandlerState {
    std::int32_t verbosity{200};
    std::array<SystemVerilogUvmReportAction, 4> severity_actions{
        SystemVerilogUvmReportAction::Display,
        SystemVerilogUvmReportAction::Display,
        SystemVerilogUvmReportAction::Display
            | SystemVerilogUvmReportAction::Count,
        SystemVerilogUvmReportAction::Display
            | SystemVerilogUvmReportAction::Exit};
    std::array<std::uint64_t, 4> severity_files{};
    std::array<std::optional<SystemVerilogUvmReportSeverity>, 4>
        severity_overrides{};
    std::map<std::string, std::int32_t, std::less<>> id_verbosities;
    std::array<std::map<std::string, std::int32_t, std::less<>>, 4>
        severity_id_verbosities;
    std::map<std::string, SystemVerilogUvmReportAction, std::less<>>
        id_actions;
    std::array<
        std::map<
            std::string, SystemVerilogUvmReportAction, std::less<>>,
        4> severity_id_actions;
    std::uint64_t default_file{};
    std::map<std::string, std::uint64_t, std::less<>> id_files;
    std::array<std::map<std::string, std::uint64_t, std::less<>>, 4>
        severity_id_files;
    std::map<
        std::string,
        std::array<std::optional<SystemVerilogUvmReportSeverity>, 4>,
        std::less<>> severity_id_overrides;
    ReportHook report_hook;
    std::array<ReportHook, 4> severity_hooks;
  };

  struct CatcherState {
    SystemVerilogClassHandle report_object{};
    std::string name;
    ReportCatcher callback;
    bool enabled{true};
  };

  void validate_request(const SystemVerilogUvmReportRequest& request) const;
  void validate_message(const SystemVerilogUvmReportMessage& message) const;
  void validate_object(SystemVerilogClassHandle object) const;
  void validate_severity(SystemVerilogUvmReportSeverity severity) const;
  void validate_action(SystemVerilogUvmReportAction action) const;
  void validate_id(std::string_view id) const;
  void validate_elements(
      const SystemVerilogUvmReportElementContainer& elements) const;
  [[nodiscard]] std::int32_t effective_verbosity(
      const SystemVerilogUvmReportRequest& request) const noexcept;
  [[nodiscard]] std::string object_name(
      SystemVerilogClassHandle object) const;
  [[nodiscard]] static std::size_t severity_index(
      SystemVerilogUvmReportSeverity severity);
  [[nodiscard]] HandlerState& mutable_handler(
      SystemVerilogClassHandle report_object);
  [[nodiscard]] const HandlerState* find_handler(
      SystemVerilogClassHandle report_object) const noexcept;
  void mutate_hierarchy(
      SystemVerilogClassHandle component,
      const std::function<void(SystemVerilogClassHandle)>& mutation);
  [[nodiscard]] bool run_hooks(
      const SystemVerilogUvmReportMessage& message);
  [[nodiscard]] bool run_catchers(SystemVerilogUvmReportMessage& message);

  SystemVerilogUvmObjectService* objects_{};
  SystemVerilogUvmComponentService* components_{};
  SystemVerilogUvmReportLimits limits_;
  SystemVerilogUvmReportServer server_;
  std::map<SystemVerilogClassHandle, HandlerState> handlers_;
  std::map<SystemVerilogUvmReportCatcherHandle, CatcherState> catchers_;
  std::vector<SystemVerilogUvmReportCatcherHandle> catcher_order_;
  RouteHook route_hook_;
  std::int32_t default_verbosity_{200};
  std::size_t setting_count_{};
  std::uint64_t next_sequence_{1};
  std::uint64_t routed_count_{};
  std::uint64_t filtered_count_{};
  std::uint64_t route_failures_{};
  std::uint64_t hook_filtered_count_{};
  std::uint64_t hook_failures_{};
  std::uint64_t action_filtered_count_{};
  SystemVerilogUvmReportCatcherHandle next_catcher_{1};
  std::array<std::uint64_t, 4> caught_counts_{};
  std::array<std::uint64_t, 4> demoted_counts_{};
  std::uint64_t catcher_invocations_{};
  std::uint64_t catcher_failures_{};
  std::uint64_t catcher_reentry_bypasses_{};
  bool in_catcher_{};
};

}  // namespace fsim::runtime
