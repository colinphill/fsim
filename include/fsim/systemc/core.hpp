// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/systemc_abi.h"

#include <algorithm>
#include <array>
#include <bit>
#include <bitset>
#include <cmath>
#include <compare>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace sc_core {

class sc_module;

enum sc_time_unit {
    SC_FS,
    SC_PS,
    SC_NS,
    SC_US,
    SC_MS,
    SC_SEC,
};

class sc_time final {
public:
    constexpr sc_time() noexcept = default;

    sc_time(const double value, const sc_time_unit unit);

    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return femtoseconds_; }
    [[nodiscard]] constexpr bool is_zero() const noexcept { return femtoseconds_ == 0; }

    friend constexpr auto operator<=>(const sc_time&, const sc_time&) noexcept = default;

    friend constexpr sc_time operator+(const sc_time lhs, const sc_time rhs) {
        if (std::numeric_limits<std::uint64_t>::max() - lhs.femtoseconds_ < rhs.femtoseconds_) {
            throw std::overflow_error{"sc_time addition overflow"};
        }
        return from_value(lhs.femtoseconds_ + rhs.femtoseconds_);
    }

    friend constexpr sc_time operator-(const sc_time lhs, const sc_time rhs) {
        if (lhs.femtoseconds_ < rhs.femtoseconds_) {
            throw std::underflow_error{"sc_time subtraction underflow"};
        }
        return from_value(lhs.femtoseconds_ - rhs.femtoseconds_);
    }

    [[nodiscard]] static constexpr sc_time from_value(const std::uint64_t value) noexcept {
        sc_time result;
        result.femtoseconds_ = value;
        return result;
    }

private:
    static constexpr std::uint64_t scale_for(const sc_time_unit unit) {
        switch (unit) {
        case SC_FS:
            return 1;
        case SC_PS:
            return 1'000;
        case SC_NS:
            return 1'000'000;
        case SC_US:
            return 1'000'000'000;
        case SC_MS:
            return 1'000'000'000'000;
        case SC_SEC:
            return 1'000'000'000'000'000;
        }
        throw std::invalid_argument{"invalid sc_time_unit"};
    }

    std::uint64_t femtoseconds_{};
};

inline constexpr sc_time SC_ZERO_TIME{};

namespace detail {

extern thread_local const fsim_sc_host_v1* current_host;
extern thread_local fsim_sc_handle_v1 current_module;
extern thread_local fsim_sc_process_kind_v1 current_process_kind;
extern thread_local fsim_sc_handle_v1 construction_root_module;
extern thread_local sc_module* current_cpp_module;
extern thread_local const char* current_module_name;

void bind_host(const fsim_sc_host_v1* host) noexcept;

class host_scope final {
public:
    host_scope(
        const fsim_sc_host_v1* host,
        const fsim_sc_handle_v1 module = 0) noexcept;

    ~host_scope();

    host_scope(const host_scope&) = delete;
    host_scope& operator=(const host_scope&) = delete;

private:
    const fsim_sc_host_v1* previous_host_;
    fsim_sc_handle_v1 previous_module_;
};

class module_construction_scope final {
public:
    explicit module_construction_scope(
        const fsim_sc_handle_v1 root_module) noexcept;

    ~module_construction_scope();

    module_construction_scope(const module_construction_scope&) = delete;
    module_construction_scope& operator=(
        const module_construction_scope&) = delete;

private:
    fsim_sc_handle_v1 previous_root_{};
    sc_module* previous_cpp_module_{};
};

void check_status(const fsim_sc_status_v1 status, const char* operation);

template <typename T>
struct value_traits;

template <typename T>
[[nodiscard]] fsim_sc_handle_v1 register_port(
    const char* name,
    fsim_sc_port_direction_v1 direction);

template <typename Interface>
[[nodiscard]] fsim_sc_handle_v1 register_export(const char* name);

void bind_export(
    fsim_sc_handle_v1 export_handle,
    fsim_sc_handle_v1 target);

template <typename T>
[[nodiscard]] T read_object(fsim_sc_handle_v1 object);

template <typename T>
void write_object(fsim_sc_handle_v1 object, const T& value);

template <typename T>
void register_signal(
    fsim_sc_handle_v1 channel,
    const char* name,
    const T& initial_value);

[[nodiscard]] bool object_event(fsim_sc_handle_v1 object);
void bind_port(
    fsim_sc_handle_v1 port,
    fsim_sc_handle_v1 channel);

[[nodiscard]] fsim_sc_handle_v1 register_event(const char* name);
[[nodiscard]] fsim_sc_handle_v1 register_primitive_channel(
    const char* name,
    fsim_sc_channel_update_v1 update,
    void* user);

[[nodiscard]] fsim_sc_handle_v1 register_metadata_object(
    const char* name,
    fsim_sc_metadata_category_v1 category,
    const char* kind);

void set_primitive_channel_kind(
    fsim_sc_handle_v1 channel, const char* kind);

} // namespace detail

