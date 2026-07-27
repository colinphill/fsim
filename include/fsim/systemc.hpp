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
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
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

    sc_time(const double value, const sc_time_unit unit) {
        const auto scale = scale_for(unit);
        const auto femtoseconds =
            static_cast<long double>(value)
            * static_cast<long double>(scale);
        if (!std::isfinite(value) || value < 0.0
            || femtoseconds
                > static_cast<long double>(
                    std::numeric_limits<std::uint64_t>::max())) {
            throw std::out_of_range{"sc_time is outside the fsim time range"};
        }
        const auto integral = std::round(femtoseconds);
        const auto tolerance =
            static_cast<long double>(
                std::numeric_limits<double>::epsilon())
            * std::max(1.0L, std::fabs(femtoseconds)) * 4.0L;
        if (std::fabs(femtoseconds - integral) > tolerance) {
            throw std::invalid_argument{
                "sc_time must be exactly representable in femtoseconds"};
        }
        if (integral
            > static_cast<long double>(
                std::numeric_limits<std::uint64_t>::max())) {
            throw std::out_of_range{"sc_time is outside the fsim time range"};
        }
        femtoseconds_ = static_cast<std::uint64_t>(integral);
    }

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

inline thread_local const fsim_sc_host_v1* current_host = nullptr;
inline thread_local fsim_sc_handle_v1 current_module = 0;
inline thread_local fsim_sc_process_kind_v1 current_process_kind =
    FSIM_SC_THREAD;
inline thread_local fsim_sc_handle_v1 construction_root_module = 0;
inline thread_local sc_module* current_cpp_module = nullptr;
inline thread_local const char* current_module_name = nullptr;

inline void bind_host(const fsim_sc_host_v1* host) noexcept {
    current_host = host;
}

class host_scope final {
public:
    host_scope(
        const fsim_sc_host_v1* host,
        const fsim_sc_handle_v1 module = 0) noexcept
        : previous_host_(current_host),
          previous_module_(current_module) {
        current_host = host;
        current_module = module;
    }

    ~host_scope() {
        current_host = previous_host_;
        current_module = previous_module_;
    }

    host_scope(const host_scope&) = delete;
    host_scope& operator=(const host_scope&) = delete;

private:
    const fsim_sc_host_v1* previous_host_;
    fsim_sc_handle_v1 previous_module_;
};

class module_construction_scope final {
public:
    explicit module_construction_scope(
        const fsim_sc_handle_v1 root_module) noexcept
        : previous_root_(construction_root_module),
          previous_cpp_module_(current_cpp_module) {
        construction_root_module = root_module;
        current_cpp_module = nullptr;
    }

    ~module_construction_scope() {
        construction_root_module = previous_root_;
        current_cpp_module = previous_cpp_module_;
    }

    module_construction_scope(const module_construction_scope&) = delete;
    module_construction_scope& operator=(
        const module_construction_scope&) = delete;

private:
    fsim_sc_handle_v1 previous_root_{};
    sc_module* previous_cpp_module_{};
};

inline void check_status(const fsim_sc_status_v1 status, const char* operation) {
    if (status != FSIM_SC_OK) {
        throw std::runtime_error{std::string{"fsim SystemC host failed to "} + operation};
    }
}

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

} // namespace detail

class sc_event {
public:
    sc_event() : handle_(detail::register_event(nullptr)) {}
    explicit sc_event(const char* name)
        : handle_(detail::register_event(name)) {}
    explicit sc_event(const fsim_sc_handle_v1 handle) noexcept : handle_(handle) {}

    void notify() const {
        notify_impl(SC_ZERO_TIME, FSIM_SC_NOTIFY_IMMEDIATE);
    }

    void notify(const sc_time delay) const {
        notify_impl(
            delay,
            delay.is_zero()
                ? FSIM_SC_NOTIFY_DELTA
                : FSIM_SC_NOTIFY_TIMED);
    }

    void cancel() const {
        if (detail::current_host == nullptr
            || detail::current_host->cancel_event == nullptr) {
            throw std::logic_error{
                "sc_event::cancel requires an active fsim SystemC process"};
        }
        detail::check_status(
            detail::current_host->cancel_event(
                detail::current_host->context, handle_),
            "cancel event");
    }

    void notify_delayed() const {
        notify_delayed(SC_ZERO_TIME);
    }

    void notify_delayed(const sc_time delay) const {
        if (detail::current_host == nullptr
            || detail::current_host->notify_event_delayed == nullptr) {
            throw std::logic_error{
                "sc_event::notify_delayed requires an active fsim "
                "SystemC process"};
        }
        detail::check_status(
            detail::current_host->notify_event_delayed(
                detail::current_host->context,
                handle_,
                delay.value()),
            "notify event with notify_delayed");
    }

    [[nodiscard]] fsim_sc_handle_v1 native_handle() const noexcept { return handle_; }

private:
    void notify_impl(
        const sc_time delay,
        const fsim_sc_notification_kind_v1 kind) const {
        if (detail::current_host == nullptr
            || detail::current_host->notify_event_mode == nullptr) {
            throw std::logic_error{
                "sc_event::notify requires an active fsim SystemC process"};
        }
        detail::check_status(
            detail::current_host->notify_event_mode(
                detail::current_host->context,
                handle_,
                delay.value(),
                kind),
            "notify event");
    }

    fsim_sc_handle_v1 handle_{};
};

class sc_event_or_list {
public:
    sc_event_or_list() = default;
    explicit sc_event_or_list(const sc_event& event) {
        append(event.native_handle());
    }

    sc_event_or_list& operator|=(const sc_event& event) {
        append(event.native_handle());
        return *this;
    }
    sc_event_or_list& operator|=(const sc_event_or_list& events) {
        for (const auto event : events.events_) {
            append(event);
        }
        return *this;
    }

    [[nodiscard]] const std::vector<fsim_sc_handle_v1>&
    native_handles() const noexcept {
        return events_;
    }

private:
    void append(const fsim_sc_handle_v1 event) {
        if (std::find(events_.begin(), events_.end(), event)
            == events_.end()) {
            events_.push_back(event);
        }
    }

    std::vector<fsim_sc_handle_v1> events_;
};

class sc_event_and_list {
public:
    sc_event_and_list() = default;
    explicit sc_event_and_list(const sc_event& event) {
        append(event.native_handle());
    }

    sc_event_and_list& operator&=(const sc_event& event) {
        append(event.native_handle());
        return *this;
    }
    sc_event_and_list& operator&=(const sc_event_and_list& events) {
        for (const auto event : events.events_) {
            append(event);
        }
        return *this;
    }

    [[nodiscard]] const std::vector<fsim_sc_handle_v1>&
    native_handles() const noexcept {
        return events_;
    }

private:
    void append(const fsim_sc_handle_v1 event) {
        if (std::find(events_.begin(), events_.end(), event)
            == events_.end()) {
            events_.push_back(event);
        }
    }

    std::vector<fsim_sc_handle_v1> events_;
};

inline sc_event_or_list operator|(
    const sc_event& left, const sc_event& right) {
    sc_event_or_list result{left};
    result |= right;
    return result;
}

inline sc_event_or_list operator|(
    sc_event_or_list left, const sc_event& right) {
    left |= right;
    return left;
}

inline sc_event_or_list operator|(
    const sc_event& left, sc_event_or_list right) {
    sc_event_or_list result{left};
    result |= right;
    return result;
}

inline sc_event_or_list operator|(
    sc_event_or_list left, const sc_event_or_list& right) {
    left |= right;
    return left;
}

inline sc_event_and_list operator&(
    const sc_event& left, const sc_event& right) {
    sc_event_and_list result{left};
    result &= right;
    return result;
}

inline sc_event_and_list operator&(
    sc_event_and_list left, const sc_event& right) {
    left &= right;
    return left;
}

inline sc_event_and_list operator&(
    const sc_event& left, sc_event_and_list right) {
    sc_event_and_list result{left};
    result &= right;
    return result;
}

