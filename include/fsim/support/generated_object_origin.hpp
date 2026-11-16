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
};

} // namespace fsim::support
