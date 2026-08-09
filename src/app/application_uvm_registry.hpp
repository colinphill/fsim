// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/packed_value.hpp"

#include <span>
#include <optional>
#include <string_view>

namespace fsim::frontend {
struct SystemVerilogClassSpecialization;
}

namespace fsim::runtime {
class SystemVerilogClassHeap;
class SystemVerilogUvmFactoryService;
class SystemVerilogUvmObjectService;
class SystemVerilogUvmRegistryService;
}

namespace fsim::app::application_detail {

[[nodiscard]] bool is_systemverilog_uvm_type(
    std::span<const frontend::SystemVerilogClassSpecialization> specializations,
    const frontend::SystemVerilogClassSpecialization& specialization,
    std::string_view base_name);

void register_systemverilog_uvm_object_types(
    std::span<const frontend::SystemVerilogClassSpecialization> specializations,
    runtime::SystemVerilogUvmObjectService& objects);

void ensure_systemverilog_uvm_object_type(
    const frontend::SystemVerilogClassSpecialization& specialization,
    runtime::SystemVerilogUvmObjectService& objects);

void register_systemverilog_uvm_registry_types(
    std::span<const frontend::SystemVerilogClassSpecialization> specializations,
    runtime::SystemVerilogUvmRegistryService& registry);

[[nodiscard]] std::optional<runtime::PackedLogic4>
invoke_systemverilog_uvm_factory_method(
    std::span<const frontend::SystemVerilogClassSpecialization> specializations,
    const runtime::SystemVerilogClassHeap& heap,
    runtime::SystemVerilogUvmFactoryService& factory,
    std::uint64_t receiver,
    std::string_view canonical_identity,
    std::span<const runtime::PackedLogic4> actuals);

}