inline sc_event_and_list operator&(
    sc_event_and_list left, const sc_event_and_list& right) {
    left &= right;
    return left;
}

namespace detail {

inline void set_event_list_trigger(
    const std::vector<fsim_sc_handle_v1>& events,
    const fsim_sc_event_list_kind_v1 kind,
    const char* operation) {
    if (current_host == nullptr
        || current_host->wait_event_list == nullptr) {
        throw std::logic_error{
            std::string{"sc_core::"} + operation
            + " requires an active fsim SystemC process"};
    }
    check_status(
        current_host->wait_event_list(
            current_host->context,
            events.data(),
            events.size(),
            kind),
        operation);
}

} // namespace detail

inline void wait(const sc_time delay) {
    if (detail::current_host == nullptr || detail::current_host->wait_time == nullptr) {
        throw std::logic_error{"sc_core::wait requires an active fsim SystemC process"};
    }
    if (detail::current_process_kind == FSIM_SC_METHOD) {
        throw std::logic_error{
            "SC_METHOD cannot call wait; use next_trigger"};
    }
    detail::check_status(
        detail::current_host->wait_time(detail::current_host->context, delay.value()),
        "wait for time");
}

inline void wait(const sc_event& event) {
    if (detail::current_host == nullptr || detail::current_host->wait_event == nullptr) {
        throw std::logic_error{"sc_core::wait requires an active fsim SystemC process"};
    }
    if (detail::current_process_kind == FSIM_SC_METHOD) {
        throw std::logic_error{
            "SC_METHOD cannot call wait; use next_trigger"};
    }
    detail::check_status(
        detail::current_host->wait_event(
            detail::current_host->context, event.native_handle()),
        "wait for event");
}

inline void wait(const sc_event_or_list& events) {
    if (detail::current_process_kind == FSIM_SC_METHOD) {
        throw std::logic_error{
            "SC_METHOD cannot call wait; use next_trigger"};
    }
    detail::set_event_list_trigger(
        events.native_handles(),
        FSIM_SC_EVENT_OR_LIST,
        "wait for event OR list");
}

inline void wait(const sc_event_and_list& events) {
    if (detail::current_process_kind == FSIM_SC_METHOD) {
        throw std::logic_error{
            "SC_METHOD cannot call wait; use next_trigger"};
    }
    detail::set_event_list_trigger(
        events.native_handles(),
        FSIM_SC_EVENT_AND_LIST,
        "wait for event AND list");
}

inline void wait() {
    if (detail::current_host == nullptr
        || detail::current_host->wait_static == nullptr) {
        throw std::logic_error{
            "plain wait() requires an active fsim SystemC thread"};
    }
    if (detail::current_process_kind == FSIM_SC_METHOD) {
        throw std::logic_error{
            "SC_METHOD cannot call wait; use next_trigger"};
    }
    detail::check_status(
        detail::current_host->wait_static(
            detail::current_host->context),
        "wait on static sensitivity");
}

inline void next_trigger(const sc_time delay) {
    if (detail::current_host == nullptr
        || detail::current_host->wait_time == nullptr) {
        throw std::logic_error{
            "sc_core::next_trigger requires an active fsim SystemC process"};
    }
    detail::check_status(
        detail::current_host->wait_time(
            detail::current_host->context, delay.value()),
        "set next time trigger");
}

inline void next_trigger(const sc_event& event) {
    if (detail::current_host == nullptr
        || detail::current_host->wait_event == nullptr) {
        throw std::logic_error{
            "sc_core::next_trigger requires an active fsim SystemC process"};
    }
    detail::check_status(
        detail::current_host->wait_event(
            detail::current_host->context, event.native_handle()),
        "set next event trigger");
}

inline void next_trigger(const sc_event_or_list& events) {
    detail::set_event_list_trigger(
        events.native_handles(),
        FSIM_SC_EVENT_OR_LIST,
        "set next event OR trigger");
}

inline void next_trigger(const sc_event_and_list& events) {
    detail::set_event_list_trigger(
        events.native_handles(),
        FSIM_SC_EVENT_AND_LIST,
        "set next event AND trigger");
}

class sc_module_name {
public:
    sc_module_name(const char* name)
        : state_(std::make_shared<State>()) {
        if (name == nullptr || *name == '\0') {
            throw std::invalid_argument{
                "sc_module_name must not be empty"};
        }
        state_->name = name;
        state_->previous_module = detail::current_module;
        state_->previous_cpp_module = detail::current_cpp_module;
        state_->previous_name = detail::current_module_name;
        detail::current_module_name = state_->name.c_str();
        if (detail::current_host == nullptr
            || detail::current_module == 0) {
            return;
        }
        if (detail::construction_root_module
            == detail::current_module) {
            state_->handle = detail::current_module;
            detail::construction_root_module = 0;
            return;
        }
        if (detail::current_host->register_native_module == nullptr) {
            throw std::logic_error{
                "nested SystemC module construction requires an "
                "active hierarchy host"};
        }
        fsim_sc_handle_v1 child = 0;
        detail::check_status(
            detail::current_host->register_native_module(
                detail::current_host->context,
                detail::current_module,
                state_->name.c_str(),
                &child),
            "register native child module");
        if (child == 0) {
            throw std::runtime_error{
                "fsim SystemC host returned an invalid child handle"};
        }
        state_->handle = child;
        state_->restore_module = true;
        detail::current_module = child;
    }

    [[nodiscard]] const char* c_str() const noexcept {
        return state_->name.c_str();
    }
    [[nodiscard]] operator const char*() const noexcept {
        return c_str();
    }

private:
    struct State {
        ~State() {
            detail::current_cpp_module = previous_cpp_module;
            detail::current_module_name = previous_name;
            if (restore_module) {
                detail::current_module = previous_module;
            }
        }

        std::string name;
        fsim_sc_handle_v1 handle{};
        fsim_sc_handle_v1 previous_module{};
        sc_module* previous_cpp_module{};
        const char* previous_name{};
        bool restore_module{};
    };

    [[nodiscard]] fsim_sc_handle_v1 native_handle() const noexcept {
        return state_->handle;
    }
    [[nodiscard]] sc_module* previous_cpp_module() const noexcept {
        return state_->previous_cpp_module;
    }

    std::shared_ptr<State> state_;

    friend class sc_module;
};

class sc_interface {
public:
    virtual ~sc_interface() = default;
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

class sc_prim_channel {
public:
    virtual ~sc_prim_channel() = default;

    sc_prim_channel(const sc_prim_channel&) = delete;
    sc_prim_channel& operator=(const sc_prim_channel&) = delete;

    [[nodiscard]] bool update_requested() const noexcept {
        return update_requested_;
    }

    void request_update() {
        if (update_requested_) {
            return;
        }
        if (host_ == nullptr || handle_ == 0
            || host_->request_update == nullptr) {
            throw std::logic_error{
                "sc_prim_channel::request_update requires an active "
                "fsim SystemC kernel"};
        }
        detail::check_status(
            host_->request_update(host_->context, handle_),
            "request primitive-channel update");
        update_requested_ = true;
    }

    [[nodiscard]] fsim_sc_handle_v1 native_handle() const noexcept {
        return handle_;
    }

protected:
    sc_prim_channel()
        : sc_prim_channel(nullptr) {}

    explicit sc_prim_channel(const char* name)
        : host_(detail::current_host),
          handle_(detail::register_primitive_channel(
              name, invoke_update, this)) {}

    virtual void update() {}

private:
    static void invoke_update(void* user) noexcept {
        auto* channel = static_cast<sc_prim_channel*>(user);
        if (channel == nullptr || channel->host_ == nullptr) {
            return;
        }
        detail::host_scope scope{channel->host_};
        try {
            channel->update();
        } catch (const std::exception& exception) {
            if (channel->host_->report != nullptr) {
                channel->host_->report(
                    channel->host_->context, 3, exception.what());
            }
        } catch (...) {
            if (channel->host_->report != nullptr) {
                channel->host_->report(
                    channel->host_->context,
                    3,
                    "SystemC primitive-channel update threw an "
                    "unknown exception");
            }
        }
        channel->update_requested_ = false;
    }

