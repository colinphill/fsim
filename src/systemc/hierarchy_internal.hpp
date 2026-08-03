// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "fsim/systemc/hierarchy.hpp"

#include "fsim/runtime/simir.hpp"
#include "fsim/systemc/plugin_loader.hpp"

#if defined(FSIM_HAS_BOOST_CONTEXT)
#include <boost/context/fiber.hpp>
#endif

#include <algorithm>
#include <cctype>
#include <exception>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fsim::systemc {

#if defined(FSIM_HAS_BOOST_CONTEXT)
struct ThreadFiberState {
    boost::context::fiber process;
    boost::context::fiber caller;
    bool started{};
    bool terminated{};
    bool stopping{};
};
#endif

struct HierarchyRegistry::Impl {
    struct Factory {
        fsim_sc_module_factory_v1 legacy{};
        fsim_sc_module_elaborate_v1 elaborate{};
        fsim_sc_module_destroy_v1 destroy{};
        void* user{};
        std::vector<ConstructionParameterDescription> parameters;
    };

    struct Object {
        enum class Kind : std::uint8_t {
            port,
            signal,
            export_object,
        };

        fsim_sc_handle_v1 module{};
        std::size_t index{};
        fsim_sc_value_encoding_v1 encoding{FSIM_SC_BIT2};
        std::uint32_t width{};
        Kind kind{Kind::port};
        fsim_sc_port_direction_v1 direction{FSIM_SC_INOUT};
        bool writable{true};
    };

    struct Child {
        fsim_sc_handle_v1 module{};
        std::size_t child{};
    };

    struct Process {
        fsim_sc_handle_v1 module{};
        std::size_t process{};
    };

    struct Event {
        fsim_sc_handle_v1 module{};
        std::size_t event{};
    };

    struct PrimitiveChannel {
        fsim_sc_handle_v1 module{};
        std::size_t channel{};
    };

    struct LiveModule {
        enum class LifecycleState : std::uint8_t {
            constructed,
            before_elaboration_complete,
            elaborated,
            started,
            ended,
            poisoned,
        };

        ModuleDescription description;
        fsim_sc_module_destroy_v1 destroy{};
        void* user{};
        void* object{};
        LifecycleState lifecycle{LifecycleState::constructed};
    };

    std::filesystem::path path;
    std::unique_ptr<Plugin> plugin;
    std::unordered_map<std::string, Factory> factories;
    std::unordered_map<fsim_sc_handle_v1, ModuleDescription> pending;
    std::unordered_map<
        fsim_sc_handle_v1, std::vector<fsim_sc_handle_v1>>
        native_children;
    std::unordered_map<fsim_sc_handle_v1, Object> objects;
    std::unordered_map<fsim_sc_handle_v1, Child> children;
    std::unordered_map<fsim_sc_handle_v1, Process> processes;
    std::unordered_map<fsim_sc_handle_v1, Event> events;
    std::unordered_map<fsim_sc_handle_v1, PrimitiveChannel>
        primitive_channels;
    std::unordered_set<fsim_sc_handle_v1> internal_signals;
    std::unordered_map<fsim_sc_handle_v1, std::uint32_t> runtime_objects;
#if defined(FSIM_HAS_BOOST_CONTEXT)
    std::unordered_map<
        fsim_sc_handle_v1, std::unique_ptr<ThreadFiberState>>
        thread_fibers;
#endif
    std::vector<LiveModule> live;
    fsim_sc_handle_v1 next_handle{1};
    std::uint64_t femtoseconds_per_tick{1};
    std::string elaboration_failure;

    [[nodiscard]] std::optional<fsim_sc_handle_v1> allocate_handle();

    void rollback(const fsim_sc_handle_v1 module) noexcept;

    [[nodiscard]] ModuleDescription collect(
        const fsim_sc_handle_v1 module);
};

namespace hierarchy_detail {

struct ActiveInvocation {
    HierarchyRegistry::Impl* registry{};
    runtime::simir::ProcessExecutionContext* context{};
#if defined(FSIM_HAS_BOOST_CONTEXT)
    ThreadFiberState* thread{};
#endif
    std::vector<std::uint8_t> read_buffer;
    std::string failure;
    std::optional<MethodSuspendResult> suspension;
};

extern thread_local ActiveInvocation* active_invocation;

class InvocationScope final {
public:
    explicit InvocationScope(ActiveInvocation& invocation) noexcept;
    ~InvocationScope();

