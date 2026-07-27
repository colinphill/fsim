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

template <typename T>
[[nodiscard]] T read_object(fsim_sc_handle_v1 object);

template <typename T>
void write_object(fsim_sc_handle_v1 object, const T& value);

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
    throw std::logic_error{"plain wait() requires a statically sensitive fsim SystemC thread"};
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
    constexpr sc_module_name(const char* name) noexcept : name_(name) {}
    [[nodiscard]] constexpr const char* c_str() const noexcept { return name_; }
    [[nodiscard]] constexpr operator const char*() const noexcept { return name_; }

private:
    const char* name_;
};

class sc_module;

class sc_interface {
public:
    virtual ~sc_interface() = default;
};

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
    explicit sc_export(const char*) noexcept {}

    void bind(Interface& interface) noexcept { interface_ = &interface; }
    void operator()(Interface& interface) noexcept { bind(interface); }

    [[nodiscard]] Interface& get_interface() const {
        if (interface_ == nullptr) {
            throw std::logic_error{"access through unbound sc_export"};
        }
        return *interface_;
    }

    [[nodiscard]] Interface* operator->() const {
        return &get_interface();
    }
    operator Interface&() const { return get_interface(); }

private:
    Interface* interface_{};
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
    explicit sc_module(const sc_module_name name)
        : sensitive(this),
          name_(
              name.c_str() == nullptr
                  ? throw std::invalid_argument{
                        "sc_module_name must not be null"}
                  : name.c_str()) {}
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

    sc_sensitive sensitive;

protected:
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
    std::vector<Process> processes_;
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
class sc_signal {
public:
    using value_type = T;

    sc_signal() = default;
    explicit sc_signal(const char*) {}

    [[nodiscard]] const T& read() const noexcept { return value_; }
    void write(const T& value) {
        event_ = value_ != value;
        value_ = value;
    }
    [[nodiscard]] bool event() const noexcept { return event_; }
    [[nodiscard]] fsim_sc_handle_v1 native_handle() const noexcept { return handle_; }

    operator const T&() const noexcept { return read(); }
    sc_signal& operator=(const T& value) {
        write(value);
        return *this;
    }

private:
    T value_{};
    fsim_sc_handle_v1 handle_{};
    bool event_{};
};

template <typename T>
class sc_in {
public:
    sc_in() = default;
    explicit sc_in(const char* name)
        : handle_(
              detail::register_port<T>(name, FSIM_SC_INPUT)) {}

    void bind(const sc_signal<T>& signal) noexcept { signal_ = &signal; }
    void operator()(const sc_signal<T>& signal) noexcept { bind(signal); }
    [[nodiscard]] const T& read() const {
        if (signal_ != nullptr) {
            return signal_->read();
        }
        if (handle_ == 0) {
            throw std::logic_error{"read from unbound sc_in"};
        }
        value_ = detail::read_object<T>(handle_);
        return value_;
    }
    [[nodiscard]] fsim_sc_handle_v1 native_handle() const noexcept {
        return signal_ == nullptr ? handle_ : signal_->native_handle();
    }
    [[nodiscard]] sc_event_finder pos() const noexcept {
        return {native_handle(), FSIM_SC_POSEDGE};
    }
    [[nodiscard]] sc_event_finder neg() const noexcept {
        return {native_handle(), FSIM_SC_NEGEDGE};
    }
    operator const T&() const { return read(); }

private:
    const sc_signal<T>* signal_{};
    fsim_sc_handle_v1 handle_{};
    mutable T value_{};
};

template <typename T>
class sc_out {
public:
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

    void bind(sc_signal<T>& signal) noexcept { signal_ = &signal; }
    void operator()(sc_signal<T>& signal) noexcept { bind(signal); }
    void write(const T& value) {
        if (signal_ != nullptr) {
            signal_->write(value);
            return;
        }
        if (handle_ == 0) {
            throw std::logic_error{"write to unbound sc_out"};
        }
        detail::write_object(handle_, value);
    }
    [[nodiscard]] fsim_sc_handle_v1 native_handle() const noexcept {
        return signal_ == nullptr ? handle_ : signal_->native_handle();
    }
    sc_out& operator=(const T& value) {
        write(value);
        return *this;
    }

private:
    sc_signal<T>* signal_{};
    fsim_sc_handle_v1 handle_{};
};

template <typename T>
class sc_inout : public sc_out<T> {
public:
    sc_inout() = default;
    explicit sc_inout(const char* name)
        : sc_out<T>(name, FSIM_SC_INOUT) {}

    void bind(sc_signal<T>& signal) noexcept {
        sc_out<T>::bind(signal);
        input_.bind(signal);
    }
    void operator()(sc_signal<T>& signal) noexcept { bind(signal); }
    [[nodiscard]] const T& read() const {
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
            auto object = std::make_unique<Module>(
                sc_core::sc_module_name{instance_name});
            const auto status = object->fsim_elaborate(host, module);
            if (status != FSIM_SC_OK) {
                return status;
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
        || host->request_update == nullptr) {
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
    explicit name(::sc_core::sc_module_name fsim_module_name) \
        : ::sc_core::sc_module(fsim_module_name)
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