    const fsim_sc_host_v1* host_{};
    fsim_sc_handle_v1 handle_{};
    bool update_requested_{};
};

template <typename Interface>
class sc_export {
    static_assert(
        std::is_base_of_v<sc_interface, Interface>,
        "sc_export requires an sc_interface-derived type");

public:
    sc_export() = default;
    explicit sc_export(const char* name)
        : handle_(detail::register_export<Interface>(name)) {}

    void bind(Interface& interface) {
        if constexpr (
            detail::signal_interface_traits<Interface>::supported) {
            detail::bind_export(
                handle_, interface.native_handle());
        }
        interface_ = &interface;
        export_ = nullptr;
    }
    void operator()(Interface& interface) noexcept { bind(interface); }

    void bind(sc_export& target) {
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
    explicit sc_sensitive(sc_module* owner) noexcept : owner_(owner) {}

    template <typename T>
    sc_sensitive& operator<<(const T& object);

private:
    sc_module* owner_;
};

class sc_module {
public:
    sc_module()
        : sensitive(this),
          name_(
              detail::current_module_name == nullptr
                  ? throw std::logic_error{
                        "sc_module construction requires an active "
                        "sc_module_name"}
                  : detail::current_module_name),
          handle_(detail::current_module) {
        attach_to_current_parent();
    }

    explicit sc_module(const sc_module_name name)
        : sensitive(this),
          name_(
              name.c_str() == nullptr
                  ? throw std::invalid_argument{
                        "sc_module_name must not be null"}
                  : name.c_str()),
          handle_(name.native_handle()) {
        attach_to_parent(name.previous_cpp_module());
    }
    virtual ~sc_module() = default;

    sc_module(const sc_module&) = delete;
    sc_module& operator=(const sc_module&) = delete;

    [[nodiscard]] const char* name() const noexcept { return name_.c_str(); }

    template <typename Function>
    void fsim_register_process(
        std::string name, const fsim_sc_process_kind_v1 kind, Function&& function) {
        processes_.push_back(Process{
            std::move(name),
            kind,
            std::function<void()>{std::forward<Function>(function)},
            {},
            true,
            nullptr});
    }

    void fsim_add_sensitivity(
        const fsim_sc_handle_v1 object,
        const fsim_sc_edge_kind_v1 edge = FSIM_SC_ANY_EDGE) {
        if (!processes_.empty()) {
            processes_.back().sensitivity.push_back({object, edge});
        }
    }

    void dont_initialize() noexcept {
        if (!processes_.empty()) {
            processes_.back().initialize = false;
        }
    }

    [[nodiscard]] fsim_sc_status_v1 fsim_elaborate(
        const fsim_sc_host_v1* host,
        const fsim_sc_handle_v1 module) {
        if (host == nullptr || host->register_process == nullptr
            || host->add_sensitivity == nullptr) {
            return FSIM_SC_ABI_MISMATCH;
        }
        if (handle_ != 0 && handle_ != module) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        for (auto* child : children_) {
            if (child == nullptr || child->handle_ == 0) {
                return FSIM_SC_RUNTIME_ERROR;
            }
            const auto status =
                child->fsim_elaborate(host, child->handle_);
            if (status != FSIM_SC_OK) {
                return status;
            }
        }
        for (auto& process : processes_) {
            process.host = host;
            fsim_sc_handle_v1 handle = 0;
            auto status = host->register_process(
                host->context,
                module,
                process.name.c_str(),
                process.kind,
                invoke_process,
                &process,
                &handle);
            if (status != FSIM_SC_OK) {
                return status;
            }
            for (const auto& sensitivity : process.sensitivity) {
                status = host->add_sensitivity(
                    host->context,
                    handle,
                    sensitivity.object,
                    sensitivity.edge);
                if (status != FSIM_SC_OK) {
                    return status;
                }
            }
            if (!process.initialize) {
                if (host->set_process_initialize == nullptr) {
                    return FSIM_SC_ABI_MISMATCH;
                }
                status = host->set_process_initialize(
                    host->context, handle, 0);
                if (status != FSIM_SC_OK) {
                    return status;
                }
            }
        }
        return FSIM_SC_OK;
    }

    [[nodiscard]] fsim_sc_status_v1 fsim_register_lifecycle(
        const fsim_sc_host_v1* host,
        const fsim_sc_handle_v1 module) {
        if (host == nullptr || host->register_lifecycle == nullptr
            || module == 0 || handle_ != module) {
            return FSIM_SC_ABI_MISMATCH;
        }
        lifecycle_host_ = host;
        return host->register_lifecycle(
            host->context,
            module,
            invoke_before_end_of_elaboration,
            invoke_end_of_elaboration,
            invoke_start_of_simulation,
            invoke_end_of_simulation,
            this);
    }

    sc_sensitive sensitive;

protected:
    virtual void before_end_of_elaboration() {}
    virtual void end_of_elaboration() {}
    virtual void start_of_simulation() {}
    virtual void end_of_simulation() {}

    struct Sensitivity {
        fsim_sc_handle_v1 object{};
        fsim_sc_edge_kind_v1 edge{FSIM_SC_ANY_EDGE};
    };

    struct Process {
        std::string name;
        fsim_sc_process_kind_v1 kind;
        std::function<void()> entry;
        std::vector<Sensitivity> sensitivity;
        bool initialize{true};
        const fsim_sc_host_v1* host{};
    };

    [[nodiscard]] const std::vector<Process>& fsim_processes() const noexcept {
        return processes_;
    }

private:
    enum class LifecyclePhase : std::uint8_t {
        before_end_of_elaboration,
        end_of_elaboration,
        start_of_simulation,
        end_of_simulation,
    };

    void fsim_invoke_lifecycle(const LifecyclePhase phase) {
        if (phase == LifecyclePhase::end_of_simulation) {
            for (auto child = children_.rbegin();
                 child != children_.rend(); ++child) {
                (*child)->fsim_invoke_lifecycle(phase);
            }
            end_of_simulation();
            return;
        }
        switch (phase) {
        case LifecyclePhase::before_end_of_elaboration:
            before_end_of_elaboration();
            break;
        case LifecyclePhase::end_of_elaboration:
            end_of_elaboration();
            break;
        case LifecyclePhase::start_of_simulation:
            start_of_simulation();
            break;
        case LifecyclePhase::end_of_simulation:
            break;
        }
        for (auto* child : children_) {
            child->fsim_invoke_lifecycle(phase);
        }
    }

    static void invoke_lifecycle(
        void* user,
        const LifecyclePhase phase,
        const char* unknown_failure) noexcept {
        auto* module = static_cast<sc_module*>(user);
        if (module == nullptr || module->lifecycle_host_ == nullptr) {
            return;
        }
        detail::host_scope scope{module->lifecycle_host_};
        try {
            module->fsim_invoke_lifecycle(phase);
        } catch (const std::exception& exception) {
            if (module->lifecycle_host_->report != nullptr) {
                module->lifecycle_host_->report(
                    module->lifecycle_host_->context,
                    3,
                    exception.what());
            }
        } catch (...) {
            if (module->lifecycle_host_->report != nullptr) {
                module->lifecycle_host_->report(
                    module->lifecycle_host_->context,
                    3,
                    unknown_failure);
            }
        }
    }

    static void invoke_before_end_of_elaboration(
        void* user) noexcept {
        invoke_lifecycle(
            user,
            LifecyclePhase::before_end_of_elaboration,
            "SystemC before_end_of_elaboration callback threw an "
            "unknown exception");
    }

    static void invoke_end_of_elaboration(void* user) noexcept {
        invoke_lifecycle(
            user,
            LifecyclePhase::end_of_elaboration,
            "SystemC end_of_elaboration callback threw an "
            "unknown exception");
    }