class sc_object {
public:
    virtual ~sc_object();

    sc_object(const sc_object&) = delete;
    sc_object& operator=(const sc_object&) = delete;

    [[nodiscard]] const char* name() const noexcept;
    [[nodiscard]] const char* basename() const noexcept;
    [[nodiscard]] const char* kind() const noexcept;
    [[nodiscard]] const sc_object* get_parent_object() const noexcept;
    [[nodiscard]] const std::vector<sc_object*>&
    get_child_objects() const noexcept;

protected:
    sc_object(const char* basename, const char* kind);
    sc_object(
        const char* basename,
        const char* kind,
        const sc_object* parent);

    void fsim_set_kind(const char* kind);

private:
    std::string name_;
    std::string basename_;
    std::string kind_;
    sc_object* parent_{};
    void* hierarchy_domain_{};
    std::vector<sc_object*> children_;
};

[[nodiscard]] sc_object* sc_find_object(const char* name) noexcept;

[[nodiscard]] const std::vector<sc_object*>&
sc_get_top_level_objects() noexcept;

class sc_event : public sc_object {
public:
    sc_event();
    explicit sc_event(const char* name);
    explicit sc_event(fsim_sc_handle_v1 handle);

    void notify() const;

    void notify(const sc_time delay) const;

    void cancel() const;

    void notify_delayed() const;

    void notify_delayed(const sc_time delay) const;

    [[nodiscard]] fsim_sc_handle_v1 native_handle() const noexcept;

private:
    void notify_impl(
        const sc_time delay,
        const fsim_sc_notification_kind_v1 kind) const;

    fsim_sc_handle_v1 handle_{};
};

class sc_event_or_list {
public:
    sc_event_or_list() = default;
    explicit sc_event_or_list(const sc_event& event);

    sc_event_or_list& operator|=(const sc_event& event);
    sc_event_or_list& operator|=(const sc_event_or_list& events);

    [[nodiscard]] const std::vector<fsim_sc_handle_v1>&
    native_handles() const noexcept;

private:
    void append(const fsim_sc_handle_v1 event);

    std::vector<fsim_sc_handle_v1> events_;
};

class sc_event_and_list {
public:
    sc_event_and_list() = default;
    explicit sc_event_and_list(const sc_event& event);

    sc_event_and_list& operator&=(const sc_event& event);
    sc_event_and_list& operator&=(const sc_event_and_list& events);

    [[nodiscard]] const std::vector<fsim_sc_handle_v1>&
    native_handles() const noexcept;

private:
    void append(const fsim_sc_handle_v1 event);

    std::vector<fsim_sc_handle_v1> events_;
};

sc_event_or_list operator|(
    const sc_event& left, const sc_event& right);

sc_event_or_list operator|(
    sc_event_or_list left, const sc_event& right);

sc_event_or_list operator|(
    const sc_event& left, sc_event_or_list right);

sc_event_or_list operator|(
    sc_event_or_list left, const sc_event_or_list& right);

sc_event_and_list operator&(
    const sc_event& left, const sc_event& right);

sc_event_and_list operator&(
    sc_event_and_list left, const sc_event& right);

sc_event_and_list operator&(
    const sc_event& left, sc_event_and_list right);

sc_event_and_list operator&(
    sc_event_and_list left, const sc_event_and_list& right);

