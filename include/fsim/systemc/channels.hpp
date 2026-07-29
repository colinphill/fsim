// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/systemc/core.hpp"

namespace sc_core {

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

const char* sc_gen_unique_name(const char* base);

} // namespace sc_core