    static void invoke_start_of_simulation(void* user) noexcept {
        invoke_lifecycle(
            user,
            LifecyclePhase::start_of_simulation,
            "SystemC start_of_simulation callback threw an "
            "unknown exception");
    }

    static void invoke_end_of_simulation(void* user) noexcept {
        invoke_lifecycle(
            user,
            LifecyclePhase::end_of_simulation,
            "SystemC end_of_simulation callback threw an "
            "unknown exception");
    }

    void attach_to_current_parent() {
        attach_to_parent(detail::current_cpp_module);
    }

    void attach_to_parent(sc_module* parent) {
        if (parent != nullptr) {
            parent->children_.push_back(this);
        }
        detail::current_cpp_module = this;
    }

    static void invoke_process(void* user) noexcept {
        auto* process = static_cast<Process*>(user);
        if (process == nullptr || process->host == nullptr) {
            return;
        }
        detail::host_scope scope{process->host};
        const auto previous_kind = detail::current_process_kind;
        detail::current_process_kind = process->kind;
        try {
            process->entry();
        } catch (const std::exception& exception) {
            if (process->host->report != nullptr) {
                process->host->report(
                    process->host->context, 3, exception.what());
            }
        } catch (...) {
            if (process->host->report != nullptr) {
                process->host->report(
                    process->host->context,
                    3,
                    "SystemC process threw an unknown exception");
            }
        }
        detail::current_process_kind = previous_kind;
    }

    std::string name_;
    fsim_sc_handle_v1 handle_{};
    std::vector<sc_module*> children_;
    std::vector<Process> processes_;
    const fsim_sc_host_v1* lifecycle_host_{};
};

template <typename T>
sc_sensitive& sc_sensitive::operator<<(const T& object) {
    if constexpr (requires { object.edge_kind(); }) {
        owner_->fsim_add_sensitivity(
            object.native_handle(), object.edge_kind());
    } else {
        owner_->fsim_add_sensitivity(object.native_handle());
    }
    return *this;
}

template <typename T>
class sc_signal
    : public sc_signal_inout_if<T>,
      public sc_prim_channel {
public:
    using value_type = T;

    sc_signal()
        : sc_prim_channel(),
          value_changed_event_(native_handle()) {
        detail::register_signal<T>(
            native_handle(), nullptr, value_);
    }

    explicit sc_signal(const char* name)
        : sc_prim_channel(name),
          value_changed_event_(native_handle()) {
        detail::register_signal<T>(
            native_handle(), name, value_);
    }

    sc_signal(const char* name, const T& initial_value)
        : sc_prim_channel(name),
          value_(initial_value),
          pending_(initial_value),
          value_changed_event_(native_handle()) {
        detail::register_signal<T>(
            native_handle(), name, value_);
    }

    [[nodiscard]] const T& read() const override {
        if (native_handle() != 0 && detail::current_host != nullptr) {
            value_ = detail::read_object<T>(native_handle());
        }
        return value_;
    }
    void write(const T& value) override {
        if (native_handle() == 0) {
            event_ = value_ != value;
            value_ = value;
            pending_ = value;
            return;
        }
        pending_ = value;
        request_update();
    }
    [[nodiscard]] bool event() const override {
        return native_handle() == 0
            ? event_
            : detail::object_event(native_handle());
    }
    [[nodiscard]] const sc_event&
    default_event() const noexcept override {
        return value_changed_event_;
    }
    [[nodiscard]] const sc_event&
    value_changed_event() const noexcept override {
        return value_changed_event_;
    }
    [[nodiscard]] fsim_sc_handle_v1
    native_handle() const noexcept override {
        return sc_prim_channel::native_handle();
    }
    [[nodiscard]] sc_event_finder pos() const noexcept {
        return {native_handle(), FSIM_SC_POSEDGE};
    }
    [[nodiscard]] sc_event_finder neg() const noexcept {
        return {native_handle(), FSIM_SC_NEGEDGE};
    }

    operator const T&() const { return read(); }
    sc_signal& operator=(const T& value) {
        write(value);
        return *this;
    }

protected:
    void update() override {
        detail::write_object(native_handle(), pending_);
    }

private:
    mutable T value_{};
    T pending_{};
    sc_event value_changed_event_;
    bool event_{};
};

template <typename T>
class sc_in {
public:
    using value_type = T;

    sc_in() = default;
    explicit sc_in(const char* name)
        : handle_(
              detail::register_port<T>(name, FSIM_SC_INPUT)) {}

    void bind(const sc_signal_in_if<T>& interface) {
        detail::bind_port(handle_, interface.native_handle());
        interface_ = &interface;
        port_ = nullptr;
    }
    void operator()(const sc_signal_in_if<T>& interface) {
        bind(interface);
    }
    void bind(const sc_in& port) {
        for (auto* current = &port;
             current != nullptr; current = current->port_) {
            if (current == this) {
                throw std::logic_error{"cyclic sc_in binding"};
            }
        }
        detail::bind_port(handle_, port.native_handle());
        interface_ = nullptr;
        port_ = &port;
    }
    void operator()(const sc_in& port) { bind(port); }
    template <typename Interface>
        requires std::is_base_of_v<
            sc_signal_in_if<T>, Interface>
    void bind(const sc_export<Interface>& export_object) {
        detail::bind_port(
            handle_, export_object.native_handle());
        interface_ = &export_object.get_interface();
        port_ = nullptr;
    }
    template <typename Interface>
        requires std::is_base_of_v<
            sc_signal_in_if<T>, Interface>
    void operator()(const sc_export<Interface>& export_object) {
        bind(export_object);
    }
    [[nodiscard]] const T& read() const {
        if (interface_ != nullptr) {
            return interface_->read();
        }
        if (port_ != nullptr) {
            return port_->read();
        }
        if (handle_ == 0) {
            throw std::logic_error{"read from unbound sc_in"};
        }
        value_ = detail::read_object<T>(handle_);
        return value_;
    }
    [[nodiscard]] fsim_sc_handle_v1 native_handle() const noexcept {
        if (interface_ != nullptr) {
            return interface_->native_handle();
        }
        return port_ == nullptr ? handle_ : port_->native_handle();
    }
    [[nodiscard]] sc_event_finder pos() const noexcept {
        return {native_handle(), FSIM_SC_POSEDGE};
    }
    [[nodiscard]] sc_event_finder neg() const noexcept {
        return {native_handle(), FSIM_SC_NEGEDGE};
    }
    operator const T&() const { return read(); }

private:
    const sc_signal_in_if<T>* interface_{};
    const sc_in* port_{};
    fsim_sc_handle_v1 handle_{};
    mutable T value_{};
};

template <typename T>
class sc_out {
public:
    using value_type = T;

    sc_out() = default;
    explicit sc_out(const char* name)
        : sc_out(name, FSIM_SC_OUTPUT) {}

protected:
    sc_out(
        const char* name,
        const fsim_sc_port_direction_v1 direction)
        : handle_(
              detail::register_port<T>(name, direction)) {}

public:

    void bind(sc_signal_inout_if<T>& interface) {
        detail::bind_port(handle_, interface.native_handle());
        interface_ = &interface;
        port_ = nullptr;
    }
    void operator()(sc_signal_inout_if<T>& interface) {
        bind(interface);
    }
    void bind(sc_out& port) {
        for (auto* current = &port;
             current != nullptr; current = current->port_) {
            if (current == this) {
                throw std::logic_error{"cyclic sc_out binding"};
            }
        }
        detail::bind_port(handle_, port.native_handle());
        interface_ = nullptr;
        port_ = &port;
    }
    void operator()(sc_out& port) { bind(port); }
    template <typename Interface>
        requires std::is_base_of_v<
            sc_signal_inout_if<T>, Interface>
    void bind(const sc_export<Interface>& export_object) {
        detail::bind_port(
            handle_, export_object.native_handle());
        interface_ = &export_object.get_interface();
        port_ = nullptr;
    }
    template <typename Interface>
        requires std::is_base_of_v<
            sc_signal_inout_if<T>, Interface>
    void operator()(const sc_export<Interface>& export_object) {
        bind(export_object);
    }
    void write(const T& value) {
        if (interface_ != nullptr) {
            interface_->write(value);
            return;
        }
        if (port_ != nullptr) {
            port_->write(value);
            return;
        }
        if (handle_ == 0) {
            throw std::logic_error{"write to unbound sc_out"};
        }
        detail::write_object(handle_, value);
    }
    [[nodiscard]] fsim_sc_handle_v1 native_handle() const noexcept {
        if (interface_ != nullptr) {
            return interface_->native_handle();
        }
        return port_ == nullptr ? handle_ : port_->native_handle();
    }
    sc_out& operator=(const T& value) {
        write(value);
        return *this;
    }

private:
    sc_signal_inout_if<T>* interface_{};
    sc_out* port_{};
    fsim_sc_handle_v1 handle_{};
};

template <typename T>
class sc_inout : public sc_out<T> {
public:
    sc_inout() = default;
    explicit sc_inout(const char* name)
        : sc_out<T>(name, FSIM_SC_INOUT) {}

