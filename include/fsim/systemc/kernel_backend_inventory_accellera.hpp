// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/systemc/kernel_backend_inventory.hpp"

#include <systemc>

#include <cstdint>
#include <string>
#include <type_traits>

namespace fsim::systemc {
namespace detail {

    template <typename T>
    struct AccelleraValueProfile {
        static constexpr bool supported = std::is_integral_v<T>;
        static constexpr SystemCKernelValueKind kind
            = SystemCKernelValueKind::bit2;
        static constexpr std::uint32_t width
            = static_cast<std::uint32_t>(sizeof(T) * 8U);
        static constexpr bool is_signed = std::is_signed_v<T>;
        static constexpr const char* name = "integral";
    };

    template <>
    struct AccelleraValueProfile<bool> {
        static constexpr bool supported = true;
        static constexpr SystemCKernelValueKind kind
            = SystemCKernelValueKind::bit2;
        static constexpr std::uint32_t width = 1U;
        static constexpr bool is_signed = false;
        static constexpr const char* name = "bool";
    };

    template <>
    struct AccelleraValueProfile<sc_dt::sc_logic> {
        static constexpr bool supported = true;
        static constexpr SystemCKernelValueKind kind
            = SystemCKernelValueKind::logic4;
        static constexpr std::uint32_t width = 1U;
        static constexpr bool is_signed = false;
        static constexpr const char* name = "sc_logic";
    };

    template <>
    struct AccelleraValueProfile<sc_dt::sc_bit> {
        static constexpr bool supported = true;
        static constexpr SystemCKernelValueKind kind
            = SystemCKernelValueKind::bit2;
        static constexpr std::uint32_t width = 1U;
        static constexpr bool is_signed = false;
        static constexpr const char* name = "sc_bit";
    };

    template <int Width>
    struct AccelleraValueProfile<sc_dt::sc_int<Width>> {
        static constexpr bool supported = true;
        static constexpr SystemCKernelValueKind kind
            = SystemCKernelValueKind::bit2;
        static constexpr std::uint32_t width = static_cast<std::uint32_t>(Width);
        static constexpr bool is_signed = true;
        static constexpr const char* name = "sc_int";
    };

    template <int Width>
    struct AccelleraValueProfile<sc_dt::sc_uint<Width>> {
        static constexpr bool supported = true;
        static constexpr SystemCKernelValueKind kind
            = SystemCKernelValueKind::bit2;
        static constexpr std::uint32_t width = static_cast<std::uint32_t>(Width);
        static constexpr bool is_signed = false;
        static constexpr const char* name = "sc_uint";
    };

    template <int Width>
    struct AccelleraValueProfile<sc_dt::sc_bigint<Width>> {
        static constexpr bool supported = true;
        static constexpr SystemCKernelValueKind kind
            = SystemCKernelValueKind::bit2;
        static constexpr std::uint32_t width = static_cast<std::uint32_t>(Width);
        static constexpr bool is_signed = true;
        static constexpr const char* name = "sc_bigint";
    };

    template <int Width>
    struct AccelleraValueProfile<sc_dt::sc_biguint<Width>> {
        static constexpr bool supported = true;
        static constexpr SystemCKernelValueKind kind
            = SystemCKernelValueKind::bit2;
        static constexpr std::uint32_t width = static_cast<std::uint32_t>(Width);
        static constexpr bool is_signed = false;
        static constexpr const char* name = "sc_biguint";
    };

    template <int Width>
    struct AccelleraValueProfile<sc_dt::sc_bv<Width>> {
        static constexpr bool supported = true;
        static constexpr SystemCKernelValueKind kind
            = SystemCKernelValueKind::bit2;
        static constexpr std::uint32_t width = static_cast<std::uint32_t>(Width);
        static constexpr bool is_signed = false;
        static constexpr const char* name = "sc_bv";
    };

    template <int Width>
    struct AccelleraValueProfile<sc_dt::sc_lv<Width>> {
        static constexpr bool supported = true;
        static constexpr SystemCKernelValueKind kind
            = SystemCKernelValueKind::logic4;
        static constexpr std::uint32_t width = static_cast<std::uint32_t>(Width);
        static constexpr bool is_signed = false;
        static constexpr const char* name = "sc_lv";
    };

    constexpr SystemCKernelWriterPolicy writer_policy(
        const sc_core::sc_writer_policy policy) noexcept
    {
        switch (policy) {
        case sc_core::SC_ONE_WRITER:
            return SystemCKernelWriterPolicy::one;
        case sc_core::SC_MANY_WRITERS:
            return SystemCKernelWriterPolicy::many;
        case sc_core::SC_UNCHECKED_WRITERS:
            return SystemCKernelWriterPolicy::unchecked;
        }
        return SystemCKernelWriterPolicy::none;
    }