namespace detail {

void set_event_list_trigger(
    const std::vector<fsim_sc_handle_v1>& events,
    const fsim_sc_event_list_kind_v1 kind,
    const char* operation);

void set_timed_event_wait(
    sc_time delay,
    const std::vector<fsim_sc_handle_v1>& events,
    fsim_sc_event_list_kind_v1 kind,
    const char* operation);

} // namespace detail

void wait(const sc_time delay);

void wait(const sc_event& event);

void wait(const sc_time delay, const sc_event& event);

void wait(const sc_event_or_list& events);

void wait(const sc_time delay, const sc_event_or_list& events);

void wait(const sc_event_and_list& events);

void wait(const sc_time delay, const sc_event_and_list& events);

void wait();

void next_trigger(const sc_time delay);

void next_trigger(const sc_event& event);

void next_trigger(const sc_time delay, const sc_event& event);

void next_trigger(const sc_event_or_list& events);

void next_trigger(
    const sc_time delay, const sc_event_or_list& events);

void next_trigger(const sc_event_and_list& events);

void next_trigger(
    const sc_time delay, const sc_event_and_list& events);

class sc_module_name {
public:
    sc_module_name(const char* name);

    [[nodiscard]] const char* c_str() const noexcept;
    [[nodiscard]] operator const char*() const noexcept;

private:
    struct State {
        ~State();

        std::string name;
        fsim_sc_handle_v1 handle{};
        fsim_sc_handle_v1 previous_module{};
        sc_module* previous_cpp_module{};
        const char* previous_name{};
        bool restore_module{};
    };

    [[nodiscard]] fsim_sc_handle_v1 native_handle() const noexcept;
    [[nodiscard]] sc_module* previous_cpp_module() const noexcept;

    std::shared_ptr<State> state_;

    friend class sc_module;
};

class sc_interface {
public:
    virtual ~sc_interface() = default;
};

namespace detail {

template <typename Interface>
[[nodiscard]] const char* custom_interface_kind() {
    if constexpr (requires { Interface::fsim_kind(); }) {
        return Interface::fsim_kind();
    } else {
        return "sc_interface";
    }
}

} // namespace detail

/// Metadata-only generic custom-interface port. The v1 subset preserves this
/// object's hierarchy and interface kind but rejects binding and value access
/// while a compiled fsim elaboration host is active.
template <typename Interface>
class sc_port : public sc_object {
    static_assert(
        std::is_base_of_v<sc_interface, Interface>,
        "sc_port requires an sc_interface-derived type");

public:
    sc_port() : sc_object(nullptr, "sc_port") {}
    explicit sc_port(const char* name)
        : sc_object(name, "sc_port"),
          handle_(detail::register_metadata_object(
              name,
              FSIM_SC_METADATA_PORT,
              detail::custom_interface_kind<Interface>())) {}

    void bind(Interface& interface) {
        if (handle_ != 0) {
            throw std::logic_error{
                "custom SystemC interface binding is metadata-only"};
        }
        interface_ = &interface;
    }
    void operator()(Interface& interface) { bind(interface); }

    [[nodiscard]] Interface& get_interface() const {
        if (interface_ == nullptr) {
            throw std::logic_error{
                "custom SystemC interface value access is unsupported"};
        }
        return *interface_;
    }
    [[nodiscard]] Interface* operator->() const {
        return &get_interface();
    }
    [[nodiscard]] fsim_sc_handle_v1 native_handle() const noexcept {
        return handle_;
    }

private:
    Interface* interface_{};
    fsim_sc_handle_v1 handle_{};
};

template <typename T>
class sc_signal_in_if : virtual public sc_interface {
public:
    using value_type = T;

    [[nodiscard]] virtual const T& read() const = 0;
    [[nodiscard]] virtual bool event() const = 0;
    [[nodiscard]] virtual const sc_event& default_event() const = 0;
    [[nodiscard]] virtual const sc_event&
    value_changed_event() const = 0;
    [[nodiscard]] virtual fsim_sc_handle_v1
    native_handle() const noexcept = 0;
};

template <typename T>
class sc_signal_write_if : virtual public sc_interface {
public:
    using value_type = T;

    virtual void write(const T& value) = 0;
};