    void bind(sc_signal_inout_if<T>& interface) {
        sc_out<T>::bind(interface);
        input_.bind(interface);
        port_ = nullptr;
    }
    void operator()(sc_signal_inout_if<T>& interface) {
        bind(interface);
    }
    void bind(sc_inout& port) {
        sc_out<T>::bind(static_cast<sc_out<T>&>(port));
        port_ = &port;
    }
    void operator()(sc_inout& port) { bind(port); }
    template <typename Interface>
        requires std::is_base_of_v<
            sc_signal_inout_if<T>, Interface>
    void bind(const sc_export<Interface>& export_object) {
        sc_out<T>::bind(export_object);
        input_.bind(export_object.get_interface());
        port_ = nullptr;
    }
    template <typename Interface>
        requires std::is_base_of_v<
            sc_signal_inout_if<T>, Interface>
    void operator()(const sc_export<Interface>& export_object) {
        bind(export_object);
    }
    [[nodiscard]] const T& read() const {
        if (port_ != nullptr) {
            return port_->read();
        }
        if (this->native_handle() != 0) {
            value_ = detail::read_object<T>(this->native_handle());
            return value_;
        }
        return input_.read();
    }
    [[nodiscard]] sc_event_finder pos() const noexcept {
        return {this->native_handle(), FSIM_SC_POSEDGE};
    }
    [[nodiscard]] sc_event_finder neg() const noexcept {
        return {this->native_handle(), FSIM_SC_NEGEDGE};
    }
    operator const T&() const { return read(); }

private:
    sc_in<T> input_;
    sc_inout* port_{};
    mutable T value_{};
};

inline const char* sc_gen_unique_name(const char* base) {
    if (base == nullptr) {
        throw std::invalid_argument{"sc_gen_unique_name base must not be null"};
    }
    static thread_local std::uint64_t counter = 0;
    static thread_local std::deque<std::string> names;
    names.emplace_back(std::string{base} + "_" + std::to_string(counter++));
    return names.back().c_str();
}

} // namespace sc_core

namespace sc_dt {

class sc_logic final {
public:
    enum value_t : std::uint8_t { Log_0 = 0, Log_1 = 1, Log_Z = 2, Log_X = 3 };

    constexpr sc_logic() noexcept = default;
    constexpr sc_logic(const bool value) noexcept : value_(value ? Log_1 : Log_0) {}
    constexpr sc_logic(const value_t value) : value_(validate(value)) {}
    constexpr explicit sc_logic(const char value) : value_(from_char(value)) {}

    [[nodiscard]] constexpr value_t value() const noexcept { return value_; }
    [[nodiscard]] constexpr char to_char() const noexcept {
        constexpr std::array chars{'0', '1', 'Z', 'X'};
        const auto index = static_cast<std::size_t>(value_);
        return index < chars.size() ? chars[index] : 'X';
    }
    [[nodiscard]] constexpr bool is_01() const noexcept {
        return value_ == Log_0 || value_ == Log_1;
    }
    [[nodiscard]] constexpr bool to_bool() const {
        if (!is_01()) {
            throw std::logic_error{"X or Z cannot be converted to bool"};
        }
        return value_ == Log_1;
    }

    friend constexpr auto operator<=>(const sc_logic&, const sc_logic&) noexcept = default;

private:
    static constexpr value_t validate(const value_t value) {
        if (static_cast<std::uint8_t>(value)
            > static_cast<std::uint8_t>(Log_X)) {
            throw std::invalid_argument{"invalid sc_logic value"};
        }
        return value;
    }

    static constexpr value_t from_char(const char value) {
        switch (value) {
        case '0':
            return Log_0;
        case '1':
            return Log_1;
        case 'z':
        case 'Z':
            return Log_Z;
        case 'x':
        case 'X':
            return Log_X;
        default:
            throw std::invalid_argument{"invalid sc_logic character"};
        }
    }

    value_t value_{Log_X};
};

template <int Width>
class sc_bv final {
    static_assert(Width > 0, "sc_bv width must be positive");

public:
    sc_bv() = default;
    explicit sc_bv(const char* text) { assign(text); }

    void assign(const std::string_view text) {
        if (text.size() != static_cast<std::size_t>(Width)) {
            throw std::invalid_argument{"sc_bv text has the wrong width"};
        }
        for (int i = 0; i < Width; ++i) {
            const auto c = text[static_cast<std::size_t>(Width - 1 - i)];
            if (c != '0' && c != '1') {
                throw std::invalid_argument{"sc_bv accepts only 0 and 1"};
            }
            bits_.set(static_cast<std::size_t>(i), c == '1');
        }
    }

    [[nodiscard]] bool operator[](const std::size_t index) const { return bits_.test(index); }
    [[nodiscard]] std::string to_string() const {
        std::string result(static_cast<std::size_t>(Width), '0');
        for (int i = 0; i < Width; ++i) {
            result[static_cast<std::size_t>(Width - 1 - i)] =
                bits_.test(static_cast<std::size_t>(i)) ? '1' : '0';
        }
        return result;
    }

    friend bool operator==(const sc_bv&, const sc_bv&) = default;

private:
    std::bitset<static_cast<std::size_t>(Width)> bits_;
};

template <int Width>
class sc_lv final {
    static_assert(Width > 0, "sc_lv width must be positive");

public:
    sc_lv() { values_.fill(sc_logic{}); }
    explicit sc_lv(const char* text) { assign(text); }

    void assign(const std::string_view text) {
        if (text.size() != static_cast<std::size_t>(Width)) {
            throw std::invalid_argument{"sc_lv text has the wrong width"};
        }
        for (int i = 0; i < Width; ++i) {
            values_[static_cast<std::size_t>(i)] =
                sc_logic{text[static_cast<std::size_t>(Width - 1 - i)]};
        }
    }

    [[nodiscard]] const sc_logic& operator[](const std::size_t index) const {
        return values_.at(index);
    }
    [[nodiscard]] sc_logic& operator[](const std::size_t index) { return values_.at(index); }
    [[nodiscard]] std::string to_string() const {
        std::string result(static_cast<std::size_t>(Width), 'X');
        for (int i = 0; i < Width; ++i) {
            result[static_cast<std::size_t>(Width - 1 - i)] =
                values_[static_cast<std::size_t>(i)].to_char();
        }
        return result;
    }

    friend bool operator==(const sc_lv&, const sc_lv&) = default;

private:
    std::array<sc_logic, static_cast<std::size_t>(Width)> values_;
};

template <int Width>
class sc_uint final {
    static_assert(Width > 0 && Width <= 64, "sc_uint supports widths 1 through 64");

public:
    constexpr sc_uint() noexcept = default;
    constexpr sc_uint(const std::uint64_t value) noexcept : value_(value & mask()) {}

    constexpr sc_uint& operator=(const std::uint64_t value) noexcept {
        value_ = value & mask();
        return *this;
    }
    [[nodiscard]] constexpr std::uint64_t to_uint64() const noexcept { return value_; }
    constexpr operator std::uint64_t() const noexcept { return value_; }