    template <typename T, sc_core::sc_writer_policy Policy>
    SystemCKernelChannelDescriptor signal_descriptor(
        const sc_core::sc_signal<T, Policy>& channel,
        const SystemCKernelChannelKind kind,
        const SystemCKernelObservationMode observation)
    {
        using Profile = AccelleraValueProfile<T>;
        SystemCKernelChannelDescriptor descriptor;
        descriptor.canonical_path = channel.name();
        descriptor.type_name = std::string { channel.kind() } + ":"
            + Profile::name + ":" + std::to_string(Profile::width);
        descriptor.kind = kind;
        descriptor.writer = writer_policy(Policy);
        descriptor.update_owner = SystemCKernelUpdateOwner::signal_kernel;
        descriptor.observation = observation;
        descriptor.supported = Profile::supported;
        if constexpr (Profile::supported) {
            descriptor.value = SystemCKernelChannelValueProfile {
                Profile::kind, Profile::width, Profile::is_signed
            };
        }
        return descriptor;
    }

} // namespace detail

template <typename T, sc_core::sc_writer_policy Policy>
SystemCKernelChannelDescriptor describe_accellera_channel(
    const sc_core::sc_signal<T, Policy>& channel)
{
    return detail::signal_descriptor(
        channel, SystemCKernelChannelKind::signal,
        SystemCKernelObservationMode::value_changed);
}

template <typename T, sc_core::sc_writer_policy Policy>
SystemCKernelChannelDescriptor describe_accellera_channel(
    const sc_core::sc_buffer<T, Policy>& channel)
{
    return detail::signal_descriptor(
        channel, SystemCKernelChannelKind::buffer,
        SystemCKernelObservationMode::value_changed);
}

inline SystemCKernelChannelDescriptor describe_accellera_channel(
    const sc_core::sc_clock& channel)
{
    auto descriptor = detail::signal_descriptor(
        channel, SystemCKernelChannelKind::clock,
        SystemCKernelObservationMode::clock_edges);
    descriptor.type_name = "sc_clock:bool:1";
    return descriptor;
}

inline SystemCKernelChannelDescriptor describe_accellera_channel(
    const sc_core::sc_signal_resolved& channel)
{
    return detail::signal_descriptor(
        channel, SystemCKernelChannelKind::resolved_signal,
        SystemCKernelObservationMode::value_changed);
}

template <int Width>
SystemCKernelChannelDescriptor describe_accellera_channel(
    const sc_core::sc_signal_rv<Width>& channel)
{
    return detail::signal_descriptor(
        channel, SystemCKernelChannelKind::resolved_vector,
        SystemCKernelObservationMode::value_changed);
}

inline SystemCKernelChannelDescriptor describe_accellera_channel(
    const sc_core::sc_mutex& channel)
{
    return { channel.name(), "sc_mutex", SystemCKernelChannelKind::mutex,
        std::nullopt, SystemCKernelWriterPolicy::none,
        SystemCKernelUpdateOwner::primitive_kernel,
        SystemCKernelObservationMode::primitive_events, true };
}

inline SystemCKernelChannelDescriptor describe_accellera_channel(
    const sc_core::sc_semaphore& channel)
{
    return { channel.name(), "sc_semaphore",
        SystemCKernelChannelKind::semaphore, std::nullopt,
        SystemCKernelWriterPolicy::none,
        SystemCKernelUpdateOwner::primitive_kernel,
        SystemCKernelObservationMode::primitive_events, true };
}

inline SystemCKernelChannelDescriptor describe_accellera_channel(
    const sc_core::sc_event_queue& channel)
{
    return { channel.name(), "sc_event_queue",
        SystemCKernelChannelKind::event_queue, std::nullopt,
        SystemCKernelWriterPolicy::none,
        SystemCKernelUpdateOwner::primitive_kernel,
        SystemCKernelObservationMode::primitive_events, true };
}

inline SystemCKernelChannelDescriptor describe_accellera_channel(
    const sc_core::sc_prim_channel& channel)
{
    return { channel.name(), channel.kind(), SystemCKernelChannelKind::custom,
        std::nullopt, SystemCKernelWriterPolicy::none,
        SystemCKernelUpdateOwner::custom,
        SystemCKernelObservationMode::unsupported, false };
}

} // namespace fsim::systemc
