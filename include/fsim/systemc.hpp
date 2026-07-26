// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/systemc_abi.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <compare>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
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

inline void bind_host(const fsim_sc_host_v1* host) noexcept {
    current_host = host;
}

inline void check_status(const fsim_sc_status_v1 status, const char* operation) {
    if (status != FSIM_SC_OK) {
        throw std::runtime_error{std::string{"fsim SystemC host failed to "} + operation};
    }
}

} // namespace detail

class sc_event {
public:
    sc_event() = default;
    explicit sc_event(const fsim_sc_handle_v1 handle) noexcept : handle_(handle) {}

    void notify(const sc_time delay = SC_ZERO_TIME) const {
        if (detail::current_host == nullptr || detail::current_host->notify_event == nullptr) {
            throw std::logic_error{
                "sc_event::notify requires an active fsim SystemC process"};
        }
        detail::check_status(
            detail::current_host->notify_event(
                detail::current_host->context, handle_, delay.value()),
            "notify event");
    }

    [[nodiscard]] fsim_sc_handle_v1 native_handle() const noexcept { return handle_; }

private:
    fsim_sc_handle_v1 handle_{};
};

inline void wait(const sc_time delay) {
    if (detail::current_host == nullptr || detail::current_host->wait_time == nullptr) {
        throw std::logic_error{"sc_core::wait requires an active fsim SystemC process"};
    }
    detail::check_status(
        detail::current_host->wait_time(detail::current_host->context, delay.value()),
        "wait for time");
}

inline void wait(const sc_event& event) {
    if (detail::current_host == nullptr || detail::current_host->wait_event == nullptr) {
        throw std::logic_error{"sc_core::wait requires an active fsim SystemC process"};
    }
    detail::check_status(
        detail::current_host->wait_event(
            detail::current_host->context, event.native_handle()),
        "wait for event");
}

inline void wait() {
    throw std::logic_error{"plain wait() requires a statically sensitive fsim SystemC thread"};
}

inline void next_trigger(const sc_time delay) {
    wait(delay);
}

inline void next_trigger(const sc_event& event) {
    wait(event);
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
            true});
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
    };

    [[nodiscard]] const std::vector<Process>& fsim_processes() const noexcept {
        return processes_;
    }

private:
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
    explicit sc_in(const char*) {}

    void bind(const sc_signal<T>& signal) noexcept { signal_ = &signal; }
    void operator()(const sc_signal<T>& signal) noexcept { bind(signal); }
    [[nodiscard]] const T& read() const {
        if (signal_ == nullptr) {
            throw std::logic_error{"read from unbound sc_in"};
        }
        return signal_->read();
    }
    [[nodiscard]] fsim_sc_handle_v1 native_handle() const noexcept {
        return signal_ == nullptr ? 0 : signal_->native_handle();
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
};

template <typename T>
class sc_out {
public:
    sc_out() = default;
    explicit sc_out(const char*) {}

    void bind(sc_signal<T>& signal) noexcept { signal_ = &signal; }
    void operator()(sc_signal<T>& signal) noexcept { bind(signal); }
    void write(const T& value) {
        if (signal_ == nullptr) {
            throw std::logic_error{"write to unbound sc_out"};
        }
        signal_->write(value);
    }
    [[nodiscard]] fsim_sc_handle_v1 native_handle() const noexcept {
        return signal_ == nullptr ? 0 : signal_->native_handle();
    }
    sc_out& operator=(const T& value) {
        write(value);
        return *this;
    }

private:
    sc_signal<T>* signal_{};
};

template <typename T>
class sc_inout : public sc_out<T> {
public:
    using sc_out<T>::sc_out;

    void bind(sc_signal<T>& signal) noexcept {
        sc_out<T>::bind(signal);
        input_.bind(signal);
    }
    void operator()(sc_signal<T>& signal) noexcept { bind(signal); }
    [[nodiscard]] const T& read() const { return input_.read(); }
    [[nodiscard]] sc_event_finder pos() const noexcept {
        return {this->native_handle(), FSIM_SC_POSEDGE};
    }
    [[nodiscard]] sc_event_finder neg() const noexcept {
        return {this->native_handle(), FSIM_SC_NEGEDGE};
    }
    operator const T&() const { return read(); }

private:
    sc_in<T> input_;
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
