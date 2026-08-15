// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/systemc_abi.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wold-style-cast"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif
#include <systemc.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <concepts>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#define FSIM_SYSTEMC_ACCELERA_VERSION "3.0.2"
#define FSIM_SYSTEMC_ACCELERA_SOURCE_SHA256 \
    "9b3693ed286aab958b9e5d79bb0ad3bc523bbc46931100553275352038f4a0c4"
#define FSIM_SYSTEMC_BRIDGE_REVISION 2u

extern "C" const char* fsim_systemc_accellera_version() noexcept;
extern "C" const char* fsim_systemc_accellera_runtime_identity() noexcept;
extern "C" const void* fsim_systemc_accellera_context() noexcept;
extern "C" bool fsim_systemc_accellera_accepts_identity(
    const char* candidate) noexcept;
extern "C" const char*
fsim_systemc_accellera_compatibility_identity() noexcept;
extern "C" bool fsim_systemc_accellera_accepts_compatibility_identity(
    const char* candidate) noexcept;

namespace fsim::systemc {

inline constexpr std::string_view accellera_version = FSIM_SYSTEMC_ACCELERA_VERSION;
inline constexpr std::string_view accellera_source_sha256 = FSIM_SYSTEMC_ACCELERA_SOURCE_SHA256;
inline constexpr std::uint32_t accellera_bridge_revision = FSIM_SYSTEMC_BRIDGE_REVISION;

struct factory_parameter {
    const char* name { };
    fsim_sc_construction_type_v1 type {
        FSIM_SC_CONSTRUCTION_INTEGER
    };
    bool has_default { };
    std::int64_t default_value { };
};

class backend_bindable_endpoint {
public:
    virtual ~backend_bindable_endpoint() = default;

    [[nodiscard]] virtual bool bind_backend_interface(
        sc_core::sc_interface& target) = 0;
};

enum class backend_endpoint_direction : std::uint8_t {
    input = 1,
    output = 2,
};

class backend_scalar_endpoint {
public:
    virtual ~backend_scalar_endpoint() = default;

    [[nodiscard]] virtual backend_endpoint_direction backend_direction()
        const noexcept = 0;
    [[nodiscard]] virtual std::uint16_t backend_width() const noexcept = 0;
    [[nodiscard]] virtual bool backend_signed() const noexcept = 0;
    [[nodiscard]] virtual bool apply_backend_scalar(
        std::uint64_t bits, std::uint16_t width, bool is_signed) = 0;
    [[nodiscard]] virtual bool sample_backend_scalar(
        std::uint64_t& bits) const = 0;
};

template <typename Interface, int MaximumBindings = 1,
    sc_core::sc_port_policy Policy = sc_core::SC_ONE_OR_MORE_BOUND>
class backend_port
    : public sc_core::sc_port<Interface, MaximumBindings, Policy>
    , public backend_bindable_endpoint {
public:
    using base_type =
        sc_core::sc_port<Interface, MaximumBindings, Policy>;

    backend_port() = default;

    explicit backend_port(const char* name)
        : base_type { name }
    {
    }

    [[nodiscard]] bool bind_backend_interface(
        sc_core::sc_interface& target) override
    {
        auto* typed = dynamic_cast<Interface*>(&target);
        if (typed == nullptr) {
            return false;
        }
        this->bind(*typed);
        return true;
    }
};

template <std::integral T>
class backend_input final
    : public backend_port<sc_core::sc_signal_in_if<T>>
    , public backend_scalar_endpoint {
public:
    using base_type = backend_port<sc_core::sc_signal_in_if<T>>;
    using base_type::base_type;

    [[nodiscard]] bool bind_backend_interface(
        sc_core::sc_interface& target) override
    {
        target_ = dynamic_cast<sc_core::sc_signal_inout_if<T>*>(&target);
        return target_ != nullptr && base_type::bind_backend_interface(target);
    }

    [[nodiscard]] backend_endpoint_direction backend_direction()
        const noexcept override
    {
        return backend_endpoint_direction::input;
    }

    [[nodiscard]] std::uint16_t backend_width() const noexcept override
    {
        if constexpr (std::same_as<T, bool>) {
            return 1U;
        }
        return static_cast<std::uint16_t>(sizeof(T) * 8U);
    }

    [[nodiscard]] bool backend_signed() const noexcept override
    {
        return std::is_signed_v<T>;
    }

    [[nodiscard]] bool apply_backend_scalar(const std::uint64_t bits,
        const std::uint16_t width, const bool is_signed) override
    {
        if (target_ == nullptr || width != backend_width()
            || is_signed != backend_signed()
            || (width < 64U && (bits >> width) != 0U)) {
            return false;
        }
        if constexpr (std::same_as<T, bool>) {
            target_->write(bits != 0U);
        } else {
            using unsigned_type = std::make_unsigned_t<T>;
            const auto raw = static_cast<unsigned_type>(bits);
            if constexpr (std::is_signed_v<T>) {
                target_->write(std::bit_cast<T>(raw));
            } else {
                target_->write(raw);
            }
        }
        return true;
    }

    [[nodiscard]] bool sample_backend_scalar(
        std::uint64_t& bits) const override
    {
        if (target_ == nullptr) {
            return false;
        }
        if constexpr (std::same_as<T, bool>) {
            bits = target_->read() ? 1U : 0U;
        } else if constexpr (std::is_signed_v<T>) {
            using unsigned_type = std::make_unsigned_t<T>;
            bits = static_cast<std::uint64_t>(
                std::bit_cast<unsigned_type>(target_->read()));
        } else {
            bits = static_cast<std::uint64_t>(target_->read());
        }
        return true;
    }

private:
    sc_core::sc_signal_inout_if<T>* target_ { };
};

template <std::integral T>
class backend_output final
    : public backend_port<sc_core::sc_signal_inout_if<T>>
    , public backend_scalar_endpoint {
public:
    using base_type = backend_port<sc_core::sc_signal_inout_if<T>>;
    using base_type::base_type;

    [[nodiscard]] bool bind_backend_interface(
        sc_core::sc_interface& target) override
    {
        target_ = dynamic_cast<sc_core::sc_signal_inout_if<T>*>(&target);
        return target_ != nullptr && base_type::bind_backend_interface(target);
    }

    [[nodiscard]] backend_endpoint_direction backend_direction()
        const noexcept override
    {
        return backend_endpoint_direction::output;
    }

    [[nodiscard]] std::uint16_t backend_width() const noexcept override
    {
        if constexpr (std::same_as<T, bool>) {
            return 1U;
        }
        return static_cast<std::uint16_t>(sizeof(T) * 8U);
    }

    [[nodiscard]] bool backend_signed() const noexcept override
    {
        return std::is_signed_v<T>;
    }

    [[nodiscard]] bool apply_backend_scalar(
        std::uint64_t, std::uint16_t, bool) override
    {
        return false;
    }

    [[nodiscard]] bool sample_backend_scalar(
        std::uint64_t& bits) const override
    {
        if (target_ == nullptr) {
            return false;
        }
        if constexpr (std::same_as<T, bool>) {
            bits = target_->read() ? 1U : 0U;
        } else if constexpr (std::is_signed_v<T>) {
            using unsigned_type = std::make_unsigned_t<T>;
            bits = static_cast<std::uint64_t>(
                std::bit_cast<unsigned_type>(target_->read()));
        } else {
            bits = static_cast<std::uint64_t>(target_->read());
        }
        return true;
    }

private:
    sc_core::sc_signal_inout_if<T>* target_ { };
};

template <typename... Parameters>
    requires(
        std::same_as<std::remove_cvref_t<Parameters>, factory_parameter>
        && ...)
[[nodiscard]] constexpr auto make_factory_parameters(
    Parameters&&... parameters)
{
    return std::array<factory_parameter, sizeof...(Parameters)> {
        std::forward<Parameters>(parameters)...
    };
}

namespace detail {