    InvocationScope(const InvocationScope&) = delete;
    InvocationScope& operator=(const InvocationScope&) = delete;
};

[[nodiscard]] fsim_sc_status_v1 suspend_thread() noexcept;

void shutdown_threads(HierarchyRegistry::Impl& registry) noexcept;

[[nodiscard]] bool valid_direction(
    const fsim_sc_port_direction_v1 direction) noexcept;

[[nodiscard]] bool valid_encoding(
    const fsim_sc_value_encoding_v1 encoding) noexcept;

[[nodiscard]] bool valid_construction_type(
    const fsim_sc_construction_type_v1 type) noexcept;

[[nodiscard]] bool valid_construction_value(
    const fsim_sc_construction_type_v1 type,
    const std::int64_t value) noexcept;

[[nodiscard]] const ModuleDescription* find_module_description(
    const ModuleDescription& root,
    const fsim_sc_handle_v1 handle) noexcept;

[[nodiscard]] HierarchyRegistry::Impl::LiveModule* find_live_module(
    HierarchyRegistry::Impl& registry,
    const fsim_sc_handle_v1 handle) noexcept;

void invoke_lifecycle_entry(
    HierarchyRegistry::Impl& registry,
    const fsim_sc_lifecycle_entry_v1 entry,
    void* user,
    const std::string_view phase);

extern "C" fsim_sc_status_v1 registry_register_port(
    void* context,
    const fsim_sc_handle_v1 module,
    const char* name,
    const fsim_sc_port_direction_v1 direction,
    const fsim_sc_value_encoding_v1 encoding,
    const std::uint32_t width,
    fsim_sc_handle_v1* result) noexcept;

extern "C" fsim_sc_status_v1 registry_register_export(
    void* context,
    const fsim_sc_handle_v1 module,
    const char* name,
    const fsim_sc_value_encoding_v1 encoding,
    const std::uint32_t width,
    fsim_sc_handle_v1* result) noexcept;

extern "C" fsim_sc_status_v1 registry_register_event(
    void* context,
    const fsim_sc_handle_v1 module,
    const char* name,
    fsim_sc_handle_v1* result) noexcept;

extern "C" fsim_sc_status_v1 registry_register_primitive_channel(
    void* context,
    const fsim_sc_handle_v1 module,
    const char* name,
    const fsim_sc_channel_update_v1 update,
    void* user,
    fsim_sc_handle_v1* result) noexcept;

extern "C" fsim_sc_status_v1 registry_register_foreign_child(
    void* context,
    const fsim_sc_handle_v1 module,
    const char* name,
    fsim_sc_handle_v1* result) noexcept;

extern "C" fsim_sc_status_v1 registry_set_foreign_child_actual(
    void* context,
    const fsim_sc_handle_v1 child,
    const char* name,
    const std::int64_t value) noexcept;

extern "C" fsim_sc_status_v1 registry_get_construction_value(
    void* context,
    const fsim_sc_handle_v1 module,
    const char* name,
    std::int64_t* result) noexcept;

extern "C" fsim_sc_status_v1 registry_connect_foreign_port(
    void* context,
    const fsim_sc_handle_v1 child,
    const char* name,
    const fsim_sc_port_direction_v1 direction,
    const fsim_sc_value_encoding_v1 encoding,
    const std::uint32_t width,
    const fsim_sc_handle_v1 object) noexcept;

extern "C" fsim_sc_status_v1 registry_register_process(
    void* context,
    const fsim_sc_handle_v1 module,
    const char* name,
    const fsim_sc_process_kind_v1 kind,
    const fsim_sc_process_entry_v1 entry,
    void* user,
    fsim_sc_handle_v1* result) noexcept;

extern "C" fsim_sc_status_v1 registry_add_sensitivity(
    void* context,
    const fsim_sc_handle_v1 process,
    const fsim_sc_handle_v1 object,
    const fsim_sc_edge_kind_v1 edge) noexcept;

extern "C" fsim_sc_status_v1 registry_set_process_initialize(
    void* context,
    const fsim_sc_handle_v1 process,
    const std::uint8_t initialize) noexcept;

[[nodiscard]] std::size_t plane_size(
    const std::uint32_t width) noexcept;

extern "C" fsim_sc_status_v1 registry_register_signal(
    void* context,
    const fsim_sc_handle_v1 module,
    const fsim_sc_handle_v1 channel,
    const char* name,
    const fsim_sc_value_encoding_v1 encoding,
    const std::uint32_t width,
    const fsim_sc_value_view_v1* initial_value) noexcept;

extern "C" fsim_sc_status_v1 registry_bind_port(
    void* context,
    const fsim_sc_handle_v1 port,
    const fsim_sc_handle_v1 channel) noexcept;

extern "C" fsim_sc_status_v1 registry_bind_export(
    void* context,
    const fsim_sc_handle_v1 export_handle,
    const fsim_sc_handle_v1 target) noexcept;

extern "C" fsim_sc_status_v1 registry_set_export_writable(
    void* context,
    fsim_sc_handle_v1 export_handle,
    std::uint8_t writable) noexcept;

extern "C" fsim_sc_status_v1 registry_register_metadata_object(
    void* context,
    fsim_sc_handle_v1 module,
    const char* name,
    fsim_sc_metadata_category_v1 category,
    const char* kind,
    fsim_sc_handle_v1* result) noexcept;

extern "C" fsim_sc_status_v1 registry_set_primitive_channel_kind(
    void* context,
    fsim_sc_handle_v1 channel,
    const char* kind) noexcept;

extern "C" fsim_sc_status_v1 registry_register_native_module(
    void* context,
    const fsim_sc_handle_v1 parent,
    const char* name,
    fsim_sc_handle_v1* result) noexcept;

extern "C" fsim_sc_status_v1 registry_register_lifecycle(
    void* context,
    const fsim_sc_handle_v1 module,
    const fsim_sc_lifecycle_entry_v1 before_end_of_elaboration,
    const fsim_sc_lifecycle_entry_v1 end_of_elaboration,
    const fsim_sc_lifecycle_entry_v1 start_of_simulation,
    const fsim_sc_lifecycle_entry_v1 end_of_simulation,
    void* user) noexcept;

extern "C" fsim_sc_status_v1 registry_read(
    void* context,
    const fsim_sc_handle_v1 object,
    fsim_sc_value_view_v1* result) noexcept;

extern "C" fsim_sc_status_v1 registry_value_changed(
    void* context,
    const fsim_sc_handle_v1 object,
    std::uint8_t* result) noexcept;

extern "C" fsim_sc_status_v1 registry_write(
    void* context,
    const fsim_sc_handle_v1 object,
    const fsim_sc_value_view_v1* value) noexcept;

[[nodiscard]] fsim_sc_status_v1 convert_delay(
    HierarchyRegistry::Impl& registry,
    const std::uint64_t femtoseconds,
    std::uint64_t& ticks);

extern "C" fsim_sc_status_v1 registry_wait_time(
    void* context, const std::uint64_t femtoseconds) noexcept;

extern "C" fsim_sc_status_v1 registry_wait_event(
    void* context, const fsim_sc_handle_v1 event) noexcept;

extern "C" fsim_sc_status_v1 registry_wait_event_list(
    void* context,
    const fsim_sc_handle_v1* events,
    const std::size_t event_count,
    const fsim_sc_event_list_kind_v1 kind) noexcept;

extern "C" fsim_sc_status_v1 registry_wait_static(
    void* context) noexcept;
extern "C" fsim_sc_status_v1 registry_wait_event_timeout(
    void* context,
    std::uint64_t femtoseconds,
    const fsim_sc_handle_v1* events,
    std::size_t event_count,
    fsim_sc_event_list_kind_v1 kind) noexcept;

extern "C" fsim_sc_status_v1 registry_notify_mode(
    void* context,
    const fsim_sc_handle_v1 event,
    const std::uint64_t femtoseconds,
    const fsim_sc_notification_kind_v1 kind) noexcept;

extern "C" fsim_sc_status_v1 registry_notify_delayed(
    void* context,
    const fsim_sc_handle_v1 event,
    const std::uint64_t femtoseconds) noexcept;

extern "C" fsim_sc_status_v1 registry_cancel_event(
    void* context,
    const fsim_sc_handle_v1 event) noexcept;

extern "C" fsim_sc_status_v1 registry_request_update(
    void* context,
    const fsim_sc_handle_v1 channel) noexcept;

extern "C" fsim_sc_status_v1 registry_notify(
    void* context,
    const fsim_sc_handle_v1 event,
    const std::uint64_t femtoseconds) noexcept;

extern "C" void registry_report(
    void* context, const int severity, const char* message) noexcept;

extern "C" fsim_sc_status_v1 registry_register_factory(
    void* context,
    const char* name,
    const fsim_sc_module_factory_v1 factory,
    const fsim_sc_module_destroy_v1 destroy,
    void* user) noexcept;

extern "C" fsim_sc_status_v1 registry_register_elaboration_factory(
    void* context,
    const char* name,
    const fsim_sc_module_elaborate_v1 factory,
    const fsim_sc_module_destroy_v1 destroy,
    void* user) noexcept;

extern "C" fsim_sc_status_v1 registry_register_factory_parameter(
    void* context,
    const char* factory,
    const char* name,
    const fsim_sc_construction_type_v1 type,
    const std::uint8_t has_default,
    const std::int64_t default_value) noexcept;


} // namespace hierarchy_detail
} // namespace fsim::systemc
