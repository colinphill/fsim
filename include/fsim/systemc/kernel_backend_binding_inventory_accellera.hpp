// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/systemc/kernel_backend_binding_inventory.hpp"

#include <systemc>

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::systemc {
namespace detail {

    inline SystemCKernelBindingTarget accellera_target(
        const sc_core::sc_prim_channel& channel,
        const std::span<const std::string_view> intermediate)
    {
        SystemCKernelBindingTarget target;
        target.chain.reserve(intermediate.size() + 1U);
        for (const auto path : intermediate) {
            target.chain.emplace_back(path);
        }
        target.final_channel_path = channel.name();
        target.chain.push_back(target.final_channel_path);
        return target;
    }

    template <typename Port>
    SystemCKernelBindingDescriptor accellera_port(const Port& port,
        const SystemCKernelBindingDirection direction,
        std::vector<SystemCKernelBindingTarget> targets)
    {
        SystemCKernelBindingDescriptor descriptor;
        descriptor.declared_path = port.name();
        descriptor.interface_name = port.kind();
        descriptor.kind = SystemCKernelBindingKind::port;
        descriptor.direction = direction;
        descriptor.targets = std::move(targets);
        for (auto& target : descriptor.targets) {
            target.chain.insert(target.chain.begin(), descriptor.declared_path);
        }
        return descriptor;
    }

} // namespace detail

inline SystemCKernelBindingTarget make_accellera_binding_target(
    const sc_core::sc_prim_channel& channel,
    const std::span<const std::string_view> intermediate = { })
{
    return detail::accellera_target(channel, intermediate);
}

template <typename T>
SystemCKernelBindingDescriptor describe_accellera_binding(
    const sc_core::sc_in<T>& port,
    std::vector<SystemCKernelBindingTarget> targets)
{
    return detail::accellera_port(
        port, SystemCKernelBindingDirection::input, std::move(targets));
}

template <typename T>
SystemCKernelBindingDescriptor describe_accellera_binding(
    const sc_core::sc_out<T>& port,
    std::vector<SystemCKernelBindingTarget> targets)
{
    return detail::accellera_port(
        port, SystemCKernelBindingDirection::output, std::move(targets));
}

template <typename T>
SystemCKernelBindingDescriptor describe_accellera_binding(
    const sc_core::sc_inout<T>& port,
    std::vector<SystemCKernelBindingTarget> targets)
{
    return detail::accellera_port(
        port, SystemCKernelBindingDirection::inout, std::move(targets));
}

template <typename Interface, int Maximum, sc_core::sc_port_policy Policy>
SystemCKernelBindingDescriptor describe_accellera_binding(
    const sc_core::sc_port<Interface, Maximum, Policy>& port,
    const SystemCKernelBindingDirection direction,
    std::vector<SystemCKernelBindingTarget> targets)
{
    return detail::accellera_port(port, direction, std::move(targets));
}

template <typename Interface>
SystemCKernelBindingDescriptor describe_accellera_binding(
    const sc_core::sc_export<Interface>& export_interface,
    std::vector<SystemCKernelBindingTarget> targets)
{
    SystemCKernelBindingDescriptor descriptor;
    descriptor.declared_path = export_interface.name();
    descriptor.interface_name = export_interface.kind();
    descriptor.kind = SystemCKernelBindingKind::export_interface;
    descriptor.targets = std::move(targets);
    for (auto& target : descriptor.targets) {
        target.chain.insert(target.chain.begin(), descriptor.declared_path);
    }
    return descriptor;
}

} // namespace fsim::systemc