    struct factory_context {
        const fsim_sc_host_v1* host { };
        fsim_sc_handle_v1 module { };
        std::unordered_map<const sc_core::sc_module*, fsim_sc_handle_v1>*
            modules { };
    };

    inline thread_local factory_context active_factory_context { };

    class factory_scope final {
    public:
        factory_scope(
            const fsim_sc_host_v1* host,
            const fsim_sc_handle_v1 module,
            std::unordered_map<const sc_core::sc_module*, fsim_sc_handle_v1>*
                modules) noexcept
            : previous_ { active_factory_context }
        {
            active_factory_context = { host, module, modules };
        }

        ~factory_scope() { active_factory_context = previous_; }

        factory_scope(const factory_scope&) = delete;
        factory_scope& operator=(const factory_scope&) = delete;

    private:
        factory_context previous_;
    };

    inline void report_factory_failure(
        const fsim_sc_host_v1* host,
        const char* message) noexcept
    {
        if (host != nullptr && host->report != nullptr) {
            host->report(host->context, 3, message);
        }
    }

    template <typename T>
    struct application_bridge_codec;

    template <>
    struct application_bridge_codec<bool> {
        static constexpr fsim_sc_value_encoding_v1 encoding = FSIM_SC_BIT2;
        static constexpr std::uint32_t width = 1U;

        static bool decode(const fsim_sc_value_view_v1& view)
        {
            return (view.data[0] & 1U) != 0U;
        }

        static std::vector<std::uint8_t> encode(const bool value)
        {
            return { static_cast<std::uint8_t>(value ? 1U : 0U) };
        }
    };

    template <>
    struct application_bridge_codec<sc_dt::sc_logic> {
        static constexpr fsim_sc_value_encoding_v1 encoding = FSIM_SC_LOGIC4;
        static constexpr std::uint32_t width = 1U;

        static sc_dt::sc_logic decode(const fsim_sc_value_view_v1& view)
        {
            const auto code = static_cast<unsigned>(view.data[0] & 1U)
                | (static_cast<unsigned>(view.data[1] & 1U) << 1U);
            return sc_dt::sc_logic { static_cast<sc_dt::sc_logic_value_t>(code) };
        }

        static std::vector<std::uint8_t> encode(const sc_dt::sc_logic value)
        {
            const auto code = static_cast<unsigned>(value.value());
            return { static_cast<std::uint8_t>(code & 1U),
                static_cast<std::uint8_t>((code >> 1U) & 1U) };
        }
    };

