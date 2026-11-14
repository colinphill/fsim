// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include "fsim/support/uvm_role.hpp"

namespace fsim::support {

/// The function and task methods of the UVM library's uvm_component the
/// simulation's phase services call (IEEE 1800.2-2020 13.1.3, 13.1.4): the
/// phase callbacks and the phase hooks, by their recorded roles. A dispatch
/// request names one by its position here plus one; zero is no request. The
/// generated dispatch process (the lowerer) and the host request (the
/// simulation) read this one table.
inline constexpr std::array<UvmRole, 24U> uvm_api_function_methods {
    UvmRole::phase_started,
    UvmRole::phase_ready_to_end,
    UvmRole::phase_ended,
    UvmRole::build_phase,
    UvmRole::connect_phase,
    UvmRole::end_of_elaboration_phase,
    UvmRole::start_of_simulation_phase,
    UvmRole::extract_phase,
    UvmRole::check_phase,
    UvmRole::report_phase,
    UvmRole::final_phase,
    UvmRole::run_phase,
    UvmRole::pre_reset_phase,
    UvmRole::reset_phase,
    UvmRole::post_reset_phase,
    UvmRole::pre_configure_phase,
    UvmRole::configure_phase,
    UvmRole::post_configure_phase,
    UvmRole::pre_main_phase,
    UvmRole::main_phase,
    UvmRole::post_main_phase,
    UvmRole::pre_shutdown_phase,
    UvmRole::shutdown_phase,
    UvmRole::post_shutdown_phase,
};

/// The request kind that runs the constructor chain of a component the
/// simulation allocated without constructing it (the lowered `new`, IEEE
/// 1800.2-2020 13.1.2): the kind after the methods'.
inline constexpr std::uint32_t uvm_api_construct_kind
    = static_cast<std::uint32_t>(uvm_api_function_methods.size()) + 1U;

/// Whether a library method with role `role` is one of the factory methods
/// (IEEE 1800.2-2020 8.3.1) the simulation serves by allocating an object of
/// a user class without constructing it: a design process that calls one
/// runs the constructor chain of the object it returns as generated code.
[[nodiscard]] constexpr bool uvm_factory_creates(const UvmRole role) noexcept
{
    return role == UvmRole::factory_create_object_by_type
        || role == UvmRole::factory_create_component_by_type
        || role == UvmRole::factory_create_object_by_name
        || role == UvmRole::factory_create_component_by_name;
}

/// The request kind of a library method role, if it is one of the table.
[[nodiscard]] constexpr std::optional<std::uint32_t> uvm_api_request_kind(
    const UvmRole method) noexcept
{
    for (std::uint32_t index {}; index < uvm_api_function_methods.size();
        ++index) {
        if (uvm_api_function_methods[index] == method) {
            return index + 1U;
        }
    }
    return std::nullopt;
}

} // namespace fsim::support
