// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace fsim::support {

// Which elaboration step made an object that no declaration of the source
// stands for: a signal, process, container, string or event the elaborator
// adds to lower a construct. `none` is an object of the source. Consumers
// that list a design's declared objects (VPI, traces, the debugger) skip
// every object whose origin is not `none`; consumers that must find one
// kind of generated object test for that kind.
enum class GeneratedObjectOrigin : std::uint8_t {
    none,
    // The value of a `$sampled(expr)` or of a $monitor / $strobe argument.
    monitored_value,
    // The driver process of a $monitor / $strobe argument.
    monitor_driver,
    // A user-defined primitive's value, previous-input and initialization
    // signals; the process that evaluates its table, and its input and
    // output processes for an array of terminals.
    udp_signal,
    udp_table_process,
    udp_input_process,
    udp_output_process,
    // The monitor of a toggle or finite-state-machine coverage point.
    toggle_coverage,
    fsm_coverage,
    // The pieces of an annotated interconnect delay: the delayed copy of a
    // net, the process that drives the copy, the source the copy reads, and
    // a process cloned for a copy.
    mipd_copy,
    mipd_driver,
    mipd_source,
    mipd_clone,
    // The control signal of a switch whose conduction is annotated.
    switch_control,
    // A port's default driver, and the adapters between a port formal and
    // its actual.
    port_default_driver,
    actual_adapter,
    inout_adapter,
    array_element_output_adapter,
    alias_adapter,
    // The driver and the value process of an implicit net.
    implicit_driver,
    implicit_value,
    // A clocking block's event.
    clocking_event,
    // A generic of a package instance bound to an actual.
    generic_actual,
    // A signal of the implied block of a VHDL port association.
    local_signal,
    // A VHDL source of a wire made for VITAL timing.
    vital_source,
    // The owner of a procedural assign or force.
    procedural_assign_owner,
    force_owner,
    // The storage of a class's static container, and of a static local of
    // a method.
    class_static,
    class_static_local,
    // An event made to notify one use of a construct, and the change signal
    // of a string object.
    local_event,
    string_change,
    // The process that drives a formal from its actual, and the one that
    // drives an actual from its formal, of a port association.
    port_input_adapter,
    port_output_adapter,
    // The delayed copy of a clocking block's input (the input skew), and
    // the flag that marks a synchronous drive as pending.
    clocking_skew,
    clocking_pending,
    // The process that drives a procedural continuous assignment (IEEE
    // 1800-2023 10.6.1); VPI publishes it under its generated name, hidden
    // from iteration.
    procedural_assign_driver,
};

// Whether the objects of this origin are named with a leading `$` and are
// left out of a scope's children by the VPI (IEEE 1800-2023 37.12); every
// other generated object keeps the source-like name it was made with.
[[nodiscard]] constexpr bool vpi_hidden_origin(
    const GeneratedObjectOrigin origin) noexcept
{
    switch (origin) {
    case GeneratedObjectOrigin::monitored_value:
    case GeneratedObjectOrigin::udp_signal:
    case GeneratedObjectOrigin::mipd_copy:
    case GeneratedObjectOrigin::mipd_source:
    case GeneratedObjectOrigin::switch_control:
    case GeneratedObjectOrigin::actual_adapter:
    case GeneratedObjectOrigin::clocking_event:
    case GeneratedObjectOrigin::clocking_skew:
    case GeneratedObjectOrigin::generic_actual:
    case GeneratedObjectOrigin::local_signal:
    case GeneratedObjectOrigin::vital_source:
    case GeneratedObjectOrigin::procedural_assign_owner:
    case GeneratedObjectOrigin::force_owner:
    case GeneratedObjectOrigin::class_static:
    case GeneratedObjectOrigin::class_static_local:
    case GeneratedObjectOrigin::local_event:
        return true;
    case GeneratedObjectOrigin::none:
    case GeneratedObjectOrigin::monitor_driver:
    case GeneratedObjectOrigin::udp_table_process:
    case GeneratedObjectOrigin::udp_input_process:
    case GeneratedObjectOrigin::udp_output_process:
    case GeneratedObjectOrigin::toggle_coverage:
    case GeneratedObjectOrigin::fsm_coverage:
    case GeneratedObjectOrigin::mipd_driver:
    case GeneratedObjectOrigin::mipd_clone:
    case GeneratedObjectOrigin::port_default_driver:
    case GeneratedObjectOrigin::inout_adapter:
    case GeneratedObjectOrigin::array_element_output_adapter:
    case GeneratedObjectOrigin::alias_adapter:
    case GeneratedObjectOrigin::implicit_driver:
    case GeneratedObjectOrigin::implicit_value:
    case GeneratedObjectOrigin::string_change:
    case GeneratedObjectOrigin::port_input_adapter:
    case GeneratedObjectOrigin::port_output_adapter:
    case GeneratedObjectOrigin::clocking_pending:
    case GeneratedObjectOrigin::procedural_assign_driver:
        return false;
    }
    return false;
}

} // namespace fsim::support
