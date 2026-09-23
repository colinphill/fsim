// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/class_heap.hpp"

#include <string>
#include <string_view>

namespace fsim::tests::runtime::test_support {

[[nodiscard]] inline fsim::runtime::SystemVerilogClassDescriptor
make_sequence_class_descriptor(const std::string_view specialization,
    const std::string_view sequencer_type,
    const std::string_view sequence_type)
{
    fsim::runtime::SystemVerilogClassDescriptor result;
    result.dynamic_type = std::string { specialization };
    result.specialization_identity = std::string { specialization };
    if (specialization == sequencer_type) {
        result.declared_type = "uvm_pkg::uvm_sequencer";
        result.assignable_declared_types = {
            std::string { sequencer_type }, "uvm_pkg::uvm_sequencer",
            "uvm_pkg::uvm_component", "uvm_pkg::uvm_object"
        };
    } else if (specialization == sequence_type) {
        result.declared_type = "uvm_pkg::uvm_sequence";
        result.assignable_declared_types = {
            std::string { sequence_type }, "uvm_pkg::uvm_sequence",
            "uvm_pkg::uvm_sequence_item", "uvm_pkg::uvm_object"
        };
    } else {
        result.declared_type = "uvm_pkg::uvm_sequence_item";
        result.assignable_declared_types = {
            std::string { specialization }, "uvm_pkg::uvm_sequence_item",
            "uvm_pkg::uvm_object"
        };
    }
    return result;
}

} // namespace fsim::tests::runtime::test_support