    friend constexpr auto operator<=>(const sc_uint&, const sc_uint&) noexcept = default;

private:
    static consteval std::uint64_t mask() {
        if constexpr (Width == 64) {
            return std::numeric_limits<std::uint64_t>::max();
        } else {
            return (std::uint64_t{1} << Width) - 1;
        }
    }

    std::uint64_t value_{};
};

template <int Width>
class sc_int final {
    static_assert(Width > 0 && Width <= 64, "sc_int supports widths 1 through 64");

public:
    constexpr sc_int() noexcept = default;
    constexpr sc_int(const std::int64_t value) noexcept : value_(normalize(value)) {}

    constexpr sc_int& operator=(const std::int64_t value) noexcept {
        value_ = normalize(value);
        return *this;
    }
    [[nodiscard]] constexpr std::int64_t to_int64() const noexcept { return value_; }
    constexpr operator std::int64_t() const noexcept { return value_; }

    friend constexpr auto operator<=>(const sc_int&, const sc_int&) noexcept = default;

private:
    static constexpr std::int64_t normalize(const std::int64_t value) noexcept {
        if constexpr (Width == 64) {
            return value;
        } else {
            const auto mask = (std::uint64_t{1} << Width) - 1;
            auto truncated = static_cast<std::uint64_t>(value) & mask;
            const auto sign = std::uint64_t{1} << (Width - 1);
            if ((truncated & sign) != 0) {
                truncated |= ~mask;
            }
            return static_cast<std::int64_t>(truncated);
        }
    }

    std::int64_t value_{};
};

} // namespace sc_dt

namespace sc_core::detail {

template <typename T>
struct value_traits {
    static constexpr bool supported = false;
};

template <>
struct value_traits<bool> {
    static constexpr bool supported = true;
    static constexpr auto encoding = FSIM_SC_BIT2;
    static constexpr std::uint32_t width = 1;

    [[nodiscard]] static char state(const bool value, std::size_t) noexcept {
        return value ? '1' : '0';
    }
    [[nodiscard]] static bool from_text(const std::string_view text) {
        return text == "1";
    }
};

template <>
struct value_traits<sc_dt::sc_logic> {
    static constexpr bool supported = true;
    static constexpr auto encoding = FSIM_SC_LOGIC4;
    static constexpr std::uint32_t width = 1;

    [[nodiscard]] static char state(
        const sc_dt::sc_logic value, std::size_t) noexcept {
        return value.to_char();
    }
    [[nodiscard]] static sc_dt::sc_logic from_text(
        const std::string_view text) {
        return sc_dt::sc_logic{text.front()};
    }
};

template <int Width>
struct value_traits<sc_dt::sc_bv<Width>> {
    static constexpr bool supported = true;
    static constexpr auto encoding = FSIM_SC_BIT2;
    static constexpr std::uint32_t width =
        static_cast<std::uint32_t>(Width);

    [[nodiscard]] static char state(
        const sc_dt::sc_bv<Width>& value,
        const std::size_t bit) {
        return value[bit] ? '1' : '0';
    }
    [[nodiscard]] static sc_dt::sc_bv<Width> from_text(
        const std::string_view text) {
        const std::string owned{text};
        return sc_dt::sc_bv<Width>{owned.c_str()};
    }
};

template <int Width>
struct value_traits<sc_dt::sc_lv<Width>> {
    static constexpr bool supported = true;
    static constexpr auto encoding = FSIM_SC_LOGIC4;
    static constexpr std::uint32_t width =
        static_cast<std::uint32_t>(Width);

    [[nodiscard]] static char state(
        const sc_dt::sc_lv<Width>& value,
        const std::size_t bit) {
        return value[bit].to_char();
    }
    [[nodiscard]] static sc_dt::sc_lv<Width> from_text(
        const std::string_view text) {
        const std::string owned{text};
        return sc_dt::sc_lv<Width>{owned.c_str()};
    }
};

template <int Width>
struct value_traits<sc_dt::sc_uint<Width>> {
    static constexpr bool supported = true;
    static constexpr auto encoding = FSIM_SC_UNSIGNED;
    static constexpr std::uint32_t width =
        static_cast<std::uint32_t>(Width);

    [[nodiscard]] static char state(
        const sc_dt::sc_uint<Width> value,
        const std::size_t bit) noexcept {
        return ((value.to_uint64() >> bit) & 1U) != 0 ? '1' : '0';
    }
    [[nodiscard]] static sc_dt::sc_uint<Width> from_text(
        const std::string_view text) {
        std::uint64_t value = 0;
        for (const char state : text) {
            if (state != '0' && state != '1') {
                throw std::logic_error{
                    "X or Z cannot be read into sc_uint"};
            }
            value = (value << 1U)
                | static_cast<std::uint64_t>(state == '1');
        }
        return sc_dt::sc_uint<Width>{value};
    }
};

template <int Width>
struct value_traits<sc_dt::sc_int<Width>> {
    static constexpr bool supported = true;
    static constexpr auto encoding = FSIM_SC_SIGNED;
    static constexpr std::uint32_t width =
        static_cast<std::uint32_t>(Width);