    template <int Width>
    struct application_bridge_codec<sc_dt::sc_bv<Width>> {
        static_assert(Width > 0);
        static constexpr fsim_sc_value_encoding_v1 encoding = FSIM_SC_BIT2;
        static constexpr std::uint32_t width = Width;

        static sc_dt::sc_bv<Width> decode(const fsim_sc_value_view_v1& view)
        {
            sc_dt::sc_bv<Width> result;
            for (int bit = 0; bit < Width; ++bit) {
                result.set_bit(bit, static_cast<typename sc_dt::sc_bv<Width>::value_type>(
                    (view.data[static_cast<std::size_t>(bit) / 8U]
                        >> (static_cast<unsigned>(bit) % 8U))
                    & 1U));
            }
            return result;
        }

        static std::vector<std::uint8_t> encode(
            const sc_dt::sc_bv<Width>& value)
        {
            std::vector<std::uint8_t> result(
                (static_cast<std::size_t>(Width) + 7U) / 8U);
            for (int bit = 0; bit < Width; ++bit) {
                if (value.get_bit(bit) != 0) {
                    result[static_cast<std::size_t>(bit) / 8U]
                        |= static_cast<std::uint8_t>(
                            1U << (static_cast<unsigned>(bit) % 8U));
                }
            }
            return result;
        }
    };

    template <int Width>
    struct application_bridge_codec<sc_dt::sc_lv<Width>> {
        static_assert(Width > 0);
        static constexpr fsim_sc_value_encoding_v1 encoding = FSIM_SC_LOGIC4;
        static constexpr std::uint32_t width = Width;

        static sc_dt::sc_lv<Width> decode(const fsim_sc_value_view_v1& view)
        {
            sc_dt::sc_lv<Width> result;
            const auto bytes = (static_cast<std::size_t>(Width) + 7U) / 8U;
            for (int bit = 0; bit < Width; ++bit) {
                const auto byte = static_cast<std::size_t>(bit) / 8U;
                const auto shift = static_cast<unsigned>(bit) % 8U;
                const auto code = static_cast<unsigned>(
                    (view.data[byte] >> shift) & 1U)
                    | (static_cast<unsigned>(
                           (view.data[bytes + byte] >> shift) & 1U)
                        << 1U);
                result.set_bit(bit,
                    static_cast<typename sc_dt::sc_lv<Width>::value_type>(code));
            }
            return result;
        }

        static std::vector<std::uint8_t> encode(
            const sc_dt::sc_lv<Width>& value)
        {
            const auto bytes = (static_cast<std::size_t>(Width) + 7U) / 8U;
            std::vector<std::uint8_t> result(bytes * 2U);
            for (int bit = 0; bit < Width; ++bit) {
                const auto code = static_cast<unsigned>(value.get_bit(bit));
                const auto byte = static_cast<std::size_t>(bit) / 8U;
                const auto mask = static_cast<std::uint8_t>(
                    1U << (static_cast<unsigned>(bit) % 8U));
                if ((code & 1U) != 0U) {
                    result[byte] |= mask;
                }
                if ((code & 2U) != 0U) {
                    result[bytes + byte] |= mask;
                }
            }
            return result;
        }
    };

    template <int Width>
    struct application_bridge_codec<sc_dt::sc_uint<Width>> {
        static_assert(Width > 0);
        static constexpr fsim_sc_value_encoding_v1 encoding = FSIM_SC_UNSIGNED;
        static constexpr std::uint32_t width = Width;

        static sc_dt::sc_uint<Width> decode(const fsim_sc_value_view_v1& view)
        {
            sc_dt::sc_uint<Width> result;
            for (int bit = 0; bit < Width; ++bit) {
                result[bit] = ((view.data[static_cast<std::size_t>(bit) / 8U]
                                   >> (static_cast<unsigned>(bit) % 8U))
                    & 1U)
                    != 0U;
            }
            return result;
        }

        static std::vector<std::uint8_t> encode(
            const sc_dt::sc_uint<Width>& value)
        {
            std::vector<std::uint8_t> result(
                ((static_cast<std::size_t>(Width) + 7U) / 8U) * 2U);
            for (int bit = 0; bit < Width; ++bit) {
                if (value[bit]) {
                    result[static_cast<std::size_t>(bit) / 8U]
                        |= static_cast<std::uint8_t>(
                            1U << (static_cast<unsigned>(bit) % 8U));
                }
            }
            return result;
        }
    };

    template <int Width>
    struct application_bridge_codec<sc_dt::sc_int<Width>>
        : application_bridge_codec<sc_dt::sc_uint<Width>> {
        static constexpr fsim_sc_value_encoding_v1 encoding = FSIM_SC_SIGNED;

        static sc_dt::sc_int<Width> decode(const fsim_sc_value_view_v1& view)
        {
            sc_dt::sc_int<Width> result;
            for (int bit = 0; bit < Width; ++bit) {
                result[bit] = ((view.data[static_cast<std::size_t>(bit) / 8U]
                                   >> (static_cast<unsigned>(bit) % 8U))
                    & 1U)
                    != 0U;
            }
            return result;
        }

        static std::vector<std::uint8_t> encode(
            const sc_dt::sc_int<Width>& value)
        {
            std::vector<std::uint8_t> result(
                ((static_cast<std::size_t>(Width) + 7U) / 8U) * 2U);
            for (int bit = 0; bit < Width; ++bit) {
                if (value[bit]) {
                    result[static_cast<std::size_t>(bit) / 8U]
                        |= static_cast<std::uint8_t>(
                            1U << (static_cast<unsigned>(bit) % 8U));
                }
            }
            return result;
        }
    };