template <typename T>
class sc_signal_inout_if
    : public sc_signal_in_if<T>,
      public sc_signal_write_if<T> {
public:
    using value_type = T;
};

namespace detail {

template <typename Interface, typename = void>
struct signal_interface_traits {
    static constexpr bool supported = false;
};

template <typename Interface>
struct signal_interface_traits<
    Interface,
    std::void_t<typename Interface::value_type>> {
    using value_type = typename Interface::value_type;
    static constexpr bool supported =
        std::is_base_of_v<
            sc_signal_in_if<value_type>, Interface>;
    static constexpr bool writable =
        std::is_base_of_v<
            sc_signal_write_if<value_type>, Interface>;
};

} // namespace detail

class sc_prim_channel : public sc_object {
public:
    virtual ~sc_prim_channel() = default;

    sc_prim_channel(const sc_prim_channel&) = delete;
    sc_prim_channel& operator=(const sc_prim_channel&) = delete;

    [[nodiscard]] bool update_requested() const noexcept;

    void request_update();

    [[nodiscard]] fsim_sc_handle_v1 native_handle() const noexcept;

protected:
    sc_prim_channel();

    explicit sc_prim_channel(const char* name);

    sc_prim_channel(const char* name, const char* kind);

    virtual void update();

private:
    static void invoke_update(void* user) noexcept;

    const fsim_sc_host_v1* host_{};
    fsim_sc_handle_v1 handle_{};
    bool update_requested_{};
    bool metadata_only_{};
};

template <typename Interface>
class sc_export : public sc_object {
    static_assert(
        std::is_base_of_v<sc_interface, Interface>,
        "sc_export requires an sc_interface-derived type");

public:
    sc_export() : sc_object(nullptr, "sc_export") {}
    explicit sc_export(const char* name)
        : sc_object(name, "sc_export"),
          handle_(detail::register_export<Interface>(name)) {}

    void bind(Interface& interface) {
        if constexpr (
            detail::signal_interface_traits<Interface>::supported) {
            detail::bind_export(
                handle_, interface.native_handle());
        } else if (handle_ != 0) {
            throw std::logic_error{
                "custom SystemC export binding is metadata-only"};
        }
        interface_ = &interface;
        export_ = nullptr;
    }
    void operator()(Interface& interface) { bind(interface); }

    void bind(sc_export& target) {
        if constexpr (
            !detail::signal_interface_traits<Interface>::supported) {
            if (handle_ != 0 || target.handle_ != 0) {
                throw std::logic_error{
                    "custom SystemC export binding is metadata-only"};
            }
        }
        for (auto* current = &target;
             current != nullptr; current = current->export_) {
            if (current == this) {
                throw std::logic_error{
                    "cyclic sc_export binding"};
            }
        }
        detail::bind_export(handle_, target.handle_);
        interface_ = nullptr;
        export_ = &target;
    }
    void operator()(sc_export& target) { bind(target); }

    [[nodiscard]] Interface& get_interface() const {
        if (interface_ != nullptr) {
            return *interface_;
        }
        if (export_ != nullptr) {
            return export_->get_interface();
        }
        throw std::logic_error{"access through unbound sc_export"};
    }

    [[nodiscard]] Interface* operator->() const {
        return &get_interface();
    }
    operator Interface&() const { return get_interface(); }
    [[nodiscard]] fsim_sc_handle_v1 native_handle() const noexcept {
        return handle_;
    }

private:
    Interface* interface_{};
    sc_export* export_{};
    fsim_sc_handle_v1 handle_{};
};

class sc_event_finder final {
public:
    constexpr sc_event_finder(
        const fsim_sc_handle_v1 handle,
        const fsim_sc_edge_kind_v1 edge) noexcept
        : handle_(handle), edge_(edge) {}

    [[nodiscard]] constexpr fsim_sc_handle_v1 native_handle() const noexcept {
        return handle_;
    }
    [[nodiscard]] constexpr fsim_sc_edge_kind_v1 edge_kind() const noexcept {
        return edge_;
    }

private:
    fsim_sc_handle_v1 handle_{};
    fsim_sc_edge_kind_v1 edge_{FSIM_SC_ANY_EDGE};
};

class sc_sensitive final {
public:
    explicit sc_sensitive(sc_module* owner) noexcept;