    [[nodiscard]] static char state(
        const sc_dt::sc_int<Width> value,
        const std::size_t bit) noexcept {
        const auto bits =
            std::bit_cast<std::uint64_t>(value.to_int64());
        return ((bits >> bit) & 1U) != 0 ? '1' : '0';
    }
    [[nodiscard]] static sc_dt::sc_int<Width> from_text(
        const std::string_view text) {
        std::uint64_t bits = 0;
        for (const char state : text) {
            if (state != '0' && state != '1') {
                throw std::logic_error{
                    "X or Z cannot be read into sc_int"};
            }
            bits = (bits << 1U)
                | static_cast<std::uint64_t>(state == '1');
        }
        if constexpr (Width < 64) {
            const auto sign = std::uint64_t{1} << (Width - 1);
            if ((bits & sign) != 0) {
                bits |= ~((std::uint64_t{1} << Width) - 1U);
            }
        }
        return sc_dt::sc_int<Width>{
            std::bit_cast<std::int64_t>(bits)};
    }
};

template <typename T>
[[nodiscard]] fsim_sc_handle_v1 register_port(
    const char* name,
    const fsim_sc_port_direction_v1 direction) {
    using traits = value_traits<std::remove_cv_t<T>>;
    static_assert(
        traits::supported,
        "this fsim SystemC port value type is not supported");
    if (current_host == nullptr || current_module == 0) {
        return 0;
    }
    if (current_host->register_port == nullptr
        || name == nullptr || *name == '\0') {
        throw std::logic_error{
            "named SystemC port requires an active elaboration host"};
    }
    fsim_sc_handle_v1 handle = 0;
    check_status(
        current_host->register_port(
            current_host->context,
            current_module,
            name,
            direction,
            traits::encoding,
            traits::width,
            &handle),
        "register port");
    return handle;
}

template <typename Interface>
[[nodiscard]] fsim_sc_handle_v1 register_export(
    const char* name) {
    using interface_traits =
        signal_interface_traits<std::remove_cv_t<Interface>>;
    if constexpr (!interface_traits::supported) {
        return 0;
    } else {
        using traits =
            value_traits<typename interface_traits::value_type>;
        static_assert(
            traits::supported,
            "this fsim SystemC export value type is not supported");
        if (current_host == nullptr || current_module == 0) {
            return 0;
        }
        if (current_host->register_export == nullptr
            || name == nullptr || *name == '\0') {
            throw std::logic_error{
                "named SystemC export requires an active "
                "elaboration host"};
        }
        fsim_sc_handle_v1 handle = 0;
        check_status(
            current_host->register_export(
                current_host->context,
                current_module,
                name,
                traits::encoding,
                traits::width,
                &handle),
            "register export");
        return handle;
    }
}

inline fsim_sc_handle_v1 register_event(const char* name) {
    if (current_host == nullptr || current_module == 0) {
        return 0;
    }
    if (current_host->register_event == nullptr) {
        throw std::logic_error{
            "SystemC event requires an event-capable elaboration host"};
    }
    fsim_sc_handle_v1 handle = 0;
    check_status(
        current_host->register_event(
            current_host->context,
            current_module,
            name,
            &handle),
        "register event");
    return handle;
}

inline fsim_sc_handle_v1 register_primitive_channel(
    const char* name,
    const fsim_sc_channel_update_v1 update,
    void* user) {
    if (current_host == nullptr || current_module == 0) {
        return 0;
    }
    if (current_host->register_primitive_channel == nullptr
        || update == nullptr || user == nullptr) {
        throw std::logic_error{
            "SystemC primitive channel requires a channel-capable "
            "elaboration host"};
    }
    fsim_sc_handle_v1 handle = 0;
    check_status(
        current_host->register_primitive_channel(
            current_host->context,
            current_module,
            name,
            update,
            user,
            &handle),
        "register primitive channel");
    return handle;
}

[[nodiscard]] inline std::size_t value_plane_size(
    const std::uint32_t width) noexcept {
    return (static_cast<std::size_t>(width) + 7U) / 8U;
}

template <typename T>
[[nodiscard]] std::vector<std::uint8_t> encode_value(
    const T& value) {
    using traits = value_traits<std::remove_cv_t<T>>;
    static_assert(
        traits::supported,
        "this fsim SystemC value type is not supported");
    const auto bytes = value_plane_size(traits::width);
    const bool four_state = traits::encoding != FSIM_SC_BIT2;
    std::vector<std::uint8_t> storage(
        bytes * (four_state ? 2U : 1U), 0);
    for (std::size_t bit = 0; bit < traits::width; ++bit) {
        const auto state = traits::state(value, bit);
        const auto byte = bit / 8U;
        const auto mask =
            static_cast<std::uint8_t>(1U << (bit % 8U));
        if (state == '1' || state == 'X') {
            storage[byte] |= mask;
        }
        if (state == 'X' || state == 'Z') {
            if (!four_state) {
                throw std::logic_error{
                    "two-state SystemC value contains X or Z"};
            }
            storage[bytes + byte] |= mask;
        }
    }
    return storage;
}

template <typename T>
void register_signal(
    const fsim_sc_handle_v1 channel,
    const char* name,
    const T& initial_value) {
    using traits = value_traits<std::remove_cv_t<T>>;
    static_assert(
        traits::supported,
        "this fsim SystemC signal value type is not supported");
    if (channel == 0) {
        return;
    }
    if (current_host == nullptr || current_module == 0
        || current_host->register_signal == nullptr) {
        throw std::logic_error{
            "SystemC signal requires a signal-capable elaboration host"};
    }
    const auto storage = encode_value(initial_value);
    const fsim_sc_value_view_v1 view{
        sizeof(fsim_sc_value_view_v1),
        traits::encoding,
        traits::width,
        storage.data(),
        storage.size()};
    check_status(
        current_host->register_signal(
            current_host->context,
            current_module,
            channel,
            name,
            traits::encoding,
            traits::width,
            &view),
        "register signal");
}

inline bool object_event(const fsim_sc_handle_v1 object) {
    if (current_host == nullptr || current_host->value_changed == nullptr
        || object == 0) {
        throw std::logic_error{
            "SystemC signal event query requires an active process"};
    }
    std::uint8_t result = 0;
    check_status(
        current_host->value_changed(
            current_host->context, object, &result),
        "query signal event");
    return result != 0;
}

inline void bind_port(
    const fsim_sc_handle_v1 port,
    const fsim_sc_handle_v1 channel) {
    if (port == 0 || channel == 0) {
        return;
    }
    if (current_host == nullptr || current_host->bind_port == nullptr) {
        throw std::logic_error{
            "SystemC port binding requires an active elaboration host"};
    }
    check_status(
        current_host->bind_port(
            current_host->context, port, channel),
        "bind port to channel");
}

inline void bind_export(
    const fsim_sc_handle_v1 export_handle,
    const fsim_sc_handle_v1 target) {
    if (export_handle == 0 || target == 0) {
        return;
    }
    if (current_host == nullptr
        || current_host->bind_export == nullptr) {
        throw std::logic_error{
            "SystemC export binding requires an active elaboration host"};
    }
    check_status(
        current_host->bind_export(
            current_host->context, export_handle, target),
        "bind export");
}

template <typename T>
[[nodiscard]] T read_object(const fsim_sc_handle_v1 object) {
    using traits = value_traits<std::remove_cv_t<T>>;
    static_assert(
        traits::supported,
        "this fsim SystemC value type is not supported");
    if (current_host == nullptr || current_host->read_value == nullptr
        || object == 0) {
        throw std::logic_error{
            "SystemC port read requires an active process"};
    }
    fsim_sc_value_view_v1 view{};
    view.struct_size = sizeof(view);
    check_status(
        current_host->read_value(
            current_host->context, object, &view),
        "read port");
    const auto bytes = value_plane_size(traits::width);
    const bool four_state = traits::encoding != FSIM_SC_BIT2;
    const auto expected = bytes * (four_state ? 2U : 1U);
    if (view.encoding != traits::encoding
        || view.width != traits::width
        || view.data == nullptr || view.data_size != expected) {
        throw std::runtime_error{
            "fsim SystemC host returned an invalid port value"};
    }
    std::string text(traits::width, '0');
    for (std::size_t bit = 0; bit < traits::width; ++bit) {
        const auto byte = bit / 8U;
        const auto mask =
            static_cast<std::uint8_t>(1U << (bit % 8U));
        const bool aval = (view.data[byte] & mask) != 0;
        const bool bval = four_state
            && (view.data[bytes + byte] & mask) != 0;
        text[traits::width - 1U - bit] =
            !bval ? (aval ? '1' : '0') : (aval ? 'X' : 'Z');
    }
    return traits::from_text(text);
}

template <typename T>
void write_object(
    const fsim_sc_handle_v1 object, const T& value) {
    using traits = value_traits<std::remove_cv_t<T>>;
    static_assert(
        traits::supported,
        "this fsim SystemC value type is not supported");
    if (current_host == nullptr || current_host->write_value == nullptr
        || object == 0) {
        throw std::logic_error{
            "SystemC port write requires an active process"};
    }
    const auto storage = encode_value(value);
    const fsim_sc_value_view_v1 view{
        sizeof(fsim_sc_value_view_v1),
        traits::encoding,
        traits::width,
        storage.data(),
        storage.size()};
    check_status(
        current_host->write_value(
            current_host->context, object, &view),
        "write port");
}

} // namespace sc_core::detail

namespace fsim::systemc {

/// An elaboration-time placeholder for a VHDL or Verilog/SystemVerilog child.
///
/// Construct this as a member of an sc_module, connect its typed ports to
/// objects registered on that module, and select the actual HDL design unit
/// with an explicit binding for the resulting hierarchy path in fsim.toml.
class hdl_instance final {
public:
    explicit hdl_instance(const char* name)
        : host_(sc_core::detail::current_host) {
        if (host_ == nullptr || name == nullptr || *name == '\0'
            || sc_core::detail::current_module == 0
            || host_->register_foreign_child == nullptr
            || host_->connect_foreign_port == nullptr) {
            throw std::logic_error{
                "hdl_instance construction requires an active fsim "
                "SystemC hierarchy host"};
        }
        sc_core::detail::check_status(
            host_->register_foreign_child(
                host_->context,
                sc_core::detail::current_module,
                name,
                &handle_),
            "register HDL child instance");
        if (handle_ == 0) {
            throw std::runtime_error{
                "fsim SystemC host returned an invalid HDL child handle"};
        }
    }

    hdl_instance(const hdl_instance&) = delete;
    hdl_instance& operator=(const hdl_instance&) = delete;