    class application_port_bridge {
    public:
        virtual ~application_port_bridge() = default;
        [[nodiscard]] virtual bool input() const noexcept = 0;
        virtual void read_host() = 0;
        virtual void write_host() = 0;
        fsim_sc_handle_v1 handle { };
        fsim_sc_handle_v1 module { };
    };

    class application_signal_bridge {
    public:
        virtual ~application_signal_bridge() = default;
        virtual void add_port_direction(fsim_sc_port_direction_v1 direction) = 0;
        virtual void read_host() = 0;
        virtual void write_host() = 0;
    };

    template <typename T,
        sc_core::sc_writer_policy WriterPolicy = sc_core::SC_ONE_WRITER>
    class application_typed_signal_bridge final
        : public application_signal_bridge {
    public:
        application_typed_signal_bridge(const fsim_sc_host_v1* host,
            sc_core::sc_signal<T, WriterPolicy>& signal,
            const fsim_sc_handle_v1 handle)
            : host_ { host }
            , signal_ { signal }
            , handle_ { handle }
        {
        }

        void add_port_direction(
            const fsim_sc_port_direction_v1 direction) override
        {
            if (!has_port_direction_) {
                read_enabled_ = false;
                write_enabled_ = false;
                has_port_direction_ = true;
            }
            read_enabled_ = read_enabled_ || direction != FSIM_SC_OUTPUT;
            write_enabled_ = write_enabled_ || direction != FSIM_SC_INPUT;
        }

        void read_host() override
        {
            if (!read_enabled_) {
                return;
            }
            fsim_sc_value_view_v1 view { };
            view.struct_size = sizeof(view);
            if (host_->read_value(host_->context, handle_, &view) != FSIM_SC_OK
                || view.data == nullptr
                || view.encoding != application_bridge_codec<T>::encoding
                || view.width != application_bridge_codec<T>::width) {
                throw std::runtime_error {
                    "cannot read an Accellera internal signal"
                };
            }
            signal_.write(application_bridge_codec<T>::decode(view));
        }

        void write_host() override
        {
            if (!write_enabled_) {
                return;
            }
            const auto bytes = application_bridge_codec<T>::encode(signal_.read());
            const fsim_sc_value_view_v1 view { sizeof(fsim_sc_value_view_v1),
                application_bridge_codec<T>::encoding,
                application_bridge_codec<T>::width, bytes.data(), bytes.size() };
            if (host_->write_value(host_->context, handle_, &view) != FSIM_SC_OK) {
                throw std::runtime_error {
                    "cannot write an Accellera internal signal"
                };
            }
        }

    private:
        const fsim_sc_host_v1* host_;
        sc_core::sc_signal<T, WriterPolicy>& signal_;
        fsim_sc_handle_v1 handle_;
        bool has_port_direction_ { };
        bool read_enabled_ { };
        bool write_enabled_ { true };
    };

    template <typename T>
    class application_typed_port_bridge final
        : public application_port_bridge {
    public:
        application_typed_port_bridge(const fsim_sc_host_v1* host,
            const fsim_sc_port_direction_v1 direction)
            : host_ { host }
            , direction_ { direction }
            , signal_ { sc_core::sc_gen_unique_name("fsim_bridge") }
        {
        }

        sc_core::sc_signal<T>& signal() noexcept { return signal_; }
        [[nodiscard]] bool input() const noexcept override
        {
            return direction_ != FSIM_SC_OUTPUT;
        }

        void read_host() override
        {
            if (!input()) {
                return;
            }
            fsim_sc_value_view_v1 view { };
            view.struct_size = sizeof(view);
            if (host_->read_value(host_->context, handle, &view) != FSIM_SC_OK
                || view.data == nullptr
                || view.encoding != application_bridge_codec<T>::encoding
                || view.width != application_bridge_codec<T>::width) {
                throw std::runtime_error { "cannot read an Accellera bridge input" };
            }
            signal_.write(application_bridge_codec<T>::decode(view));
        }

        void write_host() override
        {
            if (direction_ == FSIM_SC_INPUT) {
                return;
            }
            const auto bytes = application_bridge_codec<T>::encode(signal_.read());
            const fsim_sc_value_view_v1 view { sizeof(fsim_sc_value_view_v1),
                application_bridge_codec<T>::encoding,
                application_bridge_codec<T>::width, bytes.data(), bytes.size() };
            if (host_->write_value(host_->context, handle, &view) != FSIM_SC_OK) {
                throw std::runtime_error { "cannot write an Accellera bridge output" };
            }
        }

    private:
        const fsim_sc_host_v1* host_;
        fsim_sc_port_direction_v1 direction_;
        sc_core::sc_signal<T> signal_;
    };

    template <typename Module>
    struct application_factory_instance {
        struct registered_port {
            fsim_sc_handle_v1 handle { };
            fsim_sc_handle_v1 module { };
            fsim_sc_port_direction_v1 direction { FSIM_SC_INPUT };
        };