    template <typename T>
    sc_sensitive& operator<<(const T& object);

private:
    sc_module* owner_;
};

class sc_module : public sc_object {
    class ProcessObject final : public sc_object {
    public:
        ProcessObject(
            const char* name,
            fsim_sc_process_kind_v1 kind)
            : sc_object(name, kind_name(kind)) {}

    private:
        [[nodiscard]] static const char* kind_name(
            fsim_sc_process_kind_v1 kind) noexcept {
            switch (kind) {
            case FSIM_SC_METHOD:
                return "sc_method_process";
            case FSIM_SC_THREAD:
                return "sc_thread_process";
            case FSIM_SC_CTHREAD:
                return "sc_cthread_process";
            }
            return "sc_process";
        }
    };

public:
    sc_module();

    explicit sc_module(const sc_module_name name);
    virtual ~sc_module() = default;

    sc_module(const sc_module&) = delete;
    sc_module& operator=(const sc_module&) = delete;

    template <typename Function>
    void fsim_register_process(
        std::string name, const fsim_sc_process_kind_v1 kind, Function&& function) {
        auto object = std::make_unique<ProcessObject>(name.c_str(), kind);
        processes_.push_back(Process{
            std::move(object),
            std::move(name),
            kind,
            std::function<void()>{std::forward<Function>(function)},
            {},
            true,
            nullptr});
    }

    void fsim_add_sensitivity(
        const fsim_sc_handle_v1 object,
        const fsim_sc_edge_kind_v1 edge = FSIM_SC_ANY_EDGE);

    void dont_initialize() noexcept;

    [[nodiscard]] fsim_sc_status_v1 fsim_elaborate(
        const fsim_sc_host_v1* host,
        const fsim_sc_handle_v1 module);

    [[nodiscard]] fsim_sc_status_v1 fsim_register_lifecycle(
        const fsim_sc_host_v1* host,
        const fsim_sc_handle_v1 module);

    sc_sensitive sensitive;

protected:
    void fsim_mark_hdl_proxy() noexcept;

    virtual void before_end_of_elaboration();
    virtual void end_of_elaboration();
    virtual void start_of_simulation();
    virtual void end_of_simulation();

    struct Sensitivity {
        fsim_sc_handle_v1 object{};
        fsim_sc_edge_kind_v1 edge{FSIM_SC_ANY_EDGE};
    };

    struct Process {
        std::unique_ptr<ProcessObject> object;
        std::string name;
        fsim_sc_process_kind_v1 kind;
        std::function<void()> entry;
        std::vector<Sensitivity> sensitivity;
        bool initialize{true};
        const fsim_sc_host_v1* host{};
    };

    [[nodiscard]] const std::vector<Process>& fsim_processes() const noexcept;

private:
    enum class LifecyclePhase : std::uint8_t {
        before_end_of_elaboration,
        end_of_elaboration,
        start_of_simulation,
        end_of_simulation,
    };

    void fsim_invoke_lifecycle(const LifecyclePhase phase);

    static void invoke_lifecycle(
        void* user,
        const LifecyclePhase phase,
        const char* unknown_failure) noexcept;

    static void invoke_before_end_of_elaboration(
        void* user) noexcept;

    static void invoke_end_of_elaboration(void* user) noexcept;

    static void invoke_start_of_simulation(void* user) noexcept;

    static void invoke_end_of_simulation(void* user) noexcept;

    void attach_to_current_parent();

    void attach_to_parent(sc_module* parent);

    void fsim_register_object_name(std::string_view name);

    [[nodiscard]] const char* fsim_unique_name(std::string_view base);

    static void invoke_process(void* user) noexcept;

    fsim_sc_handle_v1 handle_{};
    std::vector<sc_module*> children_;
    std::vector<Process> processes_;
    std::unordered_set<std::string> object_names_;
    std::unordered_map<std::string, std::uint64_t>
        unique_name_counters_;
    std::deque<std::string> unique_names_;
    const fsim_sc_host_v1* lifecycle_host_{};
    bool hdl_proxy_{};

    friend class sc_object;
    friend const char* sc_gen_unique_name(const char* base);
};


} // namespace sc_core