    /// Supply one named, immutable scalar construction actual to the
    /// manifest-selected VHDL or Verilog/SystemVerilog child.
    void set_actual(const char* name, const std::int64_t value) {
        if (name == nullptr || *name == '\0'
            || host_->set_foreign_child_actual == nullptr) {
            throw std::invalid_argument{
                "HDL child construction actual name must be valid"};
        }
        sc_core::detail::check_status(
            host_->set_foreign_child_actual(
                host_->context, handle_, name, value),
            "set HDL child construction actual");
    }

    template <typename T>
    void bind_input(
        const char* name, const sc_core::sc_in<T>& object) {
        connect<T>(name, FSIM_SC_INPUT, object.native_handle());
    }

    template <typename T>
    void bind_input(
        const char* name, const sc_core::sc_inout<T>& object) {
        connect<T>(name, FSIM_SC_INPUT, object.native_handle());
    }

    template <typename T>
    void bind_input(
        const char* name,
        const sc_core::sc_signal_in_if<T>& object) {
        connect<T>(name, FSIM_SC_INPUT, object.native_handle());
    }

    template <typename Interface>
        requires sc_core::detail::
            signal_interface_traits<Interface>::supported
    void bind_input(
        const char* name,
        const sc_core::sc_export<Interface>& object) {
        using value_type = typename sc_core::detail::
            signal_interface_traits<Interface>::value_type;
        connect<value_type>(
            name, FSIM_SC_INPUT, object.native_handle());
    }

    template <typename T>
    void bind_output(
        const char* name, const sc_core::sc_out<T>& object) {
        connect<T>(name, FSIM_SC_OUTPUT, object.native_handle());
    }

    template <typename T>
    void bind_output(
        const char* name,
        const sc_core::sc_signal_inout_if<T>& object) {
        connect<T>(name, FSIM_SC_OUTPUT, object.native_handle());
    }

    template <typename Interface>
        requires (
            sc_core::detail::
                signal_interface_traits<Interface>::supported
            && sc_core::detail::
                signal_interface_traits<Interface>::writable)
    void bind_output(
        const char* name,
        const sc_core::sc_export<Interface>& object) {
        using value_type = typename sc_core::detail::
            signal_interface_traits<Interface>::value_type;
        connect<value_type>(
            name, FSIM_SC_OUTPUT, object.native_handle());
    }

    template <typename T>
    void bind_inout(
        const char* name, const sc_core::sc_inout<T>& object) {
        connect<T>(name, FSIM_SC_INOUT, object.native_handle());
    }

    template <typename T>
    void bind_inout(
        const char* name,
        const sc_core::sc_signal_inout_if<T>& object) {
        connect<T>(name, FSIM_SC_INOUT, object.native_handle());
    }

    template <typename Interface>
        requires (
            sc_core::detail::
                signal_interface_traits<Interface>::supported
            && sc_core::detail::
                signal_interface_traits<Interface>::writable)
    void bind_inout(
        const char* name,
        const sc_core::sc_export<Interface>& object) {
        using value_type = typename sc_core::detail::
            signal_interface_traits<Interface>::value_type;
        connect<value_type>(
            name, FSIM_SC_INOUT, object.native_handle());
    }

    [[nodiscard]] fsim_sc_handle_v1
    native_handle() const noexcept {
        return handle_;
    }

private:
    template <typename T>
    void connect(
        const char* name,
        const fsim_sc_port_direction_v1 direction,
        const fsim_sc_handle_v1 object) {
        using traits =
            sc_core::detail::value_traits<std::remove_cv_t<T>>;
        static_assert(
            traits::supported,
            "this fsim SystemC value type is not supported");
        if (name == nullptr || *name == '\0' || object == 0) {
            throw std::invalid_argument{
                "HDL child port name and bound object must be valid"};
        }
        sc_core::detail::check_status(
            host_->connect_foreign_port(
                host_->context,
                handle_,
                name,
                direction,
                traits::encoding,
                traits::width,
                object),
            "connect HDL child port");
    }

    const fsim_sc_host_v1* host_{};
    fsim_sc_handle_v1 handle_{};
};

template <typename Module>
struct module_factory_state {
    static fsim_sc_status_v1 elaborate(
        void* user,
        const char* instance_name,
        const fsim_sc_handle_v1 module,
        fsim_sc_handle_v1,
        void** result) noexcept {
        static_assert(
            std::is_base_of_v<sc_core::sc_module, Module>,
            "registered SystemC module must derive from sc_module");
        const auto* host =
            static_cast<const fsim_sc_host_v1*>(user);
        if (host == nullptr || instance_name == nullptr
            || *instance_name == '\0' || result == nullptr) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        try {
            sc_core::detail::host_scope scope{host, module};
            sc_core::detail::module_construction_scope
                construction{module};
            auto object = std::make_unique<Module>(
                sc_core::sc_module_name{instance_name});
            const auto status = object->fsim_elaborate(host, module);
            if (status != FSIM_SC_OK) {
                return status;
            }
            const auto lifecycle_status =
                object->fsim_register_lifecycle(host, module);
            if (lifecycle_status != FSIM_SC_OK) {
                return lifecycle_status;
            }
            *result = object.release();
            return FSIM_SC_OK;
        } catch (...) {
            return FSIM_SC_RUNTIME_ERROR;
        }
    }

    static void destroy(void*, void* module) noexcept {
        delete static_cast<Module*>(module);
    }
};

template <typename Module>
[[nodiscard]] fsim_sc_status_v1 register_module_factory(
    const fsim_sc_host_v1* host,
    fsim_sc_registrar_v1* registrar,
    const char* name) noexcept {
    if (host == nullptr || registrar == nullptr || name == nullptr
        || *name == '\0'
        || host->abi_version != FSIM_SYSTEMC_ABI_VERSION
        || registrar->abi_version != FSIM_SYSTEMC_ABI_VERSION
        || host->struct_size < sizeof(fsim_sc_host_v1)
        || registrar->struct_size < sizeof(fsim_sc_registrar_v1)
        || registrar->register_elaboration_factory == nullptr
        || host->register_port == nullptr
        || host->register_process == nullptr
        || host->add_sensitivity == nullptr
        || host->read_value == nullptr
        || host->write_value == nullptr
        || host->report == nullptr
        || host->set_process_initialize == nullptr
        || host->register_event == nullptr
        || host->notify_event_mode == nullptr
        || host->cancel_event == nullptr
        || host->wait_event_list == nullptr
        || host->notify_event_delayed == nullptr
        || host->register_primitive_channel == nullptr
        || host->request_update == nullptr
        || host->register_signal == nullptr
        || host->value_changed == nullptr
        || host->bind_port == nullptr
        || host->register_native_module == nullptr
        || host->register_lifecycle == nullptr
        || host->register_export == nullptr
        || host->bind_export == nullptr
        || host->register_foreign_child == nullptr
        || host->connect_foreign_port == nullptr
        || host->wait_static == nullptr
        || host->set_foreign_child_actual == nullptr) {
        return FSIM_SC_ABI_MISMATCH;
    }
    return registrar->register_elaboration_factory(
        registrar->context,
        name,
        module_factory_state<Module>::elaborate,
        module_factory_state<Module>::destroy,
        const_cast<fsim_sc_host_v1*>(host));
}

} // namespace fsim::systemc

#define SC_MODULE(name) struct name : public ::sc_core::sc_module
#define SC_CTOR(name) \
    explicit name( \
        [[maybe_unused]] ::sc_core::sc_module_name fsim_module_name)
#define SC_HAS_PROCESS(name)
#define SC_METHOD(function_name) \
    this->fsim_register_process( \
        #function_name, FSIM_SC_METHOD, [this]() { this->function_name(); })
#define SC_THREAD(function_name) \
    this->fsim_register_process( \
        #function_name, FSIM_SC_THREAD, [this]() { this->function_name(); })
#define SC_CTHREAD(function_name, edge_expression) \
    do { \
        this->fsim_register_process( \
            #function_name, FSIM_SC_CTHREAD, [this]() { this->function_name(); }); \
        this->sensitive << (edge_expression); \
    } while (false)