        const fsim_sc_host_v1* host { };
        std::unique_ptr<Module> module;
        std::vector<std::unique_ptr<application_port_bridge>> ports;
        std::vector<std::unique_ptr<application_signal_bridge>> signals;
        std::vector<registered_port> registered_ports;
        std::unordered_map<const sc_core::sc_module*, fsim_sc_handle_v1>
            module_handles;
        std::unordered_map<const sc_core::sc_interface*, fsim_sc_handle_v1>
            interface_handles;
        std::unordered_map<const sc_core::sc_interface*, application_signal_bridge*>
            interface_bridges;
        std::unordered_map<const sc_core::sc_module*,
            std::unordered_map<std::string, fsim_sc_handle_v1>>
            port_handles;
        fsim_sc_handle_v1 root_module { };
        std::uint64_t last_host_time_femtoseconds { };

        void advance_to_host_time()
        {
            std::uint64_t host_time { };
            const auto status = host->current_time_femtoseconds(
                host->context, &host_time);
            if (status != FSIM_SC_OK) {
                throw std::runtime_error {
                    "cannot read the fsim application time (status "
                    + std::to_string(static_cast<int>(status)) + ")"
                };
            }
            if (host_time < last_host_time_femtoseconds) {
                throw std::runtime_error {
                    "fsim application time moved backward from "
                    + std::to_string(last_host_time_femtoseconds)
                    + " fs to " + std::to_string(host_time) + " fs"
                };
            }
            const auto delay = host_time - last_host_time_femtoseconds;
            sc_core::sc_start(
                sc_core::sc_time {
                    static_cast<double>(delay), sc_core::SC_FS},
                sc_core::SC_RUN_TO_TIME);
            last_host_time_femtoseconds = host_time;
        }

