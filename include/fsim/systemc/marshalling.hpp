// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/systemc/datatypes.hpp"

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

fsim_sc_handle_v1 register_event(const char* name);

fsim_sc_handle_v1 register_primitive_channel(
    const char* name,
    const fsim_sc_channel_update_v1 update,
    void* user);

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

bool object_event(const fsim_sc_handle_v1 object);

void bind_port(
    const fsim_sc_handle_v1 port,
    const fsim_sc_handle_v1 channel);

void bind_export(
    const fsim_sc_handle_v1 export_handle,
    const fsim_sc_handle_v1 target);

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