        void wait_for_input_or_native_activity()
        {
            if (!sc_core::sc_pending_activity()) {
                return;
            }
            const auto native_delay =
                sc_core::sc_time_to_pending_activity();
            const auto femtoseconds = std::round(
                static_cast<long double>(native_delay.to_seconds())
                * 1'000'000'000'000'000.0L);
            if (femtoseconds < 0.0L
                || femtoseconds
                    > static_cast<long double>(
                        std::numeric_limits<std::uint64_t>::max())) {
                throw std::runtime_error {
                    "next Accellera activity exceeds the application time range"
                };
            }
            const auto delay = static_cast<std::uint64_t>(femtoseconds);
            const auto status = host->wait_for_input_or_native_activity(
                host->context, delay);
            if (status != FSIM_SC_OK) {
                throw std::runtime_error {
                    "cannot schedule the next Accellera application activity "
                    "after " + std::to_string(delay) + " fs (status "
                    + std::to_string(static_cast<int>(status)) + ")"
                };
            }
        }

        static void execute(void* user)
        {
            auto& self = *static_cast<application_factory_instance*>(user);
            if (sc_core::sc_get_status() == sc_core::SC_STOPPED) {
                return;
            }
            for (const auto& port : self.ports) {
                port->read_host();
            }
            for (const auto& signal : self.signals) {
                signal->read_host();
            }
            self.advance_to_host_time();
            for (const auto& port : self.ports) {
                port->write_host();
            }
            for (const auto& signal : self.signals) {
                signal->write_host();
            }
            self.wait_for_input_or_native_activity();
        }

        static void lifecycle_noop(void*) { }

        static void collect_native_processes(sc_core::sc_object& object,
            std::vector<sc_core::sc_process_handle>& processes)
        {
            for (auto* child : object.get_child_objects()) {
                collect_native_processes(*child, processes);
            }
            sc_core::sc_process_handle process { &object };
            if (process.valid()) {
                processes.push_back(std::move(process));
            }
        }

        static void terminate_native_processes()
        {
            std::vector<sc_core::sc_process_handle> processes;
            for (auto* object : sc_core::sc_get_top_level_objects()) {
                collect_native_processes(*object, processes);
            }
            const auto current = sc_core::sc_get_current_process_handle();
            for (auto& process : processes) {
                if (process != current && !process.terminated()) {
                    process.kill();
                }
            }
        }

        static void end_simulation(void* user)
        {
            auto& self = *static_cast<application_factory_instance*>(user);
            if (sc_core::sc_get_status() != sc_core::SC_STOPPED) {
                self.advance_to_host_time();
                sc_core::sc_spawn_options options;
                options.spawn_method();
                sc_core::sc_spawn(&terminate_native_processes,
                    sc_core::sc_gen_unique_name("fsim_shutdown"), &options);
                sc_core::sc_start(sc_core::SC_ZERO_TIME);
                sc_core::sc_stop();
            }
        }

        template <typename T,
            sc_core::sc_writer_policy WriterPolicy = sc_core::SC_ONE_WRITER>
        bool register_signal(sc_core::sc_object& object,
            const fsim_sc_handle_v1 module_handle)
        {
            auto* signal =
                dynamic_cast<sc_core::sc_signal<T, WriterPolicy>*>(&object);
            if (signal == nullptr) {
                return false;
            }
            fsim_sc_handle_v1 handle { };
            const auto bytes = application_bridge_codec<T>::encode(signal->read());
            const fsim_sc_value_view_v1 initial { sizeof(fsim_sc_value_view_v1),
                application_bridge_codec<T>::encoding,
                application_bridge_codec<T>::width, bytes.data(), bytes.size() };
            if (host->register_signal(host->context, module_handle,
                    object.basename(), application_bridge_codec<T>::encoding,
                    application_bridge_codec<T>::width, &initial, &handle)
                != FSIM_SC_OK) {
                throw std::runtime_error {
                    "cannot register an Accellera internal signal"
                };
            }
            auto bridge =
                std::make_unique<
                    application_typed_signal_bridge<T, WriterPolicy>>(
                    host, *signal, handle);
            interface_bridges.emplace(signal, bridge.get());
            signals.push_back(std::move(bridge));
            interface_handles.emplace(signal, handle);
            return true;
        }

        template <typename T>
        bool register_export(sc_core::sc_object& object,
            const fsim_sc_handle_v1 module_handle)
        {
            const auto register_typed = [&](auto* export_object,
                                            const bool writable) {
                if (export_object == nullptr) {
                    return false;
                }
                fsim_sc_handle_v1 handle { };
                if (host->register_export(host->context, module_handle,
                        object.basename(),
                        application_bridge_codec<T>::encoding,
                        application_bridge_codec<T>::width, &handle)
                        != FSIM_SC_OK
                    || host->set_export_writable(host->context, handle,
                           writable ? 1U : 0U)
                        != FSIM_SC_OK) {
                    throw std::runtime_error {
                        "cannot register an Accellera application export"
                    };
                }
                const auto target =
                    interface_handles.find(export_object->get_interface());
                if (target != interface_handles.end()
                    && host->bind_export(host->context, handle,
                           target->second)
                        != FSIM_SC_OK) {
                    throw std::runtime_error {
                        "cannot bind an Accellera application export"
                    };
                }
                return true;
            };
            return register_typed(
                       dynamic_cast<sc_core::sc_export<
                           sc_core::sc_signal_in_if<T>>*>(&object),
                       false)
                || register_typed(
                    dynamic_cast<sc_core::sc_export<
                        sc_core::sc_signal_inout_if<T>>*>(&object),
                    true);
        }

        template <typename T>
        bool bind_port(sc_core::sc_port_base& base,
            sc_core::sc_module& owner,
            const fsim_sc_handle_v1 module_handle)
        {
            fsim_sc_port_direction_v1 direction;
            auto bind = [&](auto& port) {
                fsim_sc_handle_v1 handle { };
                if (host->register_port(host->context, module_handle,
                        base.basename(), direction,
                        application_bridge_codec<T>::encoding,
                        application_bridge_codec<T>::width, &handle)
                    != FSIM_SC_OK) {
                    throw std::runtime_error {
                        "cannot register an Accellera bridge port"
                    };
                }
                registered_ports.push_back(
                    {handle, module_handle, direction});
                port_handles[&owner].emplace(base.basename(), handle);
                if (base.get_interface() == nullptr) {
                    if (base.bind_count() != 0) {
                        const auto* parent = dynamic_cast<sc_core::sc_module*>(
                            owner.get_parent_object());
                        const auto parent_ports = port_handles.find(parent);
                        if (parent_ports != port_handles.end()) {
                            const auto parent_port
                                = parent_ports->second.find(base.basename());
                            if (parent_port != parent_ports->second.end()
                                && host->bind_port(host->context, handle,
                                       parent_port->second)
                                    == FSIM_SC_OK) {
                                return;
                            }
                        }
                    }
                    auto bridge
                        = std::make_unique<application_typed_port_bridge<T>>(
                            host, direction);
                    port.bind(bridge->signal());
                    bridge->handle = handle;
                    bridge->module = module_handle;
                    interface_handles.emplace(&bridge->signal(), handle);
                    ports.push_back(std::move(bridge));
                    return;
                }
                const auto parent = interface_handles.find(base.get_interface());
                if (parent == interface_handles.end()
                    || host->bind_port(host->context, handle, parent->second)
                        != FSIM_SC_OK) {
                    throw std::runtime_error {
                        "cannot bind an Accellera native child port"
                    };
                }
                if (const auto bridge =
                        interface_bridges.find(base.get_interface());
                    bridge != interface_bridges.end()) {
                    bridge->second->add_port_direction(direction);
                }
            };
            if (auto* port = dynamic_cast<sc_core::sc_in<T>*>(&base)) {
                direction = FSIM_SC_INPUT;
                bind(*port);
                return true;
            }
            if (auto* port = dynamic_cast<sc_core::sc_out<T>*>(&base)) {
                direction = FSIM_SC_OUTPUT;
                bind(*port);
                return true;
            }
            if (auto* port = dynamic_cast<sc_core::sc_inout<T>*>(&base)) {
                direction = FSIM_SC_INOUT;
                bind(*port);
                return true;
            }
            return false;
        }

        void connect_module(
            sc_core::sc_module& owner,
            const fsim_sc_handle_v1 module_handle)
        {
            for (auto* object : owner.get_child_objects()) {
                if (dynamic_cast<sc_core::sc_port_base*>(object) != nullptr
                    || dynamic_cast<sc_core::sc_module*>(object) != nullptr) {
                    continue;
                }
                const bool supported_signal
                    = register_signal<bool>(*object, module_handle)
                        || register_signal<sc_dt::sc_logic>(*object, module_handle)
                        || register_signal<sc_dt::sc_logic,
                               sc_core::SC_MANY_WRITERS>(*object, module_handle)
                        || register_signal<sc_dt::sc_bv<8>>(*object, module_handle)
                    || register_signal<sc_dt::sc_lv<8>>(*object, module_handle)
                    || register_signal<sc_dt::sc_lv<137>>(*object, module_handle)
                    || register_signal<sc_dt::sc_uint<8>>(*object, module_handle)
                    || register_signal<sc_dt::sc_int<8>>(*object, module_handle);
                (void)supported_signal;
            }
            for (auto* object : owner.get_child_objects()) {
                if (dynamic_cast<sc_core::sc_export_base*>(object) == nullptr) {
                    continue;
                }
                const bool supported
                    = register_export<bool>(*object, module_handle)
                    || register_export<sc_dt::sc_logic>(*object, module_handle)
                    || register_export<sc_dt::sc_bv<8>>(*object, module_handle)
                    || register_export<sc_dt::sc_lv<8>>(*object, module_handle)
                    || register_export<sc_dt::sc_lv<137>>(*object, module_handle)
                    || register_export<sc_dt::sc_uint<8>>(*object, module_handle)
                    || register_export<sc_dt::sc_int<8>>(*object, module_handle);
                (void)supported;
            }
            for (auto* object : owner.get_child_objects()) {
                auto* port = dynamic_cast<sc_core::sc_port_base*>(object);
                if (port == nullptr) {
                    continue;
                }
                const bool supported
                    = bind_port<bool>(*port, owner, module_handle)
                    || bind_port<sc_dt::sc_logic>(
                        *port, owner, module_handle)
                    || bind_port<sc_dt::sc_bv<8>>(
                        *port, owner, module_handle)
                    || bind_port<sc_dt::sc_lv<8>>(
                        *port, owner, module_handle)
                    || bind_port<sc_dt::sc_lv<137>>(
                        *port, owner, module_handle)
                    || bind_port<sc_dt::sc_uint<8>>(
                        *port, owner, module_handle)
                    || bind_port<sc_dt::sc_int<8>>(
                        *port, owner, module_handle);
                (void)supported;
            }
            for (auto* object : owner.get_child_objects()) {
                auto* child = dynamic_cast<sc_core::sc_module*>(object);
                if (child == nullptr) {
                    continue;
                }
                auto found = module_handles.find(child);
                fsim_sc_handle_v1 child_handle { };
                if (found == module_handles.end()) {
                    if (host->register_native_module(host->context,
                            module_handle, child->basename(), &child_handle)
                        != FSIM_SC_OK) {
                        throw std::runtime_error {
                            "cannot register an Accellera native module"
                        };
                    }
                    module_handles.emplace(child, child_handle);
                } else {
                    child_handle = found->second;
                }
                connect_module(*child, child_handle);
            }
        }

        void connect(const fsim_sc_handle_v1 module_handle)
        {
            root_module = module_handle;
            module_handles.emplace(module.get(), module_handle);
            connect_module(*module, module_handle);
            if (host->register_lifecycle(host->context, module_handle,
                    lifecycle_noop, lifecycle_noop, lifecycle_noop,
                    end_simulation, this)
                != FSIM_SC_OK) {
                throw std::runtime_error {
                    "cannot register the Accellera application lifecycle"
                };
            }
            fsim_sc_handle_v1 process { };
            if (host->register_process(host->context, module_handle,
                    "$accellera_kernel", execute, this,
                    &process)
                != FSIM_SC_OK) {
                throw std::runtime_error {
                    "cannot register the Accellera application kernel bridge"
                };
            }
            for (const auto& port : registered_ports) {
                if (port.module == module_handle
                    && port.direction != FSIM_SC_OUTPUT
                    && host->add_sensitivity(host->context, process,
                           port.handle, FSIM_SC_ANY_EDGE)
                        != FSIM_SC_OK) {
                    throw std::runtime_error {
                        "cannot register Accellera bridge sensitivity"
                    };
                }
            }
        }
    };

} // namespace detail

template <typename T>
[[nodiscard]] T construction_value(const char* name)
{
    static_assert(
        std::is_integral_v<T>,
        "SystemC construction values currently support integral types");
    const auto context = detail::active_factory_context;
    if (context.host == nullptr || context.module == 0 || name == nullptr
        || *name == '\0'
        || context.host->get_construction_value == nullptr) {
        throw std::logic_error {
            "construction_value requires an active typed fsim factory"
        };
    }
    std::int64_t value { };
    const auto status = context.host->get_construction_value(
        context.host->context, context.module, name, &value);
    if (status != FSIM_SC_OK) {
        throw std::runtime_error { "cannot read SystemC construction value" };
    }
    if constexpr (std::is_same_v<T, bool>) {
        if (value != 0 && value != 1) {
            throw std::out_of_range {
                "Boolean construction value is not 0 or 1"
            };
        }
    } else if constexpr (std::is_signed_v<T>) {
        if (value < static_cast<std::int64_t>(std::numeric_limits<T>::min())
            || value
                > static_cast<std::int64_t>(
                    std::numeric_limits<T>::max())) {
            throw std::out_of_range {
                "SystemC construction value does not fit target type"
            };
        }
    } else if (
        value < 0
        || static_cast<std::uint64_t>(value)
            > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
        throw std::out_of_range {
            "SystemC construction value does not fit target type"
        };
    }
    return static_cast<T>(value);
}

template <typename Module>
struct module_factory_state {
    static fsim_sc_status_v1 elaborate(
        void* user,
        const char* instance_name,
        const fsim_sc_handle_v1 module,
        fsim_sc_handle_v1,
        void** result) noexcept
    {
        static_assert(
            std::is_base_of_v<sc_core::sc_module, Module>,
            "registered SystemC module must derive from sc_module");
        const auto* host = static_cast<const fsim_sc_host_v1*>(user);
        if (host == nullptr || instance_name == nullptr
            || *instance_name == '\0' || result == nullptr) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        try {
            auto object = std::make_unique<detail::application_factory_instance<Module>>();
            object->host = host;
            detail::factory_scope scope {
                host, module, &object->module_handles
            };
            object->module = std::make_unique<Module>(
                sc_core::sc_module_name { instance_name });
            object->connect(module);
            *result = object.release();
            return FSIM_SC_OK;
        } catch (const std::exception& exception) {
            detail::report_factory_failure(host, exception.what());
            return FSIM_SC_RUNTIME_ERROR;
        } catch (...) {
            detail::report_factory_failure(
                host, "unknown SystemC module-construction failure");
            return FSIM_SC_RUNTIME_ERROR;
        }
    }

    static void destroy(void*, void* module) noexcept
    {
        auto* object
            = static_cast<detail::application_factory_instance<Module>*>(module);
        object->module.reset();
        delete object;
    }
};

template <typename Module>
[[nodiscard]] fsim_sc_status_v1 register_module_factory(
    const fsim_sc_host_v1* host,
    fsim_sc_registrar_v1* registrar,
    const char* name,
    const std::span<const factory_parameter> parameters) noexcept
{
    if (host == nullptr || registrar == nullptr || name == nullptr
        || *name == '\0'
        || host->abi_version != FSIM_SYSTEMC_ABI_VERSION
        || registrar->abi_version != FSIM_SYSTEMC_ABI_VERSION
        || host->struct_size < sizeof(fsim_sc_host_v1)
        || registrar->struct_size < sizeof(fsim_sc_registrar_v1)
        || registrar->register_elaboration_factory == nullptr
        || registrar->register_factory_parameter == nullptr) {
        return FSIM_SC_ABI_MISMATCH;
    }
    auto status = registrar->register_elaboration_factory(
        registrar->context,
        name,
        module_factory_state<Module>::elaborate,
        module_factory_state<Module>::destroy,
        const_cast<fsim_sc_host_v1*>(host));
    for (const auto& parameter : parameters) {
        if (status != FSIM_SC_OK) {
            break;
        }
        status = registrar->register_factory_parameter(
            registrar->context,
            name,
            parameter.name,
            parameter.type,
            parameter.has_default ? 1 : 0,
            parameter.default_value);
    }
    return status;
}

template <typename Module>
[[nodiscard]] fsim_sc_status_v1 register_module_factory(
    const fsim_sc_host_v1* host,
    fsim_sc_registrar_v1* registrar,
    const char* name) noexcept
{
    return register_module_factory<Module>(host, registrar, name, { });
}

namespace detail {

    struct export_descriptor {
        const char* public_name { };
        fsim_sc_status_v1 (*register_export)(
            const fsim_sc_host_v1*,
            fsim_sc_registrar_v1*,
            const char*) noexcept { };
        export_descriptor* next { };
    };

    void add_export_descriptor(export_descriptor* descriptor) noexcept;

    template <typename Module>
    struct export_registration final {
        explicit export_registration(const char* public_name) noexcept
            : descriptor_ { public_name, register_export, nullptr }
        {
            add_export_descriptor(&descriptor_);
        }

    private:
        static fsim_sc_status_v1 register_export(
            const fsim_sc_host_v1* host,
            fsim_sc_registrar_v1* registrar,
            const char* public_name) noexcept
        {
            if constexpr (requires { Module::fsim_factory_parameters; }) {
                const auto& parameters = Module::fsim_factory_parameters;
                return register_module_factory<Module>(
                    host,
                    registrar,
                    public_name,
                    std::span<const factory_parameter> { parameters });
            } else {
                return register_module_factory<Module>(
                    host, registrar, public_name, { });
            }
        }

        export_descriptor descriptor_;
    };

} // namespace detail

} // namespace fsim::systemc

#define FSIM_SC_DETAIL_CONCAT_INNER(left, right) left##right
#define FSIM_SC_DETAIL_CONCAT(left, right) \
    FSIM_SC_DETAIL_CONCAT_INNER(left, right)
#define FSIM_SC_DETAIL_EXPORT(type, public_name, identifier)                      \
    namespace {                                                                   \
        [[maybe_unused]] const ::fsim::systemc::detail::export_registration<type> \
            FSIM_SC_DETAIL_CONCAT(                                                \
                fsim_sc_export_registration_, identifier) { public_name };        \
    }
#define SC_FSIM_EXPORT_AS(type, public_name) \
    FSIM_SC_DETAIL_EXPORT(type, public_name, __LINE__)
#define SC_FSIM_EXPORT(type) SC_FSIM_EXPORT_AS(type, #type)
